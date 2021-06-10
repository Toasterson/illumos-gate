/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/fasttrap_isa.h>
#include <sys/fasttrap_impl.h>
#include <sys/dtrace.h>
#include <sys/dtrace_impl.h>
#include <sys/cmn_err.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/sysmacros.h>
#include <sys/trap.h>
#include <sys/archsystm.h>
#include <sys/fp.h>
#include <stdbool.h>
#include <sys/promif.h>
#include <sys/sbi.h>

static ulong_t fasttrap_getreg(const struct regs *, uint_t);
static void fasttrap_setreg(struct regs *, uint_t, ulong_t);

static uint64_t
fasttrap_anarg(struct regs *rp, int argno)
{
	uint64_t value;

	uintptr_t *stack;

	if (argno < 8)
		return fasttrap_getreg(rp, argno + 10);

	stack = (uintptr_t *)rp->r_sp;
	DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
	value = dtrace_fulword(&stack[argno - 8]);
	DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT | CPU_DTRACE_BADADDR);

	return (value);
}

/*ARGSUSED*/
int
fasttrap_tracepoint_init(proc_t *p, fasttrap_tracepoint_t *tp, uintptr_t pc,
    fasttrap_probe_type_t type)
{
	uint32_t instr;

	for (;;) {
		uint16_t instr16;
		instr = 0;
		if (uread(p, &instr16, sizeof(instr16), pc) != 0)
			return (-1);
		instr |= instr16;
		if ((instr & 3) == 3) {
			if (uread(p, &instr16, sizeof(instr16), pc + sizeof(instr16)) != 0)
				return (-1);
			instr |= (instr16 << 16);

			if (uread(p, &instr16, sizeof(instr16), pc) != 0)
				return (-1);
			if ((instr & 0xffff) != instr16)
				continue;
		}
		break;
	}

	tp->ftt_instr = instr;

	if ((instr & 0x7f) == 0x6f)
		tp->ftt_type = FASTTRAP_T_JAL;
	else if ((instr & 0x707f) == 0x67)
		tp->ftt_type = FASTTRAP_T_JALR;
	else if ((instr & 0x707f) == 0x0063)
		tp->ftt_type = FASTTRAP_T_BEQ;
	else if ((instr & 0x707f) == 0x1063)
		tp->ftt_type = FASTTRAP_T_BNE;
	else if ((instr & 0x707f) == 0x4063)
		tp->ftt_type = FASTTRAP_T_BLT;
	else if ((instr & 0x707f) == 0x5063)
		tp->ftt_type = FASTTRAP_T_BGE;
	else if ((instr & 0x707f) == 0x6063)
		tp->ftt_type = FASTTRAP_T_BLTU;
	else if ((instr & 0x707f) == 0x7063)
		tp->ftt_type = FASTTRAP_T_BGEU;
	else if ((instr & 0xe003) == 0xa001)
		tp->ftt_type = FASTTRAP_T_CJ;
	else if ((instr & 0xe07f) == 0x8002 && (instr & 0xf80) != 0)
		tp->ftt_type = FASTTRAP_T_CJR;
	else if ((instr & 0xe003) == 0xc001)
		tp->ftt_type = FASTTRAP_T_CBEQZ;
	else if ((instr & 0xe003) == 0xe001)
		tp->ftt_type = FASTTRAP_T_CBNEZ;
	else if ((instr & 0x7f) == 0x17)
		tp->ftt_type = FASTTRAP_T_AUIPC;
	else
		tp->ftt_type = FASTTRAP_T_COMMON;

	return (0);
}

int
fasttrap_tracepoint_install(proc_t *p, fasttrap_tracepoint_t *tp)
{
	fasttrap_instr_t instr = FASTTRAP_INSTR;

	if (uwrite(p, &instr, sizeof(instr), tp->ftt_pc) != 0)
		return (-1);

	return (0);
}

int
fasttrap_tracepoint_remove(proc_t *p, fasttrap_tracepoint_t *tp)
{
	fasttrap_instr_t instr;

	/*
	 * Distinguish between read or write failures and a changed
	 * instruction.
	 */
	if (uread(p, &instr, sizeof(instr), tp->ftt_pc) != 0)
		return (0);
	if (instr != FASTTRAP_INSTR)
		return (0);
	instr = tp->ftt_instr & 0xffff;
	if (uwrite(p, &instr, sizeof(instr), tp->ftt_pc) != 0)
		return (-1);

	return (0);
}

static void
fasttrap_return_common(struct regs *rp, uintptr_t pc, pid_t pid,
    uintptr_t new_pc)
{
	fasttrap_tracepoint_t *tp;
	fasttrap_bucket_t *bucket;
	fasttrap_id_t *id;
	kmutex_t *pid_mtx;

	pid_mtx = &cpu_core[CPU->cpu_id].cpuc_pid_lock;
	mutex_enter(pid_mtx);
	bucket = &fasttrap_tpoints.fth_table[FASTTRAP_TPOINTS_INDEX(pid, pc)];

	for (tp = bucket->ftb_data; tp != NULL; tp = tp->ftt_next) {
		if (pid == tp->ftt_pid && pc == tp->ftt_pc &&
		    tp->ftt_proc->ftpc_acount != 0)
			break;
	}

	/*
	 * Don't sweat it if we can't find the tracepoint again; unlike
	 * when we're in fasttrap_pid_probe(), finding the tracepoint here
	 * is not essential to the correct execution of the process.
	 */
	if (tp == NULL) {
		mutex_exit(pid_mtx);
		return;
	}

	for (id = tp->ftt_retids; id != NULL; id = id->fti_next) {
		/*
		 * If there's a branch that could act as a return site, we
		 * need to trace it, and check here if the program counter is
		 * external to the function.
		 */
		if (new_pc - id->fti_probe->ftp_faddr < id->fti_probe->ftp_fsize) {
			bool is_ret = false;
			if (tp->ftt_type == FASTTRAP_T_JALR) {
				if (((tp->ftt_instr >> 15) & 0x1f) == 1)
					is_ret = true;
			} else if (tp->ftt_type == FASTTRAP_T_CJR) {
				if (((tp->ftt_instr >> 7) & 0x1f) == 1)
					is_ret = true;
			}
			if (!is_ret)
				continue;
		}

		dtrace_probe(id->fti_probe->ftp_id,
		    pc - id->fti_probe->ftp_faddr,
		    rp->r_a0, rp->r_a1, 0, 0);
	}

	mutex_exit(pid_mtx);
}

static uint64_t
sign_extend(uint64_t v, int num)
{
	return ((v + (1ul << (num - 1))) & ((1ul << num) - 1)) - (1ul << (num - 1));
}

static ulong_t
fasttrap_getreg(const struct regs *rp, uint_t reg)
{
	if (reg == 0)
		return 0;
	const greg_t *r = &rp->r_ra + (reg - 1);
	return *r;
}

static void
fasttrap_setreg(struct regs *rp, uint_t reg, ulong_t val)
{
	if (reg != 0)
	{
		greg_t *r = &rp->r_ra + (reg - 1);
		*r = val;
	}
}

int
fasttrap_pid_probe(struct regs *rp)
{
	proc_t *p = curproc;
	uintptr_t pc = rp->r_pc, new_pc = 0;
	fasttrap_bucket_t *bucket;
	kmutex_t *pid_mtx;
	fasttrap_tracepoint_t *tp, tp_local;
	pid_t pid;
	dtrace_icookie_t cookie;
	uint_t is_enabled = 0;

	/*
	 * It's possible that a user (in a veritable orgy of bad planning)
	 * could redirect this thread's flow of control before it reached the
	 * return probe fasttrap. In this case we need to kill the process
	 * since it's in a unrecoverable state.
	 */
	if (curthread->t_dtrace_step) {
		ASSERT(curthread->t_dtrace_on);
		fasttrap_sigtrap(p, curthread, pc);
		return (0);
	}

	/*
	 * Clear all user tracing flags.
	 */
	curthread->t_dtrace_ft = 0;
	curthread->t_dtrace_pc = 0;
	curthread->t_dtrace_npc = 0;
	curthread->t_dtrace_scrpc = 0;
	curthread->t_dtrace_astpc = 0;

	/*
	 * Treat a child created by a call to vfork(2) as if it were its
	 * parent. We know that there's only one thread of control in such a
	 * process: this one.
	 */
	while (p->p_flag & SVFORK) {
		p = p->p_parent;
	}

	pid = p->p_pid;
	pid_mtx = &cpu_core[CPU->cpu_id].cpuc_pid_lock;
	mutex_enter(pid_mtx);
	bucket = &fasttrap_tpoints.fth_table[FASTTRAP_TPOINTS_INDEX(pid, pc)];

	/*
	 * Lookup the tracepoint that the process just hit.
	 */
	for (tp = bucket->ftb_data; tp != NULL; tp = tp->ftt_next) {
		if (pid == tp->ftt_pid && pc == tp->ftt_pc &&
		    tp->ftt_proc->ftpc_acount != 0)
			break;
	}

	/*
	 * If we couldn't find a matching tracepoint, either a tracepoint has
	 * been inserted without using the pid<pid> ioctl interface (see
	 * fasttrap_ioctl), or somehow we have mislaid this tracepoint.
	 */
	if (tp == NULL) {
		mutex_exit(pid_mtx);
		return (-1);
	}

	/*
	 * Set the program counter to the address of the traced instruction
	 * so that it looks right in ustack() output.
	 */
	rp->r_pc = pc;

	if (tp->ftt_ids != NULL) {
		fasttrap_id_t *id;

		for (id = tp->ftt_ids; id != NULL; id = id->fti_next) {
			fasttrap_probe_t *probe = id->fti_probe;

			if (id->fti_ptype == DTFTP_ENTRY) {
				/*
				 * We note that this was an entry
				 * probe to help ustack() find the
				 * first caller.
				 */
				cookie = dtrace_interrupt_disable();
				DTRACE_CPUFLAG_SET(CPU_DTRACE_ENTRY);
				dtrace_probe(probe->ftp_id, rp->r_a0,
				    rp->r_a1, rp->r_a2, rp->r_a3,
				    rp->r_a4);
				DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_ENTRY);
				dtrace_interrupt_enable(cookie);
			} else if (id->fti_ptype == DTFTP_IS_ENABLED) {
				/*
				 * Note that in this case, we don't
				 * call dtrace_probe() since it's only
				 * an artificial probe meant to change
				 * the flow of control so that it
				 * encounters the true probe.
				 */
				is_enabled = 1;
			} else {
				dtrace_probe(probe->ftp_id, rp->r_a0,
				    rp->r_a1, rp->r_a2, rp->r_a3,
				    rp->r_a4);
			}
		}
	}

	/*
	 * We're about to do a bunch of work so we cache a local copy of
	 * the tracepoint to emulate the instruction, and then find the
	 * tracepoint again later if we need to light up any return probes.
	 */
	tp_local = *tp;
	mutex_exit(pid_mtx);
	tp = &tp_local;

	/*
	 * If there's an is-enabled probe connected to this tracepoint it
	 * means that there was a 'xorl %eax, %eax' or 'xorq %rax, %rax'
	 * instruction that was placed there by DTrace when the binary was
	 * linked. As this probe is, in fact, enabled, we need to stuff 1
	 * into %eax or %rax. Accordingly, we can bypass all the instruction
	 * emulation logic since we know the inevitable result. It's possible
	 * that a user could construct a scenario where the 'is-enabled'
	 * probe was on some other instruction, but that would be a rather
	 * exotic way to shoot oneself in the foot.
	 */
	if (is_enabled) {
		uint32_t instr = tp->ftt_instr;
		rp->r_a0 = 1;
		new_pc = rp->r_pc + ((instr & 3) == 3? 4: 2);
		goto done;
	}

	/*
	 * We emulate certain types of instructions to ensure correctness
	 * (in the case of position dependent instructions) or optimize
	 * common cases. The rest we have the thread execute back in user-
	 * land.
	 */
	uint32_t instr = tp->ftt_instr;
	switch (tp->ftt_type) {
	case FASTTRAP_T_JAL:
		{
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 20) |
			    (((instr >> 21) & 0x3FF) << 1) |
			    (((instr >> 20) & 0x1) << 11) |
			    (((instr >> 12) & 0xFF) << 12);
			int rd = (instr >> 7) & 0x1f;
			fasttrap_setreg(rp, rd, pc + 4);
			new_pc = pc + sign_extend(imm, 21);
		}
		break;
	case FASTTRAP_T_JALR:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rd = (instr >> 7) & 0x1f;
			uint64_t imm = (instr >> 20) & 0xfff;
			fasttrap_setreg(rp, rd, pc + 4);
			new_pc = fasttrap_getreg(rp, rs1) + sign_extend(imm, 12);
		}
		break;

	case FASTTRAP_T_BEQ:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rs2 = (instr >> 20) & 0x1f;
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 12) |
			    (((instr >> 25) & 0x3F) << 5) |
			    (((instr >>  8) & 0xF) <<  1) |
			    (((instr >>  7) & 0x1) << 11);
			if (fasttrap_getreg(rp, rs1) == fasttrap_getreg(rp, rs2))
				new_pc = pc + sign_extend(imm, 13);
			else
				new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_BNE:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rs2 = (instr >> 20) & 0x1f;
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 12) |
			    (((instr >> 25) & 0x3F) << 5) |
			    (((instr >>  8) & 0xF) <<  1) |
			    (((instr >>  7) & 0x1) << 11);
			if (fasttrap_getreg(rp, rs1) != fasttrap_getreg(rp, rs2))
				new_pc = pc + sign_extend(imm, 13);
			else
				new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_BLT:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rs2 = (instr >> 20) & 0x1f;
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 12) |
			    (((instr >> 25) & 0x3F) << 5) |
			    (((instr >>  8) & 0xF) <<  1) |
			    (((instr >>  7) & 0x1) << 11);
			if ((int64_t)fasttrap_getreg(rp, rs1) < (int64_t)fasttrap_getreg(rp, rs2))
				new_pc = pc + sign_extend(imm, 13);
			else
				new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_BGE:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rs2 = (instr >> 20) & 0x1f;
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 12) |
			    (((instr >> 25) & 0x3F) << 5) |
			    (((instr >>  8) & 0xF) <<  1) |
			    (((instr >>  7) & 0x1) << 11);
			if ((int64_t)fasttrap_getreg(rp, rs1) >= (int64_t)fasttrap_getreg(rp, rs2))
				new_pc = pc + sign_extend(imm, 13);
			else
				new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_BLTU:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rs2 = (instr >> 20) & 0x1f;
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 12) |
			    (((instr >> 25) & 0x3F) << 5) |
			    (((instr >>  8) & 0xF) <<  1) |
			    (((instr >>  7) & 0x1) << 11);
			if (fasttrap_getreg(rp, rs1) < fasttrap_getreg(rp, rs2))
				new_pc = pc + sign_extend(imm, 13);
			else
				new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_BGEU:
		{
			int rs1 = (instr >> 15) & 0x1f;
			int rs2 = (instr >> 20) & 0x1f;
			uint64_t imm =
			    (((instr >> 31) & 0x1) << 12) |
			    (((instr >> 25) & 0x3F) << 5) |
			    (((instr >>  8) & 0xF) <<  1) |
			    (((instr >>  7) & 0x1) << 11);
			if (fasttrap_getreg(rp, rs1) >= fasttrap_getreg(rp, rs2))
				new_pc = pc + sign_extend(imm, 13);
			else
				new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_CJ:
		{
			uint64_t imm =
			    (((instr >> 12) & 0x1) << 11) |
			    (((instr >> 11) & 0x1) << 4) |
			    (((instr >> 9) & 0x3) << 8) |
			    (((instr >> 8) & 0x1) << 10) |
			    (((instr >> 7) & 0x1) << 6) |
			    (((instr >> 6) & 0x1) << 7) |
			    (((instr >> 3) & 0x7) << 1) |
			    (((instr >> 2) & 0x1) << 5);
			new_pc = pc + sign_extend(imm, 12);
		}
		break;
	case FASTTRAP_T_CJR:
		{
			int rs1 = (instr >> 7) & 0x1f;
			if ((instr >> 12) & 1)
				fasttrap_setreg(rp, 1, pc + 2);
			new_pc = fasttrap_getreg(rp, rs1);
		}
		break;
	case FASTTRAP_T_CBEQZ:
		{
			uint64_t imm =
			    (((instr >> 12) & 0x1) << 8) |
			    (((instr >> 10) & 0x3) << 3) |
			    (((instr >> 5) & 0x3) << 6) |
			    (((instr >> 3) & 0x3) << 1) |
			    (((instr >> 2) & 0x1) << 5);
			int rs1 = ((instr >> 7) & 0x7) + 8;
			if (fasttrap_getreg(rp, rs1) == 0)
				new_pc = pc + sign_extend(imm, 9);
			else
				new_pc = pc + 2;
		}
		break;
	case FASTTRAP_T_CBNEZ:
		{
			uint64_t imm =
			    (((instr >> 12) & 0x1) << 8) |
			    (((instr >> 10) & 0x3) << 3) |
			    (((instr >> 5) & 0x3) << 6) |
			    (((instr >> 3) & 0x3) << 1) |
			    (((instr >> 2) & 0x1) << 5);
			int rs1 = ((instr >> 7) & 0x7) + 8;
			if (fasttrap_getreg(rp, rs1) != 0)
				new_pc = pc + sign_extend(imm, 9);
			else
				new_pc = pc + 2;
		}
		break;
	case FASTTRAP_T_AUIPC:
		{
			uint64_t imm = (instr & 0xfffff000);
			int rd = (instr >> 7) & 0x1f;
			fasttrap_setreg(rp, rd, pc + sign_extend(imm, 32));
			new_pc = pc + 4;
		}
		break;
	case FASTTRAP_T_COMMON:
	{
		uintptr_t addr;
		uint16_t scratch[3];

		addr = fasttrap_getreg(rp, 4); // tp
		addr += sizeof (void *);

		/*
		 * Generic Instruction Tracing
		 * ---------------------------
		 *
		 * 	<original instruction>
		 *	svc #T_DTRACE_RET
		 */
		int index = 0;
		scratch[index++] = instr & 0xffff;
		if ((instr & 3) == 3)
			scratch[index++] = (instr >> 16) & 0xffff;
		scratch[index++] = FASTTRAP_INSTR;
		if (copyout(scratch, (void*)addr, sizeof(scratch))) {
			fasttrap_sigtrap(p, curthread, pc);
			new_pc = pc;
		} else {
			sync_icache((caddr_t)addr, sizeof(scratch));

			curthread->t_dtrace_step = 1;
			curthread->t_dtrace_ret = 1;
			curthread->t_dtrace_pc = pc;
			curthread->t_dtrace_npc = pc + (((instr & 3) == 3)? 4: 2);
			curthread->t_dtrace_on = 1;
			new_pc = curthread->t_dtrace_astpc = curthread->t_dtrace_scrpc = addr;
		}
		break;
	}

	default:
		panic("fasttrap: mishandled an instruction");
	}

done:
	/*
	 * If there were no return probes when we first found the tracepoint,
	 * we should feel no obligation to honor any return probes that were
	 * subsequently enabled -- they'll just have to wait until the next
	 * time around.
	 */
	if (tp->ftt_retids != NULL) {
		/*
		 * We need to wait until the results of the instruction are
		 * apparent before invoking any return probes. If this
		 * instruction was emulated we can just call
		 * fasttrap_return_common(); if it needs to be executed, we
		 * need to wait until the user thread returns to the kernel.
		 */
		if (tp->ftt_type != FASTTRAP_T_COMMON) {
			/*
			 * Set the program counter to the address of the traced
			 * instruction so that it looks right in ustack()
			 * output. We had previously set it to the end of the
			 * instruction to simplify %rip-relative addressing.
			 */
			fasttrap_return_common(rp, pc, pid, new_pc);
		} else {
			ASSERT(curthread->t_dtrace_ret != 0);
			ASSERT(curthread->t_dtrace_pc == pc);
			ASSERT(curthread->t_dtrace_scrpc != 0);
			ASSERT(new_pc == curthread->t_dtrace_astpc);
		}
	}

	rp->r_pc = new_pc;

	return (0);
}

int
fasttrap_return_probe(struct regs *rp)
{
	proc_t *p = curproc;
	uintptr_t pc = curthread->t_dtrace_pc;
	uintptr_t npc = curthread->t_dtrace_npc;

	curthread->t_dtrace_pc = 0;
	curthread->t_dtrace_npc = 0;
	curthread->t_dtrace_scrpc = 0;
	curthread->t_dtrace_astpc = 0;

	/*
	 * Treat a child created by a call to vfork(2) as if it were its
	 * parent. We know that there's only one thread of control in such a
	 * process: this one.
	 */
	while (p->p_flag & SVFORK) {
		p = p->p_parent;
	}

	/*
	 * We set rp->r_pc to the address of the traced instruction so
	 * that it appears to dtrace_probe() that we're on the original
	 * instruction, and so that the user can't easily detect our
	 * complex web of lies. dtrace_return_probe() (our caller)
	 * will correctly set %pc after we return.
	 */
	rp->r_pc = pc;

	fasttrap_return_common(rp, pc, p->p_pid, npc);

	return (0);
}

/*ARGSUSED*/
uint64_t
fasttrap_pid_getarg(void *arg, dtrace_id_t id, void *parg, int argno,
    int aframes)
{
	return (fasttrap_anarg(ttolwp(curthread)->lwp_regs, argno));
}

/*ARGSUSED*/
uint64_t
fasttrap_usdt_getarg(void *arg, dtrace_id_t id, void *parg, int argno,
    int aframes)
{
	return (fasttrap_anarg(ttolwp(curthread)->lwp_regs, argno));
}


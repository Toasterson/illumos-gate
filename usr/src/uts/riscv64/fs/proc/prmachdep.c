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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/psw.h>
#include <sys/pcb.h>
#include <sys/buf.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/cpuvar.h>

#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/cmn_err.h>
#include <sys/stack.h>
#include <sys/copyops.h>

#include <sys/vmem.h>
#include <sys/mman.h>
#include <sys/vmparam.h>
#include <sys/fp.h>
#include <sys/archsystm.h>
#include <sys/vmsystm.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_kp.h>
#include <vm/page.h>
#include <sys/sbi.h>

#include <fs/proc/prdata.h>

int	prnwatch = 10000;	/* maximum number of watched areas */

/*
 * Force a thread into the kernel if it is not already there.
 * This is a no-op on uniprocessors.
 */
/* ARGSUSED */
void
prpokethread(kthread_t *t)
{
	if (t->t_state == TS_ONPROC && t->t_cpu != CPU)
		poke_cpu(t->t_cpu->cpu_id);
}

/*
 * Return general registers.
 */
void
prgetprregs(klwp_t *lwp, prgregset_t prp)
{
	ASSERT(MUTEX_NOT_HELD(&lwptoproc(lwp)->p_lock));

	getgregs(lwp, prp);
}

/*
 * Set general registers.
 * (Note: This can be an alias to setgregs().)
 */
void
prsetprregs(klwp_t *lwp, prgregset_t prp, int initial)
{
	if (initial)		/* set initial values */
		lwptoregs(lwp)->r_ssr = SSR_USERINIT;
	(void) setgregs(lwp, prp);
}

/*
 * Get the syscall return values for the lwp.
 */
int
prgetrvals(klwp_t *lwp, long *rval1, long *rval2)
{
	struct regs *r = lwptoregs(lwp);

	if (r->r_t0 != 0)
		return (r->r_t1);
	if (lwp->lwp_eosys == JUSTRETURN) {
		*rval1 = 0;
		*rval2 = 0;
	} else {
		*rval1 = r->r_a0;
		*rval2 = r->r_a1;
	}
	return (0);
}

/*
 * Does the system support floating-point, either through hardware
 * or by trapping and emulating floating-point machine instructions?
 */
int
prhasfp(void)
{
	return 1;
}

/*
 * Get floating-point registers.
 */
void
prgetprfpregs(klwp_t *lwp, prfpregset_t *pfp)
{
	bzero(pfp, sizeof (prfpregset_t));
	getfpregs(lwp, pfp);
}

/*
 * Set floating-point registers.
 * (Note: This can be an alias to setfpregs().)
 */
void
prsetprfpregs(klwp_t *lwp, prfpregset_t *pfp)
{
	setfpregs(lwp, pfp);
}

/*
 * Does the system support extra register state?
 */
/* ARGSUSED */
int
prhasx(proc_t *p)
{
	return (0);
}

/*
 * Get the size of the extra registers.
 */
/* ARGSUSED */
int
prgetprxregsize(proc_t *p)
{
	return (0);
}

/*
 * Get extra registers.
 */
/*ARGSUSED*/
void
prgetprxregs(klwp_t *lwp, caddr_t prx)
{
	/* no extra registers */
}

/*
 * Set extra registers.
 */
/*ARGSUSED*/
void
prsetprxregs(klwp_t *lwp, caddr_t prx)
{
	/* no extra registers */
}

/*
 * Return the base (lower limit) of the process stack.
 */
caddr_t
prgetstackbase(proc_t *p)
{
	return (p->p_usrstack - p->p_stksize);
}

/*
 * Return the "addr" field for pr_addr in prpsinfo_t.
 * This is a vestige of the past, so whatever we return is OK.
 */
caddr_t
prgetpsaddr(proc_t *p)
{
	return ((caddr_t)p);
}

static int
set_breakpoint(klwp_t *lwp, uint64_t pc)
{
	uint16_t instr;
	uint16_t new_instr;
	int res;

	res = copyin_nowatch((void *)pc, &instr, 2);
	if (res != 0) {
		return -1;
	}

	new_instr = 0x9002;
	res = uwrite(lwp->lwp_procp, &new_instr, sizeof(new_instr), pc);
	if (res != 0) {
		return -1;
	}
	sync_icache((caddr_t)pc, sizeof(new_instr));

	lwp->lwp_pcb.pcb_bpaddr = pc;
	lwp->lwp_pcb.pcb_bpinstr = instr;

	struct regs *r = lwptoregs(lwp);
	return 0;
}

static void
clear_breakpoint(klwp_t *lwp)
{
	struct regs *r = lwptoregs(lwp);
	uint64_t addr = lwp->lwp_pcb.pcb_bpaddr;
	if (addr == 0) {
		return;
	}
	lwp->lwp_pcb.pcb_bpaddr = 0;
	uint16_t instr;
	int res = copyin_nowatch((void *)addr, &instr, 2);
	if (res == 0 && instr == 0x9002) {
		instr = lwp->lwp_pcb.pcb_bpinstr;
		uwrite(lwp->lwp_procp, &instr, sizeof(instr), addr);
		sync_icache((caddr_t)addr, sizeof(instr));
	}
}

/*
 * Arrange to single-step the lwp.
 */
void
prstep(klwp_t *lwp, int watchstep)
{
	ASSERT(MUTEX_NOT_HELD(&lwptoproc(lwp)->p_lock));

	if (lwp->lwp_pcb.pcb_step == STEP_ACTIVE) {
		/* already set breakpoint. we must reset. */
		clear_breakpoint(lwp);
	}

	lwp->lwp_pcb.pcb_step = STEP_REQUESTED;

	if (watchstep) {
		lwp->lwp_pcb.pcb_flags |= WATCH_STEP;
	} else { 
		lwp->lwp_pcb.pcb_flags |= NORMAL_STEP;
	}

	aston(lwptot(lwp));	/* let trap() */
}

/*
 * Undo prstep().
 */
void
prnostep(klwp_t *lwp)
{
	ASSERT(ttolwp(curthread) == lwp ||
	    MUTEX_NOT_HELD(&lwptoproc(lwp)->p_lock));

	clear_breakpoint(lwp);
	lwp->lwp_pcb.pcb_step = STEP_NONE;

	lwp->lwp_pcb.pcb_flags &= ~(NORMAL_STEP|WATCH_STEP);
}

/*
 * Return non-zero if a single-step is in effect.
 */
int
prisstep(klwp_t *lwp)
{
	ASSERT(MUTEX_NOT_HELD(&lwptoproc(lwp)->p_lock));

	return (lwp->lwp_pcb.pcb_step != STEP_NONE);
}

/*
 * Set the PC to the specified virtual address.
 */
void
prsvaddr(klwp_t *lwp, caddr_t vaddr)
{
	struct regs *r = lwptoregs(lwp);

	ASSERT(MUTEX_NOT_HELD(&lwptoproc(lwp)->p_lock));

	r->r_pc = (uintptr_t)vaddr;
}

/*
 * Map address "addr" in address space "as" into a kernel virtual address.
 * The memory is guaranteed to be resident and locked down.
 */
caddr_t
prmapin(struct as *as, caddr_t addr, int writing)
{
	page_t *pp;
	caddr_t kaddr;
	pfn_t pfnum;

	/*
	 * XXX - Because of past mistakes, we have bits being returned
	 * by getpfnum that are actually the page type bits of the pte.
	 * When the object we are trying to map is a memory page with
	 * a page structure everything is ok and we can use the optimal
	 * method, ppmapin.  Otherwise, we have to do something special.
	 */
	pfnum = hat_getpfnum(as->a_hat, addr);
	if (pf_is_memory(pfnum)) {
		pp = page_numtopp_nolock(pfnum);
		if (pp != NULL) {
			ASSERT(PAGE_LOCKED(pp));
			kaddr = ppmapin(pp, writing ?
			    (PROT_READ | PROT_WRITE) : PROT_READ, (caddr_t)-1);
			return (kaddr + ((uintptr_t)addr & PAGEOFFSET));
		}
	}

	/*
	 * Oh well, we didn't have a page struct for the object we were
	 * trying to map in; ppmapin doesn't handle devices, but allocating a
	 * heap address allows ppmapout to free virtual space when done.
	 */
	kaddr = vmem_alloc(heap_arena, PAGESIZE, VM_SLEEP);

	hat_devload(kas.a_hat, kaddr, MMU_PAGESIZE,  pfnum,
	    writing ? (PROT_READ | PROT_WRITE) : PROT_READ, 0);

	return (kaddr + ((uintptr_t)addr & PAGEOFFSET));
}

/*
 * Unmap address "addr" in address space "as"; inverse of prmapin().
 */
/* ARGSUSED */
void
prmapout(struct as *as, caddr_t addr, caddr_t vaddr, int writing)
{
	extern void ppmapout(caddr_t);

	vaddr = (caddr_t)((uintptr_t)vaddr & PAGEMASK);
	ppmapout(vaddr);
}

static uint64_t
sign_extend(uint64_t v, int num)
{
	return ((v + (1ul << (num - 1))) & ((1ul << num) - 1)) - (1ul << (num - 1));
}

static uint64_t
get_reg_value(const struct regs *r, int rs)
{
	if (rs == 0)
		return 0;
	return *(&r->r_ra + (rs - 1));
}

static uint64_t
get_next_address(const struct regs *r, uint64_t pc, uint32_t instr)
{
	if ((instr & 0x7f) == 0x6f) {
		// JAL
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 20) |
		    (((instr >> 21) & 0x3FF) << 1) |
		    (((instr >> 20) & 0x1) << 11) |
		    (((instr >> 12) & 0xFF) << 12);
		return pc + sign_extend(imm, 21);
	}
	if ((instr & 0x707f) == 0x0067) {
		// JALR
		int rs1 = (instr >> 15) & 0x1f;
		uint64_t imm = (instr >> 20) & 0xfff;
		return get_reg_value(r, rs1) + sign_extend(imm, 12);
	}
	if ((instr & 0x707f) == 0x0063) {
		// BEQ
		int rs1 = (instr >> 15) & 0x1f;
		int rs2 = (instr >> 20) & 0x1f;
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 12) |
		    (((instr >> 25) & 0x3F) << 5) |
		    (((instr >>  8) & 0xF) <<  1) |
		    (((instr >>  7) & 0x1) << 11);
		if (get_reg_value(r, rs1) == get_reg_value(r, rs2))
			return pc + sign_extend(imm, 13);
	}
	if ((instr & 0x707f) == 0x1063) {
		// BNE
		int rs1 = (instr >> 15) & 0x1f;
		int rs2 = (instr >> 20) & 0x1f;
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 12) |
		    (((instr >> 25) & 0x3F) << 5) |
		    (((instr >>  8) & 0xF) <<  1) |
		    (((instr >>  7) & 0x1) << 11);
		if (get_reg_value(r, rs1) != get_reg_value(r, rs2))
			return pc + sign_extend(imm, 13);
	}
	if ((instr & 0x707f) == 0x4063) {
		// BLT
		int rs1 = (instr >> 15) & 0x1f;
		int rs2 = (instr >> 20) & 0x1f;
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 12) |
		    (((instr >> 25) & 0x3F) << 5) |
		    (((instr >>  8) & 0xF) <<  1) |
		    (((instr >>  7) & 0x1) << 11);
		if ((int64_t)get_reg_value(r, rs1) < (int64_t)get_reg_value(r, rs2))
			return pc + sign_extend(imm, 13);
	}
	if ((instr & 0x707f) == 0x5063) {
		// BGE
		int rs1 = (instr >> 15) & 0x1f;
		int rs2 = (instr >> 20) & 0x1f;
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 12) |
		    (((instr >> 25) & 0x3F) << 5) |
		    (((instr >>  8) & 0xF) <<  1) |
		    (((instr >>  7) & 0x1) << 11);
		if ((int64_t)get_reg_value(r, rs1) >= (int64_t)get_reg_value(r, rs2))
			return pc + sign_extend(imm, 13);
	}
	if ((instr & 0x707f) == 0x6063) {
		// BLTU
		int rs1 = (instr >> 15) & 0x1f;
		int rs2 = (instr >> 20) & 0x1f;
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 12) |
		    (((instr >> 25) & 0x3F) << 5) |
		    (((instr >>  8) & 0xF) <<  1) |
		    (((instr >>  7) & 0x1) << 11);
		if (get_reg_value(r, rs1) < get_reg_value(r, rs2))
			return pc + sign_extend(imm, 13);
	}
	if ((instr & 0x707f) == 0x7063) {
		// BGEU
		int rs1 = (instr >> 15) & 0x1f;
		int rs2 = (instr >> 20) & 0x1f;
		uint64_t imm =
		    (((instr >> 31) & 0x1) << 12) |
		    (((instr >> 25) & 0x3F) << 5) |
		    (((instr >>  8) & 0xF) <<  1) |
		    (((instr >>  7) & 0x1) << 11);
		if (get_reg_value(r, rs1) >= get_reg_value(r, rs2))
			return pc + sign_extend(imm, 13);
	}
	if ((instr & 0xe003) == 0xa001) {
		// C.J
		uint64_t imm =
		    (((instr >> 12) & 0x1) << 11) |
		    (((instr >> 11) & 0x1) << 4) |
		    (((instr >> 9) & 0x3) << 8) |
		    (((instr >> 8) & 0x1) << 10) |
		    (((instr >> 7) & 0x1) << 6) |
		    (((instr >> 6) & 0x1) << 7) |
		    (((instr >> 3) & 0x7) << 1) |
		    (((instr >> 2) & 0x1) << 5);
		return pc + sign_extend(imm, 12);
	}
	if ((instr & 0xe07f) == 0x8002 && (instr & 0xf80) != 0) {
		// C.JR
		// C.JALR
		int rs1 = (instr >> 7) & 0x1f;
		return get_reg_value(r, rs1);
	}
	if ((instr & 0xe003) == 0xc001) {
		// C.BEQZ
		uint64_t imm =
		    (((instr >> 12) & 0x1) << 8) |
		    (((instr >> 10) & 0x3) << 3) |
		    (((instr >> 5) & 0x3) << 6) |
		    (((instr >> 3) & 0x3) << 1) |
		    (((instr >> 2) & 0x1) << 5);
		int rs1 = ((instr >> 7) & 0x7) + 8;
		if (get_reg_value(r, rs1) == 0)
			return pc + sign_extend(imm, 9);
	}
	if ((instr & 0xe003) == 0xe001) {
		// C.BNEZ
		uint64_t imm =
		    (((instr >> 12) & 0x1) << 8) |
		    (((instr >> 10) & 0x3) << 3) |
		    (((instr >> 5) & 0x3) << 6) |
		    (((instr >> 3) & 0x3) << 1) |
		    (((instr >> 2) & 0x1) << 5);
		int rs1 = ((instr >> 7) & 0x7) + 8;
		if (get_reg_value(r, rs1) != 0)
			return pc + sign_extend(imm, 9);
	}
	if ((instr & 0x3) == 0x3)
		return pc + 4;
	return pc + 2;
}

/*
 * Prepare to single-step the lwp if requested.
 * This is called by the lwp itself just before returning to user level.
 */
void
prdostep(void)
{
	klwp_t *lwp = ttolwp(curthread);
	struct regs *r = lwptoregs(lwp);
	proc_t *p = lwptoproc(lwp);
	struct as *as = p->p_as;
	uint64_t pc;

	ASSERT(lwp != NULL);
	ASSERT(r != NULL);

	if (lwp->lwp_pcb.pcb_step == STEP_NONE ||
	    lwp->lwp_pcb.pcb_step == STEP_ACTIVE)
		return;

	pc = r->r_pc;

	uint32_t instr;
	if (fuword32_nowatch((void *)pc, (uint32_t *)&instr) != 0)
		return;

	if ((instr & 0x3) != 0x3)
		instr &= 0xffff;

	uint64_t npc = get_next_address(r, pc, instr);
	if (set_breakpoint(lwp, npc) != 0)
		return;

	lwp->lwp_pcb.pcb_step = STEP_ACTIVE;
}

/*
 * Wrap up single stepping of the lwp.
 * This is called by the lwp itself just after it has taken
 * the FLTBOUNDS trap.  We fix up the PC and nPC to have their
 * proper values after the step.  We return 1 to indicate that
 * this fault really is the one we are expecting, else 0.
 *
 * This is also called from syscall() and stop() to reset PC
 * and nPC to their proper values for debugger visibility.
 */
int
prundostep(void)
{
	klwp_t *lwp = ttolwp(curthread);
	int rc = 0;
	uint64_t pc;

	ASSERT(lwp != NULL);

	if (lwp->lwp_pcb.pcb_step == STEP_ACTIVE) {
		struct regs *r = lwptoregs(lwp);

		ASSERT(r != NULL);

		pc = r->r_pc;

		if (pc == lwp->lwp_pcb.pcb_bpaddr) {
			rc = 1; /* expecting */
		}
		clear_breakpoint(lwp);
		lwp->lwp_pcb.pcb_step = STEP_WASACTIVE;
	}

	return (rc);
}

/*
 * Make sure the lwp is in an orderly state
 * for inspection by a debugger through /proc.
 *
 * This needs to be called only once while the current thread remains in the
 * kernel and needs to be called while holding no resources (mutex locks, etc).
 *
 * As a hedge against these conditions, if prstop() is called repeatedly
 * before prunstop() is called, it does nothing and just returns.
 *
 * prunstop() must be called before the thread returns to user level.
 */
/* ARGSUSED */
void
prstop(int why, int what)
{
	klwp_t *lwp = ttolwp(curthread);
	struct regs *r = lwptoregs(lwp);

	if (lwp->lwp_pcb.pcb_flags & PRSTOP_CALLED)
		return;

	/*
	 * Make sure we don't deadlock on a recursive call
	 * to prstop().  stop() tests the lwp_nostop flag.
	 */
	ASSERT(lwp->lwp_nostop == 0);
	lwp->lwp_nostop = 1;

	if (lwp->lwp_pcb.pcb_step != STEP_NONE)
		(void) prundostep();

	uint32_t instr;
	uint16_t instr16;
	if (copyin_nowatch((caddr_t)r->r_pc, &instr, sizeof(instr)) == 0) {
		if ((instr & 0x3) != 0x3)
			instr &= 0xffff;
		lwp->lwp_pcb.pcb_instr = instr;
		lwp->lwp_pcb.pcb_flags |= INSTR_VALID;
	}
	else if (copyin_nowatch((caddr_t)r->r_pc, &instr16, sizeof(instr16)) == 0) {
		if ((instr16 & 0x3) == 0x3) {
			lwp->lwp_pcb.pcb_instr = 0;
			lwp->lwp_pcb.pcb_flags &= ~INSTR_VALID;
		} else {
			lwp->lwp_pcb.pcb_instr = instr16;
			lwp->lwp_pcb.pcb_flags |= INSTR_VALID;
		}
	}
	else {
		lwp->lwp_pcb.pcb_flags &= ~INSTR_VALID;
		lwp->lwp_pcb.pcb_instr = 0;
	}

	(void) save_syscall_args();
	ASSERT(lwp->lwp_nostop == 1);
	lwp->lwp_nostop = 0;

	lwp->lwp_pcb.pcb_flags |= PRSTOP_CALLED;
	aston(curthread);	/* so prunstop() will be called */
}

/*
 * Inform prstop() that it should do its work again
 * the next time it is called.
 */
void
prunstop(void)
{
	ttolwp(curthread)->lwp_pcb.pcb_flags &= ~PRSTOP_CALLED;
}

/*
 * Fetch the user-level instruction on which the lwp is stopped.
 * It was saved by the lwp itself, in prstop().
 * Return non-zero if the instruction is valid.
 */
int
prfetchinstr(klwp_t *lwp, ulong_t *ip)
{
	*ip = (ulong_t)lwp->lwp_pcb.pcb_instr;
	return (lwp->lwp_pcb.pcb_flags & INSTR_VALID);
}

/*
 * Called from trap() when a load or store instruction
 * falls in a watched page but is not a watchpoint.
 * We emulate the instruction in the kernel.
 */
/* ARGSUSED */
int
pr_watch_emul(struct regs *rp, caddr_t addr, enum seg_rw rw)
{
#ifdef SOMEDAY
	int res;
	proc_t *p = curproc;
	char *badaddr = (caddr_t)(-1);
	int mapped;

	/* prevent recursive calls to pr_watch_emul() */
	ASSERT(!(curthread->t_flag & T_WATCHPT));
	curthread->t_flag |= T_WATCHPT;

	watch_disable_addr(addr, 8, rw);
	res = do_unaligned(rp, &badaddr);
	watch_enable_addr(addr, 8, rw);

	curthread->t_flag &= ~T_WATCHPT;
	if (res == SIMU_SUCCESS) {
		/* adjust the pc */
		return (1);
	}
#endif
	return (0);
}


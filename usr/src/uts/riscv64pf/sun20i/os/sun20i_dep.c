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
 * Copyright 2022 Hayashi Naoyuki
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>
#include <sys/sdt_impl.h>
#include <sys/sbi.h>
#include <asm/t-head.h>

void
sync_icache(caddr_t addr, uint_t len)
{
	thead_dcache_clean((uintptr_t)addr, len);
	thead_icache_invalidate((uintptr_t)addr, len);
	sbi_remote_fence_i(0, -1ul);
}

void
sync_data_memory(caddr_t addr, size_t len)
{
	thead_dcache_clean((uintptr_t)addr, len);
}

void
kobj_sync_instruction_memory(caddr_t addr, size_t len)
{
	thead_dcache_clean((uintptr_t)addr, len);
	thead_icache_invalidate((uintptr_t)addr, len);
	sbi_remote_fence_i(0, -1ul);
}

int
sdt_write_instruction(sdt_instr_t *inst, sdt_instr_t val)
{
	*inst = val;
	thead_dcache_clean((uintptr_t)inst, sizeof(sdt_instr_t));
	thead_icache_invalidate((uintptr_t)inst, sizeof(sdt_instr_t));
	asm volatile ("fence.i" ::: "memory");
	return 0;
}


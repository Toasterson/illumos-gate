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
 * Copyright 2017 Hayashi Naoyuki
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/saio.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/bootconf.h>
#include <sys/salib.h>
#include <sys/memlist.h>
#include <sys/machparam.h>
#include <sys/cpu.h>
#include <sys/pte.h>
#include <sys/memlist_impl.h>
#include <sys/csr.h>
#include <sys/platform.h>
#include <sys/boot.h>
#include <alloca.h>
#include "boot_plat.h"

#ifdef DEBUG
static int	debug = 1;
#else /* DEBUG */
extern int	debug;
#endif /* DEBUG */
#define	dprintf	if (debug) printf

extern caddr_t		memlistpage;
extern char _BootScratch[];
extern char _RamdiskStart[];
extern char _BootStart[];
extern char _BootEnd[];

extern void init_physmem(void);
extern void init_iolist(void);

static caddr_t scratch_used_top;
static pte_t *l2_ptbl;

pte_t pte_mt_mem;
pte_t pte_mt_nc;
pte_t pte_mt_io;

static void init_pt(void);
static inline int l2_pteidx(caddr_t vaddr) { return ((((uintptr_t)vaddr) >> (PAGESHIFT+2*NPTESHIFT)) & ((1<<NPTESHIFT)-1));}
static inline int l1_pteidx(caddr_t vaddr) { return ((((uintptr_t)vaddr) >> (PAGESHIFT+1*NPTESHIFT)) & ((1<<NPTESHIFT)-1));}
static inline int l0_pteidx(caddr_t vaddr) { return ((((uintptr_t)vaddr) >> (PAGESHIFT+0*NPTESHIFT)) & ((1<<NPTESHIFT)-1));}


void
init_memory(void)
{
	kmem_init();
	init_iolist();
	init_pt();
}

void
init_memlists(void)
{
	scratch_used_top = _BootScratch;
	memlistpage = scratch_used_top;
	scratch_used_top += MMU_PAGESIZE;

	init_physmem();
}

static pnode_t
get_cpu_node()
{
	pnode_t node = prom_finddevice("/cpus");
	if (node < 0) {
		return node;
	}
	pnode_t child = prom_childnode(node);
	while (child > 0) {
		int len = prom_getproplen(child, "mmu-type");
		if (len > 0) {
			char *mmu_type = __builtin_alloca(len + 1);
			prom_getprop(child, "mmu-type", mmu_type);
			if (strncmp(mmu_type, "riscv,sv", strlen("riscv,sv")) == 0)
				return child;
		}
		child = prom_nextnode(child);
	}
	return child;
}

static bool
is_supported_pbmt(void)
{
	pnode_t node = get_cpu_node();
	if (node > 0) {
		int len = prom_getproplen(node, "riscv,isa");
		if (len > 0) {
			char *isa = __builtin_alloca(len + 1);
			prom_getprop(node, "riscv,isa", isa);
			while (*isa != 0) {
				if (strncmp(isa, "svpbmt", strlen("svpbmt")) == 0)
					return true;
				while (*isa != 0) {
					if (*isa == '_') {
						isa++;
						break;
					}
					isa++;
				}
			}
		}
	}
	return false;
}

static bool
is_supported_thead_mmu(void)
{
	pnode_t node = get_cpu_node();
	return node > 0 && prom_is_compatible(node, "thead,c906");
}

/*
 * ページテーブルの初期化
 */
static void
init_pt(void)
{
	uintptr_t paddr;
	if (is_supported_thead_mmu()) {
		pte_mt_mem = PTE_MT_THEAD_MEM;
		pte_mt_nc = PTE_MT_THEAD_NC;
		pte_mt_io = PTE_MT_THEAD_IO;
	} else if (is_supported_pbmt()) {
		pte_mt_mem = PTE_MT_PBMT_PMA;
		pte_mt_nc = PTE_MT_PBMT_NC;
		pte_mt_io = PTE_MT_PBMT_IO;
	} else {
		pte_mt_mem = 0;
		pte_mt_nc = 0;
		pte_mt_io = 0;
	}

	paddr = memlist_get(MMU_PAGESIZE, MMU_PAGESIZE, &pfreelistp);
	if (paddr == 0)
		prom_panic("phy alloc error for L1 PT\n");
	bzero((void *)paddr, MMU_PAGESIZE);
	l2_ptbl = (pte_t *)paddr;

	for (struct memlist *ml = plinearlistp; ml != NULL; ml = ml->ml_next) {
		uintptr_t pa = ml->ml_address;
		uintptr_t sz = ml->ml_size;
		map_phys(PTE_A | PTE_D | PTE_G | PTE_X | PTE_W | PTE_R | pte_mt_mem, (caddr_t)pa, pa, sz);
	}

	for (struct memlist *ml = piolistp; ml != NULL; ml = ml->ml_next) {
		uintptr_t pa = ml->ml_address;
		uintptr_t sz = ml->ml_size;
		map_phys(PTE_A | PTE_D | PTE_G | PTE_W | PTE_R | pte_mt_io, (caddr_t)pa, pa, sz);
	}
	asm volatile ("fence":::"memory");
	csr_write_satp(SATP_MODE_SV39 | (0ul << SATP_ASID_SHIFT) | (((uint64_t)l2_ptbl) >> MMU_PAGESHIFT));
	asm volatile ("fence.i":::"memory");
	asm volatile ("sfence.vma" ::: "memory");
}


static void
map_pages(pte_t pte_attr, caddr_t vaddr, uint64_t paddr, size_t bytes)
{
	int l0_idx = l0_pteidx(vaddr);
	int l1_idx = l1_pteidx(vaddr);
	int l2_idx = l2_pteidx(vaddr);

	if (bytes == MMU_PAGESIZE1G) {
		if ((uintptr_t)vaddr & (bytes - 1)) {
			prom_panic("invalid vaddr (1G)\n");
		}
		if (paddr & (bytes - 1)) {
			prom_panic("invalid paddr (1G)\n");
		}
		if (l2_ptbl[l2_idx] & PTE_V) {
			prom_panic("invalid L2 PT\n");
		}
		l2_ptbl[l2_idx] = PTE_FROM_PA(paddr) | pte_attr | PTE_V;
		asm volatile ("fence":::"memory");
		return;
	}

	if ((l2_ptbl[l2_idx] & PTE_V) == 0) {
		paddr_t pa = memlist_get(MMU_PAGESIZE, MMU_PAGESIZE, &pfreelistp);
		if (pa == 0)
			prom_panic("phy alloc error for L2 PT\n");
		bzero((void *)(uintptr_t)pa, MMU_PAGESIZE);
		asm volatile ("fence":::"memory");
		l2_ptbl[l2_idx] = PTE_FROM_PA(pa) | PTE_V;
	}

	if (!IS_TABLE(l2_ptbl[l2_idx]))
		prom_panic("invalid L2 PT\n");

	pte_t *l1_ptbl = (pte_t *)(uintptr_t)PTE_TO_PA(l2_ptbl[l2_idx]);

	if (bytes == MMU_PAGESIZE2M) {
		if ((uintptr_t)vaddr & (bytes - 1)) {
			prom_panic("invalid vaddr (2M)\n");
		}
		if (paddr & (bytes - 1)) {
			prom_panic("invalid paddr (2M)\n");
		}
		if (l1_ptbl[l1_idx] & PTE_V) {
			prom_panic("invalid L1 PT\n");
		}
		l1_ptbl[l1_idx] = PTE_FROM_PA(paddr) | pte_attr | PTE_V;
		asm volatile ("fence":::"memory");
		return;
	}

	if ((l1_ptbl[l1_idx] & PTE_V) == 0) {
		paddr_t pa = memlist_get(MMU_PAGESIZE, MMU_PAGESIZE, &pfreelistp);
		if (pa == 0)
			prom_panic("phy alloc error for L1 PT\n");
		bzero((void *)(uintptr_t)pa, MMU_PAGESIZE);
		asm volatile ("fence":::"memory");
		l1_ptbl[l1_idx] = PTE_FROM_PA(pa) | PTE_V;
	}

	if (!IS_TABLE(l1_ptbl[l1_idx]))
		prom_panic("invalid L1 PT\n");

	pte_t *l0_ptbl = (pte_t *)(uintptr_t)PTE_TO_PA(l1_ptbl[l1_idx]);
	if (bytes == MMU_PAGESIZE) {
		if ((uintptr_t)vaddr & (bytes - 1)) {
			prom_panic("invalid vaddr (4K)\n");
		}
		if (paddr & (bytes - 1)) {
			prom_panic("invalid paddr (4K)\n");
		}
		if (l0_ptbl[l0_idx] & PTE_V) {
			prom_panic("invalid L0 PT\n");
		}
		l0_ptbl[l0_idx] = PTE_FROM_PA(paddr) | pte_attr | PTE_V;
		asm volatile ("fence":::"memory");
		return;
	}
	prom_panic("invalid size\n");
}

void
map_phys(pte_t pte_attr, caddr_t vaddr, uint64_t paddr, size_t bytes)
{
	if (((uintptr_t)vaddr % MMU_PAGESIZE) != 0) {
		prom_panic("map_phys invalid vaddr\n");
	}
	if ((paddr % MMU_PAGESIZE) != 0) {
		prom_panic("map_phys invalid paddr\n");
	}
	if ((bytes % MMU_PAGESIZE) != 0) {
		prom_panic("map_phys invalid size\n");
	}

	while (bytes) {
		uintptr_t va = (uintptr_t)vaddr;
		size_t maxalign = va & (-va);
		size_t mapsz;
		if (maxalign >= MMU_PAGESIZE1G && bytes >= MMU_PAGESIZE1G && paddr >= MMU_PAGESIZE1G) {
			mapsz = MMU_PAGESIZE1G;
		} else if (maxalign >= MMU_PAGESIZE2M && bytes >= MMU_PAGESIZE2M && paddr >= MMU_PAGESIZE2M) {
			mapsz = MMU_PAGESIZE2M;
		} else {
			mapsz = MMU_PAGESIZE;
		}
		map_pages(pte_attr, vaddr, paddr, mapsz);
		bytes -= mapsz;
		vaddr += mapsz;
		paddr += mapsz;
	}
}

static caddr_t
get_low_vpage(size_t bytes)
{
	caddr_t v;

	if ((scratch_used_top + bytes) <= _RamdiskStart) {
		v = scratch_used_top;
		scratch_used_top += bytes;
		return (v);
	}

	return (NULL);
}

caddr_t
resalloc(enum RESOURCES type, size_t bytes, caddr_t virthint, int align)
{
	caddr_t	vaddr = 0;
	uintptr_t paddr = 0;

	if (bytes != 0) {
		/* extend request to fill a page */
		bytes = roundup(bytes, MMU_PAGESIZE);
		dprintf("resalloc:  bytes = %lu\n", bytes);
		switch (type) {
		case RES_BOOTSCRATCH:
			vaddr = get_low_vpage(bytes);
			break;
		case RES_CHILDVIRT:
			vaddr = virthint;
			while (bytes) {
				uintptr_t va = (uintptr_t)virthint;
				size_t maxalign = va & (-va);
				size_t mapsz;
				if (maxalign >= MMU_PAGESIZE1G && bytes >= MMU_PAGESIZE1G) {
					mapsz = MMU_PAGESIZE1G;
				} else if (maxalign >= MMU_PAGESIZE2M && bytes >= MMU_PAGESIZE2M) {
					mapsz = MMU_PAGESIZE2M;
				} else {
					mapsz = MMU_PAGESIZE;
				}
				paddr = memlist_get(mapsz, mapsz, &pfreelistp);
				if (paddr == 0) {
					prom_panic("phys mem allocate error\n");
				}
				map_phys(PTE_A | PTE_D | PTE_G | PTE_W | PTE_R | PTE_X | pte_mt_mem, virthint, paddr, mapsz);
				bytes -= mapsz;
				virthint += mapsz;
			}
			break;
		default:
			dprintf("Bad resurce type\n");
			break;
		}
	}

	return vaddr;
}

void
reset_alloc(void)
{}

void
resfree(enum RESOURCES type, caddr_t virtaddr, size_t size)
{}

void
fini_memory(void)
{
	for (int l2_idx = NPTEPERPT / 2; l2_idx < NPTEPERPT; l2_idx++) {
		if ((l2_ptbl[l2_idx] & PTE_V) == 0) {
			paddr_t pa = memlist_get(MMU_PAGESIZE, MMU_PAGESIZE, &pfreelistp);
			if (pa == 0)
				prom_panic("phy alloc error for L2 PT\n");
			bzero((void *)(uintptr_t)pa, MMU_PAGESIZE);
			l2_ptbl[l2_idx] = PTE_FROM_PA(pa) | PTE_V;
		}
	}
}

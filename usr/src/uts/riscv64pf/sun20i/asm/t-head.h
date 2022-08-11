/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 */

#pragma once

#include <sys/types.h>
#include <sys/platform.h>
#include <sys/sysmacros.h>

static inline void
thead_sync_s(void)
{
	asm volatile (".long  ((0 << 25) | (0x19 << 20) | (0 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":::"memory");
}
static inline void
thead_sync_is(void)
{
	asm volatile (".long  ((0 << 25) | (0x1b << 20) | (0 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":::"memory");
}
static inline void
thead_dcache_clean_all(void)
{
	asm volatile (".long  ((0 << 25) | (1 << 20) | (0 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":::"memory");
	thead_sync_s();
}
static inline void
_thead_dcache_clean_vaddr(uintptr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (4 << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
_thead_dcache_clean_paddr(paddr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (8 << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
thead_dcache_invalidate_all(void)
{
	asm volatile (".long  ((0 << 25) | (2 << 20) | (0 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":::"memory");
	thead_sync_s();
}
static inline void
_thead_dcache_invalidate_vaddr(uintptr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (6 << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
_thead_dcache_invalidate_paddr(paddr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (0xa << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
thead_dcache_flush_all(void)
{
	asm volatile (".long  ((0 << 25) | (3 << 20) | (0 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":::"memory");
	thead_sync_s();
}
static inline void
_thead_dcache_flush_vaddr(uintptr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (7 << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
_thead_dcache_flush_paddr(paddr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (0xb << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
thead_icache_invalidate_all(void)
{
	asm volatile (".long  ((0 << 25) | (0x11 << 20) | (0 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":::"memory");
	thead_sync_s();
}
static inline void
_thead_icache_invalidate_vaddr(uintptr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (0x10 << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}
static inline void
_thead_icache_invalidate_paddr(paddr_t addr)
{
	register uint64_t a0 asm ("a0") = addr;
	asm volatile (".long ((1 << 25) | (0x18 << 20) | (10 << 15) | (0 << 12) | (0 << 7) | (0xb << 0))":: "r"(a0):"memory");
}

static inline void
thead_dcache_clean(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, DCACHE_LINE);
	while (addr < end) {
		_thead_dcache_clean_vaddr(addr);
		addr += DCACHE_LINE;
	}
	thead_sync_s();
}
static inline void
thead_dcache_clean_phys(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, DCACHE_LINE);
	while (addr < end) {
		_thead_dcache_clean_paddr(addr);
		addr += DCACHE_LINE;
	}
	thead_sync_s();
}
static inline void
thead_dcache_invalidate(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, DCACHE_LINE);
	while (addr < end) {
		_thead_dcache_invalidate_vaddr(addr);
		addr += DCACHE_LINE;
	}
	thead_sync_s();
}
static inline void
thead_dcache_invalidate_phys(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, DCACHE_LINE);
	while (addr < end) {
		_thead_dcache_invalidate_paddr(addr);
		addr += DCACHE_LINE;
	}
	thead_sync_s();
}
static inline void
thead_dcache_flush(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, DCACHE_LINE);
	while (addr < end) {
		_thead_dcache_flush_vaddr(addr);
		addr += DCACHE_LINE;
	}
	thead_sync_s();
}
static inline void
thead_dcache_flush_phys(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, DCACHE_LINE);
	while (addr < end) {
		_thead_dcache_flush_paddr(addr);
		addr += DCACHE_LINE;
	}
	thead_sync_s();
}
static inline void
thead_icache_invalidate(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, ICACHE_LINE);
	while (addr < end) {
		_thead_icache_invalidate_vaddr(addr);
		addr += ICACHE_LINE;
	}
	thead_sync_is();
}
static inline void
thead_icache_invalidate_phys(uintptr_t addr, size_t size)
{
	uintptr_t end = addr + size;
	addr = P2ALIGN(addr, ICACHE_LINE);
	while (addr < end) {
		_thead_icache_invalidate_paddr(addr);
		addr += ICACHE_LINE;
	}
	thead_sync_is();
}

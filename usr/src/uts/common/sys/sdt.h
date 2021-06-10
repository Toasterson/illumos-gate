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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2018 Nexenta Systems, Inc.  All rights reserved.
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 */

#ifndef _SYS_SDT_H
#define	_SYS_SDT_H

#include <sys/stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _KERNEL

#define	DTRACE_PROBE(provider, name) {					\
	extern void __dtrace_##provider##___##name(void);		\
	__dtrace_##provider##___##name();				\
}

#define	DTRACE_PROBE1(provider, name, arg1) {				\
	extern void __dtrace_##provider##___##name(unsigned long);	\
	__dtrace_##provider##___##name((unsigned long)arg1);		\
}

#define	DTRACE_PROBE2(provider, name, arg1, arg2) {			\
	extern void __dtrace_##provider##___##name(unsigned long,	\
	    unsigned long);						\
	__dtrace_##provider##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2);					\
}

#define	DTRACE_PROBE3(provider, name, arg1, arg2, arg3) {		\
	extern void __dtrace_##provider##___##name(unsigned long,	\
	    unsigned long, unsigned long);				\
	__dtrace_##provider##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2, (unsigned long)arg3);			\
}

#define	DTRACE_PROBE4(provider, name, arg1, arg2, arg3, arg4) {		\
	extern void __dtrace_##provider##___##name(unsigned long,	\
	    unsigned long, unsigned long, unsigned long);		\
	__dtrace_##provider##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2, (unsigned long)arg3,			\
	    (unsigned long)arg4);					\
}

#define	DTRACE_PROBE5(provider, name, arg1, arg2, arg3, arg4, arg5) {	\
	extern void __dtrace_##provider##___##name(unsigned long,	\
	    unsigned long, unsigned long, unsigned long, unsigned long);\
	__dtrace_##provider##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2, (unsigned long)arg3,			\
	    (unsigned long)arg4, (unsigned long)arg5);			\
}

#else /* _KERNEL */

#define TO_STRING(s) #s

#if defined __aarch64
#define	DTRACE_PROBE(name)	{					\
	asm volatile (							\
		"1:\n"							\
		"bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
		".pushsection .probepoint, \"aw\"\n"			\
		".xword 1b\n"						\
		".popsection\n"						\
	:								\
	:								\
	: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",	\
	 "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	 "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE1(name, type1, arg1) {				\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1)						\
	    :								\
	    : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",	\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE2(name, type1, arg1, type2, arg2)  {		\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2)					\
	    :								\
	    : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",		\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE3(name, type1, arg1, type2, arg2, type3, arg3) {	\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("x2") = ((uintptr_t)(arg3));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3)			\
	    :								\
	    : "x3", "x4", "x5", "x6", "x7", "x8", "x9",			\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4) {	\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("x2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("x3") = ((uintptr_t)(arg4));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4)	\
	    :								\
	    : "x4", "x5", "x6", "x7", "x8", "x9",			\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5) {	\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("x2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("x3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("x4") = ((uintptr_t)(arg5));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5)						\
	    :								\
	    : "x5", "x6", "x7", "x8", "x9",				\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE6(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6) {	\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("x2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("x3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("x4") = ((uintptr_t)(arg5));	\
	register uintptr_t _arg6 asm ("x5") = ((uintptr_t)(arg6));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5), "+r"(_arg6)					\
	    :								\
	    : "x6", "x7", "x8", "x9",					\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE7(name, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7) {	\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("x2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("x3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("x4") = ((uintptr_t)(arg5));	\
	register uintptr_t _arg6 asm ("x5") = ((uintptr_t)(arg6));	\
	register uintptr_t _arg7 asm ("x6") = ((uintptr_t)(arg7));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5), "+r"(_arg6), "+r"(_arg7)			\
	    :								\
	    : "x7", "x8", "x9",						\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#define	DTRACE_PROBE8(name, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7, type8, arg8) {	\
	register uintptr_t _arg1 asm ("x0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("x1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("x2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("x3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("x4") = ((uintptr_t)(arg5));	\
	register uintptr_t _arg6 asm ("x5") = ((uintptr_t)(arg6));	\
	register uintptr_t _arg7 asm ("x6") = ((uintptr_t)(arg7));	\
	register uintptr_t _arg8 asm ("x7") = ((uintptr_t)(arg8));	\
	asm volatile (							\
	    "1:\n"							\
	    "bl\t" TO_STRING(__dtrace_probe_##name) "\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".xword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5), "+r"(_arg6), "+r"(_arg7), "+r"(_arg8)	\
	    :								\
	    : "x8", "x9",						\
	    "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",	\
	    "x30", "cc", "memory");					\
}

#elif defined __riscv
#define	DTRACE_PROBE(name)	{					\
	asm volatile (							\
		"1:\n"							\
		"call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"	\
		".pushsection .probepoint, \"aw\"\n"			\
		".dword 1b\n"						\
		".popsection\n"						\
	:								\
	:								\
	: "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",		\
	 "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE1(name, type1, arg1) {				\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1)						\
	    :								\
	    : "a1", "a2", "a3", "a4", "a5", "a6", "a7",			\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE2(name, type1, arg1, type2, arg2)  {		\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2)					\
	    :								\
	    : "a2", "a3", "a4", "a5", "a6", "a7",			\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE3(name, type1, arg1, type2, arg2, type3, arg3) {	\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("a2") = ((uintptr_t)(arg3));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3)			\
	    :								\
	    : "a3", "a4", "a5", "a6", "a7",				\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4) {	\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("a2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("a3") = ((uintptr_t)(arg4));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4)	\
	    :								\
	    : "a4", "a5", "a6", "a7",					\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5) {	\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("a2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("a3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("a4") = ((uintptr_t)(arg5));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5)						\
	    :								\
	    : "a5", "a6", "a7",						\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE6(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6) {	\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("a2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("a3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("a4") = ((uintptr_t)(arg5));	\
	register uintptr_t _arg6 asm ("a5") = ((uintptr_t)(arg6));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5), "+r"(_arg6)					\
	    :								\
	    : "a6", "a7",						\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE7(name, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7) {	\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("a2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("a3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("a4") = ((uintptr_t)(arg5));	\
	register uintptr_t _arg6 asm ("a5") = ((uintptr_t)(arg6));	\
	register uintptr_t _arg7 asm ("a6") = ((uintptr_t)(arg7));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5), "+r"(_arg6), "+r"(_arg7)			\
	    :								\
	    : "a7",							\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#define	DTRACE_PROBE8(name, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7, type8, arg8) {	\
	register uintptr_t _arg1 asm ("a0") = ((uintptr_t)(arg1));	\
	register uintptr_t _arg2 asm ("a1") = ((uintptr_t)(arg2));	\
	register uintptr_t _arg3 asm ("a2") = ((uintptr_t)(arg3));	\
	register uintptr_t _arg4 asm ("a3") = ((uintptr_t)(arg4));	\
	register uintptr_t _arg5 asm ("a4") = ((uintptr_t)(arg5));	\
	register uintptr_t _arg6 asm ("a5") = ((uintptr_t)(arg6));	\
	register uintptr_t _arg7 asm ("a6") = ((uintptr_t)(arg7));	\
	register uintptr_t _arg8 asm ("a7") = ((uintptr_t)(arg8));	\
	asm volatile (							\
	    "1:\n"							\
	    "call\t" TO_STRING(__dtrace_probe_##name) "@plt\n"		\
	    ".pushsection .probepoint, \"aw\"\n"			\
	    ".dword 1b\n"						\
	    ".popsection\n"						\
	    : "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),	\
	      "+r"(_arg5), "+r"(_arg6), "+r"(_arg7), "+r"(_arg8)	\
	    :								\
	    :								\
	     "t0", "t1", "t2", "t3", "t4", "t5", "t6", "ra", "memory");	\
}

#else

#define	DTRACE_PROBE(name)	{					\
	extern void __dtrace_probe_##name(void);			\
	__dtrace_probe_##name();					\
}

#define	DTRACE_PROBE1(name, type1, arg1) {				\
	extern void __dtrace_probe_##name(uintptr_t);			\
	__dtrace_probe_##name((uintptr_t)(arg1));			\
}

#define	DTRACE_PROBE2(name, type1, arg1, type2, arg2) {			\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t);	\
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2));	\
}

#define	DTRACE_PROBE3(name, type1, arg1, type2, arg2, type3, arg3) {	\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t, uintptr_t); \
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2),	\
	    (uintptr_t)(arg3));						\
}

#define	DTRACE_PROBE4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4) {						\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t,		\
	    uintptr_t, uintptr_t);					\
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2),	\
	    (uintptr_t)(arg3), (uintptr_t)(arg4));			\
}

#define	DTRACE_PROBE5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5) {				\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t,		\
	    uintptr_t, uintptr_t, uintptr_t);				\
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2),	\
	    (uintptr_t)(arg3), (uintptr_t)(arg4), (uintptr_t)(arg5));	\
}

#define	DTRACE_PROBE6(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6) {		\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t,		\
	    uintptr_t, uintptr_t, uintptr_t, uintptr_t);		\
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2),	\
	    (uintptr_t)(arg3), (uintptr_t)(arg4), (uintptr_t)(arg5),	\
	    (uintptr_t)(arg6));						\
}

#define	DTRACE_PROBE7(name, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7) {		\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t,		\
	    uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);	\
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2),	\
	    (uintptr_t)(arg3), (uintptr_t)(arg4), (uintptr_t)(arg5),	\
	    (uintptr_t)(arg6), (uintptr_t)(arg7));			\
}

#define	DTRACE_PROBE8(name, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7, type8, arg8) {	\
	extern void __dtrace_probe_##name(uintptr_t, uintptr_t,		\
	    uintptr_t, uintptr_t, uintptr_t, uintptr_t,			\
	    uintptr_t, uintptr_t);					\
	__dtrace_probe_##name((uintptr_t)(arg1), (uintptr_t)(arg2),	\
	    (uintptr_t)(arg3), (uintptr_t)(arg4), (uintptr_t)(arg5),	\
	    (uintptr_t)(arg6), (uintptr_t)(arg7), (uintptr_t)(arg8));	\
}

#endif

#define	DTRACE_SCHED(name)						\
	DTRACE_PROBE(__sched_##name);

#define	DTRACE_SCHED1(name, type1, arg1)				\
	DTRACE_PROBE1(__sched_##name, type1, arg1);

#define	DTRACE_SCHED2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__sched_##name, type1, arg1, type2, arg2);

#define	DTRACE_SCHED3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__sched_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_SCHED4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__sched_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_PROC(name)						\
	DTRACE_PROBE(__proc_##name);

#define	DTRACE_PROC1(name, type1, arg1)					\
	DTRACE_PROBE1(__proc_##name, type1, arg1);

#define	DTRACE_PROC2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__proc_##name, type1, arg1, type2, arg2);

#define	DTRACE_PROC3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__proc_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_PROC4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__proc_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_IO(name)							\
	DTRACE_PROBE(__io_##name);

#define	DTRACE_IO1(name, type1, arg1)					\
	DTRACE_PROBE1(__io_##name, type1, arg1);

#define	DTRACE_IO2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__io_##name, type1, arg1, type2, arg2);

#define	DTRACE_IO3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__io_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_IO4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__io_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_ISCSI_2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__iscsi_##name, type1, arg1, type2, arg2);

#define	DTRACE_ISCSI_3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__iscsi_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_ISCSI_4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__iscsi_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_ISCSI_5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5)				\
	DTRACE_PROBE5(__iscsi_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5);

#define	DTRACE_ISCSI_6(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6)			\
	DTRACE_PROBE6(__iscsi_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6);

#define	DTRACE_ISCSI_7(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6, type7, arg7)	\
	DTRACE_PROBE7(__iscsi_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6,		\
	    type7, arg7);

#define	DTRACE_ISCSI_8(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6,			\
    type7, arg7, type8, arg8)						\
	DTRACE_PROBE8(__iscsi_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6,		\
	    type7, arg7, type8, arg8);

#define	DTRACE_NFSV3_3(name, type1, arg1, type2, arg2,			\
    type3, arg3)							\
	DTRACE_PROBE3(__nfsv3_##name, type1, arg1, type2, arg2,		\
	    type3, arg3);

#define	DTRACE_NFSV3_4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__nfsv3_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_NFSV3_5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5)				\
	DTRACE_PROBE5(__nfsv3_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5);

#define	DTRACE_NFSV4_1(name, type1, arg1) \
	DTRACE_PROBE1(__nfsv4_##name, type1, arg1);

#define	DTRACE_NFSV4_2(name, type1, arg1, type2, arg2) \
	DTRACE_PROBE2(__nfsv4_##name, type1, arg1, type2, arg2);

#define	DTRACE_NFSV4_3(name, type1, arg1, type2, arg2, type3, arg3) \
	DTRACE_PROBE3(__nfsv4_##name, type1, arg1, type2, arg2, type3, arg3);

/*
 * The SMB probes are done a little differently from the other probes
 * in this file for the benefit of the libfksmbsrv USDT provider.
 * See: lib/smbsrv/libfksmbsrv/common/sys/sdt.h
 */
#define	DTRACE_SMB_START(name, type1, arg1) \
	DTRACE_PROBE1(__smb_##name##__start, type1, arg1);
#define	DTRACE_SMB_DONE(name, type1, arg1) \
	DTRACE_PROBE1(__smb_##name##__done, type1, arg1);

#define	DTRACE_SMB2_START(name, type1, arg1) \
	DTRACE_PROBE1(__smb2_##name##__start, type1, arg1);
#define	DTRACE_SMB2_DONE(name, type1, arg1) \
	DTRACE_PROBE1(__smb2_##name##__done, type1, arg1);

#define	DTRACE_IP(name)						\
	DTRACE_PROBE(__ip_##name);

#define	DTRACE_IP1(name, type1, arg1)					\
	DTRACE_PROBE1(__ip_##name, type1, arg1);

#define	DTRACE_IP2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__ip_##name, type1, arg1, type2, arg2);

#define	DTRACE_IP3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__ip_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_IP4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__ip_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_IP5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5)				\
	DTRACE_PROBE5(__ip_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5);

#define	DTRACE_IP6(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6)			\
	DTRACE_PROBE6(__ip_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6);

#define	DTRACE_IP7(name, type1, arg1, type2, arg2, type3, arg3,		\
    type4, arg4, type5, arg5, type6, arg6, type7, arg7)			\
	DTRACE_PROBE7(__ip_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6,		\
	    type7, arg7);

#define	DTRACE_TCP(name)						\
	DTRACE_PROBE(__tcp_##name);

#define	DTRACE_TCP1(name, type1, arg1)					\
	DTRACE_PROBE1(__tcp_##name, type1, arg1);

#define	DTRACE_TCP2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__tcp_##name, type1, arg1, type2, arg2);

#define	DTRACE_TCP3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__tcp_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_TCP4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__tcp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_TCP5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5)				\
	DTRACE_PROBE5(__tcp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5);

#define	DTRACE_TCP6(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5, type6, arg6)			\
	DTRACE_PROBE6(__tcp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6);

#define	DTRACE_UDP(name)						\
	DTRACE_PROBE(__udp_##name);

#define	DTRACE_UDP1(name, type1, arg1)					\
	DTRACE_PROBE1(__udp_##name, type1, arg1);

#define	DTRACE_UDP2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__udp_##name, type1, arg1, type2, arg2);

#define	DTRACE_UDP3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__udp_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_UDP4(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4)						\
	DTRACE_PROBE4(__udp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_UDP5(name, type1, arg1, type2, arg2,			\
    type3, arg3, type4, arg4, type5, arg5)				\
	DTRACE_PROBE5(__udp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5);


#define	DTRACE_SYSEVENT2(name, type1, arg1, type2, arg2)		\
	DTRACE_PROBE2(__sysevent_##name, type1, arg1, type2, arg2);

#define	DTRACE_XPV(name)						\
	DTRACE_PROBE(__xpv_##name);

#define	DTRACE_XPV1(name, type1, arg1)					\
	DTRACE_PROBE1(__xpv_##name, type1, arg1);

#define	DTRACE_XPV2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__xpv_##name, type1, arg1, type2, arg2);

#define	DTRACE_XPV3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__xpv_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_XPV4(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4)						\
	DTRACE_PROBE4(__xpv_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_FC_1(name, type1, arg1) \
	DTRACE_PROBE1(__fc_##name, type1, arg1);

#define	DTRACE_FC_2(name, type1, arg1, type2, arg2) \
	DTRACE_PROBE2(__fc_##name, type1, arg1, type2, arg2);

#define	DTRACE_FC_3(name, type1, arg1, type2, arg2, type3, arg3) \
	DTRACE_PROBE3(__fc_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_FC_4(name, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
	DTRACE_PROBE4(__fc_##name, type1, arg1, type2, arg2, type3, arg3, \
	    type4, arg4);

#define	DTRACE_FC_5(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4, type5, arg5)					\
	DTRACE_PROBE5(__fc_##name, type1, arg1, type2, arg2, type3, arg3, \
	    type4, arg4, type5, arg5);

#define	DTRACE_SRP_1(name, type1, arg1)					\
	DTRACE_PROBE1(__srp_##name, type1, arg1);

#define	DTRACE_SRP_2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__srp_##name, type1, arg1, type2, arg2);

#define	DTRACE_SRP_3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__srp_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_SRP_4(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4)						\
	DTRACE_PROBE4(__srp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4);

#define	DTRACE_SRP_5(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4, type5, arg5)					\
	DTRACE_PROBE5(__srp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5);

#define	DTRACE_SRP_6(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4, type5, arg5, type6, arg6)			\
	DTRACE_PROBE6(__srp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6);

#define	DTRACE_SRP_7(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4, type5, arg5, type6, arg6, type7, arg7)		\
	DTRACE_PROBE7(__srp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6, type7, arg7);

#define	DTRACE_SRP_8(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4, type5, arg5, type6, arg6, type7, arg7, type8, arg8) \
	DTRACE_PROBE8(__srp_##name, type1, arg1, type2, arg2,		\
	    type3, arg3, type4, arg4, type5, arg5, type6, arg6,		\
	    type7, arg7, type8, arg8);

#define	DTRACE_MIB_2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__mib_##name, type1, arg1, type2, arg2);

#define	DTRACE_VTRACE_0(name)						\
	DTRACE_PROBE(__vtrace_##name);

#define	DTRACE_VTRACE_1(name, type1, arg1)				\
	DTRACE_PROBE1(__vtrace_##name, type1, arg1);

#define	DTRACE_VTRACE_2(name, type1, arg1, type2, arg2)			\
	DTRACE_PROBE2(__vtrace_##name, type1, arg1, type2, arg2);

#define	DTRACE_VTRACE_3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__vtrace_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_VTRACE_4(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4)						\
	DTRACE_PROBE4(__vtrace_##name, type1, arg1, type2, arg2,	\
	    type3, arg3, type4, arg4);

#define	DTRACE_VTRACE_5(name, type1, arg1, type2, arg2, type3, arg3,	\
	    type4, arg4, type5, arg5)					\
	DTRACE_PROBE5(__vtrace_##name, type1, arg1, type2, arg2,	\
	    type3, arg3, type4, arg4, type5, arg5);

#define	DTRACE_CPU_3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__cpu_##name, type1, arg1, type2, arg2, type3, arg3);

#define	DTRACE_FSINFO_3(name, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE3(__fsinfo_##name, type1, arg1, type2, arg2, type3, arg3);

/*
 * The set-error SDT probe is extra static, in that we declare its fake
 * function literally, rather than with the DTRACE_PROBE1() macro.  This is
 * necessary so that SET_ERROR() can evaluate to a value, which wouldn't
 * be possible if it required multiple statements (to declare the function
 * and then call it).
 *
 * SET_ERROR() uses the comma operator so that it can be used without much
 * additional code.  For example, "return (EINVAL);" becomes
 * "return (SET_ERROR(EINVAL));".  Note that the argument will be evaluated
 * twice, so it should not have side effects (e.g. something like:
 * "return (SET_ERROR(log_error(EINVAL, info)));" would log the error twice).
 */
#define	SET_ERROR(err) \
    ((DTRACE_PROBE1(set__error, uintptr_t, err)), err)

#endif /* _KERNEL */

extern const char *sdt_prefix;

typedef struct sdt_probedesc {
	char			*sdpd_name;	/* name of this probe */
	unsigned long		sdpd_offset;	/* offset of call in text */
	struct sdt_probedesc	*sdpd_next;	/* next static probe */
} sdt_probedesc_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SDT_H */

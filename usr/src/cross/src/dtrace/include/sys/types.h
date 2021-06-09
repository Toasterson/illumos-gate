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
 * Copyright 2021 Hayashi Naoyuki
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include_next <sys/types.h>

typedef unsigned char   uchar_t;
typedef unsigned short  ushort_t;
typedef unsigned int    uint_t;
typedef unsigned long   ulong_t;
typedef long long longlong_t;
typedef unsigned long long u_longlong_t;

typedef enum { B_FALSE, B_TRUE } boolean_t;
typedef int zoneid_t;
typedef int64_t hrtime_t;

struct utsname
{
};

#define	SIG2STR_MAX	32
#define SEC 1LL
#define MILLISEC 1000LL
#define MICROSEC 1000000LL
#define NANOSEC 1000000000LL
#define __NORETURN

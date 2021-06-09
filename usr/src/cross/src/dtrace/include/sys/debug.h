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

#include <assert.h>

#define ASSERT(x) assert(x)
#define ASSERT3S(x, op, y) assert((int64_t)(x) op (int64_t)(y))
#define ASSERT3U(x, op, y) assert((uint64_t)(x) op (uint64_t)(y))
#define ASSERT3P(x, op, y) assert((uintptr_t)(x) op (uintptr_t)(y))

#define VERIFY(x) do { int _x = (x); assert(_x); } while (0)
#define VERIFY0(x) do { int _x = (x); assert((_x) == 0); } while (0)
#define VERIFY3S(x, op, y) do { int _x = ((int64_t)(x) op (int64_t)(y)); assert(_x); } while (0)
#define VERIFY3U(x, op, y) do { int _x = ((uint64_t)(x) op (uint64_t)(y)); assert(_x); } while (0)
#define VERIFY3P(x, op, y) do { int _x = ((uintptr_t)(x) op (uintptr_t)(y)); assert(_x); } while (0)
#define MUTEX_HELD(x) 1


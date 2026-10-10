/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Flavius Anton
 * Copyright (c) 2016 Mihai Tiganus
 * Copyright (c) 2016-2019 Mihai Carabas
 * Copyright (c) 2017-2019 Darius Mihai
 * Copyright (c) 2017-2019 Elena Mihailescu
 * Copyright (c) 2018-2019 Sergiu Weisz
 * All rights reserved.
 * The bhyve-snapshot feature was developed under sponsorships
 * from Matthew Grooms.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */
/* This file is dual-licensed; see usr/src/contrib/bhyve/LICENSE */

/*
 * Copyright 2026 Till Wegmueller
 */

#ifndef _BHYVE_SNAPSHOT_
#define	_BHYVE_SNAPSHOT_

#include <machine/vmm_snapshot.h>
#include <libnvpair.h>

#include "debug.h"

struct vmctx;

struct restore_state {
	int kdata_fd;
	int vmmem_fd;

	void *kdata_map;
	size_t kdata_len;

	size_t vmmem_len;

	nvlist_t *meta_nvl;
};

typedef int (*vm_snapshot_dev_cb)(struct vm_snapshot_meta *);

void destroy_restore_state(struct restore_state *rstate);

const char *lookup_vmname(struct restore_state *rstate);
size_t lookup_memsize(struct restore_state *rstate);
int lookup_guest_ncpus(struct restore_state *rstate);

void checkpoint_cpu_add(int vcpu);
void checkpoint_cpu_resume(int vcpu);
void checkpoint_cpu_suspend(int vcpu);
void checkpoint_cpu_hold(void);

int load_restore_file(const char *filename, struct restore_state *rstate);
int vm_restore(struct vmctx *ctx, struct restore_state *rstate);

int vm_snapshot_guest2host_addr(struct vmctx *ctx, void **addrp, size_t len,
    bool restore_null, struct vm_snapshot_meta *meta);

/*
 * Address variables are pointers to guest memory.
 *
 * When RNULL != 0, do not enforce invalid address checks; instead, make the
 * pointer NULL at restore time.
 */
#define	SNAPSHOT_GUEST2HOST_ADDR_OR_LEAVE(CTX, ADDR, LEN, RNULL, META, RES, \
    LABEL)								\
do {									\
	(RES) = vm_snapshot_guest2host_addr((CTX), (void **)&(ADDR), (LEN), \
	    (RNULL), (META));						\
	if ((RES) != 0) {						\
		if ((RES) == EFAULT) {					\
			EPRINTLN("%s: invalid address: %s", __func__, #ADDR); \
		}							\
		goto LABEL;						\
	}								\
} while (0)

#endif	/* _BHYVE_SNAPSHOT_ */

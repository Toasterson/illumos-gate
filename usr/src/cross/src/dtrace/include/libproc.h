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

#include <stddef.h>
#include <stdint.h>
#include <gelf.h>
#include <sys/ctf_api.h>

#define	PS_LOST 0
#define	PS_UNDEAD 1

#define	PGRAB_RDONLY 0
#define	PGRAB_FORCE 0

typedef struct {
	int pr_wstat;
} psinfo_t;

typedef struct {
	int pr_pid;
} pstatus_t;

typedef int rd_agent_t;

static inline const psinfo_t *
Ppsinfo(struct ps_prochandle *p)
{
	return NULL;
}
static inline const pstatus_t *
Pstatus(struct ps_prochandle *p)
{
	return NULL;
}
static inline int
Pstate(struct ps_prochandle *p)
{
	return 0;
}
static inline char *
proc_signame(int sig, char *buf, size_t bufsz)
{
	return NULL;
}
static inline int
Plookup_by_addr(struct ps_prochandle *P, uintptr_t addr, char *buf,
    size_t size, GElf_Sym *symp)
{
	return 0;
}

static inline char *
Pobjname(struct ps_prochandle *P, uintptr_t addr,
    char *buffer, size_t bufsize)
{
	return NULL;
}

static inline ctf_file_t *
Pname_to_ctf(struct ps_prochandle *P, const char *name)
{
	return NULL;
}

typedef struct {
	uintptr_t pr_vaddr;
} prmap_t;
typedef int proc_map_f(void *, const prmap_t *, const char *);
typedef ulong_t		Lmid_t;

static inline int
Pobject_iter_resolved(struct ps_prochandle *P, proc_map_f *func, void *cd)
{
	return 1;
}
static inline int
Plmid(struct ps_prochandle *P, uintptr_t addr, Lmid_t *lmidp)
{
	return -1;
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Rob Wing <rew@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _IPC_H_
#define	_IPC_H_

#include <sys/types.h>
#include <sys/linker_set.h>
#include <libnvpair.h>

#define	BHYVE_RUN_DIR	"/var/run/bhyve"

/*
 * bhyve and bhyvectl exchange packed nvlists over a stream socket.  The
 * sender shuts down its side of the connection once the message has been
 * written, so end-of-file marks the end of a message.  A request may carry
 * file descriptors as SCM_RIGHTS ancillary data; ipc_msg_recv() adds any that
 * arrive to the request as an int32 array named IPC_FDS_KEY.  The receiver of
 * those descriptors owns them.
 */
#define	IPC_MSG_MAXLEN	(64 * 1024)
#define	IPC_MAXFDS	4
#define	IPC_FDS_KEY	"fds"

struct vmctx;

struct ipc_command {
	const char *name;
	nvlist_t *(*handler)(struct vmctx *ctx, const nvlist_t *nvl);
};

#define	IPC_COMMAND(name, function)					\
	static struct ipc_command name##_ipc_command = { #name, function }; \
	DATA_SET(ipc_cmd_set, name##_ipc_command)

int ipc_msg_send(int sock, nvlist_t *nvl, const int *fds, uint_t nfds);
int ipc_msg_recv(int sock, nvlist_t **nvlp);

int init_ipc_thread(struct vmctx *ctx, const char *bhyve_run_dir);
void ipc_exit_after_reply(int status);

#endif	/* _IPC_H_ */

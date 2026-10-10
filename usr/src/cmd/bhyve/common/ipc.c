/*
 * Copyright (c) 2016 Flavius Anton
 * Copyright (c) 2016 Mihai Tiganus
 * Copyright (c) 2016-2019 Mihai Carabas
 * Copyright (c) 2017-2019 Darius Mihai
 * Copyright (c) 2017-2019 Elena Mihailescu
 * Copyright (c) 2018-2019 Sergiu Weisz
 * Copyright (c) 2025 Bojan Novković <bnovkov@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "ipc.h"

#include <sys/types.h>
#include <sys/linker_set.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <machine/vmm.h>

#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vmmapi.h>

#include "debug.h"

#define	IPC_COMMAND_FOREACH(pvar, set) SET_FOREACH(pvar, set)

SET_DECLARE(ipc_cmd_set, struct ipc_command);

static struct ipc_thread_ctx {
	struct vmctx *vmctx;
	int sockfd;
} thr_ctx;

/*
 * Set by a command handler which needs bhyve to terminate once its reply has
 * been delivered.
 */
static int ipc_exit_status = -1;

void
ipc_exit_after_reply(int status)
{
	ipc_exit_status = status;
}

static nvlist_t *
handle_message(struct vmctx *ctx, nvlist_t *nvl)
{
	char *cmd;
	struct ipc_command **ipc_cmd;

	if (nvlist_lookup_string(nvl, "cmd", &cmd) != 0)
		return (NULL);

	IPC_COMMAND_FOREACH(ipc_cmd, ipc_cmd_set) {
		if (strcmp(cmd, (*ipc_cmd)->name) == 0)
			return ((*ipc_cmd)->handler(ctx, nvl));
	}

	return (NULL);
}

static void
ipc_close_fds(nvlist_t *nvl)
{
	int32_t *fds;
	uint_t nfds;

	if (nvlist_lookup_int32_array(nvl, IPC_FDS_KEY, &fds, &nfds) != 0)
		return;

	for (uint_t i = 0; i < nfds; i++)
		(void) close(fds[i]);
}

/*
 * Listen for commands from bhyvectl.
 */
static void *
ipc_thread(void *param)
{
	struct ipc_thread_ctx *ctx = param;
	nvlist_t *nvl, *reply;
	char *cmdname;
	int fd, err;

	pthread_set_name_np(pthread_self(), "IPC thread");

	for (;;) {
		fd = accept(ctx->sockfd, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR || errno == ECONNABORTED)
				continue;
			EPRINTLN("%s: accept() failed: %s", __func__,
			    strerror(errno));
			break;
		}

		err = ipc_msg_recv(fd, &nvl);
		if (err != 0) {
			EPRINTLN("%s: failed to receive message: %s", __func__,
			    strerror(err));
			(void) close(fd);
			continue;
		}

		reply = NULL;
		if (nvlist_lookup_string(nvl, "cmd", &cmdname) != 0) {
			if (nvlist_alloc(&reply, NV_UNIQUE_NAME, 0) == 0) {
				(void) nvlist_add_string(reply, "error",
				    "missing command name");
			}
		} else if ((reply = handle_message(ctx->vmctx, nvl)) == NULL) {
			char errmsg[64];

			(void) snprintf(errmsg, sizeof (errmsg),
			    "command '%s' not found", cmdname);
			if (nvlist_alloc(&reply, NV_UNIQUE_NAME, 0) == 0) {
				(void) nvlist_add_string(reply, "error",
				    errmsg);
			}
		}

		/* The handler should set the error message if need be. */
		if (reply != NULL)
			(void) ipc_msg_send(fd, reply, NULL, 0);

		ipc_close_fds(nvl);
		nvlist_free(nvl);
		nvlist_free(reply);
		(void) close(fd);

		if (ipc_exit_status != -1)
			exit(ipc_exit_status);
	}

	return (NULL);
}

/*
 * Create the listening socket for IPC with bhyvectl.
 */
int
init_ipc_thread(struct vmctx *ctx, const char *bhyve_run_dir)
{
	struct sockaddr_un addr;
	pthread_t ipc_pthread;
	mode_t omask;
	int socket_fd;
	int err, ret;

	(void) memset(&addr, 0, sizeof (addr));

	if (mkdir(bhyve_run_dir, 0700) != 0 && errno != EEXIST) {
		err = errno;
		EPRINTLN("Failed to create directory \"%s\": %s",
		    bhyve_run_dir, strerror(err));
		return (err);
	}

	socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		err = errno;
		EPRINTLN("Socket creation failed: %s", strerror(err));
		return (err);
	}

	addr.sun_family = AF_UNIX;

	ret = snprintf(addr.sun_path, sizeof (addr.sun_path), "%s/%s",
	    bhyve_run_dir, vm_get_name(ctx));
	if ((ret < 0) || ((size_t)ret >= sizeof (addr.sun_path))) {
		EPRINTLN("%s: error setting socket path (%d)", __func__, ret);
		(void) close(socket_fd);
		return (ENAMETOOLONG);
	}

	(void) unlink(addr.sun_path);

	omask = umask(S_IRWXG | S_IRWXO);
	ret = bind(socket_fd, (struct sockaddr *)&addr, sizeof (addr));
	(void) umask(omask);
	if (ret != 0) {
		err = errno;
		EPRINTLN("Failed to bind socket \"%s\": %s",
		    addr.sun_path, strerror(err));
		goto fail;
	}

	if (listen(socket_fd, 10) < 0) {
		err = errno;
		EPRINTLN("ipc socket listen: %s", strerror(err));
		goto fail;
	}

	(void) memset(&thr_ctx, 0, sizeof (thr_ctx));
	thr_ctx.vmctx = ctx;
	thr_ctx.sockfd = socket_fd;

	err = pthread_create(&ipc_pthread, NULL, ipc_thread, &thr_ctx);
	if (err != 0)
		goto fail;

	return (0);
fail:
	(void) close(socket_fd);
	(void) unlink(addr.sun_path);

	return (err);
}

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

/*
 * Copyright 2026 Till Wegmueller
 */

/*
 * Message transport for the bhyve control socket, shared by bhyve and
 * bhyvectl.  FreeBSD uses nvlist_send(3) and nvlist_recv(3) from libnv for
 * this; libnvpair has no equivalent, so messages are packed and sent here
 * directly.  See ipc.h for a description of the format.
 */

/* The X/Open socket interfaces are needed for SCM_RIGHTS. */
#define	_XPG4_2
#define	__EXTENSIONS__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libnvpair.h>

#include "ipc.h"

#define	IPC_CMSG_SPACE	(CMSG_SPACE(IPC_MAXFDS * sizeof (int)))

int
ipc_msg_send(int sock, nvlist_t *nvl, const int *fds, uint_t nfds)
{
	union {
		struct cmsghdr	hdr;
		char		buf[IPC_CMSG_SPACE];
	} cmsgbuf;
	struct msghdr msg = { 0 };
	struct iovec iov;
	char *buf = NULL;
	size_t buflen = 0;
	ssize_t n;
	int err;

	if (nfds > IPC_MAXFDS)
		return (EINVAL);

	err = nvlist_pack(nvl, &buf, &buflen, NV_ENCODE_NATIVE, 0);
	if (err != 0)
		return (err);
	if (buflen > IPC_MSG_MAXLEN) {
		free(buf);
		return (E2BIG);
	}

	iov.iov_base = buf;
	iov.iov_len = buflen;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (nfds > 0) {
		struct cmsghdr *cmsg;

		(void) memset(&cmsgbuf, 0, sizeof (cmsgbuf));
		msg.msg_control = cmsgbuf.buf;
		msg.msg_controllen = CMSG_SPACE(nfds * sizeof (int));
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(nfds * sizeof (int));
		(void) memcpy(CMSG_DATA(cmsg), fds, nfds * sizeof (int));
	}

	/* The descriptors travel with the first part of the message. */
	while (iov.iov_len > 0) {
		/* The peer going away must not raise SIGPIPE in bhyve. */
		n = sendmsg(sock, &msg, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			err = errno;
			goto out;
		}
		iov.iov_base = (char *)iov.iov_base + n;
		iov.iov_len -= n;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
	}

	if (shutdown(sock, SHUT_WR) != 0)
		err = errno;
out:
	free(buf);
	return (err);
}

int
ipc_msg_recv(int sock, nvlist_t **nvlp)
{
	union {
		struct cmsghdr	hdr;
		char		buf[IPC_CMSG_SPACE];
	} cmsgbuf;
	int fds[IPC_MAXFDS];
	uint_t nfds = 0;
	nvlist_t *nvl = NULL;
	char *buf;
	size_t len = 0;
	int err = 0;

	if ((buf = malloc(IPC_MSG_MAXLEN)) == NULL)
		return (ENOMEM);

	for (;;) {
		struct msghdr msg = { 0 };
		struct iovec iov;
		struct cmsghdr *cmsg;
		ssize_t n;

		if (len == IPC_MSG_MAXLEN) {
			err = E2BIG;
			goto out;
		}

		iov.iov_base = buf + len;
		iov.iov_len = IPC_MSG_MAXLEN - len;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = cmsgbuf.buf;
		msg.msg_controllen = sizeof (cmsgbuf.buf);

		n = recvmsg(sock, &msg, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			err = errno;
			goto out;
		}

		for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
		    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			uint_t count;

			if (cmsg->cmsg_level != SOL_SOCKET ||
			    cmsg->cmsg_type != SCM_RIGHTS) {
				continue;
			}
			count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof (int);
			for (uint_t i = 0; i < count; i++) {
				int fd;

				(void) memcpy(&fd,
				    CMSG_DATA(cmsg) + i * sizeof (int),
				    sizeof (int));
				if (nfds < IPC_MAXFDS)
					fds[nfds++] = fd;
				else
					(void) close(fd);
			}
		}
		if ((msg.msg_flags & MSG_CTRUNC) != 0) {
			err = EMSGSIZE;
			goto out;
		}

		if (n == 0)
			break;
		len += n;
	}

	if (len == 0) {
		err = ENODATA;
		goto out;
	}

	err = nvlist_unpack(buf, len, &nvl, 0);
	if (err != 0)
		goto out;

	/* Only descriptors which were actually passed may be listed. */
	(void) nvlist_remove_all(nvl, IPC_FDS_KEY);
	if (nfds > 0) {
		err = nvlist_add_int32_array(nvl, IPC_FDS_KEY, fds, nfds);
		if (err != 0)
			goto out;
	}

	*nvlp = nvl;
	nvl = NULL;
	nfds = 0;
out:
	for (uint_t i = 0; i < nfds; i++)
		(void) close(fds[i]);
	nvlist_free(nvl);
	free(buf);
	return (err);
}

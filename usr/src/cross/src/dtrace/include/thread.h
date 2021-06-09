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

#include <pthread.h>

typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;

#define USYNC_THREAD 0
#define LOCK_ERRORCHECK 0
#define MUTEX_HELD(x) 1
#define ERRORCHECKMUTEX PTHREAD_MUTEX_INITIALIZER

static inline int
mutex_init(mutex_t *mutex, int type, void *arg)
{
	return pthread_mutex_init(mutex, NULL);
}

static inline int
mutex_lock(mutex_t *mutex)
{
	return pthread_mutex_lock(mutex);
}

static inline int
mutex_unlock(mutex_t *mutex)
{
	return pthread_mutex_unlock(mutex);
}
static inline int
mutex_enter(mutex_t *mutex)
{
	return pthread_mutex_lock(mutex);
}

static inline int
mutex_exit(mutex_t *mutex)
{
	return pthread_mutex_unlock(mutex);
}

static inline int
mutex_destroy(mutex_t *mutex)
{
	return pthread_mutex_destroy(mutex);
}

static inline int
cond_init(cond_t *cond, int type, void *arg)
{
	return pthread_cond_init(cond, NULL);
}

static inline int
cond_wait(cond_t *cond, mutex_t *mutex)
{
	return pthread_cond_wait(cond, mutex);
}

static inline int
cond_broadcast(cond_t *cond)
{
	return pthread_cond_broadcast(cond);
}

static inline int
cond_destroy(cond_t *cond)
{
	return pthread_cond_destroy(cond);
}

typedef pthread_t thread_t;
static inline int
thr_create(void *stack_base, size_t stack_size,
    void *(*start_routine) (void *), void *arg,
    long flags, thread_t *new_thread)
{
	return pthread_create(new_thread, NULL, start_routine, arg);
}
static inline int
thr_join(thread_t tid, thread_t *departedid, void **status)
{
	return pthread_join(tid, status);
}

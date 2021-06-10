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
 * Copyright 2019 Hayashi Naoyuki
 */

#pragma once

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	SBI_SUCCESS			0
#define	SBI_ERR_FAILURE			-1
#define	SBI_ERR_NOT_SUPPORTED		-2
#define	SBI_ERR_INVALID_PARAM		-3
#define	SBI_ERR_DENIED			-4
#define	SBI_ERR_INVALID_ADDRESS		-5
#define	SBI_ERR_ALREADY_AVAILABLE	-6
#define	SBI_ERR_ALREADY_STARTED		-7
#define	SBI_ERR_ALREADY_STOPPED		-8

#define	SBI_EID_BASE		0x10
#define	SBI_EID_TIME		0x54494D45
#define	SBI_EID_IPI		0x735049
#define	SBI_EID_RFNC		0x52464E43
#define	SBI_EID_HSM		0x48534D
#define	SBI_EID_SRST		0x53525354


struct sbiret {
	long error;
	long value;
};

static inline
struct sbiret sbi_call(
    unsigned long eid, unsigned long fid,
    unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
	register unsigned long a0 asm ("a0") = arg0;
	register unsigned long a1 asm ("a1") = arg1;
	register unsigned long a2 asm ("a2") = arg2;
	register unsigned long a3 asm ("a3") = arg3;
	register unsigned long a4 asm ("a4") = arg4;
	register unsigned long a5 asm ("a5") = arg5;
	register unsigned long a6 asm ("a6") = fid;
	register unsigned long a7 asm ("a7") = eid;
	asm volatile ("ecall"
	    : "+r"(a0), "+r"(a1)
	    : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
	    : "memory");
	struct sbiret ret = {a0, a1};
	return ret;
}

/* Base Extension (EID #0x10) */
#define	SBI_BASE_FID_GET_SPEC_VERSION	0x0
#define	SBI_BASE_FID_GET_IMPL_ID	0x1
#define	SBI_BASE_FID_GET_IMPL_VERSION	0x2
#define	SBI_BASE_FID_PROBE_EXT		0x3
#define	SBI_BASE_FID_GET_MVENDORID	0x4
#define	SBI_BASE_FID_GET_MARCHID	0x5
#define	SBI_BASE_FID_GET_MIMPID		0x6
static inline
struct sbiret sbi_get_spec_version(void)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_get_impl_id(void)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_GET_IMPL_ID, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_get_impl_version(void)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_GET_IMPL_VERSION, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_probe_extension(long extension_id)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_PROBE_EXT, extension_id, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_get_mvendorid(void)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_GET_MVENDORID, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_get_marchid(void)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_GET_MARCHID, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_get_mimpid(void)
{
	return sbi_call(SBI_EID_BASE, SBI_BASE_FID_GET_MIMPID, 0, 0, 0, 0, 0, 0);
}

/* Legacy Extensions */
#define	SBI_EID_CONSOLE_PUTCHAR		1
#define	SBI_EID_CONSOLE_GETCHAR		2
static inline
void sbi_console_putchar(int ch)
{
	sbi_call(SBI_EID_CONSOLE_PUTCHAR, 0, ch, 0, 0, 0, 0, 0);
}
static inline
int sbi_console_getchar(void)
{
	struct sbiret ret = sbi_call(SBI_EID_CONSOLE_GETCHAR, 0, 0, 0, 0, 0, 0, 0);
	return ret.error;
}

/* Timer Extension (EID #0x54494D45 "TIME") */
#define	SBI_TIME_FID_SET_TIMER		0x0
static inline
struct sbiret sbi_set_timer(uint64_t stime_value)
{
	return sbi_call(SBI_EID_TIME, SBI_TIME_FID_SET_TIMER, stime_value, 0, 0, 0, 0, 0);
}

/* IPI Extension (EID #0x735049 "sPI: s-mode IPI") */
#define	SBI_IPI_FID_SEND_IPI		0x0
static inline
struct sbiret sbi_send_ipi(unsigned long hart_mask, unsigned long hart_mask_base)
{
	return sbi_call(SBI_EID_IPI, SBI_IPI_FID_SEND_IPI, hart_mask, hart_mask_base, 0, 0, 0, 0);
}

/* RFENCE Extension (EID #0x52464E43 "RFNC") */
#define	SBI_RFNC_FID_REMOTE_FENCE_I		0x0
#define	SBI_RFNC_FID_REMOTE_SFENCE_VMA		0x1
#define	SBI_RFNC_FID_REMOTE_SFENCE_VMA_ASID	0x2
#define	SBI_RFNC_FID_REMOTE_HFENCE_GVMA		0x3
#define	SBI_RFNC_FID_REMOTE_HFENCE_GVMA_VMID	0x4
#define	SBI_RFNC_FID_REMOTE_HFENCE_VVMA		0x5
#define	SBI_RFNC_FID_REMOTE_HFENCE_VVMA_ASID	0x6
static inline
struct sbiret sbi_remote_fence_i(unsigned long hart_mask, unsigned long hart_mask_base)
{
	return sbi_call(SBI_EID_RFNC, SBI_RFNC_FID_REMOTE_FENCE_I, hart_mask, hart_mask_base, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_remote_sfence_vma(
    unsigned long hart_mask, unsigned long hart_mask_base, unsigned long start_addr, unsigned long size)
{
	return sbi_call(SBI_EID_RFNC, SBI_RFNC_FID_REMOTE_SFENCE_VMA, hart_mask, hart_mask_base, start_addr, size, 0, 0);
}
static inline
struct sbiret sbi_remote_sfence_vma_asid(
    unsigned long hart_mask, unsigned long hart_mask_base, unsigned long start_addr, unsigned long size, unsigned long asid)
{
	return sbi_call(SBI_EID_RFNC, SBI_RFNC_FID_REMOTE_SFENCE_VMA_ASID, hart_mask, hart_mask_base, start_addr, size, asid, 0);
}

/* Hart State Management Extension (EID #0x48534D "HSM") */
#define	SBI_HSM_FID_HART_START		0x0
#define	SBI_HSM_FID_HART_STOP		0x1
#define	SBI_HSM_FID_HART_GET_STATUS	0x2
#define	SBI_HSM_FID_HART_SUSPEND	0x3
/* HSM Hart States */
#define	SBI_HSM_STATE_STARTED		0x0
#define	SBI_HSM_STATE_STOPPED		0x1
#define	SBI_HSM_STATE_START_PENDING	0x2
#define	SBI_HSM_STATE_STOP_PENDING	0x3
#define	SBI_HSM_STATE_SUSPENDED		0x4
#define	SBI_HSM_STATE_SUSPEND_PENDING	0x5
#define	SBI_HSM_STATE_RESUME_PENDING	0x6
static inline
struct sbiret sbi_hart_start(unsigned long hartid, unsigned long start_addr, unsigned long opaque)
{
	return sbi_call(SBI_EID_HSM, SBI_HSM_FID_HART_START, hartid, start_addr, opaque, 0, 0, 0);
}
static inline
struct sbiret sbi_hart_stop(void)
{
	return sbi_call(SBI_EID_HSM, SBI_HSM_FID_HART_STOP, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_hart_get_status(unsigned long hartid)
{
	return sbi_call(SBI_EID_HSM, SBI_HSM_FID_HART_GET_STATUS, 0, 0, 0, 0, 0, 0);
}
static inline
struct sbiret sbi_hart_suspend(uint32_t suspend_type, unsigned long resume_addr, unsigned long opaque)
{
	return sbi_call(SBI_EID_HSM, SBI_HSM_FID_HART_SUSPEND, suspend_type, resume_addr, opaque, 0, 0, 0);
}

/* System Reset Extension (EID #0x53525354 "SRST") */
#define	SBI_SRST_FID_SYSTEM_RESET	0x0
#define	SBI_SRST_RESET_TYPE_SHUTDOWN	0x0
#define	SBI_SRST_RESET_TYPE_COLD_REBOOT	0x1
#define	SBI_SRST_RESET_TYPE_WARM_REBOOT	0x2
#define	SBI_SRST_RESET_REASON_NONE	0x0
#define	SBI_SRST_RESET_REASON_FAILURE	0x1
static inline
struct sbiret sbi_system_reset(uint32_t reset_type, uint32_t reset_reason)
{
	return sbi_call(SBI_EID_SRST, SBI_SRST_FID_SYSTEM_RESET, reset_type, reset_reason, 0, 0, 0, 0);
}

#ifdef __cplusplus
}
#endif

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
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
 *
 * $FreeBSD$
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
 *
 * Copyright 2014 Pluribus Networks Inc.
 * Copyright 2018 Joyent, Inc.
 * Copyright 2020 Oxide Computer Company
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/pcpu.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/x86_archext.h>

#include <machine/clock.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <machine/specialreg.h>

#include <machine/vmm.h>
#include <sys/vmm_kernel.h>

#include "vmm_host.h"
#include "vmm_util.h"

SYSCTL_DECL(_hw_vmm);

#define	CPUID_VM_HIGH		0x40000000

static const char bhyve_id[12] = "bhyve bhyve ";

/* Number of times an unknown cpuid leaf was accessed */
static uint64_t bhyve_xcpuids;

static int cpuid_leaf_b = 1;

/*
 * Force exposition of the invariant TSC capability, regardless of whether the
 * host CPU reports having it.
 */
static int vmm_force_invariant_tsc = 0;

#define	CPUID_0000_0000	(0x0)
#define	CPUID_0000_0001	(0x1)
#define	CPUID_0000_0002	(0x2)
#define	CPUID_0000_0003	(0x3)
#define	CPUID_0000_0004	(0x4)
#define	CPUID_0000_0006	(0x6)
#define	CPUID_0000_0007	(0x7)
#define	CPUID_0000_000A	(0xA)
#define	CPUID_0000_000B	(0xB)
#define	CPUID_0000_000D	(0xD)
#define	CPUID_0000_000F	(0xF)
#define	CPUID_0000_0010	(0x10)
#define	CPUID_0000_0015	(0x15)
#define	CPUID_8000_0000	(0x80000000)
#define	CPUID_8000_0001	(0x80000001)
#define	CPUID_8000_0002	(0x80000002)
#define	CPUID_8000_0003	(0x80000003)
#define	CPUID_8000_0004	(0x80000004)
#define	CPUID_8000_0006	(0x80000006)
#define	CPUID_8000_0007	(0x80000007)
#define	CPUID_8000_0008	(0x80000008)
#define	CPUID_8000_001D	(0x8000001D)
#define	CPUID_8000_001E	(0x8000001E)

/*
 * CPUID instruction Fn0000_0001:
 */
#define	CPUID_0000_0001_APICID_MASK	(0xff<<24)
#define	CPUID_0000_0001_APICID_SHIFT	24

/*
 * CPUID instruction Fn0000_0001 ECX
 */
#define	CPUID_0000_0001_FEAT0_VMX	(1<<5)


/*
 * Round up to the next power of two, if necessary, and then take log2.
 * Returns -1 if argument is zero.
 */
static __inline int
log2(uint_t x)
{

	return (fls(x << (1 - powerof2(x))) - 1);
}

int
x86_emulate_cpuid(struct vm *vm, int vcpu_id, uint64_t *rax, uint64_t *rbx,
    uint64_t *rcx, uint64_t *rdx)
{
	const struct xsave_limits *limits;
	uint64_t cr4;
	int error, enable_invpcid, level, width = 0, x2apic_id = 0;
	unsigned int func, regs[4], logical_cpus = 0, param;
	enum x2apic_state x2apic_state;
	uint16_t cores, maxcpus, sockets, threads;

	/*
	 * The function of CPUID is controlled through the provided value of
	 * %eax (and secondarily %ecx, for certain leaf data).
	 */
	func = (uint32_t)*rax;
	param = (uint32_t)*rcx;

	/*
	 * Requests for invalid CPUID levels should map to the highest
	 * available level instead.
	 */
	if (cpu_exthigh != 0 && func >= 0x80000000) {
		if (func > cpu_exthigh)
			func = cpu_exthigh;
	} else if (func >= 0x40000000) {
		if (func > CPUID_VM_HIGH)
			func = CPUID_VM_HIGH;
	} else if (func > cpu_high) {
		func = cpu_high;
	}

	/*
	 * In general the approach used for CPU topology is to
	 * advertise a flat topology where all CPUs are packages with
	 * no multi-core or SMT.
	 */
	switch (func) {
		/*
		 * Pass these through to the guest
		 */
		case CPUID_0000_0000:
		case CPUID_0000_0002:
		case CPUID_0000_0003:
		case CPUID_8000_0000:
		case CPUID_8000_0002:
		case CPUID_8000_0003:
		case CPUID_8000_0004:
		case CPUID_8000_0006:
			cpuid_count(func, param, regs);
			break;
		case CPUID_8000_0008:
			cpuid_count(func, param, regs);
			if (vmm_is_svm()) {
				/*
				 * As on Intel (0000_0007:0, EDX), mask out
				 * unsupported or unsafe AMD extended features
				 * (8000_0008 EBX).
				 */
				regs[1] &= (AMDFEID_CLZERO | AMDFEID_IRPERF |
				    AMDFEID_XSAVEERPTR);

				vm_get_topology(vm, &sockets, &cores, &threads,
				    &maxcpus);
				/*
				 * Here, width is ApicIdCoreIdSize, present on
				 * at least Family 15h and newer.  It
				 * represents the "number of bits in the
				 * initial apicid that indicate thread id
				 * within a package."
				 *
				 * Our topo_probe_amd() uses it for
				 * pkg_id_shift and other OSes may rely on it.
				 */
				width = MIN(0xF, log2(threads * cores));
				if (width < 0x4)
					width = 0;
				logical_cpus = MIN(0xFF, threads * cores - 1);
				regs[2] = (width << AMDID_COREID_SIZE_SHIFT) |
				    logical_cpus;
			}
			break;

		case CPUID_8000_0001:
			cpuid_count(func, param, regs);

			/*
			 * Hide SVM from guest.
			 */
			regs[2] &= ~AMDID2_SVM;

			/*
			 * Don't advertise extended performance counter MSRs
			 * to the guest.
			 */
			regs[2] &= ~AMDID2_PCXC;
			regs[2] &= ~AMDID2_PNXC;
			regs[2] &= ~AMDID2_PTSCEL2I;

			/*
			 * Don't advertise Instruction Based Sampling feature.
			 */
			regs[2] &= ~AMDID2_IBS;

			/* NodeID MSR not available */
			regs[2] &= ~AMDID2_NODE_ID;

			/* Don't advertise the OS visible workaround feature */
			regs[2] &= ~AMDID2_OSVW;

			/* Hide mwaitx/monitorx capability from the guest */
			regs[2] &= ~AMDID2_MWAITX;

#ifndef __FreeBSD__
			/*
			 * Detection routines for TCE and FFXSR are missing
			 * from our vm_cpuid_capability() detection logic
			 * today.  Mask them out until that is remedied.
			 * They do not appear to be in common usage, so their
			 * absence should not cause undue trouble.
			 */
			regs[2] &= ~AMDID2_TCE;
			regs[3] &= ~AMDID_FFXSR;
#endif

			/*
			 * Hide rdtscp/ia32_tsc_aux until we know how
			 * to deal with them.
			 */
			regs[3] &= ~AMDID_RDTSCP;
			break;

		case CPUID_8000_0007:
			cpuid_count(func, param, regs);
			/*
			 * AMD uses this leaf to advertise the processor's
			 * power monitoring and RAS capabilities. These
			 * features are hardware-specific and exposing
			 * them to a guest doesn't make a lot of sense.
			 *
			 * Intel uses this leaf only to advertise the
			 * "Invariant TSC" feature with all other bits
			 * being reserved (set to zero).
			 */
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;

			/*
			 * If the host system possesses an invariant TSC, then
			 * it is safe to expose to the guest.
			 *
			 * If there is measured skew between host TSCs, it will
			 * be properly offset so guests do not observe any
			 * change between CPU migrations.
			 */
			regs[3] &= AMDPM_TSC_INVARIANT;

			/*
			 * Since illumos avoids deep C-states on CPUs which do
			 * not support an invariant TSC, it may be safe (and
			 * desired) to unconditionally expose that capability to
			 * the guest.
			 */
			if (vmm_force_invariant_tsc != 0) {
				regs[3] |= AMDPM_TSC_INVARIANT;
			}
			break;

		case CPUID_8000_001D:
			/* AMD Cache topology, like 0000_0004 for Intel. */
			if (!vmm_is_svm())
				goto default_leaf;

			/*
			 * Similar to Intel, generate a ficticious cache
			 * topology for the guest with L3 shared by the
			 * package, and L1 and L2 local to a core.
			 */
			vm_get_topology(vm, &sockets, &cores, &threads,
			    &maxcpus);
			switch (param) {
			case 0:
				logical_cpus = threads;
				level = 1;
				func = 1;	/* data cache */
				break;
			case 1:
				logical_cpus = threads;
				level = 2;
				func = 3;	/* unified cache */
				break;
			case 2:
				logical_cpus = threads * cores;
				level = 3;
				func = 3;	/* unified cache */
				break;
			default:
				logical_cpus = 0;
				level = 0;
				func = 0;
				break;
			}

			logical_cpus = MIN(0xfff, logical_cpus - 1);
			regs[0] = (logical_cpus << 14) | (1 << 8) |
			    (level << 5) | func;
			regs[1] = (func > 0) ? (CACHE_LINE_SIZE - 1) : 0;
			regs[2] = 0;
			regs[3] = 0;
			break;

		case CPUID_8000_001E:
			/*
			 * AMD Family 16h+ and Hygon Family 18h additional
			 * identifiers.
			 */
			if (!vmm_is_svm() || CPUID_TO_FAMILY(cpu_id) < 0x16)
				goto default_leaf;

			vm_get_topology(vm, &sockets, &cores, &threads,
			    &maxcpus);
			regs[0] = vcpu_id;
			threads = MIN(0xFF, threads - 1);
			regs[1] = (threads << 8) |
			    (vcpu_id >> log2(threads + 1));
			/*
			 * XXX Bhyve topology cannot yet represent >1 node per
			 * processor.
			 */
			regs[2] = 0;
			regs[3] = 0;
			break;

		case CPUID_0000_0001:
			do_cpuid(1, regs);

			error = vm_get_x2apic_state(vm, vcpu_id, &x2apic_state);
			if (error) {
				panic("x86_emulate_cpuid: error %d "
				    "fetching x2apic state", error);
			}

			/*
			 * Override the APIC ID only in ebx
			 */
			regs[1] &= ~(CPUID_LOCAL_APIC_ID);
			regs[1] |= (vcpu_id << CPUID_0000_0001_APICID_SHIFT);

			/*
			 * Don't expose VMX, SpeedStep, TME or SMX capability.
			 * Advertise x2APIC capability and Hypervisor guest.
			 */
			regs[2] &= ~(CPUID2_VMX | CPUID2_EST | CPUID2_TM2);
			regs[2] &= ~(CPUID2_SMX);

			regs[2] |= CPUID2_HV;

			if (x2apic_state != X2APIC_DISABLED)
				regs[2] |= CPUID2_X2APIC;
			else
				regs[2] &= ~CPUID2_X2APIC;

			/*
			 * Only advertise CPUID2_XSAVE in the guest if
			 * the host is using XSAVE.
			 */
			if (!(regs[2] & CPUID2_OSXSAVE))
				regs[2] &= ~CPUID2_XSAVE;

			/*
			 * If CPUID2_XSAVE is being advertised and the
			 * guest has set CR4_XSAVE, set
			 * CPUID2_OSXSAVE.
			 */
			regs[2] &= ~CPUID2_OSXSAVE;
			if (regs[2] & CPUID2_XSAVE) {
				error = vm_get_register(vm, vcpu_id,
				    VM_REG_GUEST_CR4, &cr4);
				if (error)
					panic("x86_emulate_cpuid: error %d "
					    "fetching %%cr4", error);
				if (cr4 & CR4_XSAVE)
					regs[2] |= CPUID2_OSXSAVE;
			}

			/*
			 * Hide monitor/mwait until we know how to deal with
			 * these instructions.
			 */
			regs[2] &= ~CPUID2_MON;

			/*
			 * Hide the performance and debug features.
			 */
			regs[2] &= ~CPUID2_PDCM;

			/*
			 * No TSC deadline support in the APIC yet
			 */
			regs[2] &= ~CPUID2_TSCDLT;

			/*
			 * Hide thermal monitoring
			 */
			regs[3] &= ~(CPUID_ACPI | CPUID_TM);

			/*
			 * Hide the debug store capability.
			 */
			regs[3] &= ~CPUID_DS;

			/*
			 * Advertise the Machine Check and MTRR capability.
			 *
			 * Some guest OSes (e.g. Windows) will not boot if
			 * these features are absent.
			 */
			regs[3] |= (CPUID_MCA | CPUID_MCE | CPUID_MTRR);

			vm_get_topology(vm, &sockets, &cores, &threads,
			    &maxcpus);
			logical_cpus = threads * cores;
			regs[1] &= ~CPUID_HTT_CORES;
			regs[1] |= (logical_cpus & 0xff) << 16;
			regs[3] |= CPUID_HTT;
			break;

		case CPUID_0000_0004:
			cpuid_count(func, param, regs);

			if (regs[0] || regs[1] || regs[2] || regs[3]) {
				vm_get_topology(vm, &sockets, &cores, &threads,
				    &maxcpus);
				regs[0] &= 0x3ff;
				regs[0] |= (cores - 1) << 26;
				/*
				 * Cache topology:
				 * - L1 and L2 are shared only by the logical
				 *   processors in a single core.
				 * - L3 and above are shared by all logical
				 *   processors in the package.
				 */
				logical_cpus = threads;
				level = (regs[0] >> 5) & 0x7;
				if (level >= 3)
					logical_cpus *= cores;
				regs[0] |= (logical_cpus - 1) << 14;
			}
			break;

		case CPUID_0000_0007:
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;

			/* leaf 0 */
			if (param == 0) {
				cpuid_count(func, param, regs);

				/* Only leaf 0 is supported */
				regs[0] = 0;

				/*
				 * Expose known-safe features.
				 */
				regs[1] &= (CPUID_STDEXT_FSGSBASE |
				    CPUID_STDEXT_BMI1 | CPUID_STDEXT_HLE |
				    CPUID_STDEXT_AVX2 | CPUID_STDEXT_SMEP |
				    CPUID_STDEXT_BMI2 |
				    CPUID_STDEXT_ERMS | CPUID_STDEXT_RTM |
				    CPUID_STDEXT_AVX512F |
				    CPUID_STDEXT_RDSEED |
				    CPUID_STDEXT_SMAP |
				    CPUID_STDEXT_AVX512PF |
				    CPUID_STDEXT_AVX512ER |
				    CPUID_STDEXT_AVX512CD | CPUID_STDEXT_SHA);
				regs[2] = 0;
				regs[3] &= CPUID_STDEXT3_MD_CLEAR;

				/* Advertise INVPCID if it is enabled. */
				error = vm_get_capability(vm, vcpu_id,
				    VM_CAP_ENABLE_INVPCID, &enable_invpcid);
				if (error == 0 && enable_invpcid)
					regs[1] |= CPUID_STDEXT_INVPCID;
			}
			break;

		case CPUID_0000_0006:
			regs[0] = CPUTPM1_ARAT;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;
			break;

		case CPUID_0000_000A:
			/*
			 * Handle the access, but report 0 for
			 * all options
			 */
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;
			break;

		case CPUID_0000_000B:
			/*
			 * Intel processor topology enumeration
			 */
			if (vmm_is_intel()) {
				vm_get_topology(vm, &sockets, &cores, &threads,
				    &maxcpus);
				if (param == 0) {
					logical_cpus = threads;
					width = log2(logical_cpus);
					level = CPUID_TYPE_SMT;
					x2apic_id = vcpu_id;
				}

				if (param == 1) {
					logical_cpus = threads * cores;
					width = log2(logical_cpus);
					level = CPUID_TYPE_CORE;
					x2apic_id = vcpu_id;
				}

				if (!cpuid_leaf_b || param >= 2) {
					width = 0;
					logical_cpus = 0;
					level = 0;
					x2apic_id = 0;
				}

				regs[0] = width & 0x1f;
				regs[1] = logical_cpus & 0xffff;
				regs[2] = (level << 8) | (param & 0xff);
				regs[3] = x2apic_id;
			} else {
				regs[0] = 0;
				regs[1] = 0;
				regs[2] = 0;
				regs[3] = 0;
			}
			break;

		case CPUID_0000_000D:
			limits = vmm_get_xsave_limits();
			if (!limits->xsave_enabled) {
				regs[0] = 0;
				regs[1] = 0;
				regs[2] = 0;
				regs[3] = 0;
				break;
			}

			cpuid_count(func, param, regs);
			switch (param) {
			case 0:
				/*
				 * Only permit the guest to use bits
				 * that are active in the host in
				 * %xcr0.  Also, claim that the
				 * maximum save area size is
				 * equivalent to the host's current
				 * save area size.  Since this runs
				 * "inside" of vmrun(), it runs with
				 * the guest's xcr0, so the current
				 * save area size is correct as-is.
				 */
				regs[0] &= limits->xcr0_allowed;
				regs[2] = limits->xsave_max_size;
				regs[3] &= (limits->xcr0_allowed >> 32);
				break;
			case 1:
				/* Only permit XSAVEOPT. */
				regs[0] &= CPUID_EXTSTATE_XSAVEOPT;
				regs[1] = 0;
				regs[2] = 0;
				regs[3] = 0;
				break;
			default:
				/*
				 * If the leaf is for a permitted feature,
				 * pass through as-is, otherwise return
				 * all zeroes.
				 */
				if (!(limits->xcr0_allowed & (1ul << param))) {
					regs[0] = 0;
					regs[1] = 0;
					regs[2] = 0;
					regs[3] = 0;
				}
				break;
			}
			break;

		case CPUID_0000_000F:
		case CPUID_0000_0010:
			/*
			 * Do not report any Resource Director Technology
			 * capabilities.  Exposing control of cache or memory
			 * controller resource partitioning to the guest is not
			 * at all sensible.
			 *
			 * This is already hidden at a high level by masking of
			 * leaf 0x7.  Even still, a guest may look here for
			 * detailed capability information.
			 */
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;
			break;

		case CPUID_0000_0015:
			/*
			 * Don't report CPU TSC/Crystal ratio and clock
			 * values since guests may use these to derive the
			 * local APIC frequency..
			 */
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;
			break;

		case 0x40000000:
			regs[0] = CPUID_VM_HIGH;
			bcopy(bhyve_id, &regs[1], 4);
			bcopy(bhyve_id + 4, &regs[2], 4);
			bcopy(bhyve_id + 8, &regs[3], 4);
			break;

		default:
default_leaf:
			/*
			 * The leaf value has already been clamped so
			 * simply pass this through, keeping count of
			 * how many unhandled leaf values have been seen.
			 */
			atomic_add_long(&bhyve_xcpuids, 1);
			cpuid_count(func, param, regs);
			break;
	}

	/*
	 * CPUID clears the upper 32-bits of the long-mode registers.
	 */
	*rax = regs[0];
	*rbx = regs[1];
	*rcx = regs[2];
	*rdx = regs[3];

	return (1);
}

/*
 * Return 'true' if the capability 'cap' is enabled in this virtual cpu
 * and 'false' otherwise.
 */
bool
vm_cpuid_capability(struct vm *vm, int vcpuid, enum vm_cpuid_capability cap)
{
	bool rv;

	KASSERT(cap > 0 && cap < VCC_LAST, ("%s: invalid vm_cpu_capability %d",
	    __func__, cap));

	/*
	 * Simply passthrough the capabilities of the host cpu for now.
	 */
	rv = false;
	switch (cap) {
#ifdef __FreeBSD__
	case VCC_NO_EXECUTE:
		if (amd_feature & AMDID_NX)
			rv = true;
		break;
	case VCC_FFXSR:
		if (amd_feature & AMDID_FFXSR)
			rv = true;
		break;
	case VCC_TCE:
		if (amd_feature2 & AMDID2_TCE)
			rv = true;
		break;
#else
	case VCC_NO_EXECUTE:
		if (is_x86_feature(x86_featureset, X86FSET_NX))
			rv = true;
		break;
	/* XXXJOY: No kernel detection for FFXR or TCE at present, so ignore */
	case VCC_FFXSR:
	case VCC_TCE:
		break;
#endif
	default:
		panic("%s: unknown vm_cpu_capability %d", __func__, cap);
	}
	return (rv);
}

bool
validate_guest_xcr0(uint64_t val, uint64_t limit_mask)
{
	/* x87 feature must be enabled */
	if ((val & XFEATURE_ENABLED_X87) == 0) {
		return (false);
	}
	/* AVX cannot be enabled without SSE */
	if ((val & (XFEATURE_ENABLED_SSE | XFEATURE_ENABLED_AVX)) ==
	    XFEATURE_ENABLED_SSE) {
		return (false);
	}
	/* No bits should be outside what we dictate to be allowed */
	if ((val & ~limit_mask) != 0) {
		return (false);
	}

	return (true);
}

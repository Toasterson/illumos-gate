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

/*
 * Checkpoint and restore of a running VM.
 *
 * A checkpoint consists of three files: the guest memory in <file>, the
 * device and vCPU state in <file>.kern and a packed nvlist describing the
 * contents of the other two in <file>.meta.  bhyvectl(8) creates the files
 * and passes their descriptors to bhyve over the IPC socket, so bhyve does
 * not need the privilege to create files once it is running.
 *
 * This is derived from the FreeBSD implementation, with two significant
 * differences.  The metadata is an nvlist rather than JSON, and the state
 * held by the kernel is transferred through the vmm-data interface and the
 * register, FPU and run-state ioctls instead of a dedicated snapshot ioctl.
 * A checkpoint is therefore not interchangeable with one taken on FreeBSD.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/vmm_data.h>
#include <sys/vmm_dev.h>

#include <machine/cpufunc.h>
#include <machine/vmm.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vmmapi.h>

#include "bhyverun.h"
#ifdef __amd64__
#include "atkbdc.h"
#endif
#include "config.h"
#include "debug.h"
#include "ipc.h"
#include "pci_emul.h"
#include "snapshot.h"

#define	KB		(1024UL)
#define	MB		(1024UL * KB)

#define	SNAPSHOT_CHUNK	(4 * MB)

#define	SNAPSHOT_BUFFER_SIZE (40 * MB)

/* Bumped whenever the layout of the saved state changes incompatibly. */
#define	SNAPSHOT_VERSION		1

#define	META_VERSION_KEY		"version"
#define	META_KERNEL_KEY			"kern_structs"
#define	META_DEV_KEY			"devices"
#define	META_BASIC_METADATA_KEY		"basic_metadata"
#define	META_SIZE_KEY			"size"
#define	META_FILE_OFFSET_KEY		"file_offset"

#define	META_NCPUS_KEY			"ncpus"
#define	META_VMNAME_KEY			"vmname"
#define	META_MEMSIZE_KEY		"memsize"

struct vm_snapshot_kern_info {
	const char *struct_name;	/* kernel structure name */
	uint16_t data_class;		/* vmm-data class */
	uint16_t data_version;		/* vmm-data class version */
};

/*
 * The VM-wide state held by the kernel, in the order in which it is restored.
 * The time data comes first since the kernel expresses the timer deadlines of
 * the emulated devices relative to the boot time of the VM.  The per-vCPU
 * state follows these entries.
 */
static const struct vm_snapshot_kern_info snapshot_kern_structs[] = {
	{ "vmm_time",	VDC_VMM_TIME,	1 },
	{ "vioapic",	VDC_IOAPIC,	1 },
	{ "vatpic",	VDC_ATPIC,	1 },
	{ "vatpit",	VDC_ATPIT,	1 },
	{ "vhpet",	VDC_HPET,	1 },
	{ "vpmtmr",	VDC_PM_TIMER,	1 },
	{ "vrtc",	VDC_RTC,	2 },
};

/*
 * vCPU registers transferred with the register-set ioctls.  The segment
 * register entries hold the selectors; the remaining segment state is
 * carried by the descriptors below.
 *
 * VM_REG_GUEST_INTR_SHADOW is not saved since VMX does not allow a vCPU to be
 * placed into an interrupt shadow from userspace.  A vCPU which is
 * checkpointed in a shadow will accept an interrupt one instruction early.
 */
static const int snapshot_vcpu_regs[] = {
	VM_REG_GUEST_RAX,
	VM_REG_GUEST_RBX,
	VM_REG_GUEST_RCX,
	VM_REG_GUEST_RDX,
	VM_REG_GUEST_RSI,
	VM_REG_GUEST_RDI,
	VM_REG_GUEST_RBP,
	VM_REG_GUEST_R8,
	VM_REG_GUEST_R9,
	VM_REG_GUEST_R10,
	VM_REG_GUEST_R11,
	VM_REG_GUEST_R12,
	VM_REG_GUEST_R13,
	VM_REG_GUEST_R14,
	VM_REG_GUEST_R15,
	VM_REG_GUEST_RSP,
	VM_REG_GUEST_RIP,
	VM_REG_GUEST_RFLAGS,
	VM_REG_GUEST_CR0,
	VM_REG_GUEST_CR2,
	VM_REG_GUEST_CR3,
	VM_REG_GUEST_CR4,
	VM_REG_GUEST_XCR0,
	VM_REG_GUEST_EFER,
	VM_REG_GUEST_DR0,
	VM_REG_GUEST_DR1,
	VM_REG_GUEST_DR2,
	VM_REG_GUEST_DR3,
	VM_REG_GUEST_DR6,
	VM_REG_GUEST_DR7,
	VM_REG_GUEST_PDPTE0,
	VM_REG_GUEST_PDPTE1,
	VM_REG_GUEST_PDPTE2,
	VM_REG_GUEST_PDPTE3,
	VM_REG_GUEST_ES,
	VM_REG_GUEST_CS,
	VM_REG_GUEST_SS,
	VM_REG_GUEST_DS,
	VM_REG_GUEST_FS,
	VM_REG_GUEST_GS,
	VM_REG_GUEST_LDTR,
	VM_REG_GUEST_TR,
};

static const int snapshot_vcpu_descs[] = {
	VM_REG_GUEST_ES,
	VM_REG_GUEST_CS,
	VM_REG_GUEST_SS,
	VM_REG_GUEST_DS,
	VM_REG_GUEST_FS,
	VM_REG_GUEST_GS,
	VM_REG_GUEST_LDTR,
	VM_REG_GUEST_TR,
	VM_REG_GUEST_GDTR,
	VM_REG_GUEST_IDTR,
};

static cpuset_t vcpus_active, vcpus_suspended;
static pthread_mutex_t vcpu_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vcpus_idle = PTHREAD_COND_INITIALIZER;
static pthread_cond_t vcpus_can_run = PTHREAD_COND_INITIALIZER;
static bool checkpoint_active;

/*
 * TODO: Harden this function and all of its callers since 'base_str' is a user
 * provided string.
 */
static char *
strcat_extension(const char *base_str, const char *ext)
{
	char *res;

	if (asprintf(&res, "%s%s", base_str, ext) < 0) {
		EPRINTLN("Failed to allocate memory: %s", strerror(errno));
		return (NULL);
	}

	return (res);
}

static int
write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0) {
		ssize_t n = write(fd, p, MIN(len, SNAPSHOT_CHUNK));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return (errno);
		}
		p += n;
		len -= n;
	}

	return (0);
}

static int
read_all(int fd, void *buf, size_t len)
{
	uint8_t *p = buf;

	while (len > 0) {
		ssize_t n = read(fd, p, MIN(len, SNAPSHOT_CHUNK));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return (errno);
		}
		if (n == 0)
			return (EIO);
		p += n;
		len -= n;
	}

	return (0);
}

void
destroy_restore_state(struct restore_state *rstate)
{
	if (rstate == NULL) {
		EPRINTLN("Attempting to destroy NULL restore struct.");
		return;
	}

	if (rstate->kdata_map != MAP_FAILED)
		(void) munmap(rstate->kdata_map, rstate->kdata_len);

	if (rstate->kdata_fd >= 0)
		(void) close(rstate->kdata_fd);
	if (rstate->vmmem_fd >= 0)
		(void) close(rstate->vmmem_fd);

	nvlist_free(rstate->meta_nvl);

	rstate->kdata_map = MAP_FAILED;
	rstate->kdata_fd = -1;
	rstate->vmmem_fd = -1;
	rstate->meta_nvl = NULL;
}

static int
load_vmmem_file(const char *filename, struct restore_state *rstate)
{
	struct stat sb;

	rstate->vmmem_fd = open(filename, O_RDONLY);
	if (rstate->vmmem_fd < 0) {
		EPRINTLN("Failed to open restore file '%s': %s", filename,
		    strerror(errno));
		return (-1);
	}

	if (fstat(rstate->vmmem_fd, &sb) < 0) {
		EPRINTLN("Failed to stat restore file '%s': %s", filename,
		    strerror(errno));
		return (-1);
	}

	if (sb.st_size == 0) {
		EPRINTLN("Restore file '%s' is empty.", filename);
		return (-1);
	}

	rstate->vmmem_len = sb.st_size;

	return (0);
}

static int
load_kdata_file(const char *filename, struct restore_state *rstate)
{
	struct stat sb;

	rstate->kdata_fd = open(filename, O_RDONLY);
	if (rstate->kdata_fd < 0) {
		EPRINTLN("Failed to open kernel data file '%s': %s", filename,
		    strerror(errno));
		return (-1);
	}

	if (fstat(rstate->kdata_fd, &sb) < 0) {
		EPRINTLN("Failed to stat kernel data file '%s': %s", filename,
		    strerror(errno));
		return (-1);
	}

	if (sb.st_size == 0) {
		EPRINTLN("Kernel data file '%s' is empty.", filename);
		return (-1);
	}

	rstate->kdata_len = sb.st_size;
	rstate->kdata_map = mmap(NULL, rstate->kdata_len, PROT_READ,
	    MAP_PRIVATE, rstate->kdata_fd, 0);
	if (rstate->kdata_map == MAP_FAILED) {
		EPRINTLN("Failed to map kernel data file '%s': %s", filename,
		    strerror(errno));
		return (-1);
	}

	return (0);
}

static int
load_metadata_file(const char *filename, struct restore_state *rstate)
{
	struct stat sb;
	char *buf = NULL;
	uint32_t version;
	int fd, err;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		EPRINTLN("Failed to open metadata file '%s': %s", filename,
		    strerror(errno));
		return (-1);
	}

	if (fstat(fd, &sb) < 0 || sb.st_size == 0) {
		EPRINTLN("Failed to read metadata file '%s'", filename);
		goto fail;
	}

	if ((buf = malloc(sb.st_size)) == NULL) {
		EPRINTLN("Failed to allocate memory: %s", strerror(errno));
		goto fail;
	}

	err = read_all(fd, buf, sb.st_size);
	if (err != 0) {
		EPRINTLN("Failed to read metadata file '%s': %s", filename,
		    strerror(err));
		goto fail;
	}

	err = nvlist_unpack(buf, sb.st_size, &rstate->meta_nvl, 0);
	if (err != 0) {
		EPRINTLN("Failed to parse metadata file '%s': %s", filename,
		    strerror(err));
		goto fail;
	}

	if (nvlist_lookup_uint32(rstate->meta_nvl, META_VERSION_KEY,
	    &version) != 0 || version != SNAPSHOT_VERSION) {
		EPRINTLN("Unsupported checkpoint version in '%s'", filename);
		goto fail;
	}

	free(buf);
	(void) close(fd);
	return (0);

fail:
	free(buf);
	(void) close(fd);
	return (-1);
}

int
load_restore_file(const char *filename, struct restore_state *rstate)
{
	char *kdata_filename = NULL, *meta_filename = NULL;
	int err = -1;

	assert(filename != NULL);
	assert(rstate != NULL);

	(void) memset(rstate, 0, sizeof (*rstate));
	rstate->kdata_fd = -1;
	rstate->vmmem_fd = -1;
	rstate->kdata_map = MAP_FAILED;

	if (load_vmmem_file(filename, rstate) != 0) {
		EPRINTLN("Failed to load guest RAM file.");
		goto done;
	}

	kdata_filename = strcat_extension(filename, ".kern");
	if (kdata_filename == NULL)
		goto done;

	if (load_kdata_file(kdata_filename, rstate) != 0) {
		EPRINTLN("Failed to load guest kernel data file.");
		goto done;
	}

	meta_filename = strcat_extension(filename, ".meta");
	if (meta_filename == NULL)
		goto done;

	if (load_metadata_file(meta_filename, rstate) != 0) {
		EPRINTLN("Failed to load guest metadata file.");
		goto done;
	}

	err = 0;
done:
	if (err != 0)
		destroy_restore_state(rstate);
	free(kdata_filename);
	free(meta_filename);
	return (err);
}

/*
 * Find the saved state of the device or kernel structure 'dev_name' in the
 * list 'key' of the metadata.
 */
static void *
lookup_dev(const char *dev_name, const char *key, struct restore_state *rstate,
    size_t *data_size)
{
	nvlist_t *devs, *dev;
	uint64_t size, file_offset;

	if (nvlist_lookup_nvlist(rstate->meta_nvl, key, &devs) != 0) {
		EPRINTLN("Failed to find '%s' object.", key);
		return (NULL);
	}

	if (nvlist_lookup_nvlist(devs, dev_name, &dev) != 0)
		return (NULL);

	if (nvlist_lookup_uint64(dev, META_SIZE_KEY, &size) != 0 ||
	    nvlist_lookup_uint64(dev, META_FILE_OFFSET_KEY,
	    &file_offset) != 0) {
		EPRINTLN("Malformed metadata for '%s'", dev_name);
		return (NULL);
	}

	if (file_offset > rstate->kdata_len ||
	    size > rstate->kdata_len - file_offset) {
		EPRINTLN("Saved state of '%s' exceeds the kernel data file",
		    dev_name);
		return (NULL);
	}

	*data_size = (size_t)size;
	return ((uint8_t *)rstate->kdata_map + file_offset);
}

static nvlist_t *
lookup_basic_metadata_object(struct restore_state *rstate)
{
	nvlist_t *basic_meta_nvl;

	if (nvlist_lookup_nvlist(rstate->meta_nvl, META_BASIC_METADATA_KEY,
	    &basic_meta_nvl) != 0) {
		EPRINTLN("Failed to find '%s' object.",
		    META_BASIC_METADATA_KEY);
		return (NULL);
	}

	return (basic_meta_nvl);
}

const char *
lookup_vmname(struct restore_state *rstate)
{
	nvlist_t *nvl;
	char *vmname;

	if ((nvl = lookup_basic_metadata_object(rstate)) == NULL)
		return (NULL);

	if (nvlist_lookup_string(nvl, META_VMNAME_KEY, &vmname) != 0)
		return (NULL);

	return (vmname);
}

size_t
lookup_memsize(struct restore_state *rstate)
{
	nvlist_t *nvl;
	uint64_t memsize;

	if ((nvl = lookup_basic_metadata_object(rstate)) == NULL)
		return (0);

	if (nvlist_lookup_uint64(nvl, META_MEMSIZE_KEY, &memsize) != 0)
		return (0);

	return ((size_t)memsize);
}

int
lookup_guest_ncpus(struct restore_state *rstate)
{
	nvlist_t *nvl;
	int32_t ncpus;

	if ((nvl = lookup_basic_metadata_object(rstate)) == NULL)
		return (0);

	if (nvlist_lookup_int32(nvl, META_NCPUS_KEY, &ncpus) != 0)
		return (0);

	return (ncpus);
}

static int
vm_snapshot_mem_part(int snapfd, off_t foff, void *addr, size_t len,
    bool op_wr)
{
	if (lseek(snapfd, foff, SEEK_SET) < 0)
		return (errno);

	if (op_wr)
		return (write_all(snapfd, addr, len));
	else
		return (read_all(snapfd, addr, len));
}

/*
 * Save or restore the guest memory.  The low memory segment is followed by
 * the high memory segment, if any, in the file.
 */
static int
vm_snapshot_mem(struct vmctx *ctx, int snapfd, bool op_wr)
{
	size_t lowmem, highmem;
	void *lowaddr, *highaddr;
	int err;

	lowmem = vm_get_lowmem_size(ctx);
	highmem = vm_get_highmem_size(ctx);

	lowaddr = vm_map_gpa(ctx, 0, lowmem);
	if (lowaddr == NULL)
		return (EFAULT);

	err = vm_snapshot_mem_part(snapfd, 0, lowaddr, lowmem, op_wr);
	if (err != 0) {
		EPRINTLN("%s: Could not %s lowmem: %s", __func__,
		    op_wr ? "write" : "read", strerror(err));
		return (err);
	}

	if (highmem == 0)
		return (0);

	highaddr = vm_map_gpa(ctx, vm_get_highmem_base(ctx), highmem);
	if (highaddr == NULL)
		return (EFAULT);

	err = vm_snapshot_mem_part(snapfd, lowmem, highaddr, highmem, op_wr);
	if (err != 0) {
		EPRINTLN("%s: Could not %s highmem: %s", __func__,
		    op_wr ? "write" : "read", strerror(err));
		return (err);
	}

	return (0);
}

static int
restore_vm_mem(struct vmctx *ctx, struct restore_state *rstate)
{
	size_t memsize;

	memsize = vm_get_lowmem_size(ctx) + vm_get_highmem_size(ctx);
	if (rstate->vmmem_len != memsize) {
		EPRINTLN("%s: mem size mismatch: %zu vs %zu", __func__,
		    rstate->vmmem_len, memsize);
		return (-1);
	}

	return (vm_snapshot_mem(ctx, rstate->vmmem_fd, false) == 0 ? 0 : -1);
}

static bool
host_is_svm(void)
{
	uint_t regs[4];
	char vendor[13];

	do_cpuid(0, regs);
	(void) memcpy(&vendor[0], &regs[1], sizeof (uint_t));
	(void) memcpy(&vendor[4], &regs[3], sizeof (uint_t));
	(void) memcpy(&vendor[8], &regs[2], sizeof (uint_t));
	vendor[12] = '\0';

	return (strcmp(vendor, "AuthenticAMD") == 0 ||
	    strcmp(vendor, "HygonGenuine") == 0);
}

/*
 * Save or restore one item of vmm-data.  The class and version are recorded
 * so that a mismatch is detected on restore, followed by the length of the
 * data, which varies for some classes.
 */
static int
vm_snapshot_vmm_data(struct vmctx *ctx, int vcpuid, uint16_t data_class,
    uint16_t data_version, struct vm_snapshot_meta *meta)
{
	uint8_t data[VM_DATA_XFER_LIMIT];
	struct vm_data_xfer xfer = {
		.vdx_vcpuid = vcpuid,
		.vdx_class = data_class,
		.vdx_version = data_version,
		.vdx_data = data,
	};
	uint32_t len = 0;
	int ret;

	SNAPSHOT_VAR_CMP_OR_LEAVE(data_class, meta, ret, done);
	SNAPSHOT_VAR_CMP_OR_LEAVE(data_version, meta, ret, done);

	if (meta->op == VM_SNAPSHOT_SAVE) {
		xfer.vdx_len = sizeof (data);
		ret = vm_data_read(ctx, &xfer);
		if (ret != 0) {
			EPRINTLN("%s: failed to read class %u for vCPU %d: %s",
			    __func__, data_class, vcpuid, strerror(ret));
			goto done;
		}
		len = xfer.vdx_result_len;
	}

	SNAPSHOT_VAR_OR_LEAVE(len, meta, ret, done);
	if (len > sizeof (data)) {
		ret = EINVAL;
		goto done;
	}
	SNAPSHOT_BUF_OR_LEAVE(data, len, meta, ret, done);

	if (meta->op == VM_SNAPSHOT_RESTORE) {
		xfer.vdx_len = len;
		ret = vm_data_write(ctx, &xfer);
		if (ret != 0) {
			EPRINTLN("%s: failed to write class %u for vCPU %d: %s",
			    __func__, data_class, vcpuid, strerror(ret));
		}
	}

done:
	return (ret);
}

/*
 * The guest does not observe the time for which it was suspended: its TSC
 * resumes from the saved value and its boot time is moved forward by the
 * time that has passed on the host.
 */
static int
vm_snapshot_vmm_time(struct vmctx *ctx, struct vm_snapshot_meta *meta)
{
	struct vdi_time_info_v1 time_info;
	struct vm_data_xfer xfer = {
		.vdx_vcpuid = -1,
		.vdx_class = VDC_VMM_TIME,
		.vdx_version = 1,
		.vdx_len = sizeof (time_info),
		.vdx_data = &time_info,
	};
	int ret;

	if (meta->op == VM_SNAPSHOT_SAVE) {
		ret = vm_data_read(ctx, &xfer);
		if (ret != 0) {
			EPRINTLN("%s: failed to read time data: %s", __func__,
			    strerror(ret));
			goto done;
		}
	}

	SNAPSHOT_VAR_OR_LEAVE(time_info, meta, ret, done);

	if (meta->op == VM_SNAPSHOT_RESTORE) {
		hrtime_t now = gethrtime();

		time_info.vt_boot_hrtime += now - time_info.vt_hrtime;
		time_info.vt_hrtime = now;

		ret = vm_data_write(ctx, &xfer);
		if (ret != 0) {
			EPRINTLN("%s: failed to write time data: %s", __func__,
			    strerror(ret));
		}
	}

done:
	return (ret);
}

static int
vm_snapshot_vcpu(struct vmctx *ctx, struct vcpu *vcpu,
    struct vm_snapshot_meta *meta)
{
	uint64_t regs[nitems(snapshot_vcpu_regs)];
	struct seg_desc descs[nitems(snapshot_vcpu_descs)];
	enum vcpu_run_state run_state;
	uint8_t sipi_vector;
	void *fpu = NULL;
	size_t fpu_len;
	const int vcpuid = vcpu_id(vcpu);
	int ret;

	if (meta->op == VM_SNAPSHOT_SAVE) {
		if (vm_get_register_set(vcpu, nitems(snapshot_vcpu_regs),
		    snapshot_vcpu_regs, regs) != 0) {
			ret = errno;
			goto fail;
		}
		for (uint_t i = 0; i < nitems(snapshot_vcpu_descs); i++) {
			if (vm_get_desc(vcpu, snapshot_vcpu_descs[i],
			    &descs[i].base, &descs[i].limit,
			    &descs[i].access) != 0) {
				ret = errno;
				goto fail;
			}
		}
		ret = vm_get_run_state(vcpu, &run_state, &sipi_vector);
		if (ret != 0)
			goto fail;
	}

	SNAPSHOT_BUF_OR_LEAVE(regs, sizeof (regs), meta, ret, done);
	SNAPSHOT_BUF_OR_LEAVE(descs, sizeof (descs), meta, ret, done);
	SNAPSHOT_VAR_OR_LEAVE(run_state, meta, ret, done);
	SNAPSHOT_VAR_OR_LEAVE(sipi_vector, meta, ret, done);

	if (meta->op == VM_SNAPSHOT_RESTORE) {
		if (vm_set_register_set(vcpu, nitems(snapshot_vcpu_regs),
		    snapshot_vcpu_regs, regs) != 0) {
			ret = errno;
			goto fail;
		}
		for (uint_t i = 0; i < nitems(snapshot_vcpu_descs); i++) {
			if (vm_set_desc(vcpu, snapshot_vcpu_descs[i],
			    descs[i].base, descs[i].limit,
			    descs[i].access) != 0) {
				ret = errno;
				goto fail;
			}
		}
		ret = vm_set_run_state(vcpu, run_state, sipi_vector);
		if (ret != 0)
			goto fail;
	}

	/*
	 * The layout of the FPU state depends on the features of the host, so
	 * it can only be restored on a host with the same layout.
	 */
	ret = vm_get_fpu_size(ctx, &fpu_len);
	if (ret != 0)
		goto fail;
	if ((fpu = malloc(fpu_len)) == NULL) {
		ret = ENOMEM;
		goto fail;
	}
	if (meta->op == VM_SNAPSHOT_SAVE) {
		ret = vm_get_fpu(vcpu, fpu, fpu_len);
		if (ret != 0)
			goto fail;
	}
	SNAPSHOT_VAR_CMP_OR_LEAVE(fpu_len, meta, ret, done);
	SNAPSHOT_BUF_OR_LEAVE(fpu, fpu_len, meta, ret, done);
	if (meta->op == VM_SNAPSHOT_RESTORE) {
		ret = vm_set_fpu(vcpu, fpu, fpu_len);
		if (ret != 0)
			goto fail;
	}

	ret = vm_snapshot_vmm_data(ctx, vcpuid, VDC_MSR, 1, meta);
	if (ret != 0)
		goto done;
	ret = vm_snapshot_vmm_data(ctx, vcpuid, VDC_LAPIC, 1, meta);
	if (ret != 0)
		goto done;
	ret = vm_snapshot_vmm_data(ctx, vcpuid, VDC_VMM_ARCH, 1, meta);
	if (ret != 0)
		goto done;
	if (host_is_svm())
		ret = vm_snapshot_vmm_data(ctx, vcpuid, VDC_PMU_AMD, 1, meta);
	goto done;

fail:
	EPRINTLN("%s: failed to %s state of vCPU %d: %s", __func__,
	    meta->op == VM_SNAPSHOT_SAVE ? "save" : "restore", vcpuid,
	    strerror(ret));
done:
	free(fpu);
	return (ret);
}

static uint_t
vm_snapshot_kern_count(void)
{
	return (nitems(snapshot_kern_structs) + guest_ncpus);
}

static void
vm_snapshot_kern_name(uint_t idx, char *buf, size_t len)
{
	if (idx < nitems(snapshot_kern_structs)) {
		(void) strlcpy(buf, snapshot_kern_structs[idx].struct_name,
		    len);
	} else {
		(void) snprintf(buf, len, "vcpu%u",
		    idx - (uint_t)nitems(snapshot_kern_structs));
	}
}

static int
vm_snapshot_kern_struct(struct vmctx *ctx, uint_t idx,
    struct vm_snapshot_meta *meta)
{
	const struct vm_snapshot_kern_info *info;

	if (idx >= nitems(snapshot_kern_structs)) {
		idx -= nitems(snapshot_kern_structs);
		return (vm_snapshot_vcpu(ctx, fbsdrun_vcpu(idx), meta));
	}

	info = &snapshot_kern_structs[idx];
	if (info->data_class == VDC_VMM_TIME)
		return (vm_snapshot_vmm_time(ctx, meta));

	return (vm_snapshot_vmm_data(ctx, -1, info->data_class,
	    info->data_version, meta));
}

static int
vm_restore_kern_structs(struct vmctx *ctx, struct restore_state *rstate)
{
	for (uint_t i = 0; i < vm_snapshot_kern_count(); i++) {
		char name[32];
		void *data;
		size_t size;

		vm_snapshot_kern_name(i, name, sizeof (name));
		data = lookup_dev(name, META_KERNEL_KEY, rstate, &size);
		if (data == NULL) {
			EPRINTLN("Cannot find kern struct %s", name);
			return (-1);
		}

		if (size == 0) {
			EPRINTLN("data with zero size for %s", name);
			return (-1);
		}

		struct vm_snapshot_meta meta = {
			.dev_name = name,

			.buffer.buf_start = data,
			.buffer.buf_size = size,

			.buffer.buf = data,
			.buffer.buf_rem = size,

			.op = VM_SNAPSHOT_RESTORE,
		};

		if (vm_snapshot_kern_struct(ctx, i, &meta) != 0) {
			EPRINTLN("Failed to restore %s", name);
			return (-1);
		}
	}

	return (0);
}

static int
vm_restore_device(struct restore_state *rstate, vm_snapshot_dev_cb func,
    const char *name, void *data)
{
	void *dev_ptr;
	size_t dev_size;
	int ret;

	dev_ptr = lookup_dev(name, META_DEV_KEY, rstate, &dev_size);

	if (dev_ptr == NULL) {
		EPRINTLN("Failed to lookup dev: %s", name);
		return (EINVAL);
	}

	if (dev_size == 0) {
		EPRINTLN("Restore device size is 0: %s", name);
		return (EINVAL);
	}

	struct vm_snapshot_meta meta = {
		.dev_name = name,
		.dev_data = data,

		.buffer.buf_start = dev_ptr,
		.buffer.buf_size = dev_size,

		.buffer.buf = dev_ptr,
		.buffer.buf_rem = dev_size,

		.op = VM_SNAPSHOT_RESTORE,
	};

	ret = func(&meta);
	if (ret != 0) {
		EPRINTLN("Failed to restore dev: %s %d", name, ret);
		return (ret);
	}

	return (0);
}

/*
 * Every device in the checkpoint must be present in the VM being restored;
 * the converse is checked when the devices are restored.
 */
static int
vm_restore_check_devices(struct restore_state *rstate)
{
	nvlist_t *devs;
	nvpair_t *nvp = NULL;

	if (nvlist_lookup_nvlist(rstate->meta_nvl, META_DEV_KEY, &devs) != 0) {
		EPRINTLN("Failed to find '%s' object.", META_DEV_KEY);
		return (-1);
	}

	while ((nvp = nvlist_next_nvpair(devs, nvp)) != NULL) {
		const char *name = nvpair_name(nvp);
		struct pci_devinst *pdi = NULL;
		bool found = strcmp(name, "atkbdc") == 0;

		while (!found && (pdi = pci_next(pdi)) != NULL)
			found = strcmp(pdi->pi_name, name) == 0;

		if (!found) {
			EPRINTLN("Device %s of the checkpoint is not configured",
			    name);
			return (-1);
		}
	}

	return (0);
}

static int
vm_restore_devices(struct restore_state *rstate)
{
	int ret;
	struct pci_devinst *pdi = NULL;

	while ((pdi = pci_next(pdi)) != NULL)
		pci_snapshot_disable_decoding(pdi);

	while ((pdi = pci_next(pdi)) != NULL) {
		ret = vm_restore_device(rstate, pci_snapshot, pdi->pi_name,
		    pdi);
		if (ret)
			return (ret);
	}

#ifdef __amd64__
	ret = vm_restore_device(rstate, atkbdc_snapshot, "atkbdc", NULL);
#else
	ret = 0;
#endif
	return (ret);
}

static int
vm_pause_devices(void)
{
	int ret;
	struct pci_devinst *pdi = NULL;

	while ((pdi = pci_next(pdi)) != NULL) {
		ret = pci_pause(pdi);
		if (ret) {
			EPRINTLN("Cannot pause dev %s: %d", pdi->pi_name, ret);
			return (ret);
		}
	}

	return (0);
}

static int
vm_resume_devices(void)
{
	int ret;
	struct pci_devinst *pdi = NULL;

	while ((pdi = pci_next(pdi)) != NULL) {
		ret = pci_resume(pdi);
		if (ret) {
			EPRINTLN("Cannot resume '%s': %d", pdi->pi_name, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * Write the saved state in 'meta' to the data file and record its location
 * under 'name' in 'list'.
 */
static int
vm_snapshot_dev_write_data(int data_fd, nvlist_t *list, const char *name,
    struct vm_snapshot_meta *meta, off_t *offset)
{
	nvlist_t *nvl;
	size_t data_size;
	int ret;

	data_size = vm_get_snapshot_size(meta);

	ret = write_all(data_fd, meta->buffer.buf_start, data_size);
	if (ret != 0) {
		EPRINTLN("Failed to write all snapshotted data: %s",
		    strerror(ret));
		return (ret);
	}

	if ((ret = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0)
		return (ret);
	if ((ret = nvlist_add_uint64(nvl, META_SIZE_KEY, data_size)) != 0 ||
	    (ret = nvlist_add_uint64(nvl, META_FILE_OFFSET_KEY,
	    *offset)) != 0 ||
	    (ret = nvlist_add_nvlist(list, name, nvl)) != 0) {
		nvlist_free(nvl);
		return (ret);
	}
	nvlist_free(nvl);

	*offset += data_size;

	return (0);
}

static void
vm_snapshot_meta_reset(struct vm_snapshot_meta *meta, const char *name,
    void *data)
{
	(void) memset(meta->buffer.buf_start, 0, meta->buffer.buf_size);
	meta->buffer.buf = meta->buffer.buf_start;
	meta->buffer.buf_rem = meta->buffer.buf_size;
	meta->dev_name = name;
	meta->dev_data = data;
}

static int
vm_save_kern_structs(struct vmctx *ctx, int data_fd, nvlist_t *list,
    struct vm_snapshot_meta *meta, off_t *offset)
{
	int ret;

	for (uint_t i = 0; i < vm_snapshot_kern_count(); i++) {
		char name[32];

		vm_snapshot_kern_name(i, name, sizeof (name));
		vm_snapshot_meta_reset(meta, name, NULL);

		ret = vm_snapshot_kern_struct(ctx, i, meta);
		if (ret != 0) {
			EPRINTLN("%s: Failed to snapshot struct %s", __func__,
			    name);
			return (ret);
		}

		ret = vm_snapshot_dev_write_data(data_fd, list, name, meta,
		    offset);
		if (ret != 0)
			return (ret);
	}

	return (0);
}

static int
vm_snapshot_basic_metadata(struct vmctx *ctx, nvlist_t *nvl)
{
	nvlist_t *basic;
	size_t memsize;
	int ret;

	memsize = vm_get_lowmem_size(ctx) + vm_get_highmem_size(ctx);

	if ((ret = nvlist_alloc(&basic, NV_UNIQUE_NAME, 0)) != 0)
		return (ret);

	if ((ret = nvlist_add_int32(basic, META_NCPUS_KEY, guest_ncpus)) != 0 ||
	    (ret = nvlist_add_string(basic, META_VMNAME_KEY,
	    vm_get_name(ctx))) != 0 ||
	    (ret = nvlist_add_uint64(basic, META_MEMSIZE_KEY, memsize)) != 0 ||
	    (ret = nvlist_add_nvlist(nvl, META_BASIC_METADATA_KEY,
	    basic)) != 0) {
		nvlist_free(basic);
		return (ret);
	}

	nvlist_free(basic);
	return (0);
}

static int
vm_snapshot_device(vm_snapshot_dev_cb func, const char *dev_name,
    void *devdata, int data_fd, nvlist_t *list,
    struct vm_snapshot_meta *meta, off_t *offset)
{
	int ret;

	vm_snapshot_meta_reset(meta, dev_name, devdata);

	ret = func(meta);
	if (ret != 0) {
		EPRINTLN("Failed to snapshot %s; ret=%d", dev_name, ret);
		return (ret);
	}

	return (vm_snapshot_dev_write_data(data_fd, list, dev_name, meta,
	    offset));
}

static int
vm_snapshot_devices(int data_fd, nvlist_t *list, struct vm_snapshot_meta *meta,
    off_t *offset)
{
	struct pci_devinst *pdi;
	int ret;

	/* Save PCI devices */
	pdi = NULL;
	while ((pdi = pci_next(pdi)) != NULL) {
		ret = vm_snapshot_device(pci_snapshot, pdi->pi_name, pdi,
		    data_fd, list, meta, offset);
		if (ret != 0)
			return (ret);
	}

#ifdef __amd64__
	ret = vm_snapshot_device(atkbdc_snapshot, "atkbdc", NULL,
	    data_fd, list, meta, offset);
#else
	ret = 0;
#endif

	return (ret);
}

/*
 * Find a device which cannot be saved, so that a checkpoint can be refused
 * before the VM is stopped.
 */
static struct pci_devinst *
vm_snapshot_unsupported_dev(void)
{
	struct pci_devinst *pdi = NULL;

	while ((pdi = pci_next(pdi)) != NULL) {
		if (pdi->pi_d->pe_snapshot == NULL)
			return (pdi);
	}

	return (NULL);
}

void
checkpoint_cpu_add(int vcpu)
{

	pthread_mutex_lock(&vcpu_lock);
	CPU_SET(vcpu, &vcpus_active);

	if (checkpoint_active) {
		CPU_SET(vcpu, &vcpus_suspended);
		while (checkpoint_active)
			pthread_cond_wait(&vcpus_can_run, &vcpu_lock);
		CPU_CLR(vcpu, &vcpus_suspended);
	}
	pthread_mutex_unlock(&vcpu_lock);
}

/*
 * When a vCPU is suspended for any reason, it calls
 * checkpoint_cpu_suspend().  This records that the vCPU is idle.
 * Before returning from suspension, checkpoint_cpu_resume() is
 * called.  In suspend we note that the vCPU is idle.  In resume we
 * pause the vCPU thread until the checkpoint is complete.  The reason
 * for the two-step process is that vCPUs might already be stopped in
 * the debug server when a checkpoint is requested.  This approach
 * allows us to account for and handle those vCPUs.
 */
void
checkpoint_cpu_suspend(int vcpu)
{

	pthread_mutex_lock(&vcpu_lock);
	CPU_SET(vcpu, &vcpus_suspended);
	if (checkpoint_active && CPU_CMP(&vcpus_active, &vcpus_suspended) == 0)
		pthread_cond_signal(&vcpus_idle);
	pthread_mutex_unlock(&vcpu_lock);
}

void
checkpoint_cpu_resume(int vcpu)
{

	pthread_mutex_lock(&vcpu_lock);
	while (checkpoint_active)
		pthread_cond_wait(&vcpus_can_run, &vcpu_lock);
	CPU_CLR(vcpu, &vcpus_suspended);
	pthread_mutex_unlock(&vcpu_lock);
}

/*
 * Prevent the vCPU threads from entering the guest until vm_restore() has
 * loaded the saved state.  This must be called before the vCPUs are started.
 */
void
checkpoint_cpu_hold(void)
{

	pthread_mutex_lock(&vcpu_lock);
	checkpoint_active = true;
	pthread_mutex_unlock(&vcpu_lock);
}

static void
vm_vcpu_pause(struct vmctx *ctx)
{

	pthread_mutex_lock(&vcpu_lock);
	checkpoint_active = true;
	vm_suspend_all_cpus(ctx);
	while (CPU_CMP(&vcpus_active, &vcpus_suspended) != 0)
		pthread_cond_wait(&vcpus_idle, &vcpu_lock);
	pthread_mutex_unlock(&vcpu_lock);
}

static void
vm_vcpu_resume(struct vmctx *ctx)
{

	pthread_mutex_lock(&vcpu_lock);
	checkpoint_active = false;
	pthread_mutex_unlock(&vcpu_lock);
	vm_resume_all_cpus(ctx);
	pthread_cond_broadcast(&vcpus_can_run);
}

/*
 * Load the state saved in 'rstate' into the VM.  The vCPUs must have been
 * held with checkpoint_cpu_hold() before they were started; they are
 * released once the state is in place.
 */
int
vm_restore(struct vmctx *ctx, struct restore_state *rstate)
{
	int error;

	if (vm_restore_check_devices(rstate) != 0)
		return (-1);

	/*
	 * Keep the emulated timers in the kernel from firing while their
	 * state is being written.
	 */
	error = vm_pause_instance(ctx);
	if (error != 0) {
		EPRINTLN("Failed to pause the VM: %s", strerror(error));
		return (-1);
	}

	if (vm_pause_devices() != 0) {
		EPRINTLN("Failed to pause PCI device state.");
		return (-1);
	}

	if (restore_vm_mem(ctx, rstate) != 0) {
		EPRINTLN("Failed to restore VM memory.");
		return (-1);
	}

	if (vm_restore_devices(rstate) != 0) {
		EPRINTLN("Failed to restore PCI device state.");
		return (-1);
	}

	if (vm_restore_kern_structs(ctx, rstate) != 0) {
		EPRINTLN("Failed to restore kernel structs.");
		return (-1);
	}

	error = vm_resume_instance(ctx);
	if (error != 0) {
		EPRINTLN("Failed to resume the VM: %s", strerror(error));
		return (-1);
	}

	if (vm_resume_devices() != 0) {
		EPRINTLN("Failed to resume PCI device state.");
		return (-1);
	}

	vm_vcpu_resume(ctx);

	return (0);
}

static int
vm_checkpoint(struct vmctx *ctx, int vmmem_fd, int kdata_fd, int meta_fd,
    bool stop_vm, char *errmsg, size_t errlen)
{
	nvlist_t *meta_nvl = NULL, *kern_nvl = NULL, *dev_nvl = NULL;
	struct pci_devinst *pdi;
	char *meta_buf = NULL;
	size_t meta_len = 0;
	void *buffer;
	off_t offset = 0;
	bool vm_paused = false;
	int error, ret;

	if ((pdi = vm_snapshot_unsupported_dev()) != NULL) {
		(void) snprintf(errmsg, errlen,
		    "device %s does not support checkpoints", pdi->pi_name);
		return (ENOTSUP);
	}

	buffer = malloc(SNAPSHOT_BUFFER_SIZE);
	if (buffer == NULL)
		return (ENOMEM);

	struct vm_snapshot_meta meta = {
		.buffer.buf_start = buffer,
		.buffer.buf_size = SNAPSHOT_BUFFER_SIZE,

		.op = VM_SNAPSHOT_SAVE,
	};

	if ((error = nvlist_alloc(&meta_nvl, NV_UNIQUE_NAME, 0)) != 0 ||
	    (error = nvlist_alloc(&kern_nvl, NV_UNIQUE_NAME, 0)) != 0 ||
	    (error = nvlist_alloc(&dev_nvl, NV_UNIQUE_NAME, 0)) != 0) {
		goto out;
	}

	vm_vcpu_pause(ctx);

	error = vm_pause_devices();
	if (error != 0) {
		EPRINTLN("Could not pause devices");
		goto done;
	}

	/*
	 * With the vCPUs stopped and the devices quiesced, pausing the VM
	 * stops the timers in the kernel and moves any event which is pending
	 * injection into a vCPU to where it can be saved.
	 */
	error = vm_pause_instance(ctx);
	if (error != 0) {
		EPRINTLN("Could not pause the VM: %s", strerror(error));
		goto done;
	}
	vm_paused = true;

	error = vm_snapshot_mem(ctx, vmmem_fd, true);
	if (error != 0) {
		EPRINTLN("Could not write guest memory to file");
		goto done;
	}

	error = vm_snapshot_basic_metadata(ctx, meta_nvl);
	if (error != 0) {
		EPRINTLN("Failed to snapshot vm basic metadata.");
		goto done;
	}

	error = vm_save_kern_structs(ctx, kdata_fd, kern_nvl, &meta, &offset);
	if (error != 0) {
		EPRINTLN("Failed to snapshot vm kernel data.");
		goto done;
	}

	error = vm_snapshot_devices(kdata_fd, dev_nvl, &meta, &offset);
	if (error != 0) {
		EPRINTLN("Failed to snapshot device state.");
		goto done;
	}

	if ((error = nvlist_add_uint32(meta_nvl, META_VERSION_KEY,
	    SNAPSHOT_VERSION)) != 0 ||
	    (error = nvlist_add_nvlist(meta_nvl, META_KERNEL_KEY,
	    kern_nvl)) != 0 ||
	    (error = nvlist_add_nvlist(meta_nvl, META_DEV_KEY, dev_nvl)) != 0 ||
	    (error = nvlist_pack(meta_nvl, &meta_buf, &meta_len,
	    NV_ENCODE_XDR, 0)) != 0) {
		EPRINTLN("Failed to build the checkpoint metadata: %s",
		    strerror(error));
		goto done;
	}

	error = write_all(meta_fd, meta_buf, meta_len);
	if (error != 0) {
		EPRINTLN("Failed to write the checkpoint metadata: %s",
		    strerror(error));
		goto done;
	}

	if (stop_vm) {
		if (fsync(vmmem_fd) != 0 || fsync(kdata_fd) != 0 ||
		    fsync(meta_fd) != 0) {
			error = errno;
			EPRINTLN("Failed to sync the checkpoint: %s",
			    strerror(error));
			goto done;
		}

		/*
		 * The vCPUs and devices remain paused.  bhyve exits once the
		 * reply has been sent.
		 */
		vm_destroy(ctx);
		ipc_exit_after_reply(BHYVE_EXIT_SUSPEND);
		goto out;
	}

done:
	if (vm_paused) {
		ret = vm_resume_instance(ctx);
		if (ret != 0)
			EPRINTLN("Could not resume the VM: %s", strerror(ret));
	}
	ret = vm_resume_devices();
	if (ret != 0)
		EPRINTLN("Could not resume devices");
	vm_vcpu_resume(ctx);
out:
	free(meta_buf);
	nvlist_free(dev_nvl);
	nvlist_free(kern_nvl);
	nvlist_free(meta_nvl);
	free(buffer);
	return (error);
}

/*
 * The request carries the descriptors of the memory, kernel data and
 * metadata files, in that order.
 */
static nvlist_t *
vm_do_checkpoint(struct vmctx *ctx, const nvlist_t *nvl)
{
	nvlist_t *reply;
	boolean_t suspend;
	int32_t *fds;
	uint_t nfds;
	char errmsg[128] = "";
	int error;

	if (nvlist_alloc(&reply, NV_UNIQUE_NAME, 0) != 0)
		return (NULL);

	if (nvlist_lookup_boolean_value((nvlist_t *)nvl, "suspend",
	    &suspend) != 0 ||
	    nvlist_lookup_int32_array((nvlist_t *)nvl, IPC_FDS_KEY, &fds,
	    &nfds) != 0 || nfds != 3) {
		error = EINVAL;
	} else {
		error = vm_checkpoint(ctx, fds[0], fds[1], fds[2], suspend,
		    errmsg, sizeof (errmsg));
	}
	if (error != 0) {
		(void) nvlist_add_string(reply, "error",
		    errmsg[0] != '\0' ? errmsg : strerror(error));
	}

	return (reply);
}
IPC_COMMAND(checkpoint, vm_do_checkpoint);

void
vm_snapshot_buf_err(const char *bufname, const enum vm_snapshot_op op)
{
	const char *opname;

	if (op == VM_SNAPSHOT_SAVE)
		opname = "save";
	else if (op == VM_SNAPSHOT_RESTORE)
		opname = "restore";
	else
		opname = "unknown";

	EPRINTLN("%s: snapshot-%s failed for %s", __func__, opname, bufname);
}

int
vm_snapshot_buf(void *data, size_t data_size, struct vm_snapshot_meta *meta)
{
	struct vm_snapshot_buffer *buffer;
	int op;

	buffer = &meta->buffer;
	op = meta->op;

	if (buffer->buf_rem < data_size) {
		EPRINTLN("%s: buffer too small", __func__);
		return (E2BIG);
	}

	if (op == VM_SNAPSHOT_SAVE)
		(void) memcpy(buffer->buf, data, data_size);
	else if (op == VM_SNAPSHOT_RESTORE)
		(void) memcpy(data, buffer->buf, data_size);
	else
		return (EINVAL);

	buffer->buf += data_size;
	buffer->buf_rem -= data_size;

	return (0);
}

size_t
vm_get_snapshot_size(struct vm_snapshot_meta *meta)
{
	size_t length;
	struct vm_snapshot_buffer *buffer;

	buffer = &meta->buffer;

	if (buffer->buf_size < buffer->buf_rem) {
		EPRINTLN("%s: Invalid buffer: size = %zu, rem = %zu",
		    __func__, buffer->buf_size, buffer->buf_rem);
		length = 0;
	} else {
		length = buffer->buf_size - buffer->buf_rem;
	}

	return (length);
}

int
vm_snapshot_guest2host_addr(struct vmctx *ctx, void **addrp, size_t len,
    bool restore_null, struct vm_snapshot_meta *meta)
{
	int ret;
	vm_paddr_t gaddr;

	if (meta->op == VM_SNAPSHOT_SAVE) {
		gaddr = paddr_host2guest(ctx, *addrp);
		if (gaddr == (vm_paddr_t)-1) {
			if (!restore_null ||
			    (restore_null && (*addrp != NULL))) {
				ret = EFAULT;
				goto done;
			}
		}

		SNAPSHOT_VAR_OR_LEAVE(gaddr, meta, ret, done);
	} else if (meta->op == VM_SNAPSHOT_RESTORE) {
		SNAPSHOT_VAR_OR_LEAVE(gaddr, meta, ret, done);
		if (gaddr == (vm_paddr_t)-1) {
			if (!restore_null) {
				ret = EFAULT;
				goto done;
			}
		}

		*addrp = paddr_guest2host(ctx, gaddr, len);
	} else {
		ret = EINVAL;
	}

done:
	return (ret);
}

int
vm_snapshot_buf_cmp(void *data, size_t data_size, struct vm_snapshot_meta *meta)
{
	struct vm_snapshot_buffer *buffer;
	int op;
	int ret;

	buffer = &meta->buffer;
	op = meta->op;

	if (buffer->buf_rem < data_size) {
		EPRINTLN("%s: buffer too small", __func__);
		ret = E2BIG;
		goto done;
	}

	if (op == VM_SNAPSHOT_SAVE) {
		ret = 0;
		(void) memcpy(buffer->buf, data, data_size);
	} else if (op == VM_SNAPSHOT_RESTORE) {
		ret = memcmp(data, buffer->buf, data_size) == 0 ? 0 : EINVAL;
	} else {
		ret = EINVAL;
		goto done;
	}

	buffer->buf += data_size;
	buffer->buf_rem -= data_size;

done:
	return (ret);
}

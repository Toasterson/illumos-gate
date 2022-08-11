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
 * Copyright 2022 Hayashi Naoyuki
 */

#pragma once

#include <sys/types.h>
#include <stdbool.h>
#include <sys/platform.h>
#include <sys/ddi_impldefs.h>
#include <sys/blkdev.h>

struct mmc_reg {
	ddi_acc_handle_t handle;
	uint32_t *addr;
};

struct mmc_dma {
	caddr_t addr;
	size_t size;
	uint64_t dmac_addr;
	ddi_dma_handle_t dma_handle;
	ddi_acc_handle_t mem_handle;
	ddi_dma_cookie_t cookie;
};

struct mmc_sc {
	dev_info_t *dip;
	bd_handle_t bdh;
	kmutex_t lock;

	uint32_t interrupted;
	uint32_t dma_interrupted;

	uint32_t ocr;
	uint32_t ocr_avail;
	uint32_t vdd;
	uint32_t csd[4];
	uint32_t cid[4];
	uint32_t capacity;
	uint32_t scr[2];
	uint32_t func_status[16];
	uint32_t rca;

	bool detach;

	ddi_taskq_t *tq;
	list_t free_request;

	// interrupt
	ddi_intr_handle_t ihandle;
	kmutex_t intrlock;
	kcondvar_t waitcv;

	struct mmc_reg reg;
	struct mmc_dma buffer;
	struct mmc_dma desc;
};


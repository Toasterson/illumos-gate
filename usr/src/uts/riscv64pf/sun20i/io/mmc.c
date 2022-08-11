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

#include <stddef.h>
#include <sys/promif.h>
#include <sys/byteorder.h>
#include <sys/debug.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/sunndi.h>
#include <sys/ndi_impldefs.h>
#include <sys/ddi_impldefs.h>
#include <sys/sysmacros.h>
#include <sys/smhcreg.h>
#include <sys/sdcard/sda.h>
#include <sys/callo.h>
#include <sys/ddi_subrdefs.h>
#include "mmc.h"

#define MMC_BUFFER_SIZE 0x1000
#define MMC_MAX_TRANSFER_SIZE 0x100000
#define MMC_DESC_BUFFER_SIZE 0x1000
#define MMC_REQUESTS_MAX 0x20

#define CCU_BASE	0x02001000
#define HOSC_FREQ	24000000

struct smhc_info {
	uint32_t base;
	uint32_t clk;
	uint32_t bgr;
	uint32_t bgr_rst;
	uint32_t bgr_gate;
};

static const struct smhc_info sun20i_info[] = {
	{ 0x4020000, CCU_BASE + 0x830, CCU_BASE + 0x84c, (1u << 16), (1u << 0) },
	{ 0x4021000, CCU_BASE + 0x834, CCU_BASE + 0x84c, (1u << 17), (1u << 1) },
	{ 0x4022000, CCU_BASE + 0x838, CCU_BASE + 0x84c, (1u << 18), (1u << 2) },
};

static const union smhc_rintsts interrupt_error = {
	.re = 1, .rce = 1, .dce = 1, .rto_back = 1, .dto_bds = 1,
	.fu_fo = 1, .cb_iw = 1, .dse_bc = 1, .dee = 1
};

struct mmc_request
{
	list_node_t node;
	struct mmc_sc *sc;
	bd_xfer_t *xfer;
};

static void
usecwait(int usec)
{
	drv_usecwait(usec);
}

static uint32_t
mmc_extract_bits(uint32_t *bits, int hi, int len)
{
	uint32_t val = 0;

	for (int i = hi; i >= (hi - len + 1); i--) {
		val = (val << 1) | (((bits[i / 32]) >> (i % 32)) & 1);
	}

	return val;
}

static int32_t
get_pll_peri()
{
	uint32_t pll_peri = *(volatile uint32_t *)(SEGKPM_BASE + CCU_BASE + 0x20);
	uint32_t pll_p0 = ((pll_peri >> 16) & 0x7);
	uint32_t pll_n = ((pll_peri >> 8) & 0xFF);
	uint32_t pll_input_div2 = ((pll_peri >> 1) & 1);

	return HOSC_FREQ * (pll_n + 1) / (pll_input_div2 + 1) / (pll_p0 + 1) / 2;
}

static const struct smhc_info *
mmc_get_smhc_info(struct mmc_sc *sc)
{
	uint64_t base;
	pnode_t node = ddi_get_nodeid(sc->dip);
	if (prom_get_reg_address(node, 0, &base) != 0)
		return NULL;
	int c;
	for (c = 0; c < ARRAY_SIZE(sun20i_info); c++) {
		if (sun20i_info[c].base == base)
			break;
	}
	if (c == ARRAY_SIZE(sun20i_info))
		return NULL;
	return &sun20i_info[c];
}

static int
mmc_set_clockrate(struct mmc_sc *sc, int hz)
{
	uint32_t src_sel = (hz > HOSC_FREQ)? 1: 0;
	uint32_t src_freq = (hz > HOSC_FREQ)? get_pll_peri(): HOSC_FREQ;
	uint32_t n = 0;
	uint32_t div = (src_freq / hz);
	while (div > 16) {
		div /= 2;
		n++;
	}
	if (n > 3 || hz * (1 << (n)) * div != src_freq)
		return -1;

	const struct smhc_info *info = mmc_get_smhc_info(sc);
	ASSERT(info != NULL);

	volatile uint32_t *clk = (volatile uint32_t *)(SEGKPM_BASE + info->clk);
	*clk = (1u << 31) | (src_sel << 24) | (n << 8) | (div - 1);
	usecwait(10000);

	return 0;
}

static void
mmc_reg_write(struct mmc_sc *sc, size_t offset, uint32_t val)
{
	uint32_t *addr = sc->reg.addr + offset / 4;
	ddi_put32(sc->reg.handle, addr, val);
}

static uint32_t
mmc_reg_read(struct mmc_sc *sc, size_t offset)
{
	uint32_t *addr = sc->reg.addr + offset / 4;
	return ddi_get32(sc->reg.handle, addr);
}

static int
mmc_set_sd_clock(struct mmc_sc *sc, bool enable)
{
	union smhc_clkdiv clkdiv = { mmc_reg_read(sc, SMHC_CLKDIV) };
	clkdiv.cclk_enb = (enable? 1: 0);
	clkdiv.cclk_div = 0;
	clkdiv.cclk_ctrl = 0;
	clkdiv.mask_data0 = 1;
	mmc_reg_write(sc, SMHC_CLKDIV, clkdiv.dw);

	union smhc_cmd cmd = {0};
	cmd.wait_pre_over = 1;
	cmd.prg_clk = 1;
	cmd.cmd_load = 1;
	mmc_reg_write(sc, SMHC_CMD, cmd.dw);
	for (int i = 0; i < 10; i++) {
		usecwait(1000);
		cmd.dw = mmc_reg_read(sc, SMHC_CMD);
		if (!cmd.cmd_load)
			break;
	}

	clkdiv.dw = mmc_reg_read(sc, SMHC_CLKDIV);
	clkdiv.mask_data0 = 0;
	mmc_reg_write(sc, SMHC_CLKDIV, clkdiv.dw);

	return cmd.cmd_load? -1: 0;
}

static void
mmc_calibrate(struct mmc_sc *sc)
{
	union smhc_samp_dl samp_dl = { .samp_dl_sw_en = 1 };
	mmc_reg_write(sc, SMHC_SAMP_DL, samp_dl.dw);
}

static int
mmc_set_clock(struct mmc_sc *sc, int hz)
{
	if (mmc_set_sd_clock(sc, false) < 0)
		return -1;

	if (mmc_set_clockrate(sc, hz * 2) < 0)
		return -1;

 	union smhc_ntsr ntsr = { mmc_reg_read(sc, SMHC_NTSR) };
	ntsr.mode_select = 1;
	mmc_reg_write(sc, SMHC_NTSR, ntsr.dw);

	mmc_calibrate(sc);

	if (mmc_set_sd_clock(sc, true) < 0)
		return -1;

	return 0;
}

static void
mmc_reset(struct mmc_sc *sc)
{
	const struct smhc_info *info = mmc_get_smhc_info(sc);
	ASSERT(info != NULL);

	volatile uint32_t *bgr = (volatile uint32_t *)(SEGKPM_BASE + info->bgr);
	uint32_t x = *bgr;
	x &= ~(info->bgr_rst | info->bgr_gate);
	*bgr = x;
	usecwait(100000);
	*bgr |= (info->bgr_rst | info->bgr_gate);
	usecwait(10000);
}

static int
mmc_soft_reset(struct mmc_sc *sc)
{
	uint32_t old_ctrl = mmc_reg_read(sc, SMHC_CTRL);
	uint32_t old_idmac = mmc_reg_read(sc, SMHC_IDMAC);
	mmc_reg_write(sc, SMHC_CTRL, 0);
	mmc_reg_write(sc, SMHC_IDMAC, 0);

	struct smhc_desc *desc = (struct smhc_desc *)sc->desc.addr;
	desc->des0.dw = 0;
	asm volatile ("fence":::"memory");

	union smhc_idmac idmac = { .idmac_rst = 1 };
	mmc_reg_write(sc, SMHC_IDMAC, idmac.dw);

	union smhc_ctrl ctrl = { .soft_rst = 1, .fifo_rst = 1, .dma_rst = 1 };
	mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);
	for (int i = 0; i < 1000; i++) {
		usecwait(200);
		ctrl.dw = mmc_reg_read(sc, SMHC_CTRL);
		if (!(ctrl.soft_rst || ctrl.fifo_rst || ctrl.dma_rst))
			break;
	}
	mmc_reg_write(sc, SMHC_CTRL, old_ctrl);
	mmc_reg_write(sc, SMHC_IDMAC, old_idmac);

	if (ctrl.soft_rst || ctrl.fifo_rst || ctrl.dma_rst)
		return -1;

	{
		mutex_enter(&sc->intrlock);

		uint32_t val = mmc_reg_read(sc, SMHC_RINTSTS);
		mmc_reg_write(sc, SMHC_RINTSTS, val);
		sc->interrupted &= ~val;

		val = mmc_reg_read(sc, SMHC_IDST);
		mmc_reg_write(sc, SMHC_IDST, val);
		sc->dma_interrupted &= ~val;

		mutex_exit(&sc->intrlock);
	}
	return 0;
}

static int
mmc_wait_intr(struct mmc_sc *sc, uint32_t mask, uint32_t dma_mask, hrtime_t nsec)
{
	bool completed = false;
	bool error_occured = false;
	bool timeout_occurred = false;
	{
		mutex_enter(&sc->intrlock);

		hrtime_t timeout = gethrtime() + nsec;

		for (;;) {
			if ((sc->interrupted & mask) == mask && (sc->dma_interrupted & dma_mask) == dma_mask)
				completed = true;

			union smhc_idst dma_error = { .fatal_berr_int = 1, .err_flag_sum = 1 };
			if ((sc->interrupted & interrupt_error.dw) || (sc->dma_interrupted & dma_error.dw)) {
				error_occured = true;
			}

			if (completed || error_occured || timeout_occurred)
				break;

			if (cv_timedwait_hires(&sc->waitcv,
				    &sc->intrlock, timeout, USEC2NSEC(1),
				    CALLOUT_FLAG_ABSOLUTE) < 0)
				timeout_occurred = true;
		}

		if (!completed || error_occured) {
			cmn_err(CE_WARN, "%s%d: interrupted %08x dma_interrupted %08x mask %08x dma_mask %08x",
			    ddi_driver_name(sc->dip), ddi_get_instance(sc->dip),
			    sc->interrupted, sc->dma_interrupted, mask, dma_mask);
			cmn_err(CE_WARN, "%s%d: SMHC_RINTSTS %08x SMHC_IDST %08x",
			    ddi_driver_name(sc->dip), ddi_get_instance(sc->dip),
			    mmc_reg_read(sc, SMHC_RINTSTS), mmc_reg_read(sc, SMHC_IDST));
		}

		sc->interrupted = 0;
		sc->dma_interrupted = 0;

		mutex_exit(&sc->intrlock);
	}

	if (error_occured || !completed) {
		mmc_soft_reset(sc);
		return -1;
	}

	return 0;
}

static void
mmc_map_desc(struct mmc_sc *sc, struct sda_cmd *cmdp)
{
	struct smhc_desc *desc = (struct smhc_desc *)sc->desc.addr;
	for (uint_t n = 0; n < cmdp->sc_ndmac; n++) {
		ddi_dma_cookie_t dmac;

		if (n == 0) {
			dmac = cmdp->sc_dmac;
		} else {
			ddi_dma_nextcookie(cmdp->sc_dmah, &dmac);
		}
		typeof(desc->des0) des0 = {
			.chain_mod = 1,
			.first_flag = (n == 0? 1: 0),
			.last_flag = ((n == cmdp->sc_ndmac - 1)? 1: 0),
			.cur_txrx_over_int_dis = ((n == cmdp->sc_ndmac - 1)? 0: 1),
			.hold = (n == 0? 0: 1),
		};

		ASSERT3U(dmac.dmac_size, <, 0x2000);
		ASSERT(dmac.dmac_size % 4 == 0);
		ASSERT(dmac.dmac_address % 4 == 0);
		typeof(desc->des1) des1 = { .buff_size = dmac.dmac_size };
		typeof(desc->des2) des2 = { .buff_addr = dmac.dmac_address / 4 };
		typeof(desc->des3) des3 = { .next_desp_addr = ((n == cmdp->sc_ndmac - 1)? 0: (sc->desc.cookie.dmac_address + sizeof(struct smhc_desc) * (n + 1)) / 4) };
		desc[n].des0.dw = des0.dw;
		desc[n].des1.dw = des1.dw;
		desc[n].des2.dw = des2.dw;
		desc[n].des3.dw = des3.dw;
	}
	asm volatile ("fence":::"memory");
	desc[0].des0.hold = 1;
}

static void
mmc_start_cmd(struct mmc_sc *sc, struct sda_cmd *cmdp)
{
	union smhc_cmd cmd = {
		.cmd_idx = cmdp->sc_index,
		.cmd_load = 1,
		.send_init_seq = ((cmdp->sc_index == CMD_GO_IDLE)? 1: 0),
		.stop_abt_cmd = ((cmdp->sc_index == CMD_STOP_TRANSMIT)? 1: 0),
	};

	switch (cmdp->sc_rtype) {
	case R1:
	case R5:
	case R6:
	case R7:
		// resp 48
		cmd.resp_rcv = 1;
		cmd.chk_resp_crc = 1;
		break;
	case R1b:
	case R5b:
		// resp 48b
		cmd.resp_rcv = 1;
		cmd.chk_resp_crc = 1;
		break;
	case R2:
		// resp 136
		cmd.resp_rcv = 1;
		cmd.long_resp = 1;
		cmd.chk_resp_crc = 1;
		break;
	case R3:
	case R4:
		// resp 48
		cmd.resp_rcv = 1;
		break;
	case R0:
		break;
	default:
		break;
	}

	union smhc_intmask intmask = { interrupt_error.dw };

	if (cmdp->sc_flags & (SDA_CMDF_READ | SDA_CMDF_WRITE)) {
		cmd.data_trans = 1;
		cmd.wait_pre_over = 1;
		if (cmdp->sc_flags & SDA_CMDF_WRITE)
			cmd.trans_dir = 1;
		if (cmdp->sc_index == CMD_READ_MULTI || cmdp->sc_index == CMD_WRITE_MULTI) {
			cmd.stop_cmd_flag = 1;
			intmask.acd = 1;
		} else {
			intmask.dtc = 1;
		}

		union smhc_blksiz blksiz = {.blk_sz = cmdp->sc_blksz};
		mmc_reg_write(sc, SMHC_BLKSIZ, blksiz.dw);

		union smhc_bytcnt bytcnt = {.byte_cnt = cmdp->sc_blksz * cmdp->sc_nblks};
		mmc_reg_write(sc, SMHC_BYTCNT, bytcnt.dw);

		if (cmdp->sc_ndmac) {
			mmc_map_desc(sc, cmdp);
			asm volatile ("fence":::"memory");
			union smhc_idmac idmac_enb = { .fix_bust_ctrl = 1, .idmac_enb = 1, .des_load_ctrl = 1 };
			mmc_reg_write(sc, SMHC_IDMAC, idmac_enb.dw);
		}
	} else {
		intmask.cc = 1;
	}
	mmc_reg_write(sc, SMHC_INTMASK, intmask.dw);
	union smhc_cmdarg cmdarg = { .cmd_arg = cmdp->sc_argument };
	mmc_reg_write(sc, SMHC_CMDARG, cmdarg.dw);
	mmc_reg_write(sc, SMHC_CMD, cmd.dw);
}

static int
mmc_wait_cmd_done(struct mmc_sc *sc, struct sda_cmd *cmd)
{
	union smhc_rintsts cmd_done = { .cc = 1, };
	union smhc_idst dma_done = { 0 };
	if (cmd->sc_flags & (SDA_CMDF_READ | SDA_CMDF_WRITE)) {
		cmd_done.dtc = 1;
		dma_done.rx_int = !!(cmd->sc_flags & SDA_CMDF_READ);
		if (cmd->sc_index == CMD_READ_MULTI || cmd->sc_index == CMD_WRITE_MULTI) {
			cmd_done.acd = 1;
		}
	}
	if (mmc_wait_intr(sc, cmd_done.dw, dma_done.dw, SEC2NSEC(30)) < 0)
		return -1;

	switch (cmd->sc_rtype) {
	case R0:
		break;
	case R2:
		cmd->sc_response[0] = mmc_reg_read(sc, SMHC_RESP0);
		cmd->sc_response[1] = mmc_reg_read(sc, SMHC_RESP1);
		cmd->sc_response[2] = mmc_reg_read(sc, SMHC_RESP2);
		cmd->sc_response[3] = mmc_reg_read(sc, SMHC_RESP3);
		break;
	default:
		cmd->sc_response[0] = mmc_reg_read(sc, SMHC_RESP0);
		break;
	}
	return 0;
}

static int mmc_stop_transmission(struct mmc_sc *);

static int
mmc_send_cmd(struct mmc_sc *sc, struct sda_cmd *cmd)
{
	int retry_remain = 500;
	while (--retry_remain > 0) {
		union smhc_status status = { mmc_reg_read(sc, SMHC_STATUS) };
		if (status.card_busy == 0)
			break;
		usecwait(1000);
	}
	if (retry_remain == 0)
		return -1;

	mmc_start_cmd(sc, cmd);

	if (mmc_wait_cmd_done(sc, cmd) != 0)
		goto err_exit;

	if (cmd->sc_index == CMD_STOP_TRANSMIT)
		mmc_soft_reset(sc);

	return 0;
err_exit:
	if (cmd->sc_index == CMD_READ_MULTI || cmd->sc_index == CMD_WRITE_MULTI) {
		mmc_stop_transmission(sc);
	}
	return -1;
}

static int
mmc_go_idle_state(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_GO_IDLE,
		.sc_rtype = R0,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;
	return 0;
}

static int
mmc_send_if_cond(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_SEND_IF_COND,
		.sc_rtype = R7,
		.sc_argument = ((!!(sc->ocr_avail & OCR_HI_MASK)) << 8) | 0xaa,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;
	if ((cmd.sc_response[0] & 0xff) != 0xaa)
		return -1;
	return 0;
}

static int
mmc_sd_send_ocr(struct mmc_sc *sc)
{
	struct sda_cmd acmd = {
		.sc_index = CMD_APP_CMD,
		.sc_rtype = R1,
	};
	if (mmc_send_cmd(sc, &acmd) != 0)
		return -1;

	uint32_t ocr = (sc->ocr_avail & OCR_HI_MASK) | OCR_CCS;

	struct sda_cmd cmd = {
		.sc_index = ACMD_SD_SEND_OCR,
		.sc_rtype = R3,
		.sc_argument = ocr,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;
	if (cmd.sc_response[0] & OCR_POWER_UP)
		sc->ocr = cmd.sc_response[0];

	return 0;
}

static int
mmc_all_send_cid(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_BCAST_CID,
		.sc_rtype = R2,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	memcpy(sc->cid, cmd.sc_response, sizeof(sc->cid));

	return 0;
}

static int
mmc_send_relative_addr(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_SEND_RCA,
		.sc_rtype = R6,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	sc->rca = (cmd.sc_response[0] >> 16) & 0xffff;

	return 0;
}

static int
mmc_send_csd(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_SEND_CSD,
		.sc_rtype = R2,
		.sc_argument = sc->rca << 16,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	memcpy(sc->csd, cmd.sc_response, sizeof(sc->csd));

	return 0;
}

static int
mmc_select_card(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_SELECT_CARD,
		.sc_rtype = R1,
		.sc_argument = sc->rca << 16,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	return 0;
}

static int
mmc_send_scr(struct mmc_sc *sc)
{
	struct sda_cmd acmd = {
		.sc_index = CMD_APP_CMD,
		.sc_rtype = R1,
		.sc_argument = sc->rca << 16,
	};
	if (mmc_send_cmd(sc, &acmd) != 0)
		return -1;

	struct sda_cmd cmd = {
		.sc_index = ACMD_SEND_SCR,
		.sc_rtype = R1,

		.sc_nblks = 1,
		.sc_blksz = 8,
		.sc_flags = SDA_CMDF_READ,
		.sc_dmah = sc->buffer.dma_handle,
		.sc_ndmac = 1,
		.sc_dmac = sc->buffer.cookie,
	};
	cmd.sc_dmac.dmac_size = 8;
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	ddi_dma_sync(sc->buffer.dma_handle, 0, sizeof(sc->scr), DDI_DMA_SYNC_FORKERNEL);

	for (int i = 0; i < ARRAY_SIZE(sc->scr); i++)
		sc->scr[i] = ntohl(*(uint32_t *)(sc->buffer.addr + sizeof(sc->scr) * (ARRAY_SIZE(sc->scr) - 1 - i)));

	return 0;
}

static int
mmc_swtch_func(struct mmc_sc *sc, uint32_t argument)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_SWITCH_FUNC,
		.sc_rtype = R1,
		.sc_argument = argument,

		.sc_nblks = 1,
		.sc_blksz = 64,
		.sc_flags = SDA_CMDF_READ,
		.sc_dmah = sc->buffer.dma_handle,
		.sc_ndmac = 1,
		.sc_dmac = sc->buffer.cookie,
	};
	cmd.sc_dmac.dmac_size = 64;
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	ddi_dma_sync(sc->buffer.dma_handle, 0, sizeof(sc->func_status), DDI_DMA_SYNC_FORKERNEL);

	for (int i = 0; i < ARRAY_SIZE(sc->func_status); i++)
		sc->func_status[i] = ntohl(*(uint32_t *)(sc->buffer.addr + sizeof(sc->func_status[0]) * (ARRAY_SIZE(sc->func_status) - 1 - i)));

	return 0;
}

static int
mmc_stop_transmission(struct mmc_sc *sc)
{
	struct sda_cmd cmd = {
		.sc_index = CMD_STOP_TRANSMIT,
		.sc_rtype = R1b,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;
	return 0;
}

static int
mmc_set_bus_width(struct mmc_sc *sc, int width)
{
	ASSERT(width == 1 || width == 4);
	struct sda_cmd acmd = {
		.sc_index = CMD_APP_CMD,
		.sc_rtype = R1,
		.sc_argument = sc->rca << 16,
	};
	if (mmc_send_cmd(sc, &acmd) < 0)
		return -1;

	struct sda_cmd cmd = {
		.sc_index = ACMD_SET_BUS_WIDTH,
		.sc_rtype = R1,
		.sc_argument = (width == 1? 0: 2),
	};
	if (mmc_send_cmd(sc, &cmd) < 0)
		return -1;

	union smhc_ctype ctype = { mmc_reg_read(sc, SMHC_CTYPE) };
	ctype.card_wid = (width == 1? 0: 1);
	mmc_reg_write(sc, SMHC_CTYPE, ctype.dw);

	return 0;
}

static int
mmc_set_blocklen(struct mmc_sc *sc, int len)
{
	ASSERT(len == DEV_BSIZE);
	struct sda_cmd cmd = {
		.sc_index = CMD_SET_BLOCKLEN,
		.sc_rtype = R1,

		.sc_argument = len,
	};
	if (mmc_send_cmd(sc, &cmd) < 0)
		return -1;

	return 0;
}

static int
mmc_init(struct mmc_sc *sc)
{
	pnode_t node = ddi_get_nodeid(sc->dip);

	// reset
	mmc_reset(sc);

	{
		union smhc_fifoth fifoth = { .tx_tl = 240, .rx_tl = 15, .bsize_of_trans = 3 };
		mmc_reg_write(sc, SMHC_FIFOTH, fifoth.dw);

		union smhc_tmout tmout = { .rto_lmt = 0xff, .dto_lmt = 0xffffff };
		mmc_reg_write(sc, SMHC_TMOUT, tmout.dw);

		mmc_reg_write(sc, SMHC_INTMASK, 0);
		mmc_reg_write(sc, SMHC_IDIE, 0);

		mmc_reg_write(sc, SMHC_RINTSTS, 0xffffffff);
		mmc_reg_write(sc, SMHC_IDST, 0xffffffff);

		union smhc_idie idie = { .rx_int = 1 };
		mmc_reg_write(sc, SMHC_IDIE, idie.dw);

		union smhc_dlba dlba = { .des_base_addr = sc->desc.cookie.dmac_address / 4 };
		mmc_reg_write(sc, SMHC_DLBA, dlba.dw);

		struct smhc_desc *desc = (struct smhc_desc *)sc->desc.addr;
		desc->des0.dw = 0;
		asm volatile ("fence":::"memory");

		union smhc_ctrl ctrl = { mmc_reg_read(sc, SMHC_CTRL) };
		ctrl.int_enb = 1;
		ctrl.dma_enb = 1;
		mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);
		ctrl.dma_rst = 1;
		mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);

		union smhc_idmac idmac = { .idmac_rst = 1 };
		mmc_reg_write(sc, SMHC_IDMAC, idmac.dw);
	}

	if (mmc_set_clock(sc, 400000) != 0)
		goto err_exit;

	sc->ocr_avail = OCR_33_34V | OCR_32_33V;

	int i;
	if (mmc_go_idle_state(sc) != 0)
		goto err_exit;

	if (mmc_send_if_cond(sc) != 0)
		goto err_exit;

	for (i = 0; i < 1000; i++) {
		if (mmc_sd_send_ocr(sc) != 0)
			goto err_exit;

		if (sc->ocr & OCR_POWER_UP)
			break;

		usecwait(1000);
	}
	if (i >= 1000)
		goto err_exit;

	if (mmc_all_send_cid(sc) != 0)
		goto err_exit;

	if (mmc_send_relative_addr(sc) != 0)
		goto err_exit;

	if (mmc_send_csd(sc) != 0)
		goto err_exit;

	if (mmc_select_card(sc) != 0)
		goto err_exit;

	for (i = 0; i < 3; i++) {
		if (mmc_send_scr(sc) == 0)
			break;
	}
	if (i >= 3)
		goto err_exit;

	if (mmc_swtch_func(sc, 0) != 0)
		goto err_exit;

	// 4bit
	if (mmc_extract_bits(sc->scr, 51, 4) & (1 << 2)) {
		if (mmc_set_bus_width(sc, 4) < 0)
			goto err_exit;
	}

	if (mmc_set_blocklen(sc, DEV_BSIZE) != 0)
		goto err_exit;

	{
		uint32_t argument = (1u << 31) | 0xffffff;
		// group 1
		if (mmc_extract_bits(sc->func_status, 415, 16) & (1u << 1)) {
			argument &= ~(0xf << ((1 - 1) * 4));
			argument |= (0x1 << ((1 - 1) * 4));
		}

		if (mmc_swtch_func(sc, argument) != 0)
			goto err_exit;
	}

	{
		int freq;
		switch (mmc_extract_bits(sc->func_status, 379, 4)) {
		case 1:
			freq = 50000000;
			break;
		case 0:
		default:
			freq = 25000000;
			break;
		}
		mmc_set_clock(sc, freq);
	}

	return DDI_SUCCESS;

err_exit:
	return DDI_FAILURE;
}

static void
mmc_read_block(void *arg)
{
	struct mmc_sc *sc = ((struct mmc_request *)arg)->sc;
	bd_xfer_t *xfer = ((struct mmc_request *)arg)->xfer;

	ASSERT(xfer->x_dmah != 0);
	ASSERT(xfer->x_kaddr == NULL);

	bool detach;
	mutex_enter(&sc->lock);
	detach = sc->detach;
	mutex_exit(&sc->lock);

	int r = EIO;
	if (!detach) {
		struct sda_cmd cmd = {
			.sc_index = (xfer->x_nblks == 1? CMD_READ_SINGLE: CMD_READ_MULTI),
			.sc_rtype = R1,
			.sc_nblks = xfer->x_nblks,
			.sc_blksz = DEV_BSIZE,
			.sc_flags = SDA_CMDF_READ,

			.sc_dmah = xfer->x_dmah,
			.sc_ndmac = xfer->x_ndmac,
			.sc_dmac = xfer->x_dmac,
		};
		if (sc->ocr & OCR_CCS) {
			cmd.sc_argument = xfer->x_blkno;
		} else {
			cmd.sc_argument = xfer->x_blkno * DEV_BSIZE;
		}

		if (mmc_send_cmd(sc, &cmd) == 0) {
			r = 0;
		}
	}

	mutex_enter(&sc->lock);
	list_insert_head(&sc->free_request, arg);
	mutex_exit(&sc->lock);

	bd_xfer_done(xfer, r);
}


static void
mmc_write_block(void *arg)
{
	struct mmc_sc *sc = ((struct mmc_request *)arg)->sc;
	bd_xfer_t *xfer = ((struct mmc_request *)arg)->xfer;

	ASSERT(xfer->x_dmah != 0);
	ASSERT(xfer->x_kaddr == NULL);

	bool detach;
	mutex_enter(&sc->lock);
	detach = sc->detach;
	mutex_exit(&sc->lock);

	int r = EIO;
	if (!detach) {
		struct sda_cmd cmd = {
			.sc_index = (xfer->x_nblks == 1? CMD_WRITE_SINGLE: CMD_WRITE_MULTI),
			.sc_rtype = R1,
			.sc_nblks = xfer->x_nblks,
			.sc_blksz = DEV_BSIZE,
			.sc_flags = SDA_CMDF_WRITE,

			.sc_dmah = xfer->x_dmah,
			.sc_ndmac = xfer->x_ndmac,
			.sc_dmac = xfer->x_dmac,
		};
		if (sc->ocr & OCR_CCS) {
			cmd.sc_argument = xfer->x_blkno;
		} else {
			cmd.sc_argument = xfer->x_blkno * DEV_BSIZE;
		}

		if (mmc_send_cmd(sc, &cmd) == 0)
			r = 0;
	}

	mutex_enter(&sc->lock);
	list_insert_head(&sc->free_request, arg);
	mutex_exit(&sc->lock);

	bd_xfer_done(xfer, r);
}

static int
mmc_bd_read(void *arg, bd_xfer_t *xfer)
{
	if (xfer->x_flags & BD_XFER_POLL)
		return (EIO);

	int r = 0;
	struct mmc_request *req = NULL;
	struct mmc_sc *sc = arg;
	mutex_enter(&sc->lock);
	if (!sc->detach) {
		req = list_head(&sc->free_request);
		if (req != NULL) {
			list_remove(&sc->free_request, req);
			req->sc = sc;
			req->xfer = xfer;
		} else {
			r = ENOMEM;
		}
	} else {
		r = ENXIO;
	}
	mutex_exit(&sc->lock);
	if (req) {
		if (ddi_taskq_dispatch(sc->tq, mmc_read_block, req, DDI_SLEEP) != DDI_SUCCESS) {
			mutex_enter(&sc->lock);
			list_insert_head(&sc->free_request, req);
			mutex_exit(&sc->lock);
			r = EIO;
		}
	}
	return r;
}

static int
mmc_bd_write(void *arg, bd_xfer_t *xfer)
{
	if (xfer->x_flags & BD_XFER_POLL)
		return (EIO);

	int r = 0;
	struct mmc_request *req = NULL;
	struct mmc_sc *sc = arg;
	mutex_enter(&sc->lock);
	if (!sc->detach) {
		req = list_head(&sc->free_request);
		if (req != NULL) {
			list_remove(&sc->free_request, req);
			req->sc = sc;
			req->xfer = xfer;
		} else {
			r = ENOMEM;
		}
	} else {
		r = ENXIO;
	}
	mutex_exit(&sc->lock);
	if (req) {
		if (ddi_taskq_dispatch(sc->tq, mmc_write_block, req, DDI_SLEEP) != DDI_SUCCESS) {
			mutex_enter(&sc->lock);
			list_insert_head(&sc->free_request, req);
			mutex_exit(&sc->lock);
			r = EIO;
		}
	}
	return r;
}

static void
mmc_bd_driveinfo(void *arg, bd_drive_t *drive)
{
	struct mmc_sc *sc = arg;
	drive->d_qsize = 4;
	drive->d_removable = B_FALSE;
	drive->d_hotpluggable = B_FALSE;
	drive->d_target = 0;
	drive->d_lun = 0;
	drive->d_maxxfer = MMC_MAX_TRANSFER_SIZE;

	switch (mmc_extract_bits(sc->cid, 127, 8)) {
	case 0x01: drive->d_vendor = "Panasonic"; break;
	case 0x02: drive->d_vendor = "Toshiba"; break;
	case 0x03: drive->d_vendor = "SanDisk"; break;
	case 0x1b: drive->d_vendor = "Samsung"; break;
	case 0x1d: drive->d_vendor = "AData"; break;
	case 0x27: drive->d_vendor = "Phison"; break;
	case 0x28: drive->d_vendor = "Lexar"; break;
	case 0x31: drive->d_vendor = "Silicon Power"; break;
	case 0x41: drive->d_vendor = "Kingston"; break;
	case 0x74: drive->d_vendor = "Transcend"; break;
	default: drive->d_vendor = "unknown"; break;
	}
	drive->d_vendor_len = strlen(drive->d_vendor);

	drive->d_product = kmem_zalloc(6, KM_SLEEP);
	drive->d_product[4] = mmc_extract_bits(sc->cid, 71, 8);
	drive->d_product[3] = mmc_extract_bits(sc->cid, 79, 8);
	drive->d_product[2] = mmc_extract_bits(sc->cid, 87, 8);
	drive->d_product[1] = mmc_extract_bits(sc->cid, 95, 8);
	drive->d_product[0] = mmc_extract_bits(sc->cid, 103, 8);
	drive->d_product_len = strlen(drive->d_product);

	uint32_t serial = mmc_extract_bits(sc->cid, 55, 32);
	drive->d_serial = kmem_zalloc(9, KM_SLEEP);
	sprintf(drive->d_serial, "%08x", serial);
	drive->d_serial_len = 8;
	uint32_t rev = mmc_extract_bits(sc->cid, 63, 8);
	drive->d_revision = kmem_zalloc(6, KM_SLEEP);
	sprintf(drive->d_revision, "%d.%d", rev >> 4, rev & 0xF);
	drive->d_revision_len = strlen(drive->d_revision);
}

static int
mmc_bd_mediainfo(void *arg, bd_media_t *media)
{
	struct mmc_sc *sc = arg;
	uint64_t size;
	if (mmc_extract_bits(sc->csd, 127, 2) == 0) {
		uint64_t csz = mmc_extract_bits(sc->csd, 73, 12);
		uint32_t cmult = mmc_extract_bits(sc->csd, 49, 3);
		uint32_t read_bl_len = mmc_extract_bits(sc->csd, 83, 4);
		size = ((csz + 1) * (1 << (cmult + 2))) * read_bl_len;
	}
	else if (mmc_extract_bits(sc->csd, 127, 2) == 1) {
		uint64_t csz = mmc_extract_bits(sc->csd, 69, 22);
		size = (csz + 1) * (512 * 1024);
	} else {
		return -1;
	}

	media->m_nblks = size / 512;
	media->m_blksize = 512;
	media->m_readonly = B_FALSE;
	media->m_solidstate = B_TRUE;
	return (0);
}

static bd_ops_t mmc_bd_ops = {
	BD_OPS_VERSION_0,
	mmc_bd_driveinfo,
	mmc_bd_mediainfo,
	NULL,			/* devid_init */
	NULL,			/* sync_cache */
	mmc_bd_read,
	mmc_bd_write,
};

static void
mmc_destroy(struct mmc_sc *sc)
{
	if (sc->bdh) {
		bd_free_handle(sc->bdh);
	}
	if (sc->tq) {
		ddi_taskq_destroy(sc->tq);
	}

	for (;;) {
		struct mmc_request *req = list_head(&sc->free_request);
		if (req == NULL)
			break;
		list_remove(&sc->free_request, req);
		kmem_free(req, sizeof (struct mmc_request));
	}
	if (sc->buffer.dma_handle) {
		ddi_dma_unbind_handle(sc->buffer.dma_handle);
		ddi_dma_mem_free(&sc->buffer.mem_handle);
		ddi_dma_free_handle(&sc->buffer.dma_handle);
	}
	if (sc->ihandle) {
		ddi_intr_disable(sc->ihandle);
		ddi_intr_remove_handler(sc->ihandle);
		ddi_intr_free(sc->ihandle);
	}
	if (sc->reg.handle) {
		mmc_reg_write(sc, SMHC_CTRL, 0);
		mmc_reg_write(sc, SMHC_IDMAC, 0);
		union smhc_ctrl ctrl = { .soft_rst = 1, .fifo_rst = 1, .dma_rst = 1 };
		mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);
		for (int i = 0; i < 1000; i++) {
			usecwait(200);
			ctrl.dw = mmc_reg_read(sc, SMHC_CTRL);
			if (!(ctrl.soft_rst || ctrl.fifo_rst || ctrl.dma_rst))
				break;
		}
		ddi_regs_map_free(&sc->reg.handle);
	}

	list_destroy(&sc->free_request);
	cv_destroy(&sc->waitcv);
	mutex_destroy(&sc->intrlock);
	mutex_destroy(&sc->lock);
	kmem_free(sc, sizeof (*sc));
}

static int
mmc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}
	struct mmc_sc *sc = ddi_get_driver_private(dip);

	mutex_enter(&sc->lock);
	sc->detach = true;
	mutex_exit(&sc->lock);
	ddi_taskq_wait(sc->tq);

	bd_detach_handle(sc->bdh);

	ddi_set_driver_private(sc->dip, NULL);
	mmc_destroy(sc);

	return DDI_SUCCESS;
}

static int
mmc_quiesce(dev_info_t *dip)
{
	cmn_err(CE_WARN, "%s%d: mmc_quiesce is not implemented",
	    ddi_driver_name(dip), ddi_get_instance(dip));
	return DDI_FAILURE;
}

static void
find_wifi(pnode_t nodeid, pnode_t *arg)
{
	if (*arg != OBP_NONODE)
		return;

	int namelen = prom_getproplen(nodeid, "name");
	if (namelen >= strlen("wifi")) {
		caddr_t name = __builtin_alloca(namelen);
		prom_getprop(nodeid, "name", name);
		if (memcmp(name, "wifi", strlen("wifi")) == 0)
			*arg = nodeid;
	}

	pnode_t child = prom_childnode(nodeid);
	while (child > 0) {
		find_wifi(child, arg);
		child = prom_nextnode(child);
	}
}

static int
mmc_probe(dev_info_t *dip)
{
	int len;
	char buf[80];
	pnode_t node = ddi_get_nodeid(dip);
	if (node < 0)
		return (DDI_PROBE_FAILURE);

	len = prom_getproplen(node, "status");
	if (len <= 0 || len >= sizeof(buf))
		return (DDI_PROBE_FAILURE);

	prom_getprop(node, "status", (caddr_t)buf);
	if (strcmp(buf, "ok") != 0 && (strcmp(buf, "okay") != 0))
		return (DDI_PROBE_FAILURE);

	pnode_t wifi = OBP_NONODE;
	find_wifi(node, &wifi);
	if (wifi != OBP_NONODE)
		return (DDI_PROBE_FAILURE);

	return (DDI_PROBE_SUCCESS);
}

static uint_t
mmc_intr(caddr_t arg1, caddr_t arg2)
{
	struct mmc_sc *sc = (struct mmc_sc *)arg1;
	uint_t status = DDI_INTR_UNCLAIMED;

	mutex_enter(&sc->intrlock);

	uint32_t interrupt = mmc_reg_read(sc, SMHC_RINTSTS);
	if (interrupt) {
		mmc_reg_write(sc, SMHC_RINTSTS, interrupt);
		status = DDI_INTR_CLAIMED;
		sc->interrupted |= interrupt;
	}
	uint32_t dma_interrupt = mmc_reg_read(sc, SMHC_IDST);
	if (dma_interrupt) {
		mmc_reg_write(sc, SMHC_IDST, dma_interrupt);
		status = DDI_INTR_CLAIMED;
		sc->dma_interrupted |= dma_interrupt;
	}
	if (status == DDI_INTR_CLAIMED)
		cv_signal(&sc->waitcv);

	mutex_exit(&sc->intrlock);

	return status;
}

static ddi_device_acc_attr_t mem_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STORECACHING_OK_ACC,
};

static ddi_device_acc_attr_t desc_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_MERGING_OK_ACC,
};

static ddi_device_acc_attr_t reg_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

static ddi_dma_attr_t desc_dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version	*/
	0x0000000000000000ull,		/* dma_attr_addr_lo	*/
	0x00000000FFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x00000000FFFFFFFFull,		/* dma_attr_count_max	*/
	DCACHE_LINE,			/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_maxxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_seg		*/
	1,				/* dma_attr_sgllen	*/
	0x00000001,			/* dma_attr_granular	*/
	DDI_DMA_FLAGERR			/* dma_attr_flags	*/
};

static ddi_dma_attr_t buffer_dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version	*/
	0x0000000000000000ull,		/* dma_attr_addr_lo	*/
	0x00000000FFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x0000000000000FFFull,		/* dma_attr_count_max	*/
	DCACHE_LINE,			/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_maxxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_seg		*/
	1,				/* dma_attr_sgllen	*/
	0x00000001,			/* dma_attr_granular	*/
	DDI_DMA_FLAGERR			/* dma_attr_flags	*/
};

static ddi_dma_attr_t dma_attr = {
	DMA_ATTR_V0,						/* dma_attr_version	*/
	0x0000000000000000ull,					/* dma_attr_addr_lo	*/
	0x00000000FFFFFFFFull,					/* dma_attr_addr_hi	*/
	0x0000000000001FFFull - DCACHE_LINE,			/* dma_attr_count_max	*/
	DCACHE_LINE,						/* dma_attr_align	*/
	0x00000FFF,						/* dma_attr_burstsizes	*/
	0x00000001,						/* dma_attr_minxfer	*/
	MMC_MAX_TRANSFER_SIZE,					/* dma_attr_maxxfer	*/
	0x00000000FFFFFFFFull,					/* dma_attr_seg		*/
	MMC_DESC_BUFFER_SIZE / sizeof(struct smhc_desc),	/* dma_attr_sgllen	*/
	512,							/* dma_attr_granular	*/
	DDI_DMA_FLAGERR						/* dma_attr_flags	*/
};

static int
mmc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	struct mmc_sc *sc = kmem_zalloc(sizeof(struct mmc_sc), KM_SLEEP);
	ddi_set_driver_private(dip, sc);

	sc->dip = dip;

	mutex_init(&sc->lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&sc->intrlock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&sc->waitcv, NULL, CV_DRIVER, NULL);
	list_create(&sc->free_request, sizeof(struct mmc_request), offsetof(struct mmc_request, node));

	int rv;
	rv = ddi_regs_map_setup(sc->dip, 0, (caddr_t*)&sc->reg.addr, 0, 0, &reg_acc_attr, &sc->reg.handle);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_regs_map_setup failed (%d)!", rv);
		sc->reg.handle = 0;
		goto err_exit;
	}

	rv = ddi_dma_alloc_handle(dip, &buffer_dma_attr, DDI_DMA_SLEEP, NULL, &sc->buffer.dma_handle);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_dma_alloc_handle failed (%d)!", rv);
		sc->buffer.dma_handle = 0;
		goto err_exit;
	}

	rv = ddi_dma_mem_alloc(sc->buffer.dma_handle, MMC_BUFFER_SIZE,
	    &mem_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &sc->buffer.addr, &sc->buffer.size, &sc->buffer.mem_handle);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_dma_mem_alloc failed (%d)!", rv);
		ddi_dma_free_handle(&sc->buffer.dma_handle);
		sc->buffer.dma_handle = 0;
		goto err_exit;
	}
	uint_t ndmac;
	rv = ddi_dma_addr_bind_handle(sc->buffer.dma_handle, NULL, sc->buffer.addr,
	    sc->buffer.size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &sc->buffer.cookie, &ndmac);
	if ((rv != DDI_DMA_MAPPED) || (ndmac != 1)) {
		cmn_err(CE_WARN, "ddi_dma_addr_bind_handle failed (%d, %u)!",
		    rv, ndmac);
		ddi_dma_mem_free(&sc->buffer.mem_handle);
		ddi_dma_free_handle(&sc->buffer.dma_handle);
		sc->buffer.dma_handle = 0;
		goto err_exit;
	}

	rv = ddi_dma_alloc_handle(dip, &desc_dma_attr, DDI_DMA_SLEEP, NULL, &sc->desc.dma_handle);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_dma_alloc_handle failed (%d)!", rv);
		sc->buffer.dma_handle = 0;
		goto err_exit;
	}

	rv = ddi_dma_mem_alloc(sc->desc.dma_handle, MMC_DESC_BUFFER_SIZE,
	    &desc_acc_attr, DDI_DMA_CONSISTENT | IOMEM_DATA_UC_WR_COMBINE, DDI_DMA_SLEEP, NULL,
	    &sc->desc.addr, &sc->desc.size, &sc->desc.mem_handle);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_dma_mem_alloc failed (%d)!", rv);
		ddi_dma_free_handle(&sc->desc.dma_handle);
		sc->buffer.dma_handle = 0;
		goto err_exit;
	}
	rv = ddi_dma_addr_bind_handle(sc->desc.dma_handle, NULL, sc->desc.addr,
	    sc->desc.size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &sc->desc.cookie, &ndmac);
	if ((rv != DDI_DMA_MAPPED) || (ndmac != 1)) {
		cmn_err(CE_WARN, "ddi_dma_addr_bind_handle failed (%d, %u)!",
		    rv, ndmac);
		ddi_dma_mem_free(&sc->desc.mem_handle);
		ddi_dma_free_handle(&sc->desc.dma_handle);
		sc->desc.dma_handle = 0;
		goto err_exit;
	}

	for (int i = 0; i < MMC_REQUESTS_MAX; i++) {
		void *req = kmem_alloc(sizeof (struct mmc_request), KM_NOSLEEP);
		if (req == NULL) {
			cmn_err(CE_WARN, "kmem_alloc failed for mmc_request");
			goto err_exit;
		}
		list_insert_head(&sc->free_request, req);
	}

	int actual;
	rv = ddi_intr_alloc(sc->dip, &sc->ihandle, DDI_INTR_TYPE_FIXED, 0, 1, &actual, DDI_INTR_ALLOC_STRICT);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_intr_alloc failed (%d)!", rv);
		sc->ihandle = 0;
		goto err_exit;
	}

	rv = ddi_intr_add_handler(sc->ihandle, mmc_intr, sc, NULL);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_intr_add_handler failed (%d)!", rv);
		ddi_intr_free(sc->ihandle);
		sc->ihandle = 0;
		goto err_exit;
	}

	rv = ddi_intr_enable(sc->ihandle);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed enabling interrupts");
		ddi_intr_remove_handler(sc->ihandle);
		ddi_intr_free(sc->ihandle);
		sc->ihandle = 0;
		goto err_exit;
	}

	rv = mmc_init(sc);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "mmc_init failed!");
		goto err_exit;
	}

	sc->tq = ddi_taskq_create(sc->dip, "taskq", 1, TASKQ_DEFAULTPRI, 0);
	if (sc->tq == NULL) {
		cmn_err(CE_WARN, "ddi_taskq_create failed!");
		goto err_exit;
	}

	sc->bdh = bd_alloc_handle(sc, &mmc_bd_ops, &dma_attr, KM_SLEEP);
	if (sc->bdh == NULL) {
		cmn_err(CE_WARN, "bd_alloc_handle failed!");
		goto err_exit;
	}

	rv = bd_attach_handle(sc->dip, sc->bdh);
	if (rv != DDI_SUCCESS) {
		cmn_err(CE_WARN, "bd_attach_handle failed!");
		goto err_exit;
	}

	return DDI_SUCCESS;
err_exit:
	mmc_destroy(sc);
	return (DDI_FAILURE);
}

static struct dev_ops mmc_dev_ops = {
	DEVO_REV,			/* devo_rev */
	0,				/* devo_refcnt */
	ddi_no_info,			/* devo_getinfo */
	nulldev,			/* devo_identify */
	mmc_probe,			/* devo_probe */
	mmc_attach,			/* devo_attach */
	mmc_detach,			/* devo_detach */
	nodev,				/* devo_reset */
	NULL,				/* devo_cb_ops */
	NULL,				/* devo_bus_ops */
	NULL,				/* devo_power */
	mmc_quiesce,			/* devo_quiesce */
};

static struct modldrv mmc_modldrv = {
	&mod_driverops,			/* drv_modops */
	"Allwinner D1 MMC",		/* drv_linkinfo */
	&mmc_dev_ops			/* drv_dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,			/* ml_rev */
	{ &mmc_modldrv, NULL }	/* ml_linkage */
};

int
_init(void)
{
	int i;

	bd_mod_init(&mmc_dev_ops);
	if ((i = mod_install(&modlinkage)) != 0) {
		bd_mod_fini(&mmc_dev_ops);
	}
	return (i);
}

int
_fini(void)
{
	int i;

	if ((i = mod_remove(&modlinkage)) == 0) {
		bd_mod_fini(&mmc_dev_ops);
	}
	return (i);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

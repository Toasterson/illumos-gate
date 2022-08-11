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
 * Copyright 2017 Hayashi Naoyuki
 */

#include <stdbool.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/promif.h>
#include <sys/salib.h>
#include <sys/byteorder.h>
#include <sys/sysmacros.h>
#include <sys/t-head.h>
#include <sys/dditypes.h>
#include <sys/devops.h>
#include <sys/sdcard/sda.h>
#include <sys/platform.h>
#include <util/sscanf.h>
#include <sys/platmod.h>
#include <sys/smhcreg.h>
#include "prom_dev.h"
#include "boot_plat.h"
#include "mmc.h"

#define BUFFER_SIZE	0x1000

#define CCU_BASE	0x02001000
#define HOSC_FREQ	24000000

struct mmc_sc {
	uint64_t base;
	uint32_t ocr;
	uint32_t ocr_avail;
	uint32_t vdd;
	uint32_t csd[4];
	uint32_t cid[4];
	uint32_t scr[2];
	uint32_t func_status[16];
	uint32_t rca;
	char *buffer;
	struct smhc_desc *desc;
};

static struct mmc_sc *mmc_dev[3];

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

static void
msecwait(int msec)
{
	uint_t end = prom_gettime() + msec + 2;
	while (prom_gettime() < end) {}
}

static uint64_t
get_msec()
{
	return prom_gettime();
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
	uint32_t pll_peri = *(volatile uint32_t *)(CCU_BASE + 0x20);
	uint32_t pll_p0 = ((pll_peri >> 16) & 0x7);
	uint32_t pll_n = ((pll_peri >> 8) & 0xFF);
	uint32_t pll_input_div2 = ((pll_peri >> 1) & 1);

	return HOSC_FREQ * (pll_n + 1) / (pll_input_div2 + 1) / (pll_p0 + 1) / 2;
}

static const struct smhc_info *
mmc_get_smhc_info(struct mmc_sc *sc)
{
	int c;
	for (c = 0; c < ARRAY_SIZE(sun20i_info); c++) {
		if (sun20i_info[c].base == sc->base)
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
	if (info == NULL)
		return -1;

	volatile uint32_t *clk = (volatile uint32_t *)(uintptr_t)info->clk;
	*clk = (1u << 31) | (src_sel << 24) | (n << 8) | (div - 1);
	msecwait(10);

	return 0;
}

static void
mmc_reg_write(struct mmc_sc *sc, size_t offset, uint32_t val)
{
	*(volatile uint32_t *)(sc->base + offset) = val;
}

static uint32_t
mmc_reg_read(struct mmc_sc *sc, size_t offset)
{
	uint32_t val = *(volatile uint32_t *)(sc->base + offset);
	return val;
}

static void
mmc_set_sd_clock(struct mmc_sc *sc, bool enable)
{
	union smhc_clkdiv clkdiv = { mmc_reg_read(sc, SMHC_CLKDIV) };
	clkdiv.cclk_enb = (enable? 1: 0);
	clkdiv.cclk_div = 0;
	clkdiv.cclk_ctrl = 0;
	clkdiv.mask_data0 = 1;
	mmc_reg_write(sc, SMHC_CLKDIV, clkdiv.dw);

	union smhc_cmd cmd = { .wait_pre_over = 1, .prg_clk = 1, .cmd_load = 1 };
	mmc_reg_write(sc, SMHC_CMD, cmd.dw);
	do {
		cmd.dw = mmc_reg_read(sc, SMHC_CMD);
	} while (cmd.cmd_load);

	clkdiv.dw = mmc_reg_read(sc, SMHC_CLKDIV);
	clkdiv.mask_data0 = 0;
	mmc_reg_write(sc, SMHC_CLKDIV, clkdiv.dw);
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
	mmc_set_sd_clock(sc, false);

	if (mmc_set_clockrate(sc, hz * 2) < 0)
		return -1;

	mmc_calibrate(sc);

	mmc_set_sd_clock(sc, true);

	return 0;
}

static int
mmc_reset(struct mmc_sc *sc)
{
	const struct smhc_info *info = mmc_get_smhc_info(sc);
	if (info == NULL)
		return -1;

	volatile uint32_t *bgr = (volatile uint32_t *)(uintptr_t)info->bgr;
	uint32_t x = *bgr;
	x &= ~(info->bgr_rst | info->bgr_gate);
	*bgr = x;
	msecwait(100);
	*bgr |= (info->bgr_rst | info->bgr_gate);
	msecwait(10);
	return 0;
}

static int
mmc_soft_reset(struct mmc_sc *sc)
{
	uint32_t old_ctrl = mmc_reg_read(sc, SMHC_CTRL);
	uint32_t old_idmac = mmc_reg_read(sc, SMHC_IDMAC);
	mmc_reg_write(sc, SMHC_CTRL, 0);
	mmc_reg_write(sc, SMHC_IDMAC, 0);

	if (sc->desc)
		sc->desc->des0.dw = 0;

	union smhc_idmac idmac = { .idmac_rst = 1 };
	mmc_reg_write(sc, SMHC_IDMAC, idmac.dw);

	union smhc_ctrl ctrl = { .soft_rst = 1, .fifo_rst = 1, .dma_rst = 1 };
	mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);
	for (int i = 0; i < 200; i++) {
		msecwait(1);
		ctrl.dw = mmc_reg_read(sc, SMHC_CTRL);
		if (!(ctrl.soft_rst || ctrl.fifo_rst || ctrl.dma_rst))
			break;
	}

	mmc_reg_write(sc, SMHC_CTRL, old_ctrl);
	mmc_reg_write(sc, SMHC_IDMAC, old_idmac);

	return (ctrl.soft_rst || ctrl.fifo_rst || ctrl.dma_rst)? -1: 0;
}

static int
mmc_wait_intr(struct mmc_sc *sc, uint32_t mask, uint32_t dma_mask, uint64_t msec)
{
	uint64_t timeout = get_msec() + msec;
	bool completed = false;
	bool error_occured = false;
	bool timeout_occurred = false;

	for (;;) {
		timeout_occurred = (get_msec() > timeout);
		uint32_t rintsts = mmc_reg_read(sc, SMHC_RINTSTS);
		uint32_t idst = mmc_reg_read(sc, SMHC_IDST);
		if ((rintsts & mask) == mask && (idst & dma_mask) == dma_mask)
			completed = true;
		union smhc_idst dma_error = { .fatal_berr_int = 1, .err_flag_sum = 1 };
		if ((rintsts & interrupt_error.dw) || (idst & dma_error.dw))
			error_occured = true;

		if ((timeout_occurred && !completed) || error_occured) {
			prom_printf("%s: rintsts %08x idst %08x\n", __func__, rintsts, idst);
			prom_printf("%s: mask %08x dma_mask %08x\n", __func__, mask, dma_mask);
		}

		if (completed || error_occured || timeout_occurred) {
			mmc_reg_write(sc, SMHC_RINTSTS, rintsts);
			mmc_reg_write(sc, SMHC_IDST, idst);
			break;
		}
	}

	if (error_occured || !completed) {
		mmc_soft_reset(sc);
		return -1;
	}

	return 0;
}

static int
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

	if (cmdp->sc_flags & (SDA_CMDF_READ | SDA_CMDF_WRITE)) {
		cmd.data_trans = 1;
		cmd.wait_pre_over = 1;
		if (cmdp->sc_flags & SDA_CMDF_WRITE)
			cmd.trans_dir = 1;
		if (cmdp->sc_index == CMD_READ_MULTI || cmdp->sc_index == CMD_WRITE_MULTI)
			cmd.stop_cmd_flag = 1;

		union smhc_blksiz blksiz = {.blk_sz = cmdp->sc_blksz};
		mmc_reg_write(sc, SMHC_BLKSIZ, blksiz.dw);

		union smhc_bytcnt bytcnt = {.byte_cnt = cmdp->sc_blksz * cmdp->sc_nblks};
		mmc_reg_write(sc, SMHC_BYTCNT, bytcnt.dw);

		typeof(sc->desc->des0) des0 = { .chain_mod = 1, .first_flag = 1, .last_flag = 1, .hold = 1 };
		typeof(sc->desc->des1) des1 = { .buff_size = cmdp->sc_blksz * cmdp->sc_nblks };
		typeof(sc->desc->des2) des2 = { .buff_addr = (uintptr_t)cmdp->sc_kvaddr / 4 };
		typeof(sc->desc->des3) des3 = { .next_desp_addr = 0 };
		sc->desc->des1.dw = des1.dw;
		sc->desc->des2.dw = des2.dw;
		sc->desc->des3.dw = des3.dw;
		asm volatile ("fence":::"memory");
		sc->desc->des0.dw = des0.dw;

		asm volatile ("fence":::"memory");
		union smhc_idmac idmac_enb = { .fix_bust_ctrl = 1, .idmac_enb = 1, .des_load_ctrl = 1 };
		mmc_reg_write(sc, SMHC_IDMAC, idmac_enb.dw);
	}

	union smhc_cmdarg cmdarg = { .cmd_arg = cmdp->sc_argument };
	mmc_reg_write(sc, SMHC_CMDARG, cmdarg.dw);
	mmc_reg_write(sc, SMHC_CMD, cmd.dw);

	return 0;
}

static int
mmc_wait_cmd_done(struct mmc_sc *sc, struct sda_cmd *cmd)
{
	union smhc_rintsts cmd_done = { .cc = 1, };
	union smhc_idst dma_done = { 0 };
	if (cmd->sc_flags & (SDA_CMDF_READ | SDA_CMDF_WRITE)) {
		cmd_done.dtc = 1;
		dma_done.rx_int = !!(cmd->sc_flags & SDA_CMDF_READ);
		dma_done.tx_int = !!(cmd->sc_flags & SDA_CMDF_WRITE);
		if (cmd->sc_index == CMD_READ_MULTI || cmd->sc_index == CMD_WRITE_MULTI) {
			cmd_done.acd = 1;
		}
	}
	if (mmc_wait_intr(sc, cmd_done.dw, dma_done.dw, 5000) < 0)
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

	if (cmd->sc_flags & (SDA_CMDF_READ | SDA_CMDF_WRITE)) {
		thead_dcache_flush((uintptr_t)sc->desc, sizeof(struct smhc_desc));
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
		msecwait(1000);
	}
	if (retry_remain == 0)
		return -1;

	if (mmc_start_cmd(sc, cmd) != 0)
		goto err_exit;

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
		.sc_kvaddr = sc->buffer,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	thead_dcache_flush((uintptr_t)sc->buffer, sizeof(sc->scr));

	for (int i = 0; i < ARRAY_SIZE(sc->scr); i++)
		sc->scr[i] = ntohl(*(uint32_t *)(sc->buffer + sizeof(sc->scr) * (ARRAY_SIZE(sc->scr) - 1 - i)));

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
		.sc_kvaddr = sc->buffer,
	};
	if (mmc_send_cmd(sc, &cmd) != 0)
		return -1;

	thead_dcache_flush((uintptr_t)sc->buffer, sizeof(sc->func_status));

	for (int i = 0; i < ARRAY_SIZE(sc->func_status); i++)
		sc->func_status[i] = ntohl(*(uint32_t *)(sc->buffer + sizeof(sc->func_status[0]) * (ARRAY_SIZE(sc->func_status) - 1 - i)));

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
mmc_open(const char *name)
{
	pnode_t node;
	int fd;

	for (fd = 0; fd < sizeof(mmc_dev) / sizeof(mmc_dev[0]); fd++) {
		if (mmc_dev[fd] == NULL)
			break;
	}

	if (fd == sizeof(mmc_dev) / sizeof(mmc_dev[0]))
		return -1;

	struct mmc_sc *sc = kmem_alloc(sizeof(struct mmc_sc), 0);
	memset(sc, 0, sizeof(struct mmc_sc));

	node = prom_finddevice(name);
	if (node <= 0)
		return -1;

	if (!prom_is_compatible(node, "allwinner,sun20i-d1-mmc"))
		return -1;

	if (prom_get_reg_address(node, 0, &sc->base) != 0)
		return -1;

	sc->buffer = malloc(BUFFER_SIZE + 2 * DCACHE_LINE);
	thead_dcache_flush((uintptr_t)sc->buffer, BUFFER_SIZE + 2 * DCACHE_LINE);
	sc->buffer = (char *)roundup((uintptr_t)sc->buffer, DCACHE_LINE);

	uintptr_t desc_addr = (uintptr_t)kmem_alloc(MMU_PAGESIZE * 2, 0);
	desc_addr = roundup(desc_addr, MMU_PAGESIZE);
	memset((void*)desc_addr, 0, MMU_PAGESIZE);
	uintptr_t desc_vaddr = memlist_get(MMU_PAGESIZE, MMU_PAGESIZE, &ptmplistp);
	map_phys(PTE_A | PTE_D | PTE_G | PTE_W | PTE_R  | pte_mt_nc, (caddr_t)desc_vaddr, desc_addr, MMU_PAGESIZE);
	thead_dcache_clean(desc_addr, MMU_PAGESIZE);
	sc->desc = (struct smhc_desc *)desc_vaddr;

	// reset
	mmc_reset(sc);

	if (mmc_soft_reset(sc) < 0)
		return -1;

	{
		union smhc_fifoth fifoth = { .tx_tl = 240, .rx_tl = 15, .bsize_of_trans = 3 };
		mmc_reg_write(sc, SMHC_FIFOTH, fifoth.dw);

		union smhc_tmout tmout = { .rto_lmt = 0xff, .dto_lmt = 0xffffff };
		mmc_reg_write(sc, SMHC_TMOUT, tmout.dw);

		mmc_reg_write(sc, SMHC_INTMASK, 0);
		mmc_reg_write(sc, SMHC_IDIE, 0);
		mmc_reg_write(sc, SMHC_RINTSTS, 0xffffffff);
		mmc_reg_write(sc, SMHC_IDST, 0xffffffff);

		union smhc_idie idie = { .tx_int = 1, .rx_int = 1, .fatal_berr_int = 1, .err_flag_sum = 1 };
		mmc_reg_write(sc, SMHC_IDIE, idie.dw);

		union smhc_dlba dlba = { .des_base_addr = (uintptr_t)desc_addr / 4 };
		mmc_reg_write(sc, SMHC_DLBA, dlba.dw);

		union smhc_ctrl ctrl = { mmc_reg_read(sc, SMHC_CTRL) };
		ctrl.dma_enb = 1;
		mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);
		ctrl.dma_rst = 1;
		mmc_reg_write(sc, SMHC_CTRL, ctrl.dw);

		union smhc_idmac idmac = { .idmac_rst = 1 };
		mmc_reg_write(sc, SMHC_IDMAC, idmac.dw);
	}

	mmc_set_clock(sc, 400000);

	sc->ocr_avail = OCR_33_34V | OCR_32_33V;

	int i;
	if (mmc_go_idle_state(sc) != 0)
		return -1;

	if (mmc_send_if_cond(sc) != 0)
		return -1;

	for (i = 0; i < 1000; i++) {
		if (mmc_sd_send_ocr(sc) != 0)
			return -1;

		if (sc->ocr & OCR_POWER_UP)
			break;

		msecwait(1);
	}
	if (i >= 1000)
		return -1;

	if (mmc_all_send_cid(sc) != 0)
		return -1;

	if (mmc_send_relative_addr(sc) != 0)
		return -1;

	if (mmc_send_csd(sc) != 0)
		return -1;

	if (mmc_select_card(sc) != 0)
		return -1;

	for (i = 0; i < 3; i++) {
		if (mmc_send_scr(sc) == 0)
			break;
	}
	if (i >= 3)
		return -1;

	if (mmc_swtch_func(sc, 0) != 0)
		return -1;

	// 4bit
	if (mmc_extract_bits(sc->scr, 51, 4) & (1 << 2)) {
		if (mmc_set_bus_width(sc, 4) < 0)
			return -1;
	}

	if (mmc_set_blocklen(sc, DEV_BSIZE) != 0)
		return -1;

	{
		uint32_t argument = (1u << 31) | 0xffffff;
		// group 1
		if (mmc_extract_bits(sc->func_status, 415, 16) & (1u << 1)) {
			argument &= ~(0xf << ((1 - 1) * 4));
			argument |= (0x1 << ((1 - 1) * 4));
		}

		if (mmc_swtch_func(sc, argument) != 0)
			return -1;
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

	mmc_dev[fd] = sc;

	return fd;
}

static ssize_t
mmc_read(int dev, caddr_t buf, size_t buf_len, uint_t startblk)
{
	size_t read_size = buf_len;
	struct mmc_sc *sc = mmc_dev[dev];

	while (read_size > 0) {
		size_t nblks = MIN(read_size, BUFFER_SIZE) / DEV_BSIZE;

		struct sda_cmd cmd = {
			.sc_index = (nblks == 1? CMD_READ_SINGLE: CMD_READ_MULTI),
			.sc_rtype = R1,
			.sc_nblks = nblks,
			.sc_blksz = DEV_BSIZE,
			.sc_flags = SDA_CMDF_READ,
			.sc_kvaddr = sc->buffer,
		};
		if (sc->ocr & OCR_CCS) {
			cmd.sc_argument = startblk;
		} else {
			cmd.sc_argument = startblk * DEV_BSIZE;
		}
		if (mmc_send_cmd(sc, &cmd) < 0)
			return -1;

		thead_dcache_flush((uintptr_t)sc->buffer, nblks * DEV_BSIZE);
		memcpy(buf, sc->buffer, nblks * DEV_BSIZE);

		buf += nblks * DEV_BSIZE;
		startblk += nblks;
		read_size -= nblks * DEV_BSIZE;
	}

	return buf_len;
}

static int
mmc_match(const char *path)
{
	pnode_t node = prom_finddevice(path);
	if (node <= 0)
		return 0;

	if (!prom_is_compatible(node, "allwinner,sun20i-d1-mmc"))
		return 0;

	return 1;
}

static int
mmc_close(int dev)
{
	struct mmc_sc *sc = mmc_dev[dev];
	if (sc == NULL)
		return -1;

	mmc_reg_write(sc, SMHC_CTRL, 0);
	mmc_reg_write(sc, SMHC_IDMAC, 0);

	mmc_soft_reset(sc);

	return 0;
}

static struct prom_dev mmc_prom_dev =
{
	.match = mmc_match,
	.open = mmc_open,
	.read = mmc_read,
	.close = mmc_close,
};

void init_mmc(void)
{
	prom_register(&mmc_prom_dev);
}


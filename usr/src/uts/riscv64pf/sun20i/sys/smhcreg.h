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
#include <sys/platform.h>


#define SMHC_CTRL		0x0000
#define SMHC_CLKDIV		0x0004
#define SMHC_TMOUT		0x0008
#define SMHC_CTYPE		0x000C
#define SMHC_BLKSIZ		0x0010
#define SMHC_BYTCNT		0x0014
#define SMHC_CMD		0x0018
#define SMHC_CMDARG		0x001C
#define SMHC_RESP0		0x0020
#define SMHC_RESP1		0x0024
#define SMHC_RESP2		0x0028
#define SMHC_RESP3		0x002C
#define SMHC_INTMASK		0x0030
#define SMHC_MINTSTS		0x0034
#define SMHC_RINTSTS		0x0038
#define SMHC_STATUS		0x003C
#define SMHC_FIFOTH		0x0040
#define SMHC_FUNS		0x0044
#define SMHC_TBC0		0x0048
#define SMHC_TBC1		0x004C
#define SMHC_CSDC		0x0054
#define SMHC_A12A		0x0058
#define SMHC_NTSR		0x005C
#define SMHC_HWRST		0x0078
#define SMHC_IDMAC		0x0080
#define SMHC_DLBA		0x0084
#define SMHC_IDST		0x0088
#define SMHC_IDIE		0x008C
#define SMHC_THLD		0x0100
#define SMHC_SFC		0x0104
#define SMHC_A23A		0x0108
#define EMMC_DDR_SBIT_DET	0x010C
#define SMHC_EXT_CMD		0x0138
#define SMHC_EXT_RESP		0x013C
#define SMHC_DRV_DL		0x0140
#define SMHC_SAMP_DL		0x0144
#define SMHC_DS_DL		0x0148
#define SMHC_HS400_DL		0x014C
#define SMHC_FIFO		0x0200


union smhc_ctrl {
	uint32_t dw;
	struct {
		uint32_t soft_rst	:  1;
		uint32_t fifo_rst	:  1;
		uint32_t dma_rst	:  1;
		uint32_t		:  1;
		uint32_t int_enb	:  1;
		uint32_t dma_enb	:  1;
		uint32_t		:  2;
		uint32_t cd_dbc_enb	:  1;
		uint32_t		:  1;
		uint32_t ddr_mod_sel	:  1;
		uint32_t time_unit_dat	:  1;
		uint32_t time_unit_cmd	:  1;
		uint32_t		: 18;
		uint32_t fifo_ac_mod	:  1;
	};
};

union smhc_clkdiv {
	uint32_t dw;
	struct {
		uint32_t cclk_div	:  8;
		uint32_t		:  8;
		uint32_t cclk_enb	:  1;
		uint32_t cclk_ctrl	:  1;
		uint32_t		: 13;
		uint32_t mask_data0	:  1;
	};
};

union smhc_tmout {
	uint32_t dw;
	struct {
		uint32_t rto_lmt	:  8;
		uint32_t dto_lmt	: 24;
	};
};

union smhc_ctype {
	uint32_t dw;
	struct {
		uint32_t card_wid	:  2;
		uint32_t		: 30;
	};
};

union smhc_blksiz {
	uint32_t dw;
	struct {
		uint32_t blk_sz		: 16;
		uint32_t		: 16;
	};
};

union smhc_bytcnt {
	uint32_t dw;
	struct {
		uint32_t byte_cnt	: 32;
	};
};

union smhc_cmd {
	uint32_t dw;
	struct {
		uint32_t cmd_idx	:  6;
		uint32_t resp_rcv	:  1;
		uint32_t long_resp	:  1;
		uint32_t chk_resp_crc	:  1;
		uint32_t data_trans	:  1;
		uint32_t trans_dir	:  1;
		uint32_t trans_mode	:  1;
		uint32_t stop_cmd_flag	:  1;
		uint32_t wait_pre_over	:  1;
		uint32_t stop_abt_cmd	:  1;
		uint32_t send_init_seq	:  1;
		uint32_t		:  5;
		uint32_t prg_clk	:  1;
		uint32_t		:  2;
		uint32_t boot_mod	:  2;
		uint32_t exp_boot_ack	:  1;
		uint32_t boot_abt	:  1;
		uint32_t vol_sw		:  1;
		uint32_t		:  2;
		uint32_t cmd_load	:  1;
	};
};

union smhc_cmdarg {
	uint32_t dw;
	struct {
		uint32_t cmd_arg	: 32;
	};
};

union smhc_resp0 {
	uint32_t dw;
	struct {
		uint32_t cmd_resp0	: 32;
	};
};

union smhc_resp1 {
	uint32_t dw;
	struct {
		uint32_t cmd_resp1	: 32;
	};
};

union smhc_resp2 {
	uint32_t dw;
	struct {
		uint32_t cmd_resp2	: 32;
	};
};

union smhc_resp3 {
	uint32_t dw;
	struct {
		uint32_t cmd_resp3	: 32;
	};
};

union smhc_intmask {
	uint32_t dw;
	struct {
		uint32_t		:  1;
		uint32_t re		:  1;
		uint32_t cc		:  1;
		uint32_t dtc		:  1;
		uint32_t dtr		:  1;
		uint32_t drr		:  1;
		uint32_t rce		:  1;
		uint32_t dce		:  1;
		uint32_t rto_back	:  1;
		uint32_t dto_bds	:  1;
		uint32_t dsto_vsd	:  1;
		uint32_t fu_fo		:  1;
		uint32_t cb_iw		:  1;
		uint32_t dse_bc		:  1;
		uint32_t acd		:  1;
		uint32_t dee		:  1;
		uint32_t sdio		:  1;
		uint32_t		: 13;
		uint32_t card_insert	:  1;
		uint32_t card_removal	:  1;
	};
};

union smhc_mintsts {
	uint32_t dw;
	struct {
		uint32_t		:  1;
		uint32_t re		:  1;
		uint32_t cc		:  1;
		uint32_t dtc		:  1;
		uint32_t dtr		:  1;
		uint32_t drr		:  1;
		uint32_t rce		:  1;
		uint32_t dce		:  1;
		uint32_t rto_back	:  1;
		uint32_t dto_bds	:  1;
		uint32_t dsto_vsd	:  1;
		uint32_t fu_fo		:  1;
		uint32_t cb_iw		:  1;
		uint32_t dse_bc		:  1;
		uint32_t acd		:  1;
		uint32_t dee		:  1;
		uint32_t sdio		:  1;
		uint32_t		: 13;
		uint32_t card_insert	:  1;
		uint32_t card_removal	:  1;
	};
};

union smhc_rintsts {
	uint32_t dw;
	struct {
		uint32_t		:  1;
		uint32_t re		:  1;
		uint32_t cc		:  1;
		uint32_t dtc		:  1;
		uint32_t dtr		:  1;
		uint32_t drr		:  1;
		uint32_t rce		:  1;
		uint32_t dce		:  1;
		uint32_t rto_back	:  1;
		uint32_t dto_bds	:  1;
		uint32_t dsto_vsd	:  1;
		uint32_t fu_fo		:  1;
		uint32_t cb_iw		:  1;
		uint32_t dse_bc		:  1;
		uint32_t acd		:  1;
		uint32_t dee		:  1;
		uint32_t sdio		:  1;
		uint32_t		: 13;
		uint32_t card_insert	:  1;
		uint32_t card_removal	:  1;
	};
};

union smhc_status {
	uint32_t dw;
	struct {
		uint32_t fifo_rx_level	:  1;
		uint32_t fifo_tx_level	:  1;
		uint32_t fifo_empty	:  1;
		uint32_t fifo_full	:  1;
		uint32_t fsm_sta	:  4;
		uint32_t card_present	:  1;
		uint32_t card_busy	:  1;
		uint32_t fsm_busy	:  1;
		uint32_t resp_idx	:  6;
		uint32_t fifo_level	:  9;
		uint32_t		:  5;
		uint32_t dma_req	:  1;
	};
};

union smhc_fifoth {
	uint32_t dw;
	struct {
		uint32_t tx_tl		:  8;
		uint32_t		:  8;
		uint32_t rx_tl		:  8;
		uint32_t		:  4;
		uint32_t bsize_of_trans	:  3;
		uint32_t		:  1;
	};
};

union smhc_funs {
	uint32_t dw;
	struct {
		uint32_t host_send_mmc_irqresq	:  1;
		uint32_t read_wait		:  1;
		uint32_t abt_rdata		:  1;
		uint32_t			: 29;
	};
};

union smhc_tbc0 {
	uint32_t dw;
	struct {
		uint32_t tbc0		: 32;
	};
};

union smhc_tbc1 {
	uint32_t dw;
	struct {
		uint32_t tbc1		: 32;
	};
};

union smhc_csdc {
	uint32_t dw;
	struct {
		uint32_t crc_det_para	:  4;
		uint32_t		: 28;
	};
};

union smhc_a12a {
	uint32_t dw;
	struct {
		uint32_t sd_a12a	: 16;
		uint32_t		: 16;
	};
};

union smhc_ntsr {
	uint32_t dw;
	struct {
		uint32_t hs400_new_sample_en		:  1;
		uint32_t				:  3;
		uint32_t cmd_sample_timing_phase	:  2;
		uint32_t				:  2;
		uint32_t dat_sample_timing_phase	:  2;
		uint32_t				:  6;
		uint32_t cmd_send_rx_phase_clr		:  1;
		uint32_t				:  3;
		uint32_t dat_recv_rx_phase_clr		:  1;
		uint32_t dat_trans_rx_phase_clr		:  1;
		uint32_t dat_crc_status_rx_phase_clr	:  1;
		uint32_t				:  1;
		uint32_t cmd_dat_rx_phase_clr		:  1;
		uint32_t				:  6;
		uint32_t mode_select			:  1;
	};
};

union smhc_hwrst {
	uint32_t dw;
	struct {
		uint32_t hw_rst	:  1;
		uint32_t	: 31;
	};
};

union smhc_idmac {
	uint32_t dw;
	struct {
		uint32_t idmac_rst	:  1;
		uint32_t fix_bust_ctrl	:  1;
		uint32_t		:  5;
		uint32_t idmac_enb	:  1;
		uint32_t		:  3;
		uint32_t		: 20;
		uint32_t des_load_ctrl	:  1;
	};
};

union smhc_dlba {
	uint32_t dw;
	struct {
		uint32_t des_base_addr	: 32;
	};
};

union smhc_idst {
	uint32_t dw;
	struct {
		uint32_t tx_int		:  1;
		uint32_t rx_int		:  1;
		uint32_t fatal_berr_int	:  1;
		uint32_t		:  1;
		uint32_t des_unavl_int	:  1;
		uint32_t err_flag_sum	:  1;
		uint32_t		:  2;
		uint32_t nor_int_sum	:  1;
		uint32_t abn_int_sum	:  1;
		uint32_t idmac_err_sta	:  3;
		uint32_t		:  4;
		uint32_t		: 15;
	};
};

union smhc_idie {
	uint32_t dw;
	struct {
		uint32_t tx_int		:  1;
		uint32_t rx_int		:  1;
		uint32_t fatal_berr_int	:  1;
		uint32_t		:  1;
		uint32_t des_unavl_int	:  1;
		uint32_t err_flag_sum	:  1;
		uint32_t		: 26;
	};
};

union smhc_thld {
	uint32_t dw;
	struct {
		uint32_t card_rd_thld_enb	:  1;
		uint32_t bcig			:  1;
		uint32_t card_wr_thld_enb	:  1;
		uint32_t			: 13;
		uint32_t card_wr_thld		: 12;
		uint32_t			:  4;
	};
};

union smhc_sfc {
	uint32_t dw;
	struct {
		uint32_t bypass_en		:  1;
		uint32_t stop_clk_ctrl		:  4;
		uint32_t			: 27;
	};
};

union smhc_a23a {
	uint32_t dw;
	struct {
		uint32_t a23a			: 32;
	};
};

union emmc_ddr_sbit_det {
	uint32_t dw;
	struct {
		uint32_t half_start_bit		:  1;
		uint32_t			: 30;
		uint32_t hs400_md_en		:  1;
	};
};

union smhc_ext_cmd {
	uint32_t dw;
	struct {
		uint32_t auto_cmd23_en		:  1;
		uint32_t			: 31;
	};
};

union smhc_ext_resp {
	uint32_t dw;
	struct {
		uint32_t smhc_ext_resp		: 32;
	};
};

union smhc_drv_dl {
	uint32_t dw;
	struct {
		uint32_t			: 16;
		uint32_t cmd_drv_ph_sel		:  1;
		uint32_t dat_drv_ph_sel		:  1;
		uint32_t			: 14;
	};
};

union smhc_samp_dl {
	uint32_t dw;
	struct {
		uint32_t samp_dl_sw		:  6;
		uint32_t			:  1;
		uint32_t samp_dl_sw_en		:  1;
		uint32_t samp_dl		:  6;
		uint32_t samp_dl_cal_done	:  1;
		uint32_t samp_dl_cal_start	:  1;
		uint32_t			: 16;
	};
};

union smhc_ds_dl {
	uint32_t dw;
	struct {
		uint32_t ds_dl_sw		:  6;
		uint32_t			:  1;
		uint32_t ds_dl_sw_en		:  1;
		uint32_t ds_dl			:  6;
		uint32_t ds_dl_cal_done		:  1;
		uint32_t ds_dl_cal_start	:  1;
		uint32_t			: 16;
	};
};

union smhc_hs400_dl {
	uint32_t dw;
	struct {
		uint32_t hs400_dl_sw		:  4;
		uint32_t			:  3;
		uint32_t hs400_dl_sw_en		:  1;
		uint32_t hs400_dl		:  4;
		uint32_t			:  2;
		uint32_t hs400_dl_cal_done	:  1;
		uint32_t hs400_dl_cal_start	:  1;
		uint32_t			: 16;
	};
};

union smhc_fifo {
	uint32_t dw;
	struct {
		uint32_t fifo			: 32;
	};
};

struct smhc_desc {
	union {
		uint32_t dw;
		struct {
			uint32_t			:  1;
			uint32_t cur_txrx_over_int_dis	:  1;
			uint32_t last_flag		:  1;
			uint32_t first_flag		:  1;
			uint32_t chain_mod		:  1;
			uint32_t			: 25;
			uint32_t error			:  1;
			uint32_t hold			:  1;
		};
	} des0;
	union {
		uint32_t dw;
		struct {
			uint32_t buff_size		: 13;
			uint32_t			: 19;
		};
	} des1;
	union {
		uint32_t dw;
		struct {
			uint32_t buff_addr		: 32;
		};
	} des2;
	union {
		uint32_t dw;
		struct {
			uint32_t next_desp_addr		: 32;
		};
	} des3;
} __attribute__ ((aligned(DCACHE_LINE)));

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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _SYS_UMC_H
#define	_SYS_UMC_H

#include <sys/bitext.h>

/*
 * Various register definitions for accessing the AMD Unified Memory Controller
 * (UMC) over SMN (the system management network). Note, that the SMN exists
 * independently in each die and must be accessed through the appropriate
 * IOHC.
 *
 * There are effectively four different revisions of the UMC that we know about
 * and support querying:
 *
 *   o DDR4 capable APUs
 *   o DDR4 capable CPUs
 *   o DDR5 capable APUs
 *   o DDR5 capable CPUs
 *
 * In general for a given revision and generation of a controller (DDR4 vs.
 * DDR5), all of the address layouts are the same whether it is for an APU or a
 * CPU. The main difference is generally in the number of features. For example,
 * most APUs may not support the same rank multiplication bits and related in a
 * device. However, unlike the DF where everything changes, the main difference
 * within a generation is just which bits are implemented. This makes it much
 * easier to define UMC information.
 *
 * Between DDR4 and DDR5 based devices, the register locations have shifted;
 * however, generally speaking, the registers themselves are actually the same.
 * Registers here, similar to the DF, have a common form:
 *
 * UMC_<reg name>_<vers>
 *
 * Here, <reg name> would be something like 'BASE', for the UMC
 * UMC::CH::BaseAddr register. <vers> is one of DDR4 or DDR5. When the same
 * register is supported at the same address between versions, then <vers> is
 * elided.
 *
 * For fields inside of these registers, everything follows the same pattern in
 * <sys/amdzen/df.h> which is:
 *
 * UMC_<reg name>_<vers>_GET_<field>
 *
 * Note, <vers> will be elided if the register is the same between the DDR4 and
 * DDR5 versions.
 *
 * Finally, a cautionary note. While the DF provided a way for us to determine
 * what version something is, we have not determined a way to programmatically
 * determine what something supports outside of making notes based on the
 * family, model, and stepping CPUID information. Unfortunately, you must look
 * towards the documentation and find what you need in the PPR (processor
 * programming reference).
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * UMC Channel registers. These are in SMN Space. DDR4 and DDR5 based UMCs share
 * the same base address, somewhat surprisingly. This constructs the appropriate
 * offset and ensures that a caller doesn't exceed the number of known instances
 * of the register.
 */
static inline uint32_t
amdzen_umc_smn_addr(uint8_t umcno, uint32_t base_reg, uint32_t nents,
    uint32_t reginst)
{
	ASSERT3U(umcno, <, 12);
	ASSERT3U(nents, >, reginst);

	uint32_t base = 0x50000;
	uint32_t reg = base_reg + reginst * 4;
	return ((umcno << 20) + base + reg);
}

/*
 * UMC::CH::BaseAddr, UMC::CH::BaseAddrSec -- determines the base address used
 * to match a chip select. Instances 0/1 always refer to DIMM 0, while
 * instances 2/3 always refer to DIMM 1.
 */
#define	UMC_BASE(u, i)		amdzen_umc_smn_addr(u, 0x00, 4, i)
#define	UMC_BASE_SEC(u, i)	amdzen_umc_smn_addr(u, 0x10, 4, i)
#define	UMC_BASE_GET_ADDR(r)	bitx32(r, 31, 1)
#define	UMC_BASE_ADDR_SHIFT	9
#define	UMC_BASE_GET_EN(r)	bitx32(r, 0, 0)

/*
 * UMC::BaseAddrExt, UMC::BaseAddrSecExt -- The first of several extensions to
 * registers that allow more address bits. Note, only present in some DDR5
 * capable SoCs.
 */
#define	UMC_BASE_EXT_DDR5(u, i)		amdzen_umc_smn_addr(u, 0xb00, 4, i)
#define	UMC_BASE_EXT_SEC_DDR5(u, i)	amdzen_umc_smn_addr(u, 0xb10, 4, i)
#define	UMC_BASE_EXT_GET_ADDR(r)	bitx32(r, 7, 0)
#define	UMC_BASE_EXT_ADDR_SHIFT		40


/*
 * UMC::CH::AddrMask, UMC::CH::AddrMaskSec -- This register is used to compare
 * the incoming address to see it matches the base. Tweaking what is used for
 * match is often part of the interleaving strategy.
 */
#define	UMC_MASK_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x20, 2, i)
#define	UMC_MASK_SEC_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x28, 2, i)
#define	UMC_MASK_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x20, 4, i)
#define	UMC_MASK_SEC_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x30, 4, i)
#define	UMC_MASK_GET_ADDR(r)	bitx32(r, 31, 1)
#define	UMC_MASK_ADDR_SHIFT	9

/*
 * UMC::AddrMaskExt, UMC::AddrMaskSecExt -- Extended mask addresses.
 */
#define	UMC_MASK_EXT_DDR5(u, i)		amdzen_umc_smn_addr(u, 0xb20, 4, i)
#define	UMC_MASK_EXT_SEC_DDR5(u, i)	amdzen_umc_smn_addr(u, 0xb30, 4, i)
#define	UMC_MASK_EXT_GET_ADDR(r)	bitx32(r, 7, 0)
#define	UMC_MASK_EXT_ADDR_SHIFT		40

/*
 * UMC::CH::AddrCfg -- This register contains a number of bits that describe how
 * the address is actually used, one per DIMM. Note, not all members are valid
 * for all classes of DIMMs. It's worth calling out that the total number of
 * banks value here describes the total number of banks on the entire chip, e.g.
 * it is bank groups * banks/groups. Therefore to determine the number of
 * banks/group you must subtract the number of bank group bits from the total
 * number of bank bits.
 */
#define	UMC_ADDRCFG_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x30, 2, i)
#define	UMC_ADDRCFG_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x40, 4, i)
#define	UMC_ADDRCFG_GET_NBANK_BITS(r)		bitx32(r, 21, 20)
#define	UMC_ADDRCFG_NBANK_BITS_BASE		3
#define	UMC_ADDRCFG_GET_NCOL_BITS(r)		bitx32(r, 19, 16)
#define	UMC_ADDRCFG_NCOL_BITS_BASE		5
#define	UMC_ADDRCFG_GET_NROW_BITS_LO(r)		bitx32(r, 11, 8)
#define	UMC_ADDRCFG_NROW_BITS_LO_BASE		10
#define	UMC_ADDRCFG_GET_NBANKGRP_BITS(r)	bitx32(r, 3, 2)

#define	UMC_ADDRCFG_DDR4_GET_NROW_BITS_HI(r)	bitx32(r, 15, 12)
#define	UMC_ADDRCFG_DDR4_GET_NRM_BITS(r)	bitx32(r, 5, 4)
#define	UMC_ADDRCFG_DDR5_GET_CSXOR(r)		bitx32(r, 31, 30)
#define	UMC_ADDRCFG_DDR5_GET_NRM_BITS(r)	bitx32(r, 6, 4)

/*
 * UMC::CH::AddrSel -- This register is used to program how the actual bits in
 * the normalized address map to the row and bank. While the bank can select
 * which bits in the normalized address are used to construct the bank number,
 * row bits are contiguous from the starting number.
 */
#define	UMC_ADDRSEL_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x40, 2, i)
#define	UMC_ADDRSEL_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x50, 4, i)
#define	UMC_ADDRSEL_GET_ROW_LO(r)	bitx32(r, 27, 24)
#define	UMC_ADDRSEL_ROW_LO_BASE		12
#define	UMC_ADDRSEL_GET_BANK4(r)	bitx32(r, 19, 16)
#define	UMC_ADDRSEL_GET_BANK3(r)	bitx32(r, 15, 12)
#define	UMC_ADDRSEL_GET_BANK2(r)	bitx32(r, 11, 8)
#define	UMC_ADDRSEL_GET_BANK1(r)	bitx32(r, 7, 4)
#define	UMC_ADDRSEL_GET_BANK0(r)	bitx32(r, 3, 0)
#define	UMC_ADDRSEL_BANK_BASE		5

#define	UMC_ADDRSEL_DDR4_GET_ROW_HI(r)	bitx32(r, 31, 28)
#define	UMC_ADDRSEL_DDR4_ROW_HI_BASE	24

/*
 * UMC::CH::ColSelLo, UMC::CH::ColSelHi -- This register selects which address
 * bits map to the various column select bits. These registers interleave so in
 * the case of DDR4, it's 0x50, 0x54 for DIMM 0 lo, hi. Then 0x58, 0x5c for
 * DIMM1. DDR5 based entries do something similar; however, instead of being
 * per-DIMM, there is one of these for each CS.
 *
 * This leads to a somewhat odder construction for the maximum number of
 * instances. Because amdzen_umc_smn_addr() assumes each register instance is 4
 * bytes apart, we instead take the actual register instance and multiply it by
 * 2. This means that in the DDR4 case we will always access what
 * amdzen_umc_smn_addr() considers instance 0 and 2. In the DDR5 case this is 0,
 * 2, 4, and 6. This means our maximum instance for both cases has to be one
 * higher than this, 3 and 7 respectively. While technically you could use 4 and
 * 8, this is a tighter bind.
 */
#define	UMC_COLSEL_LO_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x50, 3, i * 2)
#define	UMC_COLSEL_HI_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x54, 3, i * 2)
#define	UMC_COLSEL_LO_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x60, 7, i * 2)
#define	UMC_COLSEL_HI_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x64, 7, i * 2)

#define	UMC_COLSEL_REMAP_GET_COL(r, x)	bitx32(r, (3 + (4 * (x))), (4 * ((x))))
#define	UMC_COLSEL_LO_BASE		2
#define	UMC_COLSEL_HI_BASE		8

/*
 * UMC::CH::RmSel -- This register contains the bits that determine how the rank
 * is determined. Which fields of this are valid vary a lot in the different
 * parts. The DDR4 and DDR5 versions are different enough that we use totally
 * disjoint definitions. It's also worth noting that DDR5 doesn't have a
 * secondary version of this as it is included in the main register.
 *
 * In general, APUs have some of the MSBS (most significant bit swap) related
 * fields; however, they do not have rank multiplication bits.
 */
#define	UMC_RMSEL_DDR4(u, i)		amdzen_umc_smn_addr(u, 0x70, 2, i)
#define	UMC_RMSEL_SEC_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x78, 2, i)
#define	UMC_RMSEL_DDR4_GET_INV_MSBO(r)	bitx32(r, 19, 18)
#define	UMC_RMSEL_DDR4_GET_INV_MSBE(r)	bitx32(r, 17, 16)
#define	UMC_RMSEL_DDR4_GET_RM2(r)	bitx32(r, 11, 8)
#define	UMC_RMSEL_DDR4_GET_RM1(r)	bitx32(r, 7, 4)
#define	UMC_RMSEL_DDR4_GET_RM0(r)	bitx32(r, 3, 0)
#define	UMC_RMSEL_BASE			12

#define	UMC_RMSEL_DDR5(u, i)		amdzen_umc_smn_addr(u, 0x80, 4, i)
#define	UMC_RMSEL_DDR5_GET_INV_MSBS_SEC(r)	bitx32(r, 31, 30)
#define	UMC_RMSEL_DDR5_GET_INV_MSBS(r)		bitx32(r, 29, 28)
#define	UMC_RMSEL_DDR5_GET_SUBCHAN(r)	bitx32(r, 19, 16)
#define	UMC_RMSEL_DDR5_SUBCHAN_BASE	5
#define	UMC_RMSEL_DDR5_GET_RM3(r)	bitx32(r, 15, 12)
#define	UMC_RMSEL_DDR5_GET_RM2(r)	bitx32(r, 11, 8)
#define	UMC_RMSEL_DDR5_GET_RM1(r)	bitx32(r, 7, 4)
#define	UMC_RMSEL_DDR5_GET_RM0(r)	bitx32(r, 3, 0)


/*
 * UMC::CH::DimmCfg -- This describes several properties of the DIMM that is
 * installed, such as its overall width or type.
 */
#define	UMC_DIMMCFG_DDR4(u, i)	amdzen_umc_smn_addr(u, 0x80, 2, i)
#define	UMC_DIMMCFG_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x90, 2, i)
#define	UMC_DIMMCFG_GET_PKG_RALIGN(r)	bitx32(r, 10, 10)
#define	UMC_DIMMCFG_GET_REFRESH_DIS(r)	bitx32(r, 9, 9)
#define	UMC_DIMMCFG_GET_DQ_SWAP_DIS(r)	bitx32(r, 8, 8)
#define	UMC_DIMMCFG_GET_X16(r)		bitx32(r, 7, 7)
#define	UMC_DIMMCFG_GET_X4(r)		bitx32(r, 6, 6)
#define	UMC_DIMMCFG_GET_LRDIMM(r)	bitx32(r, 5, 5)
#define	UMC_DIMMCFG_GET_RDIMM(r)	bitx32(r, 4, 4)
#define	UMC_DIMMCFG_GET_CISCS(r)	bitx32(r, 3, 3)
#define	UMC_DIMMCFG_GET_3DS(r)		bitx32(r, 2, 2)

#define	UMC_DIMMCFG_DDR4_GET_NVDIMMP(r)	bitx32(r, 12, 12)
#define	UMC_DIMMCFG_DDR4_GET_DDR4e(r)	bitx32(r, 11, 11)
#define	UMC_DIMMCFG_DDR5_GET_RALIGN(r)	bitx32(r, 13, 12)
#define	UMC_DIMMCFG_DDR5_GET_ASYM(r)	bitx32(r, 11, 11)

#define	UMC_DIMMCFG_DDR4_GET_OUTPUT_INV(r)	bitx32(r, 1, 1)
#define	UMC_DIMMCFG_DDR4_GET_MRS_MIRROR(r)	bitx32(r, 0, 0)

/*
 * UMC::CH::AddrHashBank -- These registers contain various instructions about
 * how to hash an address across a bank to influence which bank is used.
 */
#define	UMC_BANK_HASH_DDR4(u, i)	amdzen_umc_smn_addr(u, 0xc8, 5, i)
#define	UMC_BANK_HASH_DDR5(u, i)	amdzen_umc_smn_addr(u, 0x98, 5, i)
#define	UMC_BANK_HASH_GET_ROW(r)	bitx32(r, 31, 14)
#define	UMC_BANK_HASH_GET_COL(r)	bitx32(r, 13, 1)
#define	UMC_BANK_HASH_GET_EN(r)		bitx32(r, 0, 0)

/*
 * UMC::CH::AddrHashRM -- This hash register describes how to transform a UMC
 * address when trying to do rank hashing. Note, instance 3 is is reserved in
 * DDR5 modes.
 */
#define	UMC_RANK_HASH_DDR4(u, i)	amdzen_umc_smn_addr(u, 0xdc, 3, i)
#define	UMC_RANK_HASH_DDR5(u, i)	amdzen_umc_smn_addr(u, 0xb0, 4, i)
#define	UMC_RANK_HASH_GET_ADDR(r)	bitx32(r, 31, 1)
#define	UMC_RANK_HASH_SHIFT		9
#define	UMC_RANK_HASH_GET_EN(r)		bitx32(r, 0, 0)

/*
 * UMC::AddrHashRMExt -- Extended rank hash addresses.
 */
#define	UMC_RANK_HASH_EXT_DDR5(u, i)	amdzen_umc_smn_addr(u, 0xbb0, 4, i)
#define	UMC_RANK_HASH_EXT_GET_ADDR(r)	bitx32(r, 7, 0)
#define	UMC_RANK_HASH_EXT_ADDR_SHIFT	40

/*
 * UMC::CH::AddrHashPC, UMC::CH::AddrHashPC2 -- These registers describe a hash
 * to use for the DDR5 sub-channel. Note, in the DDR4 case this is actually the
 * upper two rank hash registers defined above because on the systems where this
 * occurs for DDR4, they only have up to one rank hash.
 */
#define	UMC_PC_HASH_DDR4(u)	UMC_RANK_HASH_DDR4(u, 1)
#define	UMC_PC_HASH2_DDR4(u)	UMC_RANK_HASH_DDR4(u, 2)
#define	UMC_PC_HASH_DDR5(u)	amdzen_umc_smn_addr(u, 0xc0, 1, 0)
#define	UMC_PC_HASH2_DDR5(u)	amdzen_umc_smn_addr(u, 0xc4, 1, 0)
#define	UMC_PC_HASH_GET_ROW(r)		bitx32(r, 31, 14)
#define	UMC_PC_HASH_GET_COL(r)		bitx32(r, 13, 1)
#define	UMC_PC_HASH_GET_EN(r)		bitx32(r, 0, 0)
#define	UMC_PC_HASH2_GET_BANK(r)	bitx32(r, 4, 0)

/*
 * UMC::CH::AddrHashCS -- Hashing: chip-select edition. Note, these can
 * ultimately cause you to change which DIMM is being actually accessed.
 */
#define	UMC_CS_HASH_DDR4(u, i)	amdzen_umc_smn_addr(u, 0xe8, 2, i)
#define	UMC_CS_HASH_DDR5(u, i)	amdzen_umc_smn_addr(u, 0xc8, 2, i)
#define	UMC_CS_HASH_GET_ADDR(r)		bitx32(r, 31, 1)
#define	UMC_CS_HASH_SHIFT		9
#define	UMC_CS_HASH_GET_EN(r)		bitx32(r, 0, 0)

/*
 * UMC::AddrHashExtCS -- Extended chip-select hash addresses.
 */
#define	UMC_CS_HASH_EXT_DDR5(u, i)	amdzen_umc_smn_addr(u, 0xbc8, 2, i)
#define	UMC_CS_HASH_EXT_GET_ADDR(r)	bitx32(r, 7, 0)
#define	UMC_CS_HASH_EXT_ADDR_SHIFT	40

/*
 * UMC::CH::UmcConfig -- This register controls various features of the device.
 * For our purposes we mostly care about seeing if ECC is enabled and a DIMM
 * type.
 */
#define	UMC_UMCCFG(u)	amdzen_umc_smn_addr(u, 0x100, 1, 0)
#define	UMC_UMCCFG_GET_READY(r)		bitx32(r, 31, 31)
#define	UMC_UMCCFG_GET_ECC_EN(r)	bitx32(r, 12, 12)
#define	UMC_UMCCFG_GET_BURST_CTL(r)	bitx32(r, 11, 10)
#define	UMC_UMCCFG_GET_BURST_LEN(r)	bitx32(r, 9, 8)
#define	UMC_UMCCFG_GET_DDR_TYPE(r)	bitx32(r, 2, 0)
#define	UMC_UMCCFG_DDR4_T_DDR4		0
#define	UMC_UMCCFG_DDR4_T_LPDDR4	5

#define	UMC_UMCCFG_DDR5_T_DDR4		0
#define	UMC_UMCCFG_DDR5_T_DDR5		1
#define	UMC_UMCCFG_DDR5_T_LPDDR4	5
#define	UMC_UMCCFG_DDR5_T_LPDDR5	6

/*
 * UMC::CH::DataCtrl -- Various settings around whether data encryption or
 * scrambling is enabled. Note, this register really changes a bunch from family
 * to family.
 */
#define	UMC_DATACTL(u)		amdzen_umc_smn_addr(u, 0x144, 1, 0)
#define	UMC_DATACTL_GET_ENCR_EN(r)	bitx32(r, 8, 8)
#define	UMC_DATACTL_GET_SCRAM_EN(r)	bitx32(r, 0, 0)

#define	UMC_DATACTL_DDR4_GET_TWEAK(r)		bitx32(r, 19, 16)
#define	UMC_DATACTL_DDR4_GET_VMG2M(r)		bitx32(r, 12, 12)
#define	UMC_DATACTL_DDR4_GET_FORCE_ENCR(r)	bitx32(r, 11, 11)

#define	UMC_DATACTL_DDR5_GET_TWEAK(r)	bitx32(r, 16, 16)
#define	UMC_DATACTL_DDR5_GET_XTS(r)	bitx32(r, 14, 14)
#define	UMC_DATACTL_DDR5_GET_AES256(r)	bitx32(r, 13, 13)

/*
 * UMC::CH:EccCtrl -- Various settings around how ECC operates.
 */
#define	UMC_ECCCTL(u)	amdzen_umc_smn_addr(u, 0x14c, 1, 0)
#define	UMC_ECCCTL_GET_RD_EN(r)		bitx32(x, 10, 10)
#define	UMC_ECCCTL_GET_X16(r)		bitx32(x, 9, 9)
#define	UMC_ECCCTL_GET_UC_FATAL(r)	bitx32(x, 8, 8)
#define	UMC_ECCCTL_GET_SYM_SIZE(r)	bitx32(x, 7, 7)
#define	UMC_ECCCTL_GET_BIT_IL(r)	bitx32(x, 6, 6)
#define	UMC_ECCCTL_GET_HIST_EN(r)	bitx32(x, 5, 5)
#define	UMC_ECCCTL_GET_SW_SYM_EN(r)	bitx32(x, 4, 4)
#define	UMC_ECCCTL_GET_WR_EN(r)		bitx32(x, 0, 0)

/*
 * Note, while this group appears generic and is the same in both DDR4/DDR5
 * systems, this is not always present on every SoC and seems to depend on
 * something else inside the chip.
 */
#define	UMC_ECCCTL_DDR_GET_PI(r)		bitx32(r, 13, 13)
#define	UMC_ECCCTL_DDR_GET_PF_DIS(r)		bitx32(r, 12, 12)
#define	UMC_ECCCTL_DDR_GET_SDP_OVR(r)		bitx32(x, 11, 11)
#define	UMC_ECCCTL_DDR_GET_REPLAY_EN(r)	bitx32(x, 1, 1)

#define	UMC_ECCCTL_DDR5_GET_PIN_RED(r)	bitx32(r, 14, 14)

/*
 * UMC::Ch::UmcCap, UMC::CH::UmcCapHi -- Various capability registers and
 * feature disables. We mostly just record these for future us for debugging
 * purposes. They aren't used as part of memory decoding.
 */
#define	UMC_UMCCAP(u)		amdzen_umc_smn_addr(u, 0xdf0, 1, 0)
#define	UMC_UMCCAP_HI(u)	amdzen_umc_smn_addr(u, 0xdf4, 1, 0)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_UMC_H */

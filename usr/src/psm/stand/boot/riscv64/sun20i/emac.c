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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/promif.h>
#include <sys/salib.h>
#include <sys/byteorder.h>
#include <sys/sysmacros.h>

#include <sys/miiregs.h>
#include <sys/ethernet.h>
#include <sys/gpio.h>
#include <sys/platmod.h>
#include <sys/platform.h>
#include <sys/emacreg.h>
#include <sys/t-head.h>
#include "prom_dev.h"
#include "emac.h"
#include "boot_plat.h"

#define TX_DESC_NUM 32
#define RX_DESC_NUM 48

#define BUFFER_SIZE 1536

struct emac_sc
{
	uintptr_t base;
	uint8_t mac_addr[6];
	int phy_id;
	int phy_speed;
	int phy_fullduplex;
	pnode_t node;

	uintptr_t tx_desc_base;
	uintptr_t rx_desc_base;
	int tx_index;
	int rx_head;
	caddr_t tx_buffer;
	caddr_t rx_buffer;
	paddr_t tx_buffer_phys;
	paddr_t tx_desc_phys;
	paddr_t rx_desc_phys;
	paddr_t rx_buffer_phys;
};
static struct emac_sc *emac_dev[3];

static void
emac_reg_write(struct emac_sc *sc, uint32_t off, uint32_t val)
{
	*((volatile uint32_t *)(sc->base + off)) = val;
}

static uint32_t
emac_reg_read(struct emac_sc *sc, uint32_t off)
{
	uint32_t val = *((volatile uint32_t *)(sc->base + off));
	return val;
}

static struct emac_txdesc *
emac_get_tx_desc(struct emac_sc *sc, int index)
{
	return (struct emac_txdesc *)(sc->tx_desc_base + index * sizeof(struct emac_txdesc));
}
static struct emac_rxdesc *
emac_get_rx_desc(struct emac_sc *sc, int index)
{
	return (struct emac_rxdesc *)(sc->rx_desc_base + index * sizeof(struct emac_rxdesc));
}
static paddr_t
emac_get_tx_desc_phys(struct emac_sc *sc, int index)
{
	return (sc->tx_desc_phys + index * sizeof(struct emac_txdesc));
}
static paddr_t
emac_get_rx_desc_phys(struct emac_sc *sc, int index)
{
	return (sc->rx_desc_phys + index * sizeof(struct emac_rxdesc));
}

static caddr_t
emac_get_tx_buffer(struct emac_sc *sc, int index)
{
	return (caddr_t)(sc->tx_buffer + index * BUFFER_SIZE);
}
static caddr_t
emac_get_rx_buffer(struct emac_sc *sc, int index)
{
	return (caddr_t)(sc->rx_buffer + index * BUFFER_SIZE);
}
static paddr_t
emac_get_tx_buffer_phys(struct emac_sc *sc, int index)
{
	return (paddr_t)(sc->tx_buffer_phys + index * BUFFER_SIZE);
}
static paddr_t
emac_get_rx_buffer_phys(struct emac_sc *sc, int index)
{
	return (paddr_t)(sc->rx_buffer_phys + index * BUFFER_SIZE);
}


static int
emac_setup_buffer(struct emac_sc *sc)
{
	size_t size = 0;
	size_t offset = 0;
	size += sizeof(struct emac_txdesc) * TX_DESC_NUM;
	size += sizeof(struct emac_rxdesc) * RX_DESC_NUM;
	size_t alloc_size = size + 2 * MMU_PAGESIZE;
	uintptr_t orig_addr = (uintptr_t)kmem_alloc(alloc_size, 0);
	memset((void*)orig_addr, 0, alloc_size);
	uintptr_t buf_addr = roundup(orig_addr, MMU_PAGESIZE);
	size_t buf_size = roundup(size, MMU_PAGESIZE);
	uintptr_t buf_vaddr = memlist_get(buf_size, MMU_PAGESIZE, &ptmplistp);
	map_phys(PTE_A | PTE_D | PTE_G | PTE_W | PTE_R  | pte_mt_nc, (caddr_t)buf_vaddr, buf_addr, buf_size);
	thead_dcache_clean(buf_addr, buf_size);

	sc->tx_desc_phys = (paddr_t)(buf_addr + offset);
	sc->tx_desc_base = (buf_vaddr + offset);
	offset += sizeof(struct emac_txdesc) * TX_DESC_NUM;

	offset = roundup(offset, DCACHE_LINE);
	sc->rx_desc_phys = (paddr_t)(buf_addr + offset);
	sc->rx_desc_base = (buf_vaddr + offset);
	offset += sizeof(struct emac_rxdesc) * RX_DESC_NUM;

	size = 0;
	offset = 0;
	size += BUFFER_SIZE * TX_DESC_NUM;
	size += BUFFER_SIZE * RX_DESC_NUM;
	alloc_size = size + 2 * MMU_PAGESIZE;
	orig_addr = (uintptr_t)kmem_alloc(alloc_size, 0);
	memset((void*)orig_addr, 0, alloc_size);
	buf_addr = roundup(orig_addr, MMU_PAGESIZE);

	sc->tx_buffer_phys = (paddr_t)(buf_addr + offset);
	sc->tx_buffer = (caddr_t)(buf_addr + offset);
	offset += BUFFER_SIZE * TX_DESC_NUM;

	sc->rx_buffer_phys = (paddr_t)(buf_addr + offset);
	sc->rx_buffer = (caddr_t)(buf_addr + offset);
	offset += BUFFER_SIZE * RX_DESC_NUM;

	for (int i = 0; i < TX_DESC_NUM; i++) {
		struct emac_txdesc *desc = emac_get_tx_desc(sc, i);
		desc->tdesc0.dw = 0;
		desc->tdesc1.dw = 0;
		desc->tdesc2.buf_addr = emac_get_tx_buffer_phys(sc, i);
		desc->tdesc3.next_desc_addr = emac_get_tx_desc_phys(sc, (i + 1) % TX_DESC_NUM);
	}
	for (int i = 0; i < RX_DESC_NUM; i++) {
		struct emac_rxdesc *desc = emac_get_rx_desc(sc, i);
		desc->rdesc0.dw = 0;
		desc->rdesc1.dw = 0;
		desc->rdesc2.buf_addr = emac_get_rx_buffer_phys(sc, i);
		desc->rdesc3.next_desc_addr = emac_get_rx_desc_phys(sc, (i + 1) % RX_DESC_NUM);
		desc->rdesc1.buf_size = BUFFER_SIZE;
		desc->rdesc0.rx_desc_ctl = 1;
	}

	return 0;
}

static int
emac_get_macaddr(struct emac_sc *sc)
{
	uint8_t mac[6] = {0};
	if (prom_getproplen(sc->node, "mac-address") == 6) {
		prom_getprop(sc->node, "mac-address", (caddr_t)mac);
	} else if (prom_getproplen(sc->node, "local-mac-address") == 6) {
		prom_getprop(sc->node, "local-mac-address", (caddr_t)mac);
	} else if (prom_getproplen(sc->node, "address") == 6) {
		prom_getprop(sc->node, "address", (caddr_t)mac);
	} else {
		return -1;
	}
	for (int i = 0; i < 6; i++) {
		sc->mac_addr[i] = mac[i];
	}
	prom_printf("MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	    sc->mac_addr[0],
	    sc->mac_addr[1],
	    sc->mac_addr[2],
	    sc->mac_addr[3],
	    sc->mac_addr[4],
	    sc->mac_addr[5]
	    );
	return 0;
}

static int
emac_match(const char *name)
{
	pnode_t node = prom_finddevice(name);
	if (node <= 0)
		return 0;
	if (prom_is_compatible(node, "allwinner,sun50i-a64-emac"))
		return 1;
	if (prom_is_compatible(node, "allwinner,sun20i-d1-emac"))
		return 1;
	return 0;
}

static pnode_t
get_phynode(pnode_t node)
{
	int len = prom_getproplen(node, "phy-handle");
	if (len <= 0)
		return -1;
	phandle_t phandle;
	prom_getprop(node, "phy-handle", (caddr_t)&phandle);
	return prom_findnode_by_phandle(htonl(phandle));
}

static void
emac_mii_write(struct emac_sc *sc, int offset, uint16_t val)
{
	union emac_mii_cmd mii_cmd = {0};
	union emac_mii_data mii_data = {0};
	mii_cmd.dw = emac_reg_read(sc, EMAC_MII_CMD);
	if (mii_cmd.mii_busy)
		return;

	mii_cmd.dw = 0;
	mii_cmd.phy_addr = sc->phy_id;
	mii_cmd.phy_reg_addr = offset;
	mii_cmd.mdc_div_ratio_m = 3;
	mii_cmd.mii_wr = 1;
	mii_cmd.mii_busy = 1;
	mii_data.mii_data = val;
	emac_reg_write(sc, EMAC_MII_DATA, mii_data.dw);
	emac_reg_write(sc, EMAC_MII_CMD, mii_cmd.dw);

	for (;;) {
		mii_cmd.dw = emac_reg_read(sc, EMAC_MII_CMD);
		if (!mii_cmd.mii_busy)
			break;
	}
}

static uint16_t
emac_mii_read(struct emac_sc *sc, int offset)
{
	union emac_mii_cmd mii_cmd = {0};
	mii_cmd.dw = emac_reg_read(sc, EMAC_MII_CMD);
	if (mii_cmd.mii_busy)
		return 0xffff;

	mii_cmd.dw = 0;
	mii_cmd.phy_addr = sc->phy_id;
	mii_cmd.phy_reg_addr = offset;
	mii_cmd.mdc_div_ratio_m = 3;
	mii_cmd.mii_wr = 0;
	mii_cmd.mii_busy = 1;
	emac_reg_write(sc, EMAC_MII_CMD, mii_cmd.dw);

	for (;;) {
		mii_cmd.dw = emac_reg_read(sc, EMAC_MII_CMD);
		if (!mii_cmd.mii_busy)
			break;
	}
	union emac_mii_data mii_data = { emac_reg_read(sc, EMAC_MII_DATA) };
	return mii_data.mii_data;
}

static int
emac_phy_reset(struct emac_sc *sc)
{
	uint16_t advert = emac_mii_read(sc, MII_AN_ADVERT) & 0x1F;
	advert |= MII_ABILITY_100BASE_TX_FD;
	advert |= MII_ABILITY_100BASE_TX;
	advert |= MII_ABILITY_10BASE_T_FD;
	advert |= MII_ABILITY_10BASE_T;
	uint16_t gigctrl =  MII_MSCONTROL_1000T_FD | MII_MSCONTROL_1000T;

	emac_mii_write(sc, MII_AN_ADVERT, advert);
	emac_mii_write(sc, MII_MSCONTROL, gigctrl);

	uint16_t bmcr = MII_CONTROL_ANE | MII_CONTROL_RSAN | MII_CONTROL_1GB | MII_CONTROL_FDUPLEX;
	emac_mii_write(sc, MII_CONTROL, bmcr);

	int i;
	uint16_t bmsr = 0;
	for (i = 0; i < 10000; i++) {
		uint_t s = prom_gettime();
		while (prom_gettime() < s + 2) {}
		bmsr = emac_mii_read(sc, MII_STATUS);
		if (bmsr == 0xffff)
			continue;
		if (bmsr & MII_STATUS_LINKUP)
			break;
	}
	if (i == 10000 || !(bmsr & MII_STATUS_LINKUP))
		return -1;

	uint16_t lpar = emac_mii_read(sc, MII_AN_LPABLE);
	uint16_t msstat = emac_mii_read(sc, MII_MSSTATUS);
	if (msstat & MII_MSSTATUS_LP1000T_FD) {
		sc->phy_speed = 1000;
		sc->phy_fullduplex = 1;
	} else if (msstat & MII_MSSTATUS_LP1000T) {
		sc->phy_speed = 1000;
		sc->phy_fullduplex = 0;
	} else if (lpar & MII_ABILITY_100BASE_TX_FD) {
		sc->phy_speed = 100;
		sc->phy_fullduplex = 1;
	} else if (lpar & MII_ABILITY_100BASE_TX) {
		sc->phy_speed = 100;
		sc->phy_fullduplex = 0;
	} else if (lpar & MII_ABILITY_10BASE_T_FD) {
		sc->phy_speed = 10;
		sc->phy_fullduplex = 1;
	} else if (lpar & MII_ABILITY_10BASE_T) {
		sc->phy_speed = 10;
		sc->phy_fullduplex = 0;
	} else {
		sc->phy_speed = 0;
		sc->phy_fullduplex = 0;
	}

	return 0;
}

static int
emac_open(const char *name)
{
	pnode_t node = prom_finddevice(name);
	if (node <= 0)
		return -1;
	if (!prom_is_compatible(node, "allwinner,sun50i-a64-emac") &&
	    !prom_is_compatible(node, "allwinner,sun20i-d1-emac"))
		return -1;

	int fd;

	for (fd = 0; fd < sizeof(emac_dev) / sizeof(emac_dev[0]); fd++) {
		if (emac_dev[fd] == NULL)
			break;
	}
	if (fd == sizeof(emac_dev) / sizeof(emac_dev[0]))
		return -1;
	struct emac_sc *sc = kmem_alloc(sizeof(struct emac_sc), 0);
	sc->node = node;

	if (emac_get_macaddr(sc))
		return -1;

	uint64_t regbase;
	if (prom_get_reg_address(prom_finddevice(name), 0, &regbase) != 0)
		return -1;
	sc->base = regbase;

	// reset
	union emac_basic_ctl1 ctl1 = { emac_reg_read(sc, EMAC_BASIC_CTL1) };
	ctl1.soft_rst = 1;
	emac_reg_write(sc, EMAC_BASIC_CTL1, ctl1.dw);
	for (;;) {
		ctl1.dw = emac_reg_read(sc, EMAC_BASIC_CTL1);
		if (ctl1.soft_rst == 0)
			break;
	}

	union emac_addr_low0 addr_low0 = {0};
	addr_low0.mac_addr_low0 =
	    (sc->mac_addr[3] << 24) |
	    (sc->mac_addr[2] << 16) |
	    (sc->mac_addr[1] << 8) |
	    sc->mac_addr[0];
	union emac_addr_high0 addr_high0 = {0};
	addr_high0.mac_addr_high0 =
	    (sc->mac_addr[5] << 8) |
	    sc->mac_addr[4];
	emac_reg_write(sc, EMAC_ADDR_LOW0, addr_low0.dw);
	emac_reg_write(sc, EMAC_ADDR_HIGH0, addr_high0.dw);

	union emac_tx_ctl1 tx_ctl1 = { emac_reg_read(sc, EMAC_TX_CTL1) };
	tx_ctl1.tx_md = 1;
	emac_reg_write(sc, EMAC_TX_CTL1, tx_ctl1.dw);

	union emac_rx_ctl1 rx_ctl1 = { emac_reg_read(sc, EMAC_RX_CTL1) };
	rx_ctl1.rx_md = 1;
	emac_reg_write(sc, EMAC_RX_CTL1, rx_ctl1.dw);

	ctl1.dw = emac_reg_read(sc, EMAC_BASIC_CTL1);
	ctl1.burst_len = 8;
	emac_reg_write(sc, EMAC_BASIC_CTL1, ctl1.dw);

	// get phy id
	pnode_t phy_node = get_phynode(node);
	if (phy_node < 0)
		return -1;
	{
		uint32_t phy_id;
		if (prom_getproplen(phy_node, "reg") != sizeof(phy_id))
			return -1;
		prom_getprop(phy_node, "reg", (caddr_t)&phy_id);
		sc->phy_id = htonl(phy_id);
	}

	if (emac_phy_reset(sc))
		return -1;

	if (emac_setup_buffer(sc))
		return -1;

	union emac_rx_dma_desc_list rx_dma_desc_list = {0};
	rx_dma_desc_list.rx_desc_list = sc->rx_desc_phys;
	emac_reg_write(sc, EMAC_RX_DMA_DESC_LIST, rx_dma_desc_list.dw);

	union emac_tx_dma_desc_list tx_dma_desc_list = {0};
	tx_dma_desc_list.tx_desc_list = sc->tx_desc_phys;
	emac_reg_write(sc, EMAC_TX_DMA_DESC_LIST, tx_dma_desc_list.dw);

	union emac_basic_ctl0 basic_ctl0 = { emac_reg_read(sc, EMAC_BASIC_CTL0) };
	switch (sc->phy_speed) {
	case 1000: basic_ctl0.speed = 0; break;
	case  100: basic_ctl0.speed = 3; break;
	case   10: basic_ctl0.speed = 2; break;
	}
	if (sc->phy_fullduplex)
		basic_ctl0.duplex = 1;
	else
		basic_ctl0.duplex = 0;
	emac_reg_write(sc, EMAC_BASIC_CTL0, basic_ctl0.dw);

	tx_ctl1.dw = emac_reg_read(sc, EMAC_TX_CTL1);
	tx_ctl1.tx_dma_en = 1;
	emac_reg_write(sc, EMAC_TX_CTL1, tx_ctl1.dw);

	rx_ctl1.dw = emac_reg_read(sc, EMAC_RX_CTL1);
	rx_ctl1.rx_dma_en = 1;
	emac_reg_write(sc, EMAC_RX_CTL1, rx_ctl1.dw);

	union emac_tx_ctl0 tx_ctl0 = { emac_reg_read(sc, EMAC_TX_CTL0) };
	tx_ctl0.tx_en = 1;
	emac_reg_write(sc, EMAC_TX_CTL0, tx_ctl0.dw);

	union emac_rx_ctl0 rx_ctl0 = { emac_reg_read(sc, EMAC_RX_CTL0) };
	rx_ctl0.rx_en = 1;
	emac_reg_write(sc, EMAC_RX_CTL0, rx_ctl0.dw);

	phandle_t chosen = prom_chosennode();

	char *str;
	str = "bootp";
	prom_setprop(chosen, "net-config-strategy", (caddr_t)str, strlen(str));
	str = "ethernet,100,rj45,full";
	prom_setprop(chosen, "network-interface-type", (caddr_t)str, strlen(str));
	str = "Ethernet controller";
	prom_setprop(prom_finddevice(name), "model", (caddr_t)str, strlen(str) + 1);
	str = "okay";
	prom_setprop(prom_finddevice(name), "status", (caddr_t)str, strlen(str) + 1);

	emac_dev[fd] = sc;
	return fd;
}

static ssize_t
emac_send(int dev, caddr_t data, size_t packet_length, uint_t startblk)
{
	if (!(0 <= dev && dev < sizeof(emac_dev) / sizeof(emac_dev[0])))
		return -1;

	struct emac_sc *sc = emac_dev[dev];
	if (!sc)
		return -1;

	if (packet_length > BUFFER_SIZE)
		return -1;

	int index = sc->tx_index;
	struct emac_txdesc *tx_desc = emac_get_tx_desc(sc, index);

	do {
	} while (tx_desc->tdesc0.tx_desc_ctl);
	asm volatile ("fence":::"memory");
	caddr_t buffer = emac_get_tx_buffer(sc, index);
	memcpy(buffer, data, packet_length);
	thead_dcache_clean((uintptr_t)buffer, packet_length);

	typeof(tx_desc->tdesc0) tdesc0 = { 0 };
	typeof(tx_desc->tdesc1) tdesc1 = { 0 };
	tdesc1.fir_desc = 1;
	tdesc1.last_desc = 1;
	tdesc1.tx_int_ctl = 1;
	tdesc1.buf_size = ((packet_length < ETHERMIN) ? ETHERMIN: packet_length);
	tdesc0.tx_desc_ctl = 1;

	tx_desc->tdesc1.dw = tdesc1.dw;
	asm volatile ("fence":::"memory");
	tx_desc->tdesc0.dw = tdesc0.dw;

	union emac_tx_ctl1 tx_ctl1 = { emac_reg_read(sc, EMAC_TX_CTL1) };
	tx_ctl1.tx_dma_start = 1;
	emac_reg_write(sc, EMAC_TX_CTL1, tx_ctl1.dw);

	sc->tx_index = (sc->tx_index + 1) % TX_DESC_NUM;

	return packet_length;
}

static ssize_t
emac_recv(int dev, caddr_t buf, size_t buf_len, uint_t startblk)
{
	if (!(0 <= dev && dev < sizeof(emac_dev) / sizeof(emac_dev[0])))
		return -1;

	struct emac_sc *sc = emac_dev[dev];
	if (!sc)
		return -1;

	int index = sc->rx_head;
	size_t len = 0;

	struct emac_rxdesc *rx_desc = emac_get_rx_desc(sc, index);
	typeof(rx_desc->rdesc0) rdesc0 = { rx_desc->rdesc0.dw };
	asm volatile ("fence":::"memory");
	if (rdesc0.rx_desc_ctl)
		return 0;

	if (
	    rdesc0.rx_daf_fail ||
	    rdesc0.rx_no_enough_buf_err ||
	    rdesc0.rx_saf_fail ||
	    rdesc0.rx_overflow_err ||
	    rdesc0.rx_header_err ||
	    rdesc0.rx_col_err ||
	    rdesc0.rx_length_err ||
	    rdesc0.rx_phy_err ||
	    rdesc0.rx_crc_err ||
	    rdesc0.rx_payload_err ||
	    rdesc0.fir_desc == 0||
	    rdesc0.last_desc == 0||
	    rdesc0.rx_frm_len < 64) {
		// error
		prom_printf("%s:%d rdesc0 = %08x\n",__func__,__LINE__, rdesc0.dw);
	} else {
		len = rdesc0.rx_frm_len;
		caddr_t buffer = emac_get_rx_buffer(sc, index);
		thead_dcache_flush((uintptr_t)buffer, len);
		memcpy(buf, buffer, len);
	}

	rdesc0.dw = 0;
	rdesc0.rx_desc_ctl = 1;
	asm volatile ("fence":::"memory");
	rx_desc->rdesc0.dw = rdesc0.dw;

	index = (index + 1) % RX_DESC_NUM;
	sc->rx_head = index;

	return len;
}

static int
emac_getmacaddr(ihandle_t dev, caddr_t ea)
{
	if (!(0 <= dev && dev < sizeof(emac_dev) / sizeof(emac_dev[0])))
		return -1;

	struct emac_sc *sc = emac_dev[dev];
	if (!sc)
		return -1;
	memcpy(ea, sc->mac_addr, 6);
	return 0;
}

static int
emac_close(int dev)
{
	if (!(0 <= dev && dev < sizeof(emac_dev) / sizeof(emac_dev[0])))
		return -1;
	struct emac_sc *sc = emac_dev[dev];
	if (!sc)
		return -1;

	union emac_tx_ctl0 tx_ctl0 = { emac_reg_read(sc, EMAC_TX_CTL0) };
	tx_ctl0.tx_en = 0;
	emac_reg_write(sc, EMAC_TX_CTL0, tx_ctl0.dw);

	union emac_rx_ctl0 rx_ctl0 = { emac_reg_read(sc, EMAC_RX_CTL0) };
	rx_ctl0.rx_en = 0;
	emac_reg_write(sc, EMAC_RX_CTL0, rx_ctl0.dw);

	union emac_tx_ctl1 tx_ctl1 = { emac_reg_read(sc, EMAC_TX_CTL1) };
	tx_ctl1.tx_dma_en = 0;
	emac_reg_write(sc, EMAC_TX_CTL1, tx_ctl1.dw);

	union emac_rx_ctl1 rx_ctl1 = { emac_reg_read(sc, EMAC_RX_CTL1) };
	rx_ctl1.rx_dma_en = 0;
	emac_reg_write(sc, EMAC_RX_CTL1, rx_ctl1.dw);

	emac_dev[dev] = NULL;
	return 0;
}

static struct prom_dev emac_prom_dev =
{
	.match = emac_match,
	.open = emac_open,
	.write = emac_send,
	.read = emac_recv,
	.close = emac_close,
	.getmacaddr = emac_getmacaddr,
};

void init_emac(void)
{
	prom_register(&emac_prom_dev);
}


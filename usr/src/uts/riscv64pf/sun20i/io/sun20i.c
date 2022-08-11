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

#include <sys/types.h>
#include <sys/machclock.h>
#include <sys/platform.h>
#include <sys/modctl.h>
#include <sys/platmod.h>
#include <sys/errno.h>
#include <sys/machparam.h>

char *plat_get_cpu_str()
{
	return "Allwinner D1";
}

void set_platform_defaults(void)
{
}

uint64_t plat_get_cpu_clock(int cpu_no)
{
#define PLL_CPU_CTRL_REG (*(volatile uint32_t *)(SEGKPM_BASE + 0x02001000))
	uint32_t pll_cpu = PLL_CPU_CTRL_REG;
	uint32_t pll_n = ((pll_cpu >> 8) & 0xFF);
	uint32_t pll_m = ((pll_cpu >> 0) & 0x3);

	return 24000000ul * (pll_n + 1) * (pll_m + 1);
}

static struct modlmisc modlmisc = {
	&mod_miscops, "platmod"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init(void)
{
	return mod_install(&modlinkage);
}

int
_fini(void)
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

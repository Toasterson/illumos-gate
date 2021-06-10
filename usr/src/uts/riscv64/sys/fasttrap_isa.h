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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_FASTTRAP_ISA_H
#define	_FASTTRAP_ISA_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif
#define FASTTRAP_INSTR	0x0000
#define	FASTTRAP_SUNWDTRACE_SIZE	128
typedef uint16_t	fasttrap_instr_t;

typedef struct fasttrap_machtp {
	uint32_t	ftmt_instr;	/* orig. instr. */
	uint8_t		ftmt_type;	/* emulation type */
} fasttrap_machtp_t;

#define	ftt_instr	ftt_mtp.ftmt_instr
#define	ftt_type	ftt_mtp.ftmt_type

#define	FASTTRAP_RETURN_AFRAMES		4
#define	FASTTRAP_ENTRY_AFRAMES		3
#define	FASTTRAP_OFFSET_AFRAMES		3

enum {
	FASTTRAP_T_COMMON,
	FASTTRAP_T_JAL,
	FASTTRAP_T_JALR,
	FASTTRAP_T_BEQ,
	FASTTRAP_T_BNE,
	FASTTRAP_T_BLT,
	FASTTRAP_T_BGE,
	FASTTRAP_T_BLTU,
	FASTTRAP_T_BGEU,
	FASTTRAP_T_CJ,
	FASTTRAP_T_CJR,
	FASTTRAP_T_CBEQZ,
	FASTTRAP_T_CBNEZ,
	FASTTRAP_T_AUIPC,
};

#ifdef	__cplusplus
}
#endif

#endif	/* _FASTTRAP_ISA_H */

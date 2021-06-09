#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2021 Hayashi Naoyuki
#

LIBCTFSRC=	$(SRC)/lib/libctf/common
LIBDWARFSRC=	$(SRC)/lib/libdwarf/common

CPPFLAGS+=	-I$(CROSS_TOOLS)/src/dtrace/include -I$(SRC)/common/ctf -I$(LIBDWARFSRC)
CPPFLAGS+=	-I$(CROSS_TOOLS)/include
CPPFLAGS+=	-I$(LIBCTFSRC)
CPPFLAGS+=	-D_LARGEFILE64_SOURCE -DCTF_OLD_VERSIONS -D__STDC_WANT_LIB_EXT2__
CPPFLAGS+=	-D_CROSS_TOOLS -D_LP64 -DELF_TARGET_ALL=1
CFLAGS+=	-pthread -std=gnu11 -ffunction-sections -fdata-sections -g

CPPFLAGS_aarch64 += -D_TARGET_AARCH64 -D_LITTLE_ENDIAN
CPPFLAGS_riscv64 += -D_TARGET_RISCV -D_LITTLE_ENDIAN
CPPFLAGS+=	$(CPPFLAGS_$(MACH))

LFLAGS = -t -v
YFLAGS = -d -v

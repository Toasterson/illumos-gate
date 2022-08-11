#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
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
# Copyright 2022 Hayashi Naoyuki
#

include ../../Makefile.uts

SUBDIRS = platmod platuart platmac rootnex unix platmmc

def all install install_h clean clobber lint: $(SUBDIRS)

def		: 	TARGET = def
all		: 	TARGET = all
clean		: 	TARGET = clean
clobber		: 	TARGET = clobber
lint		: 	TARGET = lint
install		: 	TARGET = install
install_h	: 	TARGET = install_h

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

BOARD=			sun20i
ROOT_PSM_DIR		= $(ROOT_PLAT_DIR)/SUNW,sun20i

BOOT_ARCHIVE		= boot_archive
ROOT_BOOT_ARCHIVE	= $(ROOT_PSM_DIR)/boot_archive

install: $(ROOT_BOOT_ARCHIVE)
$(ROOT_BOOT_ARCHIVE): $(SUBDIRS) FRC

MKISOFS = mkisofs
MKISOFS_FLAGS = -R -J
MKISOFS_EXCEPT_FILES = stand lib etc/termcap etc/terminfo tmp
MKISOFS_EXCEPT_FILES += etc/svc
MKISOFS_EXCEPT_FILES += bin sbin root
MKISOFS_EXCEPT_FILES += usr var mnt
MKISOFS_EXCEPT_FILES += kernel/genunix
MKISOFS_EXCEPT_FILES += "platform/*/boot_archive"
MKISOFS_EXCEPT_FILES += "platform/*/inetboot"
MKISOFS_FLAGS += $(MKISOFS_EXCEPT_FILES:%=-x $(ROOT)/%)
MKISOFS_FLAGS += -x $(BOOT_ARCHIVE)

$(ROOT_BOOT_ARCHIVE): $(ROOT_PSM_DIR)
	$(MKISOFS) $(MKISOFS_FLAGS) -o $@ $(ROOT)

$(ROOT_PSM_DIR):
	$(INS.dir)

FRC:

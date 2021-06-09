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
# Copyright 2017 Hayashi Naoyuki
#

include ../../../Makefile.master


HDRS= \
      ctf_api.h \
      ctf.h \
      list.h \
      list_impl.h \
      avl_impl.h \
      avl.h \
      bitmap.h \
      dtrace_impl.h \
      dtrace.h \
      objfs.h \
      note.h \
      zmod.h

.PHONY: all install clean clobber ctfconvert_build ctfmerge_build ctfdump_build dtrace_build libctf_build libdwarf_build
all install: ctfconvert_build ctfmerge_build ctfdump_build dtrace_build libctf_build libdwarf_build

ctfconvert_build: $(HDRS:%=include/sys/%) libctf_build libdwarf_build
	$(MAKE) -C ctfconvert $(MAKECMDGOALS)

ctfmerge_build: $(HDRS:%=include/sys/%) libctf_build libdwarf_build
	$(MAKE) -C ctfmerge $(MAKECMDGOALS)

ctfdump_build: $(HDRS:%=include/sys/%) libctf_build libdwarf_build
	$(MAKE) -C ctfdump $(MAKECMDGOALS)

dtrace_build: $(HDRS:%=include/sys/%) libctf_build libdwarf_build
	$(MAKE) -C dtrace $(MAKECMDGOALS)

libctf_build: $(HDRS:%=include/sys/%)
	$(MAKE) -C libctf $(MAKECMDGOALS)

libdwarf_build: $(HDRS:%=include/sys/%)
	$(MAKE) -C libdwarf $(MAKECMDGOALS)

include/sys/%: $(SRC)/uts/common/sys/%
	$(INS.file)

clean clobber: ctfconvert_clean ctfmerge_clean ctfdump_clean dtrace_clean libctf_clean libdwarf_clean
	$(RM) $(HDRS:%=include/sys/%)

ctfconvert_clean:
	$(MAKE) -C ctfconvert $(MAKECMDGOALS)

ctfmerge_clean:
	$(MAKE) -C ctfmerge $(MAKECMDGOALS)

ctfdump_clean:
	$(MAKE) -C ctfdump $(MAKECMDGOALS)

dtrace_clean:
	$(MAKE) -C dtrace $(MAKECMDGOALS)

libctf_clean:
	$(MAKE) -C libctf $(MAKECMDGOALS)

libdwarf_clean:
	$(MAKE) -C libdwarf $(MAKECMDGOALS)

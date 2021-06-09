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

include ../../Makefile.cross
include ../Makefile.com

LIBRARY=	libctf.a

OBJECTS=	ctf_convert.o ctf_create.o ctf_decl.o ctf_diff.o \
		ctf_dwarf.o ctf_elfwrite.o ctf_error.o ctf_hash.o \
		ctf_labels.o ctf_lib.o ctf_lookup.o ctf_merge.o \
		ctf_open.o ctf_subr.o ctf_types.o ctf_util.o \
		mergeq.o workq.o avl.o list.o

MERGEQ=		$(SRC)/lib/mergeq
CTFCOMMON=	$(SRC)/common/ctf
AVLCOMMON=	$(SRC)/common/avl
LISTCOMMON=	$(SRC)/common/list
CPPFLAGS+=	-I$(MERGEQ)

all install clean: FRC

%.o: $(MERGEQ)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(AVLCOMMON)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(LISTCOMMON)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(CTFCOMMON)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(LIBCTFSRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)

install: all

clobber: clean

clean:
	$(RM) $(LIBRARY) $(OBJECTS)

FRC:

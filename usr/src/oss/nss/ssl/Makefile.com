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
# Copyright 2017 Hayashi Naoyuki
#

LIBRARY = libssl3.a
VERS = .1
OBJECTS = \
        authcert.o \
        cmpcert.o \
        dtls13con.o \
        dtlscon.o \
        prelib.o \
        selfencrypt.o \
        ssl3con.o \
        ssl3ecc.o \
        ssl3ext.o \
        ssl3exthandle.o \
        ssl3gthr.o \
        sslauth.o \
        sslbloom.o \
        sslcert.o \
        sslcon.o \
        ssldef.o \
        sslencode.o \
        sslenum.o \
        sslerr.o \
        sslerrstrs.o \
        sslgrp.o \
        sslinfo.o \
        sslinit.o \
        sslmutex.o \
        sslnonce.o \
        sslprimitive.o \
        sslreveal.o \
        sslsecur.o \
        sslsnce.o \
        sslsock.o \
        sslspec.o \
        ssltrace.o \
        sslver.o \
        tls13con.o \
        tls13ech.o \
        tls13echv.o \
        tls13exthandle.o \
        tls13hashstate.o \
        tls13hkdf.o \
        tls13psk.o \
        tls13replay.o \
        tls13subcerts.o \
        unix_err.o

include ../../Makefile.nss

HDRDIR=		$(NSS_BASE)/lib/ssl
SRCDIR=		$(NSS_BASE)/lib/ssl

LIBS =		$(DYNLIB)

MAPFILE=$(SRCDIR)/ssl.def
MAPFILES=mapfile-vers
LDLIBS += -lnss3 -lnssutil3 $(NSSLIBS) -lbsm -lz

all: $(LIBS)
install: all $(ROOTLIBS) $(ROOTLINKS)
include $(SRC)/lib/Makefile.targ

$(LIBS): $(MAPFILES)
$(MAPFILES): $(MAPFILE)
	grep -v ';-' $< | sed -e 's,;+,,' -e 's; DATA ;;' -e 's,;;,,' -e 's,;.*,;,' > $@

CLEANFILES+=$(MAPFILES)

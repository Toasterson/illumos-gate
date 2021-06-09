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

PROG=		dtrace

MACHOBJS=	dtrace.o dt_options.o dt_error.o dt_subr.o dt_string.o \
		dt_ident.o dt_strtab.o dt_list.o dt_grammar.o dt_lex.o \
		dt_parser.o dt_errtags.o dt_provider.o gmatch.o \
		dt_program.o  dt_decl.o dt_cc.o dt_open.o \
		dt_xlator.o dt_as.o dt_cg.o dt_regset.o \
		dt_inttab.o dt_module.o dt_dis.o dt_dof.o dt_buf.o \
		dt_printf.o dt_names.o dt_map.o dt_pcb.o dt_sugar.o \
		dt_pragma.o dt_handle.o dt_link.o

LIBDTRACESRC=	$(SRC)/lib/libdtrace/common
CPPFLAGS+=	-I. -I$(LIBDTRACESRC)


CLEANFILES+= dt_errtags.c dt_names.c dt_grammar.c dt_grammar.h y.tab.h y.tab.c dt_lex.c y.output


all install clean clobber: FRC


dt_errtags.c: $(LIBDTRACESRC)/mkerrtags.sh $(LIBDTRACESRC)/dt_errtags.h
	sh $(LIBDTRACESRC)/mkerrtags.sh < $(LIBDTRACESRC)/dt_errtags.h > $@

dt_names.c: $(LIBDTRACESRC)/mknames.sh $(SRC)/uts/common/sys/dtrace.h
	sh $(LIBDTRACESRC)/mknames.sh < $(SRC)/uts/common/sys/dtrace.h > $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(SRC)/cmd/dtrace/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(SRC)/lib/libgen/common/%.c
	$(CC) $(CPPFLAGS) -I$(SRC)/lib/libgen/inc $(CFLAGS) -c -o $@ $<

%.o: $(LIBDTRACESRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

all: $(PROG)

$(PROG): $(MACHOBJS)
	$(CC) -o $@ $(MACHOBJS) -L$(CROSS_TOOLS)/lib -Wl,-rpath $(CROSS_TOOLS)/lib ../libctf/libctf.a ../libdwarf/libdwarf.a -lpthread -ldl -lelf -lbsd

install: all
	test -d $(BINDIR) || mkdir -p $(BINDIR)
	$(INSTALL) -m 755 $(PROG) $(BINDIR)/$(PROG)

$(MACHOBJS): dt_grammar.h

dt_grammar.c: dt_grammar.h
	$(MV) y.tab.c $@
	$(RM) y.tab.c

dt_grammar.h: $(LIBDTRACESRC)/dt_grammar.y
	$(YACC) $(YFLAGS) $<
	$(MV) y.tab.h $@

dt_lex.c: $(LIBDTRACESRC)/dt_lex.l dt_grammar.h
	$(LEX) $(LFLAGS) $< | $(SED) -e 's/FILE \*yyin = {stdin}, \*yyout = {stdout};/FILE *yyin, *yyout; __attribute__((constructor)) static void constructor() { yyin = stdin; yyout = stdout; }/' > $@

clobber: clean

clean:
	$(RM) $(PROG) $(MACHOBJS) $(CLEANFILES)

FRC:

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

LIBRARY=	libdwarf.a

OBJECTS=dwarf_abbrev.o		\
	dwarf_alloc.o		\
	dwarf_arange.o		\
	dwarf_debuglink.o	\
	dwarf_die_deliv.o	\
	dwarf_dnames.o		\
	dwarf_dsc.o		\
	dwarf_elf_access.o	\
	dwarf_elf_load_headers.o \
	dwarf_elf_rel_detector.o \
	dwarf_elfread.o		\
	dwarf_error.o		\
	dwarf_form.o		\
	dwarf_frame.o		\
	dwarf_frame2.o		\
	dwarf_funcs.o		\
	dwarf_gdbindex.o	\
	dwarf_generic_init.o	\
	dwarf_global.o		\
	dwarf_groups.o		\
	dwarf_harmless.o	\
	dwarf_init_finish.o	\
	dwarf_leb.o		\
	dwarf_line.o		\
	dwarf_loc.o		\
	dwarf_locationop_read.o \
	dwarf_loclists.o	\
	dwarf_machoread.o	\
	dwarf_macro.o		\
	dwarf_macro5.o		\
	dwarf_names.o		\
	dwarf_object_detector.o	\
	dwarf_object_read_common.o \
	dwarf_original_elf_init.o	\
	dwarf_peread.o		\
	dwarf_print_lines.o	\
	dwarf_pubtypes.o	\
	dwarf_query.o		\
	dwarf_ranges.o		\
	dwarf_rnglists.o	\
	dwarf_str_offsets.o	\
	dwarf_stringsection.o	\
	dwarf_stubs.o		\
	dwarf_tied.o		\
	dwarf_tsearchhash.o	\
	dwarf_types.o		\
	dwarf_util.o		\
	dwarf_vars.o		\
	dwarf_weaks.o		\
	dwarf_xu_index.o	\
	dwarfstring.o		\
	dwgetopt.o		\
	gennames.o		\
	malloc_check.o		\
	pro_alloc.o		\
	pro_arange.o		\
	pro_die.o		\
	pro_dnames.o		\
	pro_encode_nm.o		\
	pro_error.o		\
	pro_expr.o		\
	pro_finish.o		\
	pro_forms.o		\
	pro_frame.o		\
	pro_funcs.o		\
	pro_init.o		\
	pro_line.o		\
	pro_log_extra_flag_strings.o \
	pro_macinfo.o		\
	pro_pubnames.o		\
	pro_reloc_stream.o	\
	pro_reloc_symbolic.o	\
	pro_reloc.o		\
	pro_section.o		\
	pro_types.o		\
	pro_vars.o		\
	pro_weaks.o

all install clean: FRC

%.o: $(LIBDWARFSRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)

install: all

clobber: clean

clean:
	$(RM) $(LIBRARY) $(OBJECTS)

FRC:

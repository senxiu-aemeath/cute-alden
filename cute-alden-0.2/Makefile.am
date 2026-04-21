## Process this file with automake to produce Makefile.in

#
# Makefile for cute-alden
# Copyright (C) 2025  Matthew Skala
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Matthew Skala
# http://ansuz.sooke.bc.ca/
# mskala@ansuz.sooke.bc.ca
#
# Modified for the cute-alden fork on 2026-04-21.
# See the repository README.md and CHANGELOG for downstream changes.
#

############################################################################

# BASIC AUTOTOOLS STUFF


ACLOCAL_AMFLAGS = -I m4
@SET_MAKE@

empty:=
space:=$(empty) $(empty)
comma:=,
percent=%

$(if $(VPATH),$(warning ===================================================))
$(if $(VPATH),$(warning Builds outside the source tree are not recommended,))
$(if $(VPATH),$(warning and are supported only for the purpose of creating))
$(if $(VPATH),$(warning packages with make dist.  Build inside the source))
$(if $(VPATH),$(warning tree if possible.))
$(if $(VPATH),$(warning ===================================================))

############################################################################

# FILENAMES FOR AUTOTOOLS

EXTRA_DIST:=gnugetopt.h

bin_PROGRAMS=cute-alden
cute_alden_SOURCES = alden.c

man1_MANS = cute-alden.1

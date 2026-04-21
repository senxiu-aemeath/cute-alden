# Process this file with autoconf to produce a configure script.

#
# Configuration source for cute-alden
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

#
############################################################################
#
# Override Autoconf's argument parsing - must be done here,
# before AC_INIT has a chance to run
#

# General macro for editing a definition
m4_define([ALDEN_DEFINITION_SUBST],
          [m4_define([$1],
                     m4_bpatsubst(m4_dquote(m4_defn([$1])),
                                  [$2],
                                  [$3]))])

# Delete an option from the argument parser
m4_define([ALDEN_DELETE_CMDLINE_OPTION],
          [ALDEN_DEFINITION_SUBST([_AC_INIT_PARSE_ARGS],
                                  [\
  -$1[^)]*)\
.*;;\
  -$1=\*[^)]*)\
.*;;\
],
                                  [\
  # Deleted $1 option\
])
           ALDEN_DEFINITION_SUBST([_AC_INIT_PARSE_ARGS],
                                  [\
  -$1[^)]*)\
    # Obsolete.*\
.*;;\
],
                                  [\
  # Deleted obsolete $1 option\
])
           ])

# Delete an install dir option from both parser and help
m4_define([ALDEN_DELETE_INSTDIR_OPTION],
          [ALDEN_DELETE_CMDLINE_OPTION([$1dir])
           ALDEN_DEFINITION_SUBST([_AC_INIT_PARSE_ARGS],
                                  [for ac_var in\([^#]*\).\b$1dir\b],
                                  [for ac_var in\1])
           ALDEN_DEFINITION_SUBST([_AC_INIT_HELP],
                                  [  --$1dir=DIR.*\
],
                                  [])])

# Delete nearly all of the default install dirs
ALDEN_DELETE_INSTDIR_OPTION([data])
ALDEN_DELETE_INSTDIR_OPTION([dataroot])
ALDEN_DELETE_INSTDIR_OPTION([doc])
ALDEN_DELETE_INSTDIR_OPTION([dvi])
ALDEN_DELETE_INSTDIR_OPTION([html])
ALDEN_DELETE_INSTDIR_OPTION([include])
ALDEN_DELETE_INSTDIR_OPTION([info])
ALDEN_DELETE_INSTDIR_OPTION([lib])
ALDEN_DELETE_INSTDIR_OPTION([libexec])
ALDEN_DELETE_INSTDIR_OPTION([info])
ALDEN_DELETE_INSTDIR_OPTION([locale])
ALDEN_DELETE_INSTDIR_OPTION([localstate])
ALDEN_DELETE_INSTDIR_OPTION([oldinclude])
ALDEN_DELETE_INSTDIR_OPTION([pdf])
ALDEN_DELETE_INSTDIR_OPTION([ps])
ALDEN_DELETE_INSTDIR_OPTION([runstate])
ALDEN_DELETE_INSTDIR_OPTION([sbin])
ALDEN_DELETE_INSTDIR_OPTION([sharedstate])
ALDEN_DELETE_INSTDIR_OPTION([sysconf])

#
############################################################################
#
# Autoconf initialization
#
AC_PREREQ([2.72])
AC_INIT([cute-alden],[0.2],[mskala@ansuz.sooke.bc.ca],
  [cute-alden],[[http://ansuz.sooke.bc.ca/]])
AC_PRESERVE_HELP_ORDER
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([alden.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_REVISION([$Id: configure.ac 12372 2025-06-24 15:17:53Z mskala $])
AC_COPYRIGHT([Copyright (C) 2025 Matthew Skala])
AC_SUBST([release_date],["June 24, 2025"])
AM_SILENT_RULES
#
############################################################################
#
# Checks for programs (and other things controlled by environment variables)
#
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_GREP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_MKDIR_P
AC_PATH_PROG([AUTOCONF],[autoconf])
AC_PATH_PROG([AUTOMAKE],[automake])
AC_PATH_PROG([GZIP],[gzip])
AC_PATH_PROG([MAKE],[make])
#
############################################################################
#
# Checks for libraries.
#
#
############################################################################
#
# Checks for header files.
#
#
############################################################################
#
# Checks for typedefs, structures, and compiler characteristics.
#
#
############################################################################
#
# Checks for library functions.
#
AX_FUNC_GETOPT_LONG
AC_CHECK_FUNCS([pipe2])
#
############################################################################
#
# Checks for system services
#
AC_SYS_LONG_FILE_NAMES
#
############################################################################
#
# User-settable options
#
#
############################################################################
#
# Generate output
#
AC_SUBST([andt],[])
AC_CONFIG_FILES([Makefile cute-alden.1])
AC_OUTPUT

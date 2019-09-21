# Adapted from Autoconf Version 2.63 (GPLv2).
#
# Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008
# Free Software Foundation, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#
# As a special exception, the Free Software Foundation gives unlimited
# permission to copy, distribute and modify the configure scripts that
# are the output of Autoconf.  You need not follow the terms of the GNU
# General Public License when using or distributing such scripts, even
# though portions of the text of Autoconf appear in them.  The GNU
# General Public License (GPL) does govern all other use of the material
# that constitutes the Autoconf program.
#
# Certain portions of the Autoconf source text are designed to be copied
# (in certain cases, depending on the input) into the output of
# Autoconf.  We call these the "data" portions.  The rest of the Autoconf
# source text consists of comments plus executable code that decides which
# of the data portions to output in any given case.  We call these
# comments and executable code the "non-data" portions.  Autoconf never
# copies any of the non-data portions into its output.
#
# This special exception to the GPL applies to versions of Autoconf
# released by the Free Software Foundation.  When you make and
# distribute a modified version of Autoconf, you may extend this special
# exception to the GPL to apply to your modified version as well, *unless*
# your modified version has the potential to copy into its output some
# of the text that was the non-data portion of the version that you started
# with.  (In other words, unless your change moves or copies text from
# the non-data portions to the data portions.)  If your modification has
# such potential, you must delete any notice of this special exception
# to the GPL from your modified version.

# AS_LN_SRF_PREPARE
# ------------------------------------
m4_defun([AS_LN_SRF_PREPARE],
[rm -f conf$$ conf$$.exe conf$$.file
if test -d conf$$.dir; then
  rm -f conf$$.dir/conf$$.file
else
  rm -f conf$$.dir
  mkdir conf$$.dir 2>/dev/null
fi
if (echo >conf$$.file) 2>/dev/null; then
  if ln -s -r conf$$.file conf$$ 2>/dev/null; then
    as_ln_srf='ln -s -r -f'
  elif ln -s conf$$.file conf$$ 2>/dev/null; then
    as_ln_srf='./ln-srf'
    # ... but there are two gotchas:
    # 1) On MSYS, both `ln -s file dir' and `ln file dir' fail.
    # 2) DJGPP < 2.04 has no symlinks; `ln -s' creates a wrapper executable.
    # In both cases, we have to default to `cp -pRf'.
    ln -s conf$$.file conf$$.dir 2>/dev/null && test ! -f conf$$.exe ||
      as_ln_srf='cp -pRf'
  elif ln conf$$.file conf$$ 2>/dev/null; then
    as_ln_srf=ln
  else
    as_ln_srf='cp -pRf'
  fi
else
  as_ln_srf='cp -pRf'
fi
rm -f conf$$ conf$$.exe conf$$.dir/conf$$.file conf$$.file
rmdir conf$$.dir 2>/dev/null
])# AS_LN_SRF_PREPARE

# AC_PROG_LN_SRF
# --------------------------------
AC_DEFUN([AC_PROG_LN_SRF],
[AC_MSG_CHECKING([whether ln -s -r -f works])
AC_SUBST([LN_SRF], [$as_ln_srf])dnl
if test "$LN_SRF" = "ln -s -r -f"; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no, using $LN_SRF])
fi
])# AC_PROG_LN_SRF

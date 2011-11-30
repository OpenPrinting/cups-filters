dnl
dnl "$Id$"
dnl
dnl   Directory stuff for OpenPrinting CUPS Filters.
dnl
dnl   Copyright 2007-2011 by Apple Inc.
dnl   Copyright 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl   These coded instructions, statements, and computer programs are the
dnl   property of Apple Inc. and are protected by Federal copyright
dnl   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
dnl   which should have been included with this file.  If this file is
dnl   file is missing or damaged, see the license at "http://www.cups.org/".
dnl

AC_PREFIX_DEFAULT(/)

dnl Fix "prefix" variable if it hasn't been specified...
if test "$prefix" = "NONE"; then
	prefix="/"
fi

dnl Fix "exec_prefix" variable if it hasn't been specified...
if test "$exec_prefix" = "NONE"; then
	if test "$prefix" = "/"; then
		exec_prefix="/usr"
	else
		exec_prefix="$prefix"
	fi
fi

dnl Setup CUPS locations...
CUPS_DATADIR="`$CUPSCONFIG --datadir`"
AC_DEFINE_UNQUOTED(CUPS_DATADIR, "$CUPS_DATADIR")
AC_SUBST(CUPS_DATADIR)

CUPS_FONTPATH="$CUPS_DATADIR/fonts"
AC_SUBST(CUPS_FONTPATH)
AC_DEFINE_UNQUOTED(CUPS_FONTPATH, "$CUPS_FONTPATH")

CUPS_SERVERBIN="`$CUPSCONFIG --serverbin`"
AC_DEFINE_UNQUOTED(CUPS_SERVERBIN, "$CUPS_SERVERBIN")
AC_SUBST(CUPS_SERVERBIN)

dnl
dnl End of "$Id$".
dnl

dnl
dnl "$Id$"
dnl
dnl   Scripting configuration stuff for OpenPrinting CUPS Filters.
dnl
dnl   Copyright 2007-2011 by Apple Inc.
dnl   Copyright 1997-2006 by Easy Software Products, all rights reserved.
dnl
dnl   These coded instructions, statements, and computer programs are the
dnl   property of Apple Inc. and are protected by Federal copyright
dnl   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
dnl   which should have been included with this file.  If this file is
dnl   file is missing or damaged, see the license at "http://www.cups.org/".
dnl

dnl Do we have PHP?
AC_ARG_WITH(php, [  --with-php              set PHP interpreter for web interfaces ],
	CUPS_PHP="$withval",
	CUPS_PHP="")

PHPDIR=""
if test "x$CUPS_PHP" != xno; then
	AC_PATH_PROG(PHPCONFIG, php-config)

	if test "x$PHPCONFIG" != x; then
		PHPDIR="scripting/php"
	fi
fi

AC_SUBST(PHPDIR)

dnl
dnl End of "$Id$".
dnl

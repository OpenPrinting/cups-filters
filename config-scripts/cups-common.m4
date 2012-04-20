dnl
dnl "$Id$"
dnl
dnl   Common configuration stuff for OpenPrinting CUPS Filters.
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

dnl We need at least autoconf 2.60...
AC_PREREQ(2.60)

dnl Set the name of the config header file...
AC_CONFIG_HEADER(config.h)

dnl Version number information...
CUPSFILTERS_VERSION="1.0.17"
AC_SUBST(CUPSFILTERS_VERSION)
AC_DEFINE_UNQUOTED(CUPSFILTERS_SVERSION, "cups-filters v$CUPSFILTERS_VERSION")

dnl version of installed CUPS
CUPS_VERSION=`cups-config --version`

dnl Default compiler flags...
CFLAGS="${CFLAGS:=}"
CPPFLAGS="${CPPFLAGS:=}"
LDFLAGS="${LDFLAGS:=}"

dnl Look for CUPS...
AC_PATH_PROG(CUPSCONFIG,cups-config)
if test "x$CUPSCONFIG" = x; then
	AC_MSG_ERROR(Required cups-config is missing. Please install CUPS developer packages.)
fi

CFLAGS="$CFLAGS `cups-config --cflags`"
LDFLAGS="$LDFLAGS `cups-config --ldflags`"
LINKCUPS="`cups-config --image --libs`"
AC_SUBST(LINKCUPS)

dnl Checks for programs...
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_RANLIB
AC_PATH_PROG(AR,ar)
AC_PATH_PROG(CHMOD,chmod)
AC_PATH_PROG(LD,ld)
AC_PATH_PROG(LN,ln)
AC_PATH_PROG(MV,mv)
AC_PATH_PROG(RM,rm)
AC_PATH_PROG(RMDIR,rmdir)
AC_PATH_PROG(SED,sed)
AC_PATH_PROG(GS,gs)
AC_PATH_PROG(PS2PS,ps2ps)
AC_PATH_PROG(PDF2PS,pdf2ps)

AC_MSG_CHECKING(for install-sh script)
INSTALL="`pwd`/install-sh"
AC_SUBST(INSTALL)
AC_MSG_RESULT(using $INSTALL)

if test "x$AR" = x; then
	AC_MSG_ERROR([Unable to find required library archive command.])
fi
if test "x$CC" = x; then
	AC_MSG_ERROR([Unable to find required C compiler command.])
fi

dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_PROG(PKG_CONFIG, pkg-config)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER(sys/ioctl.h,AC_DEFINE(HAVE_SYS_IOCTL_H))

dnl Checks for string functions.
AC_CHECK_FUNCS(strdup strlcat strlcpy)

dnl Check for random number functions...
AC_CHECK_FUNCS(random lrand48 arc4random)

dnl Checks for signal functions.
case "$uname" in
	Linux | GNU)
		# Do not use sigset on Linux or GNU HURD
		;;
	*)
		# Use sigset on other platforms, if available
		AC_CHECK_FUNCS(sigset)
		;;
esac

AC_CHECK_FUNCS(sigaction)

dnl Checks for wait functions.
AC_CHECK_FUNCS(waitpid wait3)

dnl Flags for "ar" command...
case $uname in
        Darwin* | *BSD*)
                ARFLAGS="-rcv"
                ;;
        *)
                ARFLAGS="crvs"
                ;;
esac

AC_SUBST(ARFLAGS)

dnl Libraries needed by backends...
BACKLIBS=""
if test $uname = Darwin; then
	BACKLIBS="-framework IOKit -framework CoreFoundation"
fi
AC_SUBST(BACKLIBS)

dnl
dnl End of "$Id$".
dnl

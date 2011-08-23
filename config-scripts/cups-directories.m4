dnl
dnl "$Id$"
dnl
dnl   Directory stuff for CUPS Legacy.
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

###### TODO: USE cups-config TO GET DIRECTORIES ########

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

dnl Fix "bindir" variable...
if test "$bindir" = "\${exec_prefix}/bin"; then
	bindir="$exec_prefix/bin"
fi

AC_DEFINE_UNQUOTED(CUPS_BINDIR, "$bindir")

dnl Fix "sbindir" variable...
if test "$sbindir" = "\${exec_prefix}/sbin"; then
	sbindir="$exec_prefix/sbin"
fi

AC_DEFINE_UNQUOTED(CUPS_SBINDIR, "$sbindir")

dnl Fix "sharedstatedir" variable if it hasn't been specified...
if test "$sharedstatedir" = "\${prefix}/com" -a "$prefix" = "/"; then
	sharedstatedir="/usr/com"
fi

dnl Fix "datarootdir" variable if it hasn't been specified...
if test "$datarootdir" = "\${prefix}/share"; then
	if test "$prefix" = "/"; then
		datarootdir="/usr/share"
	else
		datarootdir="$prefix/share"
	fi
fi

dnl Fix "datadir" variable if it hasn't been specified...
if test "$datadir" = "\${prefix}/share"; then
	if test "$prefix" = "/"; then
		datadir="/usr/share"
	else
		datadir="$prefix/share"
	fi
elif test "$datadir" = "\${datarootdir}"; then
	datadir="$datarootdir"
fi

dnl Fix "includedir" variable if it hasn't been specified...
if test "$includedir" = "\${prefix}/include" -a "$prefix" = "/"; then
	includedir="/usr/include"
fi

dnl Fix "localstatedir" variable if it hasn't been specified...
if test "$localstatedir" = "\${prefix}/var"; then
	if test "$prefix" = "/"; then
		if test "$uname" = Darwin; then
			localstatedir="/private/var"
		else
			localstatedir="/var"
		fi
	else
		localstatedir="$prefix/var"
	fi
fi

dnl Fix "sysconfdir" variable if it hasn't been specified...
if test "$sysconfdir" = "\${prefix}/etc"; then
	if test "$prefix" = "/"; then
		if test "$uname" = Darwin; then
			sysconfdir="/private/etc"
		else
			sysconfdir="/etc"
		fi
	else
		sysconfdir="$prefix/etc"
	fi
fi

dnl Fix "libdir" variable...
if test "$libdir" = "\${exec_prefix}/lib"; then
	case "$uname" in
		IRIX*)
			libdir="$exec_prefix/lib32"
			;;
		Linux*)
			if test -d /usr/lib64; then
				libdir="$exec_prefix/lib64"
			fi
			;;
		HP-UX*)
			if test -d /usr/lib/hpux32; then
				libdir="$exec_prefix/lib/hpux32"
			fi
			;;
	esac
fi

dnl Setup default locations...
# Cache data...
AC_ARG_WITH(cachedir, [  --with-cachedir         set path for cache files],cachedir="$withval",cachedir="")

if test x$cachedir = x; then
	if test "x$uname" = xDarwin; then
		CUPS_CACHEDIR="$localstatedir/spool/cups/cache"
	else
		CUPS_CACHEDIR="$localstatedir/cache/cups"
	fi
else
	CUPS_CACHEDIR="$cachedir"
fi
AC_DEFINE_UNQUOTED(CUPS_CACHEDIR, "$CUPS_CACHEDIR")
AC_SUBST(CUPS_CACHEDIR)

# Data files
CUPS_DATADIR="$datadir/cups"
AC_DEFINE_UNQUOTED(CUPS_DATADIR, "$datadir/cups")
AC_SUBST(CUPS_DATADIR)

# Icon directory
AC_ARG_WITH(icondir, [  --with-icondir          set path for application icons],icondir="$withval",icondir="")

if test "x$icondir" = x -a -d /usr/share/icons; then
	ICONDIR="/usr/share/icons"
else
	ICONDIR="$icondir"
fi

AC_SUBST(ICONDIR)

# Fonts
AC_ARG_WITH(fontpath, [  --with-fontpath         set font path for pstoraster],fontpath="$withval",fontpath="")

if test "x$fontpath" = "x"; then
	CUPS_FONTPATH="$datadir/cups/fonts"
else
	CUPS_FONTPATH="$fontpath"
fi

AC_SUBST(CUPS_FONTPATH)
AC_DEFINE_UNQUOTED(CUPS_FONTPATH, "$CUPS_FONTPATH")

dnl
dnl End of "$Id$".
dnl

dnl
dnl Fix basic directories (Taken from CUPS)
dnl
dnl Copyright 2007-2014 by Apple Inc.
dnl Copyright 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl These coded instructions, statements, and computer programs are the
dnl property of Apple Inc. and are protected by Federal copyright
dnl law.  Distribution and use rights are outlined in the file "COPYING"
dnl which should have been included with this file.
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

dnl Fix "bindir" variable...
if test "$bindir" = "\${exec_prefix}/bin"; then
	bindir="$exec_prefix/bin"
fi

dnl Fix "sbindir" variable...
if test "$sbindir" = "\${exec_prefix}/sbin"; then
	sbindir="$exec_prefix/sbin"
fi

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
		Linux*)
			if test -d /usr/lib64 -a ! -d /usr/lib64/fakeroot; then
				libdir="$exec_prefix/lib64"
			fi
			;;
	esac
fi


dnl
dnl "$Id$"
dnl
dnl   Compiler stuff for OpenPrinting CUPS Filters.
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

dnl Clear the debugging and non-shared library options unless the user asks
dnl for them...
INSTALL_STRIP=""
OPTIM=""
AC_SUBST(INSTALL_STRIP)
AC_SUBST(OPTIM)

AC_ARG_WITH(optim, [  --with-optim            set optimization flags ])
AC_ARG_ENABLE(debug, [  --enable-debug          build with debugging symbols])

dnl For debugging, keep symbols, otherwise strip them...
if test x$enable_debug = xyes; then
	OPTIM="-g"
else
	INSTALL_STRIP="-s"
fi

dnl Read-only data/program support on Linux...
AC_ARG_ENABLE(relro, [  --enable-relro          build with the GCC relro option])

dnl Update compiler options...
PIEFLAGS=""
AC_SUBST(PIEFLAGS)

RELROFLAGS=""
AC_SUBST(RELROFLAGS)

PHPOPTIONS=""
AC_SUBST(PHPOPTIONS)

if test -n "$GCC"; then
	# Add GCC-specific compiler options...
	if test -z "$OPTIM"; then
		if test "x$with_optim" = x; then
			# Default to optimize-for-size and debug
       			OPTIM="-Os -g"
		else
			OPTIM="$with_optim $OPTIM"
		fi
	fi

	# Generate position-independent code as needed...
	if test $PICFLAG = 1 -a $uname != AIX; then
    		OPTIM="-fPIC $OPTIM"
	fi

	# The -fstack-protector option is available with some versions of
	# GCC and adds "stack canaries" which detect when the return address
	# has been overwritten, preventing many types of exploit attacks.
	AC_MSG_CHECKING(if GCC supports -fstack-protector)
	OLDCFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -fstack-protector"
	AC_TRY_LINK(,,
		OPTIM="$OPTIM -fstack-protector"
		AC_MSG_RESULT(yes),
		AC_MSG_RESULT(no))
	CFLAGS="$OLDCFLAGS"

	# The -fPIE option is available with some versions of GCC and adds
	# randomization of addresses, which avoids another class of exploits
	# that depend on a fixed address for common functions.
	AC_MSG_CHECKING(if GCC supports -fPIE)
	OLDCFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -fPIE"
	AC_TRY_COMPILE(,,
		[case "$CC" in
			*clang)
				PIEFLAGS="-fPIE -Wl,-pie"
				;;
			*)
				PIEFLAGS="-fPIE -pie"
				;;
		esac
		AC_MSG_RESULT(yes)],
		AC_MSG_RESULT(no))
	CFLAGS="$OLDCFLAGS"

	if test "x$with_optim" = x; then
		# Add useful warning options for tracking down problems...
		OPTIM="-Wall -Wno-format-y2k -Wunused $OPTIM"

		# Additional warning options for development testing...
		if test -d .svn; then
			OPTIM="-Wshadow $OPTIM"
			CFLAGS="-Werror-implicit-function-declaration $CFLAGS"
			PHPOPTIONS="-Wno-shadow"
		#else
			#AC_MSG_CHECKING(if GCC supports -Wno-tautological-compare)
			#OLDCFLAGS="$CFLAGS"
			#CFLAGS="$CFLAGS -Werror -Wno-tautological-compare"
			#AC_TRY_COMPILE(,,
			#	[OPTIM="$OPTIM -Wno-tautological-compare"
			#	AC_MSG_RESULT(yes)],
			#	AC_MSG_RESULT(no))
			#CFLAGS="$OLDCFLAGS"
		fi
	fi

	# The -z relro option is provided by the Linux linker command to
	# make relocatable data read-only.
	if test x$enable_relro = xyes; then
		RELROFLAGS="-Wl,-z,relro"
	fi

	# glibc 2.8 and higher breaks peer credentials unless you
	# define _GNU_SOURCE...
	OPTIM="$OPTIM -D_GNU_SOURCE"
fi

dnl
dnl End of "$Id$".
dnl

dnl
dnl "$Id$"
dnl
dnl   Shared library support for OpenPrinting CUPS Filters.
dnl
dnl   Copyright 2007-2011 by Apple Inc.
dnl   Copyright 1997-2005 by Easy Software Products, all rights reserved.
dnl
dnl   These coded instructions, statements, and computer programs are the
dnl   property of Apple Inc. and are protected by Federal copyright
dnl   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
dnl   which should have been included with this file.  If this file is
dnl   file is missing or damaged, see the license at "http://www.cups.org/".
dnl

PICFLAG=1
DSOFLAGS="${DSOFLAGS:=}"

AC_ARG_ENABLE(shared, [  --disable-shared        do not create shared libraries])

if test x$enable_shared != xno; then
	case "$uname" in
		SunOS*)
			LIBCUPSFILTERS="libcupsfilters.so.1"
			DSO="\$(CC)"
			DSOFLAGS="$DSOFLAGS -Wl,-h\`basename \$@\` -G \$(OPTIM)"
			;;
		Linux | GNU | *BSD*)
			LIBCUPSFILTERS="libcupsfilters.so.1"
			DSO="\$(CC)"
			DSOFLAGS="$DSOFLAGS -Wl,-soname,\`basename \$@\` -shared \$(OPTIM)"
			;;
		Darwin*)
			LIBCUPSFILTERS="libcupsfilters.1.dylib"
			DSO="\$(CC)"
			DSOFLAGS="$DSOFLAGS -dynamiclib -single_module -lc"
			;;
		*)
			echo "Warning: shared libraries may not be supported.  Trying -shared"
			echo "         option with compiler."
			LIBCUPSFILTERS="libcupsfilters.so.1"
			DSO="\$(CC)"
			DSOFLAGS="$DSOFLAGS -Wl,-soname,\`basename \$@\` -shared \$(OPTIM)"
			;;
	esac
else
	PICFLAG=0
	LIBCUPSFILTERS="libcupsfilters.a"
	DSO=":"
fi

AC_SUBST(DSO)
AC_SUBST(DSOFLAGS)
AC_SUBST(LIBCUPSFILTERS)

if test x$enable_shared = xno; then
	LINKCUPSFILTERS="../cupsfilters/libcupsfilters.a"
else
	LINKCUPSFILTERS="-L../cupsfilters -lcupsfilters"
fi

AC_SUBST(LINKCUPSFILTERS)

dnl Update libraries for DSOs...
if test "$DSO" != ":"; then
	# When using DSOs the image libraries are linked to libcupsimage.so
	# rather than to the executables.  This makes things smaller if you
	# are using any static libraries, and it also allows us to distribute
	# a single DSO rather than a bunch...
	DSOLIBS="\$(LIBTIFF) \$(LIBPNG) \$(LIBJPEG) \$(LIBZ)"
	IMGLIBS=""

	# Tell the run-time linkers where to find a DSO.  Some platforms
	# need this option, even when the library is installed in a
	# standard location...
	case $uname in
                SunOS*)
                	# Solaris...
			if test $exec_prefix != /usr; then
				DSOFLAGS="-R$libdir $DSOFLAGS"
				LDFLAGS="$LDFLAGS -R$libdir"
			fi
			;;
                *BSD*)
                        # *BSD...
			if test $exec_prefix != /usr; then
				DSOFLAGS="-Wl,-R$libdir $DSOFLAGS"
				LDFLAGS="$LDFLAGS -Wl,-R$libdir"
			fi
			;;
                Linux | GNU)
                        # Linux and HURD...
			if test $exec_prefix != /usr; then
				DSOFLAGS="-Wl,-rpath,$libdir $DSOFLAGS"
				LDFLAGS="$LDFLAGS -Wl,-rpath,$libdir"
			fi
			;;
	esac
else
	DSOLIBS=""
	IMGLIBS="\$(LIBTIFF) \$(LIBPNG) \$(LIBJPEG) \$(LIBZ)"
fi

AC_SUBST(DSOLIBS)
AC_SUBST(IMGLIBS)

dnl
dnl End of "$Id$".
dnl

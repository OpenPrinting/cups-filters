# Process this file with autoconf to create configure.

AC_PREREQ([2.65])

# ====================
# Version informations
# ====================
m4_define([cups_filters_version_major],[1])
m4_define([cups_filters_version_minor],[8])
m4_define([cups_filters_version_micro],[2])
m4_define([cups_filters_version],[cups_filters_version_major.cups_filters_version_minor.cups_filters_version_micro])

# =============
# Automake init
# =============
AC_INIT([cups-filters],[cups_filters_version])
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/ac_define_dir.m4])
m4_include([m4/ax_compare_version.m4])
m4_include([m4/basic-directories.m4])
AM_INIT_AUTOMAKE([1.11 gnu dist-xz dist-bzip2])
AM_SILENT_RULES([yes])
AC_LANG([C++])
AC_CONFIG_HEADERS([config.h])
# Extra defines for the config.h
AH_BOTTOM([
#ifdef HAVE_LONG_LONG
#  define CUPS_LLFMT   "%lld"
#  define CUPS_LLCAST  (long long)
#else
#  define CUPS_LLFMT   "%ld"
#  define CUPS_LLCAST  (long)
#endif /* HAVE_LONG_LONG */

#ifdef HAVE_ARC4RANDOM
#  define CUPS_RAND() arc4random()
#  define CUPS_SRAND(v) arc4random_stir()
#elif defined(HAVE_RANDOM)
#  define CUPS_RAND() random()
#  define CUPS_SRAND(v) srandom(v)
#elif defined(HAVE_LRAND48)
#  define CUPS_RAND() lrand48()
#  define CUPS_SRAND(v) srand48(v)
#else
#  define CUPS_RAND() rand()
#  define CUPS_SRAND(v) srand(v)
#endif /* HAVE_ARC4RANDOM */
])

# ===========================
# Find required base packages
# ===========================
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG([0.20])

# ========================================
# Specify the fontdir patch if not default
# ========================================
AC_ARG_WITH([fontdir],
	[AS_HELP_STRING([--with-fontdir=path], [Specify path to font config directory (default: fonts/conf.d/).])],
	[FONTDIR="$withval"],
	[FONTDIR="fonts/conf.d"]
)
AC_SUBST(FONTDIR)

# ================================
# Find CUPS internals (no pc file)
# ================================
AC_ARG_WITH([cups-config],
	[AS_HELP_STRING([--with-cups-config=path], [Specify path to cups-config executable.])],
	[with_cups_config="$withval"],
	[with_cups_config=system]
)

AS_IF([test "x$with_cups_config" != "xsystem"], [
	CUPSCONFIG=$with_cups_config
], [
	AC_PATH_PROG(CUPSCONFIG, [cups-config])
	AS_IF([test -z "$CUPSCONFIG"], [
		AC_MSG_ERROR([Required cups-config is missing. Please install CUPS developer packages.])
	])
])
CUPS_CFLAGS=`$CUPSCONFIG --cflags`
CUPS_LIBS=`$CUPSCONFIG --image --libs`
CUPS_VERSION=`$CUPSCONFIG --version`
AC_SUBST(CUPS_CFLAGS)
AC_SUBST(CUPS_LIBS)

CUPS_DATADIR="`$CUPSCONFIG --datadir`"
AC_DEFINE_UNQUOTED(CUPS_DATADIR, "$CUPS_DATADIR", [CUPS datadir])
AC_SUBST(CUPS_DATADIR)

CUPS_SERVERROOT="`$CUPSCONFIG --serverroot`"
AC_DEFINE_UNQUOTED(CUPS_SERVERROOT, "$CUPS_SERVERROOT", [CUPS serverroot])
AC_SUBST(CUPS_SERVERROOT)

CUPS_FONTPATH="$CUPS_DATADIR/fonts"
AC_DEFINE_UNQUOTED(CUPS_FONTPATH, "$CUPS_FONTPATH", [Path to CUPS fonts dir])
AC_SUBST(CUPS_FONTPATH)

CUPS_SERVERBIN="`$CUPSCONFIG --serverbin`"
AC_DEFINE_UNQUOTED(CUPS_SERVERBIN, "$CUPS_SERVERBIN", [Path to CUPS binaries dir])
AC_SUBST(CUPS_SERVERBIN)

AX_COMPARE_VERSION([$CUPS_VERSION],[gt],[1.4], [
	AC_DEFINE(CUPS_1_4, 1, [CUPS Version is 1.4 or newer])
])
AC_DEFINE(PDFTOPDF, [], [Needed for pdftopdf filter compilation])
AC_DEFINE_DIR(BANNERTOPDF_DATADIR, "{CUPS_DATADIR}/data", [Directory where bannertopdf finds its data files (PDF templates)])

AC_SEARCH_LIBS([dlopen],
	[dl],
	[AS_IF([test "$ac_cv_search_dlopen" != "none required"], [
		DLOPEN_LIBS="$ac_cv_search_dlopen"
	])],
	AC_MSG_ERROR([unable to find the dlopen() function])
)
AC_SUBST(DLOPEN_LIBS)

# Transient run-time state dir of CUPS
CUPS_STATEDIR=""
AC_ARG_WITH(cups-rundir, [  --with-cups-rundir           set transient run-time state directory of CUPS],CUPS_STATEDIR="$withval",[
        case "$uname" in
                Darwin*)
                        # Darwin (OS X)
                        CUPS_STATEDIR="$CUPS_SERVERROOT"
                        ;;
                *)
                        # All others
                        CUPS_STATEDIR="$localstatedir/run/cups"
                        ;;
        esac])
AC_DEFINE_UNQUOTED(CUPS_STATEDIR, "$CUPS_STATEDIR", [Transient run-time state dir of CUPS])
AC_SUBST(CUPS_STATEDIR)

# Domain socket of CUPS...
CUPS_DEFAULT_DOMAINSOCKET=""
AC_ARG_WITH(cups-domainsocket, [  --with-cups-domainsocket     set unix domain socket name used by CUPS
],
        default_domainsocket="$withval",
        default_domainsocket="")

if test x$enable_domainsocket != xno -a x$default_domainsocket != xno; then
        if test "x$default_domainsocket" = x; then
                case "$uname" in
                        Darwin*)
                                # Darwin and MaxOS X do their own thing...
                                CUPS_DEFAULT_DOMAINSOCKET="$localstatedir/run/cu
psd"
                                ;;
                        *)
                                # All others use FHS standard...
                                CUPS_DEFAULT_DOMAINSOCKET="$CUPS_STATEDIR/cups.sock"
                                ;;
                esac
        else
                CUPS_DEFAULT_DOMAINSOCKET="$default_domainsocket"
        fi
fi
AC_DEFINE_UNQUOTED(CUPS_DEFAULT_DOMAINSOCKET, "$CUPS_DEFAULT_DOMAINSOCKET", "Domain socket of CUPS")
AC_SUBST(CUPS_DEFAULT_DOMAINSOCKET)

# ======================
# Check system functions
# ======================
AC_CHECK_FUNCS(strlcat)
AC_CHECK_FUNCS(strlcpy)
AC_CHECK_FUNCS(sigaction)
AC_CHECK_FUNCS(waitpid wait3)
AC_CHECK_FUNCS(strtoll)
AC_CHECK_FUNCS(open_memstream)
AC_CHECK_FUNCS(getline,[],AC_SUBST([GETLINE],['bannertopdf-getline.$(OBJEXT)']))
AC_CHECK_FUNCS(strcasestr,[],AC_SUBST([STRCASESTR],['pdftops-strcasestr.$(OBJEXT)']))
AC_SEARCH_LIBS(pow, m)

# ========================
# Check for system headers
# ========================
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([sys/stat.h])
AC_CHECK_HEADERS([sys/types.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADERS([zlib.h])
AC_CHECK_HEADERS([endian.h])
AC_CHECK_HEADERS([dirent.h])
AC_CHECK_HEADERS([sys/ioctl.h])

# =============
# Image options
# =============
AC_ARG_ENABLE([imagefilters],
	[AS_HELP_STRING([--disable-imagefilters], [Build the image filters.])],
	[enable_imagefilters="$enableval"],
	[enable_imagefilters=yes]
)
AM_CONDITIONAL([ENABLE_IMAGEFILTERS], [test "x$enable_imagefilters" != "xno"])

# Libraries
AC_ARG_WITH([jpeg],
	[AS_HELP_STRING([--without-jpeg], [Disable jpeg support.])],
	[with_jpeg="$withval"],
	[with_jpeg=yes]
)
AS_IF([test x"$with_jpeg" != "xno"], [
	AC_DEFINE([HAVE_LIBJPEG], [], [Defines if we provide jpeg library.])
	AC_CHECK_HEADERS([jpeglib.h])
	AC_SEARCH_LIBS([jpeg_destroy_decompress],
		[jpeg],
		LIBJPEG_LIBS="-ljpeg",
		AC_MSG_ERROR([jpeg libraries not found.])
	)
	AC_SUBST(LIBJPEG_LIBS)
])

AC_ARG_WITH([png],
	[AS_HELP_STRING([--without-png], [Disable png support.])],
	[with_png="$withval"],
	[with_png=yes]
)
AS_IF([test x"$with_png" != "xno"], [
	PKG_CHECK_MODULES([LIBPNG], [libpng])
	AC_DEFINE([HAVE_LIBPNG], [], [Defines if we provide png library.])
])

AC_ARG_WITH([tiff],
	[AS_HELP_STRING([--without-tiff], [Disable tiff support.])],
	[with_tiff="$withval"],
	[with_tiff=yes]
)
AS_IF([test x"$with_tiff" != "xno"], [
	AC_DEFINE([HAVE_LIBTIFF], [], [Defines if we provide tiff library.])
	AC_CHECK_HEADERS([tiff.h])
	AC_SEARCH_LIBS([TIFFReadScanline],
		[tiff],
		LIBJPEG_LIBS="-ltiff",
		AC_MSG_ERROR([tiff libraries not found.])
	)
	AC_SUBST(LIBTIFF_LIBS)
])

# ==================================
# Check for modules needed by utils/
# ==================================

dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_PROG(PKGCONFIG, pkg-config)

dnl Avahi for cups-browsed
AVAHI_LIBS=""
AVAHI_CFLAGS=""
AVAHI_GLIB_CFLAGS=""
AVAHI_GLIB_LIBS=""

AC_ARG_ENABLE([avahi],
	[AS_HELP_STRING([--disable-avahi], [Disable DNS Service Discovery support using Avahi.])],
	[enable_avahi="$enableval"],
	[enable_avahi=yes]
)
AM_CONDITIONAL([ENABLE_AVAHI], [test "x$enable_avahi" != "xno"])

AC_ARG_WITH(avahi-libs,
	[AS_HELP_STRING([--with-avahi-libs], [Set directory for Avahi library.])],
	AVAHI_LIBS="-L$withval $AVAHI_LIBS",)
AC_ARG_WITH(avahi-includes,
	[AS_HELP_STRING([--with-avahi-includes], [Set directory for Avahi includes])],
	AVAHI_CFLAGS="-I$withval $AVAHI_CFLAGS",)

if test "x$PKGCONFIG" != x -a x$enable_avahi != xno; then
	AC_MSG_CHECKING(for Avahi)
	if $PKGCONFIG --exists avahi-client; then
		AC_MSG_RESULT(yes)
		AVAHI_CFLAGS="$AVAHI_CFLAGS `$PKGCONFIG --cflags avahi-client`"
		AVAHI_LIBS="$AVAHI_LIBS `$PKGCONFIG --libs avahi-client`"
		AC_DEFINE(HAVE_AVAHI, [], [Define if you have the avahi library])
	else
		AC_MSG_RESULT(no)
		enable_avahi=no
	fi
fi

AC_SUBST(AVAHI_LIBS)
AC_SUBST(AVAHI_CFLAGS)

dnl
dnl   LDAP configuration stuff for CUPS.
dnl
dnl   Copyright 2007-2011 by Apple Inc.
dnl   Copyright 2003-2006 by Easy Software Products, all rights reserved.
dnl
dnl   These coded instructions, statements, and computer programs are the
dnl   property of Apple Inc. and are protected by Federal copyright
dnl   law.  Distribution and use rights are outlined in the file "COPYING"
dnl   which should have been included with this file.
dnl

AC_ARG_ENABLE([ldap], [AS_HELP_STRING([--disable-ldap], [disable LDAP support.])],
        [enable_ldap="$enableval"],
        [enable_ldap=yes]
)
AC_ARG_WITH([ldap-libs], [AS_HELP_STRING([--with-ldap-libs], [set directory for LDAP library.])],
    LDFLAGS="-L$withval $LDFLAGS"
    DSOFLAGS="-L$withval $DSOFLAGS",)
AC_ARG_WITH([ldap-includes], [AS_HELP_STRING([--with-ldap-includes], [set directory for LDAP includes.])],
    CFLAGS="-I$withval $CFLAGS"
    CPPFLAGS="-I$withval $CPPFLAGS",)

if test x$enable_ldap != xno; then

    AC_CHECK_HEADER([ldap.h], [
	AC_SEARCH_LIBS([ldap_initialize], [ldap], [
	    AC_DEFINE([HAVE_LDAP], [], [Define if LDAP support should be enabled])
	    AC_DEFINE([HAVE_OPENLDAP], [], [If LDAP support is that of OpenLDAP])
	    AC_CHECK_LIB([ldap], [ldap_start_tls],
		AC_DEFINE([HAVE_LDAP_SSL], [], [If LDAP has SSL/TLS support enabled]))],[

	    AC_CHECK_LIB([ldap], [ldap_init], [
		AC_DEFINE([HAVE_LDAP], [], [Define if LDAP support should be enabled])
		AC_DEFINE([HAVE_MOZILLA_LDAP], [], [If LDAP support is that of Mozilla])
		AC_CHECK_HEADERS([ldap_ssl.h], [], [], [#include <ldap.h>])
		AC_CHECK_LIB([ldap], [ldapssl_init],
		    AC_DEFINE([HAVE_LDAP_SSL], [], [If LDAP has SSL/TLS support enabled]))])]
	)
	AC_CHECK_LIB([ldap], [ldap_set_rebind_proc], AC_DEFINE([HAVE_LDAP_REBIND_PROC], [], [If libldap implements ldap_set_rebind_proc]))
    ])

fi

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.30.2])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

if test "x$PKGCONFIG" != x -a x$enable_avahi != xno; then
	PKG_CHECK_MODULES(AVAHI_GLIB, [avahi-glib])
fi

AC_SUBST(AVAHI_GLIB_CFLAGS)
AC_SUBST(AVAHI_GLIB_LIBS)

GIO_CFLAGS=""
GIO_LIBS=""
if test "x$PKGCONFIG" != x; then
	PKG_CHECK_MODULES(GIO, [gio-2.0])
fi
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

GIO_UNIX_CFLAGS=""
GIO_UNIX_LIBS=""
if test "x$PKGCONFIG" != x; then
	PKG_CHECK_MODULES(GIO_UNIX, [gio-unix-2.0])
fi
AC_SUBST(GIO_UNIX_CFLAGS)
AC_SUBST(GIO_UNIX_LIBS)

AC_ARG_WITH([browseremoteprotocols],
	[AS_HELP_STRING([--with-browseremoteprotocols=value], [Set which protocols to listen for in cups-browsed (default: dnssd cups)])],
	[with_browseremoteprotocols="$withval"],
	[with_browseremoteprotocols="dnssd cups"]
)
BROWSEREMOTEPROTOCOLS="$with_browseremoteprotocols"
AC_SUBST(BROWSEREMOTEPROTOCOLS)

dnl Setup init.d locations...
AC_ARG_WITH(rcdir, [AS_HELP_STRING([--with-rcdir], [Set path for rc scripts])],rcdir="$withval",rcdir="")
AC_ARG_WITH(rclevels, [AS_HELP_STRING([--with-rclevels], [Set run levels for rc scripts])],rclevels="$withval",rclevels="2 3 5")
AC_ARG_WITH(rcstart, [AS_HELP_STRING([--with-rcstart], [Set start number for rc scripts])],rcstart="$withval",rcstart="99")
AC_ARG_WITH(rcstop, [AS_HELP_STRING([--with-rcstop], [Set stop number for rc scripts])],rcstop="$withval",rcstop="00")

INITDIR=""
INITDDIR=""
RCLEVELS="$rclevels"
RCSTART="$rcstart"
RCSTOP="$rcstop"

if test x$rcdir = x; then
	case "`uname`" in
		FreeBSD* | OpenBSD* | MirBSD* | ekkoBSD*)
			# FreeBSD and OpenBSD
			;;

		Linux | GNU | GNU/k*BSD*)
			# Linux/HURD seems to choose an init.d directory at random...
			if test -d /sbin/init.d; then
				# SuSE
				INITDIR="/sbin/init.d"
			else
				if test -d /etc/init.d; then
					# Others
					INITDIR="/etc"
				else
					# RedHat
					INITDIR="/etc/rc.d"
				fi
			fi
			RCSTART="82"
			RCSTOP="35"
			;;

		NetBSD*)
			# NetBSD
			INITDDIR="/etc/rc.d"
			;;

		*)
			INITDIR="/etc"
			;;

	esac
elif test "x$rcdir" != xno; then
	if test "x$rclevels" = x; then
		INITDDIR="$rcdir"
	else
		INITDIR="$rcdir"
	fi
fi

AM_CONDITIONAL([RCLINKS], [test "x$INITDIR" != "x"])

if test "x${INITDIR}" != "x" -a "x${INITDDIR}" = "x"; then
   INITDDIR="${INITDIR}/init.d"
fi

AC_SUBST(INITDIR)
AC_SUBST(INITDDIR)
AC_SUBST(RCLEVELS)
AC_SUBST(RCSTART)
AC_SUBST(RCSTOP)

# ======================================
# Check for various pdf required modules
# ======================================
PKG_CHECK_MODULES([LCMS], [lcms2], [lcms2=yes], [lcms2=no])
AS_IF([test x"$lcms2" = "xno"], [
	PKG_CHECK_MODULES([LCMS], [lcms])
	AC_DEFINE([USE_LCMS1], [1], [Defines if use lcms1])
])
PKG_CHECK_MODULES([FREETYPE], [freetype2], [AC_DEFINE([HAVE_FREETYPE_H], [1], [Have FreeType2 include files])])
PKG_CHECK_MODULES([FONTCONFIG], [fontconfig >= 2.0.0])
PKG_CHECK_MODULES([IJS], [ijs])
PKG_CHECK_MODULES([POPPLER], [poppler >= 0.18])
PKG_CHECK_MODULES([ZLIB], [zlib])
AC_DEFINE([HAVE_LIBZ], [], [Define that we use zlib])
PKG_CHECK_MODULES([LIBQPDF], [libqpdf >= 3.0.2])

# ================
# Poppler features
# ================
AC_CHECK_HEADER([poppler/cpp/poppler-version.h], [AC_DEFINE([HAVE_CPP_POPPLER_VERSION_H],,[Define if you have Poppler's "cpp/poppler-version.h" header file.])], [])

# ===============
# Check for D-Bus
# ===============
AC_ARG_ENABLE(dbus, AS_HELP_STRING([--enable-dbus],[enable DBus CMS code]),
	      enable_dbus=$enableval,enable_dbus=yes)
AM_CONDITIONAL(BUILD_DBUS, test x$enable_dbus = xyes)
if test x$enable_dbus = xyes; then
	PKG_CHECK_MODULES(DBUS, dbus-1)
fi

# ===================================
# Check for large files and long long
# ===================================
AC_SYS_LARGEFILE
LARGEFILE=""
AS_IF([test x"$enable_largefile" != "xno"], [
	LARGEFILE="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE"
	AS_IF([test x"$ac_cv_sys_large_files" = "x1"], [LARGEFILE="$LARGEFILE -D_LARGE_FILES"])
	AS_IF([test x"$ac_cv_sys_file_offset_bits" = "x64"], [LARGEFILE="$LARGEFILE -D_FILE_OFFSET_BITS=64"])
])
AC_SUBST(LARGEFILE)

AC_CHECK_TYPE(long long, [long_long_found=yes], [long_long_found=no])
AS_IF([test x"$long_long_found" = "xyes"], [
	AC_DEFINE([HAVE_LONG_LONG], [], [Platform supports long long type])
])

# ================
# Check for pdf2ps
# ================
AC_ARG_ENABLE([ghostscript],
	[AS_HELP_STRING([--disable-ghostscript], [Disable filters using Ghostscript.])],
	[enable_ghostscript="$enableval"],
	[enable_ghostscript=yes]
)
AC_ARG_WITH([pdftops],
	[AS_HELP_STRING([--with-pdftops=value], [Set which pdftops to use (gs,pdftops,pdftocairo,acroread,hybrid).])],
	[with_pdftops="$withval"],
	[with_pdftops=hybrid]
)
AS_CASE([x$with_pdftops],
        [xgs|xpdftops|xpdftocairo|xacroread|xhybrid], [],
        [AC_MSG_ERROR([Unknown value of with-pdftops provided: $with_pdftops])]
)
AC_ARG_WITH([gs-path],
	[AS_HELP_STRING([--with-gs-path=value], [Set path to ghostcript binary (default: system).])],
	[with_gs_path="$withval"],
	[with_gs_path=system]
)
AC_ARG_WITH([pdftops-path],
        [AS_HELP_STRING([--with-pdftops-path=value], [Set path to pdftops/ghostscript binary (default: system).])],
        [with_pdftops_path="$withval"],
        [with_pdftops_path=system]
)
AC_ARG_WITH([pdftocairo-path],
        [AS_HELP_STRING([--with-pdftocairo-path=value], [Set path to pdftocairo binary (default: system).])],
        [with_pdftocairo_path="$withval"],
        [with_pdftocairo_path=system]
)
AC_ARG_WITH([acroread-path],
        [AS_HELP_STRING([--with-acroread-path=value], [Set path to acroread binary (default: system).])],
        [with_acroread_path="$withval"],
        [with_acroread_path=system]
)
AC_ARG_WITH([pdftops-maxres],
	[AS_HELP_STRING([--with-pdftops-maxres=value], [Set maximum image rendering resolution for pdftops filter (0, 75, 150, 300, 600, 1200, 2400, 4800, 90, 180, 360, 720, 1440, 2880, 5760, unlimited). Default: 1440])],
	[with_pdftops_maxres="$withval"],
	[with_pdftops_maxres=1440]
)
AS_CASE([x$with_pdftops_maxres],
	[x0|x75|x150|x300|x600|x1200|x2400|x4800|x90|x180|x360|x720|x1440|x2880|x5760], [CUPS_PDFTOPS_MAXRES=$with_pdftops_maxres],
	[xunlimited], [CUPS_PDFTOPS_MAXRES=0],
	[AC_MSG_ERROR([Unknown value of with-pdftops-maxres provided: $with_pdftops])]
)

CUPS_GHOSTSCRIPT=""
AS_IF([test "x$enable_ghostscript" != "xyes"], [
	AS_IF([test "x$with_gs_path" != "xsystem"], [
	       AC_MSG_ERROR([Cannot supply both --disable-ghostscript and --gs-path=])
	], [
		with_gs_path=""
	])
], [
	AS_IF([test "x$with_gs_path" != "xsystem"], [
		CUPS_GHOSTSCRIPT="$with_gs_path"
	], [
		AC_PATH_PROG(CUPS_GHOSTSCRIPT, [gs], [AC_MSG_ERROR([Required gs binary is missing. Please install ghostscript-gpl package.])])
	])
	AS_IF([test "x$CUPS_GHOSTSCRIPT" != "x"], [
		AC_DEFINE([HAVE_GHOSTSCRIPT], [], [Define that we provide ghostscript binary])
		AS_IF([test x"$with_pdftops" = xgs], [AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_RENDERER], [GS], [Define default renderer])])

		AC_MSG_CHECKING(whether gs supports the ps2write device)
		AS_IF([`$CUPS_GHOSTSCRIPT -h 2>&1 | grep -q ps2write`], [
			AC_MSG_RESULT([yes])
			AC_DEFINE([HAVE_GHOSTSCRIPT_PS2WRITE], [], [gs supports ps2write])
		], [
			AC_MSG_RESULT([no])
		])
	], [
		CUPS_GHOSTSCRIPT="gs"
	])
])
AM_CONDITIONAL(ENABLE_GHOSTSCRIPT, test "x$enable_ghostscript" = xyes)

AS_IF([test "x$with_pdftops_path" != "xsystem"], [
	CUPS_PDFTOPS="$with_pdftops_path"
], [
	AC_PATH_PROG(CUPS_PDFTOPS, [pdftops], [AC_MSG_ERROR([Required pdftops is missing. Please install CUPS developer packages.])])
])
AS_IF([test "x$CUPS_PDFTOPS" != "x"], [
	AC_DEFINE([HAVE_POPPLER_PDFTOPS], [], [Define that we provide poppler pdftops.])
	AS_IF([test x"$with_pdftops" = xpdftops], [AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_RENDERER], [PDFTOPS], [Define default renderer])])

	AC_MSG_CHECKING([whether pdftops supports -origpagesizes])
	AS_IF([`$CUPS_PDFTOPS -h 2>&1 | grep -q -- -origpagesizes`], [
        	AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_POPPLER_PDFTOPS_WITH_ORIGPAGESIZES], [] , [pdftops supports -origpagesizes.])
	], [
	        AC_MSG_RESULT([no])
	])
	AC_MSG_CHECKING([whether pdftops supports -r])
	AS_IF([`$CUPS_PDFTOPS -h 2>&1 | grep -q -- '-r '`], [
	        AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_POPPLER_PDFTOPS_WITH_RESOLUTION], [] , [pdftops supports -r argument.])
	], [
        	AC_MSG_RESULT([no])
	])
], [
	CUPS_PDFTOPS="/usr/bin/pdftops"
])
AS_IF([test "x$with_pdftocairo_path" != "xsystem"], [
	CUPS_PDFTOCAIRO="$with_pdftocairo_path"
], [
	AC_PATH_PROG(CUPS_PDFTOCAIRO, [pdftocairo], [AC_MSG_ERROR([Required pdftocairo is missing. Please install Poppler developer packages.])])
])
AS_IF([test "x$CUPS_PDFTOCAIRO" != "x"], [
	AC_DEFINE([HAVE_POPPLER_PDFTOCAIRO], [], [Define that we provide poppler pdftocairo.])
	AS_IF([test x"$with_pdftops" = xpdftocairo], [AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_RENDERER], [PDFTOCAIRO], [Define default renderer])])
], [
	CUPS_PDFTOCAIRO="pdftocairo"
])
AS_IF([test "x$with_acroread_path" != "xsystem"], [
	CUPS_ACROREAD="$with_acroread_path"
], [
	AC_PATH_PROG(CUPS_ACROREAD, [acroread], [AC_MSG_ERROR([Required acroread binary is missing. Please install acroread package.])])
])
AS_IF([test "x$CUPS_ACROREAD" != "x"], [
	AC_DEFINE([HAVE_ACROREAD], [], [Define that we provide acroread.])
	AS_IF([test x"$with_pdftops" = xacroread], [AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_RENDERER], [ACROREAD], [Define default renderer])])
], [
	CUPS_ACROREAD="acroread"
])

AS_IF([test "x$CUPS_GHOSTSCRIPT" != "x" -a "x$CUPS_PDFTOPS" != "x"], [
	AS_IF([test x"$with_pdftops" = xhybrid], [AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_RENDERER], [HYBRID], [Define default renderer])])
])

AC_DEFINE_UNQUOTED([CUPS_GHOSTSCRIPT], "$CUPS_GHOSTSCRIPT", [gs binary to use])
AC_DEFINE_UNQUOTED([CUPS_POPPLER_PDFTOPS], "$CUPS_PDFTOPS", [pdftops binary to use.])
AC_DEFINE_UNQUOTED([CUPS_POPPLER_PDFTOCAIRO], "$CUPS_PDFTOCAIRO", [pdftocairo binary to use.])
AC_DEFINE_UNQUOTED([CUPS_ACROREAD], "$CUPS_ACROREAD", [acroread binary to use.])
AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_MAX_RESOLUTION], [$CUPS_PDFTOPS_MAXRES], [max resolution used for pdftops when converting images])

# ==================
# Check for foomatic
# ==================
AC_ARG_ENABLE([foomatic],
	[AS_HELP_STRING([--disable-foomatic], [Disable Foomatic-based filters.])],
	[enable_foomatic="$enableval"],
	[enable_foomatic=yes]
)
AM_CONDITIONAL([ENABLE_FOOMATIC], [test "x$enable_foomatic" = "xyes"])

# =============
# Check for php
# =============
# NOTE: This stuff is broken, requires internal cups headers.
AC_ARG_WITH([php],
	[AS_HELP_STRING([--with-php], [Determine whether to build php cups extension.])],
	[with_php="$withval"],
	[with_php=no]
)
AC_ARG_WITH([php-config],
	[AS_HELP_STRING([--with-php-config=path], [Specify path to php-config executable.])],
	[with_php_config="$withval"],
	[with_php_config=system]
)
AM_CONDITIONAL([WITH_PHP], [test "x$with_php" = "xyes"])
AS_IF([test x"$with_php" = "xyes"], [
	AS_IF([test "x$with_php_config" != "xsystem"], [
		PHPCONFIG=$with_php_config
	], [
		AC_PATH_PROG(PHPCONFIG, [php-config])
		AS_IF([test -z "$PHPCONFIG"], [
			AC_MSG_ERROR([Required php-config is missing. Please install PHP developer packages.])
		])
	])
	PHPDIR="`$PHPCONFIG --extension-dir`"
	AC_SUBST(PHPDIR)
])

# =========
# Test ARGS
# =========
AC_ARG_WITH([test-font-path],
	[AS_HELP_STRING([--with-test-font-path=value], [Set path to font used for tests (default: /usr/share/fonts/dejavu/DejaVuSans.ttf).])],
	[with_test_font_path="$withval"],
	[with_test_font_path="/usr/share/fonts/dejavu/DejaVuSans.ttf"]
)
AC_DEFINE_UNQUOTED([TESTFONT], ["$with_test_font_path"], [Path to font used in tests])

# ================
# Check for cflags
# ================
AC_ARG_ENABLE([werror],
	[AS_HELP_STRING([--enable-werror], [Treat all warnings as errors, useful for development.])],
	[enable_werror="$enableval"],
	[enable_werror=no]
)
AS_IF([test x"$enable_werror" = "xyes"], [
	CFLAGS="$CFLAGS -Werror"
])
AS_IF([test x"$GCC" = "xyes"], [
	# Be tough with warnings and produce less careless code
	CFLAGS="$CFLAGS -Wall -pedantic -std=gnu99"
	CXXFLAGS="$CXXFLAGS -Wall -pedantic" # -Weffc++" # TODO: enable when it does not print 1MB of warnings
])
CFLAGS="$CFLAGS -D_GNU_SOURCE"
CXXFLAGS="$CXXFLAGS -D_GNU_SOURCE"

# =======
# gstopxl
# =======
AC_PROG_SED
dnl See if it is GNU sed or else.
dnl - need more work to tell SED features.
SED_EXTENDED_REGEX_OPT=-nre
sed_variant=`sed --version 2>&1`
sed_variant=`echo $sed_variant|sed -e 's/ .*//'`
if test "$sed_variant" != GNU ; then
SED_EXTENDED_REGEX_OPT=-nEe
fi
AC_SUBST(SED_EXTENDED_REGEX_OPT)
AC_SUBST(CUPS_GHOSTSCRIPT)

# ==========================
# Braille embossing/liblouis
# ==========================
AC_ARG_ENABLE(braille, AS_HELP_STRING([--enable-braille],[enable Braille embosing filters, requires liblouis]),
	      enable_braille=$enableval,enable_braille=yes)
AC_MSG_CHECKING(for liblouis)
if $PKGCONFIG --exists liblouis; then
	AC_MSG_RESULT(yes)
	if test "x$PKGCONFIG" != x -a "x$enable_braille" = xyes; then
		TABLESDIR=`$PKGCONFIG --variable=tablesdir liblouis`
	else
		TABLESDIR=/usr/share/liblouis/tables
	fi
else
	AC_MSG_RESULT(no)
	TABLESDIR=/usr/share/liblouis/tables
fi
AM_CONDITIONAL(ENABLE_BRAILLE, test "x$enable_braille" = xyes)
AC_SUBST(TABLESDIR)

# =========================================================
# Select a different shell instead of the default /bin/bash
# =========================================================
AC_ARG_WITH([shell],
	[AS_HELP_STRING([--with-shell=path], [Specify path for a modern shell.])],
	[with_shell="$withval"],
	[with_shell="/bin/bash"]
)
AC_DEFINE_UNQUOTED([SHELL], "$with_shell", [Path for a modern shell])

# =====================
# Prepare all .in files
# =====================
AC_CONFIG_FILES([
	libcupsfilters.pc
	libfontembed.pc
	Makefile
	filter/gstopxl
	utils/cups-browsed
	utils/cups-browsed.conf
	filter/foomatic-rip/foomatic-rip.1
	filter/braille/drivers/index/indexv4.sh
	filter/braille/drivers/index/indexv3.sh
	filter/braille/drivers/index/index.sh
	filter/braille/drivers/index/textbrftoindexv3
	filter/braille/drivers/index/imageubrltoindexv3
	filter/braille/drivers/index/imageubrltoindexv4
	filter/braille/drivers/generic/brftoembosser
	filter/braille/filters/cups-braille.sh
	filter/braille/filters/imagetobrf
	filter/braille/filters/texttobrf
])
AC_OUTPUT

# ==============================================
# Display final informations about configuration
# ==============================================
AC_MSG_NOTICE([
==============================================================================
Environment settings:
	CFLAGS:          ${CFLAGS}
	CXXFLAGS:        ${CXXFLAGS}
	LDFLAGS:         ${LDFLAGS}
Build configuration:
	cups-config:     ${with_cups_config}
	font directory:  ${sysconfdir}/${FONTDIR}
	foomatic:        ${enable_foomatic}
	init directory:  ${INITDDIR}
	cups dom socket: ${CUPS_DEFAULT_DOMAINSOCKET}
	ghostscript:     ${enable_ghostscript}
	gs-path:         ${with_gs_path}
	imagefilters:    ${enable_imagefilters}
	jpeg:            ${with_jpeg}
	pdftocairo-path: ${with_pdftocairo_path}
	pdftops:         ${with_pdftops}
	pdftops-path:    ${with_pdftops_path}
	png:             ${with_png}
	php:             ${with_php}
	php-config:      ${with_php_config}
	shell:           ${with_shell}
	test-font:       ${with_test_font_path}
	tiff:            ${with_tiff}
	avahi:           ${enable_avahi}
	dbus:            ${enable_dbus}
	browsing:        ${with_browseremoteprotocols}
	werror:          ${enable_werror}
	braille:	 ${enable_braille}
	braille tables:  ${TABLESDIR}
==============================================================================
])

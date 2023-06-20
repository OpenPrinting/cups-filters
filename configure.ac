# Process this file with autoconf to create configure.

AC_PREREQ([2.65])

# ====================
# Version informations
# ====================
AC_INIT([cups-filters], [2.0rc2], [https://github.com/OpenPrinting/cups-filters/issues], [cups-filters], [https://github.com/OpenPrinting/cups-filters/])
cups_filters_version="AC_PACKAGE_VERSION"
cups_filters_version_major="`echo AC_PACKAGE_VERSION | awk -F. '{print $1}'`"
cups_filters_version_major="`echo AC_PACKAGE_VERSION | awk -F. '{printf("%d\n",$2);}'`"

# =============
# Automake init
# =============
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/ac_define_dir.m4])
m4_include([m4/ax_compare_version.m4])
m4_include([m4/basic-directories.m4])
AM_INIT_AUTOMAKE([1.11 gnu dist-xz dist-bzip2 subdir-objects foreign])
AM_SILENT_RULES([yes])
AC_LANG([C])
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
AM_PROG_CC_C_O
AM_ICONV
AC_PROG_INSTALL
AC_PROG_LN_S
AS_LN_SRF_PREPARE
AC_PROG_LN_SRF
AC_PROG_MAKE_SET
LT_INIT
PKG_PROG_PKG_CONFIG([0.20])

AM_GNU_GETTEXT_VERSION([0.18.3])
AM_GNU_GETTEXT([external])

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
	AC_PATH_TOOL(CUPSCONFIG, [cups-config])
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

# ========================
# Check for libcupsfilters
# ========================
PKG_CHECK_MODULES([LIBCUPSFILTERS], [libcupsfilters])

# ================
# Check for libppd
# ================
PKG_CHECK_MODULES([LIBPPD], [libppd])

# ======================
# Check system functions
# ======================
AC_CHECK_FUNCS(sigaction)
AC_CHECK_FUNCS(waitpid wait3)
AC_CHECK_FUNCS(strtoll)
AC_CHECK_FUNCS(open_memstream)
AC_CHECK_FUNCS(getline,[],AC_SUBST([GETLINE],['bannertopdf-getline.$(OBJEXT)']))
AC_CHECK_FUNCS(strcasestr,[],AC_SUBST([STRCASESTR],['pdftops-strcasestr.$(OBJEXT)']))
AC_SEARCH_LIBS(pow, m)
dnl Checks for string functions.
AC_CHECK_FUNCS(strdup strlcat strlcpy)
if test "$host_os_name" = "hp-ux" -a "$host_os_version" = "1020"; then
        echo Forcing snprintf emulation for HP-UX.
else
        AC_CHECK_FUNCS(snprintf vsnprintf)
fi

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
AC_CHECK_HEADER(string.h,AC_DEFINE(HAVE_STRING_H))
AC_CHECK_HEADER(strings.h,AC_DEFINE(HAVE_STRINGS_H))

# =======================
# Check for image filters
# =======================
AC_ARG_ENABLE([imagefilters],
	[AS_HELP_STRING([--disable-imagefilters], [Build the image filters.])],
	[enable_imagefilters="$enableval"],
	[enable_imagefilters=yes]
)
AM_CONDITIONAL([ENABLE_IMAGEFILTERS], [test "x$enable_imagefilters" != "xno"])

# =================
# Check for Poppler
# =================
AC_ARG_ENABLE(poppler, AS_HELP_STRING([--enable-poppler],[enable Poppler-based filters]),
	      enable_poppler=$enableval,enable_poppler=yes)
AM_CONDITIONAL(ENABLE_POPPLER, test x$enable_poppler = xyes)

# =====================
# Check for Ghostscript
# =====================
AC_ARG_ENABLE([ghostscript],
	[AS_HELP_STRING([--disable-ghostscript], [Disable filters using Ghostscript.])],
	[enable_ghostscript="$enableval"],
	[enable_ghostscript=yes]
)
AC_ARG_WITH([gs-path],
	[AS_HELP_STRING([--with-gs-path=value], [Set path to ghostcript binary (default: system).])],
	[with_gs_path="$withval"],
	[with_gs_path=system]
)
CUPS_GHOSTSCRIPT=""
AS_IF([test "x$enable_ghostscript" != "xyes"], [
       with_gs_path=""
], [
	AS_IF([test "x$with_gs_path" != "xsystem"], [
		CUPS_GHOSTSCRIPT="$with_gs_path"
	], [
		AS_IF([test "x$cross_compiling" = "xyes"], [
			CUPS_GHOSTSCRIPT="gs"
		], [
			AC_CHECK_PROG(CUPS_GHOSTSCRIPT, gs, gs)
		])
	])
	AS_IF([test "x$CUPS_GHOSTSCRIPT" = "x"], [
		AC_MSG_ERROR([Required gs binary is missing. Please install ghostscript package.])
	])
	AC_DEFINE([HAVE_GHOSTSCRIPT], [], [Define that we provide ghostscript binary])
])
AM_CONDITIONAL(ENABLE_GHOSTSCRIPT, test "x$enable_ghostscript" = xyes)
AC_SUBST(CUPS_GHOSTSCRIPT)
AC_DEFINE_UNQUOTED([CUPS_GHOSTSCRIPT], "$CUPS_GHOSTSCRIPT", [gs binary to use])

# ================
# Check for Mutool
# ================
AC_ARG_ENABLE([mutool],
	[AS_HELP_STRING([--disable-mutool], [Disable filters using mutool.])],
	[enable_mutool="$enableval"],
	[enable_mutool=yes]
)

AC_ARG_WITH([mutool-path],
	[AS_HELP_STRING([--with-mutool-path=value], [Set path to mutool binary (default: system).])],
	[with_mutool_path="$withval"],
	[with_mutool_path=system]
)

CUPS_MUTOOL=""
AS_IF([test "x$enable_mutool" != "xyes"], [
	with_mutool_path=""
], [
	AS_IF([test "x$with_mutool_path" != "xsystem"], [
		CUPS_MUTOOL="$with_mutool_path"
	], [
		AS_IF([test "x$cross_compiling" = "xyes"], [
			CUPS_MUTOOL="mutool"
		], [
			AC_CHECK_PROG(CUPS_MUTOOL, mutool, mutool)
		])
	])
	AS_IF([test "x$CUPS_MUTOOL" = "x"], [
		AC_MSG_ERROR([Required mutool binary is missing. Please install mutool.])
	])
	AC_DEFINE([HAVE_MUTOOL], [], [Define that we provide mutool binary])
])
AM_CONDITIONAL(ENABLE_MUTOOL, test "x$enable_mutool" = xyes)
AC_SUBST(CUPS_MUTOOL)
AC_DEFINE_UNQUOTED([CUPS_MUTOOL],"$CUPS_MUTOOL",[mutool binary to use])

# ==============================
# Check for "driverless" utility
# ==============================

AC_ARG_ENABLE([driverless], [AS_HELP_STRING([--enable-driverless], [enable PPD generator for driverless printing in /usr/lib/cups/driver/, for manual setup of driverless printers with printer setup tool.])],
        [enable_driverless="$enableval"],
        [enable_driverless=yes]
)
AM_CONDITIONAL([ENABLE_DRIVERLESS],
[test "x$enable_driverless" != "xno"])

dnl ippfind
AC_ARG_WITH([ippfind-path],
        [AS_HELP_STRING([--with-ippfind-path=value], [Set path to ippfind binary (default: system).])],
        [with_ippfind_path="$withval"],
        [with_ippfind_path=system]
)
AS_IF([test "x$with_ippfind_path" != "xsystem"], [
	CUPS_IPPFIND="$with_ippfind_path"
], [
	CUPS_IPPFIND="ippfind"
])
AC_DEFINE_UNQUOTED([CUPS_IPPFIND], "$CUPS_IPPFIND", [ippfind binary to use.])


dnl Avahi
AVAHI_LIBS=""
AVAHI_CFLAGS=""

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

if test "x$enable_avahi" != xno; then
	PKG_CHECK_MODULES(AVAHI, avahi-client,
		[AC_DEFINE(HAVE_AVAHI, [], [Define if you have the avahi library])])
fi

AC_SUBST(AVAHI_LIBS)
AC_SUBST(AVAHI_CFLAGS)

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

# ==================
# Check for foomatic
# ==================
AC_ARG_ENABLE([foomatic],
	[AS_HELP_STRING([--disable-foomatic], [Disable Foomatic-based filters.])],
	[enable_foomatic="$enableval"],
	[enable_foomatic=yes]
)
AM_CONDITIONAL([ENABLE_FOOMATIC], [test "x$enable_foomatic" = "xyes"])

# ================
# Check for pstops
# ================
AC_ARG_ENABLE([pstops],
	[AS_HELP_STRING([--enable-pstops], [Enable our own pstops filter.])],
	[enable_pstops="$enableval"],
	[enable_pstops=no]
)
AM_CONDITIONAL([ENABLE_PSTOPS], [test "x$enable_pstops" = "xyes"])

# =====================
# Check for rastertopwg
# =====================
AC_ARG_ENABLE([rastertopwg],
	[AS_HELP_STRING([--enable-rastertopwg], [Enable our own rastertopwg filter.])],
	[enable_rastertopwg="$enableval"],
	[enable_rastertopwg=no]
)
AM_CONDITIONAL([ENABLE_RASTERTOPWG], [test "x$enable_rastertopwg" = "xyes"])

# ===============================
# Check for universal CUPS filter
# ===============================
AC_ARG_ENABLE([universal-cups-filter],
	[AS_HELP_STRING([--enable-universal-cups-filter], [Use a single, universal CUPS filter executable for all filter functions.])],
	[enable_universal_cups_filter="$enableval"],
	[enable_universal_cups_filter=yes]
)
AS_IF([test "x$CUPS_GHOSTSCRIPT" == "x" -a "x$CUPS_PDFTOPS" == "x"], [
	enable_universal_cups_filter=no
])
AM_CONDITIONAL([ENABLE_UNIVERSAL_CUPS_FILTER],
[test "x$enable_universal_cups_filter" != "xno"])

# =================================
# Check for individual CUPS filters
# =================================
AC_ARG_ENABLE([individual-cups-filters],
	[AS_HELP_STRING([--enable-individual-cups-filters], [Use individual CUPS filter executables for each filter function.])],
        [enable_individual_cups_filters="$enableval"],
        [enable_individual_cups_filters=no]
)
AM_CONDITIONAL([ENABLE_INDIVIDUAL_CUPS_FILTERS],
[test "x$enable_individual_cups_filters" != "xno"])

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
	CFLAGS="$CFLAGS -Wall"
])
CFLAGS="$CFLAGS -D_GNU_SOURCE"

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
	Makefile
	filter/foomatic-rip/foomatic-rip.1
])
AC_OUTPUT

# ==============================================
# Display final informations about configuration
# ==============================================
AC_MSG_NOTICE([
==============================================================================
Environment settings:
	CFLAGS:                                    ${CFLAGS}
	LDFLAGS:                                   ${LDFLAGS}
Build configuration:
	cups-config:     ${with_cups_config}
	foomatic:        ${enable_foomatic}
	poppler:         ${enable_poppler}
	ghostscript:     ${enable_ghostscript}
	gs-path:         ${with_gs_path}
	mutool:          ${enable_mutool}
	mutool-path:     ${with_mutool_path}
	ippfind-path:    ${with_ippfind_path}
	imagefilters:    ${enable_imagefilters}
	pstops:          ${enable_pstops}
	rastertopwg:     ${enable_rastertopwg}
	shell:           ${with_shell}
	avahi:           ${enable_avahi}
	universal CUPS filter: ${enable_universal_cups_filter}
	individual CUPS filters: ${enable_individual_cups_filters}
	driverless:      ${enable_driverless}
	werror:          ${enable_werror}
==============================================================================
])

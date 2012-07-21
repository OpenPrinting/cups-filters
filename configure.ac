# Process this file with autoconf to create configure.

AC_PREREQ([2.65])

# ====================
# Version informations
# ====================
m4_define([cups_filters_version_major],[1])
m4_define([cups_filters_version_minor],[0])
m4_define([cups_filters_version_micro],[20])
m4_define([cups_filters_version],[cups_filters_version_major.cups_filters_version_minor.cups_filters_version_micro])

# =============
# Automake init
# =============
AC_INIT([cups-filters],[cups_filters_version])
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/ac_define_dir.m4])
m4_include([m4/ax_compare_version.m4])
AM_INIT_AUTOMAKE([1.11 gnu dist-xz dist-bzip2])
AM_SILENT_RULES([yes])
AC_LANG([C++])
AM_CONFIG_HEADER([config.h])
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
	[AS_HELP_STRING([--with-fontdir=path], [Specify path to font config directory ( default: fonts/conf.d/).])],
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
	DLOPEN_LIBS="-ldl",
	AC_MSG_ERROR([unable to find the dlopen() function])
)
AC_SUBST(DLOPEN_LIBS)

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
AM_CONDITIONAL([BUILD_LIBJPEG], [test x"$with_jpeg" = "xyes"])
AH_TEMPLATE([ENABLE_LIBJPEG], [Use libjpeg instead of builtin jpeg decoder.])

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

# ================
# Poppler features
# ================
POPPLER_INCLUDEDIR=`pkg-config poppler --variable includedir`
AC_CHECK_HEADER([UGooString.h], [AC_DEFINE([HAVE_UGOOSTRING_H],,[Have UGooString.h])], [])
AC_CHECK_HEADER([poppler/cpp/poppler-version.h], [AC_DEFINE([HAVE_CPP_POPPLER_VERSION_H],,[Define if you have Poppler's "cpp/poppler-version.h" header file.])], [])

AC_TRY_COMPILE([#include <Stream.h>],
	[virtual Stream *s = getUndecodedStream()],
	[AC_DEFINE([HAVE_GETUNDECODEDSTREAM],,[Define if you have Stream::getUndecodedStream().])],
	[])
AC_TRY_COMPILE([#include <CharCodeToUnicode.h>],
	[CharCode c; Unicode **u; int i = mapToUnicode(c, u)],
	[AC_DEFINE([OLD_MAPTOUNICODE],,[Define if you have Old CharCodeToUnicode::mapToUnicode()])],
	[])

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
AC_ARG_WITH([pdftops],
	[AS_HELP_STRING([--with-pdftops=value], [Set which pdftops to use (gs,pdftops).])],
	[with_pdftops="$withval"],
	[with_pdftops=gs]
)
AS_CASE([x$with_pdftops],
        [xgs|xpdftops], [],
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
])

AS_IF([test "x$with_pdftops_path" != "xsystem"], [
	CUPS_PDFTOPS="$with_pdftops"
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
])

AC_DEFINE_UNQUOTED([CUPS_GHOSTSCRIPT], "$CUPS_GHOSTSCRIPT", [gs binary to use])
AC_DEFINE_UNQUOTED([CUPS_POPPLER_PDFTOPS], "$CUPS_PDFTOPS", [pdftops binary to use.])
AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_MAX_RESOLUTION], [$CUPS_PDFTOPS_MAXRES], [max resolution used for pdftops when converting images])

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
	[AS_HELP_STRING([--with-test-font-path=value], [Set path to pdftops/ghostscript binary (default: /usr/share/fonts/dejavu/DejaVuSans.ttf).])],
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

# =====================
# Prepare all .in files
# =====================
AC_CONFIG_FILES([
	libcupsfilters.pc
	libfontembed.pc
	Makefile
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
	gs-path:         ${with_gs_path}
	imagefilters:    ${enable_imagefilters}
	jpeg:            ${with_jpeg}
	pdftops:         ${with_pdftops}
	pdftops-path:    ${with_pdftops_path}
	png:             ${with_png}
	php:             ${with_php}
	php-config:      ${with_php_config}
	test-font:       ${with_test_font_path}
	tiff:            ${with_tiff}
	werror:          ${enable_werror}
==============================================================================
])

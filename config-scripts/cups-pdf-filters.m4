dnl Do we have CUPS 1.4 or newer?
if test "`echo $CUPS_VERSION | cut -d '.' -f 2`" -ge "4"; then
   AC_DEFINE(CUPS_1_4, 1, [CUPS Version is 1.4 or newer])
fi

dnl @synopsis AC_DEFINE_DIR(VARNAME, DIR [, DESCRIPTION])
dnl
dnl This macro sets VARNAME to the expansion of the DIR variable,
dnl taking care of fixing up ${prefix} and such.
dnl
dnl VARNAME is then offered as both an output variable and a C
dnl preprocessor symbol.
dnl
dnl Example:
dnl
dnl    AC_DEFINE_DIR([DATADIR], [datadir], [Where data are placed to.])
dnl
dnl @category Misc
dnl @author Stepan Kasal <kasal@ucw.cz>
dnl @author Andreas Schwab <schwab@suse.de>
dnl @author Guido Draheim <guidod@gmx.de>
dnl @author Alexandre Oliva
dnl @version 2005-07-29
dnl @license AllPermissive

AC_DEFUN([AC_DEFINE_DIR], [
  prefix_NONE=
  exec_prefix_NONE=
  test "x$prefix" = xNONE && prefix_NONE=yes && prefix=$ac_default_prefix
  test "x$exec_prefix" = xNONE && exec_prefix_NONE=yes && exec_prefix=$prefix
dnl In Autoconf 2.60, ${datadir} refers to ${datarootdir}, which in turn
dnl refers to ${prefix}.  Thus we have to use `eval' twice.
  eval ac_define_dir="\"[$]$2\""
  eval ac_define_dir="\"$ac_define_dir\""
  AC_SUBST($1, "$ac_define_dir")
  AC_DEFINE_UNQUOTED($1, "$ac_define_dir", [$3])
  test "$prefix_NONE" && prefix=NONE
  test "$exec_prefix_NONE" && exec_prefix=NONE
])

dnl General checks

AC_HEADER_DIRENT
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([sys/stat.h])
AC_CHECK_HEADERS([sys/types.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADERS([zlib.h])

dnl Template directory for the bannertopdf filter

AC_DEFINE_DIR(BANNERTOPDF_DATADIR, "{CUPS_DATADIR}/data", [Directory where bannertopdf finds its data files (PDF templates)])

dnl Needed for pdftopdf filter compilation

CXXFLAGS="-DPDFTOPDF $CXXFLAGS"

dnl   Color Management

AC_ARG_ENABLE(cms,
	      AC_HELP_STRING([--disable-cms],
	                     [Don't use color management system.]),
              enable_cms=$enableval,
              enable_cms="try")
if test x$enable_cms = xyes; then
  PKG_CHECK_MODULES(LCMS, lcms2, [lcms2=yes], [lcms2=no])
  if test x$lcms2 = xno; then
      PKG_CHECK_MODULES(LCMS, lcms)
  fi
elif test x$enable_cms = xtry; then
  PKG_CHECK_MODULES(LCMS, lcms2,[lcms2=yes],[lcms2=no])
  if test x$lcms2 = xyes; then
    enable_cms=yes
  else
      PKG_CHECK_MODULES(LCMS, lcms,[enable_cms=yes],[enable_cms=no])
  fi
fi

if test "x$enable_cms" = "xyes"; then
  LCMS_LIBS=-llcms2
  if test "x$lcms2" = "xno"; then
    lcms1=yes;
    AC_DEFINE(USE_LCMS1, 1, [Defines if use lcms1])
    LCMS_LIBS=-llcms
  fi
fi
AC_SUBST(LCMS_LIBS)

dnl Switch over to C++.
AC_LANG(C++)

dnl check poppler
AC_CHECK_LIB(poppler,main,
  [ POPPLER_LIBS=-lpoppler],
  [ echo "*** poppler library not found. ***";exit ]
)
AC_SUBST(POPPLER_LIBS)

dnl poppler source dir
AC_ARG_WITH([poppler-source],[  --with-poppler-source=PATH      poppler source directory path],
  [POPPLER_SRCDIR=$withval],
  [POPPLER_SRCDIR=`eval echo $includedir`])
AC_SUBST(POPPLER_SRCDIR)
if test $POPPLER_SRCDIR = `eval echo $includedir`;then
  POPPLER_VERSION=`pkg-config --modversion poppler`
  CPPFLAGS="$CPPFLAGS -I$POPPLER_SRCDIR/poppler"
else
  POPPLER_LIBS=$POPPLER_SRCDIR/poppler/.libs/libpoppler.so
  CPPFLAGS="$CPPFLAGS -I$POPPLER_SRCDIR/poppler -I$POPPLER_SRCDIR"
  POPPLER_VERSION=`PKG_CONFIG_PATH=$POPPLER_SRCDIR pkg-config --modversion poppler`
fi

dnl poppler version
POPPLER_VERSION_MAJOR=`echo $POPPLER_VERSION|awk -F. '{print $1;}'`
AC_DEFINE_UNQUOTED(POPPLER_VERSION_MAJOR,$POPPLER_VERSION_MAJOR)
POPPLER_VERSION_MINOR=`echo $POPPLER_VERSION|awk -F. '{print $2;}'`
AC_DEFINE_UNQUOTED(POPPLER_VERSION_MINOR,$POPPLER_VERSION_MINOR)
POPPLER_VERSION_MICRO=`echo $POPPLER_VERSION|awk -F. '{print $3;}'`
AC_DEFINE_UNQUOTED(POPPLER_VERSION_MICRO,$POPPLER_VERSION_MICRO)

dnl Check for freetype headers
FREETYPE_LIBS=
FREETYPE_CFLAGS=

PKG_CHECK_MODULES(FREETYPE, freetype2,
                  [freetype_pkgconfig=yes], [freetype_pkgconfig=no])

if test "x$freetype_pkgconfig" = "xyes"; then

  AC_DEFINE(HAVE_FREETYPE_H, 1, [Have FreeType2 include files])

else

  AC_PATH_PROG(FREETYPE_CONFIG, freetype-config, no)
  if test "x$FREETYPE_CONFIG" != "xno" ; then

    FREETYPE_CFLAGS=`$FREETYPE_CONFIG --cflags`
    FREETYPE_LIBS=`$FREETYPE_CONFIG --libs`
    AC_DEFINE(HAVE_FREETYPE_H, 1, [Have FreeType2 include files])

  fi

fi

AC_SUBST(FREETYPE_CFLAGS)
AC_SUBST(FREETYPE_LIBS)

dnl check SplashFontEngin::SplashFontEngin interface
if test $POPPLER_SRCDIR = `eval echo $includedir`;then
    SPLASH_HEADER_DIR=$POPPLER_SRCDIR/poppler/splash
else
    SPLASH_HEADER_DIR=$POPPLER_SRCDIR/splash
fi
if grep "enableSlightHinting" $SPLASH_HEADER_DIR/SplashFontEngine.h >/dev/null ;then
    AC_DEFINE([SPLASH_SLIGHT_HINTING],,[SplashFontEngine enableSlightHinting])
fi

dnl check if GlobalParams::GlobalParams has a argument
if grep "GlobalParams(char \*cfgFileName)" $POPPLER_SRCDIR/poppler/GlobalParams.h >/dev/null ;then
    AC_DEFINE([GLOBALPARAMS_HAS_A_ARG],,[GlobalParams::GlobalParams has a argument.])
fi

dnl check if Parser:Parser has two arguments
if grep "Parser(XRef \*xrefA, Lexer \*lexerA)" $POPPLER_SRCDIR/poppler/Parser.h >/dev/null ;then
    AC_DEFINE([PARSER_HAS_2_ARGS],,[Parser::Parser has two arguments.])
fi

dnl check font type enumeration
if grep "fontType1COT" $POPPLER_SRCDIR/poppler/GfxFont.h >/dev/null ;then
    AC_DEFINE([FONTTYPE_ENUM2],,[New font type enumeration])
fi

dnl check Stream::getUndecodedStream
if grep "getUndecodedStream" $POPPLER_SRCDIR/poppler/Stream.h >/dev/null ;then
    AC_DEFINE([HAVE_GETUNDECODEDSTREAM],,[Have Stream::getUndecodedStream])
fi

dnl check UGooString.h
AC_CHECK_HEADER(UGooString.h,
    AC_DEFINE([HAVE_UGOOSTRING_H],,[Have UGooString.h])
,)

dnl check CharCodeToUnicode::mapToUnicode interface
if grep "mapToUnicode(.*Unicode[ ][ ]*\*u" $POPPLER_SRCDIR/poppler/CharCodeToUnicode.h >/dev/null ;then
    AC_DEFINE([OLD_MAPTOUNICODE],,[Old CharCodeToUnicode::mapToUnicode])
fi

dnl check GfxColorSpace::parse interface
if grep "GfxColorSpace *\*parse(Object *\*csObj)" $POPPLER_SRCDIR/poppler/GfxState.h >/dev/null ;then
    AC_DEFINE([OLD_CS_PARSE],,[Old GfxColorSpace::parse])
fi

dnl check new GfxFontType
if grep "fontType1C0T" $POPPLER_SRCDIR/poppler/GfxFont.h >/dev/null ;then
    AC_DEFINE([HAVE_NEW_GFX_FONTTYPE],,[have new GfxFontType])
fi

dnl check if cms is available
if grep "setDisplayProfileName" $POPPLER_SRCDIR/poppler/GfxState.h >/dev/null ;then
   if test "x$enable_cms" = "xyes"; then
       AC_DEFINE([USE_CMS],,[cms is available])
       POPPLER_LIBS="$POPPLER_LIBS $LCMS_LIBS"
   fi
fi

AC_DEFINE_DIR(POPPLER_DATADIR, "{datarootdir}/poppler", [Poppler data dir])

dnl Switch back to C.
AC_LANG(C)

dnl check ijs
AC_CHECK_LIB(ijs,main,
  [ IJS_LIBS=-lijs],
  [ echo "*** ijs library not found. ***";exit ]
)
AC_SUBST(IJS_LIBS)

dnl Test whether pdftoopvp should use the system's zlib
AC_ARG_ENABLE([zlib],
  [AS_HELP_STRING([--enable-zlib],[Build with zlib])],
  [],[enable_zlib="no"])
if test x$enable_zlib = xyes; then
  AC_CHECK_LIB([z], [inflate],,
               AC_MSG_ERROR("*** zlib library not found ***"))
  AC_CHECK_HEADERS([zlib.h],,
                   AC_MSG_ERROR("*** zlib headers not found ***"))
elif test x$enable_zlib = xtry; then
  AC_CHECK_LIB([z], [inflate],
               [enable_zlib="yes"],
               [enable_zlib="no"])
  AC_CHECK_HEADERS([zlib.h],,
                   [enable_zlib="no"])
fi

if test x$enable_zlib = xyes; then
  ZLIB_LIBS="-lz"
  AC_SUBST(ZLIB_LIBS)
  AC_DEFINE(ENABLE_ZLIB)
fi

AM_CONDITIONAL(BUILD_ZLIB, test x$enable_zlib = xyes)
AH_TEMPLATE([ENABLE_ZLIB],
            [Use zlib instead of builtin zlib decoder.])

dnl Test whether pdftoopvp should use the system's libjpeg
AC_ARG_ENABLE(libjpeg,
              AC_HELP_STRING([--disable-libjpeg],
                             [Don't build against libjpeg.]),
              enable_libjpeg=$enableval,
              enable_libjpeg="try")
if test x$enable_libjpeg != xno; then
  POPPLER_FIND_JPEG
fi

AM_CONDITIONAL(BUILD_LIBJPEG, test x$enable_libjpeg = xyes)
AH_TEMPLATE([ENABLE_LIBJPEG],
            [Use libjpeg instead of builtin jpeg decoder.])

PKG_CHECK_MODULES(FONTCONFIG, fontconfig >= 2.0.0)


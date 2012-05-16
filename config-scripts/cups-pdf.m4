dnl
dnl "$Id$"
dnl
dnl   PDF filter configuration stuff for OpenPrinting CUPS Filters.
dnl
dnl   Copyright 2007-2011 by Apple Inc.
dnl   Copyright 2006 by Easy Software Products, all rights reserved.
dnl
dnl   These coded instructions, statements, and computer programs are the
dnl   property of Apple Inc. and are protected by Federal copyright
dnl   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
dnl   which should have been included with this file.  If this file is
dnl   file is missing or damaged, see the license at "http://www.cups.org/".
dnl

AC_ARG_WITH(pdftops-renderer, [  --with-pdftops-renderer set default renderer for pdftops filter (gs,pdftops), default=gs ])
AC_ARG_WITH(pdftops-maxres, [  --with-pdftops-maxres   set default maximum image rendering resolution for pdftops filter (0, 75, 150, 300, 600, 1200, 2400, 4800, 90, 180, 360, 720, 1440, 2880, 5760, unlimited), default=1440 ])
AC_ARG_WITH(ghostscript, [  --with-ghostscript      set ghostscript path for pdftops filter (gs,/path/to/gs,none), default=gs ])
AC_ARG_WITH(poppler-pdftops, [  --with-poppler-pdftops  set Poppler pdftops path for pdftops filter (pdftops,/path/to/pdftops,none), default=pdftops ])

CUPS_PDFTOPS_RENDERER=""
CUPS_PDFTOPS_MAX_RESOLUTION=""
CUPS_POPPLER_PDFTOPS=""
CUPS_GHOSTSCRIPT=""

case "x$with_ghostscript" in
	x) # Default/auto
	AC_PATH_PROG(CUPS_GHOSTSCRIPT, gs)
	if test "x$CUPS_GHOSTSCRIPT" != x; then
		AC_DEFINE(HAVE_GHOSTSCRIPT)
	fi
	;;

	xgs)
	AC_PATH_PROG(CUPS_GHOSTSCRIPT, gs)
	if test "x$CUPS_GHOSTSCRIPT" != x; then
		AC_DEFINE(HAVE_GHOSTSCRIPT)
	else
		AC_MSG_WARN(Unable to find gs program!)
	fi
	;;

	x/*/gs) # Use /path/to/gs without any check:
	CUPS_GHOSTSCRIPT="$with_ghostscript"
	AC_DEFINE(HAVE_GHOSTSCRIPT)
	;;

	xno) # --without-ghostscript
	;;

	*) # Invalid with_ghostscript value
	AC_MSG_ERROR(Invalid with-ghostscript value!)
	exit 1
	;;
esac

case "x$with_poppler_pdftops" in
	x) # Default/auto
	AC_PATH_PROG(CUPS_POPPLER_PDFTOPS, pdftops)
	if test "x$CUPS_POPPLER_PDFTOPS" != x; then
		AC_DEFINE(HAVE_POPPLER_PDFTOPS)
	fi
	;;

	xpdftops)
	AC_PATH_PROG(CUPS_POPPLER_PDFTOPS, pdftops)
	if test "x$CUPS_POPPLER_PDFTOPS" != x; then
		AC_DEFINE(HAVE_POPPLER_PDFTOPS)
	else
		AC_MSG_WARN(Unable to find Poppler pdftops program!)
	fi
	;;

	x/*/pdftops) # Use /path/to/pdftops without any check:
	CUPS_POPPLER_PDFTOPS="$with_poppler_pdftops"
	AC_DEFINE(HAVE_POPPLER_PDFTOPS)
	;;

	xno) # --without-poppler-pdftops
	;;

	*) # Invalid with_poppler-pdftops value
	echo "$with_poppler_pdftops"
	AC_MSG_ERROR(Invalid with-poppler-pdftops value!)
	exit 1
	;;
esac

if test "x$CUPS_POPPLER_PDFTOPS" != x; then
	AC_MSG_CHECKING(whether Poppler pdftops supports -origpagesizes)
	if ($CUPS_POPPLER_PDFTOPS -h 2>&1 | grep -q -- -origpagesizes); then
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POPPLER_PDFTOPS_WITH_ORIGPAGESIZES)
	else
		AC_MSG_RESULT(no)
	fi

	AC_MSG_CHECKING(whether Poppler pdftops supports -r)
	if ($CUPS_POPPLER_PDFTOPS -h 2>&1 | grep -q -- '-r '); then
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POPPLER_PDFTOPS_WITH_RESOLUTION)
	else
		AC_MSG_RESULT(no)
	fi
fi
if test "x$CUPS_GHOSTSCRIPT" != x; then
	AC_MSG_CHECKING(whether gs supports the ps2write device)
	if ($CUPS_GHOSTSCRIPT -h 2>&1 | grep -q ps2write); then
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GHOSTSCRIPT_PS2WRITE)
	else
		AC_MSG_RESULT(no)
	fi
fi

case "x$with_pdftops_renderer" in
	x|xgs) # gs
	CUPS_PDFTOPS_RENDERER=GS
	if test "x$CUPS_GHOSTSCRIPT" = x; then
		AC_MSG_WARN(Default renderer Ghostscript is not available!)
	fi
	;;

	xpdftops) # pdftops
	CUPS_PDFTOPS_RENDERER=PDFTOPS
	if test "x$CUPS_POPPLER_PDFTOPS" = x; then
		AC_MSG_WARN(Default renderer Poppler pdftops is not available!)
	fi
	;;

	*) # Invalid with_pdftops-renderer value
	AC_MSG_ERROR(Invalid with-pdftops-renderer value!)
	exit 1
	;;
esac

case "x$with_pdftops_maxres" in

        x)
	CUPS_PDFTOPS_MAXRES=1440
	;;

	x75|x150|x300|x600|x1200|x2400|x4800|x90|x180|x360|x720|x1440|x2880|x5760)
	CUPS_PDFTOPS_MAXRES=$with_pdftops_maxres
	;;

	x0|xunlimited|xno)
	CUPS_PDFTOPS_MAXRES=0
	;;

	*) # Invalid with_pdftops-renderer value
	AC_MSG_ERROR(Invalid with-pdftops-maxres value!)
	exit 1
	;;
esac

AC_DEFINE_UNQUOTED(CUPS_PDFTOPS_RENDERER, $CUPS_PDFTOPS_RENDERER)
AC_DEFINE_UNQUOTED(CUPS_PDFTOPS_MAX_RESOLUTION, $CUPS_PDFTOPS_MAXRES)
AC_DEFINE_UNQUOTED(CUPS_PDFTOPS, "$CUPS_PDFTOPS")
AC_DEFINE_UNQUOTED(CUPS_GHOSTSCRIPT, "$CUPS_GHOSTSCRIPT")

dnl
dnl End of "$Id$".
dnl

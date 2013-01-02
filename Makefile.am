ACLOCAL_AMFLAGS = -I m4

pkgconfdir = $(libdir)/pkgconfig
pkgconf_DATA = \
	libcupsfilters.pc \
	libfontembed.pc

doc_DATA = \
	AUTHORS \
	COPYING \
	NEWS \
	INSTALL \
	README

EXTRA_DIST = \
	$(doc_DATA) \
	autogen.sh \
	libcupsfilters.pc.in \
	libfontembed.pc.in

# ========
# Backends
# ========
pkgbackenddir = $(CUPS_SERVERBIN)/backend
pkgbackend_PROGRAMS = parallel serial

check_PROGRAMS = test1284
# We need ieee1284 up and running.
# Leave it to the user to run if they have the bus.
#TESTS = test1284

parallel_SOURCES = \
	backend/backend-private.h \
	backend/ieee1284.c \
	backend/parallel.c
parallel_LDADD = $(CUPS_LIBS)
parallel_CFLAGS = $(CUPS_CFLAGS)

serial_SOURCES = \
	backend/backend-private.h \
	backend/serial.c
serial_LDADD = $(CUPS_LIBS)
serial_CFLAGS = $(CUPS_CFLAGS)

test1284_SOURCES = \
	backend/backend-private.h \
	backend/ieee1284.c \
	backend/test1284.c
test1284_LDADD = $(CUPS_LIBS)
test1284_CFLAGS = $(CUPS_CFLAGS)

# =======
# Banners
# =======
pkgbannerdir = $(CUPS_DATADIR)/banners
pkgbanner_DATA = \
	banners/classified \
	banners/confidential \
	banners/secret \
	banners/standard \
	banners/topsecret \
	banners/unclassified

EXTRA_DIST += $(pkgbanner_DATA)

# ========
# Charsets
# ========
pkgcharsetdir = $(CUPS_DATADIR)/charsets
pkgcharset_DATA = \
	charset/pdf.utf-8.heavy \
	charset/pdf.utf-8.simple

EXTRA_DIST += $(pkgcharset_DATA)

# ====================
# CUPS Filters library
# ====================
pkgfiltersincludedir = $(includedir)/cupsfilters
pkgfiltersinclude_DATA = \
	cupsfilters/driver.h \
	cupsfilters/image.h

lib_LTLIBRARIES = libcupsfilters.la

check_PROGRAMS += \
	testcmyk \
	testdither \
	testimage \
	testrgb
TESTS = \
	testdither
#	testcmyk # fails as it opens some image.ppm which is nowerhe to be found.
#	testimage # requires also some ppm file as argument
#	testrgb # same error
# FIXME: run old testdither
#	./testdither > test/0-255.pgm 2>test/0-255.log
#	./testdither 0 127 255 > test/0-127-255.pgm 2>test/0-127-255.log
#	./testdither 0 85 170 255 > test/0-85-170-255.pgm 2>test/0-85-170-255.log
#	./testdither 0 63 127 170 198 227 255 > test/0-63-127-170-198-227-255.pgm 2>test/0-63-127-170-198-227-255.log
#	./testdither 0 210 383 > test/0-210-383.pgm 2>test/0-210-383.log
#	./testdither 0 82 255 > test/0-82-255.pgm 2>test/0-82-255.log
#	./testdither 0 510 > test/0-510.pgm 2>test/0-510.log
#	./testdither 0 1020 > test/0-1020.pgm 2>test/0-1020.log


libcupsfilters_la_SOURCES = \
	cupsfilters/attr.c \
	cupsfilters/check.c \
	cupsfilters/cmyk.c \
	cupsfilters/dither.c \
	cupsfilters/image.c \
	cupsfilters/image-bmp.c \
	cupsfilters/image-colorspace.c \
	cupsfilters/image-gif.c \
	cupsfilters/image-jpeg.c \
	cupsfilters/image-photocd.c \
	cupsfilters/image-pix.c \
	cupsfilters/image-png.c \
	cupsfilters/image-pnm.c \
	cupsfilters/image-private.h \
	cupsfilters/image-sgi.c \
	cupsfilters/image-sgi.h \
	cupsfilters/image-sgilib.c \
	cupsfilters/image-sun.c \
	cupsfilters/image-tiff.c \
	cupsfilters/image-zoom.c \
	cupsfilters/lut.c \
	cupsfilters/pack.c \
	cupsfilters/rgb.c \
	cupsfilters/srgb.c \
	$(pkgfiltersinclude_DATA)
libcupsfilters_la_LIBADD = \
	$(CUPS_LIBS) \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(TIFF_LIBS) \
	-lm
libcupsfilters_la_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(TIFF_CFLAGS)

libcupsfilters_la_LDFLAGS = \
	-no-undefined \
	-version-info 1

testcmyk_SOURCES = \
	cupsfilters/testcmyk.c \
	$(pkgfiltersinclude_DATA)
testcmyk_LDADD = \
	libcupsfilters.la \
	-lm

testdither_SOURCES = \
	cupsfilters/testdither.c \
	$(pkgfiltersinclude_DATA)
testdither_LDADD = \
	libcupsfilters.la \
	-lm

testimage_SOURCES = \
	cupsfilters/testimage.c \
	$(pkgfiltersinclude_DATA)
testimage_LDADD = \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(TIFF_LIBS) \
	libcupsfilters.la \
	-lm
testimage_CFLAGS = \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(TIFF_CFLAGS)

testrgb_SOURCES = \
	cupsfilters/testrgb.c \
	$(pkgfiltersinclude_DATA)
testrgb_LDADD = \
	libcupsfilters.la \
	-lm

EXTRA_DIST += \
	$(pkgfiltersinclude_DATA) \
	cupsfilters/image.pgm \
	cupsfilters/image.ppm

# =========
# CUPS Data
# =========
pkgcupsdatadir = $(CUPS_DATADIR)/data
pkgcupsdata_DATA = \
	data/default.pdf \
	data/default-testpage.pdf \
	data/testprint

EXTRA_DIST += $(pkgcupsdata_DATA)

# =======
# Drivers
# =======
pkgdriverdir = $(CUPS_DATADIR)/drv
pkgdriver_DATA = \
	drv/cupsfilters.drv

EXTRA_DIST += $(pkgdriver_DATA)

# =====
# MIMEs
# =====
pkgmimedir = $(CUPS_DATADIR)/mime
pkgmime_DATA = \
	mime/cupsfilters.convs \
	mime/cupsfilters.types

EXTRA_DIST += $(pkgmime_DATA)

# =================
# Fontembed library
# =================
pkgfontembedincludedir = $(includedir)/fontembed
pkgfontembedinclude_DATA = \
        fontembed/bitset.h \
        fontembed/embed.h \
        fontembed/fontfile.h \
        fontembed/iofn.h \
        fontembed/sfnt.h

lib_LTLIBRARIES += libfontembed.la

bin_PROGRAMS = ttfread

check_PROGRAMS += \
	test_analyze \
	test_pdf \
	test_ps
TESTS += \
	test_analyze \
	test_pdf \
	test_ps

libfontembed_la_SOURCES = \
	fontembed/aglfn13.c \
	fontembed/bitset.h \
	fontembed/dynstring.c \
	fontembed/dynstring.h \
	fontembed/embed.c \
	fontembed/embed.h \
	fontembed/embed_sfnt.c \
	fontembed/embed_sfnt_int.h \
	fontembed/embed_pdf.c \
	fontembed/embed_pdf.h \
	fontembed/embed_pdf_int.h \
	fontembed/fontfile.c \
	fontembed/fontfile.h \
	fontembed/frequent.c \
	fontembed/frequent.h \
	fontembed/iofn.h \
	fontembed/macroman.h \
	fontembed/sfnt.c \
	fontembed/sfnt.h \
	fontembed/sfnt_int.h \
	fontembed/sfnt_subset.c
libfontembed_la_LDFLAGS = \
	-no-undefined \
	-version-info 1

ttfread_SOURCES = fontembed/main.c
ttfread_LDADD = libfontembed.la

test_analyze_SOURCES = fontembed/test_analyze.c
test_analyze_LDADD = libfontembed.la

test_pdf_SOURCES = fontembed/test_pdf.c
test_pdf_LDADD = libfontembed.la

test_ps_SOURCES = fontembed/test_ps.c
test_ps_LDADD = libfontembed.la

EXTRA_DIST += \
	$(pkgfontembedinclude_DATA) \
	fontembed/README

# ===========
# PDF to OPVP
# ===========
pkgfilterdir = $(CUPS_SERVERBIN)/filter
pkgfilter_PROGRAMS = pdftoopvp

pkgfontconfigdir = $(sysconfdir)/$(FONTDIR)
pkgfontconfig_DATA = filter/pdftoopvp/99pdftoopvp.conf

pdftoopvp_SOURCES = \
	filter/pdftoopvp/oprs/OPRS.cxx \
	filter/pdftoopvp/oprs/OPRS.h \
	filter/pdftoopvp/oprs/OPVPSplashClip.cxx \
	filter/pdftoopvp/oprs/OPVPSplashClip.h \
	filter/pdftoopvp/oprs/OPVPSplash.cxx \
	filter/pdftoopvp/oprs/OPVPSplash.h \
	filter/pdftoopvp/oprs/OPVPSplashPath.cxx \
	filter/pdftoopvp/oprs/OPVPSplashPath.h \
	filter/pdftoopvp/oprs/OPVPSplashState.cxx \
	filter/pdftoopvp/oprs/OPVPSplashState.h \
	filter/pdftoopvp/oprs/OPVPSplashXPath.cxx \
	filter/pdftoopvp/oprs/OPVPSplashXPath.h \
	filter/pdftoopvp/oprs/OPVPWrapper.cxx \
	filter/pdftoopvp/oprs/OPVPWrapper.h \
	filter/pdftoopvp/oprs/OPVPWrapper_0_2.cxx \
	filter/pdftoopvp/oprs/OPVPWrapper_0_2.h \
	filter/pdftoopvp/OPVPError.h \
	filter/pdftoopvp/opvp/opvp_common.h \
	filter/pdftoopvp/opvp/opvp.h \
	filter/pdftoopvp/opvp/opvp_0_2_0.h \
	filter/pdftoopvp/OPVPOutputDev.cxx \
	filter/pdftoopvp/OPVPOutputDev.h \
	filter/pdftoopvp/pdftoopvp.cxx
pdftoopvp_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(FREETYPE_CFLAGS) \
	$(FONTCONFIG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(POPPLER_CFLAGS) \
	-I$(srcdir)/filter/pdftoopvp/oprs \
	-I$(srcdir)/filter/pdftoopvp/opvp
pdftoopvp_CXXFLAGS = $(pdftoopvp_CFLAGS)
pdftoopvp_LDADD = \
	$(CUPS_LIBS) \
	$(FREETYPE_LIBS) \
	$(FONTCONFIG_LIBS) \
	$(LIBPNG_LIBS) \
	$(POPPLER_LIBS) \
	$(DLOPEN_LIBS)

EXTRA_DIST += $(pkgfontconfig_DATA)

# ==========
# PDF to PDF 
# ==========
pkgfilter_PROGRAMS += pdftopdf

pdftopdf_SOURCES = \
	filter/pdftopdf/pdftopdf.cc \
	filter/pdftopdf/pdftopdf_jcl.cc \
	filter/pdftopdf/pdftopdf_jcl.h \
	filter/pdftopdf/pdftopdf_processor.cc \
	filter/pdftopdf/pdftopdf_processor.h \
	filter/pdftopdf/qpdf_pdftopdf_processor.cc \
	filter/pdftopdf/qpdf_pdftopdf_processor.h \
	filter/pdftopdf/pptypes.cc \
	filter/pdftopdf/pptypes.h \
	filter/pdftopdf/nup.cc \
	filter/pdftopdf/nup.h \
	filter/pdftopdf/intervalset.cc \
	filter/pdftopdf/intervalset.h \
	filter/pdftopdf/qpdf_tools.cc \
	filter/pdftopdf/qpdf_tools.h \
	filter/pdftopdf/qpdf_xobject.cc \
	filter/pdftopdf/qpdf_xobject.h \
	filter/pdftopdf/qpdf_pdftopdf.cc \
	filter/pdftopdf/qpdf_pdftopdf.h \
	filter/pdftopdf/qpdf_cm.cc \
	filter/pdftopdf/qpdf_cm.h
pdftopdf_CFLAGS = \
	$(LIBQPDF_CFLAGS) \
	$(CUPS_CFLAGS)
pdftopdf_CXXFLAGS = -std=c++0x $(pdftopdf_CFLAGS)   # -std=c++11
pdftopdf_LDADD = \
	$(LIBQPDF_LIBS) \
	$(CUPS_LIBS)

# ======================
# Simple filter binaries
# ======================
pkgfilter_SCRIPTS = \
	filter/imagetops \
	filter/pstopdf \
	filter/textonly \
	filter/texttops
pkgfilter_PROGRAMS += \
	bannertopdf \
	commandtoescpx \
	commandtopclx \
	pdftoijs \
	pdftops \
	pdftoraster \
	rastertoescpx \
	rastertopclx \
	texttopdf \
	urftopdf
if ENABLE_IMAGEFILTERS
pkgfilter_PROGRAMS += \
	imagetopdf \
	imagetoraster
endif

check_PROGRAMS += \
	test_pdf1 \
	test_pdf2

TESTS += \
	test_pdf1 \
	test_pdf2

bannertopdf_SOURCES = \
	filter/banner.c \
	filter/banner.h \
	filter/bannertopdf.c \
	filter/pdf.cxx \
	filter/pdf.h
EXTRA_bannertopdf_SOURCES = filter/getline.c
bannertopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(POPPLER_CFLAGS) \
	$(TIFF_CFLAGS)
bannertopdf_CXXFLAGS = $(bannertopdf_CFLAGS)
bannertopdf_LDADD = \
	$(GETLINE) \
	$(CUPS_LIBS) \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(POPPLER_LIBS) \
	$(TIFF_LIBS)
bannertopdf_DEPENDENCIES = $(GETLINE)

commandtoescpx_SOURCES = \
	cupsfilters/driver.h \
	filter/commandtoescpx.c \
	filter/pcl.h
commandtoescpx_CFLAGS= \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
commandtoescpx_LDADD = $(CUPS_LIBS)

commandtopclx_SOURCES = \
	cupsfilters/driver.h \
	filter/commandtopclx.c \
	filter/pcl.h
commandtopclx_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
commandtopclx_LDADD = $(CUPS_LIBS)

imagetopdf_SOURCES = \
	cupsfilters/image.h \
	filter/common.c \
	filter/common.h \
	filter/imagetopdf.c
imagetopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(TIFF_CFLAGS) \
	-I$(srcdir)/cupsfilters/
imagetopdf_LDADD = \
	$(CUPS_LIBS) \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(TIFF_LIBS) \
	libcupsfilters.la

imagetoraster_SOURCES = \
	cupsfilters/image.h \
	cupsfilters/image-private.h \
	filter/common.c \
	filter/common.h \
	filter/imagetoraster.c
imagetoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
imagetoraster_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

urftopdf_SOURCES = \
	filter/urftopdf.cpp \
	filter/unirast.h
urftopdf_CXXFLAGS = \
	$(LIBQPDF_CFLAGS)
urftopdf_LDADD = \
	$(LIBQPDF_LIBS)

pdftoijs_SOURCES = \
	filter/pdftoijs.cxx \
	filter/PDFError.h
pdftoijs_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(IJS_CFLAGS) \
	$(POPPLER_CFLAGS)
pdftoijs_CXXFLAGS = $(pdftoijs_CFLAGS)
pdftoijs_LDADD = \
	$(CUPS_LIBS) \
	$(IJS_LIBS) \
	$(POPPLER_LIBS)

pdftops_SOURCES = \
	filter/common.c \
	filter/common.h \
	filter/pdftops.c
EXTRA_pdftops_SOURCES = filter/strcasestr.c
pdftops_CFLAGS = $(CUPS_CFLAGS)
pdftops_LDADD = $(STRCASESTR) $(CUPS_LIBS)
pdftops_DEPENDENCIES = $(STRCASESTR)

pdftoraster_SOURCES = \
	filter/pdftoraster.cxx \
	filter/PDFError.h
pdftoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LCMS_CFLAGS) \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(POPPLER_CFLAGS) \
	$(TIFF_CFLAGS) \
	-I$(srcdir)/cupsfilters/
pdftoraster_CXXFLAGS = $(pdftoraster_CFLAGS)
pdftoraster_LDADD = \
	$(CUPS_LIBS) \
	$(LCMS_LIBS) \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(POPPLER_LIBS) \
	$(TIFF_LIBS) \
	libcupsfilters.la

rastertoescpx_SOURCES = \
	cupsfilters/driver.h \
	filter/escp.h \
	filter/rastertoescpx.c
rastertoescpx_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertoescpx_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

rastertopclx_SOURCES = \
	cupsfilters/driver.h \
	filter/pcl.h \
	filter/pcl-common.c \
	filter/pcl-common.h \
	filter/rastertopclx.c
rastertopclx_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertopclx_LDADD = \
	$(CUPS_LIBS) \
	$(LIBPNG_LIBS) \
	libcupsfilters.la

test_pdf1_SOURCES = \
	filter/pdfutils.c \
	filter/pdfutils.h \
	filter/test_pdf1.c \
	fontembed/embed.h \
	fontembed/sfnt.h
test_pdf1_CFLAGS = -I$(srcdir)/fontembed/
test_pdf1_LDADD = libfontembed.la

test_pdf2_SOURCES = \
	filter/pdfutils.c \
	filter/pdfutils.h \
	filter/test_pdf2.c \
	fontembed/embed.h \
	fontembed/sfnt.h
test_pdf2_CFLAGS = -I$(srcdir)/fontembed/
test_pdf2_LDADD = libfontembed.la

texttopdf_SOURCES = \
	filter/common.c \
	filter/common.h \
	filter/pdfutils.c \
	filter/pdfutils.h \
	filter/textcommon.c \
	filter/textcommon.h \
	filter/texttopdf.c \
	fontembed/bitset.h \
	fontembed/embed.h \
	fontembed/fontfile.h \
	fontembed/iofn.h \
	fontembed/sfnt.h
texttopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(FONTCONFIG_CFLAGS) \
	-I$(srcdir)/fontembed/
texttopdf_LDADD = \
	$(CUPS_LIBS) \
	$(FONTCONFIG_LIBS) \
	libfontembed.la

# Not reliable bash script
#TESTS += filter/test.sh

EXTRA_DIST += \
	$(pkgfilter_SCRIPTS) \
	filter/test.sh

# =====
# UTILS
# =====

EXTRA_DIST += utils/cups-browsed.in
sbin_PROGRAMS =
if ENABLE_AVAHI
sbin_PROGRAMS += \
	cups-browsed
cups_browsed_SOURCES = \
	utils/cups-browsed.c
cups_browsed_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(AVAHI_CFLAGS)
cups_browsed_CXXFLAGS = $(cups_browsed_CFLAGS)
cups_browsed_LDADD = \
	$(CUPS_LIBS) \
	$(AVAHI_LIBS)
initrcdir = $(INITDDIR)
initrc_SCRIPTS = utils/cups-browsed
endif

# ===
# PPD
# ===
ppddir = $(datadir)/ppd/cupsfilters
ppd_DATA = \
	ppd/Generic-PDF_Printer-PDF.ppd \
	ppd/HP-Color_LaserJet_CM3530_MFP-PDF.ppd \
	ppd/HP-PhotoSmart_Pro_B8300-hpijs-pdftoijs.ppd \
	ppd/textonly.ppd

EXTRA_DIST += $(ppd_DATA)

# =========
# Scripting
# =========
if WITH_PHP
phpextensiondir = $(PHPDIR)
phpextension_LTLIBRARIES = libphpcups.la
libphpcups_la_SOURCES = \
	scripting/php/phpcups.c \
	scripting/php/phpcups.h
libphpcups_la_LIBADD = $(CUPS_LIBS)
libphpcups_la_CFLAGS = $(CUPS_CFLAGS)
libphpcups_la_LDFLAGS = -no-undefined
endif # WITH_PHP

EXTRA_DIST += \
	scripting/perl \
	scripting/php/README \
	scripting/php/phpcups.php

distclean-local:
	rm -rf *.cache *~

install-data-hook:
if ENABLE_AVAHI
if RCLINKS
	for level in $(RCLEVELS); do \
		$(INSTALL) -d -m 755 $(DESTDIR)$(INITDIR)/rc$${level}.d; \
		$(LN_S) ../init.d/cups-browsed $(DESTDIR)$(INITDIR)/rc$${level}.d/S$(RCSTART)cups-browsed; \
		$(LN_S) ../init.d/cups-browsed $(DESTDIR)$(INITDIR)/rc$${level}.d/K$(RCSTOP)cups-browsed; \
	done; \
	$(INSTALL) -d -m 755 $(DESTDIR)$(INITDIR)/rc0.d; \
	$(LN_S) ../init.d/cups-browsed $(DESTDIR)$(INITDIR)/rc0.d/K$(RCSTOP)cups-browsed;
endif
endif
	$(LN_S) pdf.utf-8.simple \
		$(DESTDIR)$(pkgcharsetdir)/pdf.utf-8


uninstall-hook:
if ENABLE_AVAHI
if RCLINKS
	if test "x$(INITDIR)" != x; then \
		$(RM) $(DESTDIR)$(BUILDROOT)$(INITDIR)/rc?.d/[SK]??cups-browsed || :; \
		rmdir $(DESTDIR)$(BUILDROOT)$(INITDIR)/rc?.d || :;\
	fi
endif
endif
	$(RM) $(DESTDIR)$(pkgcharsetdir)/pdf.utf-8

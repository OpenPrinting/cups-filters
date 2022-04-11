ACLOCAL_AMFLAGS = -I m4

pkgconfdir = $(libdir)/pkgconfig
pkgconf_DATA = \
	libcupsfilters.pc \
	libppd.pc \
	libfontembed.pc

doc_DATA = \
	ABOUT-NLS \
	AUTHORS \
	COPYING \
	NEWS \
	INSTALL \
	README

EXTRA_DIST = \
	$(doc_DATA) \
	autogen.sh \
	config.rpath \
	ln-srf \
	libcupsfilters.pc.in \
	libppd.pc.in \
	libfontembed.pc.in \
	utils/cups-browsed.service \
	utils/cups-browsed-upstart.conf \
	utils/driverless-fax.in \
	filter/braille/drivers/index/ubrlto4dot.c \
	filter/braille/filters/TODO.txt

EXTRA_DIST += \
	cupsfilters/testdriver.c \
	data/makePDFfromPS.sh \
	data/classified.ps \
	data/confidential.ps \
	data/secret.ps \
	data/standard.ps \
	data/topsecret.ps \
	data/unclassified.ps \
	data/banner-instr-append.txt \
	data/form-instr-append.txt \
	data/testprint-instr-append.txt \
	drv/custom-media-lines

# =========
# utilities
# =========

pkgutilsdir = $(bindir)
pkgutils_PROGRAMS =

# ========
# Backends
# ========
pkgbackenddir = $(CUPS_SERVERBIN)/backend
pkgbackend_PROGRAMS = parallel serial beh implicitclass

parallel_SOURCES = \
	backend/parallel.c
parallel_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS)
parallel_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

serial_SOURCES = \
	backend/serial.c
serial_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
serial_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

beh_SOURCES = \
	backend/beh.c
beh_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
beh_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

implicitclass_SOURCES = \
	backend/implicitclass.c
implicitclass_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS)
implicitclass_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

if ENABLE_BRAILLE
pkgbackend_PROGRAMS += cups-brf
endif

cups_brf_SOURCES = \
	backend/cups-brf.c

# ==============
# PPD Generators
# ==============
utils/driverless-fax: utils/driverless-fax.in Makefile
	sed \
	  -e "s|\@CUPS_SERVERBIN\@|$(CUPS_SERVERBIN)|" \
	  $< > $@

pkgppdgendir = $(CUPS_SERVERBIN)/driver
driverlessfaxscripts = \
	utils/driverless-fax
if ENABLE_DRIVERLESS
pkgppdgen_PROGRAMS = driverless
pkgppdgen_SCRIPTS = \
	$(driverlessfaxscripts)
endif

driverless_SOURCES = \
	utils/driverless.c
driverless_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)
driverless_CXXFLAGS = $(driverless_CFLAGS)
driverless_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS)


# =======
# Banners
# =======
pkgbannerdir = $(CUPS_DATADIR)/banners
pkgbanner_DATA =

bannerfiles = \
	banners/classified \
	banners/confidential \
	banners/secret \
	banners/standard \
	banners/form \
	banners/topsecret \
	banners/unclassified

pkgbanner_DATA += $(bannerfiles)

EXTRA_DIST += $(bannerfiles)

# ========
# Charsets
# ========
pkgcharsetdir = $(CUPS_DATADIR)/charsets
pkgcharset_DATA = \
	charset/pdf.utf-8.heavy \
	charset/pdf.utf-8.simple

EXTRA_DIST += $(pkgcharset_DATA)

# ==========================
# PPD legacy support library
# ==========================
pkgppdincludedir = $(includedir)/ppd
pkgppdinclude_DATA = \
	ppd/ppd.h \
	ppd/ppdc.h \
	cupsfilters/log.h

pkgppddefsdir = $(datadir)/ppdc
pkgppddefs_DATA = \
	ppd/epson.h \
	ppd/hp.h \
	ppd/label.h \
	ppd/font.defs \
	ppd/media.defs \
	ppd/raster.defs

lib_LTLIBRARIES = libppd.la

check_PROGRAMS = \
	testppd
TESTS = \
	testppd

libppd_la_SOURCES = \
	ppd/ppd-attr.c \
	ppd/ppd.c \
	ppd/ppd-cache.c \
	ppd/ppd-collection.cxx \
	ppd/ppd-conflicts.c \
	ppd/ppd-custom.c \
	ppd/ppd-emit.c \
	ppd/ppd-localize.c \
	ppd/ppd-mark.c \
	ppd/ppd-page.c \
	ppd/ppd-ipp.c \
	ppd/array.c \
	ppd/array-private.h \
	ppd/debug.c \
	ppd/debug-internal.h \
	ppd/debug-private.h \
	ppd/encode.c \
	ppd/file.c \
	ppd/file-private.h \
	ppd/ipp-private.h \
	ppd/language.c \
	ppd/language-private.h \
	ppd/raster-interpret.c \
	ppd/raster-error.c \
	ppd/raster-private.h \
	ppd/string.c \
	ppd/snprintf.c \
	ppd/string-private.h \
	ppd/thread.c \
	ppd/thread-private.h \
	ppd/ppdc-array.cxx \
	ppd/ppdc-attr.cxx \
	ppd/ppdc-catalog.cxx \
	ppd/ppdc-choice.cxx \
	ppd/ppdc-constraint.cxx \
	ppd/ppdc-driver.cxx \
	ppd/ppdc-file.cxx \
	ppd/ppdc-filter.cxx \
	ppd/ppdc-font.cxx \
	ppd/ppdc-group.cxx \
	ppd/ppdc-import.cxx \
	ppd/ppdc-mediasize.cxx \
	ppd/ppdc-message.cxx \
	ppd/ppdc-option.cxx \
	ppd/ppdc-private.h \
	ppd/ppdc-profile.cxx \
	ppd/ppdc-shared.cxx \
	ppd/ppdc-source.cxx \
	ppd/ppdc-string.cxx \
	ppd/ppdc-variable.cxx \
	$(pkgppdinclude_DATA) \
	$(pkgppddefs_DATA)
libppd_la_LIBADD = \
	$(CUPS_LIBS)
libppd_la_CFLAGS = \
	$(CUPS_CFLAGS)
libppd_la_LDFLAGS = \
	-no-undefined \
	-version-info 1

testppd_SOURCES = ppd/testppd.c
testppd_LDADD = libppd.la \
		-lcups

EXTRA_DIST += \
	$(pkgppdinclude_DATA) \
	$(pkgppddefs_DATA) \
	ppd/test.ppd \
	ppd/test2.ppd \
	ppd/README.md

# ===========================
# ppdc PPD compiler utilities
# ===========================

if ENABLE_PPDC_UTILS
pkgutils_PROGRAMS += \
	genstrings \
	ppdc \
	ppdhtml \
	ppdi \
	ppdmerge \
	ppdpo
endif

genstrings_SOURCES = \
	ppd/genstrings.cxx
genstrings_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
genstrings_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdc_SOURCES = \
	ppd/ppdc.cxx
ppdc_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdc_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdhtml_SOURCES = \
	ppd/ppdhtml.cxx
ppdhtml_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdhtml_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdi_SOURCES = \
	ppd/ppdi.cxx
ppdi_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdi_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdmerge_SOURCES = \
	ppd/ppdmerge.cxx
ppdmerge_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdmerge_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdpo_SOURCES = \
	ppd/ppdpo.cxx
ppdpo_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdpo_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

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

test_analyze_SOURCES = fontembed/test_analyze.c
test_analyze_LDADD = libfontembed.la

test_pdf_SOURCES = fontembed/test_pdf.c
test_pdf_LDADD = libfontembed.la

test_ps_SOURCES = fontembed/test_ps.c
test_ps_LDADD = libfontembed.la

EXTRA_DIST += \
	$(pkgfontembedinclude_DATA) \
	fontembed/README

pkgfilter_PROGRAMS =
pkgfilterdir = $(CUPS_SERVERBIN)/filter

# ====================
# CUPS Filters library
# ====================
pkgfiltersincludedir = $(includedir)/cupsfilters
pkgfiltersinclude_DATA = \
	cupsfilters/bitmap.h \
	cupsfilters/colord.h \
	cupsfilters/colormanager.h \
	cupsfilters/driver.h \
	cupsfilters/filter.h \
	cupsfilters/ieee1284.h \
	cupsfilters/image.h \
	cupsfilters/ipp.h \
	cupsfilters/log.h \
	cupsfilters/pdf.h \
	cupsfilters/pdfutils.h \
	cupsfilters/ppdgenerator.h \
	cupsfilters/raster.h

lib_LTLIBRARIES += libcupsfilters.la

check_PROGRAMS += \
	testcmyk \
	testdither \
	testimage \
	testrgb \
	test1284
TESTS += \
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

# We need ieee1284 up and running.
# Leave it to the user to run if they have the bus.
#TESTS += test1284

libcupsfilters_la_SOURCES = \
	cupsfilters/attr.c \
	cupsfilters/bannertopdf.c \
	cupsfilters/bitmap.c \
	cupsfilters/check.c \
	cupsfilters/cmyk.c \
	cupsfilters/colord.c \
	cupsfilters/colormanager.c \
	cupsfilters/dither.c \
	cupsfilters/filter.c \
	cupsfilters/ghostscript.c \
	cupsfilters/ieee1284.c \
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
	cupsfilters/image-sun.c \
	cupsfilters/image-tiff.c \
	cupsfilters/image-zoom.c \
	cupsfilters/imagetopdf.c \
	cupsfilters/imagetoraster.c \
	cupsfilters/ipp.c \
	cupsfilters/lut.c \
	cupsfilters/mupdftopwg.c \
	cupsfilters/pack.c \
	cupsfilters/pclmtoraster.cxx \
	cupsfilters/pdf.cxx \
	cupsfilters/pdftopdf/pdftopdf.cxx \
	cupsfilters/pdftopdf/pdftopdf-private.h \
	cupsfilters/pdftopdf/pdftopdf-jcl.cxx \
	cupsfilters/pdftopdf/pdftopdf-jcl-private.h \
	cupsfilters/pdftopdf/pdftopdf-processor.cxx \
	cupsfilters/pdftopdf/pdftopdf-processor-private.h \
	cupsfilters/pdftopdf/qpdf-pdftopdf-processor.cxx \
	cupsfilters/pdftopdf/qpdf-pdftopdf-processor-private.h \
	cupsfilters/pdftopdf/pptypes.cxx \
	cupsfilters/pdftopdf/pptypes-private.h \
	cupsfilters/pdftopdf/nup.cxx \
	cupsfilters/pdftopdf/nup-private.h \
	cupsfilters/pdftopdf/intervalset.cxx \
	cupsfilters/pdftopdf/intervalset-private.h \
	cupsfilters/pdftopdf/qpdf-tools.cxx \
	cupsfilters/pdftopdf/qpdf-tools-private.h \
	cupsfilters/pdftopdf/qpdf-xobject.cxx \
	cupsfilters/pdftopdf/qpdf-xobject-private.h \
	cupsfilters/pdftopdf/qpdf-pdftopdf.cxx \
	cupsfilters/pdftopdf/qpdf-pdftopdf-private.h \
	cupsfilters/pdftopdf/qpdf-cm.cxx \
	cupsfilters/pdftopdf/qpdf-cm-private.h \
	cupsfilters/pdftops.c \
	cupsfilters/pdftoraster.cxx \
	cupsfilters/pdfutils.c \
	cupsfilters/ppdgenerator.c \
	cupsfilters/imagetops-pstops.c \
	cupsfilters/pwgtoraster.c \
	cupsfilters/raster.c \
	cupsfilters/rastertops.c \
	cupsfilters/rastertopdf.cxx \
	cupsfilters/rastertopwg.c \
	cupsfilters/rgb.c \
	cupsfilters/srgb.c \
	cupsfilters/texttopdf.c \
	cupsfilters/texttotext.c \
	cupsfilters/universal.c \
	$(pkgfiltersinclude_DATA)
EXTRA_libcupsfilters_la_SOURCES = \
	cupsfilters/getline.c \
	cupsfilters/strcasestr.c
libcupsfilters_la_LIBADD = \
	libppd.la \
	libfontembed.la \
	$(FONTCONFIG_LIBS) \
	$(GETLINE) \
	$(STRCASESTR) \
	$(CUPS_LIBS) \
	$(LCMS_LIBS) \
	$(LIBQPDF_LIBS) \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(TIFF_LIBS) \
	$(POPPLER_LIBS) \
	-lm
libcupsfilters_la_CFLAGS = \
	-I$(srcdir)/ppd/ \
	-I$(srcdir)/fontembed/ \
	$(FONTCONFIG_CFLAGS) \
	$(CUPS_CFLAGS) \
	$(LCMS_CFLAGS) \
	$(LIBQPDF_CFLAGS) \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(TIFF_CFLAGS)
libcupsfilters_la_LDFLAGS = \
	-no-undefined \
	-version-info 2
if BUILD_DBUS
libcupsfilters_la_CFLAGS += $(DBUS_CFLAGS) -DHAVE_DBUS
libcupsfilters_CXXFLAGS = -std=c++0x $(libcupsfilters_CFLAGS)   # -std=c++11
libcupsfilters_la_LIBADD += $(DBUS_LIBS)
endif
libcupsfilters_la_DEPENDENCIES = \
	libppd.la \
	libfontembed.la \
	$(GETLINE) \
	$(STRCASESTR)

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

test1284_SOURCES = \
	cupsfilters/test1284.c
test1284_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS)
test1284_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

# =========
# CUPS Data
# =========
pkgcupsdatadir = $(CUPS_DATADIR)/data
pkgcupsdata_DATA = \
	data/default.pdf \
	data/form_russian.pdf \
	data/form_english.pdf \
	data/form_english_in.odt \
	data/form_russian_in.odt \
	data/default-testpage.pdf \
	data/testprint \
	data/classified.pdf \
	data/confidential.pdf \
	data/secret.pdf \
	data/standard.pdf \
	data/topsecret.pdf \
	data/unclassified.pdf

EXTRA_DIST += $(pkgcupsdata_DATA)

# ===========
# CUPS Config
# ===========
pkgcupsserverrootdir = $(CUPS_SERVERROOT)
pkgcupsserverroot_DATA = \
	utils/cups-browsed.conf

# =======
# Drivers
# =======
pkgdriverdir = $(CUPS_DATADIR)/drv

gendrvfiles = \
	drv/cupsfilters.drv
pkgdriver_DATA = $(gendrvfiles)

brldrvfiles = \
	drv/generic-brf.drv \
	drv/generic-ubrl.drv \
	drv/indexv3.drv \
	drv/indexv4.drv
if ENABLE_BRAILLE
pkgdriver_DATA += $(brldrvfiles)
endif

EXTRA_DIST += $(gendrvfiles) $(brldrvfiles)

# =======
# Definitions for drivers
# =======
pkgppdcdir = $(CUPS_DATADIR)/ppdc

genppdcfiles = \
	filter/pcl.h \
	filter/escp.h
pkgppdc_DATA = $(genppdcfiles)

GENERATED_LIBLOUIS = \
	filter/braille/filters/liblouis3.defs \
	filter/braille/filters/liblouis4.defs
GENERATED_DEFS = \
	filter/braille/filters/liblouis1.defs \
	filter/braille/filters/liblouis2.defs \
	$(GENERATED_LIBLOUIS)

filter/braille/filters/liblouis1.defs: filter/braille/filters/liblouis1.defs.gen
	$< > $@

filter/braille/filters/liblouis2.defs: filter/braille/filters/liblouis1.defs
	sed -e "s/Braille transcription/Additional Braille transcription (2)/" \
	    -e "s/^  \\*Choice /  Choice /" \
	    -e "s/^  Choice \"HyphLocale\//  *Choice \"HyphLocale\//" \
	    -e s/LibLouis/LibLouis2/ \
	    < $< > $@

$(GENERATED_LIBLOUIS): filter/braille/filters/liblouis%.defs: filter/braille/filters/liblouis1.defs
	sed -e "s/Braille transcription/Additional Braille transcription ($*)/" \
	    -e "s/^  \\*Choice /  Choice /" \
	    -e "s/^  Choice \"None\//  *Choice \"None\//" \
	    -e s/LibLouis/LibLouis$*/ \
	    < $< > $@

brlppdcfiles = \
	filter/braille/drivers/common/media-braille.defs \
	filter/braille/drivers/index/index.defs \
	filter/braille/filters/braille.defs \
	filter/braille/filters/imagemagick.defs \
	filter/braille/filters/liblouis.defs \
	filter/braille/drivers/common/fr-braille.po

if ENABLE_BRAILLE
pkgppdc_DATA += $(brlppdcfiles)
nodist_pkgppdc_DATA = \
	$(GENERATED_DEFS)
endif

EXTRA_DIST += \
	filter/braille/filters/liblouis1.defs.gen.in \
	$(genppdcfiles) \
	$(brlppdcfiles)

# =====
# MIMEs
# =====
pkgmimedir = $(CUPS_DATADIR)/mime

genmimefiles = \
	mime/cupsfilters.types \
	mime/cupsfilters.convs
pkgmime_DATA = \
	$(genmimefiles)

universalmimefiles = \
	mime/cupsfilters-universal.convs
individualmimefiles = \
	mime/cupsfilters-individual.convs
popplermimefiles = \
	mime/cupsfilters-poppler.convs
gsmimefiles = \
	mime/cupsfilters-ghostscript.convs
mutoolmimefiles = \
	mime/cupsfilters-mupdf.convs

if ENABLE_UNIVERSAL_CUPS_FILTER
if ENABLE_POPPLER
if ENABLE_GHOSTSCRIPT
pkgmime_DATA += $(universalmimefiles)
endif
endif
endif

if ENABLE_INDIVIDUAL_CUPS_FILTERS
pkgmime_DATA += $(individualmimefiles)
if ENABLE_POPPLER
pkgmime_DATA += $(popplermimefiles)
endif
if ENABLE_GHOSTSCRIPT
pkgmime_DATA += $(gsmimefiles)
endif
if ENABLE_MUTOOL
pkgmime_DATA += $(mutoolmimefiles)
endif
endif

brlmimefiles = \
	mime/braille.convs \
	mime/braille.types
if ENABLE_BRAILLE
pkgmime_DATA += $(brlmimefiles)
endif

EXTRA_DIST += \
	$(genmimefiles) \
	$(universalmimefiles) \
	$(individualmimefiles) \
	$(popplermimefiles) \
	$(gsmimefiles) \
	$(mutoolmimefiles) \
	$(brlmimefiles)

# =================
# Braille aux files
# =================
if ENABLE_BRAILLE
pkgbrailledir = $(CUPS_DATADIR)/braille
nodist_pkgbraille_SCRIPTS = \
	filter/braille/drivers/index/indexv4.sh \
	filter/braille/drivers/index/indexv3.sh \
	filter/braille/drivers/index/index.sh \
	filter/braille/filters/cups-braille.sh
endif

# ==========
# PDF to PDF 
# ==========
pkgfilter_PROGRAMS += pdftopdf

pdftopdf_SOURCES = \
	filter/pdftopdf.c
pdftopdf_CFLAGS = \
	-I$(srcdir)/ppd/ \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
pdftopdf_LDADD = \
	libppd.la \
	libcupsfilters.la \
	$(CUPS_LIBS)

# ======================
# Simple filter binaries
# ======================
genfilterscripts = \
	filter/texttops
pkgfilter_SCRIPTS = $(genfilterscripts)
if ENABLE_BRAILLE
nodist_pkgfilter_SCRIPTS = \
	filter/braille/drivers/generic/brftoembosser \
	filter/braille/drivers/index/imageubrltoindexv3 \
	filter/braille/drivers/index/imageubrltoindexv4 \
	filter/braille/drivers/index/textbrftoindexv3 \
	filter/braille/filters/imagetobrf \
	filter/braille/filters/vectortopdf \
	filter/braille/filters/vectortobrf \
	filter/braille/filters/texttobrf \
	filter/braille/filters/brftopagedbrf \
	filter/braille/filters/musicxmltobrf
endif

pkgfilter_PROGRAMS += \
	commandtoescpx \
	commandtopclx \
	texttotext \
	rastertoescpx \
	rastertopclx
if ENABLE_GHOSTSCRIPT
pkgfilter_PROGRAMS += \
	gstopxl
endif
if ENABLE_FOOMATIC
pkgfilter_PROGRAMS += \
	foomatic-rip
endif
if ENABLE_UNIVERSAL_CUPS_FILTER
if ENABLE_POPPLER
if ENABLE_GHOSTSCRIPT
pkgfilter_PROGRAMS += \
	universal
endif
endif
endif
pkgfilter_PROGRAMS += \
	pdftops \
	texttopdf \
	rastertopclm \
	rastertopdf \
	bannertopdf \
	rastertops \
	pwgtoraster \
	pclmtoraster
if ENABLE_RASTERTOPWG
pkgfilter_PROGRAMS += \
	rastertopwg
endif
if ENABLE_PSTOPS
pkgfilter_PROGRAMS += \
	pstops
endif
if ENABLE_POPPLER
pkgfilter_PROGRAMS += \
	pdftoraster
endif
if ENABLE_GHOSTSCRIPT
pkgfilter_PROGRAMS += \
	gstoraster \
	gstopdf
endif
if ENABLE_MUTOOL
pkgfilter_PROGRAMS += \
	mupdftopwg
endif
if ENABLE_IMAGEFILTERS
pkgfilter_PROGRAMS += \
	imagetopdf \
	imagetops \
	imagetoraster
endif

check_PROGRAMS += \
	test_pdf1 \
	test_pdf2

TESTS += \
	test_pdf1 \
	test_pdf2

# Not reliable bash script
#TESTS += filter/test.sh

EXTRA_DIST += \
	$(genfilterscripts) \
	filter/test.sh

bannertopdf_SOURCES = \
	filter/bannertopdf.c
bannertopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
bannertopdf_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

commandtoescpx_SOURCES = \
	cupsfilters/driver.h \
	filter/commandtoescpx.c \
	filter/pcl.h
commandtoescpx_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)
commandtoescpx_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS)

commandtopclx_SOURCES = \
	cupsfilters/driver.h \
	filter/commandtopclx.c \
	filter/pcl.h
commandtopclx_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)
commandtopclx_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS)

foomatic_rip_SOURCES = \
	filter/foomatic-rip/foomaticrip.c \
	filter/foomatic-rip/foomaticrip.h \
	filter/foomatic-rip/options.c \
	filter/foomatic-rip/options.h \
	filter/foomatic-rip/pdf.c \
	filter/foomatic-rip/pdf.h \
	filter/foomatic-rip/postscript.c \
	filter/foomatic-rip/postscript.h \
	filter/foomatic-rip/process.c \
	filter/foomatic-rip/process.h \
	filter/foomatic-rip/renderer.c \
	filter/foomatic-rip/renderer.h \
	filter/foomatic-rip/spooler.c \
	filter/foomatic-rip/spooler.h \
	filter/foomatic-rip/util.c \
	filter/foomatic-rip/util.h \
	cupsfilters/colord.h
foomatic_rip_CFLAGS = \
	-DCONFIG_PATH='"$(sysconfdir)/foomatic"' \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
foomatic_rip_LDADD = \
	$(CUPS_LIBS) \
	-lm \
	libcupsfilters.la

gstoraster_SOURCES = \
	filter/gstoraster.c
gstoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
gstoraster_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

gstopdf_SOURCES = \
	filter/gstopdf.c
gstopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
gstopdf_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

gstopxl_SOURCES = \
	filter/gstopxl.c
gstopxl_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
gstopxl_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

imagetopdf_SOURCES = \
	filter/imagetopdf.c
imagetopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
imagetopdf_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

imagetops_SOURCES = \
	filter/imagetops.c
imagetops_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
imagetops_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

imagetoraster_SOURCES = \
	filter/imagetoraster.c
imagetoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
imagetoraster_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

pclmtoraster_SOURCES = \
	filter/pclmtoraster.c
pclmtoraster_CXXFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
pclmtoraster_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

rastertopclm_SOURCES = \
	filter/rastertopclm.c
rastertopclm_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertopclm_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

rastertopdf_SOURCES = \
	filter/rastertopdf.c
rastertopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertopdf_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

mupdftopwg_SOURCES = \
	filter/mupdftopwg.c
mupdftopwg_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/
mupdftopwg_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la \
	libppd.la

pwgtoraster_SOURCES = \
	filter/pwgtoraster.c
pwgtoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
pwgtoraster_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

rastertops_SOURCES = \
	filter/rastertops.c
rastertops_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertops_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

rastertopwg_SOURCES = \
	filter/rastertopwg.c
rastertopwg_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertopwg_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

texttotext_SOURCES = \
	filter/texttotext.c
texttotext_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
texttotext_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

texttopdf_SOURCES = \
	filter/texttopdf.c
texttopdf_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
texttopdf_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

pdftops_SOURCES = \
	filter/pdftops.c
pdftops_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
pdftops_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

pstops_SOURCES = \
	filter/pstops.c
pstops_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
pstops_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

pdftoraster_SOURCES = \
	filter/pdftoraster.c
pdftoraster_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
pdftoraster_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

rastertoescpx_SOURCES = \
	cupsfilters/driver.h \
	filter/escp.h \
	filter/rastertoescpx.c
rastertoescpx_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/
rastertoescpx_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la \
	libppd.la

rastertopclx_SOURCES = \
	cupsfilters/driver.h \
	filter/pcl.h \
	filter/pcl-common.c \
	filter/pcl-common.h \
	filter/rastertopclx.c
rastertopclx_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/
rastertopclx_LDADD = \
	$(CUPS_LIBS) \
	$(LIBPNG_LIBS) \
	libcupsfilters.la \
	libppd.la

test_pdf1_SOURCES = \
	filter/test_pdf1.c \
	fontembed/embed.h \
	fontembed/sfnt.h
test_pdf1_CFLAGS = -I$(srcdir)/fontembed/ \
	-I$(srcdir)/cupsfilters/
test_pdf1_LDADD = libfontembed.la \
	libcupsfilters.la

test_pdf2_SOURCES = \
	filter/test_pdf2.c \
	fontembed/embed.h \
	fontembed/sfnt.h
test_pdf2_CFLAGS = -I$(srcdir)/fontembed/ \
	-I$(srcdir)/cupsfilters/
test_pdf2_LDADD = libfontembed.la \
	libcupsfilters.la

universal_SOURCES = \
	filter/universal.c
universal_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	$(CUPS_CFLAGS)
universal_LDADD = \
	libcupsfilters.la \
	$(CUPS_LIBS)

# =====
# UTILS
# =====

cups_notifier_sources = \
	cups-notifier.c \
	cups-notifier.h

$(cups_notifier_sources): utils/org.cups.cupsd.Notifier.xml
	gdbus-codegen \
	    --interface-prefix org.cups.cupsd \
	    --c-namespace Cups \
	    --generate-c-code cups-notifier \
	    utils/org.cups.cupsd.Notifier.xml

sbin_PROGRAMS = \
	cups-browsed
cups_browsed_SOURCES = \
	utils/cups-browsed.c
nodist_cups_browsed_SOURCES = \
	$(cups_notifier_sources)
cups_browsed_CFLAGS = \
	-I$(srcdir)/cupsfilters/ \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS) \
	$(AVAHI_CFLAGS) \
	$(AVAHI_GLIB_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
cups_browsed_CXXFLAGS = $(cups_browsed_CFLAGS)
cups_browsed_LDADD = \
	libcupsfilters.la \
	libppd.la \
	$(CUPS_LIBS) \
	$(AVAHI_LIBS) \
	$(AVAHI_GLIB_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)
initrcdir = $(INITDDIR)
initrc_SCRIPTS = utils/cups-browsed

cupsbrowsedmanpages = \
	utils/cups-browsed.8 \
	utils/cups-browsed.conf.5
man_MANS = $(cupsbrowsedmanpages)
driverlessmanpages = \
	utils/driverless.1
if ENABLE_DRIVERLESS
man_MANS += $(driverlessmanpages)
endif
foomaticmanpages = \
	filter/foomatic-rip/foomatic-rip.1
if ENABLE_FOOMATIC
man_MANS += $(foomaticmanpages)
endif
EXTRA_DIST += utils/cups-browsed.in \
	$(cupsbrowsedmanpages) \
	$(driverlessmanpages) \
	filter/foomatic-rip/foomatic-rip.1.in \
	utils/org.cups.cupsd.Notifier.xml
BUILT_SOURCES = $(cups_notifier_sources)
CLEANFILES = $(BUILT_SOURCES) $(GENERATED_DEFS)

# ===
# PPD
# ===
ppddir = $(datadir)/ppd/cupsfilters
genppdfiles = \
	ppdfiles/Fuji_Xerox-DocuPrint_CM305_df-PDF.ppd \
	ppdfiles/Generic-PDF_Printer-PDF.ppd \
	ppdfiles/HP-Color_LaserJet_CM3530_MFP-PDF.ppd \
	ppdfiles/Ricoh-PDF_Printer-PDF.ppd
ppd_DATA = $(genppdfiles)
gsppdfiles = \
	ppdfiles/pxlcolor.ppd \
	ppdfiles/pxlmono.ppd
if ENABLE_GHOSTSCRIPT
ppd_DATA += $(gsppdfiles)
endif

EXTRA_DIST += \
	$(genppdfiles) \
	$(gsppdfiles)

# =========
# Scripting
# =========
if WITH_PHP
phpextensiondir = $(PHPDIR)
phpextension_LTLIBRARIES = libphpcups.la
endif

libphpcups_la_SOURCES = \
	scripting/php/phpcups.c \
	scripting/php/phpcups.h
libphpcups_la_LIBADD = $(CUPS_LIBS)
libphpcups_la_CFLAGS = $(CUPS_CFLAGS)
libphpcups_la_LDFLAGS = -no-undefined

EXTRA_DIST += \
	scripting/perl \
	scripting/php/README \
	scripting/php/phpcups.php

distclean-local:
	rm -rf *.cache *~

install-exec-hook:
	$(INSTALL) -d -m 755 $(DESTDIR)$(bindir)
	$(INSTALL) -d -m 755 $(DESTDIR)$(pkgfilterdir)
	$(INSTALL) -d -m 755 $(DESTDIR)$(pkgbackenddir)
if ENABLE_FOOMATIC
	$(LN_SRF) $(DESTDIR)$(pkgfilterdir)/foomatic-rip $(DESTDIR)$(bindir)
endif
if ENABLE_DRIVERLESS
	$(LN_SRF) $(DESTDIR)$(pkgppdgendir)/driverless $(DESTDIR)$(bindir)
	$(LN_SRF) $(DESTDIR)$(pkgppdgendir)/driverless $(DESTDIR)$(pkgbackenddir)
	$(LN_SRF) $(DESTDIR)$(pkgppdgendir)/driverless-fax $(DESTDIR)$(bindir)
	$(LN_SRF) $(DESTDIR)$(pkgppdgendir)/driverless-fax $(DESTDIR)$(pkgbackenddir)
endif
if ENABLE_BRAILLE
	$(LN_S) -f imagetobrf $(DESTDIR)$(pkgfilterdir)/imagetoubrl
	$(LN_S) -f vectortopdf $(DESTDIR)$(pkgfilterdir)/svgtopdf
	$(LN_S) -f vectortopdf $(DESTDIR)$(pkgfilterdir)/xfigtopdf
	$(LN_S) -f vectortopdf $(DESTDIR)$(pkgfilterdir)/wmftopdf
	$(LN_S) -f vectortopdf $(DESTDIR)$(pkgfilterdir)/emftopdf
	$(LN_S) -f vectortopdf $(DESTDIR)$(pkgfilterdir)/cgmtopdf
	$(LN_S) -f vectortopdf $(DESTDIR)$(pkgfilterdir)/cmxtopdf
	$(LN_S) -f vectortobrf $(DESTDIR)$(pkgfilterdir)/vectortoubrl
	$(LN_S) -f textbrftoindexv3 $(DESTDIR)$(pkgfilterdir)/textbrftoindexv4
endif

install-data-hook:
if RCLINKS
	for level in $(RCLEVELS); do \
		$(INSTALL) -d -m 755 $(DESTDIR)$(INITDIR)/rc$${level}.d; \
		$(LN_S) -f ../init.d/cups-browsed $(DESTDIR)$(INITDIR)/rc$${level}.d/S$(RCSTART)cups-browsed; \
		$(LN_S) -f ../init.d/cups-browsed $(DESTDIR)$(INITDIR)/rc$${level}.d/K$(RCSTOP)cups-browsed; \
	done; \
	$(INSTALL) -d -m 755 $(DESTDIR)$(INITDIR)/rc0.d; \
	$(LN_S) -f ../init.d/cups-browsed $(DESTDIR)$(INITDIR)/rc0.d/K$(RCSTOP)cups-browsed;
endif
	$(LN_S) -f pdf.utf-8.simple \
		$(DESTDIR)$(pkgcharsetdir)/pdf.utf-8
	chmod 700 $(DESTDIR)/$(pkgbackenddir)/implicitclass
if ENABLE_BRAILLE
	chmod 700 $(DESTDIR)/$(pkgbackenddir)/cups-brf
endif


uninstall-hook:
if RCLINKS
	if test "x$(INITDIR)" != x; then \
		$(RM) $(DESTDIR)$(BUILDROOT)$(INITDIR)/rc?.d/[SK]??cups-browsed || :; \
		rmdir $(DESTDIR)$(BUILDROOT)$(INITDIR)/rc?.d || :;\
	fi
endif
	$(RM) $(DESTDIR)$(pkgcharsetdir)/pdf.utf-8
if ENABLE_FOOMATIC
	$(RM) $(DESTDIR)$(bindir)/foomatic-rip
endif
if ENABLE_DRIVERLESS
	$(RM) $(DESTDIR)$(bindir)/driverless
	$(RM) $(DESTDIR)$(pkgbackenddir)/driverless
	$(RM) $(DESTDIR)$(bindir)/driverless-fax
	$(RM) $(DESTDIR)$(pkgbackenddir)/driverless-fax
endif
if ENABLE_BRAILLE
	$(RM) $(DESTDIR)$(pkgfilterdir)/imagetoubrl
	$(RM) $(DESTDIR)$(pkgfilterdir)/svgtopdf
	$(RM) $(DESTDIR)$(pkgfilterdir)/xfigtopdf
	$(RM) $(DESTDIR)$(pkgfilterdir)/wmftopdf
	$(RM) $(DESTDIR)$(pkgfilterdir)/emftopdf
	$(RM) $(DESTDIR)$(pkgfilterdir)/cgmtopdf
	$(RM) $(DESTDIR)$(pkgfilterdir)/cmxtopdf
	$(RM) $(DESTDIR)$(pkgfilterdir)/vectortoubrl
	$(RM) $(DESTDIR)$(pkgfilterdir)/textbrftoindexv4
endif

SUBDIRS =

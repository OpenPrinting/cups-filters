ACLOCAL_AMFLAGS = -I m4

pkgconfdir = $(libdir)/pkgconfig
pkgconf_DATA = \
	libcupsfilters.pc \
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
	libcupsfilters.pc.in \
	libfontembed.pc.in \
	utils/cups-browsed.service \
	utils/cups-browsed-upstart.conf \
	filter/braille/drivers/index/ubrlto4dot.c \
	filter/braille/filters/TODO.txt

EXTRA_DIST += \
	cupsfilters/kmdevices.cpp \
	cupsfilters/kmdevices.h \
	cupsfilters/testdriver.c \
	data/makePDFfromPS.sh \
	data/classified.ps \
	data/confidential.ps \
	data/secret.ps \
	data/standard.ps \
	data/topsecret.ps \
	data/unclassified.ps \
	drv/custom-media-lines

# ========
# Backends
# ========
pkgbackenddir = $(CUPS_SERVERBIN)/backend
pkgbackend_PROGRAMS = parallel serial beh implicitclass

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

beh_SOURCES = \
	backend/backend-private.h \
	backend/beh.c
beh_LDADD = $(CUPS_LIBS)
beh_CFLAGS = $(CUPS_CFLAGS)

implicitclass_SOURCES = \
	backend/backend-private.h \
	backend/implicitclass.c
implicitclass_LDADD = $(CUPS_LIBS)\
	libcupsfilters.la
implicitclass_CFLAGS = $(CUPS_CFLAGS)\
	 -I$(srcdir)/cupsfilters/

test1284_SOURCES = \
	backend/backend-private.h \
	backend/ieee1284.c \
	backend/test1284.c
test1284_LDADD = $(CUPS_LIBS)
test1284_CFLAGS = $(CUPS_CFLAGS)

if ENABLE_BRAILLE
pkgbackend_PROGRAMS += cups-brf
endif

cups_brf_SOURCES = \
	backend/cups-brf.c

# ==============
# PPD Generators
# ==============
pkgppdgendir = $(CUPS_SERVERBIN)/driver

if ENABLE_DRIVERLESS
pkgppdgen_PROGRAMS = driverless
endif

driverless_SOURCES = \
	utils/driverless.c
driverless_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
driverless_CXXFLAGS = $(driverless_CFLAGS)
driverless_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

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

# ====================
# CUPS Filters library
# ====================
pkgfiltersincludedir = $(includedir)/cupsfilters
pkgfiltersinclude_DATA = \
	cupsfilters/colord.h \
	cupsfilters/colormanager.h \
	cupsfilters/driver.h \
	cupsfilters/image.h \
	cupsfilters/raster.h \
	cupsfilters/ppdgenerator.h \
	cupsfilters/pdftoippprinter.h

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
	cupsfilters/colord.c \
	cupsfilters/colormanager.c \
	cupsfilters/dither.c \
	cupsfilters/image.c \
	cupsfilters/pdftoippprinter.c \
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
	cupsfilters/ppdgenerator.c \
	cupsfilters/raster.c \
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
if BUILD_DBUS
libcupsfilters_la_CFLAGS += $(DBUS_CFLAGS) -DHAVE_DBUS
libcupsfilters_la_LIBADD += $(DBUS_LIBS)
endif

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
	mime/cupsfilters.types
pkgmime_DATA = \
	$(genmimefiles) \
	mime/cupsfilters.convs

popplermimefiles = \
	mime/cupsfilters-poppler.convs
if ENABLE_POPPLER
pkgmime_DATA += $(popplermimefiles)
endif
gsmimefiles = \
	mime/cupsfilters-ghostscript.convs
if ENABLE_GHOSTSCRIPT
pkgmime_DATA += $(gsmimefiles)
endif
mutoolmimefiles = \
	mime/cupsfilters-mupdf.convs
if ENABLE_MUTOOL
pkgmime_DATA += $(mutoolmimefiles)
endif

brlmimefiles = \
	mime/braille.convs \
	mime/braille.types
if ENABLE_BRAILLE
pkgmime_DATA += $(brlmimefiles)
endif

EXTRA_DIST += \
	$(genmimefiles) \
	$(popplermimefiles) \
	$(gsmimefiles) \
	$(mutoolmimefiles) \
	$(brlmimefiles) \
	mime/cupsfilters.convs.in

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

pkgfilter_PROGRAMS =
pkgfilterdir = $(CUPS_SERVERBIN)/filter

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
genfilterscripts = \
	filter/imagetops \
	filter/texttops  \
	filter/rastertopclm
pkgfilter_SCRIPTS = $(genfilterscripts)
gsfilterscripts = \
	filter/gstopxl \
	filter/gstopdf
if ENABLE_GHOSTSCRIPT
pkgfilter_SCRIPTS += $(gsfilterscripts)
endif
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
	sys5ippprinter \
	texttotext \
	pdftops \
	rastertoescpx \
	rastertopclx \
	texttopdf \
	rastertopdf \
	bannertopdf \
	rastertops
if ENABLE_URFTOPDF
pkgfilter_PROGRAMS += \
	urftopdf
endif
if ENABLE_POPPLER
pkgfilter_PROGRAMS += \
	pdftoraster
endif
if ENABLE_GHOSTSCRIPT
pkgfilter_PROGRAMS += \
	gstoraster
endif
if ENABLE_MUTOOL
pkgfilter_PROGRAMS += \
	mupdftoraster
endif
if ENABLE_FOOMATIC
pkgfilter_PROGRAMS += \
	foomatic-rip
endif
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

# Not reliable bash script
#TESTS += filter/test.sh

EXTRA_DIST += \
	$(genfilterscripts) \
	$(gsfilterscripts) \
	filter/test.sh

bannertopdf_SOURCES = \
	filter/banner.c \
	filter/banner.h \
	filter/bannertopdf.c \
	filter/pdf.cxx \
	filter/pdf.h \
	fontembed/embed.h \
	fontembed/sfnt.h
EXTRA_bannertopdf_SOURCES = filter/getline.c
bannertopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBJPEG_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(LIBQPDF_CFLAGS) \
	$(TIFF_CFLAGS) \
	$(FONTCONFIG_CFLAGS) \
	-I$(srcdir)/fontembed/

bannertopdf_CXXFLAGS = $(bannertopdf_CFLAGS)
bannertopdf_LDADD = \
	$(GETLINE) \
	$(CUPS_LIBS) \
	$(LIBJPEG_LIBS) \
	$(LIBPNG_LIBS) \
	$(LIBQPDF_LIBS) \
	$(TIFF_LIBS) \
	$(FONTCONFIG_LIBS) \
	libfontembed.la

bannertopdf_DEPENDENCIES = \
	$(GETLINE) \
	libfontembed.la

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
	filter/gstoraster.c \
	cupsfilters/colord.h \
	cupsfilters/raster.h
gstoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
gstoraster_LDADD = \
	$(CUPS_LIBS) \
	libcupsfilters.la

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
	-lm \
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
	-lm \
	libcupsfilters.la

urftopdf_SOURCES = \
	filter/urftopdf.cpp \
	filter/unirast.h
urftopdf_CXXFLAGS = \
	$(LIBQPDF_CFLAGS)
urftopdf_LDADD = \
	$(LIBQPDF_LIBS)

rastertopdf_SOURCES = \
	filter/rastertopdf.cpp
rastertopdf_CXXFLAGS = \
	$(CUPS_CFLAGS) \
	$(LCMS_CFLAGS) \
	$(LIBQPDF_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertopdf_LDADD = \
	$(CUPS_LIBS) \
	$(LCMS_LIBS) \
	$(LIBQPDF_LIBS) \
	libcupsfilters.la

mupdftoraster_SOURCES = \
        filter/mupdftoraster.c
mupdftoraster_CFLAGS = \
        $(CUPS_CFLAGS) \
        -I$(srcdir)/cupsfilters/
mupdftoraster_LDADD = \
        $(CUPS_LIBS) \
        libcupsfilters.la

rastertops_SOURCES = \
	filter/rastertops.c
rastertops_CFLAGS = \
	$(CUPS_CFLAGS) \
	-I$(srcdir)/cupsfilters/
rastertops_LDADD = \
	$(CUPS_LIBS) \
	-lz \
	libcupsfilters.la

sys5ippprinter_SOURCES = \
	filter/common.c \
	filter/common.h \
	filter/sys5ippprinter.c
EXTRA_sys5ippprinter_SOURCES = filter/strcasestr.c
sys5ippprinter_CFLAGS = $(CUPS_CFLAGS)
sys5ippprinter_LDADD = $(STRCASESTR) $(CUPS_LIBS)
sys5ippprinter_DEPENDENCIES = $(STRCASESTR)

texttotext_SOURCES = \
	filter/texttotext.c
EXTRA_texttotext_SOURCES = filter/strcasestr.c
texttotext_CFLAGS = $(CUPS_CFLAGS)
texttotext_LDADD = $(STRCASESTR) $(CUPS_LIBS) $(LIBICONV)
texttotext_DEPENDENCIES = $(STRCASESTR)

pdftops_SOURCES = \
	filter/common.c \
	filter/common.h \
	filter/pdftops.c
EXTRA_pdftops_SOURCES = filter/strcasestr.c
pdftops_CFLAGS = $(CUPS_CFLAGS)
pdftops_LDADD = $(STRCASESTR) $(CUPS_LIBS)
pdftops_DEPENDENCIES = $(STRCASESTR)

pdftoraster_SOURCES = \
	filter/pdftoraster.cxx
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
	$(CUPS_CFLAGS) \
	$(AVAHI_CFLAGS) \
	$(AVAHI_GLIB_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	-I$(srcdir)/cupsfilters/
cups_browsed_CXXFLAGS = $(cups_browsed_CFLAGS)
cups_browsed_LDADD = \
	$(CUPS_LIBS) \
	$(AVAHI_LIBS) \
	$(AVAHI_GLIB_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS) \
	libcupsfilters.la
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
	ppd/Fuji_Xerox-DocuPrint_CM305_df-PDF.ppd \
	ppd/Generic-PDF_Printer-PDF.ppd \
	ppd/HP-Color_LaserJet_CM3530_MFP-PDF.ppd \
	ppd/Ricoh-PDF_Printer-PDF.ppd
ppd_DATA = $(genppdfiles)
gsppdfiles = \
	ppd/pxlcolor.ppd \
	ppd/pxlmono.ppd
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
if ENABLE_BRAILLE
	chmod g-wx,o-wx $(DESTDIR)/$(pkgbackenddir)/cups-brf
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

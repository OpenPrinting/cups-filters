ACLOCAL_AMFLAGS = -I m4

doc_DATA = \
	ABOUT-NLS \
	AUTHORS \
	COPYING \
	CHANGES.md \
	CHANGES-1.x.md \
	INSTALL \
	LICENSE \
	NOTICE \
	README.md \
	DEVELOPING.md \
	CONTRIBUTING.md

EXTRA_DIST = \
	$(doc_DATA) \
	autogen.sh \
	config.rpath \
	ln-srf \
	utils/driverless-fax.in

EXTRA_DIST += \
	drv/custom-media-lines

# ========
# Backends
# ========
pkgbackenddir = $(CUPS_SERVERBIN)/backend
pkgbackend_PROGRAMS = parallel serial beh

parallel_SOURCES = \
	backend/parallel.c
parallel_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)
parallel_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)

serial_SOURCES = \
	backend/serial.c
serial_LDADD = \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)
serial_CFLAGS = \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)

beh_SOURCES = \
	backend/beh.c
beh_LDADD = \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)
beh_CFLAGS = \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)

# ====================
# "driverless" utility
# ====================
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
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
driverless_CXXFLAGS = $(driverless_CFLAGS)
driverless_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

# =======
# Drivers
# =======
pkgdriverdir = $(CUPS_DATADIR)/drv

gendrvfiles = \
	drv/cupsfilters.drv
pkgdriver_DATA = $(gendrvfiles)

EXTRA_DIST += $(gendrvfiles)

# =======================
# Definitions for drivers
# =======================
pkgppdcdir = $(datadir)/ppdc

genppdcfiles = \
	filter/pcl.h \
	filter/escp.h
pkgppdc_DATA = $(genppdcfiles)

EXTRA_DIST += \
	$(genppdcfiles)

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
universalpsmimefiles = \
	mime/cupsfilters-universal-postscript.convs
individualmimefiles = \
	mime/cupsfilters-individual.convs
popplermimefiles = \
	mime/cupsfilters-poppler.convs
gsmimefiles = \
	mime/cupsfilters-ghostscript.convs
mutoolmimefiles = \
	mime/cupsfilters-mupdf.convs

if ENABLE_UNIVERSAL_CUPS_FILTER
pkgmime_DATA += $(universalmimefiles)
if ENABLE_GHOSTSCRIPT
pkgmime_DATA += $(universalpsmimefiles)
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

EXTRA_DIST += \
	$(genmimefiles) \
	$(universalmimefiles) \
	$(universalpsmimefiles) \
	$(individualmimefiles) \
	$(popplermimefiles) \
	$(gsmimefiles) \
	$(mutoolmimefiles)

# =======
# Filters
# =======
pkgfilter_PROGRAMS =
pkgfilterdir = $(CUPS_SERVERBIN)/filter

genfilterscripts = \
	filter/texttops
pkgfilter_SCRIPTS = $(genfilterscripts)

pkgfilter_PROGRAMS += \
	pdftopdf \
	texttotext \
	rastertoescpx \
	rastertopclx \
	commandtoescpx \
	commandtopclx
if ENABLE_GHOSTSCRIPT
pkgfilter_PROGRAMS += \
	gstopxl
endif
if ENABLE_FOOMATIC
pkgfilter_PROGRAMS += \
	foomatic-rip
endif
if ENABLE_UNIVERSAL_CUPS_FILTER
pkgfilter_PROGRAMS += \
	universal
endif
pkgfilter_PROGRAMS += \
	pdftops \
	pwgtopclm \
	pwgtopdf \
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
if ENABLE_TEXTTOPDF
pkgfilter_PROGRAMS += \
	texttopdf
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

check_PROGRAMS = \
	test-external

# Not reliable bash script
#TESTS += filter/test.sh

EXTRA_DIST += \
	$(genfilterscripts) \
	filter/test.sh

bannertopdf_SOURCES = \
	filter/bannertopdf.c
bannertopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
bannertopdf_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

commandtoescpx_SOURCES = \
	filter/commandtoescpx.c \
	filter/pcl.h
commandtoescpx_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
commandtoescpx_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

commandtopclx_SOURCES = \
	filter/commandtopclx.c \
	filter/pcl.h
commandtopclx_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
commandtopclx_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
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
	filter/foomatic-rip/util.h
foomatic_rip_CFLAGS = \
	-DCONFIG_PATH='"$(sysconfdir)/foomatic"' \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
foomatic_rip_LDADD = \
	$(CUPS_LIBS) \
	-lm \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

gstoraster_SOURCES = \
	filter/gstoraster.c
gstoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
gstoraster_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

gstopdf_SOURCES = \
	filter/gstopdf.c
gstopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
gstopdf_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

gstopxl_SOURCES = \
	filter/gstopxl.c
gstopxl_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
gstopxl_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

imagetopdf_SOURCES = \
	filter/imagetopdf.c
imagetopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
imagetopdf_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

imagetops_SOURCES = \
	filter/imagetops.c
imagetops_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
imagetops_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

imagetoraster_SOURCES = \
	filter/imagetoraster.c
imagetoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
imagetoraster_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

pclmtoraster_SOURCES = \
	filter/pclmtoraster.c
pclmtoraster_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
pclmtoraster_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

pdftopdf_SOURCES = \
	filter/pdftopdf.c
pdftopdf_CFLAGS = \
	$(LIBPPD_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(CUPS_CFLAGS)
pdftopdf_LDADD = \
	$(LIBPPD_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(CUPS_LIBS)

pwgtopclm_SOURCES = \
	filter/pwgtopclm.c
pwgtopclm_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
pwgtopclm_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

pwgtopdf_SOURCES = \
	filter/pwgtopdf.c
pwgtopdf_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
pwgtopdf_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

mupdftopwg_SOURCES = \
	filter/mupdftopwg.c
mupdftopwg_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
mupdftopwg_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

pwgtoraster_SOURCES = \
	filter/pwgtoraster.c
pwgtoraster_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
pwgtoraster_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

rastertops_SOURCES = \
	filter/rastertops.c
rastertops_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
rastertops_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

rastertopwg_SOURCES = \
	filter/rastertopwg.c
rastertopwg_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
rastertopwg_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

texttotext_SOURCES = \
	filter/texttotext.c
texttotext_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
texttotext_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

texttopdf_SOURCES = \
	filter/texttopdf.c
texttopdf_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
texttopdf_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

pdftops_SOURCES = \
	filter/pdftops.c
pdftops_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
pdftops_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

pstops_SOURCES = \
	filter/pstops.c
pstops_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
pstops_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

pdftoraster_SOURCES = \
	filter/pdftoraster.c
pdftoraster_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
pdftoraster_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

rastertoescpx_SOURCES = \
	filter/escp.h \
	filter/rastertoescpx.c
rastertoescpx_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
rastertoescpx_LDADD = \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

rastertopclx_SOURCES = \
	filter/pcl.h \
	filter/pcl-common.c \
	filter/pcl-common.h \
	filter/rastertopclx.c
rastertopclx_CFLAGS = \
	$(CUPS_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS)
rastertopclx_LDADD = \
	$(CUPS_LIBS) \
	$(LIBPNG_LIBS) \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS)

universal_SOURCES = \
	filter/universal.c
universal_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(CUPS_CFLAGS)
universal_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

test_external_SOURCES = \
	filter/test-external.c
test_external_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(LIBPPD_CFLAGS) \
	$(CUPS_CFLAGS)
test_external_LDADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(LIBPPD_LIBS) \
	$(CUPS_LIBS)

# =========
# Man pages
# =========
man_MANS =

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

EXTRA_DIST += \
	$(driverlessmanpages) \
	filter/foomatic-rip/foomatic-rip.1.in

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

uninstall-hook:
if ENABLE_FOOMATIC
	$(RM) $(DESTDIR)$(bindir)/foomatic-rip
endif
if ENABLE_DRIVERLESS
	$(RM) $(DESTDIR)$(bindir)/driverless
	$(RM) $(DESTDIR)$(pkgbackenddir)/driverless
	$(RM) $(DESTDIR)$(bindir)/driverless-fax
	$(RM) $(DESTDIR)$(pkgbackenddir)/driverless-fax
endif

SUBDIRS =

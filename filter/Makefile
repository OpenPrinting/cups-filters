#
# "$Id$"
#
#   Filter makefile for OpenPrinting CUPS Filters.
#
#   Copyright 2007-2011 by Apple Inc.
#   Copyright 1997-2006 by Easy Software Products.
#
#   These coded instructions, statements, and computer programs are the
#   property of Apple Inc. and are protected by Federal copyright
#   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
#   which should have been included with this file.  If this file is
#   file is missing or damaged, see the license at "http://www.cups.org/".
#
#   This file is subject to the Apple OS-Developed Software exception.
#

include ../Makedefs


#
# Files...
#

CHARSETS =	\
		pdf.utf-8.heavy \
		pdf.utf-8.simple

DATAFILES =	\
		testprint \
		default.pdf \
		default-testpage.pdf

BANNERS	=	\
		classified \
		confidential \
		secret \
		standard \
		topsecret \
		unclassified

MIMEFILES =	\
		cupsfilters.convs \
		cupsfilters.types

OBJS	=	\
		bannertopdf.o \
		banner.o \
		pdf.o \
		commandtoescpx.o \
		commandtopclx.o \
		imagetoraster.o \
		common.o \
		pcl-common.o \
		pdftops.o \
		rastertoescpx.o \
		rastertopclx.o \
		textcommon.o \
		imagetopdf.o \
		pdftoijs.o \
		texttopdf.o \
		pdfutils.o \
		pdftoraster.o

TARGETS	=	\
		bannertopdf \
		commandtoescpx \
		commandtopclx \
		imagetoraster \
		pdftops \
		rastertoescpx \
		rastertopclx \
		imagetopdf \
		pdftoijs \
		texttopdf \
		pdftoraster

SCRIPTS	=	\
		pstopdf \
		texttops \
		imagetops \
		textonly

PPDFILES =	\
		HP-PhotoSmart_Pro_B8300-hpijs-pdftoijs.ppd \
		textonly.ppd

DRVFILES =	\
		cupsfilters.drv


#
# Make all targets...
#

all:	$(TARGETS)


#
# Clean all object files...
#

clean:
	$(RM) $(OBJS) $(TARGETS)
	$(RM) */*.d
	$(RM) */*.o


#
# Update dependencies (without system header dependencies...)
#

depend:
	makedepend -Y -I.. -fDependencies $(OBJS:.o=.c) >/dev/null 2>&1


#
# Install all targets...
#

install:	all install-data install-headers install-libs install-exec


#
# Install data files...
#

install-data:
	echo Installing charsets to $(DATADIR)/charsets...
	$(INSTALL_DIR) -m 755 $(DATADIR)/charsets
	for file in $(CHARSETS); do \
		$(INSTALL_DATA) $$file $(DATADIR)/charsets; \
	done; \
	$(LN) pdf.utf-8.simple $(DATADIR)/charsets/pdf.utf-8
	echo Installing data files to $(DATADIR)/data...
	$(INSTALL_DIR) -m 755 $(DATADIR)/data
	for file in $(DATAFILES); do \
		$(INSTALL_DATA) $$file $(DATADIR)/data; \
	done
	echo Installing banners to $(DATADIR)/banners...
	$(INSTALL_DIR) -m 755 $(DATADIR)/banners
	for file in $(BANNERS); do \
		$(INSTALL_DATA) $$file $(DATADIR)/banners; \
	done
	echo Installing MIME files to $(DATADIR)/mime...
	$(INSTALL_DIR) -m 755 $(DATADIR)/mime
	for file in $(MIMEFILES); do \
		$(INSTALL_DATA) $$file $(DATADIR)/mime; \
	done
	echo Installing DRV files to $(DATADIR)/drv...
	$(INSTALL_DIR) -m 755 $(DATADIR)/drv
	for file in $(DRVFILES); do \
		$(INSTALL_DATA) $$file $(DATADIR)/drv; \
	done
	echo Installing PPD files to $(BUILDROOT)${datadir}/ppd/cupsfilters...
	$(INSTALL_DIR) -m 755 $(BUILDROOT)${datadir}/ppd/cupsfilters
	for file in $(PPDFILES); do \
		$(INSTALL_DATA) $$file $(BUILDROOT)${datadir}/ppd/cupsfilters; \
	done


#
# Install programs...
#

install-exec:
	echo Installing filters to $(SERVERBIN)/filter...
	$(INSTALL_DIR) -m 755 $(SERVERBIN)/filter
	for file in $(TARGETS); do \
		$(INSTALL_BIN) $$file $(SERVERBIN)/filter; \
	done
	for file in $(SCRIPTS); do \
		$(INSTALL_SCRIPT) $$file $(SERVERBIN)/filter; \
	done


#
# Install headers...
#

install-headers:


#
# Install libraries...
#

install-libs:


#
# Uninstall all targets...
#

uninstall:
	echo Uninstalling charsets from $(DATADIR)/charsets
	$(RM) $(DATADIR)/charsets/pdf.utf-8
	for file in $(CHARSETS); do \
		$(RM) $(DATADIR)/charsets/$$file; \
	done
	-$(RMDIR) $(DATADIR)/charsets
	echo Uninstalling data files from $(DATADIR)/data
	for file in $(DATAFILES); do \
		$(RM) $(DATADIR)/data/$$file; \
	done
	-$(RMDIR) $(DATADIR)/data
	echo Uninstalling banners from $(DATADIR)/banners
	for file in $(BANNERS); do \
		$(RM) $(DATADIR)/banners/$$file; \
	done
	-$(RMDIR) $(DATADIR)/banners
	echo Uninstalling MIME files from $(DATADIR)/mime
	for file in $(MIMEFILES); do \
		$(RM) $(DATADIR)/mime/$$file; \
	done
	-$(RMDIR) $(DATADIR)/mime
	echo Uninstalling DRV files from $(DATADIR)/drv
	for file in $(DRVFILES); do \
		$(RM) $(DATADIR)/drv/$$file; \
	done
	-$(RMDIR) $(DATADIR)/drv
	-$(RMDIR) $(DATADIR)
	echo Uninstalling PPD files from $(BUILDROOT)${datadir}/ppd/cupsfilters
	for file in $(PPDFILES); do \
		$(RM) $(BUILDROOT)${datadir}/ppd/cupsfilters/$$file; \
	done
	-$(RMDIR) $(BUILDROOT)${datadir}/ppd/cupsfilters
	-$(RMDIR) $(BUILDROOT)${datadir}/ppd
	echo Uninstalling filters from $(SERVERBIN)/filter
	for file in $(TARGETS) $(SCRIPTS); do \
		$(RM) $(SERVERBIN)/filter/$$file; \
	done
	-$(RMDIR) $(SERVERBIN)/filter
	-$(RMDIR) $(SERVERBIN)


#
# bannertopdf: The PDF banner filter.
#

bannertopdf:	bannertopdf.o banner.o pdf.o
	echo Linking $@...
	$(CXX) $(LDFLAGS) -o $@ bannertopdf.o banner.o pdf.o \
		$(LINKCUPSFILTERS) $(LIBS) $(POPPLER_LIBS)


#
# commandtopclx: The PCL command printer driver.
#

commandtopclx:		commandtopclx.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtopclx.o $(LINKCUPSFILTERS) $(LIBS)


#
# commandtoescpx: The ESC/P command printer driver.
#

commandtoescpx:		commandtoescpx.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtoescpx.o $(LINKCUPSFILTERS) $(LIBS)


#
# imagetoraster: The CUPS Raster image filter.
#

imagetoraster:	imagetoraster.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ imagetoraster.o common.o \
		$(LINKCUPSFILTERS) $(LIBS)


#
# pdftops: The PDF to PostScript filter (wrapper).
#

pdftops:	pdftops.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ pdftops.o common.o $(LIBS)


#
# rastertoescpx: The ESC/P raster printer driver.
#

rastertoescpx:		rastertoescpx.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertoescpx.o $(LINKCUPSFILTERS) $(LIBS)


#
# rastertopclx: The ESC/P raster printer driver.
#

rastertopclx:		rastertopclx.o pcl-common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertopclx.o pcl-common.o $(LINKCUPSFILTERS) \
		$(LIBS)


#
# imagetopdf
#

imagetopdf:	imagetopdf.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ imagetopdf.o common.o $(LINKCUPSFILTERS) \
		$(IMGLIBS) $(LIBS)


#
# pdftoijs
#

pdftoijs:	pdftoijs.o
	echo Linking $@...
	$(CXX) $(LDFLAGS) -o $@ pdftoijs.o \
		$(POPPLER_LIBS) $(IJS_LIBS)


#
# texttopdf
#

fontembed/libfontembed.a:
	$(MAKE) -C fontembed

texttopdf:	texttopdf.o textcommon.o common.o pdfutils.o fontembed/libfontembed.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ texttopdf.o textcommon.o common.o pdfutils.o -Lfontembed -lfontembed $(FONTCONFIG_LIBS) $(LIBS)


#
# pdftoraster
#

pdftoraster:	pdftoraster.o
	echo Linking $@...
	$(CXX) $(LDFLAGS) -o $@ pdftoraster.o \
		$(POPPLER_LIBS) $(LCMS_LIBS) $(LINKCUPSFILTERS) $(IMGLIBS) $(LIBS)


#
# Dependencies...
#

include Dependencies


#
# End of "$Id$".
#

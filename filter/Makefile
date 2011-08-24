#
# "$Id$"
#
#   Filter makefile for CUPS Legacy.
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
		utf-8

DATAFILES =	\
		psglyphs

FONTS	=	\
		Monospace.pfa \
		Monospace-Bold.pfa \
		Monospace-BoldOblique.pfa \
		Monospace-Oblique.pfa

MIMEFILES =	\
		legacy.types

OBJS	=	\
		bannertops.o \
		commandtoescpx.o \
		commandtopclx.o \
		imagetops.o \
		imagetoraster.o \
		common.o \
		pcl-common.o \
		pdftops.o \
		pstext.o \
		rastertoescpx.o \
		rastertopclx.o \
		textcommon.o \
		texttops.o

TARGETS	=	\
		bannertops \
		commandtoescpx \
		commandtopclx \
		imagetops \
		imagetoraster \
		pdftops \
		rastertoescpx \
		rastertopclx \
		texttops


#
# Make all targets...
#

all:	$(TARGETS)


#
# Clean all object files...
#

clean:
	$(RM) $(OBJS) $(TARGETS)


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
	echo Installing data files to $(DATADIR)/data...
	$(INSTALL_DIR) -m 755 $(DATADIR)/data
	for file in $(DATAFILES); do \
		$(INSTALL_DATA) $$file $(DATADIR)/data; \
	done
	echo Installing fonts to $(DATADIR)/fonts...
	$(INSTALL_DIR) -m 755 $(DATADIR)/fonts
	for file in $(FONTS); do \
		$(INSTALL_DATA) $$file $(DATADIR)/fonts; \
	done
	echo Installing MIME files to $(DATADIR)/mime...
	$(INSTALL_DIR) -m 755 $(DATADIR)/mime
	for file in $(MIMEFILES); do \
		$(INSTALL_DATA) $$file $(DATADIR)/mime; \
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
	for file in $(CHARSETS); do \
		$(RM) $(DATADIR)/charsets/$$file; \
	done
	-$(RMDIR) $(DATADIR)/charsets
	echo Uninstalling data files from $(DATADIR)/data
	for file in $(DATAFILES); do \
		$(RM) $(DATADIR)/data/$$file; \
	done
	-$(RMDIR) $(DATADIR)/data
	echo Uninstalling fonts from $(DATADIR)/fonts
	for file in $(FONTS); do \
		$(RM) $(DATADIR)/fonts/$$file; \
	done
	-$(RMDIR) -m 755 $(DATADIR)/fonts
	echo Uninstalling MIME files from $(DATADIR)/mime
	for file in $(MIMEFILES); do \
		$(RM) $(DATADIR)/mime/$$file; \
	done
	-$(RMDIR) -m 755 $(DATADIR)/mime
	-$(RMDIR) -m 755 $(DATADIR)
	echo Uninstalling filters from $(SERVERBIN)/filter
	for file in $(TARGETS); do \
		$(RM) $(SERVERBIN)/filter/$$file; \
	done
	-$(RMDIR) $(SERVERBIN)/filter
	-$(RMDIR) $(SERVERBIN)


#
# bannertops: The PostScript banner filter.
#

bannertops:	bannertops.o pstext.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ bannertops.o pstext.o common.o $(LIBS)


#
# commandtopclx: The PCL command printer driver.
#

commandtopclx:		commandtopclx.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtopclx.o $(LIBS)


#
# commandtoescpx: The ESC/P command printer driver.
#

commandtoescpx:		commandtoescpx.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtoescpx.o $(LIBS)


#
# imagetops: The PostScript image filter.
#

imagetops:	imagetops.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ imagetops.o common.o $(LIBS)


#
# imagetoraster: The CUPS Raster image filter.
#

imagetoraster:	imagetoraster.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ imagetoraster.o common.o $(LIBS)


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
	$(CC) $(LDFLAGS) -o $@ rastertoescpx.o $(LINKCUPSLEGACY) $(LIBS)


#
# rastertopclx: The ESC/P raster printer driver.
#

rastertopclx:		rastertopclx.o pcl-common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertopclx.o pcl-common.o $(LINKCUPSLEGACY) \
		$(LIBS)


#
# texttops: The text to PostScript filter.
#

texttops:	texttops.o textcommon.o common.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ texttops.o textcommon.o common.o $(LIBS)


#
# Dependencies...
#

include Dependencies


#
# End of "$Id$".
#

#
# "$Id$"
#
#   Filter makefile for CUPS.
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


FILTERS	=	\
		$(BANNERTOPS) \
		commandtops \
		gziptoany \
		$(TEXTTOPS) \
		pstops \
		$(IMGFILTERS) \
		$(PDFTOPS) \
		rastertoepson \
		rastertohp \
		rastertolabel \
		rastertopwg
LIBTARGETS =	\
		$(LIBCUPSIMAGE) \
		libcupsimage.a \
		$(LIB32CUPSIMAGE) \
		$(LIB64CUPSIMAGE)
UNITTARGETS =	\
		rasterbench \
		testimage \
		testraster
TARGETS	=	\
		$(LIBTARGETS) \
		$(FILTERS)

IMAGEOBJS =	image-bmp.o image-colorspace.o image-gif.o image-jpeg.o \
		image-photocd.o image-pix.o image-png.o image-pnm.o \
		image-sgi.o image-sgilib.o image-sun.o image-tiff.o \
		image-zoom.o image.o error.o interpret.o raster.o
IMAGE32OBJS =	$(IMAGEOBJS:.o=.32.o)
IMAGE64OBJS =	$(IMAGEOBJS:.o=.64.o)
OBJS	=	$(IMAGEOBJS) \
		bannertops.o commandtops.o gziptoany.o imagetops.o \
		imagetoraster.o common.o pdftops.o pstext.o pstops.o \
		rasterbench.o rastertoepson.o rastertohp.o rastertolabel.o \
		rastertopwg.o testimage.o testraster.o textcommon.o texttops.o


#
# Make all targets...
#

all:	$(TARGETS)


#
# Make library targets...
#

libs:		$(LIBTARGETS)


#
# Make unit tests...
#

unittests:	$(UNITTARGETS)


#
# Clean all object files...
#

clean:
	$(RM) $(OBJS) $(TARGETS) $(UNITTARGETS)
	$(RM) libcupsimage.so libcupsimage.sl libcupsimage.dylib
	$(RM) -r 32bit 64bit


#
# Update dependencies (without system header dependencies...)
#

depend:
	touch Dependencies.tmp
	makedepend -Y -I.. -fDependencies.tmp $(OBJS:.o=.c) >/dev/null 2>&1
	$(RM) Dependencies
	cp Dependencies.tmp Dependencies
	sed -E -e '1,$$s/^([^.]+)\.o:/\1\.32.o: \1\.c /' Dependencies.tmp >>Dependencies
	sed -E -e '1,$$s/^([^.]+)\.o:/\1\.64.o: \1\.c /' Dependencies.tmp >>Dependencies
	$(RM) Dependencies.tmp


#
# Install all targets...
#

install:	all install-data install-headers install-libs install-exec


#
# Install data files...
#

install-data:


#
# Install programs...
#

install-exec:
	$(INSTALL_DIR) -m 755 $(SERVERBIN)/filter
	for file in $(FILTERS); do \
		$(INSTALL_BIN) $$file $(SERVERBIN)/filter; \
	done
	$(RM) $(SERVERBIN)/filter/rastertodymo
	$(LN) rastertolabel $(SERVERBIN)/filter/rastertodymo
	if test "x$(SYMROOT)" != "x"; then \
		$(INSTALL_DIR) $(SYMROOT); \
		for file in $(FILTERS); do \
			cp $$file $(SYMROOT); \
		done \
	fi


#
# Install headers...
#

install-headers:
	$(INSTALL_DIR) -m 755 $(INCLUDEDIR)/cups
	$(INSTALL_DATA) image.h $(INCLUDEDIR)/cups


#
# Install libraries...
#

install-libs: $(INSTALLSTATIC) $(INSTALL32) $(INSTALL64)
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) $(LIBCUPSIMAGE) $(LIBDIR)
	-if test $(LIBCUPSIMAGE) = "libcupsimage.so.2" -o $(LIBCUPSIMAGE) = "libcupsimage.sl.2"; then \
		$(RM) $(LIBDIR)/`basename $(LIBCUPSIMAGE) .2`; \
		$(LN) $(LIBCUPSIMAGE) $(LIBDIR)/`basename $(LIBCUPSIMAGE) .2`; \
	fi
	-if test $(LIBCUPSIMAGE) = "libcupsimage.2.dylib"; then \
		$(RM) $(LIBDIR)/libcupsimage.dylib; \
		$(LN) $(LIBCUPSIMAGE) $(LIBDIR)/libcupsimage.dylib; \
	fi
	if test "x$(SYMROOT)" != "x"; then \
		$(INSTALL_DIR) $(SYMROOT); \
		cp $(LIBCUPSIMAGE) $(SYMROOT); \
	fi

installstatic:
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) -m 755 libcupsimage.a $(LIBDIR)
	$(RANLIB) $(LIBDIR)/libcupsimage.a
	$(CHMOD) 555 $(LIBDIR)/libcupsimage.a

install32bit:
	$(INSTALL_DIR) -m 755 $(LIB32DIR)
	$(INSTALL_LIB) 32bit/libcupsimage.so.2 $(LIB32DIR)/libcupsimage.so.2
	$(LN) libcupsimage.so.2 $(LIB32DIR)/libcupsimage.so

install64bit:
	$(INSTALL_DIR) -m 755 $(LIB64DIR)
	$(INSTALL_LIB) 64bit/libcupsimage.so.2 $(LIB64DIR)/libcupsimage.so.2
	$(LN) libcupsimage.so.2 $(LIB64DIR)/libcupsimage.so


#
# Uninstall all targets...
#

uninstall: $(UNINSTALL32) $(UNINSTALL64)
	for file in $(FILTERS); do \
		$(RM) $(SERVERBIN)/filter/$$file; \
	done
	$(RM) $(SERVERBIN)/filter/rastertodymo
	-$(RMDIR) $(SERVERBIN)/filter
	-$(RMDIR) $(SERVERBIN)
	$(RM) $(LIBDIR)/libcupsimage.2.dylib
	$(RM) $(LIBDIR)/libcupsimage.a
	$(RM) $(LIBDIR)/libcupsimage.dylib
	$(RM) $(LIBDIR)/libcupsimage_s.a
	$(RM) $(LIBDIR)/libcupsimage.sl
	$(RM) $(LIBDIR)/libcupsimage.sl.2
	$(RM) $(LIBDIR)/libcupsimage.so
	$(RM) $(LIBDIR)/libcupsimage.so.2
	-$(RMDIR) $(LIBDIR)
	$(RM) $(INCLUDEDIR)/cups/image.h
	-$(RMDIR) $(INCLUDEDIR)/cups

uninstall32bit:
	$(RM) $(LIB32DIR)/libcupsimage.so
	$(RM) $(LIB32DIR)/libcupsimage.so.2
	-$(RMDIR) $(LIB32DIR)

uninstall64bit:
	$(RM) $(LIB64DIR)/libcupsimage.so
	$(RM) $(LIB64DIR)/libcupsimage.so.2
	-$(RMDIR) $(LIB64DIR)


#
# Automatic API help files...
#

apihelp:
	echo Generating CUPS API help files...
	mxmldoc --section "Programming" --title "Raster API" \
		--css ../doc/cups-printable.css \
		--header api-raster.header --intro api-raster.shtml \
		api-raster.xml \
		../cups/raster.h interpret.c raster.c \
		>../doc/help/api-raster.html
	mxmldoc --tokens help/api-raster.html api-raster.xml >../doc/help/api-raster.tokens
	$(RM) api-raster.xml
	mxmldoc --section "Programming" \
		--title "Developing PostScript Printer Drivers" \
		--css ../doc/cups-printable.css \
		--header postscript-driver.header \
		--intro postscript-driver.shtml \
		>../doc/help/postscript-driver.html
	mxmldoc --section "Programming" \
		--title "Introduction to the PPD Compiler" \
		--css ../doc/cups-printable.css \
		--header ppd-compiler.header \
		--intro ppd-compiler.shtml \
		>../doc/help/ppd-compiler.html
	mxmldoc --section "Programming" \
		--title "Developing Raster Printer Drivers" \
		--css ../doc/cups-printable.css \
		--header raster-driver.header \
		--intro raster-driver.shtml \
		>../doc/help/raster-driver.html
	mxmldoc --section "Specifications" \
		--title "CUPS PPD Extensions" \
		--css ../doc/cups-printable.css \
		--header spec-ppd.header \
		--intro spec-ppd.shtml \
		>../doc/help/spec-ppd.html

framedhelp:
	echo Generating CUPS API help files...
	mxmldoc --section "Programming" --title "Raster API" \
		--framed ../cups/api-raster \
		--css ../doc/cups-printable.css \
		--header api-raster.header --intro api-raster.shtml \
		../cups/raster.h interpret.c raster.c
	mxmldoc --section "Programming" \
		--title "Developing PostScript Printer Drivers" \
		--framed ../cups/postscript-driver \
		--css ../doc/cups-printable.css \
		--header postscript-driver.header \
		--intro postscript-driver.shtml
	mxmldoc --section "Programming" \
		--title "Introduction to the PPD Compiler" \
		--framed ../cups/ppd-compiler \
		--css ../doc/cups-printable.css \
		--header ppd-compiler.header \
		--intro ppd-compiler.shtml
	mxmldoc --section "Programming" \
		--title "Developing Raster Printer Drivers" \
		--framed ../cups/raster-driver \
		--css ../doc/cups-printable.css \
		--header raster-driver.header \
		--intro raster-driver.shtml
	mxmldoc --section "Specifications" \
		--title "CUPS PPD Extensions" \
		--framed ../cups/spec-ppd \
		--css ../doc/cups-printable.css \
		--header spec-ppd.header \
		--intro spec-ppd.shtml \


#
# bannertops
#

bannertops:	bannertops.o pstext.o common.o ../cups/$(LIBCUPS) $(LIBCUPSIMAGE)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ bannertops.o pstext.o common.o $(LINKCUPSIMAGE) \
		$(IMGLIBS) $(LIBS)


#
# commandtops
#

commandtops:	commandtops.o ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtops.o $(LIBS)


#
# gziptoany
#

gziptoany:	gziptoany.o ../Makedefs ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ gziptoany.o $(LIBZ) $(LIBS)


#
# libcupsimage.so.2, libcupsimage.sl.2
#

libcupsimage.so.2 libcupsimage.sl.2:	$(IMAGEOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ $(IMAGEOBJS) $(DSOLIBS) \
		-L../cups $(LINKCUPS) -lm
	$(RM) `basename $@ .2`
	$(LN) $@ `basename $@ .2`


#
# 32bit/libcupsimage.so.2
#

32bit/libcupsimage.so.2:	$(IMAGE32OBJS)
	echo Linking 32-bit $@...
	-mkdir 32bit
	$(DSO) $(ARCH32FLAGS) $(DSO32FLAGS) -o $@ $(IMAGE32OBJS) $(DSOLIBS) \
		-L../cups/32bit $(LINKCUPS) -lm


#
# 64bit/libcupsimage.so.2
#

64bit/libcupsimage.so.2:	$(IMAGE64OBJS)
	echo Linking 64-bit $@...
	-mkdir 64bit
	$(DSO) $(ARCH64FLAGS) $(DSO64FLAGS) -o $@ $(IMAGE64OBJS) $(DSOLIBS) \
		-L../cups/64bit $(LINKCUPS) -lm


#
# libcupsimage.2.dylib
#

libcupsimage.2.dylib:	$(IMAGEOBJS) $(LIBCUPSIMAGEORDER)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ \
		-install_name $(libdir)/$@ \
		-current_version 2.3.0 \
		-compatibility_version 2.0.0 \
		$(IMAGEOBJS) $(DSOLIBS) -L../cups $(LINKCUPS) -lm
	$(RM) libcupsimage.dylib
	$(LN) $@ libcupsimage.dylib


#
# libcupsimage_s.a
#

libcupsimage_s.a:	$(IMAGEOBJS) libcupsimage_s.exp
	echo Linking $@...
	$(DSO) $(DSOFLAGS) -Wl,-berok,-bexport:libcupsimage_s.exp \
		-o libcupsimage_s.o $(IMAGEOBJS) $(DSOLIBS) -lm
	$(RM) $@
	$(AR) $(ARFLAGS) $@ libcupsimage_s.o


#
# libcupsimage.la
#

libcupsimage.la:       $(IMAGEOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ $(IMAGEOBJS:.o=.lo) $(DSOLIBS) \
		-L../cups $(LINKCUPS) \
		-rpath $(LIBDIR) -version-info 2:3


#
# libcupsimage.a
#

libcupsimage.a:	$(IMAGEOBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(IMAGEOBJS)
	$(RANLIB) $@


#
# testimage
#

testimage:	testimage.o libcupsimage.a ../Makedefs
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testimage.o libcupsimage.a \
		$(IMGLIBS) $(DSOLIBS) $(LIBS)


#
# imagetops
#

imagetops:	imagetops.o common.o $(LIBCUPSIMAGE) \
		../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ imagetops.o common.o $(LINKCUPSIMAGE) \
		$(IMGLIBS) $(LIBS)


#
# imagetoraster
#

imagetoraster:	imagetoraster.o common.o $(LIBCUPSIMAGE) \
		../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ imagetoraster.o common.o $(LINKCUPSIMAGE) \
		$(IMGLIBS) $(LIBS)


#
# pstops
#

pstops:	pstops.o common.o ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ pstops.o common.o $(LIBS) -lm


#
# pdftops
#

pdftops:	pdftops.o common.o ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ pdftops.o common.o $(LIBS)


#
# rastertoepson
#

rastertoepson:	rastertoepson.o ../cups/$(LIBCUPS) $(LIBCUPSIMAGE)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertoepson.o $(LINKCUPSIMAGE) $(IMGLIBS) $(LIBS)


#
# rastertohp
#

rastertohp:	rastertohp.o ../cups/$(LIBCUPS) $(LIBCUPSIMAGE)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertohp.o $(LINKCUPSIMAGE) $(IMGLIBS) $(LIBS)


#
# rastertolabel
#

rastertolabel:	rastertolabel.o ../cups/$(LIBCUPS) $(LIBCUPSIMAGE)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertolabel.o $(LINKCUPSIMAGE) $(IMGLIBS) $(LIBS)


#
# rastertopwg
#

rastertopwg:	rastertopwg.o ../cups/$(LIBCUPS) $(LIBCUPSIMAGE)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertopwg.o $(LINKCUPSIMAGE) $(IMGLIBS) $(LIBS)


#
# testraster
#

testraster:	testraster.o ../cups/$(LIBCUPSSTATIC) libcupsimage.a
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testraster.o libcupsimage.a \
		../cups/$(LIBCUPSSTATIC) $(IMGLIBS) $(DSOLIBS) $(COMMONLIBS) \
		$(SSLLIBS) $(DNSSDLIBS) $(LIBGSSAPI)
	echo Running raster API tests...
	./testraster


#
# rasterbench
#

rasterbench:	rasterbench.o libcupsimage.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rasterbench.o libcupsimage.a $(LIBS)


#
# texttops
#

texttops:	texttops.o textcommon.o common.o \
		../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ texttops.o textcommon.o common.o $(LIBS)


#
# Dependencies...
#

include Dependencies


#
# End of "$Id$".
#
#
# "$Id$"
#
#   Makefile for the CUPS base drivers.
#
#   Copyright 2007-2010 by Apple Inc.
#   Copyright 2002-2005 by Easy Software Products.
#
#   These coded instructions, statements, and computer programs are the
#   property of Apple Inc. and are protected by Federal copyright
#   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
#   which should have been included with this file.  If this file is
#   file is missing or damaged, see the license at "http://www.cups.org/".
#

#
# Include standard definitions...
#

include ../Makedefs


#
# Object files...
#

LIBOBJS	= \
		attr.o \
		check.o \
		cmyk.o \
		dither.o \
		lut.o \
		pack.o \
		rgb.o \
		srgb.o
OBJS		= \
		commandtoescpx.o \
		commandtopclx.o \
		rastertoescpx.o \
		rastertopclx.o \
		pcl-common.o \
		testcmyk.o \
		testdither.o \
		testrgb.o \
		$(LIBOBJS)

LIBTARGETS =	\
		$(LIBCUPSDRIVER) \
		libcupsdriver.a
UNITTARGETS =	\
		testcmyk \
		testdither \
		testrgb
FILTERS =	\
		commandtoescpx \
		commandtopclx \
		rastertoescpx \
		rastertopclx
TARGETS =	\
		$(LIBTARGETS) \
		$(FILTERS)


#
# Make everything...
#

all:		$(TARGETS)


#
# Make library targets...
#

libs:		$(LIBTARGETS)


#
# Make unit tests...
#

unittests:	$(UNITTARGETS)


#
# Clean everything...
#

clean:
	$(RM) $(OBJS) core
	$(RM) *.bck core.*
	$(RM) $(TARGETS) $(UNITTARGETS)
	$(RM) -r test
	$(RM) libcupsdriver.so libcupsdriver.sl libcupsdriver.dylib


#
# Update dependencies...
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


#
# Install programs...
#

install-exec:
	$(INSTALL_DIR) $(SERVERBIN)/filter
	for file in $(FILTERS); do \
		$(INSTALL_BIN) $$file $(SERVERBIN)/filter; \
	done
	if test "x$(SYMROOT)" != "x"; then \
		$(INSTALL_DIR) $(SYMROOT); \
		for file in $(FILTERS); do \
			cp $$file $(SYMROOT); \
		done \
	fi


#
# Install headers...
#

install-headers:
	echo Installing header files in $(INCLUDEDIR)/cups...
	$(INSTALL_DIR) -m 755 $(INCLUDEDIR)/cups
	$(INSTALL_DATA) driver.h $(INCLUDEDIR)/cups


#
# Install libraries...
#

install-libs: $(INSTALLSTATIC)
	echo Installing libraries in $(LIBDIR)...
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) $(LIBCUPSDRIVER) $(LIBDIR)
	if test $(LIBCUPSDRIVER) = "libcupsdriver.so.1" -o $(LIBCUPSDRIVER) = "libcupsdriver.sl.1"; then \
		$(RM) $(LIBDIR)/`basename $(LIBCUPSDRIVER) .1`; \
		$(LN) $(LIBCUPSDRIVER) $(LIBDIR)/`basename $(LIBCUPSDRIVER) .1`; \
	fi
	if test $(LIBCUPSDRIVER) = "libcupsdriver.1.dylib"; then \
		$(RM) $(LIBDIR)/libcupsdriver.dylib; \
		$(LN) $(LIBCUPSDRIVER) $(LIBDIR)/libcupsdriver.dylib; \
	fi
	if test "x$(SYMROOT)" != "x"; then \
		$(INSTALL_DIR) $(SYMROOT); \
		cp $(LIBCUPSDRIVER) $(SYMROOT); \
	fi

installstatic:
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) -m 755 libcupsdriver.a $(LIBDIR)
	$(RANLIB) $(LIBDIR)/libcupsdriver.a
	$(CHMOD) 555 $(LIBDIR)/libcupsdriver.a


#
# Uninstall...
#

uninstall:
	for file in commandtoescpx commandtopclx rastertoescpx rastertopclx; do \
		$(RM) $(SERVERBIN)/filter/$$file; \
	done
	$(RM) $(LIBDIR)/libcupsdriver.1.dylib
	$(RM) $(LIBDIR)/libcupsdriver.a
	$(RM) $(LIBDIR)/libcupsdriver.dylib
	$(RM) $(LIBDIR)/libcupsdriver_s.a
	$(RM) $(LIBDIR)/libcupsdriver.sl
	$(RM) $(LIBDIR)/libcupsdriver.sl.1
	$(RM) $(LIBDIR)/libcupsdriver.so
	$(RM) $(LIBDIR)/libcupsdriver.so.1
	-$(RMDIR) $(LIBDIR)
	$(RM) $(INCLUDEDIR)/cups/driver.h
	-$(RMDIR) $(INCLUDEDIR)/cups


#
# Automatic API help files...
#

apihelp:
	mxmldoc --section "Programming" \
		--title "Printer Driver API" \
		--css ../doc/cups-printable.css \
		--header api-driver.header --intro api-driver.shtml \
		api-driver.xml \
		driver.h $(LIBOBJS:.o=.c) >../doc/help/api-driver.html
	mxmldoc --tokens help/api-driver.html >../doc/help/api-driver.tokens
	$(RM) api-driver.xml

framedhelp:
	mxmldoc --framed api-driver \
		--section "Programming" \
		--title "Printer Driver API" \
		--css ../doc/cups-printable.css \
		--header api-driver.header --intro api-driver.shtml \
		driver.h $(LIBOBJS:.o=.c)


#
# commandtopclx, the PCL command printer driver.
#

commandtopclx:		commandtopclx.o $(LIBCUPSDRIVER) ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtopclx.o -L. -lcupsdriver $(LIBS)


#
# commandtoescpx, the ESC/P command printer driver.
#

commandtoescpx:		commandtoescpx.o $(LIBCUPSDRIVER) ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ commandtoescpx.o -L. -lcupsdriver $(LIBS)


#
# rastertoescpx, the ESC/P raster printer driver.
#

rastertoescpx:		rastertoescpx.o $(LIBCUPSDRIVER) ../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertoescpx.o -L. -lcupsdriver \
		$(LINKCUPSIMAGE) $(LIBS)


#
# rastertopclx, the ESC/P raster printer driver.
#

rastertopclx:		rastertopclx.o pcl-common.o $(LIBCUPSDRIVER) \
			../cups/$(LIBCUPS)
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ rastertopclx.o pcl-common.o -L. -lcupsdriver \
		$(LINKCUPSIMAGE) $(LIBS)


#
# test, make a common test subdirectory for the other test programs.
#

test:
	if test ! -d test; then \
		rm -rf test; \
		mkdir test; \
	fi


#
# testcmyk, test cmyk separation functions.
#

testcmyk:		test testcmyk.o libcupsdriver.a ../cups/$(LIBCUPSSTATIC)
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testcmyk.o libcupsdriver.a \
		../cups/$(LIBCUPSSTATIC) $(LIBGSSAPI) $(SSLLIBS) $(DNSSDLIBS) \
		$(COMMONLIBS) $(LIBZ)
	echo Running CMYK API tests...
	./testcmyk > test/testcmyk.log


#
# testdither, test dithering functions.
#

testdither:		test testdither.o libcupsdriver.a ../cups/$(LIBCUPSSTATIC)
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testdither.o libcupsdriver.a \
		../cups/$(LIBCUPSSTATIC) $(LIBGSSAPI) $(SSLLIBS) $(DNSSDLIBS) \
		$(COMMONLIBS) $(LIBZ)
	echo Running dither API tests...
	./testdither > test/0-255.pgm 2>test/0-255.log
	./testdither 0 127 255 > test/0-127-255.pgm 2>test/0-127-255.log
	./testdither 0 85 170 255 > test/0-85-170-255.pgm 2>test/0-85-170-255.log
	./testdither 0 63 127 170 198 227 255 > test/0-63-127-170-198-227-255.pgm 2>test/0-63-127-170-198-227-255.log
	./testdither 0 210 383 > test/0-210-383.pgm 2>test/0-210-383.log
	./testdither 0 82 255 > test/0-82-255.pgm 2>test/0-82-255.log
	./testdither 0 510 > test/0-510.pgm 2>test/0-510.log
	./testdither 0 1020 > test/0-1020.pgm 2>test/0-1020.log


#
# testrgb, test RGB separation functions.
#

testrgb:		test testrgb.o libcupsdriver.a ../cups/$(LIBCUPSSTATIC)
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testrgb.o libcupsdriver.a \
		../cups/$(LIBCUPSSTATIC) $(LIBGSSAPI) $(SSLLIBS) $(DNSSDLIBS) \
		$(COMMONLIBS) $(LIBZ)
	echo Running RGB API tests...
	./testrgb > test/testrgb.log 2>&1 || echo "RGB tests failed!"


#
# libcupsdriver.so.1, libcupsdriver.sl.1
#

libcupsdriver.so.1 libcupsdriver.sl.1:	$(LIBOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ $(LIBOBJS) $(LIBS)
	$(RM) `basename $@ .1`
	$(LN) $@ `basename $@ .1`


#
# libcupsdriver.1.dylib
#

libcupsdriver.1.dylib:	$(LIBOBJS) libcupsdriver.exp
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ \
		-install_name $(libdir)/$@ \
		-current_version 1.0.0 \
		-compatibility_version 1.0.0 \
		-exported_symbols_list libcupsdriver.exp \
		$(LIBOBJS) $(LIBS)
	$(RM) libcupsdriver.dylib
	$(LN) $@ libcupsdriver.dylib


#
# libcupsdriver_s.a
#

libcupsdriver_s.a:	$(LIBOBJS)
	echo Creating $@...
	$(DSO) $(DSOFLAGS) -o libcupsdriver_s.o $(LIBOBJS) $(LIBS)
	$(RM) $@
	$(AR) $(ARFLAGS) $@ libcupsdriver_s.o


#
# libcupsdriver.la
#

libcupsdriver.la:    $(LIBOBJS)
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(DSOFLAGS) -o $@ $(LIBOBJS:.o=.lo) -rpath $(LIBDIR) \
		-version-info 1:0 $(LIBS)


#
# libcupsdriver.a
#

libcupsdriver.a:	$(LIBOBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(LIBOBJS)
	$(RANLIB) $@


#
# Include dependencies...
#

include Dependencies


#
# End of "$Id$".
#

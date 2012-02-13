#
# "$Id$"
#
#   Library makefile for OpenPrinting CUPS Filters.
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


LIBTARGETS =	\
		$(LIBCUPSFILTERS) \
		libcupsfilters.a

UNITTARGETS =	\
		testcmyk \
		testdither \
		testimage \
		testrgb

TARGETS	=	\
		$(LIBTARGETS) \
		$(UNITTARGETS)

LIBOBJS =	\
		attr.o \
		check.o \
		cmyk.o \
		dither.o \
		image.o \
		image-bmp.o \
		image-colorspace.o \
		image-gif.o \
		image-jpeg.o \
		image-photocd.o \
		image-pix.o \
		image-png.o \
		image-pnm.o \
		image-sgi.o \
		image-sgilib.o \
		image-sun.o \
		image-tiff.o \
		image-zoom.o \
		lut.o \
		pack.o \
		rgb.o \
		srgb.o

OBJS	=	\
		$(LIBOBJS) \
		testcmyk.o \
		testdither.o \
		testimage.o \
		testrgb.o

HEADERS	=	\
		driver.h \
		image.h


#
# Make all targets...
#

all:	$(TARGETS)


#
# Clean all object files...
#

clean:
	$(RM) $(OBJS) $(TARGETS)
	$(RM) libcupsfilters.so libcupsfilters.dylib
	$(RM) -rf cupsfilters/test


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


#
# Install programs...
#

install-exec:


#
# Install headers...
#

install-headers:
	echo Installing header files into $(INCLUDEDIR)/cupsfilters...
	$(INSTALL_DIR) -m 755 $(INCLUDEDIR)/cupsfilters
	for file in $(HEADERS); do \
		$(INSTALL_DATA) $$file $(INCLUDEDIR)/cupsfilters; \
	done


#
# Install libraries...
#

install-libs:
	echo Installing libraries in $(LIBDIR)...
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) $(LIBCUPSFILTERS) $(LIBDIR)
	$(INSTALL_DATA) libcupsfilters.a $(LIBDIR)
	if test $(LIBCUPSFILTERS) = "libcupsfilters.so.1" -o $(LIBCUPSFILTERS) = "libcupsfilters.sl.1"; then \
		$(RM) $(LIBDIR)/`basename $(LIBCUPSFILTERS) .1`; \
		$(LN) $(LIBCUPSFILTERS) $(LIBDIR)/`basename $(LIBCUPSFILTERS) .1`; \
	fi
	if test $(LIBCUPSFILTERS) = "libcupsfilters.1.dylib"; then \
		$(RM) $(LIBDIR)/libcups.dylib; \
		$(LN) $(LIBCUPSFILTERS) $(LIBDIR)/libcups.dylib; \
	fi


#
# Uninstall all targets...
#

uninstall:
	$(RM) $(LIBDIR)/libcupsfilters.dylib
	$(RM) $(LIBDIR)/libcupsfilters.1.dylib
	$(RM) $(LIBDIR)/libcupsfilters.so
	$(RM) $(LIBDIR)/libcupsfilters.so.1
	$(RM) $(LIBDIR)/libcupsfilters.sl
	$(RM) $(LIBDIR)/libcupsfilters.sl.1
	$(RM) $(LIBDIR)/libcupsfilters.a
	-$(RMDIR) $(LIBDIR)
	for file in $(HEADERS); do \
		$(RM) $(INCLUDEDIR)/cupsfilters/$$file; \
	done
	-$(RMDIR) $(INCLUDEDIR)/cupsfilters
	-$(RMDIR) $(INCLUDEDIR)


#
# libcupsfilters.so.1, libcupsfilters.sl.1
#

libcupsfilters.so.1:	$(LIBOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ $(LIBOBJS) $(DSOLIBS) $(LINKCUPS) -lm
	$(RM) `basename $@ .1`
	$(LN) $@ `basename $@ .1`


#
# libcupsfilters.1.dylib
#

libcupsfilters.1.dylib:	$(LIBOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ \
		-install_name $(libdir)/$@ \
		-current_version 1.0.0 \
		-compatibility_version 1.0.0 \
		$(LIBOBJS) $(DSOLIBS) $(LINKCUPS) -lm
	$(RM) libcupsfilters.dylib
	$(LN) $@ libcupsfilters.dylib


#
# libcupsfilters.la
#

libcupsfilters.la:       $(LIBOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(DSOFLAGS) -o $@ $(LIBOBJS:.o=.lo) $(DSOLIBS) \
		-rpath $(LIBDIR) -version-info 1:0


#
# libcupsfilters.a
#

libcupsfilters.a:	$(LIBOBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(LIBOBJS)
	$(RANLIB) $@


#
# test: Make a common test subdirectory for the other test programs.
#

test:
	if test ! -d test; then \
		rm -rf test; \
		mkdir test; \
	fi


#
# testcmyk: Test cmyk separation functions.
#

testcmyk:	test testcmyk.o libcupsfilters.a
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testcmyk.o libcupsfilters.a \
		$(LIBS) -lm
	echo Running CMYK API tests...
	./testcmyk > test/testcmyk.log


#
# testdither: Test dithering functions.
#

testdither:	test testdither.o libcupsfilters.a
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testdither.o  libcupsfilters.a \
		$(LIBS) -lm
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
# testimage: Test image loading functions.
#

testimage:	testimage.o libcupsfilters.a ../Makedefs
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testimage.o libcupsfilters.a \
		$(IMGLIBS) $(DSOLIBS) $(LIBS) -lm

#
# testrgb, test RGB separation functions.
#

testrgb:	test testrgb.o libcupsfilters.a
	echo Linking $@...
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ testrgb.o  libcupsfilters.a \
		$(LIBS) -lm
	echo Running RGB API tests...
	./testrgb > test/testrgb.log 2>&1 || echo "RGB tests failed!"


#
# Include dependencies...
#

include Dependencies


#
# End of "$Id$".
#

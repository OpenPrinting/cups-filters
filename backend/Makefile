#
# "$Id$"
#
#   Backend makefile for OpenPrinting CUPS Filters.
#
#   Copyright 2007-2011 by Apple Inc.
#   Copyright 1997-2007 by Easy Software Products, all rights reserved.
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
# Object files...
#

BACKENDS =	parallel serial
UNITTESTS =	test1284
TARGETS =	$(BACKENDS) $(UNITTESTS)
OBJS	=	\
		ieee1284.o \
		parallel.o \
		serial.o \
		test1284.o


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


#
# Install programs...
#

install-exec:
	echo Installing backends in $(SERVERBIN)/backend...
	$(INSTALL_DIR) -m 755 $(SERVERBIN)/backend
	for file in $(BACKENDS); do \
		$(INSTALL_BIN) $$file $(SERVERBIN)/backend; \
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
	echo Uninstalling backends from $(SERVERBIN)/backend...
	for file in $(BACKENDS); do \
		$(RM) $(SERVERBIN)/backend/$$file; \
	done
	-$(RMDIR) $(SERVERBIN)/backend
	-$(RMDIR) $(SERVERBIN)


#
# test1284
#

test1284:	test1284.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o test1284 test1284.o $(LIBS)


#
# parallel
#

parallel:	parallel.o ieee1284.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o parallel parallel.o ieee1284.o $(LIBS)


#
# serial
#

serial:	serial.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o serial serial.o $(BACKLIBS) $(LIBS)


#
# Dependencies...
#

include Dependencies


#
# End of "$Id$".
#

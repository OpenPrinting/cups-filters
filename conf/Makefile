#
# "$Id$"
#
#   Configuration file makefile for CUPS Legacy.
#
#   Copyright 2007-2011 by Apple Inc.
#   Copyright 1993-2006 by Easy Software Products.
#
#   These coded instructions, statements, and computer programs are the
#   property of Apple Inc. and are protected by Federal copyright
#   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
#   which should have been included with this file.  If this file is
#   file is missing or damaged, see the license at "http://www.cups.org/".
#

include ../Makedefs

#
# Config files...
#

REPLACE	=	legacy.types


#
# Make everything...
#

all:


#
# Make library targets...
#

libs:


#
# Make unit tests...
#

unittests:


#
# Clean all config and object files...
#

clean:


#
# Dummy depend...
#

depend:


#
# Install all targets...
#

install:	all install-data install-headers install-libs install-exec


#
# Install data files...
#

install-data:
	$(INSTALL_DIR) -m 755 $(DATADIR)/mime
	for file in $(REPLACE); do \
		if test -r $(DATADIR)/mime/$$file ; then \
			$(MV) $(DATADIR)/mime/$$file $(DATADIR)/mime/$$file.O ; \
		fi ; \
		if test -r $(SERVERROOT)/$$file ; then \
			$(MV) $(SERVERROOT)/$$file $(DATADIR)/mime/$$file.O ; \
		fi ; \
		$(INSTALL_DATA) $$file $(DATADIR)/mime ; \
	done


#
# Install programs...
#

install-exec:


#
# Install headers...
#

install-headers:


#
# Install libraries...
#

install-libs:


#
# Uninstall files...
#

uninstall:
	for file in $(REPLACE); do \
		$(RM) $(SERVERROOT)/$$file; \
	done
	-$(RMDIR) $(SERVERROOT)
	for file in $(REPLACE); do \
		$(RM) $(DATADIR)/mime/$$file; \
	done
	-$(RMDIR) $(DATADIR)/mime


#
# End of "$Id$".
#

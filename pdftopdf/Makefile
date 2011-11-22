#
# "$Id: Makefile 6649 2007-07-11 21:46:42Z till $"
#
#   pdftopdf filter Makefile for the Common UNIX Printing System (CUPS).
#

include ../Makedefs

#
# Object files...
#

LIBOBJS	=	\
		P2PCatalog.o \
		P2PDoc.o \
		P2PGfx.o \
		P2PObject.o \
		P2POutput.o \
		P2POutputStream.o \
		P2PPage.o \
		P2PPageTree.o \
		P2PResources.o \
		P2PXRef.o \
		P2PFont.o \
		PDFFTrueTypeFont.o \
		P2PPattern.o \
		P2PCharCodeToUnicode.o \
		P2PCMap.o \
		P2PForm.o \
		parseargs.o \
		UGooString.o

OBJS	=	pdftopdf.o $(LIBOBJS)

TARGETS	=	pdftopdf


#
# Make everything...
#

all:	$(TARGETS)


#
# Blank targets to satisfy the main Makefile of CUPS ...
#

libs:
apihelp:
framedhelp:


#
# Clean all object files...
#

clean:
	$(RM) $(OBJS)
	$(RM) pdftopdf


#
# Update dependencies (without system header dependencies...)
#

depend:
	makedepend -Y -I.. -fDependencies $(OBJS:.o=.cxx) >/dev/null 2>&1


#
# Install the filter...
#

install:	install-data install-headers install-libs install-exec

install-data:

install-exec:	all
	$(INSTALL_DIR) -m 755 $(SERVERBIN)/filter
	$(INSTALL_BIN) pdftopdf $(SERVERBIN)/filter

install-headers:

install-libs:

#
# Uninstall the filter...
#

uninstall:
	$(RM) $(SERVERBIN)/filter/pdftopdf
	-$(RMDIR) $(SERVERBIN)/filter
	-$(RMDIR) $(SERVERBIN)


#
# pdftopdf
#

pdftopdf:	pdftopdf.o $(LIBOBJS)
	echo Linking $@...
	$(CXX) $(LDFLAGS) -o $@ pdftopdf.o $(LIBOBJS) $(POPPLER_LIBS) $(CXXLIBS) -lm -lz


#
# Dependencies...
#

include Dependencies

#
# Dependencies...
#

include Dependencies

#
# Make unit tests...
#

unittests:

#
# End of "$Id: Makefile 6649 2007-07-11 21:46:42Z till $".
#

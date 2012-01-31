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
		OPVPOutputDev.o

OBJS	=	pdftoopvp.o $(LIBOBJS)

TARGETS	=	pdftoopvp


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
	$(RM) pdftoopvp


#
# Update dependencies (without system header dependencies...)
#

depend:
	makedepend -Y -I. -I.. -Ioprs -Iopvp -fDependencies $(OBJS:.o=.cxx) >/dev/null 2>&1


#
# Install the filter...
#

install:	install-data install-headers install-libs install-exec

install-data:	all
	echo Installing configuration files to $(sysconfdir)/fonts/conf.d...
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(sysconfdir)/fonts/conf.d
	$(INSTALL_DATA) -m 644 99pdftoopvp.conf $(BUILDROOT)$(sysconfdir)/fonts/conf.d

install-exec:	all
	echo Installing filters to $(SERVERBIN)/filter...
	$(INSTALL_DIR) -m 755 $(SERVERBIN)/filter
	$(INSTALL_BIN) pdftoopvp $(SERVERBIN)/filter

install-headers:

install-libs:

#
# Uninstall the filter...
#

uninstall:
	$(RM) $(SERVERBIN)/filter/pdftoopvp
	-$(RMDIR) $(SERVERBIN)/filter
	-$(RMDIR) $(SERVERBIN)
	$(RM) $(BUILDROOT)$(sysconfdir)/fonts/conf.d/99pdftoopvp.conf
	-$(RMDIR) $(BUILDROOT)$(sysconfdir)/fonts/conf.d
	-$(RMDIR) $(BUILDROOT)$(sysconfdir)/fonts


#
# pdftoopvp
#

oprs/liboprs.a:
	\$(MAKE) -C oprs

pdftoopvp:	pdftoopvp.o $(LIBOBJS) oprs/liboprs.a
	echo Linking $@...
	$(CXX) $(LDFLAGS) -o $@ pdftoopvp.o $(LIBOBJS) oprs/liboprs.a $(POPPLER_LIBS) $(FREETYPE_LIBS) $(FONTCONFIG_LIBS) $(CXXLIBS) -lpng -ldl


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

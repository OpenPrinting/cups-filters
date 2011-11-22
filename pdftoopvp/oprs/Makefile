
include ../../Makedefs

#
# Object files...
#

LIBOBJS =       \
		OPRS.o \
		OPVPSplash.o \
		OPVPSplashState.o \
		OPVPWrapper_0_2.o \
		OPVPSplashClip.o \
		OPVPSplashPath.o \
		OPVPSplashXPath.o \
		OPVPWrapper.o

OBJS    =	$(LIBOBJS)

TARGETS =	liboprs.a

LIB    =	liboprs.a

#
# Make everything...
#

all:    $(TARGETS)


#
# Clean all object files...
#

clean:
	$(RM) $(OBJS)
	$(RM) $(LIB)


#
# Update dependencies (without system header dependencies...)
#

depend:
	makedepend -Y -I. -I../.. -I../opvp -fDependencies $(OBJS:.o=.cxx) >/dev/null 2>&1


#
# liboprs.a
#

$(LIB): $(LIBOBJS)
	$(AR) rcu $@ $^ 


#
# Dependencies...
#

include Dependencies


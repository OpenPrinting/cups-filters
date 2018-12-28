#ifndef OPVPSPLASHXPATH_H
#define OPVPSPLASHXPATH_H

#include <config.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif
#include "splash/SplashXPath.h"
#include "OPVPSplashPath.h"
#include "OPVPSplashState.h"

class OPVPSplash;

class OPVPSplashXPath : public SplashXPath {
public:
  OPVPSplashXPath(OPVPSplashPath *path, SplashCoord *matrix,
                SplashCoord flatness, bool closeSubpaths) :
	SplashXPath(path,matrix,flatness,closeSubpaths) {
  }

  // Copy an expanded path.
  OPVPSplashXPath *copy() { return new OPVPSplashXPath(this); }

  OPVPSplashXPath *makeDashedPath(OPVPSplashState *state);
  void strokeNarrow(OPVPSplash *splash, OPVPSplashState *state);
private:
  OPVPSplashXPath() : SplashXPath(new SplashPath(), 0, 0, false) {};
  OPVPSplashXPath(OPVPSplashXPath *xPath) : SplashXPath(xPath) {
  }
};

#endif

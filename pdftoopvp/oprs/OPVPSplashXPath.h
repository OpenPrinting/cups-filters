#ifndef OPVPSPLASHXPATH_H
#define OPVPSPLASHXPATH_H

#include "splash/SplashXPath.h"
#include "OPVPSplashPath.h"
#include "OPVPSplashState.h"

class OPVPSplash;

class OPVPSplashXPath : public SplashXPath {
public:
  OPVPSplashXPath(OPVPSplashPath *path, SplashCoord *matrix,
                SplashCoord flatness, GBool closeSubpaths) :
	SplashXPath(path,matrix,flatness,closeSubpaths) {
  }

  // Copy an expanded path.
  OPVPSplashXPath *copy() { return new OPVPSplashXPath(this); }

  OPVPSplashXPath *makeDashedPath(OPVPSplashState *state);
  void strokeNarrow(OPVPSplash *splash, OPVPSplashState *state);
  void strokeWide(OPVPSplash *splash, OPVPSplashState *state);
private:
  OPVPSplashXPath() {};
  OPVPSplashXPath(OPVPSplashXPath *xPath) : SplashXPath(xPath) {
  }
};

#endif

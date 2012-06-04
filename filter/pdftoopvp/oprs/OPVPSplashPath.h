#ifndef OPVPSPLASHPATH_H
#define OPVPSPLASHPATH_H

#include "splash/SplashPath.h"
#include "OPVPWrapper.h"

class OPVPSplashPath : public SplashPath {
public:

  OPVPSplashPath() {};

  OPVPSplashPath(SplashPath *spath) : SplashPath(spath) {
  }

  // Copy a path.
  OPVPSplashPath *copy() { return new OPVPSplashPath(this); }
  
  void getBBox(int *xMinA, int *yMinA, int *xMaxA, int *yMaxA);
  GBool isRectanglePath(SplashCoord *xMin, SplashCoord *yMin,
    SplashCoord *xMax, SplashCoord *yMax);
  SplashError makePath(OPVPWrapper *opvp);
  void closeAllSubPath();
private:
  OPVPSplashPath(OPVPSplashPath *path) : SplashPath(path) {
  }
};

#endif

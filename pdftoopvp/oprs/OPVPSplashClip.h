#ifndef OPVPSPLASHCLIP_H
#define OPVPSPLASHCLIP_H

#include "splash/SplashClip.h"
#include "OPVPSplashPath.h"

class OPVPSplashClip : public SplashClip {
public:

  OPVPSplashClip(SplashCoord x0, SplashCoord y0,
               SplashCoord x1, SplashCoord y1,
	                    GBool antialiasA) :
     SplashClip(x0,y0,x1,y1,antialiasA) {
  }

  OPVPSplashClip(SplashClip *sclip) : SplashClip(sclip) {
  }

  OPVPSplashClip *copy() { return new OPVPSplashClip(this); }

  ~OPVPSplashClip() {}
  
  void getBBox(int *xMinA, int *yMinA, int *xMaxA, int *yMaxA);
  OPVPSplashPath *makePath();
private:
  OPVPSplashClip(OPVPSplashClip *clip) : SplashClip(clip) {
  }
};

#endif

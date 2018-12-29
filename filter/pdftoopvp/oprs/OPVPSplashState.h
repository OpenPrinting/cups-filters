//========================================================================
//
// OPVPSplashState.h
//
//========================================================================

#ifndef OPVPSPLASHSTATE_H
#define OPVPSPLASHSTATE_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif
#include "splash/SplashTypes.h"
#include "splash/SplashState.h"
#include "splash/Splash.h"

class SplashPattern;
class SplashScreen;
class OPVPSplashClip;
class SplashBitmap;

//------------------------------------------------------------------------
// SplashState
//------------------------------------------------------------------------

class OPVPSplashState {
public:

  // Create a new state object, initialized with default settings.
  OPVPSplashState(int width, int height, bool vectorAntialias,
	      SplashScreenParams *screenParams);
  OPVPSplashState(int width, int height, bool vectorAntialias,
	      SplashScreen *screenA);

  // Copy a state object.
  OPVPSplashState *copy() { return new OPVPSplashState(this); }

  ~OPVPSplashState();

  void setState(Splash *osplash);

  // Set the stroke pattern.  This does not copy <strokePatternA>.
  void setStrokePattern(SplashPattern *strokePatternA);

  // Set the fill pattern.  This does not copy <fillPatternA>.
  void setFillPattern(SplashPattern *fillPatternA);

  // Set the screen.  This does not copy <screenA>.
  void setScreen(SplashScreen *screenA);

  // Set the line dash pattern.  This copies the <lineDashA> array.
  void setLineDash(SplashCoord *lineDashA, int lineDashLengthA,
		   SplashCoord lineDashPhaseA);

  // Set the soft mask bitmap.
  void setSoftMask(SplashBitmap *softMaskA);

private:

  OPVPSplashState(OPVPSplashState *state);

  SplashCoord matrix[6];
  SplashPattern *strokePattern;
  SplashPattern *fillPattern;
  SplashScreen *screen;
  SplashBlendFunc blendFunc;
  SplashCoord strokeAlpha;
  SplashCoord fillAlpha;
  SplashCoord lineWidth;
  int lineCap;
  int lineJoin;
  SplashCoord miterLimit;
  SplashCoord flatness;
  SplashCoord *lineDash;
  int lineDashLength;
  SplashCoord lineDashPhase;
  bool strokeAdjust;
  OPVPSplashClip *clip;
  SplashBitmap *softMask;
  bool deleteSoftMask;
  bool inNonIsolatedGroup;

  OPVPSplashState *next;	// used by OPVPSplash class

  friend class OPVPSplash;
  friend class OPVPSplashXPath;
};

#endif

//========================================================================
//
// OPVPSplash.cc
//
//========================================================================

#include <config.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "goo/gmem.h"
#include "splash/SplashErrorCodes.h"
#include "splash/SplashMath.h"
#include "splash/SplashBitmap.h"
#include "splash/SplashXPathScanner.h"
#include "splash/SplashPattern.h"
#include "splash/SplashScreen.h"
#include "splash/SplashFont.h"
#include "splash/SplashGlyphBitmap.h"
#include "splash/Splash.h"
#include "OPRS.h"
#include "OPVPSplashState.h"
#include "OPVPSplash.h"
#include "OPVPSplashPath.h"
#include "OPVPSplashXPath.h"
#include "OPVPSplashClip.h"

//------------------------------------------------------------------------
// OPVPSplash
//------------------------------------------------------------------------

inline void OPVPSplash::transform(SplashCoord *matrix,
                              SplashCoord xi, SplashCoord yi,
                              SplashCoord *xo, SplashCoord *yo) {
  //                          [ m[0] m[1] 0 ]
  // [xo yo 1] = [xi yi 1] *  [ m[2] m[3] 0 ]
  //                          [ m[4] m[5] 1 ]
  *xo = xi * matrix[0] + yi * matrix[2] + matrix[4];
  *yo = xi * matrix[1] + yi * matrix[3] + matrix[5];
}

OPVPSplash::OPVPSplash(OPVPWrapper *opvpA,
  int nOptions, const char *optionKeys[], const char *optionVals[])
{
  const char *opv;

  opvp = opvpA;
  // with default screen params
  state = new OPVPSplashState(0,0,gFalse,(SplashScreenParams *)NULL);
  debugMode = gFalse;
  stateBypass = gFalse;
  clipPath = 0;
  if (getOption("OPVP_OLDLIPSDRIVER",nOptions,
     optionKeys,optionVals) != NULL) {
    oldLipsDriver = gTrue;
  } else {
    oldLipsDriver = gFalse;
  }
  if (getOption("OPVP_CLIPPATHNOTSAVED",nOptions,
     optionKeys,optionVals) != NULL) {
    clipPathNotSaved = gTrue;
  } else {
    clipPathNotSaved = gFalse;
  }
  if (getOption("OPVP_NOSHEARIMAGE",nOptions,
     optionKeys,optionVals) != NULL) {
    noShearImage = gTrue;
  } else {
    noShearImage = gFalse;
  }
  if (getOption("OPVP_NOLINESTYLE",nOptions,
     optionKeys,optionVals) != NULL) {
    noLineStyle = gTrue;
  } else {
    noLineStyle = gFalse;
  }
  if (!opvpA->supportSetLineStyle || !opvpA->supportSetLineDash
     || !opvpA->supportSetLineDashOffset) {
    noLineStyle = gTrue;
  }
  if (getOption("OPVP_NOCLIPPATH",nOptions,
     optionKeys,optionVals) != NULL) {
    noClipPath = gTrue;
  } else {
    noClipPath = gFalse;
  }
  if (getOption("OPVP_IGNOREMITERLIMIT",nOptions,
     optionKeys,optionVals) != NULL) {
    ignoreMiterLimit = gTrue;
  } else {
    ignoreMiterLimit = gFalse;
  }
  if (getOption("OPVP_NOMITERLIMIT",nOptions,
     optionKeys,optionVals) != NULL) {
    noMiterLimit = gTrue;
  } else {
    noMiterLimit = gFalse;
  }
  if (!opvpA->supportSetMiterLimit) {
    noMiterLimit = gTrue;
  }
  if ((opv = getOption("OPVP_BITMAPCHARTHRESHOLD",nOptions,
     optionKeys,optionVals)) != NULL) {
    bitmapCharThreshold = atoi(opv);
  } else {
    bitmapCharThreshold = OPVP_BITMAPCHAR_THRESHOLD;
  }
  if ((opv = getOption("OPVP_MAXCLIPPATHLENGTH",nOptions,
     optionKeys,optionVals)) != NULL) {
    maxClipPathLength = atoi(opv);
  } else {
    maxClipPathLength = OPVP_MAX_CLIPPATH_LENGTH;
  }
  if ((opv = getOption("OPVP_MAXFILLPATHLENGTH",nOptions,
     optionKeys,optionVals)) != NULL) {
    maxFillPathLength = atoi(opv);
  } else {
    maxFillPathLength = OPVP_MAX_FILLPATH_LENGTH;
  }
  if (getOption("OPVP_NOIMAGEMASK",nOptions,
     optionKeys,optionVals) != NULL) {
    noImageMask = gTrue;
  } else {
    noImageMask = gFalse;
  }
  if (getOption("OPVP_NOBITMAPCHAR",nOptions,
     optionKeys,optionVals) != NULL) {
    bitmapCharThreshold = 0;
  }
  if (!opvpA->supportSetClipPath) {
    noClipPath = gTrue;
  }
  savedNoClipPath = noClipPath;
  saveDriverStateCount = 0;
  if (noImageMask) {
    /* We draw bitmapChar with imageMask feature.
      So, when noImageMask, noBitmapChar */
    bitmapCharThreshold = 0;
  }
#ifdef OPTION_DEBUG
fprintf(stderr,"noClipPath=%d\n",noClipPath);
fprintf(stderr,"oldLipsDriver=%d\n",oldLipsDriver);
fprintf(stderr,"noLineStyle=%d\n",noLineStyle);
fprintf(stderr,"noMiterLimit=%d\n",noMiterLimit);
fprintf(stderr,"ignoreMiterLimit=%d\n",ignoreMiterLimit);
fprintf(stderr,"noShearImage=%d\n",noShearImage);
fprintf(stderr,"clipPathNotSaved=%d\n",clipPathNotSaved);
fprintf(stderr,"bitmapCharThreshold=%d\n",bitmapCharThreshold);
fprintf(stderr,"maxClipPathLength=%d\n",maxClipPathLength);
#endif
}

OPVPSplash::~OPVPSplash()
{
  while (state->next) {
    restoreState();
  }
  delete state;
  if (opvp->supportClosePrinter) {
    opvp->ClosePrinter();
  }
  delete opvp;
}

//------------------------------------------------------------------------
// state read
//------------------------------------------------------------------------


SplashPattern *OPVPSplash::getStrokePattern() {
  return state->strokePattern;
}

SplashPattern *OPVPSplash::getFillPattern() {
  return state->fillPattern;
}

SplashScreen *OPVPSplash::getScreen() {
  return state->screen;
}

SplashCoord OPVPSplash::getLineWidth() {
  return state->lineWidth;
}

int OPVPSplash::getLineCap() {
  return state->lineCap;
}

int OPVPSplash::getLineJoin() {
  return state->lineJoin;
}

SplashCoord OPVPSplash::getMiterLimit() {
  return state->miterLimit;
}

SplashCoord OPVPSplash::getFlatness() {
  return state->flatness;
}

SplashCoord *OPVPSplash::getLineDash() {
  return state->lineDash;
}

int OPVPSplash::getLineDashLength() {
  return state->lineDashLength;
}

SplashCoord OPVPSplash::getLineDashPhase() {
  return state->lineDashPhase;
}

OPVPSplashClip *OPVPSplash::getClip() {
  return state->clip;
}

//------------------------------------------------------------------------
// state write
//------------------------------------------------------------------------

opvp_cspace_t OPVPSplash::getOPVPColorSpace()
{
  switch (colorMode) {
  case splashModeMono1:
    return OPVP_CSPACE_BW;
    break;
  case splashModeMono8:
    return OPVP_CSPACE_DEVICEGRAY;
    break;
  case splashModeRGB8:
  default:
    break;
  }
  return OPVP_CSPACE_STANDARDRGB;
}

void OPVPSplash::makeBrush(SplashPattern *pattern, opvp_brush_t *brush)
{
  brush->colorSpace = getOPVPColorSpace();
  brush->pbrush = NULL;
  brush->color[3] = -1;
  brush->xorg = brush->yorg = 0;
  if (pattern == NULL) {
    /* set default black color */
    brush->color[2] = 0;
    brush->color[1] = 0;
    brush->color[0] = 0;
  } else if (typeid(*pattern) == typeid(SplashSolidColor)) {
    /* solid color */
    SplashColor color;

    pattern->getColor(0,0,color);
    switch (colorMode) {
    case splashModeMono1:
      brush->color[2] = color[0];
      brush->color[1] = 0;
      brush->color[0] = 0;
      break;
    case splashModeMono8:
      brush->color[2] = color[0];
      brush->color[1] = 0;
      brush->color[0] = 0;
      break;
    case splashModeRGB8:
      brush->color[2] = splashRGB8R(color);
      brush->color[1] = splashRGB8G(color);
      brush->color[0] = splashRGB8B(color);
      break;
    default:
      OPRS::error("Unknown color mode\n");
      brush->color[2] = splashRGB8R(color);
      brush->color[1] = splashRGB8G(color);
      brush->color[0] = splashRGB8B(color);
      break;
    }
  } else {
    /* error */
    return;
  }
}

GBool OPVPSplash::equalPattern(SplashPattern *pat1, SplashPattern *pat2)
{
  SplashColor c1, c2;
  if (pat1 == NULL || pat2 == NULL) {
    return pat1 == pat2;
  }
  if (typeid(*pat1) != typeid(*pat2)) return gFalse;

  pat1->getColor(0,0,c1);
  pat2->getColor(0,0,c2);
  switch (colorMode) {
  case splashModeMono1:
    return c1[0] == c2[0];
    break;
  case splashModeMono8:
    return c1[0] == c2[0];
    break;
  case splashModeRGB8:
    return c1[0] == c2[0] && c1[1] == c2[1] && c1[2] == c2[2];
    break;
  default:
    break;
  }
  return gTrue;
}

void OPVPSplash::setStrokePattern(SplashPattern *strokePattern) {
  opvp_brush_t brush;

  if (!stateBypass && equalPattern(strokePattern,state->strokePattern)) {
    delete strokePattern;
    return;
  }
  state->setStrokePattern(strokePattern);
  makeBrush(strokePattern,&brush);
  if (opvp->SetStrokeColor(&brush) != 0) {
    OPRS::error("SetStrokeColor error\n");
    return;
  }
}

void OPVPSplash::setFillPattern(SplashPattern *fillPattern) {
  opvp_brush_t brush;

  if (!stateBypass && equalPattern(fillPattern,state->fillPattern)) {
    delete fillPattern;
    return;
  }
  state->setFillPattern(fillPattern);
  makeBrush(fillPattern,&brush);
  if (opvp->SetFillColor(&brush) != 0) {
    OPRS::error("SetFillColor error\n");
    return;
  }
}

void OPVPSplash::setScreen(SplashScreen *screen) {
  state->setScreen(screen);
}

void OPVPSplash::setLineWidth(SplashCoord lineWidth) {
  if (stateBypass || state->lineWidth != lineWidth) {
      opvp_fix_t width;

      state->lineWidth = lineWidth;
      OPVP_F2FIX(lineWidth,width);
      if (opvp->SetLineWidth(width) < 0) {
	OPRS::error("SetLineWidth error\n");
	return;
      }
  }
}

void OPVPSplash::setLineCap(int lineCap) {
  if (stateBypass || state->lineCap != lineCap) {
      opvp_linecap_t cap;

      state->lineCap = lineCap;
      switch (lineCap) {
      case splashLineCapButt:
	cap = OPVP_LINECAP_BUTT;
	break;
      case splashLineCapRound:
	cap = OPVP_LINECAP_ROUND;
	break;
      case splashLineCapProjecting:
	cap = OPVP_LINECAP_SQUARE;
	break;
      default:
	/* error */
	cap = OPVP_LINECAP_BUTT;
	break;
      }
      if (opvp->SetLineCap(cap) < 0) {
	OPRS::error("SetLineCap error\n");
	return;
      }
  }
}

void OPVPSplash::setLineJoin(int lineJoin) {
  if (stateBypass || state->lineJoin != lineJoin) {
      opvp_linejoin_t join;

      state->lineJoin = lineJoin;
      switch (lineJoin) {
      case splashLineJoinMiter:
	join = OPVP_LINEJOIN_MITER;
	break;
      case splashLineJoinRound:
	join = OPVP_LINEJOIN_ROUND;
	break;
      case splashLineJoinBevel:
	join = OPVP_LINEJOIN_BEVEL;
	break;
      default:
	/* error */
	join = OPVP_LINEJOIN_MITER;
	break;
      }
      if (opvp->SetLineJoin(join) < 0) {
	OPRS::error("SetLineJoin error\n");
	return;
      }
  }
}

void OPVPSplash::setMiterLimit(SplashCoord miterLimit) {
  if (stateBypass || state->miterLimit != miterLimit) {
      opvp_fix_t limit;

      state->miterLimit = miterLimit;
      if (noMiterLimit) return;
      if (oldLipsDriver) {
	/* for old driver for lips */
	/* miterLimit is length/2 */
	miterLimit = miterLimit*state->lineWidth*0.5;
      }
      OPVP_F2FIX(miterLimit,limit);
      if (opvp->SetMiterLimit(limit) < 0) {
	OPRS::error("SetMiterLimit error\n");
	return;
      }
  }
}

void OPVPSplash::setFlatness(SplashCoord flatness) {
  if (flatness < 1) {
    state->flatness = 1;
  } else {
    state->flatness = flatness;
  }
}

void OPVPSplash::setLineDash(SplashCoord *lineDash, int lineDashLength,
			 SplashCoord lineDashPhase) {
  int i;
  opvp_fix_t *pdash;
  GBool equal;

  if (stateBypass || lineDash != state->lineDash) {
    if (lineDash == NULL || lineDashLength == 0) {
      if (!noLineStyle
        && opvp->SetLineStyle(OPVP_LINESTYLE_SOLID) < 0) {
	OPRS::error("SetLineStyle error\n");
	return;
      }
      state->setLineDash(lineDash, lineDashLength, lineDashPhase);
      return;
    } else if (stateBypass || state->lineDash == NULL) {
      if (!noLineStyle
        && opvp->SetLineStyle(OPVP_LINESTYLE_DASH) < 0) {
	OPRS::error("SetLineStyle error\n");
	return;
      }
    }
  }
  if (lineDash == NULL || lineDashLength == 0) return;
  if (!noLineStyle) {
    equal = (state->lineDash != NULL);
    pdash = new opvp_fix_t[lineDashLength];
    for (i = 0;i < lineDashLength;i++) {
      if (equal && lineDash[i] != state->lineDash[i]) equal = gFalse;
      OPVP_F2FIX(lineDash[i],pdash[i]);
    }
    if (!equal && opvp->SetLineDash(lineDashLength,pdash) < 0) {
      OPRS::error("SetLineDash error\n");
      goto err;
    }
    if (stateBypass || lineDashPhase != state->lineDashPhase) {
      opvp_fix_t offset;

      OPVP_F2FIX(lineDashPhase,offset);
      if (opvp->SetLineDashOffset(offset) < 0) {
	OPRS::error("SetLineDashOffset error\n");
	goto err;
      }
    }
err:
    delete[] pdash;
  }
  state->setLineDash(lineDash, lineDashLength, lineDashPhase);
}

SplashError OPVPSplash::doClipPath(OPVPSplashPath *path, GBool eo,
  OPVPClipPath *prevClip)
{
  SplashError result;

  if (path->getLength() > maxClipPathLength) {
    if (!noClipPath) {
      if (prevClip != 0 &&
          prevClip->getPath()->getLength() <= maxClipPathLength) {
	/* previous clipping is printer clipping */
	if (opvp->ResetClipPath() != 0) {
	      OPRS::error("ResetClipPath error\n");
	  return splashErrOPVP;
	}
      }
      noClipPath = gTrue;
    }
  } else {
    noClipPath = savedNoClipPath;
  }
  if (!noClipPath && path->getLength() > 0) {
    /* when path->length == 0, no drawable arae, and no output 
       so, it isn't need to set ClipPath */
    if ((result = path->makePath(opvp)) != splashOk) {
      return result;
    }
    if (opvp->SetClipPath(
	 eo ? OPVP_CLIPRULE_EVENODD : OPVP_CLIPRULE_WINDING) < 0) {
      OPRS::error("SetClipPath error\n");
      return splashErrOPVP;
    }
  }
  return splashOk;
}

SplashError OPVPSplash::makeRectanglePath(SplashCoord x0,
  SplashCoord y0, SplashCoord x1, SplashCoord y1, OPVPSplashPath **p)
{
  SplashError result;

  *p = new OPVPSplashPath();
  if ((result = (*p)->moveTo(x0,y0)) != splashOk) return result;
  if ((result = (*p)->lineTo(x1,y0)) != splashOk) return result;
  if ((result = (*p)->lineTo(x1,y1)) != splashOk) return result;
  if ((result = (*p)->lineTo(x0,y1)) != splashOk) return result;
  if ((result = (*p)->close()) != splashOk) return result;
  return splashOk;
}

void OPVPSplash::clipResetToRect(SplashCoord x0, SplashCoord y0,
			     SplashCoord x1, SplashCoord y1) {
  OPVPSplashPath *p;
  OPVPClipPath *cp;

  while ((cp = OPVPClipPath::pop()) != NULL) delete cp;
  if (clipPath != 0) {
    delete clipPath;
    clipPath = 0;
  }

  if (makeRectanglePath(x0,y0,x1,y1,&p) != splashOk) return;

  if (doClipPath(p,gTrue,clipPath) != splashOk) return;
  clipPath = new OPVPClipPath(p,gTrue);
  state->clip->resetToRect(x0, y0, x1, y1);
}

SplashError OPVPSplash::clipToPath(OPVPSplashPath *path, GBool eo) {
  SplashError result;
  SplashCoord x0, y0, x1, y1;
  SplashCoord x2, y2, x3, y3;
  SplashClipResult clipResult;
  int xMin, yMin, xMax, yMax;

  if (path == 0) return splashErrBogusPath;
  if (path->getLength() == 0) return splashOk;
  if (clipPath == 0) {
    /* no clip region exist */
    if ((result = state->clip->clipToPath(path, state->matrix,
                   state->flatness, eo)) != splashOk) {
      return result;
    }
    path = path->copy();
  } else {
    OPVPSplashPath *oldPath = clipPath->getPath();
    if (path->isRectanglePath(&x0,&y0,&x1,&y1)) {
      if ((clipResult = state->clip->testRect(
         splashRound(x0), splashRound(y0), splashRound(x1), splashRound(y1)))
	 == splashClipAllOutside) {
	 /* no drawable area */
	if ((result = state->clip->clipToPath(path, state->matrix,
	      state->flatness, eo)) != splashOk) {
	  return result;
	}
	path = new OPVPSplashPath();
      } else if (clipResult == splashClipPartial) {
	if (oldPath->isRectanglePath(&x2,&y2,&x3,&y3)) {
	  if ((result = state->clip->clipToPath(path, state->matrix,
	       state->flatness, eo)) != splashOk) {
	    return result;
	  }
	  /* both rectangle */
	  if (x0 < x2) x0 = x2;
	  if (y0 < y2) y0 = y2;
	  if (x1 > x3) x1 = x3;
	  if (y1 > y3) y1 = y3;
	  if ((result = makeRectanglePath(x0,y0,x1,y1,&path)) != splashOk) {
	    return result;
	  }
	} else {
	  state->clip->getBBox(&xMin,&yMin,&xMax,&yMax);
	  if (splashRound(x0) <= xMin && splashRound(y0) <= yMin
	      && splashRound(x1) >= xMax && splashRound(y1)) {
		/* The old path is all inside the new path */
		/* We may ignore the new path */
	    return splashOk;
	  }
	  if ((result = state->clip->clipToPath(path, state->matrix, 
	       state->flatness, eo)) != splashOk) {
	    return result;
	  }
	  if (state->clip->getNumPaths() > 0) {
	      path = state->clip->makePath();
	  } else {
	      path = new OPVPSplashPath();
	  }
	}
      } else {
	/* splashClipAllInside */
	/* We may ignore the previous region. */
	if ((result = state->clip->clipToPath(path, state->matrix,
	     state->flatness, eo)) != splashOk) {
	  return result;
	}
	path = path->copy();
      }
    } else {
      /* non rectangle path */

      OPVPSplashXPath *xpath = new OPVPSplashXPath(path, state->matrix, 
                                     state->flatness, gFalse);

      xpath->sort();
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
      SplashXPathScanner *scanner = new SplashXPathScanner(xpath,eo,
        INT_MIN,INT_MAX);
#else
      SplashXPathScanner *scanner = new SplashXPathScanner(xpath,eo);
#endif
      scanner->getBBox(&xMin,&yMin,&xMax,&yMax);
      delete scanner;
      delete xpath;
      if ((clipResult = state->clip->testRect(xMin,yMin,xMax,yMax))
         == splashClipAllOutside) {
	 /* no efect */
	 /* no drawable area */
	if ((result = state->clip->clipToPath(path, state->matrix,
	     state->flatness, eo)) != splashOk) {
	  return result;
	}
	path = new OPVPSplashPath();
      } else if (clipResult == splashClipPartial) {
	OPVPSplashClip *nclip = new OPVPSplashClip(xMin,yMin,xMax,yMax,gFalse);
	nclip->clipToPath(path,state->matrix,state->flatness,eo);
	state->clip->getBBox(&xMin,&yMin,&xMax,&yMax);
	if ((clipResult = nclip->testRect(xMin,yMin,xMax,yMax))
	   == splashClipAllOutside) {
	  /* no drawable area */
	  delete nclip;
	  if ((result = state->clip->clipToPath(path, state->matrix,
	         state->flatness, eo)) != splashOk) {
	    return result;
	  }
	  path = new OPVPSplashPath();
	} else {
	    delete nclip;
	    if (clipResult == splashClipAllInside) {
	      /* The old path is all inside the new path */
	      /* We may ignore the new path */
	      return splashOk;
	    }
	    if ((result = state->clip->clipToPath(path, state->matrix,
	         state->flatness, eo)) != splashOk) {
	      return result;
	    }
	    if (state->clip->getNumPaths() > 0) {
	      path = state->clip->makePath();
	    } else {
	      path = new OPVPSplashPath();
	    }
	}
      } else {
	/* splashClipAllInside */
	/* We may ignore the previous region. */
	if ((result = state->clip->clipToPath(path, state->matrix,
	      state->flatness, eo)) != splashOk) {
	  return result;
	}
	path = path->copy();
      }
    }
  }
  if ((result = doClipPath(path,eo,clipPath)) != splashOk) {
    delete path;
    return result;
  }
  if (clipPath != 0) delete clipPath;
  clipPath = new OPVPClipPath(path,eo);

  return splashOk;
}

//------------------------------------------------------------------------
// state save/restore
//------------------------------------------------------------------------

void OPVPSplash::saveState() {
  OPVPSplashState *newState;

  newState = state->copy();
  newState->next = state;
  state = newState;
  if (clipPath != 0) clipPath->push();
  if (opvp->SaveGS() != 0) {
    OPRS::error("SaveGS error\n");
    return;
  }
  saveDriverStateCount++;
}

SplashError OPVPSplash::restoreState() {
  OPVPSplashState *oldState;
  OPVPClipPath *oldClip;
  OPVPSplashPath *path;
  GBool saved = gFalse;

  if (!state->next) {
    return splashErrNoSave;
  }
  oldState = state;
  state = state->next;
  delete oldState;
  if (saveDriverStateCount > 0 && opvp->RestoreGS() != 0) {
    OPRS::error("RestoreGS error\n");
    return splashErrOPVP;
  }
  saveDriverStateCount--;
  oldClip = clipPath;
  if (clipPath != 0) {
    saved = clipPath->getSaved();
    delete clipPath;
    clipPath = 0;
  }
  clipPath = OPVPClipPath::pop();
  if (clipPath != 0) {
    path = clipPath->getPath();
    if (path->getLength() > maxClipPathLength) {
      if (clipPathNotSaved && !noClipPath) {
	if (opvp->ResetClipPath() != 0) {
	      OPRS::error("ResetClipPath error\n");
	    return splashErrOPVP;
	}
	noClipPath = gTrue;
      }
    } else {
      noClipPath = savedNoClipPath;
    }
  } else {
    noClipPath = savedNoClipPath;
  }
  if (clipPathNotSaved && !noClipPath) {
    if (clipPath != 0) {
      if (!saved) {
	  SplashError result;

	  if ((result = doClipPath(clipPath->getPath(),clipPath->getEo(),
	           oldClip))
		!= splashOk) return result;
      }
    } else if (oldClip != 0) {
      if (opvp->ResetClipPath() != 0) {
	OPRS::error("ResetClipPath error\n");
	return splashErrOPVP;
      }
    }
  }
  return splashOk;
}

//------------------------------------------------------------------------
// drawing operations
//------------------------------------------------------------------------

void OPVPSplash::clear(SplashColor color)
{
  opvp_brush_t brush;

  brush.colorSpace = getOPVPColorSpace();
  brush.pbrush = NULL;
  brush.color[3] = -1;
  brush.xorg = brush.yorg = 0;
  switch (colorMode) {
  case splashModeMono1:
    brush.color[2] = color[0];
    brush.color[1] = 0;
    brush.color[0] = 0;
    break;
  case splashModeMono8:
    brush.color[2] = color[0];
    brush.color[1] = 0;
    brush.color[0] = 0;
    break;
  case splashModeRGB8:
    brush.color[2] = splashRGB8R(color);
    brush.color[1] = splashRGB8G(color);
    brush.color[0] = splashRGB8B(color);
    break;
  default:
    OPRS::error("Unknown color mode\n");
    brush.color[2] = splashRGB8R(color);
    brush.color[1] = splashRGB8G(color);
    brush.color[0] = splashRGB8B(color);
    break;
  }
  opvp->SetBgColor(&brush);
}

/*
  Translate arc to Bezier Curve

  input start point (x0,y0) , center (cx, cy) and end point (x3, y3)
  return Bezier curve control points (rx1,ry1 and rx2, ry2)

  an angle should be less than eqaul 90 degree

*/
void OPVPSplash::arcToCurve(SplashCoord x0, SplashCoord y0,
  SplashCoord x3, SplashCoord y3,
  SplashCoord cx, SplashCoord cy, SplashCoord *rx1, SplashCoord *ry1,
  SplashCoord *rx2, SplashCoord *ry2)
{
#define ROTX(x,y) (x*rotcos-y*rotsin)*r+cx
#define ROTY(x,y) (x*rotsin+y*rotcos)*r+cy

  SplashCoord x1,y1,x2,y2;
  SplashCoord r;
  SplashCoord rotcos, rotsin;
  SplashCoord ox,oy,hx,hy,d;

  hx = (x0+x3)/2;
  hy = (y0+y3)/2;
  r = splashDist(x0,y0,cx,cy);
  d = splashDist(x0,y0,hx,hy);
  rotcos = (hx-cx)/d;
  rotsin = (hy-cy)/d;
  oy = (splashDist(x0,y0,x3,y3)/2)/r;
  ox = splashDist(hx,hy,cx,cy)/r;
  x1 = ((4-ox)/3);
  y1 = ((1-ox)*(3-ox)/(3*oy));
  x2 = x1;
  y2 = -y1;
  *rx1 = ROTX(x1,y1);
  *ry1 = ROTY(x1,y1);
  *rx2 = ROTX(x2,y2);
  *ry2 = ROTY(x2,y2);
#undef ROTX
#undef ROTY
}

SplashError OPVPSplash::strokeByMyself(OPVPSplashPath *path)
{
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  SplashPath *dPath;
  OPVPSplashPath *oPath;
  Splash *osplash;
  SplashPattern *savedPattern;

  /* draw dashed line by myself */
  if (path->getLength() == 0) {
    return splashOk;
  }

  osplash = new Splash(new SplashBitmap(1,1,4,splashModeMono1,gFalse),gFalse);
  state->setState(osplash);
  dPath = osplash->makeStrokePath(path,state->lineWidth);
  oPath = new OPVPSplashPath(dPath);
  delete dPath;

  if (state->lineWidth <= 1) {
    OPVPSplashXPath *xPath;
    xPath = new OPVPSplashXPath(oPath, state->matrix, state->flatness, gFalse);
    xPath->strokeNarrow(this,state);
    delete xPath;
  } else {
    /* change fill pattern temprarily */
    savedPattern = state->fillPattern->copy();
    setFillPattern(state->strokePattern->copy());

    fillByMyself(oPath,gFalse);

    /* restore fill pattern */
    setFillPattern(savedPattern);
  }
  delete osplash;
  return splashOk;
#else
  OPVPSplashXPath *xPath, *xPath2;
  SplashPattern *savedPattern;

  /* draw dashed line by myself */
  if (path->getLength() == 0) {
    return splashOk;
  }
  xPath = new OPVPSplashXPath(path, state->matrix, state->flatness, gFalse);
  if (state->lineDash != NULL && state->lineDashLength > 0) {
    xPath2 = xPath->makeDashedPath(state);
    delete xPath;
    xPath = xPath2;
  }

  if (state->lineWidth <= 1) {
    xPath->strokeNarrow(this,state);
  } else {
    /* change fill pattern temprarily */
    savedPattern = state->fillPattern->copy();
    setFillPattern(state->strokePattern->copy());

    xPath->strokeWide(this,state);

    /* restore fill pattern */
    setFillPattern(savedPattern);
  }

  delete xPath;
  return splashOk;
#endif
}

SplashError OPVPSplash::stroke(OPVPSplashPath *path) {
  SplashError result;

  if (clipPath != 0 && clipPath->getPath()->getLength() == 0) {
      return splashOk;
  }
  if ((state->lineDash != NULL
     && state->lineDashLength > 0 && noLineStyle)) {
    return strokeByMyself(path);
  }
  if (noMiterLimit && (!ignoreMiterLimit) && state->lineWidth != 0
      && state->lineJoin == splashLineJoinMiter) {
    return strokeByMyself(path);
  }
  if (noClipPath) {
    int xMin, yMin, xMax, yMax;
    SplashClipResult clipResult;
    int fatOffset = splashCeil(state->lineWidth/2);
    int miterLimit = splashCeil(state->miterLimit/2);

    if (fatOffset < miterLimit) fatOffset = miterLimit;
    path->getBBox(&xMin,&yMin,&xMax,&yMax);
    xMin -= fatOffset;
    yMin -= fatOffset;
    xMax += fatOffset;
    yMax += fatOffset;
    clipResult = state->clip->testRect(xMin,yMin,xMax,yMax);
    if (clipResult == splashClipAllOutside) {
      /* not need to draw */
      return splashOk;
    } else if (clipResult == splashClipPartial) {
      return strokeByMyself(path);
    }
    /* splashClipAllInside */
    /* fall through */
  }
  if ((result = path->makePath(opvp)) != 0) return result;
  if (opvp->StrokePath() < 0) {
    OPRS::error("StrokePath error\n");
    return splashErrOPVP;
  }
  return splashOk;
}

SplashError OPVPSplash::fillByMyself(OPVPSplashPath *path, GBool eo)
{
  OPVPSplashXPath *xPath;
  SplashXPathScanner *scanner;
  int xMinI, yMinI, xMaxI, yMaxI, x0, x1, y;
  SplashClipResult clipRes, clipRes2;

  if (path->getLength() == 0) {
    return splashOk;
  }
  xPath = new OPVPSplashXPath(path, state->matrix, state->flatness, gTrue);
  xPath->sort();
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  scanner = new SplashXPathScanner(xPath, eo, INT_MIN, INT_MAX);
#else
  scanner = new SplashXPathScanner(xPath, eo);
#endif

  // get the min and max x and y values
  scanner->getBBox(&xMinI, &yMinI, &xMaxI, &yMaxI);

  // check clipping
  if ((clipRes = state->clip->testRect(xMinI, yMinI, xMaxI, yMaxI))
      != splashClipAllOutside) {
    SplashPattern *savedPattern;

    /* change stroke pattern temporarily */
    savedPattern = state->strokePattern->copy();
    setStrokePattern(state->fillPattern->copy());

    for (y = yMinI; y < yMaxI; ++y) {
      while (scanner->getNextSpan(y, &x0, &x1)) {
        if (x0 == x1) continue;
	if (clipRes == splashClipAllInside) {
	  drawSpan(x0, x1-1, y, gTrue);
	} else {
	  clipRes2 = state->clip->testSpan(x0, x1, y);
	  drawSpan(x0, x1-1, y, clipRes2 == splashClipAllInside);
	}
      }
    }
    /* restore stroke pattern */
    setStrokePattern(savedPattern);
  }

  delete scanner;
  delete xPath;
  return splashOk;
}

SplashError OPVPSplash::fill(OPVPSplashPath *path, GBool eo) {
  SplashError result;
  opvp_fillmode_t mode;

  if (path->getLength() <= 1) return splashOk;
  if (clipPath != 0 && clipPath->getPath()->getLength() == 0) {
      return splashOk;
  }
  if (path->getLength() > maxFillPathLength) {
      return fillByMyself(path,eo);
  }
  if (noClipPath) {
    int xMin, yMin, xMax, yMax;
    SplashClipResult clipResult;

    path->getBBox(&xMin,&yMin,&xMax,&yMax);
    clipResult = state->clip->testRect(xMin,yMin,xMax,yMax);
    if (clipResult == splashClipAllOutside) {
      /* not need to draw */
      return splashOk;
    } else if (clipResult == splashClipPartial) {
      return fillByMyself(path,eo);
    }
    /* splashClipAllInside */
    /* fall through */
  }
  if ((result = path->makePath(opvp)) != 0) return result;
  mode = eo ? OPVP_FILLMODE_EVENODD : OPVP_FILLMODE_WINDING;
  if (opvp->SetFillMode(mode) < 0) {
    OPRS::error("SetFillMode error\n");
    return splashErrOPVP;
  }
  if (opvp->FillPath() < 0) {
    OPRS::error("FillPath error\n");
    return splashErrOPVP;
  }
  return splashOk;
}

void OPVPSplash::fillGlyph(SplashCoord x, SplashCoord y,
  SplashGlyphBitmap *glyph)
{
  opvp_fix_t opvpx,opvpy;
  int opvpbytes;
  int x0, y0;
  Guchar *bp;
  SplashClipResult clipRes;
  SplashCoord xt, yt;

  transform(state->matrix,x,y,&xt,&yt);
  x0 = splashFloor(xt)-glyph->x;
  y0 = splashFloor(yt)-glyph->y;
  clipRes = state->clip->testRect(x0,y0,
		     x0 + glyph->w - 1,
		     y0 + glyph->h - 1);
  if (clipRes == splashClipAllOutside) return;
  OPVP_i2Fix((x0),(opvpx));
  OPVP_i2Fix((y0),(opvpy));
  if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
    OPRS::error("SetCurrentPoint error\n");
  }

  if (oldLipsDriver && (((glyph->w+7)/8) & 3) != 0) {
    /* not 4bytes aligned, so make aligned */
    int i;
    int m = (glyph->w+7)/8;
    
    opvpbytes = (m+3)/4;
    opvpbytes *= 4;
    bp = (Guchar *)gmallocn(glyph->h,opvpbytes);
    for (i = 0;i < glyph->h;i++) {
      memcpy(bp+i*opvpbytes,glyph->data+i*m,m);
    }
  } else {
    bp = glyph->data;
    opvpbytes = (glyph->w+7)/8;
  }
  if ((!noClipPath || clipRes != splashClipPartial) && !noImageMask) {
    if (opvp->DrawImage(glyph->w,glyph->h,opvpbytes,OPVP_IFORMAT_MASK,
	 glyph->w,glyph->h,(void *)bp) < 0) {
      OPRS::error("DrawImage error\n");
    }
  } else {
    int tx,ty;
    int sx = 0;
    SplashPattern *savedPattern;
    SplashCoord *savedLineDash = 0;
    int savedLineDashLength;
    SplashCoord savedLineDashPhase;
    SplashCoord savedLineWidth;

    /* change stroke pattern temprarily */
    savedPattern = state->strokePattern->copy();
    setStrokePattern(state->fillPattern->copy());
    /* change lins style temporarily */
    savedLineDashLength = state->lineDashLength;
    savedLineDashPhase = state->lineDashPhase;
    if (savedLineDashLength > 0 && state->lineDash != 0) {
      savedLineDash = new SplashCoord[savedLineDashLength];
      memcpy(savedLineDash, state->lineDash,
	savedLineDashLength*sizeof(SplashCoord));
    }
    setLineDash(0,0,0);
    savedLineWidth = state->lineWidth;
    setLineWidth(0.0);


    for (ty = 0;ty < glyph->h;ty++) {
      GBool dmode = gFalse;
      for (tx = 0;tx < glyph->w;tx++) {
	GBool on = (bp[opvpbytes*ty+(tx/8)] & (0x80 >> (tx & 7))) != 0;

	if (on && !dmode) {
	  sx = tx;
	  dmode = gTrue;
	} else if (!on && dmode) {
	  drawSpan(x0+sx,x0+tx-1,y0+ty,gTrue);
	  dmode = gFalse;
	}
      }
      if (dmode) {
	drawSpan(x0+sx,x0+tx-1,y0+ty,gTrue);
      }
    }
    /* restore stroke pattern */
    setStrokePattern(savedPattern);
    /* restore line style */
    setLineDash(savedLineDash,savedLineDashLength,
      savedLineDashPhase);
    if (savedLineDash != 0) {
      delete[] savedLineDash;
    }
    setLineWidth(savedLineWidth);
  }
  if (bp != glyph->data) gfree(bp);
}

SplashError OPVPSplash::fillChar(SplashCoord x, SplashCoord y,
			     int c, SplashFont *font,
			     Unicode *u, double *fontMat) {
  SplashError err = splashOk;
  SplashPath *spath;
  OPVPSplashPath *path;
  SplashCoord xt, yt;
  double mx,my;

  transform(state->matrix, x, y, &xt, &yt);
  if ((spath = font->getGlyphPath(c)) == 0) return splashOk;
  path = new OPVPSplashPath(spath);
  delete spath;
  if (bitmapCharThreshold > 0) {
    mx = splashAbs(fontMat[0]);
    if (mx < splashAbs(fontMat[1])) {
	mx = splashAbs(fontMat[1]);
    }
    my = splashAbs(fontMat[3]);
    if (my < splashAbs(fontMat[2])) {
	my = splashAbs(fontMat[2]);
    }
    if (path == 0 || (mx*my < bitmapCharThreshold)) {
      /* if a char is enough small, then out a char as a bitmask */
      SplashGlyphBitmap glyph;
      int x0, y0, xFrac, yFrac;
      SplashClipResult clipRes;

      x0 = splashFloor(xt);
      xFrac = splashFloor((xt - x0) * splashFontFraction);
      y0 = splashFloor(yt);
      yFrac = splashFloor((yt - y0) * splashFontFraction);
      if (font->getGlyph(c, xFrac, yFrac, &glyph, x0, y0, state->clip,
          &clipRes)) {
	if (path != 0) delete path;
	if (glyph.w == 0 || glyph.h == 0) {
	  /* empty glyph */
	  return splashOk;
	}
	if (clipRes != splashClipAllOutside) {
	    fillGlyph(xt, yt, &glyph);
	}
	if (glyph.freeData) {
	  gfree(glyph.data);
	}
	return err;
      }
    }
    /* fall through and out a char as a path */
  }
  if (path == 0) {
    //OPRS::error("FillPath error\n");
    err = splashErrOPVP;
    goto err0;
  }
  path->offset(xt,yt);
  err = fill(path,gFalse);
err0:
  if (path != 0) delete path;
  return err;
}

SplashError OPVPSplash::fillImageMaskFastWithCTM(SplashImageMaskSource src,
       void *srcData, int w, int h, int tx, int ty,SplashCoord *mat) {
  int i, j;
  opvp_fix_t opvpx,opvpy;
  int opvpbytes;
  opvp_ctm_t opvpctm;
  Guchar *buf = 0, *bp;
  SplashError result = splashOk;
  SplashColorPtr lineBuf;

  opvpbytes = (w+7)/8;
  /* align 4 */
  opvpbytes = (opvpbytes+3)/4;
  opvpbytes *= 4;
  buf = (Guchar *)gmallocn(h,opvpbytes);
  lineBuf = (SplashColorPtr)gmallocn(8,opvpbytes);

  for (i = 0;i < h;i++) {
    int k;

    bp = buf+opvpbytes*i;
    (*src)(srcData, lineBuf);
    for (j = 0;j < w;j += k) {
      Guchar d;

      d = 0;
      for (k = 0;k < 8 && j+k < w;k++) {
	d <<= 1;
	if (lineBuf[j+k] != 0) d |= 1;
      }
      d <<= 8-k;
      *bp++ = d;
    }
  }
  free(lineBuf);
  opvpctm.a = mat[0];
  opvpctm.b = mat[1];
  opvpctm.c = mat[2];
  opvpctm.d = mat[3];
  opvpctm.e = mat[4];
  opvpctm.f = mat[5];
  OPVP_i2Fix((tx),(opvpx));
  OPVP_i2Fix((ty),(opvpy));
  if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
    OPRS::error("SetCurrentPoint error\n");
  }

  if (opvp->SetCTM(&opvpctm) < 0) {
    OPRS::error("SetCTM error\n");
  }
  if (opvp->DrawImage(w,h,opvpbytes,OPVP_IFORMAT_MASK,1,1,(void *)(buf)) < 0) {
    OPRS::error("DrawImage error\n");
    result = splashErrOPVP;
  }
  /* reset CTM */
  opvpctm.a = 1.0;
  opvpctm.b = 0.0;
  opvpctm.c = 0.0;
  opvpctm.d = 1.0;
  opvpctm.e = 0.0;
  opvpctm.f = 0.0;
  if (opvp->SetCTM(&opvpctm) < 0) {
    OPRS::error("SetCTM error\n");
  }

  if (buf != 0) gfree(buf);
  return result;
}

SplashError OPVPSplash::fillImageMask(SplashImageMaskSource src, void *srcData,
			  int w, int h, SplashCoord *mat, GBool glyphMode) {
  GBool rot;
  SplashCoord xScale, yScale, xShear, yShear;
  int tx, ty, scaledWidth, scaledHeight, xSign, ySign;
  int ulx, uly, llx, lly, urx, ury, lrx, lry;
  int ulx1, uly1, llx1, lly1, urx1, ury1, lrx1, lry1;
  int xMin, xMax, yMin, yMax;
  SplashClipResult clipRes;
  SplashColorPtr pixBuf;
  SplashColorPtr p;
  int x, y;
  int i;
  SplashPattern *savedPattern;
  SplashCoord *savedLineDash = 0;
  int savedLineDashLength;
  SplashCoord savedLineDashPhase;
  SplashCoord savedLineWidth;

  if (debugMode) {
    printf("fillImageMask: w=%d h=%d mat=[%.2f %.2f %.2f %.2f %.2f %.2f]\n",
	   w, h, mat[0], mat[1], mat[2], mat[3], mat[4], mat[5]);
  }

  // check for singular matrix
  if (splashAbs(mat[0] * mat[3] - mat[1] * mat[2]) < 0.000001) {
    return splashErrSingularMatrix;
  }

  // compute scale, shear, rotation, translation parameters
  rot = splashAbs(mat[1]) > splashAbs(mat[0]);
  if (rot) {
    xScale = -mat[1];
    yScale = mat[2] - (mat[0] * mat[3]) / mat[1];
    xShear = -mat[3] / yScale;
    yShear = -mat[0] / mat[1];
  } else {
    xScale = mat[0];
    yScale = mat[3] - (mat[1] * mat[2]) / mat[0];
    xShear = mat[2] / yScale;
    yShear = mat[1] / mat[0];
  }
  tx = splashRound(mat[4]);
  ty = splashRound(mat[5]);
  scaledWidth = abs(splashRound(mat[4] + xScale) - tx) + 1;
  scaledHeight = abs(splashRound(mat[5] + yScale) - ty) + 1;
  xSign = (xScale < 0) ? -1 : 1;
  ySign = (yScale < 0) ? -1 : 1;

  // clipping
  ulx1 = 0;
  uly1 = 0;
  urx1 = xSign * (scaledWidth - 1);
  ury1 = splashRound(yShear * urx1);
  llx1 = splashRound(xShear * ySign * (scaledHeight - 1));
  lly1 = ySign * (scaledHeight - 1) + splashRound(yShear * llx1);
  lrx1 = xSign * (scaledWidth - 1) +
           splashRound(xShear * ySign * (scaledHeight - 1));
  lry1 = ySign * (scaledHeight - 1) + splashRound(yShear * lrx1);
  if (rot) {
    ulx = tx + uly1;    uly = ty - ulx1;
    urx = tx + ury1;    ury = ty - urx1;
    llx = tx + lly1;    lly = ty - llx1;
    lrx = tx + lry1;    lry = ty - lrx1;
  } else {
    ulx = tx + ulx1;    uly = ty + uly1;
    urx = tx + urx1;    ury = ty + ury1;
    llx = tx + llx1;    lly = ty + lly1;
    lrx = tx + lrx1;    lry = ty + lry1;
  }
  xMin = (ulx < urx) ? (ulx < llx) ? (ulx < lrx) ? ulx : lrx
                                   : (llx < lrx) ? llx : lrx
		     : (urx < llx) ? (urx < lrx) ? urx : lrx
                                   : (llx < lrx) ? llx : lrx;
  xMax = (ulx > urx) ? (ulx > llx) ? (ulx > lrx) ? ulx : lrx
                                   : (llx > lrx) ? llx : lrx
		     : (urx > llx) ? (urx > lrx) ? urx : lrx
                                   : (llx > lrx) ? llx : lrx;
  yMin = (uly < ury) ? (uly < lly) ? (uly < lry) ? uly : lry
                                   : (lly < lry) ? lly : lry
		     : (ury < lly) ? (ury < lry) ? ury : lry
                                   : (lly < lry) ? lly : lry;
  yMax = (uly > ury) ? (uly > lly) ? (uly > lry) ? uly : lry
                                   : (lly > lry) ? lly : lry
		     : (ury > lly) ? (ury > lry) ? ury : lry
                                   : (lly > lry) ? lly : lry;
  clipRes = state->clip->testRect(xMin, yMin, xMax, yMax);
  if (clipRes == splashClipAllOutside) return splashOk;

  if (!noClipPath || clipRes == splashClipAllInside) {
    if (!noShearImage && !noImageMask) {
      if (fillImageMaskFastWithCTM(src,srcData,w,h,tx,ty,mat)
	  == splashOk) {
	return splashOk;
      }
    }
  }

  SplashError result = splashOk;
  /* change stroke pattern temprarily */
  savedPattern = state->strokePattern->copy();
  setStrokePattern(state->fillPattern->copy());

  /* change lins style temporarily */
  savedLineDashLength = state->lineDashLength;
  savedLineDashPhase = state->lineDashPhase;
  if (savedLineDashLength > 0 && state->lineDash != 0) {
    savedLineDash = new SplashCoord[savedLineDashLength];
    memcpy(savedLineDash, state->lineDash,
      savedLineDashLength*sizeof(SplashCoord));
  }
  setLineDash(0,0,0);
  savedLineWidth = state->lineWidth;
  setLineWidth(0.0);

  /* calculate inverse matrix */
  SplashCoord imat[4];
  double det = mat[0] * mat[3] - mat[1] * mat[2];
  imat[0] = mat[3]/det;
  imat[1] = -mat[1]/det;
  imat[2] = -mat[2]/det;
  imat[3] = mat[0]/det;

  /* read source image */
  pixBuf = (SplashColorPtr)gmallocn(h , w);

  p = pixBuf;
  for (i = 0; i < h; ++i) {
    (*src)(srcData, p);
    p += w;
  }
  int width = xMax-xMin+1;
  int height = yMax-yMin+1;
  OPVPSplashClip *clip = state->clip->copy();

  if (w < scaledWidth || h < scaledHeight) {
    OPVPSplashPath cpath;

    cpath.moveTo(tx,ty);
    cpath.lineTo(mat[0]+tx,mat[1]+ty);
    cpath.lineTo(mat[0]+mat[2]+tx,mat[1]+mat[3]+ty);
    cpath.lineTo(mat[2]+tx,mat[3]+ty);
    clip->clipToPath(&cpath,state->matrix,1.0,gFalse);
  }
  for (y = 0;y < height;y++) {
    int dy = y+yMin-ty;
    int sx = 0;
    GBool dmode = gFalse;

    for (x = 0;x < width;x++) {
      if (!clip->test(x+xMin,y+yMin)) {
	if (dmode) {
	  drawSpan(xMin+sx,xMin+x-1,yMin+y,gTrue);
	  dmode = gFalse;
	}
	continue;
      }
      int ox,oy;
      /* calculate original coordinate */
      int dx = x+xMin-tx;
      ox = (int)trunc((imat[0]*dx+imat[2]*dy)*w);
      oy = (int)trunc((imat[1]*dx+imat[3]*dy)*h);
      if (ox >= 0 && ox < w && oy >= 0 && oy < h) {
	GBool on = pixBuf[oy*w+ox] != 0;

	if (on && !dmode) {
	  dmode = gTrue;
	  sx = x;
	} else if (!on && dmode) {
	  drawSpan(xMin+sx,xMin+x-1,yMin+y,gTrue);
	  dmode = gFalse;
	}
      } else if (dmode) {
	drawSpan(xMin+sx,xMin+x-1,yMin+y,gTrue);
	dmode = gFalse;
      }
    }
    if (dmode) {
      drawSpan(xMin+sx,xMin+x-1,yMin+y,gTrue);
    }
  }
  delete clip;
  gfree(pixBuf);

  /* restore stroke pattern */
  setStrokePattern(savedPattern);
  /* restore line style */
  setLineDash(savedLineDash,savedLineDashLength,
    savedLineDashPhase);
  if (savedLineDash != 0) {
    delete[] savedLineDash;
  }
  setLineWidth(savedLineWidth);

  return result;
}

SplashError OPVPSplash::drawImageNotShear(SplashImageSource src,
                              void *srcData,
			      int w, int h,
			      int tx, int ty,
			      int scaledWidth, int scaledHeight,
			      int xSign, int ySign, GBool rot) {
  int i, j;
  opvp_fix_t opvpx,opvpy;
  int opvpbytes, linesize;
  opvp_ctm_t opvpctm;
  SplashError result = splashOk;
  Guchar *buf = 0, *bp;
  SplashColorPtr lineBuf = 0, color;
  float e,f;
  int hs,he, hstep;
  int ow = w;
  int lineBufSize;

  if (rot) {
    int t = h;

    h = w;
    w = t;
    t = scaledHeight;
    scaledHeight = scaledWidth;
    scaledWidth = t;
    if (xSign != ySign) {
	xSign = xSign >= 0 ? -1 : 1;
    } else {
	ySign = ySign >= 0 ? -1 : 1;
    }
  }

  if (xSign > 0) {
    OPVP_i2Fix((tx),(opvpx));
    e = tx;
  } else {
    OPVP_i2Fix((tx-scaledWidth),(opvpx));
    e = tx-scaledWidth;
  }
  if (ySign > 0) {
    OPVP_i2Fix((ty),(opvpy));
    f = ty;
  } else {
    OPVP_i2Fix((ty-scaledHeight),(opvpy));
    f = ty-scaledHeight;
  }
  if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
    OPRS::error("SetCurrentPoint error\n");
    return splashErrOPVP;
  }
  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    linesize = w;
    opvpbytes = (w+3)/4;
    opvpbytes *= 4;
    lineBufSize = (ow+3)/4;
    lineBufSize *= 4;
    break;
  case splashModeRGB8:
    linesize = w*3;;
    opvpbytes = (w*3+3)/4;
    opvpbytes *= 4;
    lineBufSize = (ow*3+3)/4;
    lineBufSize *= 4;
    break;
  default:
    OPRS::error("Image: no supported color mode\n");
    return splashErrOPVP;
    break;
  }
  if (ySign >= 0) {
    hstep = 1;
    hs = 0;
    he = h;
  } else {
    hstep = -1;
    hs = h-1;
    he = -1;
  }
  buf = (Guchar *)gmallocn(h,opvpbytes);
  lineBuf = (SplashColorPtr)gmallocn(lineBufSize,1);
  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    if (rot) {
      if (xSign >= 0) {
	for (i = 0;i < w;i++) {
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = hs;j != he;j += hstep) {
	    bp = buf+i+j*opvpbytes;
	    *bp = *color++;
	  }
	}
      } else {
	for (i = 0;i < w;i++) {
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = hs;j != he;j += hstep) {
	    bp = buf+linesize-1-i+j*opvpbytes;
	    *bp = *color++;
	  }
	}
      }
    } else {
      if (xSign >= 0) {
	for (i = hs;i != he;i += hstep) {
	  bp = buf+opvpbytes*i;
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = 0;j < w;j++) {
	    *bp++ = *color++;
	  }
	}
      } else {
	for (i = hs;i != he;i += hstep) {
	  bp = buf+opvpbytes*i+linesize-1;
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = 0;j < w;j++) {
	    *bp-- = *color++;
	  }
	}
      }
    }
    break;
  case splashModeRGB8:
    if (rot) {
      if (xSign >= 0) {
	for (i = 0;i < w;i++) {
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = hs;j != he;j += hstep) {
	    bp = buf+i*3+j*opvpbytes;
	    bp[0] = *color++;
	    bp[1] = *color++;
	    bp[2] = *color++;
	  }
	}
      } else {
	for (i = 0;i < w;i++) {
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = hs;j != he;j += hstep) {
	    bp = buf+linesize-3-i*3+j*opvpbytes;
	    bp[0] = *color++;
	    bp[1] = *color++;
	    bp[2] = *color++;
	  }
	}
      }
    } else {
      if (xSign >= 0) {
	for (i = hs;i != he;i += hstep) {
	  bp = buf+opvpbytes*i;
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = 0;j < w;j++) {
	    *bp++ = *color++;
	    *bp++ = *color++;
	    *bp++ = *color++;
	  }
	}
      } else {
	for (i = hs;i != he;i += hstep) {
	  bp = buf+opvpbytes*i+linesize-1;
	  (*src)(srcData, lineBuf, NULL);
	  color = lineBuf;
	  for (j = 0;j < w;j++) {
	    *bp-- = color[2];
	    *bp-- = color[1];
	    *bp-- = color[0];
	    color += 3;
	  }
	}
      }
    }
    break;
  default:
    OPRS::error("Image: no supported color mode\n");
    result = splashErrOPVP;
    goto err1;
    break;
  }
  if (lineBuf != 0) gfree(lineBuf);

  /* canonlisp driver use CTM only, ignores currentPoint */
  /* So, set start point to CTM */
  opvpctm.a = 1.0;
  opvpctm.b = 0.0;
  opvpctm.c = 0.0;
  opvpctm.d = 1.0;
  opvpctm.e = e;
  opvpctm.f = f;
  if (opvp->SetCTM(&opvpctm) < 0) {
    OPRS::error("SetCTM error\n");
  }

  if (opvp->DrawImage(w,h,opvpbytes,OPVP_IFORMAT_RAW,
       scaledWidth,scaledHeight,(void *)(buf)) < 0) {
    OPRS::error("DrawImage error\n");
    result = splashErrOPVP;
    goto err1;
  }
err1:
  /* reset CTM */
  opvpctm.e = 0.0;
  opvpctm.f = 0.0;
  if (opvp->SetCTM(&opvpctm) < 0) {
    OPRS::error("SetCTM error\n");
  }

  if (buf != 0) gfree(buf);
  return result;
}

SplashError OPVPSplash::drawImageFastWithCTM(SplashImageSource src,
                              void *srcData,
			      int w, int h, int tx, int ty,
			      SplashCoord *mat) {
  int i;
  opvp_fix_t opvpx,opvpy;
  int opvpbytes;
  opvp_ctm_t opvpctm;
  SplashError result = splashOk;
  Guchar *buf = 0, *bp;

  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    opvpbytes = (w+3)/4;
    opvpbytes *= 4;
    break;
  case splashModeRGB8:
    opvpbytes = (w*3+3)/4;
    opvpbytes *= 4;
    break;
  default:
    OPRS::error("Image: no supported color mode\n");
    return splashErrOPVP;
    break;
  }
  buf = (Guchar *)gmallocn(h,opvpbytes);

  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    for (i = 0;i < h;i++) {
      bp = buf+opvpbytes*i;
      (*src)(srcData, (SplashColorPtr)bp, NULL);
    }
    break;
  case splashModeRGB8:
    for (i = 0;i < h;i++) {
      bp = buf+opvpbytes*i;
      (*src)(srcData, (SplashColorPtr)bp, NULL);
    }
    break;
  default:
    OPRS::error("Image: no supported color mode\n");
    result = splashErrOPVP;
    goto err0;
    break;
  }

  opvpctm.a = mat[0];
  opvpctm.b = mat[1];
  opvpctm.c = mat[2];
  opvpctm.d = mat[3];
  opvpctm.e = mat[4];
  opvpctm.f = mat[5];
  OPVP_i2Fix((tx),(opvpx));
  OPVP_i2Fix((ty),(opvpy));
  if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
    OPRS::error("SetCurrentPoint error\n");
  }

  if (opvp->SetCTM(&opvpctm) < 0) {
    OPRS::error("SetCTM error\n");
  }
  if (opvp->DrawImage(w,h,opvpbytes,OPVP_IFORMAT_RAW,1,1,(void *)(buf)) < 0) {
    OPRS::error("DrawImage error\n");
    result = splashErrOPVP;
  }
err0:
  /* reset CTM */
  opvpctm.a = 1.0;
  opvpctm.b = 0.0;
  opvpctm.c = 0.0;
  opvpctm.d = 1.0;
  opvpctm.e = 0.0;
  opvpctm.f = 0.0;
  if (opvp->SetCTM(&opvpctm) < 0) {
    OPRS::error("SetCTM error\n");
  }

  if (buf != 0) gfree(buf);
  return splashOk;
}

SplashError OPVPSplash::drawImage(SplashImageSource src, void *srcData,
			      SplashColorMode srcMode, GBool srcAlpha,
			      int w, int h, SplashCoord *mat) {
  GBool ok, rot, halftone;
  SplashCoord xScale, yScale, xShear, yShear;
  int tx, ty, scaledWidth, scaledHeight, xSign, ySign;
  int ulx, uly, llx, lly, urx, ury, lrx, lry;
  int ulx1, uly1, llx1, lly1, urx1, ury1, lrx1, lry1;
  int xMin, xMax, yMin, yMax;
  SplashClipResult clipRes;
  SplashColorPtr pixBuf, p;
  int x, y;
  int i;

  if (debugMode) {
    printf("drawImage: srcMode=%d w=%d h=%d mat=[%.2f %.2f %.2f %.2f %.2f %.2f]\n",
	   srcMode, w, h, mat[0], mat[1], mat[2], mat[3], mat[4], mat[5]);
  }

  // check color modes
  ok = gFalse; // make gcc happy
  switch (colorMode) {
  case splashModeMono1:
    ok = srcMode == splashModeMono1 || srcMode == splashModeMono8;
    break;
  case splashModeMono8:
    ok = srcMode == splashModeMono8;
    break;
  case splashModeRGB8:
    ok = srcMode == splashModeRGB8;
    break;
  }
  if (!ok) {
    OPRS::error("Image Mode mismatch\n");
    return splashErrModeMismatch;
  }
  halftone = colorMode == splashModeMono1 && srcMode == splashModeMono8;

  // check for singular matrix
  if (splashAbs(mat[0] * mat[3] - mat[1] * mat[2]) < 0.000001) {
    OPRS::error("Image Not Singular Matrix\n");
    return splashErrSingularMatrix;
  }

  // compute scale, shear, rotation, translation parameters
  rot = splashAbs(mat[1]) > splashAbs(mat[0]);
  if (rot) {
    xScale = -mat[1];
    yScale = mat[2] - (mat[0] * mat[3]) / mat[1];
    xShear = -mat[3] / yScale;
    yShear = -mat[0] / mat[1];
  } else {
    xScale = mat[0];
    yScale = mat[3] - (mat[1] * mat[2]) / mat[0];
    xShear = mat[2] / yScale;
    yShear = mat[1] / mat[0];
  }
  tx = splashRound(mat[4]);
  ty = splashRound(mat[5]);
  scaledWidth = abs(splashRound(mat[4] + xScale) - tx) + 1;
  scaledHeight = abs(splashRound(mat[5] + yScale) - ty) + 1;
  xSign = (xScale < 0) ? -1 : 1;
  ySign = (yScale < 0) ? -1 : 1;

  // clipping
  ulx1 = 0;
  uly1 = 0;
  urx1 = xSign * (scaledWidth - 1);
  ury1 = splashRound(yShear * urx1);
  llx1 = splashRound(xShear * ySign * (scaledHeight - 1));
  lly1 = ySign * (scaledHeight - 1) + splashRound(yShear * llx1);
  lrx1 = xSign * (scaledWidth - 1) +
           splashRound(xShear * ySign * (scaledHeight - 1));
  lry1 = ySign * (scaledHeight - 1) + splashRound(yShear * lrx1);
  if (rot) {
    ulx = tx + uly1;    uly = ty - ulx1;
    urx = tx + ury1;    ury = ty - urx1;
    llx = tx + lly1;    lly = ty - llx1;
    lrx = tx + lry1;    lry = ty - lrx1;
  } else {
    ulx = tx + ulx1;    uly = ty + uly1;
    urx = tx + urx1;    ury = ty + ury1;
    llx = tx + llx1;    lly = ty + lly1;
    lrx = tx + lrx1;    lry = ty + lry1;
  }
  xMin = (ulx < urx) ? (ulx < llx) ? (ulx < lrx) ? ulx : lrx
                                   : (llx < lrx) ? llx : lrx
		     : (urx < llx) ? (urx < lrx) ? urx : lrx
                                   : (llx < lrx) ? llx : lrx;
  xMax = (ulx > urx) ? (ulx > llx) ? (ulx > lrx) ? ulx : lrx
                                   : (llx > lrx) ? llx : lrx
		     : (urx > llx) ? (urx > lrx) ? urx : lrx
                                   : (llx > lrx) ? llx : lrx;
  yMin = (uly < ury) ? (uly < lly) ? (uly < lry) ? uly : lry
                                   : (lly < lry) ? lly : lry
		     : (ury < lly) ? (ury < lry) ? ury : lry
                                   : (lly < lry) ? lly : lry;
  yMax = (uly > ury) ? (uly > lly) ? (uly > lry) ? uly : lry
                                   : (lly > lry) ? lly : lry
		     : (ury > lly) ? (ury > lry) ? ury : lry
                                   : (lly > lry) ? lly : lry;
  if ((clipRes = state->clip->testRect(xMin, yMin, xMax, yMax))
      == splashClipAllOutside) {
    return splashOk;
  }

  if (!noClipPath || clipRes == splashClipAllInside) {
    if (!srcAlpha && !noShearImage) {
      if (drawImageFastWithCTM(src,srcData,w,h,tx,ty,mat) == splashOk) {
	  return splashOk;
      }
    }
    if (!srcAlpha && splashRound(xShear) == 0 && splashRound(yShear) == 0) {
      /* no sheared case */
      if (drawImageNotShear(src,srcData,w,h,tx,ty,
	   scaledWidth, scaledHeight,xSign,ySign,rot) == splashOk) {
	  return splashOk;
      }
    }
  }

  /* shear case */
  SplashError result = splashOk;

  /* calculate inverse matrix */
  SplashCoord imat[4];
  double det = mat[0] * mat[3] - mat[1] * mat[2];
  imat[0] = mat[3]/det;
  imat[1] = -mat[1]/det;
  imat[2] = -mat[2]/det;
  imat[3] = mat[0]/det;

  opvp_fix_t opvpx,opvpy;
  int opvpbytes, linesize;
  OPVP_Rectangle opvprect;
  int width = xMax-xMin+1;
  int height = yMax-yMin+1;
  opvp_ctm_t opvpctm;

  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    if (srcAlpha) {
      /*  alpha data exists */
      linesize = w*2;
    } else {
      linesize = w;
    }
    opvpbytes = (width+3)/4;
    opvpbytes *= 4;
    break;
  case splashModeRGB8:
    if (srcAlpha) {
      /* alpha data exists */
      linesize = w*4;
    } else {
      linesize = w*3;
    }
    opvpbytes = (width*3+3)/4;
    opvpbytes *= 4;
    break;
  default:
    OPRS::error("Image: no supported color mode\n");
    return splashErrOPVP;
    break;
  }

  /* read source image */
  pixBuf = (SplashColorPtr)gmallocn(h , linesize);

  p = pixBuf;
  for (i = 0; i < h; ++i) {
    (*src)(srcData, p, NULL);
    p += linesize;
  }
  /* allocate line buffer */
  Guchar *lineBuf = (Guchar *)gmallocn(opvpbytes,1);
  Guchar *onBuf = (Guchar *)gmallocn(width,1);
  OPVPSplashClip *clip;
  opvpctm.a = 1.0;
  opvpctm.b = 0.0;
  opvpctm.c = 0.0;
  opvpctm.d = 1.0;
  OPVP_i2Fix(0,opvprect.p0.x);
  OPVP_i2Fix(0,opvprect.p0.y);

  clip = state->clip->copy();
  if (w < scaledWidth || h < scaledHeight) {
    OPVPSplashPath cpath;

    cpath.moveTo(tx,ty);
    cpath.lineTo(mat[0]+tx,mat[1]+ty);
    cpath.lineTo(mat[0]+mat[2]+tx,mat[1]+mat[3]+ty);
    cpath.lineTo(mat[2]+tx,mat[3]+ty);
    clip->clipToPath(&cpath,state->matrix,1.0,gFalse);
  }
  for (y = 0;y < height;y++) {
    int dy = y+yMin-ty;
    memset(onBuf,0,width);
    if (srcAlpha) {
      /* with alpha data */
      for (x = 0;x < width;x++) {
	if (!clip->test(x+xMin,y+yMin)) continue;
	int ox,oy;
	/* calculate original coordinate */
	int dx = x+xMin-tx;
	ox = (int)trunc((imat[0]*dx+imat[2]*dy)*w);
	oy = (int)trunc((imat[1]*dx+imat[3]*dy)*h);
	if (ox >= 0 && ox < w && oy >= 0 && oy < h) {
	  /* in the image */

	  switch (colorMode) {
	  case splashModeMono1:
	  case splashModeMono8:
	    onBuf[x] =  pixBuf[oy*linesize+ox] != 0;
	    lineBuf[x] = pixBuf[oy*linesize+ox+1];
	    break;
	  case splashModeRGB8:
	    p = pixBuf+oy*linesize+ox*4;
	    onBuf[x] =  (*p++) != 0;
	    lineBuf[x*3] = *p++;
	    lineBuf[x*3+1] = *p++;
	    lineBuf[x*3+2] = *p;
	    break;
	  default:
	    OPRS::error("Image: no supported color mode\n");
	    result = splashErrOPVP;
	    goto err1;
	    break;
	  }
	}
      }
    } else {
      for (x = 0;x < width;x++) {
	if (!clip->test(x+xMin,y+yMin)) continue;
	int ox,oy;
	/* calculate original coordinate */
	int dx = x+xMin-tx;
	ox = (int)trunc((imat[0]*dx+imat[2]*dy)*w);
	oy = (int)trunc((imat[1]*dx+imat[3]*dy)*h);
	if (ox >= 0 && ox < w && oy >= 0 && oy < h) {
	  /* in the image */

	  switch (colorMode) {
	  case splashModeMono1:
	  case splashModeMono8:
	    lineBuf[x] = pixBuf[oy*linesize+ox];
	    break;
	  case splashModeRGB8:
	    p = pixBuf+oy*linesize+ox*3;
	    lineBuf[x*3] = *p++;
	    lineBuf[x*3+1] = *p++;
	    lineBuf[x*3+2] = *p;
	    break;
	  default:
	    OPRS::error("Image: no supported color mode\n");
	    result = splashErrOPVP;
	    goto err1;
	    break;
	  }
	  onBuf[x] = 1;
	}
      }
    }
    /* out pixel */
    int sx = 0;
    int ex;
    while (sx < width) {
      /* find start pixel */
      for (;onBuf[sx] == 0 && sx < width;sx++);
      if (sx >= width) break;
      /* find end pixel */
      for (ex = sx+1;onBuf[ex] != 0 && ex < width;ex++);
      int n = ex-sx;
      Guchar *bp;
      int ns;

      switch (colorMode) {
      case splashModeMono1:
      case splashModeMono8:
	bp = lineBuf+sx;
	ns = n;
	break;
      case splashModeRGB8:
	bp = lineBuf+sx*3;
	ns = n*3;
	break;
      default:
	OPRS::error("Image: no supported color mode\n");
	result = splashErrOPVP;
	goto err1;
	break;
      }
      ns = (ns+3)/4;
      ns *= 4;

      OPVP_i2Fix(xMin+sx,(opvpx));
      OPVP_i2Fix(yMin+y,(opvpy));
      if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
	OPRS::error("SetCurrentPoint error\n");
	result = splashErrOPVP;
	goto err1;
      }
      OPVP_i2Fix(n,opvprect.p1.x);
      OPVP_i2Fix(1,opvprect.p1.y);
      /* canonlisp driver use CTM only, ignores currentPoint */
      /* So, set start point to CTM */
      opvpctm.e = xMin+sx;
      opvpctm.f = yMin+y;
      if (opvp->SetCTM(&opvpctm) < 0) {
	OPRS::error("SetCTM error\n");
      }

      if (opvp->DrawImage(n,1,ns,OPVP_IFORMAT_RAW,
	   n,1,(void *)(bp)) < 0) {
	OPRS::error("DrawImage error\n");
	result = splashErrOPVP;
	goto err1;
      }

      /* reset CTM */
      opvpctm.e = 0.0;
      opvpctm.f = 0.0;
      if (opvp->SetCTM(&opvpctm) < 0) {
	OPRS::error("SetCTM error\n");
      }

      sx = ex+1;
    }
  }


err1:
  delete clip;
  gfree(pixBuf);
  gfree(lineBuf);
  gfree(onBuf);
  return result;
}

void OPVPSplash::setColorMode(int colorModeA)
{
    colorMode = colorModeA;
}

void OPVPSplash::drawSpan(int x0, int x1, int y, GBool noClip)
{
  int s,e;
  opvp_point_t points[1];
  opvp_fix_t opvpx, opvpy;
  SplashCoord *savedLineDash = 0;
  int savedLineDashLength;
  SplashCoord savedLineDashPhase;
  SplashCoord savedLineWidth;
  GBool noSpan;


  if (opvp->NewPath() < 0) {
    OPRS::error("NewPath error\n");
    return;
  }
  if (noClip) {
    noSpan = gFalse;
    OPVP_i2Fix(x0,opvpx);
    OPVP_i2Fix(y,opvpy);
    if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
      OPRS::error("SetCurrentPoint error\n");
      return;
    }
    OPVP_i2Fix(x1+1,points[0].x);
    OPVP_i2Fix(y,points[0].y);
    if (opvp->LinePath(OPVP_PATHOPEN,1,points) < 0) {
      OPRS::error("LinePath error\n");
      return;
    }
  } else {
    noSpan = gTrue;
    s = x0;
    while (s < x1) {
      /* find start point */
      for (;s < x1;s++) {
	if (state->clip->test(s, y)) break;
      }
      if (s < x1) {
	/* start point was found */
	/* then find end point */
	for (e = s+1;e < x1;e++) {
	  if (!state->clip->test(e, y)) break;
	}
	/* do make span */
	noSpan = gFalse;
	OPVP_i2Fix(s,opvpx);
	OPVP_i2Fix(y,opvpy);
	if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
	  OPRS::error("SetCurrentPoint error\n");
	  return;
	}
	OPVP_i2Fix(e,points[0].x);
	OPVP_i2Fix(y,points[0].y);
	if (opvp->LinePath(OPVP_PATHOPEN,1,points) < 0) {
	  OPRS::error("LinePath error\n");
	  return;
	}
	s = e;
      }
    }
  }
  if (opvp->EndPath() < 0) {
    OPRS::error("EndPath error\n");
    return;
  }
  if (noSpan) return;
  /* change lins style temporarily */
  savedLineDashLength = state->lineDashLength;
  savedLineDashPhase = state->lineDashPhase;
  if (savedLineDashLength > 0 && state->lineDash != 0) {
    savedLineDash = new SplashCoord[savedLineDashLength];
    memcpy(savedLineDash, state->lineDash,
      savedLineDashLength*sizeof(SplashCoord));
  }
  setLineDash(0,0,0);
  savedLineWidth = state->lineWidth;
  setLineWidth(0.0);

  if (opvp->StrokePath() < 0) {
    OPRS::error("StrokePath error\n");
    return;
  }

  /* restore line style */
  setLineDash(savedLineDash,savedLineDashLength,
    savedLineDashPhase);
  if (savedLineDash != 0) {
    delete[] savedLineDash;
  }
  setLineWidth(savedLineWidth);
}

/*
  draw pixel with StrokePath
  color is stroke color
*/
void OPVPSplash::drawPixel(int x, int y, GBool noClip)
{
  opvp_point_t points[1];
  opvp_fix_t opvpx, opvpy;

  if (noClip || state->clip->test(x, y)) {
    if (opvp->NewPath() < 0) {
      OPRS::error("NewPath error\n");
      return;
    }
    OPVP_i2Fix(x,opvpx);
    OPVP_i2Fix(y,opvpy);
    if (opvp->SetCurrentPoint(opvpx,opvpy) < 0) {
      OPRS::error("NewPath error\n");
      return;
    }
    OPVP_i2Fix(x+1,points[0].x);
    OPVP_i2Fix(y,points[0].y);
    if (opvp->LinePath(OPVP_PATHOPEN,1,points) < 0) {
      OPRS::error("LinePath error\n");
      return;
    }
    if (opvp->EndPath() < 0) {
      OPRS::error("EndPath error\n");
      return;
    }
    if (opvp->StrokePath() < 0) {
      OPRS::error("StrokePath error\n");
      return;
    }
  }
}

const char *OPVPSplash::getOption(const char *key, int nOptions,
  const char *optionKeys[], const char *optionVals[])
{
  int i;

  for (i = 0;i < nOptions;i++) {
    if (strcmp(key,optionKeys[i]) == 0) {
      return optionVals[i];
    }
  }
  return 0;
}

void OPVPSplash::endPage()
{
  if (clipPath != 0) {
    delete clipPath;
    clipPath = 0;
  }
}

void OPVPSplash::restoreAllDriverState()
{
  for (;saveDriverStateCount > 0;saveDriverStateCount--) {
    opvp->RestoreGS();
  }
}

SplashCoord *OPVPSplash::getMatrix()
{
    return state->matrix;
}

OPVPClipPath *OPVPClipPath::stackTop = 0;

OPVPClipPath::OPVPClipPath(OPVPSplashPath *pathA, GBool eoA)
{
  path = pathA;
  eo = eoA;
  next = 0;
  saved = gFalse;
}

void OPVPClipPath::push()
{
  OPVPClipPath *p;

  p = stackTop;
  stackTop = copy();
  stackTop->next = p;
  saved = gTrue;
}

OPVPClipPath *OPVPClipPath::pop() {
  OPVPClipPath *p = stackTop;
  if (stackTop != 0) stackTop = stackTop->next;
  return p;
}

OPVPClipPath *OPVPClipPath::copy()
{
  OPVPClipPath *p;

  p = new OPVPClipPath(path->copy(),eo);
  p->saved = saved;
  return p;
}

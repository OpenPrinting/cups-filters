//========================================================================
//
// OPRS.cc
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
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <math.h>
#if defined __OpenBSD__
#include <sys/endian.h>
#if BYTE_ORDER == BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
#endif

#include "goo/gmem.h"
#include "splash/SplashErrorCodes.h"
#include "splash/SplashMath.h"
#include "splash/SplashBitmap.h"
#include "splash/SplashState.h"
#include "splash/SplashXPathScanner.h"
#include "splash/SplashPattern.h"
#include "splash/SplashScreen.h"
#include "splash/SplashFont.h"
#include "splash/SplashGlyphBitmap.h"
#include "splash/Splash.h"
#include "OPVPSplash.h"
#include "OPVPSplashClip.h"
#include "OPVPSplashPath.h"
#include "OPVPSplashXPath.h"
#include "OPRS.h"

//------------------------------------------------------------------------
// Splash
//------------------------------------------------------------------------

#define SPLASH(x) (rasterMode ? (splash->x) : (opvpSplash->x))

OPRS::OPRS() 
{
  opvp = 0;
  splash = 0;
  opvpSplash = 0;
  rasterMode = gFalse;
}

int OPRS::setBitmap(SplashBitmap *bitmapA) {
  if (splash != 0) {
    delete splash;
  }
  splash = new Splash(bitmapA, gFalse);
  rasterMode = gTrue;
  return 0;
}

OPRS::~OPRS() {
  if (splash != 0) {
    delete splash;
    splash = 0;
  }
  if (opvpSplash != 0) {
    opvpSplash->restoreAllDriverState();
    delete opvpSplash;
    opvpSplash = 0;
  }
}

//------------------------------------------------------------------------
// state read
//------------------------------------------------------------------------


SplashPattern *OPRS::getStrokePattern() {
  return SPLASH(getStrokePattern());
}

SplashPattern *OPRS::getFillPattern() {
  return SPLASH(getFillPattern());
}

SplashScreen *OPRS::getScreen() {
  return SPLASH(getScreen());
}

SplashCoord OPRS::getLineWidth() {
  return SPLASH(getLineWidth());
}

int OPRS::getLineCap() {
  return SPLASH(getLineCap());
}

int OPRS::getLineJoin() {
  return SPLASH(getLineJoin());
}

SplashCoord OPRS::getMiterLimit() {
  return SPLASH(getMiterLimit());
}

SplashCoord OPRS::getFlatness() {
  return 1;
}

SplashCoord *OPRS::getLineDash() {
  return SPLASH(getLineDash());
}

int OPRS::getLineDashLength() {
  return SPLASH(getLineDashLength());
}

SplashCoord OPRS::getLineDashPhase() {
  return SPLASH(getLineDashPhase());
}

OPVPSplashClip *OPRS::getClip() {
  if (rasterMode) {
    SplashClip *sclip = splash->getClip();
    OPVPSplashClip *r = new OPVPSplashClip(sclip);
    delete sclip;
    return r;
  } else {
    return opvpSplash->getClip();
  }
}

//------------------------------------------------------------------------
// state write
//------------------------------------------------------------------------

void OPRS::setStrokePattern(SplashPattern *strokePattern) {
    SPLASH(setStrokePattern(strokePattern));
}

void OPRS::setFillPattern(SplashPattern *fillPattern) {
    SPLASH(setFillPattern(fillPattern));
}

void OPRS::setScreen(SplashScreen *screen) {
    SPLASH(setScreen(screen));
}

void OPRS::setLineWidth(SplashCoord lineWidth) {
    SPLASH(setLineWidth(lineWidth));
}

void OPRS::setMiterLimit(SplashCoord miterLimit) {
    SPLASH(setMiterLimit(miterLimit));
}

void OPRS::setLineCap(int lineCap) {
    SPLASH(setLineCap(lineCap));
}

void OPRS::setLineJoin(int lineJoin) {
    SPLASH(setLineJoin(lineJoin));
}

void OPRS::setFlatness(SplashCoord flatness) {
/* ignore flatness */
}

void OPRS::setLineDash(SplashCoord *lineDash, int lineDashLength,
			 SplashCoord lineDashPhase) {
    SPLASH(setLineDash(lineDash,lineDashLength,lineDashPhase));
}

SplashError OPRS::clipToPath(OPVPSplashPath *path, GBool eo) {
    return SPLASH(clipToPath(path,eo));
}

//------------------------------------------------------------------------
// state save/restore
//------------------------------------------------------------------------

void OPRS::saveState() {
    SPLASH(saveState());
}

SplashError OPRS::restoreState() {
    SPLASH(restoreState());
    return splashOk;
}

//------------------------------------------------------------------------
// drawing operations
//------------------------------------------------------------------------

void OPRS::clear(SplashColor color) {
    SPLASH(clear(color));
}

SplashError OPRS::stroke(OPVPSplashPath *path) {
    return SPLASH(stroke(path));
}

SplashError OPRS::fill(OPVPSplashPath *path, GBool eo) {
    return SPLASH(fill(path,eo));
}

SplashError OPRS::fillChar(SplashCoord x, SplashCoord y,
			     int c, SplashFont *font, Unicode *u, 
			     double *fontMat) {
    if (rasterMode) {
	return splash->fillChar(x,y,c,font);
    } else {
	return opvpSplash->fillChar(x,y,c,font,u,fontMat);
    }
}

SplashError OPRS::fillGlyph(SplashCoord x, SplashCoord y,
			      SplashGlyphBitmap *glyph) {
    SPLASH(fillGlyph(x,y,glyph));
    return splashOk;
}

SplashError OPRS::fillImageMask(SplashImageMaskSource src, void *srcData,
			  int w, int h, SplashCoord *mat, GBool glyphMode) {
    return SPLASH(fillImageMask(src,srcData,w,h,mat,glyphMode));
}

SplashError OPRS::drawImage(SplashImageSource src, void *srcData,
			      SplashColorMode srcMode, GBool srcAlpha,
			      int w, int h, SplashCoord *mat) {
    if (rasterMode) {
#if POPPLER_VERSION_MAJOR <= 0 && (POPPLER_VERSION_MINOR <= 20 || (POPPLER_VERSION_MINOR == 21 && POPPLER_VERSION_MICRO <= 2))
	return splash->drawImage(src,srcData,srcMode,srcAlpha,w,h,mat);
#elif POPPLER_VERSION_MAJOR <= 0 && POPPLER_VERSION_MINOR <= 33
	return splash->drawImage(src,srcData,srcMode,srcAlpha,w,h,mat,gFalse);
#else
	return splash->drawImage(src,0,srcData,srcMode,srcAlpha,w,h,mat,gFalse);
#endif
    } else {
	return opvpSplash->drawImage(src,srcData,srcMode,srcAlpha,w,h,mat);
    }
}

/*
 * initialize and load vector-driver
 */
int OPRS::init(const char *driverName, int outputFD,
  const char *printerModel, int nOptions,
  const char *optionKeys[], const char *optionVals[])
{
    opvp = OPVPWrapper::loadDriver(driverName,outputFD,printerModel);
    if (opvp == 0) return -1;
    rasterMode = gFalse;
    if (!rasterMode) {
	opvpSplash = new OPVPSplash(opvp,nOptions,
	  optionKeys, optionVals);
    }
    return 0;
}

int OPRS::OPVPStartJob(char *jobInfo)
{
    if (!opvp->supportStartJob) {
      return 0;
    }
    return opvp->StartJob((const opvp_char_t *)jobInfo);
}

int OPRS::OPVPEndJob()
{
    if (!opvp->supportEndJob) {
      return 0;
    }
    return opvp->EndJob();
}

int OPRS::OPVPStartDoc(char *docInfo)
{
    if (!opvp->supportStartDoc) {
      return 0;
    }
    return opvp->StartDoc((const opvp_char_t *)docInfo);
}

int OPRS::OPVPEndDoc()
{
    if (!opvp->supportEndDoc) {
      return 0;
    }
    return opvp->EndDoc();
}

int OPRS::OPVPStartPage(char *pageInfo, int rasterWidth)
{
    int r;

    if (opvp->supportStartPage) {
	if ((r = opvp->StartPage((const opvp_char_t *)pageInfo)) < 0) {
	  return r;
	}
    }
    if (rasterMode) {
	if (!opvp->supportStartRaster) {
	    error("No StartRaster error in raster mode\n");
	    return -1;
	}
	if (opvp->supportSetCurrentPoint) {
	    opvp_fix_t x,y;

	    OPVP_F2FIX(0.0,x);
	    OPVP_F2FIX(0.0,y);
	    opvp->SetCurrentPoint(x,y);
	}
	opvp->StartRaster(rasterWidth);
    }
    return 0;
}

int OPRS::OPVPEndPage()
{
    int r;

    if (rasterMode) {
	if (!opvp->supportEndRaster) {
	    error("No EndRaster error in raster mode\n");
	    return -1;
	}
	opvp->EndRaster();
    }
    if (opvp->supportEndPage) {
	if ((r = opvp->EndPage()) < 0) {
	    return r;
	}
    }
    return splashOk;
}

unsigned char *OPRS::getScanLineDataMono1(unsigned char *dst,
  unsigned char *bitmap, int rasterWidth)
{
    int n = (rasterWidth+7)/8;

    memcpy(dst,bitmap,n);
    return bitmap+n;
}

unsigned char *OPRS::getScanLineDataMono8(unsigned char *dst,
  unsigned char *bitmap, int rasterWidth)
{
    memcpy(dst,bitmap,rasterWidth);
    return bitmap+rasterWidth;
}

unsigned char *OPRS::getScanLineDataRGB8(unsigned char *dst,
  unsigned char *bitmap, int rasterWidth)
{
    int i;

    for (i = 0;i < rasterWidth;i++) {
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
	bitmap++;
	*dst++ = *bitmap++;
	*dst++ = *bitmap++;
	*dst++ = *bitmap++;
#else
	dst[2] = *bitmap++;
	dst[1] = *bitmap++;
	dst[0] = *bitmap++;
	bitmap++;
	dst += 3;
#endif
    }
    return bitmap;
}

unsigned char *OPRS::getScanLineDataBGR8Packed(unsigned char *dst,
   unsigned char *bitmap, int rasterWidth)
{
    memcpy(dst,bitmap,rasterWidth*3);
    return bitmap+rasterWidth*3;
}

OPRS::GetScanLineDataFunT OPRS::getGetScanLineDataFun(SplashBitmap *bitmap)
{
    switch (bitmap->getMode()) {
    case splashModeMono1:
	return getScanLineDataMono1;
    case splashModeMono8:
	return getScanLineDataMono8;
    case splashModeRGB8:
	return getScanLineDataRGB8;
    default:
	OPRS::error("Unknown bitmap mode\n");
	break;
    }
    return getScanLineDataMono8;
}

int OPRS::getRasterSize(SplashBitmap *bitmap)
{
    int rw = bitmap->getWidth();

    switch (bitmap->getMode()) {
    case splashModeMono1:
	return (rw+7)/8;
    case splashModeMono8:
	return rw;
    case splashModeRGB8:
        return rw*3;
    default:
	OPRS::error("Unknown bitmap mode\n");
	break;
    }
    return 0;
}

GBool OPRS::checkAll1(unsigned char *bp, int n, int width, int mode)
{
    int lastbytemask = 0xff;
    int i;

    if (mode == splashModeMono1) {
	lastbytemask <<= (width & 0x7);
	lastbytemask &= 0xff;
    }
    for (i = 0;i < n-1;i++) {
	if (*bp++ != 0xff) return gFalse;
    }
    return (*bp & lastbytemask) == lastbytemask;
}

int OPRS::outSlice()
{
    if (rasterMode) {
	/* out bitmap */
	int rasterWidth;
	int nScanLines;
	int rasterSize;
	unsigned char *p;
	int i;
	SplashBitmap *bitmap;
	SplashColorPtr cp;
	unsigned char *bp;
	GetScanLineDataFunT fun;
	int mode;

	if (!opvp->supportStartRaster || !opvp->supportTransferRasterData
	   || !opvp->supportEndRaster) {
	  OPRS::error("No raster supporting printer driver\n");
	  return -1;
	}

	bitmap = splash->getBitmap();
	rasterWidth = bitmap->getWidth();
	nScanLines = bitmap->getHeight();
	rasterSize = getRasterSize(bitmap);
	if ((bp = new unsigned char[rasterSize]) == 0) {
	    OPRS::error("Not enough memory\n");
	    return -1;
	}
	cp = (bitmap->getDataPtr());
	p = reinterpret_cast<unsigned char *>(cp);
	fun = getGetScanLineDataFun(bitmap);
	mode = bitmap->getMode();
	for (i = 0;i < nScanLines;i++) {
	    p = (*fun)(bp,p,rasterWidth);
	    if (opvp->supportSkipRaster
	        && checkAll1(bp,rasterSize,rasterWidth,mode)) {
	      /* all white, skip raster */
		opvp->SkipRaster(1);
	    } else {
		opvp->TransferRasterData(rasterSize,bp);
	    }
	}
	delete[] bp;
    }
    return 0;
}

int OPRS::setColorMode(int colorModeA, GBool colorProfile)
{
    opvp_cspace_t cspace = OPVP_CSPACE_STANDARDRGB;

    if (opvp->supportGetColorSpace) opvp->GetColorSpace(&cspace);
    switch (cspace){
    case OPVP_CSPACE_BW:
	if (colorModeA != splashModeMono1) {
	  OPRS::error("not mono mode is specified on a monochrome printer\n");
	  return -1;
	}
	break;
    case OPVP_CSPACE_DEVICEGRAY:
	if (colorModeA != splashModeMono1 && colorModeA != splashModeMono8) {
	    OPRS::error("colorMode is specified on not a color printer\n");
	    return -1;
	}
	break;
    case OPVP_CSPACE_DEVICERGB:
	if (colorProfile) break;
    default:
	/* rgb color */
	if (colorProfile) {
	    /* try set colorspace to DEVICERGB */
	    if (opvp->supportSetColorSpace) opvp->SetColorSpace(
	       OPVP_CSPACE_DEVICERGB);
	    if (opvp->supportGetColorSpace) opvp->GetColorSpace(&cspace);
	    if (cspace == OPVP_CSPACE_DEVICERGB) break;
	    /* fail to set,  fall through */
	}
	if (opvp->supportSetColorSpace) opvp->SetColorSpace(
	   OPVP_CSPACE_STANDARDRGB);
	break;
    }
    if (!rasterMode) {
	opvpSplash->setColorMode(colorModeA);
    }
    return 0;
}

SplashBitmap *OPRS::getBitmap()
{
    return SPLASH(getBitmap());
}

void OPRS::setDebugMode(GBool debugModeA)
{
    SPLASH(setDebugMode(debugModeA));
}

void OPRS::initGS(int colorMode, int w, int h, SplashColor paperColor)
{
  SplashColor color;

  if (!rasterMode && opvp->supportInitGS) {
    opvp->InitGS();
  }
  if (opvp->supportSetPaintMode) {
      opvp->SetPaintMode(OPVP_PAINTMODE_TRANSPARENT);
  }
  switch (colorMode) {
  case splashModeMono1: color[0] = 0; break;
  case splashModeMono8: color[0] = 0; break;
  case splashModeRGB8: color[0] = color[1] = color[2] = 0; break;
  }
  if (!rasterMode) {
    opvpSplash->setStateBypass(gTrue);
  }
  SPLASH(setStrokePattern(new SplashSolidColor(color)));
  SPLASH(setFillPattern(new SplashSolidColor(color)));
  SPLASH(setLineCap(splashLineCapButt));
  SPLASH(setLineJoin(splashLineJoinMiter));
  SPLASH(setLineDash(0, 0, 0));
  SPLASH(setLineWidth(0));
  SPLASH(setMiterLimit(10));
  SPLASH(setFlatness(1));
  SPLASH(clipResetToRect(0,0,w-1,h-1));
  SPLASH(clear(paperColor));
  if (!rasterMode) {
    opvpSplash->setStateBypass(gFalse);
  }
}

void OPRS::error(const char *msg, ...)
{
    va_list args;

    fprintf(stderr,"ERROR:OPRS:");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fflush(stderr);
}

void OPRS::endPage()
{
    /* restore state */
    while (SPLASH(restoreState()) == splashOk);
    if (!rasterMode) {
	opvpSplash->endPage();
    }
}

void OPRS::setSoftMask(SplashBitmap *softMaskA)
{
    /* Soft Mask is not supported in vector mode. */
    if (rasterMode) {
	splash->setSoftMask(softMaskA);
    }
}

SplashCoord *OPRS::getMatrix()
{
    return SPLASH(getMatrix());
}

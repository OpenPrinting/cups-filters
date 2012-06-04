//========================================================================
//
// OPVPSplash.h
//
//========================================================================

#ifndef OPVPSPLASH_H
#define OPVPSPLASH_H

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include <typeinfo>
#include "splash/SplashTypes.h"
#include "splash/SplashPattern.h"
#include "splash/SplashErrorCodes.h"
#include "OPVPSplashPath.h"
#include "OPVPWrapper.h"
#include "CharTypes.h"

/* extra error code */
#define splashErrOPVP 100

#define OPVP_MAX_CLIPPATH_LENGTH 2000
#define OPVP_MAX_FILLPATH_LENGTH 4000
#define OPVP_BITMAPCHAR_THRESHOLD 2000
#define OPVP_ROP_SRCCOPY 0xCC
#define OPVP_ROP_S 0xCC
#define OPVP_ROP_P 0xF0
#define OPVP_ROP_PSDPxax 0xB8
#define OPVP_ROP_DSPDxax 0xE2

class SplashBitmap;
class SplashGlyphBitmap;
class OPVPSplashState;
class SplashPattern;
class SplashScreen;
class OPVPSplashPath;
class OPVPSplashXPath;
class OPVPSplashClip;
class SplashFont;

class OPVPClipPath {
public:
  OPVPClipPath(OPVPSplashPath *pathA, GBool eoA);
  void push();
  static OPVPClipPath *pop();
  ~OPVPClipPath() { delete path; }
  OPVPSplashPath *getPath() { return path; }
  GBool getEo() { return eo; }
  GBool getSaved() { return saved; }
private:
  OPVPClipPath *copy();
  OPVPClipPath *next;
  OPVPSplashPath *path;
  GBool eo;
  GBool saved;
  static OPVPClipPath *stackTop;
};

//------------------------------------------------------------------------
// Splash
//------------------------------------------------------------------------

class OPVPSplash {
public:

  // Create a new rasterizer object.
  OPVPSplash(OPVPWrapper *opvpA,
    int nOptions, const char *optionKeys[], const char *optionVals[]);

  virtual ~OPVPSplash();

  //----- state read

  SplashPattern *getStrokePattern();
  SplashPattern *getFillPattern();
  SplashScreen *getScreen();
  SplashCoord getLineWidth();
  int getLineCap();
  int getLineJoin();
  SplashCoord getMiterLimit();
  SplashCoord getFlatness();
  SplashCoord *getLineDash();
  int getLineDashLength();
  SplashCoord getLineDashPhase();
  OPVPSplashClip *getClip();

  //----- state write

  void setStrokePattern(SplashPattern *strokeColor);
  void setFillPattern(SplashPattern *fillColor);
  void setScreen(SplashScreen *screen);
  void setLineWidth(SplashCoord lineWidth);
  void setLineCap(int lineCap);
  void setLineJoin(int lineJoin);
  void setMiterLimit(SplashCoord miterLimit);
  void setFlatness(SplashCoord flatness);
  // the <lineDash> array will be copied
  void setLineDash(SplashCoord *lineDash, int lineDashLength,
		   SplashCoord lineDashPhase);
  void clipResetToRect(SplashCoord x0, SplashCoord y0,
		       SplashCoord x1, SplashCoord y1);
  SplashError clipToPath(OPVPSplashPath *path, GBool eo);

  //----- state save/restore

  void saveState();
  SplashError restoreState();
  void restoreAllDriverState();

  //----- drawing operations

  // Fill the bitmap with <color>.  This is not subject to clipping.
  void clear(SplashColor color);

  // Stroke a path using the current stroke pattern.
  SplashError stroke(OPVPSplashPath *path);

  // Fill a path using the current fill pattern.
  SplashError fill(OPVPSplashPath *path, GBool eo);

  // Draw a character, using the current fill pattern.
  SplashError fillChar(SplashCoord x, SplashCoord y, int c,
    SplashFont *font, Unicode *u, double *fontMat);

  // Draw a glyph, using the current fill pattern.  This function does
  // not free any data, i.e., it ignores glyph->freeData.
  // not used in vector mode
  void fillGlyph(SplashCoord x, SplashCoord y,
			SplashGlyphBitmap *glyph);

  // Draws an image mask using the fill color.  This will read <w>*<h>
  // pixels from <src>, in raster order, starting with the top line.
  // "1" pixels will be drawn with the current fill color; "0" pixels
  // are transparent.  The matrix:
  //    [ mat[0] mat[1] 0 ]
  //    [ mat[2] mat[3] 0 ]
  //    [ mat[4] mat[5] 1 ]
  // maps a unit square to the desired destination for the image, in
  // PostScript style:
  //    [x' y' 1] = [x y 1] * mat
  // Note that the Splash y axis points downward, and the image source
  // is assumed to produce pixels in raster order, starting from the
  // top line.
  SplashError fillImageMask(SplashImageMaskSource src, void *srcData,
			    int w, int h, SplashCoord *mat, GBool glyphMode);

  // Draw an image.  This will read <w>*<h> pixels from <src>, in
  // raster order, starting with the top line.  These pixels are
  // assumed to be in the source mode, <srcMode>.  The following
  // combinations of source and target modes are supported:
  //    source       target
  //    ------       ------
  //    Mono1        Mono1
  //    Mono8        Mono1   -- with dithering
  //    Mono8        Mono8
  //    RGB8         RGB8
  //    BGR8packed   BGR8Packed
  // The matrix behaves as for fillImageMask.
  SplashError drawImage(SplashImageSource src, void *srcData,
			SplashColorMode srcMode, GBool srcAlpha,
			int w, int h, SplashCoord *mat);

  //~ drawMaskedImage

  //----- misc

  // Return the associated bitmap.
  SplashBitmap *getBitmap() { return 0; }

  // Toggle debug mode on or off.
  void setDebugMode(GBool debugModeA) { debugMode = debugModeA; }

  void setColorMode(int colorModeA);
  void setStateBypass(GBool bypass) {stateBypass = bypass;}
  void endPage();
  SplashCoord *getMatrix();
  void drawSpan(int x0, int x1, int y, GBool noClip);
#ifdef OLD_DRAW_IMAGE
  void drawPixel(int x, int y, SplashColor *color, GBool noClip);
#endif
  void drawPixel(int x, int y, GBool noClip);
  void arcToCurve(SplashCoord x0, SplashCoord y0,
    SplashCoord x3, SplashCoord y3,
    SplashCoord cx, SplashCoord cy, SplashCoord *rx1, SplashCoord *ry1,
    SplashCoord *rx2, SplashCoord *ry2);

private:
  void makeBrush(SplashPattern *pattern, opvp_brush_t *brush);
  SplashError doClipPath(OPVPSplashPath *path, GBool eo,
     OPVPClipPath *prevClip);
  opvp_cspace_t getOPVPColorSpace();
  GBool equalPattern(SplashPattern *pt1, SplashPattern *pt2);
  SplashError makeRectanglePath(SplashCoord x0, SplashCoord y0,
    SplashCoord x1, SplashCoord y1, OPVPSplashPath **p);
  SplashError drawImageFastWithCTM(SplashImageSource src, void *srcData,
			      int w, int h, int tx, int ty,
			      SplashCoord *mat);
  SplashError drawImageNotShear(SplashImageSource src,
                              void *srcData,
			      int w, int h,
			      int tx, int ty,
			      int scaledWidth, int scaledHeight,
			      int xSign, int ySign, GBool rot);
  SplashError fillImageMaskFastWithCTM(SplashImageMaskSource src,
       void *srcData, int w, int h, int tx, int ty,SplashCoord *mat);
  SplashError strokeByMyself(OPVPSplashPath *path);
  SplashError fillByMyself(OPVPSplashPath *path, GBool eo);
  OPVPSplashXPath *makeDashedPath(OPVPSplashXPath *xPath);
  void transform(SplashCoord *matrix, SplashCoord xi, SplashCoord yi,
	   SplashCoord *xo, SplashCoord *yo);

  const char *getOption(const char *key, int nOptions, const char *optionKeys[],
    const char *optionVals[]);

  OPVPWrapper *opvp;
  int printerContext;

  OPVPSplashState *state;
  GBool debugMode;
  int colorMode;
  GBool stateBypass;
  OPVPClipPath *clipPath;

  GBool oldLipsDriver;
  GBool clipPathNotSaved;
  GBool noShearImage;
  GBool noLineStyle;
  GBool noClipPath;
  GBool noMiterLimit;
  GBool ignoreMiterLimit;
  GBool savedNoClipPath;
  GBool noImageMask;
  int bitmapCharThreshold;
  int maxClipPathLength;
  int maxFillPathLength;
  int saveDriverStateCount;
};

#endif

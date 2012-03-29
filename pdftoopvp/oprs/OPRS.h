//========================================================================
//
// OPRS.h
//
//========================================================================

#ifndef OPRS_H
#define OPRS_H

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include "splash/SplashTypes.h"
#include "opvp_common.h"
#include "splash/Splash.h"
#include "OPVPSplash.h"
#include "OPVPWrapper.h"

#define OPVP_BUFF_SIZE 256

class SplashBitmap;
class SplashGlyphBitmap;
class SplashState;
class SplashPattern;
class SplashScreen;
class OPVPSplashPath;
class SplashXPath;
class OPVPSplashClip;
class SplashFont;

//------------------------------------------------------------------------
// OPRS
//------------------------------------------------------------------------

class OPRS {
public:

  static void error(const char *msg, ...);
  OPRS();
  ~OPRS();

  int setBitmap(SplashBitmap *bitmapA);

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
  void setMiterLimit(SplashCoord miterLimit);
  void setLineCap(int lineCap);
  void setLineJoin(int lineJoin);
  void setFlatness(SplashCoord flatness);
  // the <lineDash> array will be copied
  void setLineDash(SplashCoord *lineDash, int lineDashLength,
		   SplashCoord lineDashPhase);
  SplashError clipToPath(OPVPSplashPath *path, GBool eo);

  //----- state save/restore

  void saveState();
  SplashError restoreState();

  void setSoftMask(SplashBitmap *softMaskA);

  //----- drawing operations

  // Fill the bitmap with <color>.  This is not subject to clipping.
  void clear(SplashColor color);

  // Stroke a path using the current stroke pattern.
  SplashError stroke(OPVPSplashPath *path);

  // Fill a path using the current fill pattern.
  SplashError fill(OPVPSplashPath *path, GBool eo);

  // Draw a character, using the current fill pattern.
  SplashError fillChar(SplashCoord x, SplashCoord y, int c, SplashFont *font,
    Unicode *u, double *fontMat);

  // Draw a glyph, using the current fill pattern.  This function does
  // not free any data, i.e., it ignores glyph->freeData.
  SplashError fillGlyph(SplashCoord x, SplashCoord y,
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
  SplashBitmap *getBitmap();

  // Toggle debug mode on or off.
  void setDebugMode(GBool debugModeA);

  int init(const char *driverName, int outputFD,
      const char *printerModel, int nOptions,
      const char *optionKeys[], const char *optionVals[]);
  void initGS(int colorMode, int w, int h, SplashColor paperColor);
  int setColorMode(int colorModeA, GBool colorProfile);
  int unloadVectorDriver();

  int OPVPStartJob(char *jobInfo);
  int OPVPEndJob();
  int OPVPStartDoc(char *docInfo);
  int OPVPEndDoc();
  int OPVPStartPage(char *pageInfo, int rasterWidth);
  int OPVPEndPage();
  int outSlice();
  Splash *getSplash() { return splash; }
  int getRasterMode() { return rasterMode; }
  void endPage();
  SplashCoord *getMatrix();

private:
  int rasterMode;
  OPVPSplash *opvpSplash;
  Splash *splash;
  OPVPWrapper *opvp;
  int getRasterSize(SplashBitmap *bitmap);

  typedef unsigned char *(*GetScanLineDataFunT)(unsigned char *dst,
    unsigned char *bitmap, int rasterWidth);

  static unsigned char *getScanLineDataMono1(unsigned char *dst,
    unsigned char *bitmap, int rasterWidth);
  static unsigned char *getScanLineDataMono8(unsigned char *dst,
    unsigned char *bitmap, int rasterWidth);
  static unsigned char *getScanLineDataRGB8(unsigned char *dst,
    unsigned char *bitmap, int rasterWidth);
  static unsigned char *getScanLineDataBGR8Packed(unsigned char *dst,
    unsigned char *bitmap, int rasterWidth);

  GetScanLineDataFunT getGetScanLineDataFun(SplashBitmap *bitmap);
  GBool checkAll1(unsigned char *bp, int n, int width, int mode);
};

#endif

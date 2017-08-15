//
// OPVPOutputDev.cc
// 	Based SplashOutputDev.cc : Copyright 2003 Glyph & Cog, LLC
//
// Copyright 2005 AXE,Inc.
//
// 2007,2008 Modified by BBR Inc.
//========================================================================

#include <config.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <string.h>
#include <math.h>
#include "goo/gfile.h"
#include "GlobalParams.h"
#include "OPVPError.h"
#include "Object.h"
#include "GfxState.h"
#include "GfxFont.h"
#include "Link.h"
#include "CharCodeToUnicode.h"
#include "FontEncodingTables.h"
#include "fofi/FoFiTrueType.h"
#include "splash/SplashMath.h"
#include "CMap.h"
#include "splash/SplashBitmap.h"
#include "splash/SplashGlyphBitmap.h"
#include "splash/SplashPattern.h"
#include "splash/SplashScreen.h"
#include "splash/SplashErrorCodes.h"
#include "splash/SplashFontEngine.h"
#include "splash/SplashFont.h"
#include "splash/SplashFontFile.h"
#include "splash/SplashFontFileID.h"
#include "OPVPSplashPath.h"
#include "OPVPSplashState.h"
#include "OPRS.h"
#include "OPVPOutputDev.h"

#define SLICE_FOR_PATTERN 1000

//------------------------------------------------------------------------
// Font substitutions
//------------------------------------------------------------------------

struct SplashOutFontSubst {
  char *name;
  double mWidth;
};

//------------------------------------------------------------------------

#define soutRound(x) ((int)(x + 0.5))

//------------------------------------------------------------------------
// SplashOutFontFileID
//------------------------------------------------------------------------

class SplashOutFontFileID: public SplashFontFileID {
public:

  SplashOutFontFileID(Ref *rA) { r = *rA; substIdx = -1; }

  ~SplashOutFontFileID() {}

  GBool matches(SplashFontFileID *id) {
    return ((SplashOutFontFileID *)id)->r.num == r.num &&
           ((SplashOutFontFileID *)id)->r.gen == r.gen;
  }

  void setSubstIdx(int substIdxA) { substIdx = substIdxA; }
  int getSubstIdx() { return substIdx; }

private:

  Ref r;
  int substIdx;
};

//------------------------------------------------------------------------
// T3FontCache
//------------------------------------------------------------------------

struct T3FontCacheTag {
  Gushort code;
  Gushort mru;			// valid bit (0x8000) and MRU index
};

class T3FontCache {
public:

  T3FontCache(Ref *fontID, double m11A, double m12A,
	      double m21A, double m22A,
	      int glyphXA, int glyphYA, int glyphWA, int glyphHA,
	      GBool aa);
  ~T3FontCache();
  GBool matches(Ref *idA, double m11A, double m12A,
		double m21A, double m22A)
    { return fontID.num == idA->num && fontID.gen == idA->gen &&
	     m11 == m11A && m12 == m12A && m21 == m21A && m22 == m22A; }

  Ref fontID;			// PDF font ID
  double m11, m12, m21, m22;	// transform matrix
  int glyphX, glyphY;		// pixel offset of glyph bitmaps
  int glyphW, glyphH;		// size of glyph bitmaps, in pixels
  int glyphSize;		// size of glyph bitmaps, in bytes
  int cacheSets;		// number of sets in cache
  int cacheAssoc;		// cache associativity (glyphs per set)
  Guchar *cacheData;		// glyph pixmap cache
  T3FontCacheTag *cacheTags;	// cache tags, i.e., char codes
};

T3FontCache::T3FontCache(Ref *fontIDA, double m11A, double m12A,
			 double m21A, double m22A,
			 int glyphXA, int glyphYA, int glyphWA, int glyphHA,
			 GBool aa) {
  int i;

  fontID = *fontIDA;
  m11 = m11A;
  m12 = m12A;
  m21 = m21A;
  m22 = m22A;
  glyphX = glyphXA;
  glyphY = glyphYA;
  glyphW = glyphWA;
  glyphH = glyphHA;
  if (aa) {
    glyphSize = glyphW * glyphH;
  } else {
    glyphSize = ((glyphW + 7) >> 3) * glyphH;
  }
  cacheAssoc = 8;
  if (glyphSize <= 256) {
    cacheSets = 8;
  } else if (glyphSize <= 512) {
    cacheSets = 4;
  } else if (glyphSize <= 1024) {
    cacheSets = 2;
  } else {
    cacheSets = 1;
  }
  cacheData = (Guchar *)gmallocn3(cacheSets , cacheAssoc , glyphSize);
  cacheTags = (T3FontCacheTag *)gmallocn3(cacheSets , cacheAssoc ,
					sizeof(T3FontCacheTag));
  for (i = 0; i < cacheSets * cacheAssoc; ++i) {
    cacheTags[i].mru = i & (cacheAssoc - 1);
  }
}

T3FontCache::~T3FontCache() {
  gfree(cacheData);
  gfree(cacheTags);
}

struct T3GlyphStack {
  Gushort code;			// character code
  double x, y;			// position to draw the glyph

  //----- cache info
  T3FontCache *cache;		// font cache for the current font
  T3FontCacheTag *cacheTag;	// pointer to cache tag for the glyph
  Guchar *cacheData;		// pointer to cache data for the glyph

  //----- saved state
  SplashBitmap *origBitmap;
  OPRS *origOPRS;
  double origCTM4, origCTM5;

  T3GlyphStack *next;		// next object on stack
};

//------------------------------------------------------------------------
// OPVPOutputDev
//------------------------------------------------------------------------

OPVPOutputDev::OPVPOutputDev()
{
  xref = 0;
  bitmap = 0;
  fontEngine = 0;
  nT3Fonts = 0;
  t3GlyphStack = 0;
  font = NULL;
  needFontUpdate = gFalse;
  textClipPath = 0;
  underlayCbk = 0;
  underlayCbkData = 0;
  scaleWidth = scaleHeight = -1;
  leftMargin = 0;
  bottomMargin = 0;
  rotate = 0;
  sliceHeight = 0;
  yoffset = 0;
  oprs = 0;
}

void OPVPOutputDev::setScale(double w, double h,
  double leftMarginA, double bottomMarginA, int rotateA,
  int yoffsetA, int sliceHeightA)
{
  scaleWidth = w;
  scaleHeight = h;
  leftMargin = leftMarginA;
  bottomMargin = bottomMarginA;
  rotate = rotateA;
  yoffset = yoffsetA;
  sliceHeight = sliceHeightA;
}

int OPVPOutputDev::init(SplashColorMode colorModeA,
				 GBool colorProfile,
				 GBool reverseVideoA,
				 SplashColor paperColorA,
                                 const char *driverName,
				 int outputFD,
				 const char *printerModel,
				 int nOptions,
				 const char *optionKeys[],
				 const char *optionVals[]) {
  int result;

  oprs = new OPRS();

  if ((result = oprs->init(driverName, outputFD, printerModel,
       nOptions,optionKeys,optionVals)) < 0) {
    opvpError(-1,"OPRS initialization fail");
    return result;
  }
  colorMode = colorModeA;
  if ((result = oprs->setColorMode(colorMode,colorProfile)) < 0) {
    opvpError(-1,"Can't setColorMode");
    return result;
  }
  reverseVideo = reverseVideoA;
  splashColorCopy(paperColor,paperColorA);

  return 0;
}

OPVPOutputDev::~OPVPOutputDev() {
  int i;

  for (i = 0; i < nT3Fonts; ++i) {
    delete t3FontCache[i];
  }
  if (fontEngine) {
    delete fontEngine;
  }
  if (oprs) {
    delete oprs;
  }
  if (bitmap) {
    delete bitmap;
  }
}

void OPVPOutputDev::startDoc(XRef *xrefA) {
  int i;

  xref = xrefA;
  if (fontEngine) {
    delete fontEngine;
  }
  fontEngine = new SplashFontEngine(
#if HAVE_T1LIB_H
				    globalParams->getEnableT1lib(),
#endif
#if HAVE_FREETYPE_FREETYPE_H || HAVE_FREETYPE_H
				    globalParams->getEnableFreeType(),
				    gFalse,
                                    gFalse,
#endif
#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 30
				    globalParams->getAntialias());
#else
                                    gFalse);
#endif
  for (i = 0; i < nT3Fonts; ++i) {
    delete t3FontCache[i];
  }
  nT3Fonts = 0;
}

void OPVPOutputDev::startPage(int pageNum, GfxState *state) {
  int w, h;

  if (state) {
    if (scaleWidth > 0 && scaleHeight > 0) {
      double *ctm = state->getCTM();

      switch (rotate) {
      case 90:
	state->setCTM(0,ctm[1],ctm[2],0,leftMargin,bottomMargin-yoffset);
	break;
      case 180:
	state->setCTM(ctm[0],0,0,ctm[3],paperWidth-leftMargin,
	  bottomMargin-yoffset);
	break;
      case 270:
	state->setCTM(0,ctm[1],ctm[2],0,paperWidth-leftMargin,
	  -bottomMargin+paperHeight-yoffset);
	break;
      default:
	state->setCTM(ctm[0],0,0,ctm[3],leftMargin,
	  -bottomMargin+paperHeight-yoffset);
	break;
      }
      state->concatCTM(scaleWidth,0.0,0.0,scaleHeight,0,0);
    }
    w = (int)(state->getPageWidth()+0.5);
    h = (int)(state->getPageHeight()+0.5);
  } else {
    w = h = 1;
  }
  oprs->initGS(colorMode,w,h,paperColor);

  if (underlayCbk) {
    (*underlayCbk)(underlayCbkData);
  }
}

void OPVPOutputDev::endPage() {
  oprs->endPage();
}

void OPVPOutputDev::saveState(GfxState *state) {
  oprs->saveState();
}

void OPVPOutputDev::restoreState(GfxState *state) {
  oprs->restoreState();
  needFontUpdate = gTrue;
}

void OPVPOutputDev::updateAll(GfxState *state) {
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
  updateFlatness(state);
  updateMiterLimit(state);
  updateFillColor(state);
  updateStrokeColor(state);
  needFontUpdate = gTrue;
}

void OPVPOutputDev::updateCTM(GfxState *state, double m11, double m12,
				double m21, double m22,
				double m31, double m32) {
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
}

void OPVPOutputDev::transLineDash(GfxState *state, SplashCoord **adash,
  int *adashLength, SplashCoord *aphase) {
  double *dashPattern;
  double dashStart;
  static SplashCoord dash[20];
  int i;

  state->getLineDash(&dashPattern, adashLength, &dashStart);
  if (*adashLength > 20) {
    *adashLength = 20;
  }
  for (i = 0; i < *adashLength; ++i) {
    dash[i] =  (SplashCoord)state->transformWidth(dashPattern[i]);
    if (dash[i] < 1) {
      dash[i] = 1;
    }
  }
  *adash = dash;
  *aphase = (SplashCoord)state->transformWidth(dashStart);
}

void OPVPOutputDev::updateSplashLineDash(GfxState *state, Splash *splash) {
  int dashLength;
  SplashCoord *dash;
  SplashCoord phase;

  transLineDash(state, &dash, &dashLength, &phase);
  splash->setLineDash(dash, dashLength, phase);
}

void OPVPOutputDev::updateLineDash(GfxState *state) {
  int dashLength;
  SplashCoord *dash;
  SplashCoord phase;

  transLineDash(state, &dash, &dashLength, &phase);
  oprs->setLineDash(dash, dashLength, phase);
}

void OPVPOutputDev::updateFlatness(GfxState *state) {
  oprs->setFlatness(state->getFlatness());
}

void OPVPOutputDev::updateLineJoin(GfxState *state) {
  oprs->setLineJoin(state->getLineJoin());
}

void OPVPOutputDev::updateLineCap(GfxState *state) {
  oprs->setLineCap(state->getLineCap());
}

void OPVPOutputDev::updateMiterLimit(GfxState *state) {
  oprs->setMiterLimit(state->getMiterLimit());
}

void OPVPOutputDev::updateLineWidth(GfxState *state) {
  oprs->setLineWidth(state->getTransformedLineWidth());
}

void OPVPOutputDev::updateFillColor(GfxState *state) {
  GfxGray gray;
  GfxRGB rgb;

  state->getFillGray(&gray);
  state->getFillRGB(&rgb);
  oprs->setFillPattern(getColor(gray, &rgb));
}

void OPVPOutputDev::updateStrokeColor(GfxState *state) {
  GfxGray gray;
  GfxRGB rgb;

  state->getStrokeGray(&gray);
  state->getStrokeRGB(&rgb);
  oprs->setStrokePattern(getColor(gray, &rgb));
}

#ifdef SPLASH_CMYK
SplashPattern *OPVPOutputDev::getColor(double gray, GfxRGB *rgb,
     GfxCMYK *cmyk) {
#else
SplashPattern *OPVPOutputDev::getColor(GfxGray gray, GfxRGB *rgb) {
#endif
  SplashPattern *pattern;
  SplashColor  color1;
  GfxColorComp r, g, b;

  if (reverseVideo) {
    gray = gfxColorComp1 - gray;
    r = gfxColorComp1 - rgb->r;
    g = gfxColorComp1 - rgb->g;
    b = gfxColorComp1 - rgb->b;
  } else {
    r = rgb->r;
    g = rgb->g;
    b = rgb->b;
  }

  pattern = NULL; // make gcc happy
  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    color1[0] = colToByte(gray);
    pattern = new SplashSolidColor(color1);
    break;
  case splashModeRGB8:
    color1[0] = colToByte(r);
    color1[1] = colToByte(g);
    color1[2] = colToByte(b);
    pattern = new SplashSolidColor(color1);
    break;
#if SPLASH_CMYK
  case splashModeCMYK8:
    color[0] = colToByte(cmyk->c);
    color[1] = colToByte(cmyk->m);
    color[2] = colToByte(cmyk->y);
    color[3] = colToByte(cmyk->k);
    pattern = new SplashSolidColor(color);
    break;
#endif
  default:
    opvpError(-1, "no supported color mode");
    break;
  }

  return pattern;
}

void OPVPOutputDev::updateFont(GfxState *state) {
    needFontUpdate = gTrue;
}

void OPVPOutputDev::doUpdateFont(GfxState *state) {
  GfxFont *gfxFont;
  GfxFontType fontType;
  SplashOutFontFileID *id;
  SplashFontFile *fontFile;
  SplashFontSrc *fontsrc = NULL;
  FoFiTrueType *ff;
  Ref embRef;
  Object refObj, strObj;
  GooString *fileName;
  char *tmpBuf;
  int tmpBufLen;
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  int *codeToGID;
#else
  Gushort *codeToGID;
#endif
  double m11, m12, m21, m22;
  int n;
  int faceIndex = 0;
  GBool recreateFont = gFalse;

  needFontUpdate = gFalse;
  font = NULL;
  fileName = NULL;
  tmpBuf = NULL;

  if (!(gfxFont = state->getFont())) {
    goto err1;
  }
  fontType = gfxFont->getType();
  if (fontType == fontType3) {
    goto err1;
  }

  // check the font file cache
  id = new SplashOutFontFileID(gfxFont->getID());
  if ((fontFile = fontEngine->getFontFile(id))) {
    delete id;

  } else {

    // if there is an embedded font, write it to disk
    if (gfxFont->getEmbeddedFontID(&embRef)) {
      tmpBuf = gfxFont->readEmbFontFile(xref, &tmpBufLen);
      if (! tmpBuf)
	goto err2;

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
    } else {
      SysFontType sftype;
      fileName = globalParams->findSystemFontFile(gfxFont,&sftype,
                          &faceIndex, NULL);
      if (fileName == 0) {
	opvpError(-1, "Couldn't find a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      switch (sftype) {
      case sysFontPFA:
      case sysFontPFB:
	fontType = gfxFont->isCIDFont() ? fontCIDType0 : fontType1;
	break;
      case sysFontTTF:
      case sysFontTTC:
	fontType = gfxFont->isCIDFont() ? fontCIDType2 : fontTrueType;
	break;
      }
    }
#else
    // if there is an external font file, use it
    } else if (!(fileName = gfxFont->getExtFontFile())) {
      DisplayFontParam *dfp;
      // look for a display font mapping or a substitute font
      dfp = NULL;
      if (gfxFont->getName()) {
        dfp = globalParams->getDisplayFont(gfxFont);
      }
      if (!dfp) {
	opvpError(-1, "Couldn't find a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      switch (dfp->kind) {
      case displayFontT1:
	fileName = dfp->t1.fileName;
	fontType = gfxFont->isCIDFont() ? fontCIDType0 : fontType1;
	break;
      case displayFontTT:
	fileName = dfp->tt.fileName;
	fontType = gfxFont->isCIDFont() ? fontCIDType2 : fontTrueType;
	faceIndex = dfp->tt.faceIndex;
	break;
      }
    }
#endif

    fontsrc = new SplashFontSrc;
    if (fileName)
      fontsrc->setFile(fileName, gFalse);
    else
      fontsrc->setBuf(tmpBuf, tmpBufLen, gTrue);

    // load the font file
    switch (fontType) {
    case fontType1:
      if (!(fontFile = fontEngine->loadType1Font(
			   id,
			   fontsrc,
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
                           (const char **)
#endif
			   ((Gfx8BitFont *)gfxFont)->getEncoding()))) {
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontType1C:
      if (!(fontFile = fontEngine->loadType1CFont(
			   id,
			   fontsrc,
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
                           (const char **)
#endif
			   ((Gfx8BitFont *)gfxFont)->getEncoding()))) {
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontType1COT:
      if (!(fontFile = fontEngine->loadOpenTypeT1CFont(
			   id,
			   fontsrc,
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
                           (const char **)
#endif
			   ((Gfx8BitFont *)gfxFont)->getEncoding()))) {
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontTrueTypeOT:
    case fontTrueType:
	if (fileName)
	 ff = FoFiTrueType::load(fileName->getCString());
	else
	ff = FoFiTrueType::make(tmpBuf, tmpBufLen);
      if (ff) {
	codeToGID = ((Gfx8BitFont *)gfxFont)->getCodeToGIDMap(ff);
	n = 256;
	delete ff;
      } else {
	codeToGID = NULL;
	n = 0;
      }
      if (!(fontFile = fontEngine->loadTrueTypeFont(
			   id,
			   fontsrc,
			   codeToGID, n))) {
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontCIDType0:
    case fontCIDType0C:
      if (!(fontFile = fontEngine->loadCIDFont(
			   id,
			   fontsrc))) {
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontCIDType0COT:
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
      n = ((GfxCIDFont *)gfxFont)->getCIDToGIDLen();
      if (n) {
        codeToGID = (int *)gmallocn(n, sizeof(int));
        memcpy(codeToGID, ((GfxCIDFont *)gfxFont)->getCIDToGID(),
                n * sizeof(int));
      } else {
          codeToGID = NULL;
      }
      if (!(fontFile = fontEngine->loadOpenTypeCFFFont(
			   id,
			   fontsrc,codeToGID,n))) {
#else
      if (!(fontFile = fontEngine->loadOpenTypeCFFFont(
			   id,
			   fontsrc))) {
#endif
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontCIDType2OT:
    case fontCIDType2:
      codeToGID = NULL;
      n = 0;
      if (((GfxCIDFont *)gfxFont)->getCIDToGID()) {
	n = ((GfxCIDFont *)gfxFont)->getCIDToGIDLen();
	if (n) {
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
	  codeToGID = (int *)gmallocn(n, sizeof(int));
	  memcpy(codeToGID, ((GfxCIDFont *)gfxFont)->getCIDToGID(),
		  n * sizeof(int));
#else
	  codeToGID = (Gushort *)gmallocn(n, sizeof(Gushort));
	  memcpy(codeToGID, ((GfxCIDFont *)gfxFont)->getCIDToGID(),
		  n * sizeof(Gushort));
#endif
	}
      } else {
	if (fileName)
	  ff = FoFiTrueType::load(fileName->getCString());
	else
	  ff = FoFiTrueType::make(tmpBuf, tmpBufLen);
	if (! ff)
	  goto err2;
	codeToGID = ((GfxCIDFont *)gfxFont)->getCodeToGIDMap(ff, &n);
	delete ff;
      }
      if (!(fontFile = fontEngine->loadTrueTypeFont(
			   id,
			   fontsrc,
			   codeToGID, n, faceIndex))) {
	opvpError(-1, "Couldn't create a font for '%s'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    default:
      // this shouldn't happen
      goto err2;
    }
    fontFile->doAdjustMatrix = gTrue;
  }

  // get the font matrix
  state->getFontTransMat(&m11, &m12, &m21, &m22);
  m11 *= state->getHorizScaling();
  m12 *= state->getHorizScaling();

  // create the scaled font
  fontMat[0] = m11;  fontMat[1] = m12;
  fontMat[2] = m21;  fontMat[3] = m22;
  font = fontEngine->getFont(fontFile, fontMat, oprs->getMatrix());

  // for substituted fonts: adjust the font matrix -- compare the
  // width of 'm' in the original font and the substituted font
  if (fontFile->doAdjustMatrix && !gfxFont->isCIDFont()) {
    double w1, w2;
    CharCode code;
    char *name;
    for (code = 0; code < 256; ++code) {
      if ((name = ((Gfx8BitFont *)gfxFont)->getCharName(code)) &&
          name[0] == 'm' && name[1] == '\0') {
        break;
      }
    }
    if (code < 256) {
      w1 = ((Gfx8BitFont *)gfxFont)->getWidth(code);
      w2 = font->getGlyphAdvance(code);
      if (!gfxFont->isSymbolic() && w2 > 0) {
        // if real font is substantially narrower than substituted
        // font, reduce the font size accordingly
        if (w1 > 0.01 && w1 < 0.9 * w2) {
          w1 /= w2;
          m11 *= w1;
          m21 *= w1;
          recreateFont = gTrue;
        }
      }
    }
  }

  if (recreateFont)
  {
    fontMat[0] = m11;  fontMat[1] = m12;
    fontMat[2] = m21;  fontMat[3] = m22;
    font = fontEngine->getFont(fontFile, fontMat, oprs->getMatrix());
  }

  if (fontsrc && !fontsrc->isFile)
      fontsrc->unref();
  return;

 err2:
  delete id;
 err1:
  if (fontsrc && !fontsrc->isFile)
      fontsrc->unref();
  return;
}

void OPVPOutputDev::stroke(GfxState *state) {
  OPVPSplashPath *path;
  GfxColorSpace *cs;

  /* check None separate color */
  if ((cs = state->getStrokeColorSpace()) == NULL) return;
  if (cs->getMode() == csSeparation) {
    GooString *name;

    name = (dynamic_cast<GfxSeparationColorSpace *>(cs))->getName();
    if (name == NULL) return;
    if (name->cmp("None") == 0) return;
  }

  path = convertPath(state, state->getPath());
  oprs->stroke(path);
  delete path;
}

void OPVPOutputDev::fill(GfxState *state) {
  OPVPSplashPath *path;
  GfxColorSpace *cs;

  /* check None separate color */
  if ((cs = state->getFillColorSpace()) == NULL) return;
  if (cs->getMode() == csSeparation) {
    GooString *name;

    name = (dynamic_cast<GfxSeparationColorSpace *>(cs))->getName();
    if (name == NULL) return;
    if (name->cmp("None") == 0) return;
  }

  path = convertPath(state, state->getPath());
  oprs->fill(path, gFalse);
  delete path;
}

void OPVPOutputDev::eoFill(GfxState *state) {
  OPVPSplashPath *path;
  GfxColorSpace *cs;

  /* check None separate color */
  if ((cs = state->getFillColorSpace()) == NULL) return;
  if (cs->getMode() == csSeparation) {
    GooString *name;

    name = (dynamic_cast<GfxSeparationColorSpace *>(cs))->getName();
    if (name == NULL) return;
    if (name->cmp("None") == 0) return;
  }

  path = convertPath(state, state->getPath());
  oprs->fill(path, gTrue);
  delete path;
}

void OPVPOutputDev::clip(GfxState *state) {
  OPVPSplashPath *path;

  path = convertPath(state, state->getPath());
  oprs->clipToPath(path, gFalse);
  delete path;
}

void OPVPOutputDev::eoClip(GfxState *state) {
  OPVPSplashPath *path;

  path = convertPath(state, state->getPath());
  oprs->clipToPath(path, gTrue);
  delete path;
}

OPVPSplashPath *OPVPOutputDev::bitmapToPath(SplashBitmap *bitmapA,
    int width, int height)
{
  int x,y;
  OPVPSplashPath *path;
  int x1, x2;
  SplashColor pix;

  path =  new OPVPSplashPath();

  for (y = 0;y < height;y++) {
    for (x = 0;x < width;x++) {
      bitmapA->getPixel(x,y,pix);
      if (pix[0] == 0) {
	/* start */
	x1 = x;
	for (x++;x < width;x++) {
	  bitmapA->getPixel(x,y,pix);
	  if (pix[0] != 0) {
	    /* end */
	    break;
	  }
	}
	x2 = x-1;
	path->moveTo(x1,y);
	path->lineTo(x2,y);
	path->lineTo(x2,(y+1));
	path->lineTo(x1,(y+1));
	path->close();
      }
    }
  }
  return path;
}

void OPVPOutputDev::clipToStrokePath(GfxState *state) {
  SplashBitmap *tbitmap;
  Splash *tsplash;
  SplashPath *spath;
  OPVPSplashPath *path, *path2;

  // use splash for makeStrokePath
  // create dummy bitmap for creating splash
  tbitmap = new SplashBitmap(1, 1, 1, splashModeMono1, gFalse);
  tsplash = new Splash(tbitmap, gFalse);
  // set line parameters
  //  except colors
  updateSplashLineDash(state, tsplash);
  tsplash->setLineJoin(state->getLineJoin());
  tsplash->setLineCap(state->getLineCap());
  tsplash->setMiterLimit(state->getMiterLimit());
  tsplash->setLineWidth(state->getTransformedLineWidth());

  path = convertPath(state, state->getPath());
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  spath = tsplash->makeStrokePath(path,0);
#else
  spath = tsplash->makeStrokePath(path);
#endif
  path2 = new OPVPSplashPath(spath);
  delete spath;
  delete path;
  delete tsplash;
  delete tbitmap;
  oprs->clipToPath(path2, gFalse);
  delete path2;
}

OPVPSplashPath *OPVPOutputDev::convertPath(GfxState *state, GfxPath *path) {
  OPVPSplashPath *sPath;
  GfxSubpath *subpath;
  double x1, y1, x2, y2, x3, y3;
  int i, j;

  sPath = new OPVPSplashPath();
  for (i = 0; i < path->getNumSubpaths(); ++i) {
    subpath = path->getSubpath(i);
    if (subpath->getNumPoints() > 0) {
      state->transform(subpath->getX(0), subpath->getY(0), &x1, &y1);
      sPath->moveTo((SplashCoord)x1, (SplashCoord)y1);
      j = 1;
      while (j < subpath->getNumPoints()) {
	if (subpath->getCurve(j)) {
	  state->transform(subpath->getX(j), subpath->getY(j), &x1, &y1);
	  state->transform(subpath->getX(j+1), subpath->getY(j+1), &x2, &y2);
	  state->transform(subpath->getX(j+2), subpath->getY(j+2), &x3, &y3);
	  sPath->curveTo((SplashCoord)x1, (SplashCoord)y1,
			 (SplashCoord)x2, (SplashCoord)y2,
			 (SplashCoord)x3, (SplashCoord)y3);
	  j += 3;
	} else {
	  state->transform(subpath->getX(j), subpath->getY(j), &x1, &y1);
	  sPath->lineTo((SplashCoord)x1, (SplashCoord)y1);
	  ++j;
	}
      }
      if (subpath->isClosed()) {
	sPath->close();
      }
    }
  }
  return sPath;
}

void OPVPOutputDev::drawChar(GfxState *state, double x, double y,
			       double dx, double dy,
			       double originX, double originY,
			       CharCode code, int nBytes,
			       Unicode *u, int uLen) {
  double x1, y1;
  SplashPath *spath;
  OPVPSplashPath *path;
  int render;

  // check for invisible text -- this is used by Acrobat Capture
  render = state->getRender();
  if (render == 3) {
    return;
  }

  if (needFontUpdate) {
    doUpdateFont(state);
  }
  if (!font) {
    return;
  }

  x -= originX;
  y -= originY;
  state->transform(x,y,&x1,&y1);

  // fill
  if (!(render & 1)) {
    oprs->fillChar((SplashCoord)x1, (SplashCoord)y1, code, font, u, fontMat);
  }

  // stroke
  if ((render & 3) == 1 || (render & 3) == 2) {
    if ((spath = font->getGlyphPath(code))) {
      path = new OPVPSplashPath(spath);
      delete spath;
      path->closeAllSubPath();
      path->offset((SplashCoord)x1, (SplashCoord)y1);
      oprs->stroke(path);
      delete path;
    } else {
      opvpError(-1,"No glyph outline infomation");
    }
  }

  // clip
  if (render & 4) {
    if ((spath = font->getGlyphPath(code)) != NULL) {
      path = new OPVPSplashPath(spath);
      delete spath;
      path->offset((SplashCoord)x1, (SplashCoord)y1);
      if (textClipPath) {
	textClipPath->append(path);
	delete path;
      } else {
	textClipPath = path;
      }
    } else {
      opvpError(-1,"No glyph outline infomation");
    }
  }
}

GBool OPVPOutputDev::beginType3Char(GfxState *state, double x, double y,
				      double dx, double dy,
				      CharCode code, Unicode *u, int uLen) {
  /* In a vector mode, cache is not needed */
  return gFalse;
}

void OPVPOutputDev::endType3Char(GfxState *state) {
  /* In a vector mode, cache is not needed */
  /* do nothing */
}

void OPVPOutputDev::type3D0(GfxState *state, double wx, double wy) {
  /* In a vector mode, cache is not needed */
  /* do nothing */
}

void OPVPOutputDev::type3D1(GfxState *state, double wx, double wy,
			      double llx, double lly, double urx, double ury) {
}

void OPVPOutputDev::drawType3Glyph(T3FontCache *t3Font,
				     T3FontCacheTag *tag, Guchar *data,
				     double x, double y) {
  SplashGlyphBitmap glyph;

  glyph.x = -t3Font->glyphX;
  glyph.y = -t3Font->glyphY;
  glyph.w = t3Font->glyphW;
  glyph.h = t3Font->glyphH;
  glyph.aa = colorMode != splashModeMono1;
  glyph.data = data;
  glyph.freeData = gFalse;
  oprs->fillGlyph((SplashCoord)x, (SplashCoord)y, &glyph);
}

void OPVPOutputDev::endTextObject(GfxState *state) {
  if (textClipPath) {
    oprs->clipToPath(textClipPath, gFalse);
    delete textClipPath;
    textClipPath = NULL;
  }
}

struct SplashOutImageMaskData {
  ImageStream *imgStr;
  GBool invert;
  int width, height, y;
};

GBool OPVPOutputDev::imageMaskSrc(void *data, SplashColorPtr line) {
  SplashOutImageMaskData *imgMaskData = (SplashOutImageMaskData *)data;
  Guchar *p;
  SplashColorPtr q;
  int x;

  if (imgMaskData->y == imgMaskData->height) {
    return gFalse;
  }
  for (x = 0, p = imgMaskData->imgStr->getLine(), q = line;
       x < imgMaskData->width;
       ++x) {
    *q++ = *p++ ^ imgMaskData->invert;
  }
  ++imgMaskData->y;
  return gTrue;
}

void OPVPOutputDev::drawImageMask(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GBool invert,
				    GBool interpolate,
				    GBool inlineImg) {
  double *ctm;
  SplashCoord mat[6];
  SplashOutImageMaskData imgMaskData;

  ctm = state->getCTM();
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];

  imgMaskData.imgStr = new ImageStream(str, width, 1, 1);
  imgMaskData.imgStr->reset();
  imgMaskData.invert = invert ? 0 : 1;
  imgMaskData.width = width;
  imgMaskData.height =  height;
  imgMaskData.y = 0;

  oprs->fillImageMask(&imageMaskSrc, &imgMaskData, width, height, mat,
  		t3GlyphStack != NULL);
  if (inlineImg) {
    while (imgMaskData.y < height) {
      imgMaskData.imgStr->getLine();
      ++imgMaskData.y;
    }
  }

  delete imgMaskData.imgStr;
}

struct SplashOutImageData {
  ImageStream *imgStr;
  GfxImageColorMap *colorMap;
  SplashColorPtr lookup;
  int *maskColors;
  SplashColorMode colorMode;
  int width, height, y;
};

GBool OPVPOutputDev::imageSrc(void *data, SplashColorPtr line,
                              Guchar *alphaLine)
{
  SplashOutImageData *imgData = (SplashOutImageData *)data;
  Guchar *p;
  SplashColorPtr q, col;
  GfxRGB rgb;
  GfxGray gray;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif
  int nComps, x;

  if (imgData->y == imgData->height) {
    return gFalse;
  }

  nComps = imgData->colorMap->getNumPixelComps();

  if (imgData->lookup) {
    switch (imgData->colorMode) {
    case splashModeMono1:
    case splashModeMono8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, ++p) {
	*q++ = imgData->lookup[*p];
      }
      break;
    case splashModeRGB8:
    case splashModeBGR8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, ++p) {
	col = &imgData->lookup[3 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
      }
      break;
#if SPLASH_CMYK
    case splashModeCMYK8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, ++p) {
	col = &imgData->lookup[4 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	*q++ = col[3];
      }
      break;
#endif
    default:
      //~ unimplemented
      break;
  }
  } else {
    switch (imgData->colorMode) {
    case splashModeMono1:
    case splashModeMono8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, p += nComps) {
	imgData->colorMap->getGray(p, &gray);
	*q++ = colToByte(gray);
      }
	break;
    case splashModeRGB8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, p += nComps) {
	imgData->colorMap->getRGB(p, &rgb);
	*q++ = colToByte(rgb.r);
	*q++ = colToByte(rgb.g);
	*q++ = colToByte(rgb.b);
      }
      break;
    case splashModeBGR8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, p += nComps) {
	imgData->colorMap->getRGB(p, &rgb);
	*q++ = colToByte(rgb.b);
	*q++ = colToByte(rgb.g);
	*q++ = colToByte(rgb.r);
      }
      break;
#if SPLASH_CMYK
    case splashModeCMYK8:
      for (x = 0, p = imgData->imgStr->getLine(), q = line;
	   x < imgData->width;
	   ++x, p += nComps) {
	imgData->colorMap->getCMYK(p, &cmyk);
	*q++ = colToByte(cmyk.c);
	*q++ = colToByte(cmyk.m);
	*q++ = colToByte(cmyk.y);
	*q++ = colToByte(cmyk.k);
      }
      break;
#endif
    default:
      //~ unimplemented
      break;
    }
  }

  ++imgData->y;
  return gTrue;
}

GBool OPVPOutputDev::alphaImageSrc(void *data, SplashColorPtr line,
                                  Guchar *alphaLine) {
  SplashOutImageData *imgData = (SplashOutImageData *)data;
  Guchar *p;
  SplashColorPtr q, col;
  GfxRGB rgb;
  GfxGray gray;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif
  Guchar alpha;
  int nComps, x, i;

  if (imgData->y == imgData->height) {
    return gFalse;
  }

  nComps = imgData->colorMap->getNumPixelComps();

  for (x = 0, p = imgData->imgStr->getLine(), q = line;
       x < imgData->width;
       ++x, p += nComps) {
    alpha = 0;
    for (i = 0; i < nComps; ++i) {
      if (p[i] < imgData->maskColors[2*i] ||
	  p[i] > imgData->maskColors[2*i+1]) {
	alpha = 0xff;
	break;
      }
    }
    if (imgData->lookup) {
      switch (imgData->colorMode) {
      case splashModeMono1:
      case splashModeMono8:
	*q++ = alpha;
	*q++ = imgData->lookup[*p];
	break;
      case splashModeRGB8:
	*q++ = alpha;
	col = &imgData->lookup[3 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	break;
      case splashModeBGR8:
	col = &imgData->lookup[3 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	*q++ = alpha;
	break;
#if SPLASH_CMYK
      case splashModeCMYK8:
	*q++ = alpha;
	col = &imgData->lookup[4 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	*q++ = col[3];
	break;
#endif
      default:
	//~ unimplemented
	break;
      }
    } else {
      switch (imgData->colorMode) {
      case splashModeMono1:
      case splashModeMono8:
	imgData->colorMap->getGray(p, &gray);
	*q++ = alpha;
	*q++ = colToByte(gray);
	break;
      case splashModeRGB8:
	imgData->colorMap->getRGB(p, &rgb);
	*q++ = alpha;
	*q++ = colToByte(rgb.r);
	*q++ = colToByte(rgb.g);
	*q++ = colToByte(rgb.b);
	break;
      case splashModeBGR8:
	imgData->colorMap->getRGB(p, &rgb);
	*q++ = colToByte(rgb.b);
	*q++ = colToByte(rgb.g);
	*q++ = colToByte(rgb.r);
	*q++ = alpha;
	break;
#if SPLASH_CMYK
      case splashModeCMYK8:
	imgData->colorMap->getCMYK(p, &cmyk);
	*q++ = alpha;
	*q++ = colToByte(cmyk.c);
	*q++ = colToByte(cmyk.m);
	*q++ = colToByte(cmyk.y);
	*q++ = colToByte(cmyk.k);
	break;
#endif
      default:
	//~ unimplemented
	break;
      }
    }
  }

  ++imgData->y;
  return gTrue;
}

void OPVPOutputDev::drawImage(GfxState *state, Object *ref, Stream *str,
				int width, int height,
				GfxImageColorMap *colorMap,
			        GBool interpolate,
				int *maskColors, GBool inlineImg) {
  double *ctm;
  SplashCoord mat[6];
  SplashOutImageData imgData;
  SplashColorMode srcMode;
  SplashImageSource src;
  GfxGray gray;
  GfxRGB rgb;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif
  Guchar pix;
  int n, i;

  ctm = state->getCTM();
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];

  imgData.imgStr = new ImageStream(str, width,
				   colorMap->getNumPixelComps(),
				   colorMap->getBits());
  imgData.imgStr->reset();
  imgData.colorMap = colorMap;
  imgData.maskColors = maskColors;
  imgData.colorMode = colorMode;
  imgData.width = width;
  imgData.height = height;
  imgData.y = 0;

  // special case for one-channel (monochrome/gray/separation) images:
  // build a lookup table here
  imgData.lookup = NULL;
  if (colorMap->getNumPixelComps() == 1) {
    n = 1 << colorMap->getBits();
    switch (colorMode) {
    case splashModeMono1:
    case splashModeMono8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,1);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getGray(&pix, &gray);
	imgData.lookup[i] = colToByte(gray);
      }
      break;
    case splashModeRGB8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,3);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getRGB(&pix, &rgb);
	imgData.lookup[3*i] = colToByte(rgb.r);
	imgData.lookup[3*i+1] = colToByte(rgb.g);
	imgData.lookup[3*i+2] = colToByte(rgb.b);
      }
      break;
    case splashModeBGR8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,3);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getRGB(&pix, &rgb);
	imgData.lookup[3*i] = colToByte(rgb.b);
	imgData.lookup[3*i+1] = colToByte(rgb.g);
	imgData.lookup[3*i+2] = colToByte(rgb.r);
      }
      break;
#if SPLASH_CMYK
    case splashModeCMYK8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,4);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getCMYK(&pix, &cmyk);
	imgData.lookup[4*i] = colToByte(cmyk.c);
	imgData.lookup[4*i+1] = colToByte(cmyk.m);
	imgData.lookup[4*i+2] = colToByte(cmyk.y);
	imgData.lookup[4*i+3] = colToByte(cmyk.k);
      }
      break;
#endif
    default:
      //~ unimplemented
      break;
    }
  }

  if (colorMode == splashModeMono1) {
    srcMode = splashModeMono8;
  } else {
    srcMode = colorMode;
  }
  src = maskColors ? &alphaImageSrc : &imageSrc;
  oprs->drawImage(src, &imgData, srcMode, maskColors ? gTrue : gFalse,
                  width, height, mat);
  if (inlineImg) {
    while (imgData.y < height) {
      imgData.imgStr->getLine();
      ++imgData.y;
    }
  }

  gfree(imgData.lookup);
  delete imgData.imgStr;
  str->close();
}

struct SplashOutMaskedImageData {
  ImageStream *imgStr;
  GfxImageColorMap *colorMap;
  SplashBitmap *mask;
  SplashColorPtr lookup;
  SplashColorMode colorMode;
  int width, height, y;
};

GBool OPVPOutputDev::maskedImageSrc(void *data, SplashColorPtr line,
     Guchar *alphaLine) {
  SplashOutMaskedImageData *imgData = (SplashOutMaskedImageData *)data;
  Guchar *p;
  SplashColor maskColor;
  SplashColorPtr q, col;
  GfxRGB rgb;
  GfxGray gray;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif
  Guchar alpha;
  int nComps, x;

  if (imgData->y == imgData->height) {
    return gFalse;
  }

  nComps = imgData->colorMap->getNumPixelComps();

  for (x = 0, p = imgData->imgStr->getLine(), q = line;
       x < imgData->width;
       ++x, p += nComps) {
    imgData->mask->getPixel(x, imgData->y, maskColor);
    alpha = maskColor[0] ? 0xff : 0x00;
    if (imgData->lookup) {
      switch (imgData->colorMode) {
      case splashModeMono1:
      case splashModeMono8:
	*q++ = alpha;
	*q++ = imgData->lookup[*p];
	break;
      case splashModeRGB8:
	*q++ = alpha;
	col = &imgData->lookup[3 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	break;
      case splashModeBGR8:
	col = &imgData->lookup[3 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	*q++ = alpha;
	break;
#if SPLASH_CMYK
      case splashModeCMYK8:
	*q++ = alpha;
	col = &imgData->lookup[4 * *p];
	*q++ = col[0];
	*q++ = col[1];
	*q++ = col[2];
	*q++ = col[3];
	break;
#endif
      default:
	//~ unimplemented
	break;
      }
    } else {
      switch (imgData->colorMode) {
      case splashModeMono1:
      case splashModeMono8:
	imgData->colorMap->getGray(p, &gray);
	*q++ = alpha;
	*q++ = colToByte(gray);
	break;
      case splashModeRGB8:
	imgData->colorMap->getRGB(p, &rgb);
	*q++ = alpha;
	*q++ = colToByte(rgb.r);
	*q++ = colToByte(rgb.g);
	*q++ = colToByte(rgb.b);
	break;
      case splashModeBGR8:
	imgData->colorMap->getRGB(p, &rgb);
	*q++ = colToByte(rgb.b);
	*q++ = colToByte(rgb.g);
	*q++ = colToByte(rgb.r);
	*q++ = alpha;
	break;
#if SPLASH_CMYK
      case splashModeCMYK8:
	imgData->colorMap->getCMYK(p, &cmyk);
	*q++ = alpha;
	*q++ = colToByte(cmyk.c);
	*q++ = colToByte(cmyk.m);
	*q++ = colToByte(cmyk.y);
	*q++ = colToByte(cmyk.k);
	break;
#endif
      default:
	//~ unimplemented
	break;
      }
    }
  }

  ++imgData->y;
  return gTrue;
}

void OPVPOutputDev::drawMaskedImage(GfxState *state, Object *ref,
				      Stream *str, int width, int height,
				      GfxImageColorMap *colorMap,
				      GBool interpolate,
				      Stream *maskStr, int maskWidth,
				      int maskHeight, GBool maskInvert,
				      GBool maskInterpolate) {
  double *ctm;
  SplashCoord mat[6];
  SplashOutMaskedImageData imgData;
  SplashOutImageMaskData imgMaskData;
  SplashColorMode srcMode;
  SplashBitmap *maskBitmap;
  Splash *maskSplash;
  SplashColor maskColor;
  GfxGray gray;
  GfxRGB rgb;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif
  Guchar pix;
  int n, i;

  //----- scale the mask image to the same size as the source image

  mat[0] = (SplashCoord)width;
  mat[1] = 0;
  mat[2] = 0;
  mat[3] = (SplashCoord)height;
  mat[4] = 0;
  mat[5] = 0;
  imgMaskData.imgStr = new ImageStream(maskStr, maskWidth, 1, 1);
  imgMaskData.imgStr->reset();
  imgMaskData.invert = maskInvert ? 0 : 1;
  imgMaskData.width = maskWidth;
  imgMaskData.height = maskHeight;
  imgMaskData.y = 0;
  maskBitmap = new SplashBitmap(width, height, 1, splashModeMono1, gFalse);
  maskSplash = new Splash(maskBitmap, gFalse);
  maskColor[0] = 0;
  maskSplash->clear(maskColor);
  maskColor[0] = 1;
  maskSplash->setFillPattern(new SplashSolidColor(maskColor));
  maskSplash->fillImageMask(&imageMaskSrc, &imgMaskData,
			    maskWidth, maskHeight, mat, gFalse);
  delete imgMaskData.imgStr;
  maskStr->close();
  delete maskSplash;

  //----- draw the source image

  ctm = state->getCTM();
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];

  imgData.imgStr = new ImageStream(str, width,
				   colorMap->getNumPixelComps(),
				   colorMap->getBits());
  imgData.imgStr->reset();
  imgData.colorMap = colorMap;
  imgData.mask = maskBitmap;
  imgData.colorMode = colorMode;
  imgData.width = width;
  imgData.height = height;
  imgData.y = 0;

  // special case for one-channel (monochrome/gray/separation) images:
  // build a lookup table here
  imgData.lookup = NULL;
  if (colorMap->getNumPixelComps() == 1) {
    n = 1 << colorMap->getBits();
    switch (colorMode) {
    case splashModeMono1:
    case splashModeMono8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,1);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getGray(&pix, &gray);
	imgData.lookup[i] = colToByte(gray);
      }
      break;
    case splashModeRGB8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,3);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getRGB(&pix, &rgb);
	imgData.lookup[3*i] = colToByte(rgb.r);
	imgData.lookup[3*i+1] = colToByte(rgb.g);
	imgData.lookup[3*i+2] = colToByte(rgb.b);
      }
      break;
    case splashModeBGR8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,3);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getRGB(&pix, &rgb);
	imgData.lookup[3*i] = colToByte(rgb.b);
	imgData.lookup[3*i+1] = colToByte(rgb.g);
	imgData.lookup[3*i+2] = colToByte(rgb.r);
      }
      break;
#if SPLASH_CMYK
    case splashModeCMYK8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,4);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getCMYK(&pix, &cmyk);
	imgData.lookup[4*i] = colToByte(cmyk.c);
	imgData.lookup[4*i+1] = colToByte(cmyk.m);
	imgData.lookup[4*i+2] = colToByte(cmyk.y);
	imgData.lookup[4*i+3] = colToByte(cmyk.k);
      }
      break;
#endif
    default:
      //~ unimplemented
      break;
    }
  }

  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    srcMode = splashModeMono8;
    break;
  case splashModeRGB8:
    srcMode = splashModeRGB8;
    break;
  case splashModeBGR8:
    srcMode = splashModeBGR8;
    break;
#if SPLASH_CMYK
  case splashModeCMYK8:
    srcMode = splashModeCMYK8;
    break;
#endif
  default:
    //~ unimplemented
    srcMode = splashModeRGB8;
    break;
  }  
  oprs->drawImage(&maskedImageSrc, &imgData, srcMode, gTrue, 
                  width, height, mat);

  delete maskBitmap;
  gfree(imgData.lookup);
  delete imgData.imgStr;
  str->close();
}

void OPVPOutputDev::drawSoftMaskedImage(GfxState *state, Object *ref,
					  Stream *str, int width, int height,
					  GfxImageColorMap *colorMap,
					  GBool interpolate,
					  Stream *maskStr,
					  int maskWidth, int maskHeight,
					  GfxImageColorMap *maskColorMap,
					  GBool maskInterpolate) {
  double *ctm;
  SplashCoord mat[6];
  SplashOutImageData imgData;
  SplashOutImageData imgMaskData;
  SplashColorMode srcMode;
  SplashBitmap *maskBitmap;
  Splash *maskSplash;
  SplashColor maskColor;
  GfxGray gray;
  GfxRGB rgb;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif
  Guchar pix;
  int n, i;

  ctm = state->getCTM();
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];

  //----- set up the soft mask

  imgMaskData.imgStr = new ImageStream(maskStr, maskWidth,
				       maskColorMap->getNumPixelComps(),
				       maskColorMap->getBits());
  imgMaskData.imgStr->reset();
  imgMaskData.colorMap = maskColorMap;
  imgMaskData.maskColors = NULL;
  imgMaskData.colorMode = splashModeMono8;
  imgMaskData.width = maskWidth;
  imgMaskData.height = maskHeight;
  imgMaskData.y = 0;
  n = 1 << maskColorMap->getBits();
  imgMaskData.lookup = (SplashColorPtr)gmallocn(n,1);
  for (i = 0; i < n; ++i) {
    pix = (Guchar)i;
    maskColorMap->getGray(&pix, &gray);
    imgMaskData.lookup[i] = colToByte(gray);
  }
  maskBitmap = new SplashBitmap(maskWidth,maskHeight,
				1, splashModeMono8, gFalse);
  maskSplash = new Splash(maskBitmap, gFalse);
  maskColor[0] = 0;
  maskSplash->clear(maskColor);
#if POPPLER_VERSION_MAJOR <= 0 && (POPPLER_VERSION_MINOR <= 20 || (POPPLER_VERSION_MINOR == 21 && POPPLER_VERSION_MICRO <= 2))
  maskSplash->drawImage(&imageSrc, &imgMaskData,
			splashModeMono8, gFalse, maskWidth, maskHeight, mat);
#elif POPPLER_VERSION_MAJOR <= 0 && POPPLER_VERSION_MINOR <= 33
  maskSplash->drawImage(&imageSrc, &imgMaskData,
			splashModeMono8, gFalse, maskWidth, maskHeight,
                        mat,gFalse);
#else
  maskSplash->drawImage(&imageSrc, 0, &imgMaskData,
                          splashModeMono8, gFalse, maskWidth, maskHeight,
			                          mat,gFalse);
#endif
  delete imgMaskData.imgStr;
  maskStr->close();
  gfree(imgMaskData.lookup);
  delete maskSplash;
  oprs->setSoftMask(maskBitmap);

  //----- draw the source image

  imgData.imgStr = new ImageStream(str, width,
				   colorMap->getNumPixelComps(),
				   colorMap->getBits());
  imgData.imgStr->reset();
  imgData.colorMap = colorMap;
  imgData.maskColors = NULL;
  imgData.colorMode = colorMode;
  imgData.width = width;
  imgData.height = height;
  imgData.y = 0;

  // special case for one-channel (monochrome/gray/separation) images:
  // build a lookup table here
  imgData.lookup = NULL;
  if (colorMap->getNumPixelComps() == 1) {
    n = 1 << colorMap->getBits();
    switch (colorMode) {
    case splashModeMono1:
    case splashModeMono8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,1);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getGray(&pix, &gray);
	imgData.lookup[i] = colToByte(gray);
      }
      break;
    case splashModeRGB8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,3);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getRGB(&pix, &rgb);
	imgData.lookup[3*i] = colToByte(rgb.r);
	imgData.lookup[3*i+1] = colToByte(rgb.g);
	imgData.lookup[3*i+2] = colToByte(rgb.b);
      }
      break;
    case splashModeBGR8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,3);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getRGB(&pix, &rgb);
	imgData.lookup[3*i] = colToByte(rgb.b);
	imgData.lookup[3*i+1] = colToByte(rgb.g);
	imgData.lookup[3*i+2] = colToByte(rgb.r);
      }
      break;
#if SPLASH_CMYK
    case splashModeCMYK8:
      imgData.lookup = (SplashColorPtr)gmallocn(n,4);
      for (i = 0; i < n; ++i) {
	pix = (Guchar)i;
	colorMap->getCMYK(&pix, &cmyk);
	imgData.lookup[4*i] = colToByte(cmyk.c);
	imgData.lookup[4*i+1] = colToByte(cmyk.m);
	imgData.lookup[4*i+2] = colToByte(cmyk.y);
	imgData.lookup[4*i+3] = colToByte(cmyk.k);
      }
      break;
#endif
    default:
      //~ unimplemented
      break;
    }
  }

  switch (colorMode) {
  case splashModeMono1:
  case splashModeMono8:
    srcMode = splashModeMono8;
    break;
  case splashModeRGB8:
    srcMode = splashModeRGB8;
    break;
  case splashModeBGR8:
    srcMode = splashModeBGR8;
    break;
#if SPLASH_CMYK
  case splashModeCMYK8:
    srcMode = splashModeCMYK8;
    break;
#endif
  default:
    //~ unimplemented
    srcMode = splashModeRGB8;
    break;
  }  
  oprs->drawImage(&imageSrc, &imgData, srcMode, gFalse, width, height, mat);

  oprs->setSoftMask(NULL);
  gfree(imgData.lookup);
  delete imgData.imgStr;
  str->close();
}

int OPVPOutputDev::getBitmapWidth() {
  return bitmap->getWidth();
}

int OPVPOutputDev::getBitmapHeight() {
  return bitmap->getHeight();
}

void OPVPOutputDev::xorRectangle(int x0, int y0, int x1, int y1,
				   SplashPattern *pattern) {
    /* no need in printing */
}

void OPVPOutputDev::setFillColor(int r, int g, int b) {
  GfxRGB rgb;
  GfxGray gray;
#if SPLASH_CMYK
  GfxCMYK cmyk;
#endif

  rgb.r = byteToCol(r);
  rgb.g = byteToCol(g);
  rgb.b = byteToCol(b);
  gray = (GfxColorComp)(0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.g + 0.5);
  if (gray > gfxColorComp1) {
    gray = gfxColorComp1;
  }
#if SPLASH_CMYK
  cmyk.c = gfxColorComp1 - rgb.r;
  cmyk.m = gfxColorComp1 - rgb.g;
  cmyk.y = gfxColorComp1 - rgb.b;
  cmyk.k = 0;
  oprs->setFillPattern(getColor(gray, &rgb, &cmyk));
#else
  oprs->setFillPattern(getColor(gray, &rgb));
#endif
}

int OPVPOutputDev::OPVPStartJob(char *jobInfo)
{
    return oprs->OPVPStartJob(jobInfo);
}

int OPVPOutputDev::OPVPEndJob()
{
    return oprs->OPVPEndJob();
}

int OPVPOutputDev::OPVPStartDoc(char *docInfo)
{
    return oprs->OPVPStartDoc(docInfo);
}

int OPVPOutputDev::OPVPEndDoc()
{
    return oprs->OPVPEndDoc();
}

int OPVPOutputDev::OPVPStartPage(char *pageInfo,
  int rasterWidth, int rasterHeight)
{
    paperWidth = rasterWidth;
    paperHeight = rasterHeight;
    return oprs->OPVPStartPage(pageInfo,rasterWidth);
}

int OPVPOutputDev::OPVPEndPage()
{
    return oprs->OPVPEndPage();
}

int OPVPOutputDev::outSlice()
{
    return oprs->outSlice();
}

void OPVPOutputDev::psXObject(Stream *psStream, Stream *level1Stream)
{
  opvpError(-1,"psXObject is found, but it is not supported");
}

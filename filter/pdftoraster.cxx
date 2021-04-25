/*
Copyright (c) 2008-2011 BBR Inc.  All rights reserved.
Copyright (c) 2012-2019 by Till Kamppeter
Copyright (c) 2019 by Tanmay Anand.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/
/*
 pdftoraster.cc
 pdf to raster filter
*/

#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

#define USE_CMS

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include <poppler/cpp/poppler-version.h>
#endif
#include <ppd/ppd.h>
#include <stdarg.h>
#include <cups/raster.h>
#include <cupsfilters/image.h>
#include <cupsfilters/raster.h>
#include <cupsfilters/colormanager.h>
#include <cupsfilters/bitmap.h>
#include <strings.h>
#include <math.h>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-global.h>
#include <poppler/cpp/poppler-image.h>
#include <poppler/cpp/poppler-page-renderer.h>
#include <poppler/cpp/poppler-rectangle.h>
#ifdef USE_LCMS1
#include <lcms.h>
#define cmsColorSpaceSignature icColorSpaceSignature
#define cmsSetLogErrorHandler cmsSetErrorHandler
#define cmsToneCurve LPGAMMATABLE
#define cmsSigXYZData icSigXYZData
#define cmsSigLuvData icSigLuvData
#define cmsSigLabData icSigLabData
#define cmsSigYCbCrData icSigYCbCrData
#define cmsSigYxyData icSigYxyData
#define cmsSigRgbData icSigRgbData
#define cmsSigHsvData icSigHsvData
#define cmsSigHlsData icSigHlsData
#define cmsSigCmyData icSigCmyData
#define cmsSig3colorData icSig3colorData
#define cmsSigGrayData icSigGrayData
#define cmsSigCmykData icSigCmykData
#define cmsSig4colorData icSig4colorData
#define cmsSig2colorData icSig2colorData
#define cmsSig5colorData icSig5colorData
#define cmsSig6colorData icSig6colorData
#define cmsSig7colorData icSig7colorData
#define cmsSig8colorData icSig8colorData
#define cmsSig9colorData icSig9colorData
#define cmsSig10colorData icSig10colorData
#define cmsSig11colorData icSig11colorData
#define cmsSig12colorData icSig12colorData
#define cmsSig13colorData icSig13colorData
#define cmsSig14colorData icSig14colorData
#define cmsSig15colorData icSig15colorData
#else
#include <lcms2.h>
#endif

#define MAX_CHECK_COMMENT_LINES	20
#define MAX_BYTES_PER_PIXEL 32

namespace {
  typedef unsigned char *(*ConvertLineFunc)(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size);
  typedef unsigned char *(*ConvertCSpaceFunc)(unsigned char *src,
    unsigned char *pixelBuf, unsigned int x, unsigned int y);

  int exitCode = 0;
  int pwgraster = 0;
  int bi_level = 0;
  int deviceCopies = 1;
  bool deviceCollate = false;
  cups_page_header2_t header;
  ppd_file_t *ppd = 0;
  unsigned int bitmapoffset[2];
  unsigned int popplerBitsPerPixel;
  unsigned int popplerNumColors;
  unsigned int bitspercolor;
  /* image swapping */
  bool swap_image_x = false;
  bool swap_image_y = false;
  /* margin swapping */
  bool swap_margin_x = false;
  bool swap_margin_y = false;
  bool allocLineBuf = false;
  ConvertLineFunc convertLineOdd;
  ConvertLineFunc convertLineEven;
  ConvertCSpaceFunc convertCSpace;
  unsigned int nplanes;
  unsigned int nbands;
  unsigned int bytesPerLine; /* number of bytes per line */
                        /* Note: When CUPS_ORDER_BANDED,
                           cupsBytesPerLine = bytesPerLine*cupsNumColors */
  /* for color profiles */
  cmsHPROFILE colorProfile = NULL;
  cmsHPROFILE popplerColorProfile = NULL;
  cmsHTRANSFORM colorTransform = NULL;
  cmsCIEXYZ D65WhitePoint;
  int renderingIntent = INTENT_PERCEPTUAL;
  int cm_disabled = 0;
  cm_calibration_t cm_calibrate;
}

cmsCIExyY adobergb_wp()
{
    double * xyY = cmWhitePointAdobeRgb();
    cmsCIExyY wp;

    wp.x = xyY[0];
    wp.y = xyY[1];
    wp.Y = xyY[2];

    return wp;
}

cmsCIExyY sgray_wp()
{
    double * xyY = cmWhitePointSGray();
    cmsCIExyY wp;

    wp.x = xyY[0];
    wp.y = xyY[1];
    wp.Y = xyY[2];

    return wp;
}

cmsCIExyYTRIPLE adobergb_matrix()
{
    cmsCIExyYTRIPLE m;

    double * matrix = cmMatrixAdobeRgb();

    m.Red.x = matrix[0];
    m.Red.y = matrix[1];
    m.Red.Y = matrix[2];
    m.Green.x = matrix[3];
    m.Green.y = matrix[4];
    m.Green.Y = matrix[5];
    m.Blue.x = matrix[6];
    m.Blue.y = matrix[7];
    m.Blue.Y = matrix[8];

    return m;
}

cmsHPROFILE adobergb_profile()
{
    cmsHPROFILE adobergb;

    cmsCIExyY wp;
    cmsCIExyYTRIPLE primaries;

#if USE_LCMS1
    cmsToneCurve Gamma = cmsBuildGamma(256, 2.2);
    cmsToneCurve Gamma3[3];
#else
    cmsToneCurve * Gamma = cmsBuildGamma(NULL, 2.2);
    cmsToneCurve * Gamma3[3];
#endif
    Gamma3[0] = Gamma3[1] = Gamma3[2] = Gamma;

    // Build AdobeRGB profile
    primaries = adobergb_matrix();
    wp = adobergb_wp();
    adobergb = cmsCreateRGBProfile(&wp, &primaries, Gamma3);

    return adobergb;
}

cmsHPROFILE sgray_profile()
{
    cmsHPROFILE sgray;

    cmsCIExyY wp;

#if USE_LCMS1
    cmsToneCurve Gamma = cmsBuildGamma(256, 2.2);
#else
    cmsToneCurve * Gamma = cmsBuildGamma(NULL, 2.2);
#endif
    // Build sGray profile
    wp = sgray_wp();
    sgray = cmsCreateGrayProfile(&wp, Gamma);

    return sgray;
}


#ifdef USE_LCMS1
static int lcmsErrorHandler(int ErrorCode, const char *ErrorText)
{
  fprintf(stderr, "ERROR: %s\n",ErrorText);
  return 1;
}
#else
static void lcmsErrorHandler(cmsContext contextId, cmsUInt32Number ErrorCode,
   const char *ErrorText)
{
  fprintf(stderr, "ERROR: %s\n",ErrorText);
}
#endif



static void  handleRqeuiresPageRegion() {
  ppd_choice_t *mf;
  ppd_choice_t *is;
  ppd_attr_t *rregions = NULL;
  ppd_size_t *size;

  if ((size = ppdPageSize(ppd,NULL)) == NULL) return;
  mf = ppdFindMarkedChoice(ppd,"ManualFeed");
  if ((is = ppdFindMarkedChoice(ppd,"InputSlot")) != NULL) {
    rregions = ppdFindAttr(ppd,"RequiresPageRegion",is->choice);
  }
  if (rregions == NULL) {
    rregions = ppdFindAttr(ppd,"RequiresPageRegion","All");
  }
  if (!strcasecmp(size->name,"Custom") || (!mf && !is) ||
      (mf && !strcasecmp(mf->choice,"False") &&
       (!is || (is->code && !is->code[0]))) ||
      (!rregions && ppd->num_filters > 0)) {
    ppdMarkOption(ppd,"PageSize",size->name);
  } else if (rregions && rregions->value
      && !strcasecmp(rregions->value,"True")) {
    ppdMarkOption(ppd,"PageRegion",size->name);
  } else {
    ppd_choice_t *page;

    if ((page = ppdFindMarkedChoice(ppd,"PageSize")) != NULL) {
      page->marked = 0;
      cupsArrayRemove(ppd->marked,page);
    }
    if ((page = ppdFindMarkedChoice(ppd,"PageRegion")) != NULL) {
      page->marked = 0;
      cupsArrayRemove(ppd->marked, page);
    }
  }
}

static void parseOpts(int argc, char **argv)
{
  int num_options = 0;
  cups_option_t *options = 0;
  char *profile = 0;
  const char *t = NULL;
  ppd_attr_t *attr;
  const char *val;

  if (argc < 6 || argc > 7) {
    fprintf(stderr, "ERROR: Usage: %s job-id user title copies options [file]\n",
	    argv[0]);
    exit(1);
  }

#ifdef HAVE_CUPS_1_7
  t = getenv("FINAL_CONTENT_TYPE");
  if (t && strcasestr(t, "pwg"))
    pwgraster = 1;
#endif /* HAVE_CUPS_1_7 */

  ppd = ppdOpenFile(getenv("PPD"));
  if (ppd == NULL)
    fprintf(stderr, "DEBUG: PPD file is not specified.\n");
  if (ppd)
    ppdMarkDefaults(ppd);
  options = NULL;
  num_options = cupsParseOptions(argv[5],0,&options);
  if (ppd) {
    ppdMarkOptions(ppd,num_options,options);
    handleRqeuiresPageRegion();
    ppdRasterInterpretPPD(&header,ppd,num_options,options,0);
    attr = ppdFindAttr(ppd,"pdftorasterRenderingIntent",NULL);
    if (attr != NULL && attr->value != NULL) {
      if (strcasecmp(attr->value,"PERCEPTUAL") != 0) {
	renderingIntent = INTENT_PERCEPTUAL;
      } else if (strcasecmp(attr->value,"RELATIVE_COLORIMETRIC") != 0) {
	renderingIntent = INTENT_RELATIVE_COLORIMETRIC;
      } else if (strcasecmp(attr->value,"SATURATION") != 0) {
	renderingIntent = INTENT_SATURATION;
      } else if (strcasecmp(attr->value,"ABSOLUTE_COLORIMETRIC") != 0) {
	renderingIntent = INTENT_ABSOLUTE_COLORIMETRIC;
      }
    }
    if (header.Duplex) {
      /* analyze options relevant to Duplex */
      const char *backside = "";
      /* APDuplexRequiresFlippedMargin */
      enum {
	FM_NO, FM_FALSE, FM_TRUE
      } flippedMargin = FM_NO;

      attr = ppdFindAttr(ppd,"cupsBackSide",NULL);
      if (attr != NULL && attr->value != NULL) {
	ppd->flip_duplex = 0;
	backside = attr->value;
      } else if (ppd->flip_duplex) {
	backside = "Rotated"; /* compatible with Max OS and GS 8.71 */
      }

      attr = ppdFindAttr(ppd,"APDuplexRequiresFlippedMargin",NULL);
      if (attr != NULL && attr->value != NULL) {
	if (strcasecmp(attr->value,"true") == 0) {
	  flippedMargin = FM_TRUE;
	} else {
	  flippedMargin = FM_FALSE;
	}
      }
      if (strcasecmp(backside,"ManualTumble") == 0 && header.Tumble) {
	swap_image_x = swap_image_y = true;
	swap_margin_x = swap_margin_y = true;
	if (flippedMargin == FM_TRUE) {
	  swap_margin_y = false;
	}
      } else if (strcasecmp(backside,"Rotated") == 0 && !header.Tumble) {
	swap_image_x = swap_image_y = true;
	swap_margin_x = swap_margin_y = true;
	if (flippedMargin == FM_TRUE) {
	  swap_margin_y = false;
	}
      } else if (strcasecmp(backside,"Flipped") == 0) {
	if (header.Tumble) {
	  swap_image_x = true;
	  swap_margin_x = swap_margin_y = true;
	} else {
	  swap_image_y = true;
	}
	if (flippedMargin == FM_FALSE) {
	  swap_margin_y = !swap_margin_y;
	}
      }
    }

    /* support the CUPS "cm-calibration" option */
    cm_calibrate = cmGetCupsColorCalibrateMode(options, num_options);

    if (cm_calibrate == CM_CALIBRATION_ENABLED)
      cm_disabled = 1;
    else
      cm_disabled = cmIsPrinterCmDisabled(getenv("PRINTER"));

    if (!cm_disabled)
      cmGetPrinterIccProfile(getenv("PRINTER"), &profile, ppd);

    if (profile != NULL) {
      colorProfile = cmsOpenProfileFromFile(profile,"r");
      free(profile);
    }

#ifdef HAVE_CUPS_1_7
    if ((attr = ppdFindAttr(ppd,"PWGRaster",0)) != 0 &&
	(!strcasecmp(attr->value, "true")
	 || !strcasecmp(attr->value, "on") ||
	 !strcasecmp(attr->value, "yes")))
      pwgraster = 1;
    if (pwgraster == 1)
      cupsRasterParseIPPOptions(&header, num_options, options, pwgraster, 0);
#endif /* HAVE_CUPS_1_7 */
  } else {
#ifdef HAVE_CUPS_1_7
    pwgraster = 1;
    t = cupsGetOption("media-class", num_options, options);
    if (t == NULL)
      t = cupsGetOption("MediaClass", num_options, options);
    if (t != NULL)
    {
      if (strcasestr(t, "pwg"))
	pwgraster = 1;
      else
	pwgraster = 0;
    }
    cupsRasterParseIPPOptions(&header,num_options,options,pwgraster,1);
#else
    fprintf(stderr, "ERROR: No PPD file specified.\n");
    exit(1);
#endif /* HAVE_CUPS_1_7 */
  }
  if ((val = cupsGetOption("print-color-mode", num_options, options)) != NULL
                           && !strncasecmp(val, "bi-level", 8))
    bi_level = 1;

  fprintf(stderr, "DEBUG: Page size requested: %s\n",
	  header.cupsPageSizeName);
}

static void parsePDFTOPDFComment(FILE *fp)
{
  char buf[4096];
  int i;

  /* skip until PDF start header */
  while (fgets(buf,sizeof(buf),fp) != 0) {
    if (strncmp(buf,"%PDF",4) == 0) {
      break;
    }
  }
  for (i = 0;i < MAX_CHECK_COMMENT_LINES;i++) {
    if (fgets(buf,sizeof(buf),fp) == 0) break;
    if (strncmp(buf,"%%PDFTOPDFNumCopies",19) == 0) {
      char *p;

      p = strchr(buf+19,':');
      deviceCopies = atoi(p+1);
    } else if (strncmp(buf,"%%PDFTOPDFCollate",17) == 0) {
      char *p;

      p = strchr(buf+17,':');
      while (*p == ' ' || *p == '\t') p++;
      if (strncasecmp(p,"true",4) == 0) {
	deviceCollate = true;
      } else {
	deviceCollate = false;
      }
    }
  }
}

static unsigned char *reverseLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *p = src;

  for (unsigned int j = 0;j < size;j++,p++) {
    *p = ~*p;
  }
  return src;
}

static unsigned char *reverseLineSwapByte(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size)
{
  unsigned char *bp = src+size-1;
  unsigned char *dp = dst;

  for (unsigned int j = 0;j < size;j++,bp--,dp++) {
    *dp = ~*bp;
  }
  return dst;
}


static unsigned char *reverseLineSwapBit(unsigned char *src,
  unsigned char *dst, unsigned int row, unsigned int plane,
  unsigned int pixels, unsigned int size)
{
  dst = reverseOneBitLineSwap(src, dst, pixels, size);
  return dst;
}

static unsigned char *rgbToCMYKLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  cupsImageRGBToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *rgbToCMYKLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *bp = src+(pixels-1)*3;
  unsigned char *dp = dst;

  for (unsigned int i = 0;i < pixels;i++, bp -= 3, dp += 4) {
    cupsImageRGBToCMYK(bp,dp,1);
  }
  return dst;
}

static unsigned char *rgbToCMYLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *rgbToCMYLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *bp = src+size-3;
  unsigned char *dp = dst;

  for (unsigned int i = 0;i < pixels;i++, bp -= 3, dp += 3) {
    cupsImageRGBToCMY(bp,dp,1);
  }
  return dst;
}

static unsigned char *rgbToKCMYLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *bp = src;
  unsigned char *dp = dst;
  unsigned char d;

  cupsImageRGBToCMYK(src,dst,pixels);
  /* CMYK to KCMY */
  for (unsigned int i = 0;i < pixels;i++, bp += 3, dp += 4) {
    d = dp[3];
    dp[3] = dp[2];
    dp[2] = dp[1];
    dp[1] = dp[0];
    dp[0] = d;
  }
  return dst;
}

static unsigned char *rgbToKCMYLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *bp = src+(pixels-1)*3;
  unsigned char *dp = dst;
  unsigned char d;

  for (unsigned int i = 0;i < pixels;i++, bp -= 3, dp += 4) {
    cupsImageRGBToCMYK(bp,dp,1);
    /* CMYK to KCMY */
    d = dp[3];
    dp[3] = dp[2];
    dp[2] = dp[1];
    dp[1] = dp[0];
    dp[0] = d;
  }
  return dst;
}

static unsigned char *lineNoop(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  /* do nothing */
  return src;
}

static unsigned char *lineSwap24(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *bp = src+size-3;
  unsigned char *dp = dst;

  for (unsigned int i = 0;i < pixels;i++, bp -= 3, dp += 3) {
    dp[0] = bp[0];
    dp[1] = bp[1];
    dp[2] = bp[2];
  }
  return dst;
}

static unsigned char *lineSwapByte(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  unsigned char *bp = src+size-1;
  unsigned char *dp = dst;

  for (unsigned int j = 0;j < size;j++,bp--,dp++) {
    *dp = *bp;
  }
  return dst;
}

static unsigned char *lineSwapBit(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  dst = reverseOneBitLine(src, dst, pixels, size);
  return dst;
}

typedef struct _funcTable {
  enum cups_cspace_e cspace;
  unsigned int bitsPerPixel;
  unsigned int bitsPerColor;
  ConvertLineFunc convertLine;
  bool allocLineBuf;
  ConvertLineFunc convertLineSwap;
  bool allocLineBufSwap;
} FuncTable;

static FuncTable specialCaseFuncs[] = {
  {CUPS_CSPACE_K,8,8,reverseLine,false,reverseLineSwapByte,true},
  {CUPS_CSPACE_K,1,1,reverseLine,false,reverseLineSwapBit,true},
  {CUPS_CSPACE_GOLD,8,8,reverseLine,false,reverseLineSwapByte,true},
  {CUPS_CSPACE_GOLD,1,1,reverseLine,false,reverseLineSwapBit,true},
  {CUPS_CSPACE_SILVER,8,8,reverseLine,false,reverseLineSwapByte,true},
  {CUPS_CSPACE_SILVER,1,1,reverseLine,false,reverseLineSwapBit,true},
  {CUPS_CSPACE_CMYK,32,8,rgbToCMYKLine,true,rgbToCMYKLineSwap,true},
  {CUPS_CSPACE_KCMY,32,8,rgbToKCMYLine,true,rgbToKCMYLineSwap,true},
  {CUPS_CSPACE_CMY,24,8,rgbToCMYLine,true,rgbToCMYLineSwap,true},
  {CUPS_CSPACE_RGB,24,8,lineNoop,false,lineSwap24,true},
  {CUPS_CSPACE_SRGB,24,8,lineNoop,false,lineSwap24,true},
  {CUPS_CSPACE_ADOBERGB,24,8,lineNoop,false,lineSwap24,true},
  {CUPS_CSPACE_W,8,8,lineNoop,false,lineSwapByte,true},
  {CUPS_CSPACE_W,1,1,lineNoop,false,lineSwapBit,true},
  {CUPS_CSPACE_SW,8,8,lineNoop,false,lineSwapByte,true},
  {CUPS_CSPACE_SW,1,1,lineNoop,false,lineSwapBit,true},
  {CUPS_CSPACE_WHITE,8,8,lineNoop,false,lineSwapByte,true},
  {CUPS_CSPACE_WHITE,1,1,lineNoop,false,lineSwapBit,true},
  {CUPS_CSPACE_RGB,0,0,NULL,false,NULL,false} /* end mark */
};

static unsigned char *convertCSpaceNone(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y)
{
  return src;
}

static unsigned char *convertCSpaceWithProfiles(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y)
{
  cmsDoTransform(colorTransform,src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *convertCSpaceXYZ8(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y)
{
  double alab[3];

  cmsDoTransform(colorTransform,src,alab,1);
  cmsCIELab lab;
  cmsCIEXYZ xyz;

  lab.L = alab[0];
  lab.a = alab[1];
  lab.b = alab[2];

  cmsLab2XYZ(&D65WhitePoint,&xyz,&lab);
  pixelBuf[0] = 231.8181*xyz.X+0.5;
  pixelBuf[1] = 231.8181*xyz.Y+0.5;
  pixelBuf[2] = 231.8181*xyz.Z+0.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceXYZ16(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y)
{
  double alab[3];
  unsigned short *sd = (unsigned short *)pixelBuf;

  cmsDoTransform(colorTransform,src,alab,1);
  cmsCIELab lab;
  cmsCIEXYZ xyz;

  lab.L = alab[0];
  lab.a = alab[1];
  lab.b = alab[2];

  cmsLab2XYZ(&D65WhitePoint,&xyz,&lab);
  sd[0] = 59577.2727*xyz.X+0.5;
  sd[1] = 59577.2727*xyz.Y+0.5;
  sd[2] = 59577.2727*xyz.Z+0.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceLab8(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y)
{
  double lab[3];
  cmsDoTransform(colorTransform,src,lab,1);
  pixelBuf[0] = 2.55*lab[0]+0.5;
  pixelBuf[1] = lab[1]+128.5;
  pixelBuf[2] = lab[2]+128.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceLab16(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y)
{
  double lab[3];
  cmsDoTransform(colorTransform,src,lab,1);
  unsigned short *sd = (unsigned short *)pixelBuf;
  sd[0] = 655.35*lab[0]+0.5;
  sd[1] = 256*(lab[1]+128)+0.5;
  sd[2] = 256*(lab[2]+128)+0.5;
  return pixelBuf;
}

static unsigned char *RGB8toRGBA(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  unsigned char *dp = pixelBuf;

  for (int i = 0;i < 3;i++) {
    *dp++ = *src++;
  }
  *dp = 255;
  return pixelBuf;
}

static unsigned char *RGB8toRGBW(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  unsigned char cmyk[4];
  unsigned char *dp = pixelBuf;

  cupsImageRGBToCMYK(src,cmyk,1);
  for (int i = 0;i < 4;i++) {
    *dp++ = ~cmyk[i];
  }
  return pixelBuf;
}

static unsigned char *RGB8toCMYK(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *RGB8toCMY(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  cupsImageRGBToCMY(src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *RGB8toYMC(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  cupsImageRGBToCMY(src,pixelBuf,1);
  /* swap C and Y */
  unsigned char d = pixelBuf[0];
  pixelBuf[0] = pixelBuf[2];
  pixelBuf[2] = d;
  return pixelBuf;
}

static unsigned char *RGB8toKCMY(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  unsigned char d = pixelBuf[3];
  pixelBuf[3] = pixelBuf[2];
  pixelBuf[2] = pixelBuf[1];
  pixelBuf[1] = pixelBuf[0];
  pixelBuf[0] = d;
  return pixelBuf;
}

static unsigned char *RGB8toYMCK(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  /* swap C and Y */
  unsigned char d = pixelBuf[0];
  pixelBuf[0] = pixelBuf[2];
  pixelBuf[2] = d;
  return pixelBuf;
}

static unsigned char *W8toK8(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  *pixelBuf = ~(*src);
  return pixelBuf;
}

static unsigned char *convertLineChunked(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+i*popplerNumColors,pixelBuf1,i,row);
      pb = convertbits(pb,pixelBuf2,i,row, header.cupsNumColors, bitspercolor);
      writepixel(dst,0,i,pb, header.cupsNumColors, header.cupsBitsPerColor, header.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLineChunkedSwap(unsigned char *src,
     unsigned char *dst, unsigned int row, unsigned int plane,
     unsigned int pixels, unsigned int size)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+(pixels-i-1)*popplerNumColors,pixelBuf1,i,row);
      pb = convertbits(pb,pixelBuf2,i,row, header.cupsNumColors, bitspercolor);
      writepixel(dst,0,i,pb, header.cupsNumColors, header.cupsBitsPerColor, header.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLinePlane(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+i*popplerNumColors,pixelBuf1,i,row);
      pb = convertbits(pb,pixelBuf2,i,row, header.cupsNumColors, bitspercolor);
      writepixel(dst,plane,i,pb, header.cupsNumColors, header.cupsBitsPerColor, header.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLinePlaneSwap(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size)
{
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+(pixels-i-1)*popplerNumColors,pixelBuf1,i,row);
      pb = convertbits(pb,pixelBuf2,i,row, header.cupsNumColors, bitspercolor);
      writepixel(dst,plane,i,pb, header.cupsNumColors, header.cupsBitsPerColor, header.cupsColorOrder);
  }
  return dst;
}

/* handle special cases which are appear in gutenprint's PPDs. */
static bool selectSpecialCase()
{
  int i;

  for (i = 0;specialCaseFuncs[i].bitsPerPixel > 0;i++) {
    if (header.cupsColorSpace == specialCaseFuncs[i].cspace
       && header.cupsBitsPerPixel == specialCaseFuncs[i].bitsPerPixel
       && header.cupsBitsPerColor == specialCaseFuncs[i].bitsPerColor) {
      convertLineOdd = specialCaseFuncs[i].convertLine;
      if (header.Duplex && swap_image_x) {
        convertLineEven = specialCaseFuncs[i].convertLineSwap;
        allocLineBuf = specialCaseFuncs[i].allocLineBufSwap;
      } else {
        convertLineEven = specialCaseFuncs[i].convertLine;
        allocLineBuf = specialCaseFuncs[i].allocLineBuf;
      }
      return true; /* found */
    }
  }
  return false;
}

static unsigned int getCMSColorSpaceType(cmsColorSpaceSignature cs)
{
    switch (cs) {
    case cmsSigXYZData:
      return PT_XYZ;
      break;
    case cmsSigLabData:
      return PT_Lab;
      break;
    case cmsSigLuvData:
      return PT_YUV;
      break;
    case cmsSigYCbCrData:
      return PT_YCbCr;
      break;
    case cmsSigYxyData:
      return PT_Yxy;
      break;
    case cmsSigRgbData:
      return PT_RGB;
      break;
    case cmsSigGrayData:
      return PT_GRAY;
      break;
    case cmsSigHsvData:
      return PT_HSV;
      break;
    case cmsSigHlsData:
      return PT_HLS;
      break;
    case cmsSigCmykData:
      return PT_CMYK;
      break;
    case cmsSigCmyData:
      return PT_CMY;
      break;
    case cmsSig2colorData:
    case cmsSig3colorData:
    case cmsSig4colorData:
    case cmsSig5colorData:
    case cmsSig6colorData:
    case cmsSig7colorData:
    case cmsSig8colorData:
    case cmsSig9colorData:
    case cmsSig10colorData:
    case cmsSig11colorData:
    case cmsSig12colorData:
    case cmsSig13colorData:
    case cmsSig14colorData:
    case cmsSig15colorData:
    default:
      break;
    }
    return PT_RGB;
}

/* select convertLine function */
static void selectConvertFunc(cups_raster_t *raster)
{
  if ((colorProfile == NULL || popplerColorProfile == colorProfile)
      && (header.cupsColorOrder == CUPS_ORDER_CHUNKED
       || header.cupsNumColors == 1)) {
    if (selectSpecialCase()) return;
  }

  switch (header.cupsColorOrder) {
  case CUPS_ORDER_BANDED:
  case CUPS_ORDER_PLANAR:
    if (header.cupsNumColors > 1) {
      convertLineEven = convertLinePlaneSwap;
      convertLineOdd = convertLinePlane;
      break;
    }
  default:
  case CUPS_ORDER_CHUNKED:
    convertLineEven = convertLineChunkedSwap;
    convertLineOdd = convertLineChunked;
    break;
  }
  if (!header.Duplex || !swap_image_x) {
    convertLineEven = convertLineOdd;
  }
  allocLineBuf = true;

  if (colorProfile != NULL && popplerColorProfile != colorProfile) {
    unsigned int bytes;

    switch (header.cupsColorSpace) {
    case CUPS_CSPACE_CIELab:
    case CUPS_CSPACE_ICC1:
    case CUPS_CSPACE_ICC2:
    case CUPS_CSPACE_ICC3:
    case CUPS_CSPACE_ICC4:
    case CUPS_CSPACE_ICC5:
    case CUPS_CSPACE_ICC6:
    case CUPS_CSPACE_ICC7:
    case CUPS_CSPACE_ICC8:
    case CUPS_CSPACE_ICC9:
    case CUPS_CSPACE_ICCA:
    case CUPS_CSPACE_ICCB:
    case CUPS_CSPACE_ICCC:
    case CUPS_CSPACE_ICCD:
    case CUPS_CSPACE_ICCE:
    case CUPS_CSPACE_ICCF:
      if (header.cupsBitsPerColor == 8) {
        convertCSpace = convertCSpaceLab8;
      } else {
        /* 16 bits */
        convertCSpace = convertCSpaceLab16;
      }
      bytes = 0; /* double */
      break;
    case CUPS_CSPACE_CIEXYZ:
      if (header.cupsBitsPerColor == 8) {
        convertCSpace = convertCSpaceXYZ8;
      } else {
        /* 16 bits */
        convertCSpace = convertCSpaceXYZ16;
      }
      bytes = 0; /* double */
      break;
    default:
      convertCSpace = convertCSpaceWithProfiles;
      bytes = header.cupsBitsPerColor/8;
      break;
    }
    bitspercolor = 0; /* convert bits in convertCSpace */
    if (popplerColorProfile == NULL) {
      popplerColorProfile = cmsCreate_sRGBProfile();
    }
    unsigned int dcst = getCMSColorSpaceType(cmsGetColorSpace(colorProfile));
    if ((colorTransform = cmsCreateTransform(popplerColorProfile,
            COLORSPACE_SH(PT_RGB) |CHANNELS_SH(3) | BYTES_SH(1),
            colorProfile,
            COLORSPACE_SH(dcst) |
            CHANNELS_SH(header.cupsNumColors) | BYTES_SH(bytes),
            renderingIntent,0)) == 0) {
      fprintf(stderr, "ERROR: Can't create color transform");
      exit(1);
    }
  } else {
    /* select convertCSpace function */
    switch (header.cupsColorSpace) {
    case CUPS_CSPACE_CIELab:
    case CUPS_CSPACE_ICC1:
    case CUPS_CSPACE_ICC2:
    case CUPS_CSPACE_ICC3:
    case CUPS_CSPACE_ICC4:
    case CUPS_CSPACE_ICC5:
    case CUPS_CSPACE_ICC6:
    case CUPS_CSPACE_ICC7:
    case CUPS_CSPACE_ICC8:
    case CUPS_CSPACE_ICC9:
    case CUPS_CSPACE_ICCA:
    case CUPS_CSPACE_ICCB:
    case CUPS_CSPACE_ICCC:
    case CUPS_CSPACE_ICCD:
    case CUPS_CSPACE_ICCE:
    case CUPS_CSPACE_ICCF:
    case CUPS_CSPACE_CIEXYZ:
      convertCSpace = convertCSpaceNone;
      break;
    case CUPS_CSPACE_CMY:
      convertCSpace = RGB8toCMY;
      break;
    case CUPS_CSPACE_YMC:
      convertCSpace = RGB8toYMC;
      break;
    case CUPS_CSPACE_CMYK:
      convertCSpace = RGB8toCMYK;
      break;
    case CUPS_CSPACE_KCMY:
      convertCSpace = RGB8toKCMY;
      break;
    case CUPS_CSPACE_KCMYcm:
      if (header.cupsBitsPerColor > 1) {
        convertCSpace = RGB8toKCMY;
      } else {
        convertCSpace = RGB8toKCMYcm;
      }
      break;
    case CUPS_CSPACE_GMCS:
    case CUPS_CSPACE_GMCK:
    case CUPS_CSPACE_YMCK:
      convertCSpace = RGB8toYMCK;
      break;
    case CUPS_CSPACE_RGBW:
      convertCSpace = RGB8toRGBW;
      break;
    case CUPS_CSPACE_RGBA:
      convertCSpace = RGB8toRGBA;
      break;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_SRGB:
    case CUPS_CSPACE_ADOBERGB:
      convertCSpace = convertCSpaceNone;
      break;
    case CUPS_CSPACE_W:
    case CUPS_CSPACE_SW:
    case CUPS_CSPACE_WHITE:
      convertCSpace = convertCSpaceNone;
      break;
    case CUPS_CSPACE_K:
    case CUPS_CSPACE_GOLD:
    case CUPS_CSPACE_SILVER:
      convertCSpace = W8toK8;
      break;
    default:
      fprintf(stderr, "ERROR: Specified ColorSpace is not supported\n" );
      exit(1);
      break;
    }
  }

  if (header.cupsBitsPerColor == 1 &&
     (header.cupsNumColors == 1 ||
     header.cupsColorSpace == CUPS_CSPACE_KCMYcm ))
    bitspercolor = 0; /*Do not convertbits*/
  else
    bitspercolor = header.cupsBitsPerColor;

}

static unsigned char *onebitpixel(unsigned char *src, unsigned char *dst, unsigned int width, unsigned int height){
  unsigned char *temp;
  temp=dst;
  for(unsigned int i=0;i<height;i++){
    oneBitLine(src + bytesPerLine*8*i, dst + bytesPerLine*i, header.cupsWidth, i, bi_level);
  }
  return temp;
}


static unsigned char *removeAlpha(unsigned char *src, unsigned char *dst, unsigned int width, unsigned int height){
  unsigned char *temp;
  temp=dst;
  for(unsigned int i=0;i<height;i++){
    for(unsigned int j=0;j<width;j++){
      dst[0]=src[2];
      dst[1]=src[1];
      dst[2]=src[0];
      src+=4;
      dst+=3;
    }
  }
  return temp;
}

static void writePageImage(cups_raster_t *raster, poppler::document *doc,
  int pageNo)
{
  ConvertLineFunc convertLine;
  unsigned char *lineBuf = NULL;
  unsigned char *dp;
  unsigned int rowsize;

  poppler::page *current_page =doc->create_page(pageNo-1);
  poppler::page_renderer pr;
  pr.set_render_hint(poppler::page_renderer::antialiasing, true);
  pr.set_render_hint(poppler::page_renderer::text_antialiasing, true);

  unsigned char *colordata,*newdata,*graydata,*onebitdata;
  unsigned int pixel_count;
  poppler::image im;
  //render the page according to the colourspace and generate the requried data
  switch (header.cupsColorSpace) {
   case CUPS_CSPACE_W://gray
   case CUPS_CSPACE_K://black
   case CUPS_CSPACE_SW://sgray
    if(header.cupsBitsPerColor==1){ //special case for 1-bit colorspaces
      im = pr.render_page(current_page,header.HWResolution[0],header.HWResolution[1],bitmapoffset[0],bitmapoffset[1],bytesPerLine*8,header.cupsHeight);
    newdata = (unsigned char *)malloc(sizeof(char)*3*im.width()*im.height());
    newdata = removeAlpha((unsigned char *)im.const_data(),newdata,im.width(),im.height());
    graydata=(unsigned char *)malloc(sizeof(char)*im.width()*im.height());
    cupsImageRGBToWhite(newdata,graydata,im.width()*im.height());
    onebitdata=(unsigned char *)malloc(sizeof(char)*bytesPerLine*im.height());
    onebitpixel(graydata,onebitdata,im.width(),im.height());
    colordata=onebitdata;
    rowsize=bytesPerLine;
    }
    else{
      im = pr.render_page(current_page,header.HWResolution[0],header.HWResolution[1],bitmapoffset[0],bitmapoffset[1],header.cupsWidth,header.cupsHeight);
      newdata = (unsigned char *)malloc(sizeof(char)*3*im.width()*im.height());
      newdata = removeAlpha((unsigned char *)im.const_data(),newdata,im.width(),im.height());
      pixel_count=im.width()*im.height();
      graydata=(unsigned char *)malloc(sizeof(char)*im.width()*im.height());
      cupsImageRGBToWhite(newdata,graydata,pixel_count);
      colordata=graydata;
      rowsize=header.cupsWidth;
    }

    break;
   case CUPS_CSPACE_RGB:
   case CUPS_CSPACE_ADOBERGB:
   case CUPS_CSPACE_CMYK:
   case CUPS_CSPACE_SRGB:
   case CUPS_CSPACE_CMY:
   case CUPS_CSPACE_RGBW:
   default:
   im = pr.render_page(current_page,header.HWResolution[0],header.HWResolution[1],bitmapoffset[0],bitmapoffset[1],header.cupsWidth,header.cupsHeight);
   newdata = (unsigned char *)malloc(sizeof(char)*3*im.width()*im.height());
   newdata = removeAlpha((unsigned char *)im.const_data(),newdata,im.width(),im.height());
   pixel_count=im.width()*im.height();
   rowsize=header.cupsWidth*3;
   colordata=newdata;
     break;
  }


  if (allocLineBuf) lineBuf = new unsigned char [bytesPerLine];
  if ((pageNo & 1) == 0) {
    convertLine = convertLineEven;
  } else {
    convertLine = convertLineOdd;
  }
  if (header.Duplex && (pageNo & 1) == 0 && swap_image_y) {
    for (unsigned int plane = 0;plane < nplanes;plane++) {
      unsigned char *bp = colordata + (header.cupsHeight - 1) * rowsize;

      for (unsigned int h = header.cupsHeight;h > 0;h--) {
        for (unsigned int band = 0;band < nbands;band++) {
          dp = convertLine(bp,lineBuf,h - 1,plane+band,header.cupsWidth,
                 bytesPerLine);
          cupsRasterWritePixels(raster,dp,bytesPerLine);
        }
        bp -= rowsize;
      }
    }
  } else {
    for (unsigned int plane = 0;plane < nplanes;plane++) {
      unsigned char *bp = colordata;

      for (unsigned int h = 0;h < header.cupsHeight;h++) {
        for (unsigned int band = 0;band < nbands;band++) {
          dp = convertLine(bp,lineBuf,h,plane+band,header.cupsWidth,
                 bytesPerLine);
          cupsRasterWritePixels(raster,dp,bytesPerLine);
        }
        bp += rowsize;
      }
    }
  }
  free(colordata);
  if (allocLineBuf) delete[] lineBuf;
}

static void outPage(poppler::document *doc, int pageNo,
  cups_raster_t *raster)
{
  int rotate = 0;
  double paperdimensions[2], /* Physical size of the paper */
    margins[4];	/* Physical margins of print */
  double l, swap;
  int imageable_area_fit = 0;
  int i;

  poppler::page *current_page =doc->create_page(pageNo-1);
  poppler::page_box_enum box = poppler::page_box_enum::media_box;
  poppler::rectf mediaBox = current_page->page_rect(box);
  poppler::page::orientation_enum orient = current_page->orientation();
  switch (orient) {
    case poppler::page::landscape: rotate=90;
     break;
    case poppler::page::upside_down: rotate=180;
     break;
    case poppler::page::seascape: rotate=270;
     break;
     default:rotate=0;
  }
  fprintf(stderr, "DEBUG: mediaBox = [ %f %f %f %f ]; rotate = %d\n",
	  mediaBox.left(), mediaBox.top(), mediaBox.right(), mediaBox.bottom(), rotate);
  l = mediaBox.width();
  if (l < 0) l = -l;
  if (rotate == 90 || rotate == 270)
    header.PageSize[1] = (unsigned)l;
  else
    header.PageSize[0] = (unsigned)l;
  l = mediaBox.height();
  if (l < 0) l = -l;
  if (rotate == 90 || rotate == 270)
    header.PageSize[0] = (unsigned)l;
  else
    header.PageSize[1] = (unsigned)l;

  memset(paperdimensions, 0, sizeof(paperdimensions));
  memset(margins, 0, sizeof(margins));
  if (ppd) {
    ppdRasterMatchPPDSize(&header, ppd, margins, paperdimensions, &imageable_area_fit, NULL);
    if (pwgraster == 1)
      memset(margins, 0, sizeof(margins));
  } else {
    for (i = 0; i < 2; i ++)
      paperdimensions[i] = header.PageSize[i];
    if (header.cupsImagingBBox[3] > 0.0) {
      /* Set margins if we have a bounding box defined ... */
      if (pwgraster == 0) {
	margins[0] = header.cupsImagingBBox[0];
	margins[1] = header.cupsImagingBBox[1];
	margins[2] = paperdimensions[0] - header.cupsImagingBBox[2];
	margins[3] = paperdimensions[1] - header.cupsImagingBBox[3];
      }
    } else
      /* ... otherwise use zero margins */
      for (i = 0; i < 4; i ++)
	margins[i] = 0.0;
    /*margins[0] = 0.0;
    margins[1] = 0.0;
    margins[2] = header.PageSize[0];
    margins[3] = header.PageSize[1];*/
  }

  if (header.Duplex && (pageNo & 1) == 0) {
    /* backside: change margin if needed */
    if (swap_margin_x) {
      swap = margins[2]; margins[2] = margins[0]; margins[0] = swap;
    }
    if (swap_margin_y) {
      swap = margins[3]; margins[3] = margins[1]; margins[1] = swap;
    }
  }

  if (imageable_area_fit == 0) {
    bitmapoffset[0] = margins[0] / 72.0 * header.HWResolution[0];
    bitmapoffset[1] = margins[3] / 72.0 * header.HWResolution[1];
  } else {
    bitmapoffset[0] = 0;
    bitmapoffset[1] = 0;
  }

  /* write page header */
  if (pwgraster == 0) {
    header.cupsWidth = ((paperdimensions[0] - margins[0] - margins[2]) /
			72.0 * header.HWResolution[0]) + 0.5;
    header.cupsHeight = ((paperdimensions[1] - margins[1] - margins[3]) /
			 72.0 * header.HWResolution[1]) + 0.5;
  } else {
    header.cupsWidth = (paperdimensions[0] /
			72.0 * header.HWResolution[0]) + 0.5;
    header.cupsHeight = (paperdimensions[1] /
			 72.0 * header.HWResolution[1]) + 0.5;
  }
  for (i = 0; i < 2; i ++) {
    header.cupsPageSize[i] = paperdimensions[i];
    header.PageSize[i] = (unsigned int)(header.cupsPageSize[i] + 0.5);
    if (pwgraster == 0)
      header.Margins[i] = margins[i] + 0.5;
    else
      header.Margins[i] = 0;
  }
  if (pwgraster == 0) {
    header.cupsImagingBBox[0] = margins[0];
    header.cupsImagingBBox[1] = margins[1];
    header.cupsImagingBBox[2] = paperdimensions[0] - margins[2];
    header.cupsImagingBBox[3] = paperdimensions[1] - margins[3];
    for (i = 0; i < 4; i ++)
      header.ImagingBoundingBox[i] =
	(unsigned int)(header.cupsImagingBBox[i] + 0.5);
  } else
    for (i = 0; i < 4; i ++) {
      header.cupsImagingBBox[i] = 0.0;
      header.ImagingBoundingBox[i] = 0;
    }

  bytesPerLine = header.cupsBytesPerLine = (header.cupsBitsPerPixel *
    header.cupsWidth + 7) / 8;
  if (header.cupsColorOrder == CUPS_ORDER_BANDED) {
    header.cupsBytesPerLine *= header.cupsNumColors;
  }
  if (!cupsRasterWriteHeader2(raster,&header)) {
      fprintf(stderr, "ERROR: Can't write page %d header\n",pageNo );
      exit(1);
  }

  /* write page image */
  writePageImage(raster,doc,pageNo);
}

static void setPopplerColorProfile()
{
  if (header.cupsBitsPerColor != 8 && header.cupsBitsPerColor != 16) {
    /* color Profile is not supported */
    return;
  }
  /* set poppler color profile */
  switch (header.cupsColorSpace) {
  case CUPS_CSPACE_CIELab:
  case CUPS_CSPACE_ICC1:
  case CUPS_CSPACE_ICC2:
  case CUPS_CSPACE_ICC3:
  case CUPS_CSPACE_ICC4:
  case CUPS_CSPACE_ICC5:
  case CUPS_CSPACE_ICC6:
  case CUPS_CSPACE_ICC7:
  case CUPS_CSPACE_ICC8:
  case CUPS_CSPACE_ICC9:
  case CUPS_CSPACE_ICCA:
  case CUPS_CSPACE_ICCB:
  case CUPS_CSPACE_ICCC:
  case CUPS_CSPACE_ICCD:
  case CUPS_CSPACE_ICCE:
  case CUPS_CSPACE_ICCF:
    if (colorProfile == NULL) {
      cmsCIExyY wp;
#ifdef USE_LCMS1
      cmsWhitePointFromTemp(6504,&wp); /* D65 White point */
#else
      cmsWhitePointFromTemp(&wp,6504); /* D65 White point */
#endif
      colorProfile = cmsCreateLab4Profile(&wp);
    }
    break;
  case CUPS_CSPACE_CIEXYZ:
    if (colorProfile == NULL) {
      /* tansform color space via CIELab */
      cmsCIExyY wp;
#ifdef USE_LCMS1
      cmsWhitePointFromTemp(6504,&wp); /* D65 White point */
#else
      cmsWhitePointFromTemp(&wp,6504); /* D65 White point */
#endif
      cmsxyY2XYZ(&D65WhitePoint,&wp);
      colorProfile = cmsCreateLab4Profile(&wp);
    }
    break;
  case CUPS_CSPACE_SRGB:
    colorProfile = cmsCreate_sRGBProfile();
    break;
  case CUPS_CSPACE_ADOBERGB:
    colorProfile = adobergb_profile();
    break;
  case CUPS_CSPACE_SW:
    colorProfile = sgray_profile();
    break;
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_WHITE:
  case CUPS_CSPACE_GOLD:
  case CUPS_CSPACE_SILVER:
    /* We can set specified profile to poppler profile */
    popplerColorProfile = colorProfile;
    break;
  case CUPS_CSPACE_CMYK:
  case CUPS_CSPACE_KCMY:
  case CUPS_CSPACE_KCMYcm:
  case CUPS_CSPACE_YMCK:
  case CUPS_CSPACE_RGBA:
  case CUPS_CSPACE_RGBW:
  case CUPS_CSPACE_GMCK:
  case CUPS_CSPACE_GMCS:
  case CUPS_CSPACE_CMY:
  case CUPS_CSPACE_YMC:
    /* use standard RGB */
    popplerColorProfile = NULL;
    break;
  default:
    fprintf(stderr, "ERROR: Specified ColorSpace is not supported\n" );
    exit(1);
    break;
  }
}

int main(int argc, char *argv[]) {
  poppler::document *doc;
  int i;
  int npages=0;
  cups_raster_t *raster;

  cmsSetLogErrorHandler(lcmsErrorHandler);
  parseOpts(argc, argv);

  if (argc == 6) {
    /* stdin */
    int fd;
    char name[BUFSIZ];
    char buf[BUFSIZ];
    int n;

    fd = cupsTempFd(name,sizeof(name));
    if (fd < 0) {
      fprintf(stderr, "ERROR: Can't create temporary file\n");
      exit(1);
    }

    /* copy stdin to the tmp file */
    while ((n = read(0,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
        fprintf(stderr, "ERROR: Can't copy stdin to temporary file\n" );
        close(fd);
	exit(1);
      }
    }
    close(fd);
    doc=poppler::document::load_from_file(name,"","");
    /* remove name */
    unlink(name);
  } else {
    /* argc == 7 filenmae is specified */
    FILE *fp;

    if ((fp = fopen(argv[6],"rb")) == 0) {
        fprintf(stderr, "ERROR: Can't open input file %s\n",argv[6]);
	exit(1);
    }
    parsePDFTOPDFComment(fp);
    fclose(fp);
    doc=poppler::document::load_from_file(argv[6],"","");
  }

  if(doc != NULL)
    npages = doc->pages();

  /* fix NumCopies, Collate ccording to PDFTOPDFComments */
  header.NumCopies = deviceCopies;
  header.Collate = deviceCollate ? CUPS_TRUE : CUPS_FALSE;
  /* fixed other values that pdftopdf handles */
  header.MirrorPrint = CUPS_FALSE;
  header.Orientation = CUPS_ORIENT_0;

  if (header.cupsBitsPerColor != 1
     && header.cupsBitsPerColor != 2
     && header.cupsBitsPerColor != 4
     && header.cupsBitsPerColor != 8
     && header.cupsBitsPerColor != 16) {
    fprintf(stderr, "ERROR: Specified color format is not supported\n");
    exit(1);
  }
  if (header.cupsColorOrder == CUPS_ORDER_PLANAR) {
    nplanes = header.cupsNumColors;
  } else {
    nplanes = 1;
  }
  if (header.cupsColorOrder == CUPS_ORDER_BANDED) {
    nbands = header.cupsNumColors;
  } else {
    nbands = 1;
  }
  /* set image's values */
  switch (header.cupsColorSpace) {
  case CUPS_CSPACE_CIELab:
  case CUPS_CSPACE_ICC1:
  case CUPS_CSPACE_ICC2:
  case CUPS_CSPACE_ICC3:
  case CUPS_CSPACE_ICC4:
  case CUPS_CSPACE_ICC5:
  case CUPS_CSPACE_ICC6:
  case CUPS_CSPACE_ICC7:
  case CUPS_CSPACE_ICC8:
  case CUPS_CSPACE_ICC9:
  case CUPS_CSPACE_ICCA:
  case CUPS_CSPACE_ICCB:
  case CUPS_CSPACE_ICCC:
  case CUPS_CSPACE_ICCD:
  case CUPS_CSPACE_ICCE:
  case CUPS_CSPACE_ICCF:
  case CUPS_CSPACE_CIEXYZ:
    if (header.cupsColorOrder != CUPS_ORDER_CHUNKED
       || (header.cupsBitsPerColor != 8
          && header.cupsBitsPerColor != 16)) {
      fprintf(stderr, "ERROR: Specified color format is not supported\n");
      exit(1);
    }
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_SRGB:
  case CUPS_CSPACE_ADOBERGB:
  case CUPS_CSPACE_CMY:
  case CUPS_CSPACE_YMC:
  case CUPS_CSPACE_CMYK:
  case CUPS_CSPACE_KCMY:
  case CUPS_CSPACE_KCMYcm:
  case CUPS_CSPACE_YMCK:
  case CUPS_CSPACE_RGBA:
  case CUPS_CSPACE_RGBW:
  case CUPS_CSPACE_GMCK:
  case CUPS_CSPACE_GMCS:
    popplerBitsPerPixel = 24;
    popplerNumColors = 3;
    break;
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_SW:
  case CUPS_CSPACE_WHITE:
  case CUPS_CSPACE_GOLD:
  case CUPS_CSPACE_SILVER:
    if (header.cupsBitsPerColor == 1
       && header.cupsBitsPerPixel == 1) {
      popplerBitsPerPixel = 1;
    } else {
      popplerBitsPerPixel = 8;
    }
    /* set paper color white */
    popplerNumColors = 1;
    break;
  default:
    fprintf(stderr, "ERROR: Specified ColorSpace is not supported\n");
    exit(1);
    break;
  }

  if (!cm_disabled) {
    setPopplerColorProfile();
  }

  if ((raster = cupsRasterOpen(1, pwgraster ? CUPS_RASTER_WRITE_PWG :
			       CUPS_RASTER_WRITE)) == 0) {
        fprintf(stderr, "ERROR: Can't open raster stream\n");
	exit(1);
  }
  selectConvertFunc(raster);
  if(doc != NULL){
    for (i = 1;i <= npages;i++) {
      outPage(doc,i,raster);
    }
  } else
    fprintf(stderr, "DEBUG: Input is empty, outputting empty file.\n");

  cupsRasterClose(raster);

  delete doc;
  if (ppd != NULL) {
    ppdClose(ppd);
  }
  if (colorProfile != NULL) {
    cmsCloseProfile(colorProfile);
  }
  if (popplerColorProfile != NULL && popplerColorProfile != colorProfile) {
    cmsCloseProfile(popplerColorProfile);
  }
  if (colorTransform != NULL) {
    cmsDeleteTransform(colorTransform);
  }

  return exitCode;
}

/* replace memory allocation methods for memory check */
/* For compatibility with g++ >= 4.7 compilers _GLIBCXX_THROW
 *  should be used as a guard, otherwise use traditional definition */
#ifndef _GLIBCXX_THROW
#define _GLIBCXX_THROW throw
#endif

void * operator new(size_t size) _GLIBCXX_THROW (std::bad_alloc)
{
  return malloc(size);
}

void operator delete(void *p) throw ()
{
  free(p);
}

void * operator new[](size_t size) _GLIBCXX_THROW (std::bad_alloc)
{
  return malloc(size);
}

void operator delete[](void *p) throw ()
{
  free(p);
}

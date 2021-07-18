/*
Copyright (c) 2008-2011 BBR Inc.  All rights reserved.
Copyright (c) 2012-2021 by Till Kamppeter
Copyright (c) 2019 by Tanmay Anand.
Modified 2021 by Pratyush Ranjan.

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
 pwgtoraster.c
 PWG Raster to CUPS Raster filter function
*/

#include "colormanager.h"
#include "image.h"
#include "bitmap.h"
#include "filter.h"
#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

#define USE_CMS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ppd/ppd.h>
#include <stdarg.h>
#include <cups/raster.h>
#include <cupsfilters/image.h>
#include <cupsfilters/raster.h>
#include <cupsfilters/colormanager.h>
#include <cupsfilters/bitmap.h>
#include <strings.h>
#include <math.h>
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

#define MAX_BYTES_PER_PIXEL 32

typedef struct cms_profile_s
{
  /* for color profiles */
  cmsHPROFILE colorProfile;
  cmsHPROFILE outputColorProfile;
  cmsHTRANSFORM colorTransform;
  cmsCIEXYZ D65WhitePoint;
  int renderingIntent;
  int cm_disabled;
  cm_calibration_t cm_calibrate;
} cms_profile_t;

typedef struct pwgtoraster_doc_s
{                /**** Document information ****/
  int bi_level;
  bool allocLineBuf;
  unsigned int bitspercolor;
  unsigned int outputNumColors; 
  unsigned int bitmapoffset[2];
  ppd_file_t *ppd;
  cups_page_header2_t inheader;
  cups_page_header2_t outheader;
  filter_logfunc_t logfunc;             /* Logging function, NULL for no
					   logging */
  void          *logdata;               /* User data for logging function, can
					   be NULL */
  cups_file_t	*inputfp;		/* Temporary file, if any */
  FILE		*outputfp;		/* Temporary file, if any */
  bool swap_image_x;
  bool swap_image_y;
  /* margin swapping */
  bool swap_margin_x;
  bool swap_margin_y;
  unsigned int nplanes;
  unsigned int nbands;
  unsigned int bytesPerLine; /* number of bytes per line */
                        /* Note: When CUPS_ORDER_BANDED,
                           cupsBytesPerLine = bytesPerLine*cupsNumColors */
  cms_profile_t color_profile;
} pwgtoraster_doc_t;

typedef unsigned char *(*ConvertCSpaceFunc)(unsigned char *src,
                        unsigned char *pixelBuf,
                        unsigned int x,
                        unsigned int y,
                        pwgtoraster_doc_t* doc);
typedef unsigned char *(*ConvertLineFunc)(unsigned char *src,
                        unsigned char *dst, 
                        unsigned int row, 
                        unsigned int plane,
                        unsigned int pixels, 
                        unsigned int size, 
                        pwgtoraster_doc_t* doc,
                        ConvertCSpaceFunc convertCSpace);

typedef struct conversion_function_s
{
  ConvertCSpaceFunc convertCSpace;	/* Function for conversion of colorspaces */
  ConvertLineFunc convertLineOdd;/* Function tom modify raster data of a line */
  ConvertLineFunc convertLineEven;
} conversion_function_t;

cmsCIExyY adobergb_wp_cms()
{
    double * xyY = cmWhitePointAdobeRgb();
    cmsCIExyY wp;

    wp.x = xyY[0];
    wp.y = xyY[1];
    wp.Y = xyY[2];

    return wp;
}

cmsCIExyY sgray_wp_cms()
{
    double * xyY = cmWhitePointSGray();
    cmsCIExyY wp;

    wp.x = xyY[0];
    wp.y = xyY[1];
    wp.Y = xyY[2];

    return wp;
}

cmsCIExyYTRIPLE adobergb_matrix_cms()
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
    primaries = adobergb_matrix_cms();
    wp = adobergb_wp_cms();
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
    wp = sgray_wp_cms();
    sgray = cmsCreateGrayProfile(&wp, Gamma);

    return sgray;
}


#ifdef USE_LCMS1
static int lcmsErrorHandler(int ErrorCode, const char *ErrorText)
{
  return 1;
}
#else
static void lcmsErrorHandler(cmsContext contextId, cmsUInt32Number ErrorCode,
   const char *ErrorText)
{
  return;
}
#endif

static void  handleRequiresPageRegion(pwgtoraster_doc_t*doc) {
  ppd_choice_t *mf;
  ppd_choice_t *is;
  ppd_attr_t *rregions = NULL;
  ppd_size_t *size;

  if ((size = ppdPageSize(doc->ppd,NULL)) == NULL) return;
  mf = ppdFindMarkedChoice(doc->ppd,"ManualFeed");
  if ((is = ppdFindMarkedChoice(doc->ppd,"InputSlot")) != NULL) {
    rregions = ppdFindAttr(doc->ppd,"RequiresPageRegion",is->choice);
  }
  if (rregions == NULL) {
    rregions = ppdFindAttr(doc->ppd,"RequiresPageRegion","All");
  }
  if (!strcasecmp(size->name,"Custom") || (!mf && !is) ||
      (mf && !strcasecmp(mf->choice,"False") &&
       (!is || (is->code && !is->code[0]))) ||
      (!rregions && doc->ppd->num_filters > 0)) {
    ppdMarkOption(doc->ppd,"PageSize",size->name);
  } else if (rregions && rregions->value
      && !strcasecmp(rregions->value,"True")) {
    ppdMarkOption(doc->ppd,"PageRegion",size->name);
  } else {
    ppd_choice_t *page;

    if ((page = ppdFindMarkedChoice(doc->ppd,"PageSize")) != NULL) {
      page->marked = 0;
      cupsArrayRemove(doc->ppd->marked,page);
    }
    if ((page = ppdFindMarkedChoice(doc->ppd,"PageRegion")) != NULL) {
      page->marked = 0;
      cupsArrayRemove(doc->ppd->marked, page);
    }
  }
}

static void parseOpts(filter_data_t *data,
		      void *parameters,
		      pwgtoraster_doc_t *doc)
{
  int num_options = 0;
  cups_option_t *options = NULL;
  char *profile = NULL;
  ppd_attr_t *attr;
  const char *val;
  filter_logfunc_t log = data->logfunc;
  void *ld = data ->logdata;

  (void)parameters;
  
  if (data->ppd)
    doc->ppd = data->ppd;
  else if (data->ppdfile)
    doc->ppd = ppdOpenFile(data->ppdfile);

  if (doc->ppd == NULL)
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
      "pwgtoraster: No PPD file is specified.");

  options = data->options;
  num_options = data->num_options;
  if (doc->ppd) {
    ppdMarkOptions(doc->ppd,num_options,options);
    handleRequiresPageRegion(doc);
    ppdRasterInterpretPPD(&(doc->outheader),doc->ppd,num_options,options,0);
    attr = ppdFindAttr(doc->ppd,"pwgtorasterRenderingIntent",NULL);
    if (attr != NULL && attr->value != NULL) {
      if (strcasecmp(attr->value,"PERCEPTUAL") == 0) {
	doc->color_profile.renderingIntent = INTENT_PERCEPTUAL;
      } else if (strcasecmp(attr->value,"RELATIVE_COLORIMETRIC") == 0) {
	doc->color_profile.renderingIntent = INTENT_RELATIVE_COLORIMETRIC;
      } else if (strcasecmp(attr->value,"SATURATION") == 0) {
	doc->color_profile.renderingIntent = INTENT_SATURATION;
      } else if (strcasecmp(attr->value,"ABSOLUTE_COLORIMETRIC") == 0) {
	doc->color_profile.renderingIntent = INTENT_ABSOLUTE_COLORIMETRIC;
      }
    }
    if (doc->outheader.Duplex) {
      /* analyze options relevant to Duplex */
      const char *backside = "";
      /* APDuplexRequiresFlippedMargin */
      enum {
	FM_NO, FM_FALSE, FM_TRUE
      } flippedMargin = FM_NO;

      attr = ppdFindAttr(doc->ppd,"cupsBackSide",NULL);
      if (attr != NULL && attr->value != NULL) {
	doc->ppd->flip_duplex = 0;
	backside = attr->value;
      } else if (doc->ppd->flip_duplex) {
	backside = "Rotated"; /* compatible with Max OS and GS 8.71 */
      }

      attr = ppdFindAttr(doc->ppd,"APDuplexRequiresFlippedMargin",NULL);
      if (attr != NULL && attr->value != NULL) {
	if (strcasecmp(attr->value,"true") == 0) {
	  flippedMargin = FM_TRUE;
	} else {
	  flippedMargin = FM_FALSE;
	}
      }
      if (strcasecmp(backside,"ManualTumble") == 0 && doc->outheader.Tumble) {
	doc->swap_image_x = doc->swap_image_y = true;
	doc->swap_margin_x = doc->swap_margin_y = true;
	if (flippedMargin == FM_TRUE) {
	  doc->swap_margin_y = false;
	}
      } else if (strcasecmp(backside,"Rotated") == 0 && !doc->outheader.Tumble) {
	doc->swap_image_x = doc->swap_image_y = true;
	doc->swap_margin_x = doc->swap_margin_y = true;
	if (flippedMargin == FM_TRUE) {
	  doc->swap_margin_y = false;
	}
      } else if (strcasecmp(backside,"Flipped") == 0) {
	if (doc->outheader.Tumble) {
	  doc->swap_image_x = true;
	  doc->swap_margin_x = doc->swap_margin_y = true;
	} else {
	  doc->swap_image_y = true;
	}
	if (flippedMargin == FM_FALSE) {
	  doc->swap_margin_y = !doc->swap_margin_y;
	}
      }
    }

    /* support the CUPS "cm-calibration" option */
    doc->color_profile.cm_calibrate = cmGetCupsColorCalibrateMode(options, num_options);

    if (doc->color_profile.cm_calibrate == CM_CALIBRATION_ENABLED)
      doc->color_profile.cm_disabled = 1;
    else
      doc->color_profile.cm_disabled = cmIsPrinterCmDisabled(data->printer);

    if (!doc->color_profile.cm_disabled)
      cmGetPrinterIccProfile(data->printer, &profile, doc->ppd);

    if (profile != NULL) {
      doc->color_profile.colorProfile = cmsOpenProfileFromFile(profile,"r");
      free(profile);
    }

    cupsRasterParseIPPOptions(&(doc->outheader), data, 0, 0);
  } else {
#ifdef HAVE_CUPS_1_7
    cupsRasterParseIPPOptions(&(doc->outheader), data, 0, 1);
    if (strcasecmp(doc->outheader.cupsRenderingIntent, "Perceptual") == 0) {
      doc->color_profile.renderingIntent = INTENT_PERCEPTUAL;
    } else if (strcasecmp(doc->outheader.cupsRenderingIntent, "Relative") == 0) {
      doc->color_profile.renderingIntent = INTENT_RELATIVE_COLORIMETRIC;
    } else if (strcasecmp(doc->outheader.cupsRenderingIntent, "Saturation") == 0) {
      doc->color_profile.renderingIntent = INTENT_SATURATION;
    } else if (strcasecmp(doc->outheader.cupsRenderingIntent, "Absolute") == 0) {
      doc->color_profile.renderingIntent = INTENT_ABSOLUTE_COLORIMETRIC;
    }
#else
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pwgtoraster: No PPD file specified.");
    exit(1);
#endif /* HAVE_CUPS_1_7 */
  }
  if ((val = cupsGetOption("print-color-mode", num_options, options)) != NULL
                           && !strncasecmp(val, "bi-level", 8))
    doc->bi_level = 1;
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
    "pwgtoraster: Page size requested: %s", doc->outheader.cupsPageSizeName);
}

static unsigned char *reverseLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  unsigned char *p = src;

  for (unsigned int j = 0;j < size;j++,p++) {
    *p = ~*p;
  }
  return src;
}

static unsigned char *reverseLineSwapByte(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
  unsigned int pixels, unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  dst = reverseOneBitLineSwap(src, dst, pixels, size);
  return dst;
}

static unsigned char *rgbToCMYKLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  cupsImageRGBToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *rgbToCMYKLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *rgbToCMYLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  /* do nothing */
  return src;
}

static unsigned char *lineSwap24(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pwgtoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  return src;
}

static unsigned char *convertCSpaceWithProfiles(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  cmsDoTransform(doc->color_profile.colorTransform,src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *convertCSpaceXYZ8(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  double alab[3];

  cmsDoTransform(doc->color_profile.colorTransform,src,alab,1);
  cmsCIELab lab;
  cmsCIEXYZ xyz;

  lab.L = alab[0];
  lab.a = alab[1];
  lab.b = alab[2];

  cmsLab2XYZ(&(doc->color_profile.D65WhitePoint),&xyz,&lab);
  pixelBuf[0] = 231.8181*xyz.X+0.5;
  pixelBuf[1] = 231.8181*xyz.Y+0.5;
  pixelBuf[2] = 231.8181*xyz.Z+0.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceXYZ16(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  double alab[3];
  unsigned short *sd = (unsigned short *)pixelBuf;

  cmsDoTransform(doc->color_profile.colorTransform,src,alab,1);
  cmsCIELab lab;
  cmsCIEXYZ xyz;

  lab.L = alab[0];
  lab.a = alab[1];
  lab.b = alab[2];

  cmsLab2XYZ(&(doc->color_profile.D65WhitePoint),&xyz,&lab);
  sd[0] = 59577.2727*xyz.X+0.5;
  sd[1] = 59577.2727*xyz.Y+0.5;
  sd[2] = 59577.2727*xyz.Z+0.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceLab8(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  double lab[3];
  cmsDoTransform(doc->color_profile.colorTransform ,src,lab,1);
  pixelBuf[0] = 2.55*lab[0]+0.5;
  pixelBuf[1] = lab[1]+128.5;
  pixelBuf[2] = lab[2]+128.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceLab16(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  double lab[3];
  cmsDoTransform(doc->color_profile.colorTransform,src,lab,1);
  unsigned short *sd = (unsigned short *)pixelBuf;
  sd[0] = 655.35*lab[0]+0.5;
  sd[1] = 256*(lab[1]+128)+0.5;
  sd[2] = 256*(lab[2]+128)+0.5;
  return pixelBuf;
}

static unsigned char *RGB8toRGBA(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  unsigned char *dp = pixelBuf;

  for (int i = 0;i < 3;i++) {
    *dp++ = *src++;
  }
  *dp = 255;
  return pixelBuf;
}

static unsigned char *RGB8toRGBW(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
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
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *RGB8toCMY(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  cupsImageRGBToCMY(src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *RGB8toYMC(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  cupsImageRGBToCMY(src,pixelBuf,1);
  /* swap C and Y */
  unsigned char d = pixelBuf[0];
  pixelBuf[0] = pixelBuf[2];
  pixelBuf[2] = d;
  return pixelBuf;
}

static unsigned char *RGB8toKCMY(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  unsigned char d = pixelBuf[3];
  pixelBuf[3] = pixelBuf[2];
  pixelBuf[2] = pixelBuf[1];
  pixelBuf[1] = pixelBuf[0];
  pixelBuf[0] = d;
  return pixelBuf;
}

static unsigned char *RGB8toKCMYcmTemp(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  return RGB8toKCMYcm(src, pixelBuf, x, y);
}

static unsigned char *RGB8toYMCK(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t* doc)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  /* swap C and Y */
  unsigned char d = pixelBuf[0];
  pixelBuf[0] = pixelBuf[2];
  pixelBuf[2] = d;
  return pixelBuf;
}

static unsigned char *W8toK8(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pwgtoraster_doc_t *doc)
{
  *pixelBuf = ~(*src);
  return pixelBuf;
}

static unsigned char *convertLineChunked(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+i*(doc->outputNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->outheader.cupsNumColors, doc->bitspercolor);
      writepixel(dst,0,i,pb, doc->outheader.cupsNumColors, doc->outheader.cupsBitsPerColor, doc->outheader.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLineChunkedSwap(unsigned char *src,
     unsigned char *dst, unsigned int row, unsigned int plane,
     unsigned int pixels, unsigned int size, pwgtoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+(pixels-i-1)*(doc->outputNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->outheader.cupsNumColors, doc->bitspercolor);
      writepixel(dst,0,i,pb, doc->outheader.cupsNumColors, doc->outheader.cupsBitsPerColor, doc->outheader.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLinePlane(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pwgtoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+i*(doc->outputNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->outheader.cupsNumColors, doc->bitspercolor);
      writepixel(dst,plane,i,pb, doc->outheader.cupsNumColors, doc->outheader.cupsBitsPerColor, doc->outheader.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLinePlaneSwap(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size, pwgtoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
{
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+(pixels-i-1)*(doc->outputNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->outheader.cupsNumColors, doc->bitspercolor);
      writepixel(dst,plane,i,pb, doc->outheader.cupsNumColors, doc->outheader.cupsBitsPerColor, doc->outheader.cupsColorOrder);
  }
  return dst;
}

/* handle special cases which are appear in gutenprint's PPDs. */
static bool selectSpecialCase(pwgtoraster_doc_t* doc, conversion_function_t* convert)
{
  int i;

  for (i = 0;specialCaseFuncs[i].bitsPerPixel > 0;i++) {
    if (doc->outheader.cupsColorSpace == specialCaseFuncs[i].cspace
       && doc->outheader.cupsBitsPerPixel == specialCaseFuncs[i].bitsPerPixel
       && doc->outheader.cupsBitsPerColor == specialCaseFuncs[i].bitsPerColor) {
      convert->convertLineOdd = specialCaseFuncs[i].convertLine;
      if (doc->outheader.Duplex && doc->swap_image_x) {
        convert->convertLineEven = specialCaseFuncs[i].convertLineSwap;
        doc->allocLineBuf = specialCaseFuncs[i].allocLineBufSwap;
      } else {
        convert->convertLineEven = specialCaseFuncs[i].convertLine;
        doc->allocLineBuf = specialCaseFuncs[i].allocLineBuf;
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
static void selectConvertFunc(cups_raster_t *raster,
			      pwgtoraster_doc_t* doc,
			      conversion_function_t *convert,
			      filter_logfunc_t log,
			      void* ld)
{
  if ((doc->color_profile.colorProfile == NULL || doc->color_profile.outputColorProfile == doc->color_profile.colorProfile)
      && (doc->outheader.cupsColorOrder == CUPS_ORDER_CHUNKED
       || doc->outheader.cupsNumColors == 1)) {
    if (selectSpecialCase(doc, convert)) return;
  }

  switch (doc->outheader.cupsColorOrder) {
  case CUPS_ORDER_BANDED:
  case CUPS_ORDER_PLANAR:
    if (doc->outheader.cupsNumColors > 1) {
      convert->convertLineEven = convertLinePlaneSwap;
      convert->convertLineOdd = convertLinePlane;
      break;
    }
  default:
  case CUPS_ORDER_CHUNKED:
    convert->convertLineEven = convertLineChunkedSwap;
    convert->convertLineOdd = convertLineChunked;
    break;
  }
  if (!doc->outheader.Duplex || !doc->swap_image_x) {
    convert->convertLineEven = convert->convertLineOdd;
  }
  doc->allocLineBuf = true;

  if (doc->color_profile.colorProfile != NULL && doc->color_profile.outputColorProfile != doc->color_profile.colorProfile) {
    unsigned int bytes;

    switch (doc->outheader.cupsColorSpace) {
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
      if (doc->outheader.cupsBitsPerColor == 8) {
        convert->convertCSpace = convertCSpaceLab8;
      } else {
        /* 16 bits */
        convert->convertCSpace = convertCSpaceLab16;
      }
      bytes = 0; /* double */
      break;
    case CUPS_CSPACE_CIEXYZ:
      if (doc->outheader.cupsBitsPerColor == 8) {
        convert->convertCSpace = convertCSpaceXYZ8;
      } else {
        /* 16 bits */
        convert->convertCSpace = convertCSpaceXYZ16;
      }
      bytes = 0; /* double */
      break;
    default:
      convert->convertCSpace = convertCSpaceWithProfiles;
      bytes = doc->outheader.cupsBitsPerColor/8;
      break;
    }
    doc->bitspercolor = 0; /* convert bits in convertCSpace */
    if (doc->color_profile.outputColorProfile == NULL) {
      doc->color_profile.outputColorProfile = cmsCreate_sRGBProfile();
    }
    unsigned int dcst =
      getCMSColorSpaceType(cmsGetColorSpace(doc->color_profile.colorProfile));
    if ((doc->color_profile.colorTransform =
	 cmsCreateTransform(doc->color_profile.outputColorProfile,
			    COLORSPACE_SH(PT_RGB) |CHANNELS_SH(3) | BYTES_SH(1),
			    doc->color_profile.colorProfile,
			    COLORSPACE_SH(dcst) |
			    CHANNELS_SH(doc->outheader.cupsNumColors) |
			    BYTES_SH(bytes),
			    doc->color_profile.renderingIntent,0)) == 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pwgtoraster: Can't create color transform.");
      exit(1);
    }
  } else {
    /* select convertCSpace function */
    switch (doc->outheader.cupsColorSpace) {
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
      convert->convertCSpace = convertCSpaceNone;
      break;
    case CUPS_CSPACE_CMY:
      convert->convertCSpace = RGB8toCMY;
      break;
    case CUPS_CSPACE_YMC:
      convert->convertCSpace = RGB8toYMC;
      break;
    case CUPS_CSPACE_CMYK:
      convert->convertCSpace = RGB8toCMYK;
      break;
    case CUPS_CSPACE_KCMY:
      convert->convertCSpace = RGB8toKCMY;
      break;
    case CUPS_CSPACE_KCMYcm:
      if (doc->outheader.cupsBitsPerColor > 1) {
        convert->convertCSpace = RGB8toKCMY;
      } else {
        convert->convertCSpace = RGB8toKCMYcmTemp;
      }
      break;
    case CUPS_CSPACE_GMCS:
    case CUPS_CSPACE_GMCK:
    case CUPS_CSPACE_YMCK:
      convert->convertCSpace = RGB8toYMCK;
      break;
    case CUPS_CSPACE_RGBW:
      convert->convertCSpace = RGB8toRGBW;
      break;
    case CUPS_CSPACE_RGBA:
      convert->convertCSpace = RGB8toRGBA;
      break;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_SRGB:
    case CUPS_CSPACE_ADOBERGB:
      convert->convertCSpace = convertCSpaceNone;
      break;
    case CUPS_CSPACE_W:
    case CUPS_CSPACE_SW:
    case CUPS_CSPACE_WHITE:
      convert->convertCSpace = convertCSpaceNone;
      break;
    case CUPS_CSPACE_K:
    case CUPS_CSPACE_GOLD:
    case CUPS_CSPACE_SILVER:
      convert->convertCSpace = W8toK8;
      break;
    default:
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pwgtoraster: Specified ColorSpace is not supported");
      exit(1);
      break;
    }
  }

  if (doc->outheader.cupsBitsPerColor == 1 &&
     (doc->outheader.cupsNumColors == 1 ||
     doc->outheader.cupsColorSpace == CUPS_CSPACE_KCMYcm ))
    doc->bitspercolor = 0; /*Do not convertbits*/
  else
    doc->bitspercolor = doc->outheader.cupsBitsPerColor;

}

static bool outPage(pwgtoraster_doc_t *doc,
		    int pageNo,
		    cups_raster_t *inras,
		    cups_raster_t *outras,
		    conversion_function_t *convert,
		    filter_logfunc_t log,
		    void *ld,
		    filter_iscanceledfunc_t iscanceled,
		    void *icd)
{
  int i, j;
  double paperdimensions[2], /* Physical size of the paper */
    margins[4];	/* Physical margins of print */
  double swap;
  int imageable_area_fit = 0;
  int landscape_fit = 0;
  unsigned int y = 0, yin = 0;
  ConvertLineFunc convertLine;
  unsigned char *lineBuf = NULL;
  unsigned char *dp;
  unsigned int inlineoffset,     // Offset where to start in input line (bytes)
               inlinesize;       // How many bytes to take from input line
  int input_color_mode;
  int color_mode_needed;
  bool ret = true;
  unsigned char *line = NULL;
  unsigned char *lineavg = NULL;
  unsigned char *pagebuf = NULL;
  unsigned int res_down_factor[2];
  unsigned int res_up_factor[2];


  if (iscanceled && iscanceled(icd))
  {
    // Canceled
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster: Job canceled on input page %d", pageNo);
    return (false);
  }

  if (!cupsRasterReadHeader2(inras, &(doc->inheader)))
  {
    // Done
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster: Job completed");
    return (false);
  }

  if (strcasecmp(doc->inheader.MediaClass, "PwgRaster") != 0)
  {
    // Not PWG Raster
    log(ld, FILTER_LOGLEVEL_ERROR,
	"pwgtoraster: Input page %d is not PWG Raster", pageNo);
    return (false);
  }
  
  if (log) {
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster: Input page %d", pageNo);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   HWResolution = [ %d %d ]",
	doc->inheader.HWResolution[0], doc->inheader.HWResolution[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   PageSize = [ %d %d ]",
	doc->inheader.PageSize[0], doc->inheader.PageSize[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsWidth = %d", doc->inheader.cupsWidth);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsHeight = %d", doc->inheader.cupsHeight);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBitsPerColor = %d", doc->inheader.cupsBitsPerColor);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBitsPerPixel = %d", doc->inheader.cupsBitsPerPixel);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBytesPerLine = %d", doc->inheader.cupsBytesPerLine);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsColorOrder = %d", doc->inheader.cupsColorOrder);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsColorSpace = %d", doc->inheader.cupsColorSpace);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsCompression = %d", doc->inheader.cupsCompression);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsPageSizeName = %s", doc->inheader.cupsPageSizeName);
  }

  doc->outheader.PageSize[0] = doc->inheader.PageSize[0];
  doc->outheader.PageSize[1] = doc->inheader.PageSize[1];

  memset(paperdimensions, 0, sizeof(paperdimensions));
  memset(margins, 0, sizeof(margins));
  if (doc->ppd)
  {
    
    ppdRasterMatchPPDSize(&(doc->outheader), doc->ppd, margins,
			  paperdimensions, &imageable_area_fit, &landscape_fit);
  }
  else
  {
    for (i = 0; i < 2; i ++)
      paperdimensions[i] = doc->outheader.PageSize[i];
    if (doc->outheader.cupsImagingBBox[3] > 0.0)
    {
      // Set margins if we have a bounding box defined ...
      margins[0] = doc->outheader.cupsImagingBBox[0];
      margins[1] = doc->outheader.cupsImagingBBox[1];
      margins[2] = paperdimensions[0] - doc->outheader.cupsImagingBBox[2];
      margins[3] = paperdimensions[1] - doc->outheader.cupsImagingBBox[3];
    }
    else
      // ... otherwise use zero margins
      for (i = 0; i < 4; i ++)
	margins[i] = 0.0;
  }

  if (doc->outheader.Duplex && (pageNo & 1) == 0) {
    /* backside: change margin if needed */
    if (doc->swap_margin_x) {
      swap = margins[2]; margins[2] = margins[0]; margins[0] = swap;
    }
    if (doc->swap_margin_y) {
      swap = margins[3]; margins[3] = margins[1]; margins[1] = swap;
    }
  }

  if (imageable_area_fit == 0) {
    doc->bitmapoffset[0] = margins[0] / 72.0 * doc->outheader.HWResolution[0];
    doc->bitmapoffset[1] = margins[3] / 72.0 * doc->outheader.HWResolution[1];
  } else {
    doc->bitmapoffset[0] = 0;
    doc->bitmapoffset[1] = 0;
  }

  // Write page header
  doc->outheader.cupsWidth = ((paperdimensions[0] - margins[0] - margins[2]) /
			      72.0 * doc->outheader.HWResolution[0]) + 0.5;
  doc->outheader.cupsHeight = ((paperdimensions[1] - margins[1] - margins[3]) /
			       72.0 * doc->outheader.HWResolution[1]) + 0.5;

  for (i = 0; i < 2; i ++) {
    doc->outheader.cupsPageSize[i] = paperdimensions[i];
    doc->outheader.PageSize[i] =
      (unsigned int)(doc->outheader.cupsPageSize[i] + 0.5);
    doc->outheader.Margins[i] = margins[i] + 0.5;
  }

  doc->outheader.cupsImagingBBox[0] = margins[0];
  doc->outheader.cupsImagingBBox[1] = margins[1];
  doc->outheader.cupsImagingBBox[2] = paperdimensions[0] - margins[2];
  doc->outheader.cupsImagingBBox[3] = paperdimensions[1] - margins[3];

  for (i = 0; i < 4; i ++)
    doc->outheader.ImagingBoundingBox[i] =
      (unsigned int)(doc->outheader.cupsImagingBBox[i] + 0.5);

  doc->bytesPerLine = doc->outheader.cupsBytesPerLine =
    (doc->outheader.cupsBitsPerPixel *
     doc->outheader.cupsWidth + 7) / 8;
  if (doc->outheader.cupsColorOrder == CUPS_ORDER_BANDED)
    doc->outheader.cupsBytesPerLine *= doc->outheader.cupsNumColors;

  if (!cupsRasterWriteHeader2(outras, &(doc->outheader))) {
    if (log) log(ld,FILTER_LOGLEVEL_ERROR,
		 "pwgtoraster: Can't write page %d header", pageNo);
    return (false);
  }

  if (log) {
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster: Output page %d", pageNo);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   Duplex = %d", doc->outheader.Duplex);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   HWResolution = [ %d %d ]",
	doc->outheader.HWResolution[0], doc->outheader.HWResolution[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   ImagingBoundingBox = [ %d %d %d %d ]",
	doc->outheader.ImagingBoundingBox[0],
	doc->outheader.ImagingBoundingBox[1],
	doc->outheader.ImagingBoundingBox[2],
	doc->outheader.ImagingBoundingBox[3]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   Margins = [ %d %d ]",
	doc->outheader.Margins[0], doc->outheader.Margins[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   ManualFeed = %d", doc->outheader.ManualFeed);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   MediaPosition = %d", doc->outheader.MediaPosition);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   NumCopies = %d", doc->outheader.NumCopies);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   Orientation = %d", doc->outheader.Orientation);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   PageSize = [ %d %d ]",
	doc->outheader.PageSize[0], doc->outheader.PageSize[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsWidth = %d", doc->outheader.cupsWidth);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsHeight = %d", doc->outheader.cupsHeight);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsMediaType = %d", doc->outheader.cupsMediaType);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBitsPerColor = %d",
	doc->outheader.cupsBitsPerColor);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBitsPerPixel = %d",
	doc->outheader.cupsBitsPerPixel);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBytesPerLine = %d",
	doc->outheader.cupsBytesPerLine);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsColorOrder = %d", doc->outheader.cupsColorOrder);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsColorSpace = %d", doc->outheader.cupsColorSpace);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsCompression = %d", doc->outheader.cupsCompression);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsPageSizeName = %s", doc->outheader.cupsPageSizeName);
  }
  
  // write page image

  if (iscanceled && iscanceled(icd))
    // Canceled
    return false;

  // Check for needed resolution conversions
  for (i = 0; i < 2; i ++)
  {
    res_down_factor[i] = 1;
    res_up_factor[i] = 1;

    if (doc->outheader.HWResolution[i] == doc->inheader.HWResolution[i])
    {
      log(ld, FILTER_LOGLEVEL_DEBUG,
	  "pwgtoraster: %s resolution: %d dpi",
	  i == 0 ? "Horizontal" : "Vertical", doc->inheader.HWResolution[i]);
      continue;
    }

    if (doc->outheader.HWResolution[i] > doc->inheader.HWResolution[i])
    {
      if (doc->outheader.HWResolution[i] % doc->inheader.HWResolution[i])
      {
	log(ld, FILTER_LOGLEVEL_ERROR,
	    "pwgtoraster: %s output resolution %d dpi is not an integer multiple of %s input resolution %d dpi",
	    i == 0 ? "Horizontal" : "Vertical", doc->outheader.HWResolution[i],
	    i == 0 ? "horizontal" : "vertical", doc->inheader.HWResolution[i]);
	return (false);
      }
      else
      {
	res_up_factor[i] =
	  doc->outheader.HWResolution[i] / doc->inheader.HWResolution[i];
	log(ld, FILTER_LOGLEVEL_DEBUG,
	    "pwgtoraster: %s input resolution: %d dpi; %s output resolution: %d dpi -> Raising by factor %d",
	    i == 0 ? "Horizontal" : "Vertical", doc->inheader.HWResolution[i],
	    i == 0 ? "Horizontal" : "Vertical", doc->outheader.HWResolution[i],
	    res_up_factor[i]);
      }
    }
    else if (doc->outheader.HWResolution[i] < doc->inheader.HWResolution[i])
    {
      if (doc->inheader.HWResolution[i] % doc->outheader.HWResolution[i])
      {
	log(ld, FILTER_LOGLEVEL_ERROR,
	    "pwgtoraster: %s input resolution %d dpi is not an integer multiple of %s output resolution %d dpi",
	    i == 0 ? "Horizontal" : "Vertical", doc->inheader.HWResolution[i],
	    i == 0 ? "horizontal" : "vertical", doc->outheader.HWResolution[i]);
	return (false);
      }
      else
      {
	res_down_factor[i] =
	  doc->inheader.HWResolution[i] / doc->outheader.HWResolution[i];
	log(ld, FILTER_LOGLEVEL_DEBUG,
	    "pwgtoraster: %s input resolution: %d dpi; %s output resolution: %d dpi -> Reducing by factor %d",
	    i == 0 ? "Horizontal" : "Vertical", doc->inheader.HWResolution[i],
	    i == 0 ? "Horizontal" : "Vertical", doc->outheader.HWResolution[i],
	    res_down_factor[i]);
      }
    }
  }
  
  // Determine the input color space we have
  if (doc->inheader.cupsNumColors == 3)
  {
    input_color_mode = 2;
    inlineoffset = doc->bitmapoffset[0] * 3;
    inlinesize = doc->outheader.cupsWidth * 3;
  }
  else if (doc->inheader.cupsNumColors == 1 &&
	   doc->inheader.cupsBitsPerColor == 8)
  {
    input_color_mode = 1;
    inlineoffset = doc->bitmapoffset[0];
    inlinesize = doc->outheader.cupsWidth;
  }
  else if (doc->inheader.cupsNumColors == 1 &&
	   doc->inheader.cupsBitsPerColor == 1)
  {
    input_color_mode = 0;
    inlineoffset = doc->bitmapoffset[0] / 8; // Round down
    inlinesize = (doc->outheader.cupsWidth + 7) / 8; // Round up 
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pwgtoraster: Unsupported input color space: Number of colors: %d; Bits per color: %d.",
		 doc->inheader.cupsNumColors, doc->inheader.cupsBitsPerColor);
    return false;
  }

  // Determine which input color space we need to obtain the output color
  // space
  switch (doc->outheader.cupsColorSpace) {
   case CUPS_CSPACE_W:  // Gray
   case CUPS_CSPACE_K:  // Black
   case CUPS_CSPACE_SW: // sGray
    if (doc->outheader.cupsBitsPerColor == 1)
      color_mode_needed = 0;
    else
      color_mode_needed = 1;
    break;
   case CUPS_CSPACE_RGB:
   case CUPS_CSPACE_ADOBERGB:
   case CUPS_CSPACE_CMYK:
   case CUPS_CSPACE_SRGB:
   case CUPS_CSPACE_CMY:
   case CUPS_CSPACE_RGBW:
   default:
    color_mode_needed = 2;
    break;
  }

  if (input_color_mode != color_mode_needed)
  {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "pwgtoraster: Need to pre-convert input from %s to %s",
		 (input_color_mode == 0 ? "1-bit mono" :
		  (input_color_mode == 1 ? "8-bit gray" :
		   "8-bit RGB")),
		 (color_mode_needed == 0 ? "1-bit mono" :
		  (color_mode_needed == 1 ? "8-bit gray" :
		   "8-bit RGB")));
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "pwgtoraster: Input color mode: %s",
		 (input_color_mode == 0 ? "1-bit mono" :
		  (input_color_mode == 1 ? "8-bit gray" :
		   "8-bit RGB")));
  }    

  // Conversion line buffer (if needed by the conversion function
  if (doc->allocLineBuf)
    lineBuf = (unsigned char *)calloc(doc->bytesPerLine, sizeof(unsigned char));

  // Switch conversion functions for even and odd pages
  if ((pageNo & 1) == 0) {
    convertLine = convert->convertLineEven;
  } else {
    convertLine = convert->convertLineOdd;
  }

  // Inout line buffer (with space for raising horizontal resolution if needed)
  line =
    (unsigned char *)calloc(doc->inheader.cupsBytesPerLine * res_up_factor[0],
			    sizeof(unsigned char));

  // Input line averaging buffer (to reduce resolution)
  if (res_down_factor[1] > 1 && input_color_mode > 0)
    lineavg =
      (unsigned char *)calloc(doc->inheader.cupsBytesPerLine *
			      res_down_factor[1],
			      sizeof(unsigned char));

  // Input page buffer for color ordered in planes (if needed)
  if (doc->nplanes > 1)
    pagebuf = (unsigned char *)calloc(doc->outheader.cupsHeight * inlinesize,
				      sizeof(unsigned char));

  // Skip upper border
  for (y = 0, yin = 0; y < doc->bitmapoffset[1]; y ++)
    if (y % res_up_factor[1] == 0)
      for (i = 0; i < res_down_factor[1]; i ++)
	if (yin < doc->inheader.cupsHeight)
	{
	  // Read input pixel line
	  if (cupsRasterReadPixels(inras, line,
				   doc->inheader.cupsBytesPerLine) !=
	      doc->inheader.cupsBytesPerLine)
	  {
	    if (log) log(ld,FILTER_LOGLEVEL_DEBUG,
			 "pwgtoraster: Unable to read line %d for page %d.",
			 yin + 1, pageNo);
	    ret = false;
	    goto out;
	  }
	  yin ++;
	}

  // Convert the page from PWG Raster to CUPS Raster
  // We will be able to stream per-line if the color order in the destination
  // raster stream is chunked or banded and stream per-page if the colors are
  // arranged in planes
  for (unsigned int plane = 0; plane < doc->nplanes; plane ++) {
    for (y = doc->bitmapoffset[1];
	 y < doc->bitmapoffset[1] + doc->outheader.cupsHeight; y ++)
    {
      unsigned char *bp;

      if (plane == 0)
      {
	// First plane or no planes

	if (y % res_up_factor[1] == 0)
	{
	  for (i = 0; i < res_down_factor[1]; i ++)
	  {
	    if (yin < doc->inheader.cupsHeight)
	    {
	      // Read input pixel line
	      if (cupsRasterReadPixels(inras, line,
				       doc->inheader.cupsBytesPerLine) !=
		  doc->inheader.cupsBytesPerLine)
	      {
		if (log) log(ld,FILTER_LOGLEVEL_DEBUG,
			     "pwgtoraster: Unable to read line %d for page %d.",
			   yin + 1, pageNo);
		ret = false;
		goto out;
	      }
	      yin ++;
	    }
	    else
	    {
	      // White lines to fill the rest of the page
	      memset(line, 255, doc->inheader.cupsBytesPerLine);
	    }

	    // Collect lines for averaging (when reducing vertical resolution)
	    if (res_down_factor[1] > 1 && input_color_mode > 0)
	      memcpy(lineavg + i * doc->inheader.cupsBytesPerLine,
		     line, doc->inheader.cupsBytesPerLine);
	  }

	  // Calculate average of res_down_factor[1] input lines for reducing
	  // resolution (only for 8-bit color modes, on 1-bit only last line
	  // of the group is used)
	  if (res_down_factor[1] > 1 && input_color_mode > 0)
	    for (i = 0; i < doc->inheader.cupsBytesPerLine; i ++)
	    {
	      int val = 0;
	      for (j = 0; j < res_down_factor[1]; j ++)
		val += (int)*(lineavg + j * doc->inheader.cupsBytesPerLine + i);
	      line[i] = (unsigned char)(val / res_down_factor[1]);
	    }

	  // Converting pixel lines for horizontal resolution
	  if (res_up_factor[0] > 1) {
	    // Repeat every pixel in the line res_up_factor[0] times
	    if (input_color_mode == 0) {
	      unsigned char *src = line + doc->inheader.cupsBytesPerLine - 1,
		            *dst = line + doc->inheader.cupsBytesPerLine *
		                          res_up_factor[0] - 1;
	      unsigned char byte = 0, mask = 0x01;
	      while (src >= line)
	      {
		byte = *(src --);
		for (i = 0; i < 8; i ++)
		{
		  for (j = 0; j < res_up_factor[0]; j ++)
		  {
		    if (mask == 0x01)
		      *dst = 0;
		    if (byte & 0x01)
		      *dst |= mask;
		    mask <<= 1;
		    if (mask == 0)
		    {
		      dst --;
		      mask = 0x01;
		    }
		  }
		  byte >>= 1;
		}
	      }
	    } else if (input_color_mode == 1) {
	      unsigned char *src = line + doc->inheader.cupsBytesPerLine - 1,
		            *dst = line + (doc->inheader.cupsBytesPerLine - 1) *
		                          res_up_factor[0];
	      while (src >= line)
	      {
		for (i = 0; i < res_up_factor[0]; i ++)
		  *(dst + i) = *src;
		src -= 1;
		dst -= res_up_factor[0];
	      }
	    } else if (input_color_mode == 2) {
	      unsigned char *src = line + doc->inheader.cupsBytesPerLine - 3,
		            *dst = line + (doc->inheader.cupsBytesPerLine - 3) *
		                          res_up_factor[0];
	      while (src >= line)
	      {
		for (i = 0; i < res_up_factor[0]; i ++)
		  for (j = 0; j < 3; j ++)
		    *(dst + 3 * i + j) = *(src + j);
		src -= 3;
		dst -= 3 * res_up_factor[0];
	      }
	    }
	  } else if (res_down_factor[0] > 1) {
	    // Reduce the number of pixels per line to 1/res_down_factor[0]
	    if (input_color_mode == 0) {
	      // Grab the last of every group of res_down_factor[0] pixels
	      unsigned char *src = line, *dst = line;
	      unsigned char byte = 0, mask = 0x80;
	      j = 0;
	      while (src < line + doc->inheader.cupsBytesPerLine)
	      {
		byte = *(src ++);
		for (i = 0; i < 8; i ++)
		{
		  j ++;
		  if (j == res_down_factor[0])
		  {
		    if (mask == 0x80)
		      *dst = 0;
		    if (byte & 0x80)
		      *dst |= mask;
		    mask >>= 1;
		    if (mask == 0)
		    {
		      dst ++;
		      mask = 0x80;
		    }
		    j = 0;
		  }
		  byte <<= 1;
		}
	      }
	    } else if (input_color_mode == 1) {
	      // Average every group of res_down_factor[0] pixels
	      unsigned char *src = line, *dst = line;
	      int val = 0;
	      j = 0;
	      while (j || src < line + doc->inheader.cupsBytesPerLine)
	      {
		if (src < line + doc->inheader.cupsBytesPerLine)
		  val += (int)*(src ++);
		else
		  val += 255;
		j ++;
		if (j == res_down_factor[0])
		{
		  *(dst ++) = (unsigned char)(val / res_down_factor[0]);
		  val = 0;
		  j = 0;
		}
	      }
	    } else if (input_color_mode == 2) {
	      // Average every group of res_down_factor[0] pixels
	      unsigned char *src = line, *dst = line;
	      int val[3];
	      for (i = 0; i < 3; i ++)
		val[i] = 0;
	      i = 0;
	      j = 0;
	      while (j || src < line + doc->inheader.cupsBytesPerLine)
	      {
		if (src < line + doc->inheader.cupsBytesPerLine)
		  val[i ++] += (int)*(src ++);
		else
		  val[i ++] += 255;
		if (i == 3)
		{
		  j ++;
		  if (j == res_down_factor[0])
		  {
		    for (i = 0; i < 3; i ++)
		    {
		      *(dst ++) = (unsigned char)(val[i] / res_down_factor[0]);
		      val[i] = 0;
		    }
		    j = 0;
		  }
		  i = 0;
		}
	      }
	    }
	  }
	}
	
	// Pointer to the part of the input line we will use
	bp = line + inlineoffset;

	// Save input line for the other planes
	if (doc->nplanes > 1)
	  memcpy(pagebuf + (y - doc->bitmapoffset[0]) * inlinesize,
		 bp, inlinesize);
      }
      else
      {
	// Further planes

	// Pointer to input line in page buffer
	bp = pagebuf + (y - doc->bitmapoffset[1]) * inlinesize;
      } 

      // Pre-convert into the color mode needed to convert to the final
      // color space
      unsigned char *preBuf1 = NULL, *preBuf2 = NULL;
      if (input_color_mode != color_mode_needed)
      {
	if (input_color_mode == 2) // 8-bit RGB
	{
	  preBuf1 = (unsigned char *)calloc(doc->outheader.cupsWidth,
					    sizeof(unsigned char));
	  cupsImageRGBToWhite(bp, preBuf1, doc->outheader.cupsWidth);
	  bp = preBuf1;
	}
	else if (input_color_mode == 0) // 1-bit mono
	{
	  preBuf1 = (unsigned char *)calloc(doc->outheader.cupsWidth,
					    sizeof(unsigned char));
	  oneBitToGrayLine(bp, preBuf1, doc->outheader.cupsWidth);
	  bp = preBuf1;
	}
	// We are always on color mode 1 (8-bit gray) at this point
	if (color_mode_needed == 2) // 8-bit RGB
	{
	  preBuf2 = (unsigned char *)calloc(doc->outheader.cupsWidth * 3,
					    sizeof(unsigned char));
	  cupsImageWhiteToRGB(bp, preBuf2, doc->outheader.cupsWidth);
	  bp = preBuf2;
	}
	else if (color_mode_needed == 0) // 1-bit mono
	{
	  preBuf2 = (unsigned char *)calloc((doc->outheader.cupsWidth + 7) / 8,
					    sizeof(unsigned char));
	  oneBitLine(bp, preBuf2, doc->outheader.cupsWidth,
		     y - doc->bitmapoffset[1], doc->bi_level);
	  bp = preBuf2;
	}
      }

      // Convert the line into the destination format and put it out
      for (unsigned int band = 0; band < doc->nbands; band ++)
      {
	dp = convertLine(bp, lineBuf, y - doc->bitmapoffset[1],
			 plane + band, doc->outheader.cupsWidth,
			 doc->bytesPerLine, doc, convert->convertCSpace);
	cupsRasterWritePixels(outras, dp, doc->bytesPerLine);
      }

      // Clean up from pre-conversion
      if (preBuf1)
	free(preBuf1);
      if (preBuf2)
	free(preBuf2);
    }
  }

  // Read remaining input pixel lines
  for (; yin < doc->inheader.cupsHeight; yin ++)
    if (cupsRasterReadPixels(inras, line,
			     doc->inheader.cupsBytesPerLine) !=
	doc->inheader.cupsBytesPerLine)
    {
      if (log) log(ld,FILTER_LOGLEVEL_DEBUG,
		   "pwgtoraster: Unable to read line %d for page %d.",
		   yin + 1, pageNo);
      ret = false;
      goto out;
    }

 out:
  // Clean up
  free(line);
  if (res_down_factor[1] > 1 && input_color_mode > 0)
    free(lineavg);
  if (doc->nplanes > 1)
    free(pagebuf);
  if (doc->allocLineBuf)
    free(lineBuf);

  return (ret);
}

static void setColorProfile(pwgtoraster_doc_t *doc, filter_logfunc_t log, void *ld)
{
  if (doc->outheader.cupsBitsPerColor != 8 && doc->outheader.cupsBitsPerColor != 16) {
    /* color Profile is not supported */
    return;
  }
  /* set output color profile */
  switch (doc->outheader.cupsColorSpace) {
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
    if (doc->color_profile.colorProfile == NULL) {
      cmsCIExyY wp;
#ifdef USE_LCMS1
      cmsWhitePointFromTemp(6504,&wp); /* D65 White point */
#else
      cmsWhitePointFromTemp(&wp,6504); /* D65 White point */
#endif
      doc->color_profile.colorProfile  = cmsCreateLab4Profile(&wp);
    }
    break;
  case CUPS_CSPACE_CIEXYZ:
    if (doc->color_profile.colorProfile  == NULL) {
      /* transform color space via CIELab */
      cmsCIExyY wp;
#ifdef USE_LCMS1
      cmsWhitePointFromTemp(6504,&wp); /* D65 White point */
#else
      cmsWhitePointFromTemp(&wp,6504); /* D65 White point */
#endif
      cmsxyY2XYZ(&(doc->color_profile.D65WhitePoint),&wp);
      doc->color_profile.colorProfile  = cmsCreateLab4Profile(&wp);
    }
    break;
  case CUPS_CSPACE_SRGB:
    doc->color_profile.colorProfile  = cmsCreate_sRGBProfile();
    break;
  case CUPS_CSPACE_ADOBERGB:
    doc->color_profile.colorProfile  = adobergb_profile();
    break;
  case CUPS_CSPACE_SW:
    doc->color_profile.colorProfile  = sgray_profile();
    break;
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_WHITE:
  case CUPS_CSPACE_GOLD:
  case CUPS_CSPACE_SILVER:
    /* We can set specified profile to output profile */
    doc->color_profile.outputColorProfile  = doc->color_profile.colorProfile ;
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
    doc->color_profile.outputColorProfile = NULL;
    break;
  default:
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pwgtoraster: Specified ColorSpace is not supported");
    exit(1);
    break;
  }
}

int pwgtoraster(int inputfd,        /* I - File descriptor input stream */
                int outputfd,       /* I - File descriptor output stream */
                int inputseekable,  /* I - Is input stream seekable? (unused) */
                filter_data_t *data,/* I - Job and printer data */
                void *parameters)   /* I - Filter-specific parameters (unused)*/
{
  pwgtoraster_doc_t doc;
  int i;
  cups_raster_t *inras,
                *outras;
  filter_logfunc_t     log = data->logfunc;
  void                 *ld = data->logdata;
  conversion_function_t convert;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void                    *icd = data->iscanceleddata;


  (void)inputseekable;

  cmsSetLogErrorHandler(lcmsErrorHandler);

 /*
  * Open the input data stream specified by inputfd ...
  */
  
  if ((inras = cupsRasterOpen(inputfd, CUPS_RASTER_READ)) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pwgtoraster: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Prepare output document header
  */

  // Initialize data structure
  memset(&doc, 0, sizeof(doc));
  doc.color_profile.renderingIntent = INTENT_PERCEPTUAL;

  // Parse the options
  parseOpts(data, parameters, &doc);

  doc.outheader.NumCopies = data->copies;
  doc.outheader.MirrorPrint = CUPS_FALSE;
  doc.outheader.Orientation = CUPS_ORIENT_0;

  if (doc.outheader.cupsBitsPerColor != 1
     && doc.outheader.cupsBitsPerColor != 2
     && doc.outheader.cupsBitsPerColor != 4
     && doc.outheader.cupsBitsPerColor != 8
     && doc.outheader.cupsBitsPerColor != 16) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pwgtoraster: Specified color format is not supported.");
    exit(1);
  }

  if (doc.outheader.cupsColorOrder == CUPS_ORDER_PLANAR) {
    doc.nplanes = doc.outheader.cupsNumColors;
  } else {
    doc.nplanes = 1;
  }
  if (doc.outheader.cupsColorOrder == CUPS_ORDER_BANDED) {
    doc.nbands = doc.outheader.cupsNumColors;
  } else {
    doc.nbands = 1;
  }

 /*
  * Check color space and set color profile
  */

  switch (doc.outheader.cupsColorSpace) {
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
    if (doc.outheader.cupsColorOrder != CUPS_ORDER_CHUNKED
       || (doc.outheader.cupsBitsPerColor != 8
          && doc.outheader.cupsBitsPerColor != 16)) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pwgtoraster: Specified color format is not supported.");
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
    doc.outputNumColors = 3;
    break;
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_SW:
  case CUPS_CSPACE_WHITE:
  case CUPS_CSPACE_GOLD:
  case CUPS_CSPACE_SILVER:
    /* set paper color white */
    doc.outputNumColors = 1;
    break;
  default:
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pwgtoraster: Specified ColorSpace is not supported.");
    exit(1);
    break;
  }
  if (!(doc.color_profile.cm_disabled)) {
    setColorProfile(&doc, log, ld);
  }

 /*
  * Open output raster stream
  */

  if ((outras = cupsRasterOpen(outputfd, CUPS_RASTER_WRITE)) == 0) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pwgtoraster: Can't open output raster stream.");
    exit(1);
  }

 /*
  * Select conversion function
  */

  memset(&convert, 0, sizeof(conversion_function_t));
  selectConvertFunc(outras, &doc, &convert, log, ld);

  if (log) {
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster: Output page header");
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   Duplex = %d", doc.outheader.Duplex);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   HWResolution = [ %d %d ]",
	doc.outheader.HWResolution[0], doc.outheader.HWResolution[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   ImagingBoundingBox = [ %d %d %d %d ]",
	doc.outheader.ImagingBoundingBox[0],
	doc.outheader.ImagingBoundingBox[1],
	doc.outheader.ImagingBoundingBox[2],
	doc.outheader.ImagingBoundingBox[3]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   Margins = [ %d %d ]",
	doc.outheader.Margins[0], doc.outheader.Margins[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   ManualFeed = %d", doc.outheader.ManualFeed);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   MediaPosition = %d", doc.outheader.MediaPosition);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   NumCopies = %d", doc.outheader.NumCopies);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   Orientation = %d", doc.outheader.Orientation);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   PageSize = [ %d %d ]",
	doc.outheader.PageSize[0], doc.outheader.PageSize[1]);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsWidth = %d", doc.outheader.cupsWidth);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsHeight = %d", doc.outheader.cupsHeight);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsMediaType = %d", doc.outheader.cupsMediaType);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBitsPerColor = %d",
	doc.outheader.cupsBitsPerColor);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBitsPerPixel = %d",
	doc.outheader.cupsBitsPerPixel);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsBytesPerLine = %d",
	doc.outheader.cupsBytesPerLine);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsColorOrder = %d", doc.outheader.cupsColorOrder);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsColorSpace = %d", doc.outheader.cupsColorSpace);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsCompression = %d", doc.outheader.cupsCompression);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"pwgtoraster:   cupsPageSizeName = %s", doc.outheader.cupsPageSizeName);
  }

 /*
  * Print the pages
  */

  i = 0;
  while (outPage(&doc, i + 1, inras, outras, &convert, log, ld, iscanceled,
		 icd))
    i ++;
  if (i == 0)
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "pwgtoraster: No page printed, outputting empty file.");

 /*
  * Close the streams
  */
  
  cupsRasterClose(inras);
  close(inputfd);
  cupsRasterClose(outras);
  close(outputfd);

 /*
  * Clean up
  */
  if (doc.ppd != NULL) {
    ppdClose(doc.ppd);
  }
  if (doc.color_profile.colorProfile != NULL) {
    cmsCloseProfile(doc.color_profile.colorProfile);
  }
  if (doc.color_profile.outputColorProfile != NULL && doc.color_profile.outputColorProfile != doc.color_profile.colorProfile) {
    cmsCloseProfile(doc.color_profile.outputColorProfile);
  }
  if (doc.color_profile.colorTransform != NULL) {
    cmsDeleteTransform(doc.color_profile.colorTransform);
  }

  return 0;
}

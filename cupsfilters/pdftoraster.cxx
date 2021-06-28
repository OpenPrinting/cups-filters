/*
Copyright (c) 2008-2011 BBR Inc.  All rights reserved.
Copyright (c) 2012-2019 by Till Kamppeter
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
 pdftoraster.cxx
 pdf to raster filter function
*/

#include "colormanager.h"
#include "image.h"
#include "filter.h"
#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

#define USE_CMS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

typedef struct cms_profile_s
{
  /* for color profiles */
  cmsHPROFILE colorProfile = NULL;
  cmsHPROFILE popplerColorProfile = NULL;
  cmsHTRANSFORM colorTransform = NULL;
  cmsCIEXYZ D65WhitePoint;
  int renderingIntent = INTENT_PERCEPTUAL;
  int cm_disabled = 0;
  cm_calibration_t cm_calibrate;
} cms_profile_t;

typedef struct pdftoraster_doc_s
{                /**** Document information ****/
  int pwgraster = 0;
  int bi_level = 0;
  bool allocLineBuf = false;
  unsigned int bitspercolor;
  unsigned int popplerNumColors; 
  unsigned int bitmapoffset[2];
  ppd_file_t *ppd = 0;
  poppler::document *poppler_doc;
  cups_page_header2_t header;
  int         *JobCanceled;            /* Caller sets to 1 when job canceled */
  filter_logfunc_t logfunc;             /* Logging function, NULL for no
					   logging */
  void          *logdata;               /* User data for logging function, can
					   be NULL */
  cups_file_t	*inputfp;		/* Temporary file, if any */
  FILE		*outputfp;		/* Temporary file, if any */
  bool swap_image_x = false;
  bool swap_image_y = false;
  /* margin swapping */
  bool swap_margin_x = false;
  bool swap_margin_y = false;
  unsigned int nplanes;
  unsigned int nbands;
  unsigned int bytesPerLine; /* number of bytes per line */
                        /* Note: When CUPS_ORDER_BANDED,
                           cupsBytesPerLine = bytesPerLine*cupsNumColors */
  cms_profile_t colour_profile;
} pdftoraster_doc_t;

typedef unsigned char *(*ConvertCSpaceFunc)(unsigned char *src,
                        unsigned char *pixelBuf,
                        unsigned int x,
                        unsigned int y,
                        pdftoraster_doc_t* doc);
typedef unsigned char *(*ConvertLineFunc)(unsigned char *src,
                        unsigned char *dst, 
                        unsigned int row, 
                        unsigned int plane,
                        unsigned int pixels, 
                        unsigned int size, 
                        pdftoraster_doc_t* doc,
                        ConvertCSpaceFunc convertCSpace);

typedef struct conversion_function_s
{
  ConvertCSpaceFunc convertCSpace;	/* Function for conversion of colorspaces */
  ConvertLineFunc convertLineOdd;/* Function tom modify raster data of a line */
  ConvertLineFunc convertLineEven;
} conversion_function_t;

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
  return 1;
}
#else
static void lcmsErrorHandler(cmsContext contextId, cmsUInt32Number ErrorCode,
   const char *ErrorText)
{
  return;
}
#endif

static void  handleRqeuiresPageRegion(pdftoraster_doc_t*doc) {
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

static void parseOpts(filter_data_t *data, pdftoraster_doc_t *doc)
{
  int num_options = 0;
  cups_option_t *options = 0;
  char *profile = 0;
  const char *t = NULL;
  ppd_attr_t *attr;
  const char *val;
  filter_logfunc_t log = data->logfunc;
  void *ld = data ->logdata;

#ifdef HAVE_CUPS_1_7
  t = getenv("FINAL_CONTENT_TYPE");
  if (t && strcasestr(t, "pwg"))
    doc->pwgraster = 1;
#endif /* HAVE_CUPS_1_7 */

  doc->ppd = ppdOpenFile(getenv("PPD"));
  if (doc->ppd == NULL)
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
      "pdftoraster: PPD file is not specified.");

  if (doc->ppd)
    ppdMarkDefaults(doc->ppd);
  options = NULL;
  num_options = data->num_options;
  if (doc->ppd) {
    ppdMarkOptions(doc->ppd,num_options,options);
    handleRqeuiresPageRegion(doc);
    ppdRasterInterpretPPD(&(doc->header),doc->ppd,num_options,options,0);
    attr = ppdFindAttr(doc->ppd,"pdftorasterRenderingIntent",NULL);
    if (attr != NULL && attr->value != NULL) {
      if (strcasecmp(attr->value,"PERCEPTUAL") != 0) {
	doc->colour_profile.renderingIntent = INTENT_PERCEPTUAL;
      } else if (strcasecmp(attr->value,"RELATIVE_COLORIMETRIC") != 0) {
	doc->colour_profile.renderingIntent = INTENT_RELATIVE_COLORIMETRIC;
      } else if (strcasecmp(attr->value,"SATURATION") != 0) {
	doc->colour_profile.renderingIntent = INTENT_SATURATION;
      } else if (strcasecmp(attr->value,"ABSOLUTE_COLORIMETRIC") != 0) {
	doc->colour_profile.renderingIntent = INTENT_ABSOLUTE_COLORIMETRIC;
      }
    }
    if (doc->header.Duplex) {
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
      if (strcasecmp(backside,"ManualTumble") == 0 && doc->header.Tumble) {
	doc->swap_image_x = doc->swap_image_y = true;
	doc->swap_margin_x = doc->swap_margin_y = true;
	if (flippedMargin == FM_TRUE) {
	  doc->swap_margin_y = false;
	}
      } else if (strcasecmp(backside,"Rotated") == 0 && !doc->header.Tumble) {
	doc->swap_image_x = doc->swap_image_y = true;
	doc->swap_margin_x = doc->swap_margin_y = true;
	if (flippedMargin == FM_TRUE) {
	  doc->swap_margin_y = false;
	}
      } else if (strcasecmp(backside,"Flipped") == 0) {
	if (doc->header.Tumble) {
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
    doc->colour_profile.cm_calibrate = cmGetCupsColorCalibrateMode(options, num_options);

    if (doc->colour_profile.cm_calibrate == CM_CALIBRATION_ENABLED)
      doc->colour_profile.cm_disabled = 1;
    else
      doc->colour_profile.cm_disabled = cmIsPrinterCmDisabled(getenv("PRINTER"));

    if (!doc->colour_profile.cm_disabled)
      cmGetPrinterIccProfile(getenv("PRINTER"), &profile, doc->ppd);

    if (profile != NULL) {
      doc->colour_profile.colorProfile = cmsOpenProfileFromFile(profile,"r");
      free(profile);
    }

#ifdef HAVE_CUPS_1_7
    if ((attr = ppdFindAttr(doc->ppd,"PWGRaster",0)) != 0 &&
	(!strcasecmp(attr->value, "true")
	 || !strcasecmp(attr->value, "on") ||
	 !strcasecmp(attr->value, "yes")))
      doc->pwgraster = 1;
    if (doc->pwgraster == 1)
      cupsRasterParseIPPOptions(&(doc->header), data, doc->pwgraster, 0);
#endif /* HAVE_CUPS_1_7 */
  } else {
#ifdef HAVE_CUPS_1_7
    doc->pwgraster = 1;
    t = cupsGetOption("media-class", num_options, options);
    if (t == NULL)
      t = cupsGetOption("MediaClass", num_options, options);
    if (t != NULL)
    {
      if (strcasestr(t, "pwg"))
	doc->pwgraster = 1;
      else
	doc->pwgraster = 0;
    }
    cupsRasterParseIPPOptions(&(doc->header),data,doc->pwgraster,1);
#else
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
        "pdftoraster: No PPD file specified.");
    exit(1);
#endif /* HAVE_CUPS_1_7 */
  }
  if ((val = cupsGetOption("print-color-mode", num_options, options)) != NULL
                           && !strncasecmp(val, "bi-level", 8))
    doc->bi_level = 1;
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
    "pdftoraster: Page size requested: %s", doc->header.cupsPageSizeName);
}

static void parsePDFTOPDFComment(FILE *fp, int* deviceCopies, bool* deviceCollate)
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
      (*deviceCopies) = atoi(p+1);
    } else if (strncmp(buf,"%%PDFTOPDFCollate",17) == 0) {
      char *p;

      p = strchr(buf+17,':');
      while (*p == ' ' || *p == '\t') p++;
      if (strncasecmp(p,"true",4) == 0) {
	*deviceCollate = true;
      } else {
	*deviceCollate = false;
      }
    }
  }
}

static unsigned char *reverseLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  unsigned char *p = src;

  for (unsigned int j = 0;j < size;j++,p++) {
    *p = ~*p;
  }
  return src;
}

static unsigned char *reverseLineSwapByte(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
  unsigned int pixels, unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  dst = reverseOneBitLineSwap(src, dst, pixels, size);
  return dst;
}

static unsigned char *rgbToCMYKLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  cupsImageRGBToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *rgbToCMYKLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *rgbToCMYLineSwap(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  /* do nothing */
  return src;
}

static unsigned char *lineSwap24(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pdftoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
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
     unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
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
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  return src;
}

static unsigned char *convertCSpaceWithProfiles(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  cmsDoTransform(doc->colour_profile.colorTransform,src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *convertCSpaceXYZ8(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  double alab[3];

  cmsDoTransform(doc->colour_profile.colorTransform,src,alab,1);
  cmsCIELab lab;
  cmsCIEXYZ xyz;

  lab.L = alab[0];
  lab.a = alab[1];
  lab.b = alab[2];

  cmsLab2XYZ(&(doc->colour_profile.D65WhitePoint),&xyz,&lab);
  pixelBuf[0] = 231.8181*xyz.X+0.5;
  pixelBuf[1] = 231.8181*xyz.Y+0.5;
  pixelBuf[2] = 231.8181*xyz.Z+0.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceXYZ16(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  double alab[3];
  unsigned short *sd = (unsigned short *)pixelBuf;

  cmsDoTransform(doc->colour_profile.colorTransform,src,alab,1);
  cmsCIELab lab;
  cmsCIEXYZ xyz;

  lab.L = alab[0];
  lab.a = alab[1];
  lab.b = alab[2];

  cmsLab2XYZ(&(doc->colour_profile.D65WhitePoint),&xyz,&lab);
  sd[0] = 59577.2727*xyz.X+0.5;
  sd[1] = 59577.2727*xyz.Y+0.5;
  sd[2] = 59577.2727*xyz.Z+0.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceLab8(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  double lab[3];
  cmsDoTransform(doc->colour_profile.colorTransform ,src,lab,1);
  pixelBuf[0] = 2.55*lab[0]+0.5;
  pixelBuf[1] = lab[1]+128.5;
  pixelBuf[2] = lab[2]+128.5;
  return pixelBuf;
}

static unsigned char *convertCSpaceLab16(unsigned char *src,
  unsigned char *pixelBuf, unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  double lab[3];
  cmsDoTransform(doc->colour_profile.colorTransform,src,lab,1);
  unsigned short *sd = (unsigned short *)pixelBuf;
  sd[0] = 655.35*lab[0]+0.5;
  sd[1] = 256*(lab[1]+128)+0.5;
  sd[2] = 256*(lab[2]+128)+0.5;
  return pixelBuf;
}

static unsigned char *RGB8toRGBA(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
{
  unsigned char *dp = pixelBuf;

  for (int i = 0;i < 3;i++) {
    *dp++ = *src++;
  }
  *dp = 255;
  return pixelBuf;
}

static unsigned char *RGB8toRGBW(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
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
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *RGB8toCMY(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
{
  cupsImageRGBToCMY(src,pixelBuf,1);
  return pixelBuf;
}

static unsigned char *RGB8toYMC(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
{
  cupsImageRGBToCMY(src,pixelBuf,1);
  /* swap C and Y */
  unsigned char d = pixelBuf[0];
  pixelBuf[0] = pixelBuf[2];
  pixelBuf[2] = d;
  return pixelBuf;
}

static unsigned char *RGB8toKCMY(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
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
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
{
  return RGB8toKCMYcm(src, pixelBuf, x, y);
}

static unsigned char *RGB8toYMCK(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t* doc)
{
  cupsImageRGBToCMYK(src,pixelBuf,1);
  /* swap C and Y */
  unsigned char d = pixelBuf[0];
  pixelBuf[0] = pixelBuf[2];
  pixelBuf[2] = d;
  return pixelBuf;
}

static unsigned char *W8toK8(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y, pdftoraster_doc_t *doc)
{
  *pixelBuf = ~(*src);
  return pixelBuf;
}

static unsigned char *convertLineChunked(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+i*(doc->popplerNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->header.cupsNumColors, doc->bitspercolor);
      writepixel(dst,0,i,pb, doc->header.cupsNumColors, doc->header.cupsBitsPerColor, doc->header.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLineChunkedSwap(unsigned char *src,
     unsigned char *dst, unsigned int row, unsigned int plane,
     unsigned int pixels, unsigned int size, pdftoraster_doc_t* doc, ConvertCSpaceFunc convertCSpace)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+(pixels-i-1)*(doc->popplerNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->header.cupsNumColors, doc->bitspercolor);
      writepixel(dst,0,i,pb, doc->header.cupsNumColors, doc->header.cupsBitsPerColor, doc->header.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLinePlane(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels,
     unsigned int size, pdftoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
{
  /* Assumed that BitsPerColor is 8 */
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+i*(doc->popplerNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->header.cupsNumColors, doc->bitspercolor);
      writepixel(dst,plane,i,pb, doc->header.cupsNumColors, doc->header.cupsBitsPerColor, doc->header.cupsColorOrder);
  }
  return dst;
}

static unsigned char *convertLinePlaneSwap(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size, pdftoraster_doc_t *doc, ConvertCSpaceFunc convertCSpace)
{
  for (unsigned int i = 0;i < pixels;i++) {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;

      pb = convertCSpace(src+(pixels-i-1)*(doc->popplerNumColors),pixelBuf1,i,row, doc);
      pb = convertbits(pb,pixelBuf2,i,row, doc->header.cupsNumColors, doc->bitspercolor);
      writepixel(dst,plane,i,pb, doc->header.cupsNumColors, doc->header.cupsBitsPerColor, doc->header.cupsColorOrder);
  }
  return dst;
}

/* handle special cases which are appear in gutenprint's PPDs. */
static bool selectSpecialCase(pdftoraster_doc_t* doc, conversion_function_t* convert)
{
  int i;

  for (i = 0;specialCaseFuncs[i].bitsPerPixel > 0;i++) {
    if (doc->header.cupsColorSpace == specialCaseFuncs[i].cspace
       && doc->header.cupsBitsPerPixel == specialCaseFuncs[i].bitsPerPixel
       && doc->header.cupsBitsPerColor == specialCaseFuncs[i].bitsPerColor) {
      convert->convertLineOdd = specialCaseFuncs[i].convertLine;
      if (doc->header.Duplex && doc->swap_image_x) {
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
static void selectConvertFunc(cups_raster_t *raster, pdftoraster_doc_t* doc, conversion_function_t *convert, filter_logfunc_t log, void* ld)
{
  if ((doc->colour_profile.colorProfile == NULL || doc->colour_profile.popplerColorProfile == doc->colour_profile.colorProfile)
      && (doc->header.cupsColorOrder == CUPS_ORDER_CHUNKED
       || doc->header.cupsNumColors == 1)) {
    if (selectSpecialCase(doc, convert)) return;
  }

  switch (doc->header.cupsColorOrder) {
  case CUPS_ORDER_BANDED:
  case CUPS_ORDER_PLANAR:
    if (doc->header.cupsNumColors > 1) {
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
  if (!doc->header.Duplex || !doc->swap_image_x) {
    convert->convertLineEven = convert->convertLineOdd;
  }
  doc->allocLineBuf = true;

  if (doc->colour_profile.colorProfile != NULL && doc->colour_profile.popplerColorProfile != doc->colour_profile.colorProfile) {
    unsigned int bytes;

    switch (doc->header.cupsColorSpace) {
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
      if (doc->header.cupsBitsPerColor == 8) {
        convert->convertCSpace = convertCSpaceLab8;
      } else {
        /* 16 bits */
        convert->convertCSpace = convertCSpaceLab16;
      }
      bytes = 0; /* double */
      break;
    case CUPS_CSPACE_CIEXYZ:
      if (doc->header.cupsBitsPerColor == 8) {
        convert->convertCSpace = convertCSpaceXYZ8;
      } else {
        /* 16 bits */
        convert->convertCSpace = convertCSpaceXYZ16;
      }
      bytes = 0; /* double */
      break;
    default:
      convert->convertCSpace = convertCSpaceWithProfiles;
      bytes = doc->header.cupsBitsPerColor/8;
      break;
    }
    doc->bitspercolor = 0; /* convert bits in convertCSpace */
    if (doc->colour_profile.popplerColorProfile == NULL) {
      doc->colour_profile.popplerColorProfile = cmsCreate_sRGBProfile();
    }
    unsigned int dcst =
      getCMSColorSpaceType(cmsGetColorSpace(doc->colour_profile.colorProfile));
    if ((doc->colour_profile.colorTransform =
	 cmsCreateTransform(doc->colour_profile.popplerColorProfile,
			    COLORSPACE_SH(PT_RGB) |CHANNELS_SH(3) | BYTES_SH(1),
			    doc->colour_profile.colorProfile,
			    COLORSPACE_SH(dcst) |
			    CHANNELS_SH(doc->header.cupsNumColors) |
			    BYTES_SH(bytes),
			    doc->colour_profile.renderingIntent,0)) == 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Can't create color transform.");
      exit(1);
    }
  } else {
    /* select convertCSpace function */
    switch (doc->header.cupsColorSpace) {
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
      if (doc->header.cupsBitsPerColor > 1) {
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
		   "pdftoraster: Specified ColorSpace is not supported");
      exit(1);
      break;
    }
  }

  if (doc->header.cupsBitsPerColor == 1 &&
     (doc->header.cupsNumColors == 1 ||
     doc->header.cupsColorSpace == CUPS_CSPACE_KCMYcm ))
    doc->bitspercolor = 0; /*Do not convertbits*/
  else
    doc->bitspercolor = doc->header.cupsBitsPerColor;

}

static unsigned char *onebitpixel(unsigned char *src, unsigned char *dst, unsigned int width, unsigned int height, pdftoraster_doc_t* doc){
  unsigned char *temp;
  temp=dst;
  for(unsigned int i=0;i<height;i++){
    oneBitLine(src + (doc->bytesPerLine)*8*i, dst + (doc->bytesPerLine)*i, doc->header.cupsWidth, i, doc->bi_level);
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

static void writePageImage(cups_raster_t *raster, pdftoraster_doc_t *doc,
  int pageNo, conversion_function_t* convert)
{
  ConvertLineFunc convertLine;
  unsigned char *lineBuf = NULL;
  unsigned char *dp;
  unsigned int rowsize;

  poppler::page *current_page =doc->poppler_doc->create_page(pageNo-1);
  poppler::page_renderer pr;
  pr.set_render_hint(poppler::page_renderer::antialiasing, true);
  pr.set_render_hint(poppler::page_renderer::text_antialiasing, true);

  unsigned char *colordata,*newdata,*graydata,*onebitdata;
  unsigned int pixel_count;
  poppler::image im;
  // Render the page according to the colourspace and generate the requried data
  switch (doc->header.cupsColorSpace) {
   case CUPS_CSPACE_W:  // Gray
   case CUPS_CSPACE_K:  // Black
   case CUPS_CSPACE_SW: // sGray
    if(doc->header.cupsBitsPerColor==1){ // Special case for 1-bit colorspaces
      im = pr.render_page(current_page,doc->header.HWResolution[0],doc->header.HWResolution[1],doc->bitmapoffset[0],doc->bitmapoffset[1],(doc->bytesPerLine)*8,doc->header.cupsHeight);
    newdata = (unsigned char *)malloc(sizeof(char)*3*im.width()*im.height());
    newdata = removeAlpha((unsigned char *)im.const_data(),newdata,im.width(),im.height());
    graydata=(unsigned char *)malloc(sizeof(char)*im.width()*im.height());
    cupsImageRGBToWhite(newdata,graydata,im.width()*im.height());
    onebitdata=(unsigned char *)malloc(sizeof(char)*(doc->bytesPerLine)*im.height());
    onebitpixel(graydata,onebitdata,im.width(),im.height(), doc);
    colordata=onebitdata;
    rowsize=doc->bytesPerLine;
    }
    else{
      im = pr.render_page(current_page,doc->header.HWResolution[0],doc->header.HWResolution[1],doc->bitmapoffset[0],doc->bitmapoffset[1],doc->header.cupsWidth,doc->header.cupsHeight);
      newdata = (unsigned char *)malloc(sizeof(char)*3*im.width()*im.height());
      newdata = removeAlpha((unsigned char *)im.const_data(),newdata,im.width(),im.height());
      pixel_count=im.width()*im.height();
      graydata=(unsigned char *)malloc(sizeof(char)*im.width()*im.height());
      cupsImageRGBToWhite(newdata,graydata,pixel_count);
      colordata=graydata;
      rowsize=doc->header.cupsWidth;
    }

    break;
   case CUPS_CSPACE_RGB:
   case CUPS_CSPACE_ADOBERGB:
   case CUPS_CSPACE_CMYK:
   case CUPS_CSPACE_SRGB:
   case CUPS_CSPACE_CMY:
   case CUPS_CSPACE_RGBW:
   default:
   im = pr.render_page(current_page,doc->header.HWResolution[0],doc->header.HWResolution[1],doc->bitmapoffset[0],doc->bitmapoffset[1],doc->header.cupsWidth,doc->header.cupsHeight);
   newdata = (unsigned char *)malloc(sizeof(char)*3*im.width()*im.height());
   newdata = removeAlpha((unsigned char *)im.const_data(),newdata,im.width(),im.height());
   pixel_count=im.width()*im.height();
   rowsize=doc->header.cupsWidth*3;
   colordata=newdata;
     break;
  }


  if (doc->allocLineBuf) lineBuf = new unsigned char [doc->bytesPerLine];
  if ((pageNo & 1) == 0) {
    convertLine = convert->convertLineEven;
  } else {
    convertLine = convert->convertLineOdd;
  }
  if (doc->header.Duplex && (pageNo & 1) == 0 && doc->swap_image_y) {
    for (unsigned int plane = 0;plane < doc->nplanes;plane++) {
      unsigned char *bp = colordata + (doc->header.cupsHeight - 1) * rowsize;

      for (unsigned int h = doc->header.cupsHeight;h > 0;h--) {
        for (unsigned int band = 0;band < doc->nbands;band++) {
          dp = convertLine(bp,lineBuf,h - 1,plane+band,doc->header.cupsWidth,
                 doc->bytesPerLine, doc, convert->convertCSpace);
          cupsRasterWritePixels(raster,dp,doc->bytesPerLine);
        }
        bp -= rowsize;
      }
    }
  } else {
    for (unsigned int plane = 0;plane < doc->nplanes;plane++) {
      unsigned char *bp = colordata;

      for (unsigned int h = 0;h < doc->header.cupsHeight;h++) {
        for (unsigned int band = 0;band < doc->nbands;band++) {
          dp = convertLine(bp,lineBuf,h,plane+band,doc->header.cupsWidth,
                 doc->bytesPerLine, doc, convert->convertCSpace);
          cupsRasterWritePixels(raster,dp,doc->bytesPerLine);
        }
        bp += rowsize;
      }
    }
  }
  free(colordata);
  if (doc->allocLineBuf) delete[] lineBuf;
}

static void outPage(pdftoraster_doc_t *doc, int pageNo,
  cups_raster_t *raster, conversion_function_t *convert, filter_logfunc_t log, void* ld)
{
  int rotate = 0;
  double paperdimensions[2], /* Physical size of the paper */
    margins[4];	/* Physical margins of print */
  double l, swap;
  int imageable_area_fit = 0;
  int i;

  poppler::page *current_page =doc->poppler_doc->create_page(pageNo-1);
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
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "pdftoraster: mediabox = [ %f %f %f %f ]; rotate = %d",
	       mediaBox.left(), mediaBox.top(), mediaBox.right(),
	       mediaBox.bottom(), rotate);
  l = mediaBox.width();
  if (l < 0) l = -l;
  if (rotate == 90 || rotate == 270)
    doc->header.PageSize[1] = (unsigned)l;
  else
    doc->header.PageSize[0] = (unsigned)l;
  l = mediaBox.height();
  if (l < 0) l = -l;
  if (rotate == 90 || rotate == 270)
    doc->header.PageSize[0] = (unsigned)l;
  else
    doc->header.PageSize[1] = (unsigned)l;

  memset(paperdimensions, 0, sizeof(paperdimensions));
  memset(margins, 0, sizeof(margins));
  if (doc->ppd) {
    ppdRasterMatchPPDSize(&(doc->header), doc->ppd, margins, paperdimensions, &imageable_area_fit, NULL);
    if (doc->pwgraster == 1)
      memset(margins, 0, sizeof(margins));
  } else {
    for (i = 0; i < 2; i ++)
      paperdimensions[i] = doc->header.PageSize[i];
    if (doc->header.cupsImagingBBox[3] > 0.0) {
      /* Set margins if we have a bounding box defined ... */
      if (doc->pwgraster == 0) {
	margins[0] = doc->header.cupsImagingBBox[0];
	margins[1] = doc->header.cupsImagingBBox[1];
	margins[2] = paperdimensions[0] - doc->header.cupsImagingBBox[2];
	margins[3] = paperdimensions[1] - doc->header.cupsImagingBBox[3];
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

  if (doc->header.Duplex && (pageNo & 1) == 0) {
    /* backside: change margin if needed */
    if (doc->swap_margin_x) {
      swap = margins[2]; margins[2] = margins[0]; margins[0] = swap;
    }
    if (doc->swap_margin_y) {
      swap = margins[3]; margins[3] = margins[1]; margins[1] = swap;
    }
  }

  if (imageable_area_fit == 0) {
    doc->bitmapoffset[0] = margins[0] / 72.0 * doc->header.HWResolution[0];
    doc->bitmapoffset[1] = margins[3] / 72.0 * doc->header.HWResolution[1];
  } else {
    doc->bitmapoffset[0] = 0;
    doc->bitmapoffset[1] = 0;
  }

  /* write page header */
  if (doc->pwgraster == 0) {
    doc->header.cupsWidth = ((paperdimensions[0] - margins[0] - margins[2]) /
			72.0 * doc->header.HWResolution[0]) + 0.5;
    doc->header.cupsHeight = ((paperdimensions[1] - margins[1] - margins[3]) /
			 72.0 * doc->header.HWResolution[1]) + 0.5;
  } else {
    doc->header.cupsWidth = (paperdimensions[0] /
			72.0 * doc->header.HWResolution[0]) + 0.5;
    doc->header.cupsHeight = (paperdimensions[1] /
			 72.0 * doc->header.HWResolution[1]) + 0.5;
  }
  for (i = 0; i < 2; i ++) {
    doc->header.cupsPageSize[i] = paperdimensions[i];
    doc->header.PageSize[i] = (unsigned int)(doc->header.cupsPageSize[i] + 0.5);
    if (doc->pwgraster == 0)
      doc->header.Margins[i] = margins[i] + 0.5;
    else
      doc->header.Margins[i] = 0;
  }
  if (doc->pwgraster == 0) {
    doc->header.cupsImagingBBox[0] = margins[0];
    doc->header.cupsImagingBBox[1] = margins[1];
    doc->header.cupsImagingBBox[2] = paperdimensions[0] - margins[2];
    doc->header.cupsImagingBBox[3] = paperdimensions[1] - margins[3];
    for (i = 0; i < 4; i ++)
      doc->header.ImagingBoundingBox[i] =
	(unsigned int)(doc->header.cupsImagingBBox[i] + 0.5);
  } else
    for (i = 0; i < 4; i ++) {
      doc->header.cupsImagingBBox[i] = 0.0;
      doc->header.ImagingBoundingBox[i] = 0;
    }

  doc->bytesPerLine = doc->header.cupsBytesPerLine = (doc->header.cupsBitsPerPixel *
    doc->header.cupsWidth + 7) / 8;
  if (doc->header.cupsColorOrder == CUPS_ORDER_BANDED) {
    doc->header.cupsBytesPerLine *= doc->header.cupsNumColors;
  }
  if (!cupsRasterWriteHeader2(raster,&(doc->header))) {
    if (log) log(ld,FILTER_LOGLEVEL_ERROR,
		 "pdftoraster: Can't write page %d header", pageNo);
    exit(1);
  }

  /* write page image */
  writePageImage(raster,doc,pageNo, convert);
}

static void setPopplerColorProfile(pdftoraster_doc_t *doc, filter_logfunc_t log, void *ld)
{
  if (doc->header.cupsBitsPerColor != 8 && doc->header.cupsBitsPerColor != 16) {
    /* color Profile is not supported */
    return;
  }
  /* set poppler color profile */
  switch (doc->header.cupsColorSpace) {
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
    if (doc->colour_profile.colorProfile == NULL) {
      cmsCIExyY wp;
#ifdef USE_LCMS1
      cmsWhitePointFromTemp(6504,&wp); /* D65 White point */
#else
      cmsWhitePointFromTemp(&wp,6504); /* D65 White point */
#endif
      doc->colour_profile.colorProfile  = cmsCreateLab4Profile(&wp);
    }
    break;
  case CUPS_CSPACE_CIEXYZ:
    if (doc->colour_profile.colorProfile  == NULL) {
      /* tansform color space via CIELab */
      cmsCIExyY wp;
#ifdef USE_LCMS1
      cmsWhitePointFromTemp(6504,&wp); /* D65 White point */
#else
      cmsWhitePointFromTemp(&wp,6504); /* D65 White point */
#endif
      cmsxyY2XYZ(&(doc->colour_profile.D65WhitePoint),&wp);
      doc->colour_profile.colorProfile  = cmsCreateLab4Profile(&wp);
    }
    break;
  case CUPS_CSPACE_SRGB:
    doc->colour_profile.colorProfile  = cmsCreate_sRGBProfile();
    break;
  case CUPS_CSPACE_ADOBERGB:
    doc->colour_profile.colorProfile  = adobergb_profile();
    break;
  case CUPS_CSPACE_SW:
    doc->colour_profile.colorProfile  = sgray_profile();
    break;
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_WHITE:
  case CUPS_CSPACE_GOLD:
  case CUPS_CSPACE_SILVER:
    /* We can set specified profile to poppler profile */
    doc->colour_profile.popplerColorProfile  = doc->colour_profile.colorProfile ;
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
    doc->colour_profile.popplerColorProfile = NULL;
    break;
  default:
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pdftoraster: Specified ColorSpace is not supported");
    exit(1);
    break;
  }
}

int pdftoraster(int inputfd,         /* I - File descriptor input stream */
       int outputfd,                 /* I - File descriptor output stream */
       int inputseekable,            /* I - Is input stream seekable? (unused) */
       filter_data_t *data,          /* I - Job and printer data */
       void *parameters)             /* I - Filter-specific parameters (unused) */
{
  pdftoraster_doc_t doc;
  int i;
  int npages = 0;
  cups_raster_t *raster;
  cups_file_t	         *inputfp;		/* Print file */
  FILE                 *outputfp;   /* Output data stream */
  filter_logfunc_t     log = data->logfunc;
  void          *ld = data->logdata;
  int deviceCopies = 1;
  bool deviceCollate = false;
  conversion_function_t convert;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void                 *icd = data->iscanceleddata;
  
  cmsSetLogErrorHandler(lcmsErrorHandler);
  parseOpts(data, &doc);

 /*
  * Open the input data stream specified by inputfd ...
  */
  
  if ((inputfp = cupsFileOpenFd(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Open the output data stream specified by the outputfd...
  */

  if ((outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Unable to open output data stream.");
    }

    cupsFileClose(inputfp);

    return (1);
  }

 /*
  * Make a temporary file if input is stdin...
  */

  if (inputseekable == 0) {
    /* stdin */
    int fd;
    char name[BUFSIZ];
    char buf[BUFSIZ];
    int n;

    fd = cupsTempFd(name,sizeof(name));
    if (fd < 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Can't create temporary file.");
      exit(1);
    }

    /* copy stdin to the tmp file */
    while ((n = read(0,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Can't copy stdin to temporary file.");
        close(fd);
	exit(1);
      }
    }
    close(fd);
    doc.poppler_doc = poppler::document::load_from_file(name,"","");
    /* remove name */
    unlink(name);
  } else {
    // Make a temporary file and save input data in it...
    int fd;
    char name[BUFSIZ];
    char buf[BUFSIZ];
    int n;

    fd = cupsTempFd(name,sizeof(name));
    if (fd < 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Can't create temporary file.");
      exit(1);
    }
        /* copy input data to the tmp file */
    while ((n = read(inputfd,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
	if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		     "pdftoraster: Can't copy input data to temporary file.");
        close(fd);
	exit(1);
      }
    }
    close(fd);
    doc.poppler_doc = poppler::document::load_from_file(name,"","");
    unlink(name);

    FILE *fp;
    if ((fp = fdopen(inputfd,"rb")) == 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Can't open input file.");
      exit(1);
    }

    parsePDFTOPDFComment(fp, &deviceCopies, &deviceCollate);
    fclose(fp);
  }

  if(doc.poppler_doc != NULL)
    npages = doc.poppler_doc->pages();

  /* fix NumCopies, Collate ccording to PDFTOPDFComments */
  doc.header.NumCopies = deviceCopies;
  doc.header.Collate = deviceCollate ? CUPS_TRUE : CUPS_FALSE;
  /* fixed other values that pdftopdf handles */
  doc.header.MirrorPrint = CUPS_FALSE;
  doc.header.Orientation = CUPS_ORIENT_0;

  if (doc.header.cupsBitsPerColor != 1
     && doc.header.cupsBitsPerColor != 2
     && doc.header.cupsBitsPerColor != 4
     && doc.header.cupsBitsPerColor != 8
     && doc.header.cupsBitsPerColor != 16) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Specified color format is not supported.");
    exit(1);
  }
  
  if (doc.header.cupsColorOrder == CUPS_ORDER_PLANAR) {
    doc.nplanes = doc.header.cupsNumColors;
  } else {
    doc.nplanes = 1;
  }
  if (doc.header.cupsColorOrder == CUPS_ORDER_BANDED) {
    doc.nbands = doc.header.cupsNumColors;
  } else {
    doc.nbands = 1;
  }
  /* set image's values */
  switch (doc.header.cupsColorSpace) {
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
    if (doc.header.cupsColorOrder != CUPS_ORDER_CHUNKED
       || (doc.header.cupsBitsPerColor != 8
          && doc.header.cupsBitsPerColor != 16)) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Specified color format is not supported.");
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
    doc.popplerNumColors = 3;
    break;
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_SW:
  case CUPS_CSPACE_WHITE:
  case CUPS_CSPACE_GOLD:
  case CUPS_CSPACE_SILVER:
    /* set paper color white */
    doc.popplerNumColors = 1;
    break;
  default:
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "pdftoraster: Specified ColorSpace is not supported.");
    exit(1);
    break;
  }
  if (!(doc.colour_profile.cm_disabled)) {
    setPopplerColorProfile(&doc, log, ld);
  }

  if ((raster = cupsRasterOpen(1, doc.pwgraster ? CUPS_RASTER_WRITE_PWG :
			       CUPS_RASTER_WRITE)) == 0) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pdftoraster: Can't open raster stream.");
    exit(1);
  }
  memset(&convert, 0, sizeof(conversion_function_t));
  selectConvertFunc(raster, &doc, &convert, log, ld);
  if(doc.poppler_doc != NULL){    
    for (i = 1;i <= npages;i++) {
      outPage(&doc,i,raster, &convert, log, ld);
    }
  } else
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "pdftoraster: Input is empty outputting empty file.");

  cupsRasterClose(raster);

  // Delete doc
  if (doc.ppd != NULL) {
    ppdClose(doc.ppd);
  }
  if (doc.colour_profile.colorProfile != NULL) {
    cmsCloseProfile(doc.colour_profile.colorProfile);
  }
  if (doc.colour_profile.popplerColorProfile != NULL && doc.colour_profile.popplerColorProfile != doc.colour_profile.colorProfile) {
    cmsCloseProfile(doc.colour_profile.popplerColorProfile);
  }
  if (doc.colour_profile.colorTransform != NULL) {
    cmsDeleteTransform(doc.colour_profile.colorTransform);
  }

  return 0;
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
/*
void operator delete[](void *p) throw ()
{
  free(p);
}
*/

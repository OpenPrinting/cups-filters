/*
Copyright (c) 2008-2011 BBR Inc.  All rights reserved.

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
#include "cpp/poppler-version.h"
#endif
#include "goo/GooString.h"
#include "goo/gmem.h"
#include "Object.h"
#include "Stream.h"
#include "PDFDoc.h"
#include "SplashOutputDev.h"
#include "GfxState.h"
#include <cups/ppd.h>
#include <stdarg.h>
#include "PDFError.h"
#include "GlobalParams.h"
#include <cups/raster.h>
#include <cupsfilters/image.h>
#include <cupsfilters/raster.h>
#include <cupsfilters/colormanager.h>
#include <splash/SplashTypes.h>
#include <splash/SplashBitmap.h>
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

#define MAX_CHECK_COMMENT_LINES	20
#define MAX_BYTES_PER_PIXEL 32

namespace {
  typedef unsigned char *(*ConvertLineFunc)(unsigned char *src,
    unsigned char *dst, unsigned int row, unsigned int plane,
    unsigned int pixels, unsigned int size);
  typedef unsigned char *(*ConvertCSpaceFunc)(unsigned char *src,
    unsigned char *pixelBuf, unsigned int x, unsigned int y);
  typedef unsigned char *(*ConvertBitsFunc)(unsigned char *src,
    unsigned char *dst, unsigned int x, unsigned int y);
  typedef void (*WritePixelFunc)(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf);

  int exitCode = 0;
  int pwgraster = 0;
  int deviceCopies = 1;
  bool deviceCollate = false;
  cups_page_header2_t header;
  ppd_file_t *ppd = 0;
  unsigned int bitmapoffset[2];
  unsigned int popplerBitsPerPixel;
  unsigned int popplerNumColors;
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
  ConvertBitsFunc convertBits;
  WritePixelFunc writePixel;
  unsigned int nplanes;
  unsigned int nbands;
  unsigned int bytesPerLine; /* number of bytes per line */
                        /* Note: When CUPS_ORDER_BANDED,
                           cupsBytesPerLine = bytesPerLine*cupsNumColors */
  unsigned char revTable[256] = {
0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0,
0x08,0x88,0x48,0xc8,0x28,0xa8,0x68,0xe8,0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8,
0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,
0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec,0x1c,0x9c,0x5c,0xdc,0x3c,0xbc,0x7c,0xfc,
0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2,0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa,
0x06,0x86,0x46,0xc6,0x26,0xa6,0x66,0xe6,0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6,
0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,
0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1,0x11,0x91,0x51,0xd1,0x31,0xb1,0x71,0xf1,
0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9,0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5,
0x0d,0x8d,0x4d,0xcd,0x2d,0xad,0x6d,0xed,0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd,
0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,
0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb,0x1b,0x9b,0x5b,0xdb,0x3b,0xbb,0x7b,0xfb,
0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7,0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff
  };
  unsigned int dither1[16][16] = {
    {0,128,32,160,8,136,40,168,2,130,34,162,10,138,42,170},
    {192,64,224,96,200,72,232,104,194,66,226,98,202,74,234,106},
    {48,176,16,144,56,184,24,152,50,178,18,146,58,186,26,154},
    {240,112,208,80,248,120,216,88,242,114,210,82,250,122,218,90},
    {12,140,44,172,4,132,36,164,14,142,46,174,6,134,38,166},
    {204,76,236,108,196,68,228,100,206,78,238,110,198,70,230,102},
    {60,188,28,156,52,180,20,148,62,190,30,158,54,182,22,150},
    {252,124,220,92,244,116,212,84,254,126,222,94,246,118,214,86},
    {3,131,35,163,11,139,43,171,1,129,33,161,9,137,41,169},
    {195,67,227,99,203,75,235,107,193,65,225,97,201,73,233,105},
    {51,179,19,147,59,187,27,155,49,177,17,145,57,185,25,153},
    {243,115,211,83,251,123,219,91,241,113,209,81,249,121,217,89},
    {15,143,47,175,7,135,39,167,13,141,45,173,5,133,37,165},
    {207,79,239,111,199,71,231,103,205,77,237,109,197,69,229,101},
    {63,191,31,159,55,183,23,151,61,189,29,157,53,181,21,149},
    {255,127,223,95,247,119,215,87,253,125,221,93,245,117,213,85} 
  };
  unsigned int dither2[8][8] = {
    {0,32,8,40,2,34,10,42},
    {48,16,56,24,50,18,58,26},
    {12,44,4,36,14,46,6,38},
    {60,28,52,20,62,30,54,22},
    {3,35,11,43,1,33,9,41},
    {51,19,59,27,49,17,57,25},
    {15,47,7,39,13,45,5,37},
    {63,31,55,23,61,29,53,21} 
  };
  unsigned int dither4[4][4] = {
    {0,8,2,10},
    {12,4,14,6},
    {3,11,1,9},
    {15,7,13,5} 
  };

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

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
void CDECL myErrorFun(void *data, ErrorCategory category,
    Goffset pos, char *msg)
#else
void CDECL myErrorFun(void *data, ErrorCategory category,
    int pos, char *msg)
#endif
{
  if (pos >= 0) {
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
    fprintf(stderr, "ERROR (%lld): ", pos);
#else
    fprintf(stderr, "ERROR (%d): ", pos);
#endif
  } else {
    fprintf(stderr, "ERROR: ");
  }
  fprintf(stderr, "%s\n",msg);
  fflush(stderr);
}
#else
void CDECL myErrorFun(int pos, char *msg, va_list args)
{
  if (pos >= 0) {
    fprintf(stderr, "ERROR (%d): ", pos);
  } else {
    fprintf(stderr, "ERROR: ");
  }
  vfprintf(stderr, msg, args);
  fprintf(stderr, "\n");
  fflush(stderr);
}
#endif

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


#if 0
static GBool getColorProfilePath(ppd_file_t *ppd, GooString *path)
{
    // get color profile path
    const char *colorModel;
    const char *cupsICCQualifier2;
    const char *cupsICCQualifier2Choice;
    const char *cupsICCQualifier3;
    const char *cupsICCQualifier3Choice;
    ppd_attr_t *attr;
    ppd_choice_t *choice;

    if ((choice = ppdFindMarkedChoice(ppd,"ColorModel")) != NULL) {
	colorModel = choice->choice;
    } else {
	colorModel = NULL;
    }
    if ((attr = ppdFindAttr(ppd,"cupsICCQualifier2",NULL)) != NULL) {
	cupsICCQualifier2 = attr->value;
    } else {
	cupsICCQualifier2 = "MediaType";
    }
    if ((choice = ppdFindMarkedChoice(ppd,cupsICCQualifier2)) != NULL) {
	cupsICCQualifier2Choice = choice->choice;
    } else {
	cupsICCQualifier2Choice = NULL;
    }
    if ((attr = ppdFindAttr(ppd,"cupsICCQualifier3",NULL)) != NULL) {
	cupsICCQualifier3 = attr->value;
    } else {
	cupsICCQualifier3 = "Resolution";
    }
    if ((choice = ppdFindMarkedChoice(ppd,cupsICCQualifier3)) != NULL) {
	cupsICCQualifier3Choice = choice->choice;
    } else {
	cupsICCQualifier3Choice = NULL;
    }

    for (attr = ppdFindAttr(ppd,"cupsICCProfile",NULL);attr != NULL;
       attr = ppdFindNextAttr(ppd,"cupsICCProfile",NULL)) {
	// check color model
	char buf[PPD_MAX_NAME];
	char *p, *r;
	char *datadir;

	strncpy(buf,attr->spec,sizeof(buf));
	if ((p = strchr(buf,'.')) != NULL) {
	    *p = '\0';
	}
	if (colorModel != NULL && buf[0] != '\0'
	    && strcasecmp(buf,colorModel) != 0) continue;
	if (p == NULL) {
	    break;
	} else {
	    p++;
	    if ((r = strchr(p,'.')) != 0) {
		*r = '\0';
	    }
	}
	if (cupsICCQualifier2Choice != NULL && p[0] != '\0'
	    && strcasecmp(p,cupsICCQualifier2Choice) != 0) continue;
	if (r == NULL) {
	    break;
	} else {
	    r++;
	    if ((p = strchr(r,'.')) != 0) {
		*p = '\0';
	    }
	}
	if (cupsICCQualifier3Choice == NULL || r[0] == '\0'
	    || strcasecmp(r,cupsICCQualifier3Choice) == 0) break;
    }
    if (attr != NULL) {
	// matched
	path->clear();
	if (attr->value[0] != '/') {
	    if ((datadir = getenv("CUPS_DATADIR")) == NULL)
		datadir = CUPS_DATADIR;
	    path->append(datadir);
	    path->append("/profiles/");
	}
	path->append(attr->value);
	return gTrue;
    }
    return gFalse;
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
  GooString profilePath;
  char *profile = 0;
  const char *t = NULL;
  ppd_attr_t *attr;

  if (argc < 6 || argc > 7) {
    pdfError(-1,const_cast<char *>("%s job-id user title copies options [file]"),
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
    cupsMarkOptions(ppd,num_options,options);
    handleRqeuiresPageRegion();
    cupsRasterInterpretPPD(&header,ppd,num_options,options,0);
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

    if (profile != NULL)
      colorProfile = cmsOpenProfileFromFile(profile,"r");    

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
  unsigned char *bp;
  unsigned char *dp;
  unsigned int npadbits = (size*8)-pixels;

  if (npadbits == 0) {
    bp = src+size-1;
    dp = dst;
    for (unsigned int j = 0;j < size;j++,bp--,dp++) {
      *dp = revTable[(unsigned char)(~*bp)];
    }
  } else {
    unsigned int pd,d;
    unsigned int sw;

    size = (pixels+7)/8;
    sw = (size*8)-pixels;
    bp = src+size-1;
    dp = dst;

    pd = *bp--;
    for (unsigned int j = 1;j < size;j++,bp--,dp++) {
      d = *bp;
      *dp = ~revTable[(((d << 8) | pd) >> sw) & 0xff];
      pd = d;
    }
    *dp = ~revTable[(pd >> sw) & 0xff];
  }
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
  unsigned char *bp;
  unsigned char *dp;
  unsigned int npadbits = (size*8)-pixels;

  if (npadbits == 0) {
    bp = src+size-1;
    dp = dst;
    for (unsigned int j = 0;j < size;j++,bp--,dp++) {
      *dp = revTable[*bp];
    }
  } else {
    unsigned int pd,d;
    unsigned int sw;

    size = (pixels+7)/8;
    sw = (size*8)-pixels;
    bp = src+size-1;
    dp = dst;

    pd = *bp--;
    for (unsigned int j = 1;j < size;j++,bp--,dp++) {
      d = *bp;
      *dp = revTable[(((d << 8) | pd) >> sw) & 0xff];
      pd = d;
    }
    *dp = revTable[(pd >> sw) & 0xff];
  }
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

static unsigned char *RGB8toKCMYcm(unsigned char *src, unsigned char *pixelBuf,
  unsigned int x, unsigned int y)
{
  unsigned char cmyk[4];
  unsigned char c;
  unsigned char d;

  cupsImageRGBToCMYK(src,cmyk,1);
  c = 0;
  d = dither1[y & 0xf][x & 0xf];
  /* K */
  if (cmyk[3] > d) {
    c |= 0x20;
  }
  /* C */
  if (cmyk[0] > d) {
    c |= 0x10;
  }
  /* M */
  if (cmyk[1] > d) {
    c |= 0x08;
  }
  /* Y */
  if (cmyk[2] > d) {
    c |= 0x04;
  }
  if (c == 0x18) { /* Blue */
    c = 0x11; /* cyan + light magenta */
  } else if (c == 0x14) { /* Green */
    c = 0x06; /* light cyan + yellow */
  }
  *pixelBuf = c;
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

static unsigned char *convertBitsNoop(unsigned char *src, unsigned char *dst,
    unsigned int x, unsigned int y)
{
    return src;
}

static unsigned char *convert8to1(unsigned char *src, unsigned char *dst,
    unsigned int x, unsigned int y)
{
  unsigned char c = 0;
  /* assumed that max number of colors is 4 */
  for (unsigned int i = 0;i < header.cupsNumColors;i++) {
    c <<= 1;
    /* ordered dithering */
    if (src[i] > dither1[y & 0xf][x & 0xf]) {
      c |= 0x1;
    }
  }
  *dst = c;
  return dst;
}

static unsigned char *convert8to2(unsigned char *src, unsigned char *dst,
    unsigned int x, unsigned int y)
{
  unsigned char c = 0;
  /* assumed that max number of colors is 4 */
  for (unsigned int i = 0;i < header.cupsNumColors;i++) {
    unsigned int d;

    c <<= 2;
    /* ordered dithering */
    d = src[i] + dither2[y & 0x7][x & 0x7];
    if (d > 255) d = 255;
    c |= d >> 6;
  }
  *dst = c;
  return dst;
}

static unsigned char *convert8to4(unsigned char *src, unsigned char *dst,
    unsigned int x, unsigned int y)
{
  unsigned short c = 0;

  /* assumed that max number of colors is 4 */
  for (unsigned int i = 0;i < header.cupsNumColors;i++) {
    unsigned int d;

    c <<= 4;
    /* ordered dithering */
    d = src[i] + dither4[y & 0x3][x & 0x3];
    if (d > 255) d = 255;
    c |= d >> 4;
  }
  if (header.cupsNumColors < 3) {
    dst[0] = c;
  } else {
    dst[0] = c >> 8;
    dst[1] = c;
  }
  return dst;
}

static unsigned char *convert8to16(unsigned char *src, unsigned char *dst,
    unsigned int x, unsigned int y)
{
  /* assumed that max number of colors is 4 */
  for (unsigned int i = 0;i < header.cupsNumColors;i++) {
    dst[i*2] = src[i];
    dst[i*2+1] = src[i];
  }
  return dst;
}

static void writePixel1(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  switch (header.cupsNumColors) {
  case 1:
    {
      unsigned int bo = pixeli & 0x7;
      if ((pixeli & 7) == 0) dst[pixeli/8] = 0;
      dst[pixeli/8] |= *pixelBuf << (7-bo);
    }
    break;
  case 6:
    dst[pixeli] = *pixelBuf;
    break;
  case 3:
  case 4:
  default:
    {
      unsigned int qo = (pixeli & 0x1)*4;
      if ((pixeli & 1) == 0) dst[pixeli/2] = 0;
      dst[pixeli/2] |= *pixelBuf << (4-qo);
    }
    break;
  }
}

static void writePlanePixel1(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  unsigned int bo = pixeli & 0x7;
  unsigned char so = header.cupsNumColors - plane - 1;
  if ((pixeli & 7) == 0) dst[pixeli/8] = 0;
  dst[pixeli/8] |= ((*pixelBuf >> so) & 1) << (7-bo);
}

static void writePixel2(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  switch (header.cupsNumColors) {
  case 1:
    {
      unsigned int bo = (pixeli & 0x3)*2;
      if ((pixeli & 3) == 0) dst[pixeli/4] = 0;
      dst[pixeli/4] |= *pixelBuf << (6-bo);
    }
    break;
  case 3:
  case 4:
  default:
    dst[pixeli] = *pixelBuf;
    break;
  }
}

static void writePlanePixel2(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  unsigned int bo = (pixeli & 0x3)*2;
  unsigned char so = (header.cupsNumColors - plane - 1)*2;
  if ((pixeli & 3) == 0) dst[pixeli/4] = 0;
  dst[pixeli/4] |= ((*pixelBuf >> so) & 3) << (6-bo);
}

static void writePixel4(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  switch (header.cupsNumColors) {
  case 1:
    {
      unsigned int bo = (pixeli & 0x1)*4;
      if ((pixeli & 1) == 0) dst[pixeli/2] = 0;
      dst[pixeli/2] |= *pixelBuf << (4-bo);
    }
    break;
  case 3:
  case 4:
  default:
    dst[pixeli*2] = pixelBuf[0];
    dst[pixeli*2+1] = pixelBuf[1];
    break;
  }
}

static void writePlanePixel4(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  unsigned short c = (pixelBuf[0] << 8) | pixelBuf[1];
  unsigned int bo = (pixeli & 0x1)*4;
  unsigned char so = (header.cupsNumColors - plane - 1)*4;
  if ((pixeli & 1) == 0) dst[pixeli/2] = 0;
  dst[pixeli/2] |= ((c >> so) & 0xf) << (4-bo);
}

static void writePixel8(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  unsigned char *dp = dst + pixeli*header.cupsNumColors;
  for (unsigned int i = 0;i < header.cupsNumColors;i++) {
    dp[i] = pixelBuf[i];
  }
}

static void writePlanePixel8(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  dst[pixeli] = pixelBuf[plane];
}

static void writePixel16(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  unsigned char *dp = dst + pixeli*header.cupsNumColors*2;
  for (unsigned int i = 0;i < header.cupsNumColors*2;i++) {
    dp[i] = pixelBuf[i];
  }
}

static void writePlanePixel16(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf)
{
  dst[pixeli*2] = pixelBuf[plane*2];
  dst[pixeli*2+1] = pixelBuf[plane*2+1];
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
      pb = convertBits(pb,pixelBuf2,i,row);
      writePixel(dst,0,i,pb);
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
      pb = convertBits(pb,pixelBuf2,i,row);
      writePixel(dst,0,i,pb);
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
      pb = convertBits(pb,pixelBuf2,i,row);
      writePixel(dst,plane,i,pb);
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
      pb = convertBits(pb,pixelBuf2,i,row);
      writePixel(dst,plane,i,pb);
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
    convertBits = convertBitsNoop; /* convert bits in convertCSpace */
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
      pdfError(-1,const_cast<char *>("Can't create color transform"));
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
      pdfError(-1,const_cast<char *>("Specified ColorSpace is not supported"));
      exit(1);
      break;
    }
    /* select convertBits function */
    switch (header.cupsBitsPerColor) {
    case 2:
      convertBits = convert8to2;
      break;
    case 4:
      convertBits = convert8to4;
      break;
    case 16:
      convertBits = convert8to16;
      break;
    case 1:
      if (header.cupsNumColors == 1
          || header.cupsColorSpace == CUPS_CSPACE_KCMYcm) {
          convertBits = convertBitsNoop;
      } else {
          convertBits = convert8to1;
      }
      break;
    case 8:
    default:
      convertBits = convertBitsNoop;
      break;
    }
  }
  /* select writePixel function */
  switch (header.cupsBitsPerColor) {
  case 2:
    if (header.cupsColorOrder == CUPS_ORDER_CHUNKED
        || header.cupsNumColors == 1) {
      writePixel = writePixel2;
    } else {
      writePixel = writePlanePixel2;
    }
    break;
  case 4:
    if (header.cupsColorOrder == CUPS_ORDER_CHUNKED
        || header.cupsNumColors == 1) {
      writePixel = writePixel4;
    } else {
      writePixel = writePlanePixel4;
    }
    break;
  case 16:
    if (header.cupsColorOrder == CUPS_ORDER_CHUNKED
        || header.cupsNumColors == 1) {
      writePixel = writePixel16;
    } else {
      writePixel = writePlanePixel16;
    }
    break;
  case 1:
    if (header.cupsColorOrder == CUPS_ORDER_CHUNKED
        || header.cupsNumColors == 1) {
      writePixel = writePixel1;
    } else {
      writePixel = writePlanePixel1;
    }
    break;
  case 8:
  default:
    if (header.cupsColorOrder == CUPS_ORDER_CHUNKED
        || header.cupsNumColors == 1) {
      writePixel = writePixel8;
    } else {
      writePixel = writePlanePixel8;
    }
    break;
  }
}

static void writePageImage(cups_raster_t *raster, SplashBitmap *bitmap,
  int pageNo)
{
  ConvertLineFunc convertLine;
  unsigned char *lineBuf = NULL;
  unsigned char *dp;
  unsigned int rowsize = bitmap->getRowSize();

  if (allocLineBuf) lineBuf = new unsigned char [bytesPerLine];
  if ((pageNo & 1) == 0) {
    convertLine = convertLineEven;
  } else {
    convertLine = convertLineOdd;
  }
  if (header.Duplex && (pageNo & 1) == 0 && swap_image_y) {
    for (unsigned int plane = 0;plane < nplanes;plane++) {
      unsigned char *bp = (unsigned char *)(bitmap->getDataPtr());

      bp += rowsize * (bitmapoffset[1] + header.cupsHeight - 1) +
        popplerBitsPerPixel * bitmapoffset[0] / 8;
      for (unsigned int h = header.cupsHeight;h > 0;h--) {
        for (unsigned int band = 0;band < nbands;band++) {
          dp = convertLine(bp,lineBuf,h,plane+band,header.cupsWidth,
                 bytesPerLine);
          cupsRasterWritePixels(raster,dp,bytesPerLine);
        }
        bp -= rowsize;
      }
    }
  } else {
    for (unsigned int plane = 0;plane < nplanes;plane++) {
      unsigned char *bp = (unsigned char *)(bitmap->getDataPtr());

      bp += rowsize * bitmapoffset[1] + 
        popplerBitsPerPixel * bitmapoffset[0] / 8;
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
  if (allocLineBuf) delete[] lineBuf;
}

static void outPage(PDFDoc *doc, Catalog *catalog, int pageNo,
  SplashOutputDev *out, cups_raster_t *raster)
{
  SplashBitmap *bitmap;
  Page *page = catalog->getPage(pageNo);
  PDFRectangle *mediaBox = page->getMediaBox();
  int rotate = page->getRotate();
  double paperdimensions[2], /* Physical size of the paper */
    margins[4];	/* Physical margins of print */
  ppd_size_t *size;		/* Page size */
  double l, swap;
  int i;
  bool landscape = 0;

  fprintf(stderr, "DEBUG: mediaBox = [ %f %f %f %f ]; rotate = %d\n",
	  mediaBox->x1, mediaBox->y1, mediaBox->x2, mediaBox->y2, rotate);
  l = mediaBox->x2 - mediaBox->x1;
  if (l < 0) l = -l;
  if (rotate == 90 || rotate == 270)
    header.PageSize[1] = (unsigned)l;
  else
    header.PageSize[0] = (unsigned)l;
  l = mediaBox->y2 - mediaBox->y1;
  if (l < 0) l = -l;
  if (rotate == 90 || rotate == 270)
    header.PageSize[0] = (unsigned)l;
  else
    header.PageSize[1] = (unsigned)l;

  memset(paperdimensions, 0, sizeof(paperdimensions));
  memset(margins, 0, sizeof(margins));
  if (ppd) {
    for (i = ppd->num_sizes, size = ppd->sizes;
	 i > 0;
	 i --, size ++) {
      /* Skip page sizes which conflict with settings of the other options */
      /* TODO XXX */
      /* Find size of document's page under the PPD page sizes */
      if (fabs(header.PageSize[1] - size->length) < 5.0 &&
	  fabs(header.PageSize[0] - size->width) < 5.0)
	break;
    }
    if (i > 0) {
      /*
       * Standard size...
       */
      fprintf(stderr, "DEBUG: size = %s\n", size->name);
      landscape = 0;
      paperdimensions[0] = size->width;
      paperdimensions[1] = size->length;
      if (pwgraster == 0) {
	margins[0] = size->left;
	margins[1] = size->bottom;
	margins[2] = size->width - size->right;
	margins[3] = size->length - size->top;
      }
      strncpy(header.cupsPageSizeName, size->name, 64);
    } else {
      /*
       * No matching portrait size; look for a matching size in
       * landscape orientation...
       */

      for (i = ppd->num_sizes, size = ppd->sizes;
	   i > 0;
	   i --, size ++)
	if (fabs(header.PageSize[0] - size->length) < 5.0 &&
	    fabs(header.PageSize[1] - size->width) < 5.0)
	  break;

      if (i > 0) {
	/*
	 * Standard size in landscape orientation...
	 */
	fprintf(stderr, "DEBUG: landscape size = %s\n", size->name);
	landscape = 1;
	paperdimensions[0] = size->width;
	paperdimensions[1] = size->length;
	if (pwgraster == 0) {
	  margins[0] = size->left;
	  margins[1] = size->bottom;
	  margins[2] = size->width - size->right;
	  margins[3] = size->length - size->top;
	}
	strncpy(header.cupsPageSizeName, size->name, 64);
      } else {
	/*
	 * Custom size...
	 */
	fprintf(stderr, "DEBUG: size = Custom\n");
	landscape = 0;
	paperdimensions[1] = size->length;
	for (i = 0; i < 2; i ++)
	  paperdimensions[i] = header.PageSize[i];
	if (pwgraster == 0)
	  for (i = 0; i < 4; i ++)
	    margins[i] = ppd->custom_margins[i];
	header.cupsPageSizeName[0] = '\0';
      }
    }
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

  doc->displayPage(out,pageNo,header.HWResolution[0],
		   header.HWResolution[1],(landscape == 0 ? 0 : 90),
		   gTrue,gTrue,gTrue);
  bitmap = out->getBitmap();
  bitmapoffset[0] = margins[0] / 72.0 * header.HWResolution[0];
  bitmapoffset[1] = margins[3] / 72.0 * header.HWResolution[1];

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
      pdfError(-1,const_cast<char *>("Can't write page %d header"),pageNo);
      exit(1);
  }

  /* write page image */
  writePageImage(raster,bitmap,pageNo);
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
    pdfError(-1,const_cast<char *>("Specified ColorSpace is not supported"));
    exit(1);
    break;
  }
  if (popplerColorProfile != NULL) {
    GfxColorSpace::setDisplayProfile(popplerColorProfile);
  }
}

int main(int argc, char *argv[]) {
  PDFDoc *doc;
  SplashOutputDev *out;
  SplashColor paperColor;
  int i;
  int npages;
  cups_raster_t *raster;
  enum SplashColorMode cmode;
  int rowpad;
  Catalog *catalog;

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  setErrorCallback(::myErrorFun,NULL);
#else
  setErrorFunction(::myErrorFun);
#endif
  cmsSetLogErrorHandler(lcmsErrorHandler);
  globalParams = new GlobalParams();
  parseOpts(argc, argv);

  if (argc == 6) {
    /* stdin */
    int fd;
    char name[BUFSIZ];
    char buf[BUFSIZ];
    int n;

    fd = cupsTempFd(name,sizeof(name));
    if (fd < 0) {
      pdfError(-1,const_cast<char *>("Can't create temporary file"));
      exit(1);
    }

    /* copy stdin to the tmp file */
    while ((n = read(0,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
        pdfError(-1,const_cast<char *>("Can't copy stdin to temporary file"));
        close(fd);
	exit(1);
      }
    }
    close(fd);
    doc = new PDFDoc(new GooString(name));
    /* remove name */
    unlink(name);
  } else {
    GooString *fileName = new GooString(argv[6]);
    /* argc == 7 filenmae is specified */
    FILE *fp;

    if ((fp = fopen(argv[6],"rb")) == 0) {
        pdfError(-1,const_cast<char *>("Can't open input file %s"),argv[6]);
	exit(1);
    }
    parsePDFTOPDFComment(fp);
    fclose(fp);
    doc = new PDFDoc(fileName,NULL,NULL);
  }

  if (!doc->isOk()) {
    exitCode = 1;
    goto err1;
  }

  catalog = doc->getCatalog();
  npages = doc->getNumPages();

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
    pdfError(-1,const_cast<char *>("Specified color format is not supported"));
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
      pdfError(-1,const_cast<char *>("Specified color format is not supported"));
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
    cmode = splashModeRGB8;
    rowpad = 4;
    /* set paper color white */
    paperColor[0] = 255;
    paperColor[1] = 255;
    paperColor[2] = 255;
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
      cmode = splashModeMono1;
      popplerBitsPerPixel = 1;
    } else {
      cmode = splashModeMono8;
      popplerBitsPerPixel = 8;
    }
    /* set paper color white */
    paperColor[0] = 255;
    rowpad = 1;
    popplerNumColors = 1;
    break;
  default:
    pdfError(-1,const_cast<char *>("Specified ColorSpace is not supported"));
    exit(1);
    break;
  }

  if (!cm_disabled) {
    setPopplerColorProfile();
  }

  out = new SplashOutputDev(cmode,rowpad/* row padding */,
    gFalse,paperColor,gTrue
#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 30
    ,gFalse
#endif
    );
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  out->startDoc(doc);
#else
  out->startDoc(doc->getXRef());
#endif

  if ((raster = cupsRasterOpen(1, pwgraster ? CUPS_RASTER_WRITE_PWG :
			       CUPS_RASTER_WRITE)) == 0) {
        pdfError(-1,const_cast<char *>("Can't open raster stream"));
	exit(1);
  }
  selectConvertFunc(raster);
  for (i = 1;i <= npages;i++) {
    outPage(doc,catalog,i,out,raster);
  }
  cupsRasterClose(raster);

  delete out;
err1:
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

  // Check for memory leaks
  Object::memCheck(stderr);
  gMemReport(stderr);

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
  return gmalloc(size);
}

void operator delete(void *p) throw ()
{
  gfree(p);
}

void * operator new[](size_t size) _GLIBCXX_THROW (std::bad_alloc)
{
  return gmalloc(size);
}

void operator delete[](void *p) throw ()
{
  gfree(p);
}

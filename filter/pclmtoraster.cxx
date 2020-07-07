/**
 * This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief Decode PCLm to a Raster file
 * @file pclmtoraster.cxx
 * @author Vikrant Malik <vikrantmalik051@gmail.com> (c) 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <cups/raster.h>
#include <cups/cups.h>
#include <ppd/ppd.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <cupsfilters/raster.h>
#include <cupsfilters/image.h>

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif
#define MAX_CHECK_COMMENT_LINES	20
#define MAX_BYTES_PER_PIXEL 32

namespace {
  typedef unsigned char *(*ConvertCSpace)(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels);
  ConvertCSpace convertcspace;
  int pwgraster = 0;
  cups_page_header2_t header;
  ppd_file_t *ppd = 0;
  char pageSizeRequested[64];
  /* image swapping */
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
}

int parse_doc_type(FILE *fp)
{
  char line1[5];
  char *rc;

  /* get the first few bytes of the file */
  rewind(fp);
  rc = fgets(line1,sizeof(line1),fp);
  /* empty input */
  if (rc == NULL)
    return 1;

  /* is PDF/PCLm */
  if (strncmp(line1,"%PDF",4) == 0 || strncmp(line1,"%PCLm",4) == 0)
    return 0;

  fprintf(stderr,"DEBUG: input file is not of PCLm format\n");
  exit(EXIT_FAILURE);
}

static void parseOpts(int argc, char **argv)
{
  int           num_options = 0;
  cups_option_t*options = NULL;
//   char*         profile = 0;
  const char*   t = NULL;
  ppd_attr_t*   attr;

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
  //Parse IPP options from command lines
  num_options = cupsParseOptions(argv[5],0,&options);
  if (ppd) {
    ppdMarkDefaults(ppd);
    ppdMarkOptions(ppd,num_options,options);
//     handleRqeuiresPageRegion();
    ppdRasterInterpretPPD(&header,ppd,num_options,options,0);
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
  strncpy(pageSizeRequested, header.cupsPageSizeName, 64);
  fprintf(stderr, "DEBUG: Page size requested: %s\n", header.cupsPageSizeName);
}

static bool dict_lookup_rect(QPDFObjectHandle object, std::string const& key, float rect[4])
{
  // preliminary checks
  if (!object.isDictionary() || !object.hasKey(key))
    return false;

  // check if the key is array or some other type
  QPDFObjectHandle value = object.getKey(key);
  if (!value.isArray())
    return false;
  
  // get values in a vector and assign it to rect
  std::vector<QPDFObjectHandle> array = value.getArrayAsVector();
  for (int i = 0; i < 4; ++i) {
    // if the value in the array is not real, we have an invalid array 
    if (!array[i].isReal() && !array[i].isInteger())
      return false;
    
    rect[i] = array[i].getNumericValue();
  }

  return array.size() == 4;
}

static unsigned char *rotatebitmap(unsigned char *src, unsigned char *dst,
     unsigned int rotate, unsigned int height, unsigned int width, int rowsize, std::string colorspace)
{
  unsigned char *bp = src;
  unsigned char *dp = dst;
  unsigned char *temp = dst;

  if (rotate == 0) {
    return src;
  } else if (rotate == 180) {
    if (colorspace == "/DeviceGray") {
      bp = src + height * rowsize - 1;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        for (unsigned int w = 0; w < width; w++, bp --, dp ++) {
          *dp = *bp;
        }
      }
    } else if (colorspace == "/DeviceCMYK") {
      bp = src + height * rowsize - 4;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        for (unsigned int w = 0; w < width; w++, bp -= 4, dp += 4) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
    } else if (colorspace == "/DeviceRGB") {
      bp = src + height * rowsize - 3;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        for (unsigned int w = 0; w < width; w++, bp -= 3, dp += 3) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
    }
  }
  else if (rotate == 270) {
    if (colorspace == "/DeviceGray") {
      bp = src;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (height - h) - 1;
        for (unsigned int w = 0; w < width; w++, bp += height , dp ++) {
          *dp = *bp;
        }
      }
    } else if (colorspace == "/DeviceCMYK") {
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (height - h)*4 - 4;
        for (unsigned int i = 0; i < width; i++, bp += height*4 , dp += 4) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
    } else if (colorspace == "/DeviceRGB") {
      bp = src;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (height - h)*3 - 3;
        for (unsigned int i = 0; i < width; i++, bp += height*3 , dp += 3) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
    }
  }
  else if (rotate == 90) {
    if (colorspace == "/DeviceGray") {
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (width - 1) * height + h;
        for (unsigned int i = 0; i < width; i++, bp -= height , dp ++) {
          *dp = *bp;
        }
      }
    } else if (colorspace == "/DeviceCMYK") {
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (width - 1) * height * 4 + 4*h;
        for (unsigned int i = 0; i < width; i++, bp -= height*4 , dp += 4) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
    } else if (colorspace == "/DeviceRGB") {
      for (unsigned int h = 0; h < height; h++) {
       bp = src + (width - 1) * height * 3 + 3*h;
        for (unsigned int i = 0; i < width; i++, bp -= height*3 , dp += 3) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
    }
  }
  else {
    fprintf(stderr, "ERROR: Incorrect Rotate Value %d\n", rotate);
    exit(1);
  }

  return temp;
}

void onebitpixel(unsigned char *src, unsigned char *dst, unsigned int width,
     unsigned int height, unsigned int row, unsigned int rowsize) {
  unsigned char t = 0;
  for(unsigned int w = 0; w < width; w+=8){
    t = 0;
    for(int k = 0; k < 8; k++){
        t <<= 1;
        if(*src > dither1[row & 0xf][(w+k) & 0xf]){
          t |= 0x1;
        }
        src +=1;
    }
    *dst = t;
    dst += 1;
  }
}

static unsigned char *RGBtoCMYKLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageRGBToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *RGBtoCMYLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *RGBtoWhiteLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageRGBToWhite(src,dst,pixels);
  return dst;
}

static unsigned char *RGBtoBlackLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  if (header.cupsBitsPerColor != 1) {
    cupsImageRGBToBlack(src,dst,pixels);
  } else {
    cupsImageRGBToBlack(src,src,pixels);
    onebitpixel(src, dst, header.cupsWidth, header.cupsHeight, row, pixels);
  }
  return dst;
}

static unsigned char *CMYKtoRGBLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageCMYKToRGB(src,dst,pixels);
  return dst;
}

static unsigned char *CMYKtoCMYLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  // Converted first to rgb and then to cmy for better outputs.
  cupsImageCMYKToRGB(src,src,pixels);
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *CMYKtoWhiteLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageCMYKToWhite(src,dst,pixels);
  return dst;
}

static unsigned char *CMYKtoBlackLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  if (header.cupsBitsPerColor != 1) {
    cupsImageCMYKToBlack(src,dst,pixels);
  } else {
    cupsImageCMYKToWhite(src,src,pixels);
    onebitpixel(src, dst, header.cupsWidth, header.cupsHeight, row, pixels);
  }
  return dst;
}

static unsigned char *GraytoRGBLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageWhiteToRGB(src,dst,pixels);
  return dst;
}

static unsigned char *GraytoCMYKLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageWhiteToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *GraytoCMYLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  cupsImageWhiteToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *GraytoBlackLine(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
  if (header.cupsBitsPerColor != 1) {
    cupsImageWhiteToBlack(src, dst, pixels);
  } else {
    onebitpixel(src, dst, header.cupsWidth, header.cupsHeight, row, pixels);
  }
  return dst;
}

static unsigned char *convertcspaceNoop(unsigned char *src, unsigned char *dst, unsigned int row, unsigned int pixels)
{
    return src;
}

static unsigned char *convertLine(unsigned char *src, unsigned char *dst,
     unsigned int row, unsigned int plane, unsigned int pixels)
{
  /* Assumed that BitsPerColor is 8 */
  unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
  unsigned char *pb;

  switch (header.cupsColorOrder)
  {
  case CUPS_ORDER_BANDED:
  case CUPS_ORDER_PLANAR:
   pb = convertcspace(src, pixelBuf1, row, pixels);
   for (unsigned int i = 0; i < pixels; i++, pb += header.cupsNumColors)
     dst[i] = pb[plane];
   break;
  case CUPS_ORDER_CHUNKED:
  default:
    pb = convertcspace(src, dst, row, pixels);
    dst = pb;
   break;
  }
  return dst;
}

static void outPage(cups_raster_t *raster, QPDFObjectHandle page, int pgno) {
  long long        rotate = 0,
                   height,
                   width;
  double           l;
  QPDFObjectHandle image;
  QPDFObjectHandle imgdict;
  int              rowsize = 0, bufsize = 0, pixel_count = 0, temp;
  float            mediaBox[4];
  unsigned char    *bitmap = NULL;
  unsigned char    *colordata = NULL;
  QPDFObjectHandle colorspace_obj;
  unsigned char    *lineBuf = NULL;
  unsigned char    *dp = NULL;
  std::string      colorspace;


  convertcspace = convertcspaceNoop;

  if (page.getKey("/Rotate").isInteger())
    rotate = page.getKey("/Rotate").getIntValueAsInt();

  if (!dict_lookup_rect(page, "/MediaBox", mediaBox)){
    fprintf(stderr, "ERROR: pdf page %d doesn't contain a valid mediaBox\n", pgno + 1);
    return;
  } else {
    fprintf(stderr, "DEBUG: mediaBox = [%f %f %f %f];\n", mediaBox[0], mediaBox[1], mediaBox[2], mediaBox[3]);
    l = mediaBox[2] - mediaBox[0];
    if (l < 0) l = -l;
    if (rotate == 90 || rotate == 270)
      header.PageSize[1] = (unsigned)l;
    else
      header.PageSize[0] = (unsigned)l;
    l = mediaBox[3] - mediaBox[1];
    if (l < 0) l = -l;
    if (rotate == 90 || rotate == 270)
      header.PageSize[0] = (unsigned)l;
    else
      header.PageSize[1] = (unsigned)l;
  }

  header.cupsWidth = 0;
  header.cupsHeight = 0;

  std::map<std::string, QPDFObjectHandle> images = page.getPageImages();
  for (auto const& iter2: images) {
    image = iter2.second;
    imgdict = image.getDict();

    PointerHolder<Buffer> actual_data = image.getStreamData(qpdf_dl_all);
    width = imgdict.getKey("/Width").getIntValue();
    height = imgdict.getKey("/Height").getIntValue();
    colorspace_obj = imgdict.getKey("/ColorSpace");
    header.cupsHeight += height;
    bufsize = actual_data->getSize();

    if(!pixel_count) {
      bitmap = (unsigned char *) malloc(bufsize);
    }
    else {
      bitmap = (unsigned char *) realloc(bitmap, pixel_count + bufsize);
    }
    memcpy(bitmap + pixel_count, actual_data->getBuffer(), bufsize);
    pixel_count += bufsize;

    if (width > header.cupsWidth) header.cupsWidth = width;
  }

  if (rotate == 270 || rotate == 90) {
    temp = header.cupsHeight;
    header.cupsHeight = header.cupsWidth;
    header.cupsWidth = temp;
  }

  bytesPerLine = header.cupsBytesPerLine = (header.cupsBitsPerPixel * header.cupsWidth + 7) / 8;
  if (header.cupsColorOrder == CUPS_ORDER_BANDED) {
    header.cupsBytesPerLine *= header.cupsNumColors;
  }

  if (!cupsRasterWriteHeader2(raster,&header)) {
    fprintf(stderr, "ERROR: Can't write page %d header\n", pgno + 1);
    exit(1);
  }

  colorspace = (colorspace_obj.isName() ? colorspace_obj.getName() : std::string());

  if (colorspace == "/DeviceRGB") {
    rowsize = header.cupsWidth*3;
  } else if (colorspace == "/DeviceCMYK") {
     rowsize = header.cupsWidth*4;
  } else if (colorspace == "/DeviceGray") {
     rowsize = header.cupsWidth;
  } else {
    fprintf(stderr, "ERROR: Colorspace %s not supported\n", colorspace.c_str());
    exit(1);
  }

  if(rotate) {
    unsigned char *bitmap2 = (unsigned char *) malloc(pixel_count);
    bitmap2 = rotatebitmap(bitmap, bitmap2, rotate, header.cupsHeight, header.cupsWidth, rowsize, colorspace);
    free(bitmap);
    bitmap = bitmap2;
  }

  colordata = bitmap;

  switch (header.cupsColorSpace) {
    case CUPS_CSPACE_K:
     if (colorspace == "/DeviceRGB") convertcspace = RGBtoBlackLine;
     else if (colorspace == "/DeviceCMYK") convertcspace = CMYKtoBlackLine;
     else if (colorspace == "/DeviceGray") convertcspace = GraytoBlackLine;
     break;
    case CUPS_CSPACE_SW:
     if (colorspace == "/DeviceRGB") convertcspace = RGBtoWhiteLine;
     else if (colorspace == "/DeviceCMYK") convertcspace = CMYKtoWhiteLine;
     break;
    case CUPS_CSPACE_CMY:
     if (colorspace == "/DeviceRGB") convertcspace = RGBtoCMYLine;
     else if (colorspace == "/DeviceCMYK") convertcspace = CMYKtoCMYLine;
     else if (colorspace == "/DeviceGray") convertcspace = GraytoCMYLine;
     break;
    case CUPS_CSPACE_CMYK:
     if (colorspace == "/DeviceRGB") convertcspace = RGBtoCMYKLine;
     else if (colorspace == "/DeviceGray") convertcspace = GraytoCMYKLine;
     break;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_ADOBERGB:
    case CUPS_CSPACE_SRGB:
    default:
     if (colorspace == "/DeviceCMYK") convertcspace = CMYKtoRGBLine;
     else if (colorspace == "/DeviceGray") convertcspace = GraytoRGBLine;
     break;
   }

  lineBuf = new unsigned char [bytesPerLine];
  for (unsigned int plane = 0; plane < nplanes ; plane++) {
    unsigned char *bp = colordata;
    for (unsigned int h = 0; h < header.cupsHeight; h++) {
      for (unsigned int band = 0; band < nbands; band++) {
       dp = convertLine(bp, lineBuf, h, plane + band, header.cupsWidth);
       cupsRasterWritePixels(raster, dp, bytesPerLine);
      }
    bp += rowsize;
    }
  }

  delete[] lineBuf;
  free(bitmap);
}

int main(int argc, char **argv)
{
  int npages=0;
  QPDF *pdf;
  FILE *fp = NULL;
  cups_raster_t *raster;


  if (argc != 7) {
    fprintf(stderr, "ERROR: Usage: %s job-id user title copies options [file]\n",
            argv[0]);
    exit(1);
  }
  parseOpts(argc, argv);

    if ((fp = fopen(argv[6],"rb")) == 0) {
        fprintf(stderr, "ERROR: Can't open input file %s\n",argv[6]);
        exit(1);
    }

  if(parse_doc_type(fp)) {
     fclose(fp);
     exit(1);
  }
  fclose(fp);

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

  if ((raster = cupsRasterOpen(1, pwgraster ? CUPS_RASTER_WRITE_PWG :
                               CUPS_RASTER_WRITE)) == 0) {
        fprintf(stderr, "ERROR: Can't open raster stream\n");
        exit(1);
  }

  pdf = new QPDF();
  pdf->processFile(argv[6]);
  std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
  npages = pages.size();

  for (int i = 0;
       i < npages; ++i) {
       fprintf(stderr, "INFO: Starting page %d.\n", i + 1);
       outPage(raster, pages[i], i);
    }

  cupsRasterClose(raster);
  delete pdf;
  if (ppd != NULL)  ppdClose(ppd);
  return 0;
}

void operator delete[](void *p) throw ()
{
  free(p);
}

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
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits>
#include <errno.h>
#include <cups/raster.h>
#include <cupsfilters/image.h>
#include <ppd/ppd.h>
#include <arpa/inet.h>   // ntohl

#include <vector>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <cupsfilters/raster.h>


#include <qpdf/Pl_Flate.hh>
#include <qpdf/Pl_DCT.hh>
#include <qpdf/Pl_Buffer.hh>

#include "unirast.h"

#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif
#define MAX_CHECK_COMMENT_LINES	20
#define MAX_BYTES_PER_PIXEL 32

namespace {
  int deviceCopies = 1;
  bool deviceCollate = false;
  int pwgraster = 1;
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

}

static void
parsePDFTOPDFComment(FILE *fp)
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

int
parse_doc_type(FILE *fp)
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

static void
parseOpts(int argc, char **argv)
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
    cupsMarkOptions(ppd,num_options,options);
//     handleRqeuiresPageRegion();
    cupsRasterInterpretPPD(&header,ppd,num_options,options,0);
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

static bool dict_lookup_rect(QPDFObjectHandle object,
                             std::string const& key,
                             float rect[4])
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
     unsigned int rotate, unsigned int height, unsigned int width, int rowsize)
{
  unsigned char *bp = src;
  unsigned char *dp = dst;
  unsigned char *temp = dst;

  if (rotate == 180) {
    switch (header.cupsColorSpace) {
     case CUPS_CSPACE_SW:
      bp = src + height * rowsize - 1;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        for (unsigned int w = 0; w < width; w++, bp --, dp ++) {
          *dp = *bp;
        }
      }
      break;
     case CUPS_CSPACE_CMYK:
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
      break;
     case CUPS_CSPACE_RGB:
     case CUPS_CSPACE_ADOBERGB:
     case CUPS_CSPACE_SRGB:
     default:
      bp = src + height * rowsize - 3;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        for (unsigned int w = 0; w < width; w++, bp -= 3, dp += 3) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
      break;
    }
  }
  else if (rotate == 270) {
    switch (header.cupsColorSpace) {
     case CUPS_CSPACE_SW:
      bp = src;
      dp = dst;
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (height - h) - 1;
        for (unsigned int w = 0; w < width; w++, bp += height , dp ++) {
          *dp = *bp;
        }
      }
      break;
     case CUPS_CSPACE_CMYK:
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (height - h)*4 - 4;
        for (unsigned int i = 0; i < width; i++, bp += height*4 , dp += 4) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
      break;
     case CUPS_CSPACE_RGB:
     case CUPS_CSPACE_ADOBERGB:
     case CUPS_CSPACE_SRGB:
     default:
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
      break;
    }
  }
  else if (rotate == 90) {
    switch (header.cupsColorSpace) {
    case CUPS_CSPACE_SW:
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (width - 1) * height + h;
        for (unsigned int i = 0; i < width; i++, bp -= height , dp ++) {
          *dp = *bp;
        }
      }
      break;
    case CUPS_CSPACE_CMYK:
      for (unsigned int h = 0; h < height; h++) {
        bp = src + (width - 1) * height * 4 + 4*h;
        for (unsigned int i = 0; i < width; i++, bp -= height*4 , dp += 4) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
      break;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_ADOBERGB:
    case CUPS_CSPACE_SRGB:
    default:
      for (unsigned int h = 0; h < height; h++) {
       bp = src + (width - 1) * height * 3 + 3*h;
        for (unsigned int i = 0; i < width; i++, bp -= height*3 , dp += 3) {
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
      break;
    }
  }
  else {
    fprintf(stderr, "ERROR: Incorrect Rotate Value %d\n", rotate);
    exit(1);
  }

  return temp;
}

static unsigned char *onebitpixel(unsigned char *src, unsigned char *dst, unsigned int width,
     unsigned int height, unsigned int rotate, unsigned int rowsize) {
  unsigned char *bp = src;
  unsigned char *dp = dst;
  unsigned char *temp = dst;
  if (rotate == 0){
    for(unsigned int h = 0; h < height; h++){
      for(unsigned int w = 0; w < width; w+=8){
        *dp = 0;
        for(int k=0;k<8;k++){
            *dp <<= 1;
            if(*bp > dither1[h & 0xf][(w+k) & 0xf]){
              *dp |= 0x1;
            }
            bp +=1;
        }
        dp+=1;
      }
    }
  }
  else if (rotate == 180) {
    dp = src;
    for(unsigned int h = 0; h < height; h++){
      for(unsigned int w = 0; w < width; w+=8){
        *dp = 0;
        for(int k=0;k<8;k++){
            *dp <<= 1;
            if(*bp > dither1[h & 0xf][(w+k) & 0xf]){
              *dp |= 0x1;
            }
            bp +=1;
        }
        dp+=1;
      }
    }
    bp = src + height * rowsize - 1;
    dp = dst;
    for (unsigned int h = 0; h < height; h++) {
      for (unsigned int w = 0; w < rowsize; w++, bp --, dp ++) {
        *dp = ~revTable[(unsigned char)(~*bp)];
      }
    }
    return dst;
  }
  else if (rotate == 270) {
    for (unsigned int h = 0; h < height; h++) {
      bp = src + (height - h) - 1;
      for (unsigned int w = 0; w < width; w+=8) {
        *dst=0;
        for (int k = 0; k < 8; k++) {
          *dst <<=1;
          if(*bp > dither1[h & 0xf][(w+k) & 0xf]){
            *dst |= 0x1;
          }
          bp += height;
        }
      dst+=1;
      }
    }
  }
  else if (rotate == 90) {
    for (unsigned int h = 0; h < height; h++) {
      bp = src + (width - 1) * height + h;
      for (unsigned int w = 0; w < width; w+=8) {
        *dst=0;
        for (int k = 0; k < 8; k++) {
          *dst <<=1;
          if(*bp > dither1[h & 0xf][(w+k) & 0xf]){
            *dst |= 0x1;
          }
          bp -= height;
        }
      dst+=1;
      }
    }
  }
  return temp;
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
  unsigned char    *bitmap2 = NULL;
  unsigned char    *graydata = NULL;
  unsigned char    *colordata = NULL;
  unsigned char    *onebitdata = NULL;


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
  if (!cupsRasterWriteHeader2(raster,&header)) {
    fprintf(stderr, "ERROR: Can't write page %d header\n", pgno + 1);
    exit(1);
  }

  switch (header.cupsColorSpace) {
   case CUPS_CSPACE_K:
   case CUPS_CSPACE_SW:
    if (header.cupsBitsPerColor == 1) {
      onebitdata=(unsigned char *)malloc(sizeof(char)*header.cupsWidth*header.cupsHeight);
      rowsize=bytesPerLine;
      onebitpixel(bitmap, onebitdata, header.cupsWidth, header.cupsHeight, rotate, rowsize);
      colordata=onebitdata;
    }
    else {
      graydata=(unsigned char *)malloc(sizeof(char)*header.cupsWidth*header.cupsHeight);
      cupsImageRGBToWhite(bitmap,graydata,header.cupsWidth*header.cupsHeight);
      rowsize = header.cupsWidth;
      colordata = graydata;
      if (rotate) {
        bitmap2 = (unsigned char *) malloc(pixel_count);
        bitmap2 = rotatebitmap(graydata, bitmap2, rotate, header.cupsHeight, header.cupsWidth, rowsize);
        free(bitmap);
        bitmap = bitmap2;
        colordata = bitmap;
      }
    }
    break;

   case CUPS_CSPACE_CMYK:
    rowsize = header.cupsWidth*4;
    if (rotate) {
        bitmap2 = (unsigned char *) malloc(pixel_count);
        bitmap2 = rotatebitmap(bitmap, bitmap2, rotate, header.cupsHeight, header.cupsWidth, rowsize);
        free(bitmap);
        bitmap = bitmap2;
    }
    colordata = bitmap;
    break;
   case CUPS_CSPACE_RGB:
   case CUPS_CSPACE_ADOBERGB:
   case CUPS_CSPACE_SRGB:
   default:
    rowsize = header.cupsWidth*3;
    if (rotate) {
        bitmap2 = (unsigned char *) malloc(pixel_count);
        bitmap2 = rotatebitmap(bitmap, bitmap2, rotate, header.cupsHeight, header.cupsWidth, rowsize);
        free(bitmap);
        bitmap = bitmap2;
    }
    colordata = bitmap;

  }

  for (unsigned int plane = 0; plane < nplanes ; plane++) {
    unsigned char *bp = colordata;
    for (unsigned int h = 0; h < header.cupsHeight; h++) {
      for (unsigned int band = 0; band < nbands; band++) {
        cupsRasterWritePixels(raster, bp, bytesPerLine);
      }
    bp += rowsize;
    }
  }

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
  parsePDFTOPDFComment(fp);
  fclose(fp);

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

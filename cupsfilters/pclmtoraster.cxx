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
 * @brief pclmtoraster filter function
 * @file pclmtoraster.cxx
 * @author Vikrant Malik <vikrantmalik051@gmail.com> (c) 2020
 */

/*
 * Include necessary headers...
 */

#include "filter.h"
#include <cups/raster.h>
#include <cups/cups.h>
#include <ppd/ppd.h>
#include <errno.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include "image.h"
#include "bitmap.h"
#include "raster.h"
#include <cupsfilters/filter.h>


#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif
#define MAX_BYTES_PER_PIXEL 32

typedef struct pclmtoraster_data_s
{
  int pwgraster = 0;
  int numcolors = 0;
  int rowsize = 0;
  cups_page_header2_t header;
  ppd_file_t *ppd = 0;
  char pageSizeRequested[64];
  int bi_level = 0;
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
  std::string colorspace; /* Colorspace of raster data */
} pclmtoraster_data_t;

typedef unsigned char *(*ConvertCSpace)(unsigned char *src, unsigned char *dst,
					unsigned int row,
					unsigned int pixels,
					pclmtoraster_data_t *data);
typedef unsigned char *(*ConvertLine)  (unsigned char *src, unsigned char *dst,
					unsigned char *buf,
					unsigned int row, unsigned int plane,
					pclmtoraster_data_t *data,
					ConvertCSpace convertcspace);

typedef struct conversion_function_s
{
  ConvertCSpace convertcspace;	/* Function for conversion of colorspaces */
  ConvertLine convertline;	/* Function tom modify raster data of a line */
} conversion_function_t;

static int
parseOpts(filter_data_t *data,
	  pclmtoraster_data_t *pclmtoraster_data)
{
  int			num_options = 0;
  cups_option_t*	options = NULL;
  const char*		t = NULL;
  ppd_attr_t*		attr;
  const char		*val;
  filter_logfunc_t	log = data->logfunc;
  void			*ld = data->logdata;
  ppd_file_t		*ppd = pclmtoraster_data->ppd;
  cups_page_header2_t	*header = &(pclmtoraster_data->header);

#ifdef HAVE_CUPS_1_7
  t = getenv("FINAL_CONTENT_TYPE");
  if (t && strcasestr(t, "pwg"))
    pclmtoraster_data->pwgraster = 1;
#endif /* HAVE_CUPS_1_7 */
 /*
  * CUPS option list
  */

  num_options = data->num_options;
  options = data->options;

  ppd = data->ppd;

  if (ppd == NULL && log)
      log(ld, FILTER_LOGLEVEL_DEBUG,
	  "pclmtoraster: PPD file is not specified.");

  if (ppd)
  {
    ppdRasterInterpretPPD(header, ppd, num_options, options, 0);
    if (header->Duplex)
    {
      /* analyze options relevant to Duplex */
      const char *backside = "";
      /* APDuplexRequiresFlippedMargin */
      enum {
        FM_NO, FM_FALSE, FM_TRUE
      } flippedMargin = FM_NO;

      attr = ppdFindAttr(ppd,"cupsBackSide",NULL);
      if (attr != NULL && attr->value != NULL)
      {
        ppd->flip_duplex = 0;
        backside = attr->value;
      }
      else if (ppd->flip_duplex)
      {
        backside = "Rotated"; /* compatible with Max OS and GS 8.71 */
      }

      attr = ppdFindAttr(ppd,"APDuplexRequiresFlippedMargin",NULL);
      if (attr != NULL && attr->value != NULL)
      {
        if (strcasecmp(attr->value,"true") == 0)
	{
          flippedMargin = FM_TRUE;
        }
	else
	{
          flippedMargin = FM_FALSE;
        }
      }
      if (strcasecmp(backside,"ManualTumble") == 0 && header->Tumble)
      {
        pclmtoraster_data->swap_image_x = pclmtoraster_data->swap_image_y =
	  true;
        pclmtoraster_data->swap_margin_x = pclmtoraster_data->swap_margin_y =
	  true;
        if (flippedMargin == FM_TRUE)
	{
          pclmtoraster_data->swap_margin_y = false;
        }
      }
      else if (strcasecmp(backside,"Rotated") == 0 && !header->Tumble)
      {
        pclmtoraster_data->swap_image_x = pclmtoraster_data->swap_image_y =
	  true;
        pclmtoraster_data->swap_margin_x = pclmtoraster_data->swap_margin_y =
	  true;
        if (flippedMargin == FM_TRUE)
	{
          pclmtoraster_data->swap_margin_y = false;
        }
      }
      else if (strcasecmp(backside,"Flipped") == 0)
      {
        if (header->Tumble)
	{
          pclmtoraster_data->swap_image_x = true;
          pclmtoraster_data->swap_margin_x = pclmtoraster_data->swap_margin_y =
	    true;
        }
	else
	{
          pclmtoraster_data->swap_image_y = true;
        }
        if (flippedMargin == FM_FALSE)
	{
          pclmtoraster_data->swap_margin_y =
	    !(pclmtoraster_data->swap_margin_y);
        }
      }
    }

#ifdef HAVE_CUPS_1_7
    if ((attr = ppdFindAttr(ppd,"PWGRaster",0)) != 0 &&
        (!strcasecmp(attr->value, "true")
         || !strcasecmp(attr->value, "on") ||
         !strcasecmp(attr->value, "yes")))
      pclmtoraster_data->pwgraster = 1;
    if (pclmtoraster_data->pwgraster == 1)
      cupsRasterParseIPPOptions(header, num_options, options,
				pclmtoraster_data->pwgraster, 0);
#endif /* HAVE_CUPS_1_7 */
  } else {
#ifdef HAVE_CUPS_1_7
    pclmtoraster_data->pwgraster = 1;
    t = cupsGetOption("media-class", num_options, options);
    if (t == NULL)
      t = cupsGetOption("MediaClass", num_options, options);
    if (t != NULL)
    {
      if (strcasestr(t, "pwg"))
        pclmtoraster_data->pwgraster = 1;
      else
        pclmtoraster_data->pwgraster = 0;
    }
    cupsRasterParseIPPOptions(header, num_options, options,
			      pclmtoraster_data->pwgraster, 1);
#else
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		"pclmtoraster: No PPD file specified: %s", strerror(errno));
    return (1);
#endif /* HAVE_CUPS_1_7 */
  }
  if ((val = cupsGetOption("print-color-mode", num_options, options)) != NULL
                           && !strncasecmp(val, "bi-level", 8))
    pclmtoraster_data->bi_level = 1;

  strncpy(pclmtoraster_data->pageSizeRequested, header->cupsPageSizeName, 64);
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		"pclmtoraster: Page size requested: %s.",
	       header->cupsPageSizeName);
  return(0);
}

static bool
mediaboxlookup(QPDFObjectHandle object,
	       float rect[4])
{
  // preliminary checks
  if (!object.isDictionary() || !object.hasKey("/MediaBox"))
    return false;

  // assign mediabox values to rect
  std::vector<QPDFObjectHandle> mediabox =
    object.getKey("/MediaBox").getArrayAsVector();
  for (int i = 0; i < 4; ++i)
  {
    rect[i] = mediabox[i].getNumericValue();
  }

  return mediabox.size() == 4;
}

/*
 * 'rotatebitmap()' - Function to rotate a bitmap
 *                    (assumed that bits-per-component of the bitmap is 8).
 */

static unsigned char *		    /* O - Output Bitmap */
rotatebitmap(unsigned char *src,    /* I - Input string */
	     unsigned char *dst,    /* O - Destination string */
	     unsigned int rotate,   /* I - Rotate value (0, 90, 180, 270) */
	     unsigned int height,   /* I - Height of raster image in pixels */
	     unsigned int width,    /* I - Width of raster image in pixels */
	     int rowsize,	    /* I - Length of one row of pixels */
	     std::string colorspace,/* I - Colorspace of input bitmap */
	     filter_logfunc_t log,  /* I - Log function */
	     void *ld)		    /* I - Aux. data for log function */
{
  unsigned char *bp = src;
  unsigned char *dp = dst;
  unsigned char *temp = dst;

  if (rotate == 0)
  {
    return (src);
  }
  else if (rotate == 180)
  {
    if (colorspace == "/DeviceGray")
    {
      bp = src + height * rowsize - 1;
      dp = dst;
      for (unsigned int h = 0; h < height; h++)
      {
        for (unsigned int w = 0; w < width; w++, bp --, dp ++)
	{
          *dp = *bp;
        }
      }
    }
    else if (colorspace == "/DeviceCMYK")
    {
      bp = src + height * rowsize - 4;
      dp = dst;
      for (unsigned int h = 0; h < height; h++)
      {
        for (unsigned int w = 0; w < width; w++, bp -= 4, dp += 4)
	{
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
    }
    else if (colorspace == "/DeviceRGB")
    {
      bp = src + height * rowsize - 3;
      dp = dst;
      for (unsigned int h = 0; h < height; h++)
      {
        for (unsigned int w = 0; w < width; w++, bp -= 3, dp += 3)
	{
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
    }
  }
  else if (rotate == 270)
  {
    if (colorspace == "/DeviceGray")
    {
      bp = src;
      dp = dst;
      for (unsigned int h = 0; h < height; h++)
      {
        bp = src + (height - h) - 1;
        for (unsigned int w = 0; w < width; w++, bp += height , dp ++)
	{
          *dp = *bp;
        }
      }
    }
    else if (colorspace == "/DeviceCMYK")
    {
      for (unsigned int h = 0; h < height; h++)
      {
        bp = src + (height - h)*4 - 4;
        for (unsigned int i = 0; i < width; i++, bp += height*4 , dp += 4)
	{
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
    }
    else if (colorspace == "/DeviceRGB")
    {
      bp = src;
      dp = dst;
      for (unsigned int h = 0; h < height; h++)
      {
        bp = src + (height - h)*3 - 3;
        for (unsigned int i = 0; i < width; i++, bp += height*3 , dp += 3)
	{
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
    }
  }
  else if (rotate == 90)
  {
    if (colorspace == "/DeviceGray")
    {
      for (unsigned int h = 0; h < height; h++)
      {
        bp = src + (width - 1) * height + h;
        for (unsigned int i = 0; i < width; i++, bp -= height , dp ++)
	{
          *dp = *bp;
        }
      }
    }
    else if (colorspace == "/DeviceCMYK")
    {
      for (unsigned int h = 0; h < height; h++)
      {
        bp = src + (width - 1) * height * 4 + 4*h;
        for (unsigned int i = 0; i < width; i++, bp -= height*4 , dp += 4)
	{
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
          dp[3] = bp[3];
        }
      }
    }
    else if (colorspace == "/DeviceRGB")
    {
      for (unsigned int h = 0; h < height; h++)
      {
       bp = src + (width - 1) * height * 3 + 3*h;
        for (unsigned int i = 0; i < width; i++, bp -= height*3 , dp += 3)
	{
          dp[0] = bp[0];
          dp[1] = bp[1];
          dp[2] = bp[2];
        }
      }
    }
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pclmtoraster: Incorrect Rotate Value %d, not rotating",
		 rotate);
    return (src);
  }

  return (temp);
}

static unsigned char *
RGBtoCMYKLine(unsigned char *src,
	      unsigned char *dst,
	      unsigned int row,
	      unsigned int pixels,
	      pclmtoraster_data_t *data)
{
  cupsImageRGBToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *
RGBtoCMYLine(unsigned char *src,
	     unsigned char *dst,
	     unsigned int row,
	     unsigned int pixels,
	     pclmtoraster_data_t *data)
{
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *
RGBtoWhiteLine(unsigned char *src,
	       unsigned char *dst,
	       unsigned int row,
	       unsigned int pixels,
	       pclmtoraster_data_t *data)
{
  if (data->header.cupsBitsPerColor != 1) {
    cupsImageRGBToWhite(src,dst,pixels);
  } else {
    cupsImageRGBToWhite(src,src,pixels);
    oneBitLine(src, dst, data->header.cupsWidth, row, data->bi_level);
  }

  return dst;
}

static unsigned char *
RGBtoBlackLine(unsigned char *src,
	       unsigned char *dst,
	       unsigned int row,
	       unsigned int pixels,
	       pclmtoraster_data_t *data)
{
  if (data->header.cupsBitsPerColor != 1) {
    cupsImageRGBToBlack(src,dst,pixels);
  } else {
    cupsImageRGBToBlack(src,src,pixels);
    oneBitLine(src, dst, data->header.cupsWidth, row, data->bi_level);
  }
  return dst;
}

static unsigned char *
CMYKtoRGBLine(unsigned char *src,
	      unsigned char *dst,
	      unsigned int row,
	      unsigned int pixels,
	      pclmtoraster_data_t *data)
{
  cupsImageCMYKToRGB(src,dst,pixels);
  return dst;
}

static unsigned char *
CMYKtoCMYLine(unsigned char *src,
	      unsigned char *dst,
	      unsigned int row,
	      unsigned int pixels,
	      pclmtoraster_data_t *data)
{
  // Converted first to rgb and then to cmy for better outputs.
  cupsImageCMYKToRGB(src,src,pixels);
  cupsImageRGBToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *
CMYKtoWhiteLine(unsigned char *src,
	        unsigned char *dst,
		unsigned int row,
		unsigned int pixels,
		pclmtoraster_data_t *data)
{
  if (data->header.cupsBitsPerColor != 1) {
    cupsImageCMYKToWhite(src,dst,pixels);
  } else {
    cupsImageCMYKToWhite(src,src,pixels);
    oneBitLine(src, dst, data->header.cupsWidth, row, data->bi_level);
  }
  return dst;
}

static unsigned char *
CMYKtoBlackLine(unsigned char *src,
	        unsigned char *dst,
		unsigned int row,
		unsigned int pixels,
		pclmtoraster_data_t *data)
{
  if (data->header.cupsBitsPerColor != 1) {
    cupsImageCMYKToBlack(src,dst,pixels);
  } else {
    cupsImageCMYKToBlack(src,src,pixels);
    oneBitLine(src, dst, data->header.cupsWidth, row, data->bi_level);
  }
  return dst;
}

static unsigned char *
GraytoRGBLine(unsigned char *src,
	      unsigned char *dst,
	      unsigned int row,
	      unsigned int pixels,
	      pclmtoraster_data_t *data)
{
  cupsImageWhiteToRGB(src,dst,pixels);
  return dst;
}

static unsigned char *
GraytoCMYKLine(unsigned char *src,
	       unsigned char *dst,
	       unsigned int row,
	       unsigned int pixels,
	      pclmtoraster_data_t *data)
{
  cupsImageWhiteToCMYK(src,dst,pixels);
  return dst;
}

static unsigned char *
GraytoCMYLine(unsigned char *src,
	      unsigned char *dst,
	      unsigned int row,
	      unsigned int pixels,
	      pclmtoraster_data_t *data)
{
  cupsImageWhiteToCMY(src,dst,pixels);
  return dst;
}

static unsigned char *
GraytoBlackLine(unsigned char *src,
		unsigned char *dst,
		unsigned int row,
		unsigned int pixels,
		pclmtoraster_data_t *data)
{
  if (data->header.cupsBitsPerColor != 1) {
    cupsImageWhiteToBlack(src, dst, pixels);
  } else {
    cupsImageWhiteToBlack(src, src, pixels);
    oneBitLine(src, dst, data->header.cupsWidth, row, data->bi_level);
  }
  return dst;
}

static unsigned char *
convertcspaceNoop(unsigned char *src,
		  unsigned char *dst,
		  unsigned int row,
		  unsigned int pixels,
		  pclmtoraster_data_t *data)
{
  return src;
}

/*
 * 'convertLine()' - Function to convert colorspace and bits-per-pixel
 *                   of a single line of raster data.
 */

static unsigned char *			/* O - Output string */
convertLine(unsigned char 	*src,	/* I - Input line */
	    unsigned char 	*dst,	/* O - Destination string */
	    unsigned char 	*buf,	/* I - Buffer string */
	    unsigned int 	row,	/* I - Current Row */
	    unsigned int 	plane,	/* I - Plane/Band */
	    pclmtoraster_data_t *data,
	    ConvertCSpace	convertcspace)
{
  /*
   Use only convertcspace if conversion of bits and conversion of color order
   is not required, or if dithering is required, for faster processing of
   raster output.
   */
  unsigned int pixels = data->header.cupsWidth;
  if ((data->header.cupsBitsPerColor == 1
	&& data->header.cupsNumColors == 1)
	|| (data->header.cupsBitsPerColor == 8
	&& data->header.cupsColorOrder == CUPS_ORDER_CHUNKED))
  {
    dst = convertcspace(src, dst, row, pixels, data);
  }
  else
  {
    for (unsigned int i = 0;i < pixels;i++)
    {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;
      pb = convertcspace(src + i*(data->numcolors), pixelBuf1, row, 1, data);
      pb = convertbits(pb, pixelBuf2, i, row, data->header.cupsNumColors,
		       data->header.cupsBitsPerColor);
      writepixel(dst, plane, i, pb, data->header.cupsNumColors,
		 data->header.cupsBitsPerColor, data->header.cupsColorOrder);
    }
  }
  return dst;
}

/*
 * 'convertReverseLine()' - Function to convert colorspace and bits-per-pixel
 *                          of a single line of raster data and reverse the
 *                          line.
 */

static unsigned char *					/* O - Output string */
convertReverseLine(unsigned char	*src,		/* I - Input line */
		   unsigned char	*dst,		/* O - Destination
							       string */
		   unsigned char	*buf,		/* I - Buffer string */
		   unsigned int		row,		/* I - Current Row */
		   unsigned int		plane,		/* I - Plane/Band */
		   pclmtoraster_data_t *data,		/* I - pclmtoraster
							       filter data */
		   ConvertCSpace	convertcspace)	/* I - Function for
							       conversion of
							       colorspace */
{
  /*
   Use only convertcspace if conversion of bits and conversion of color order
   is not required, or if dithering is required, for faster processing of
   raster output.
  */
  unsigned int pixels = data->header.cupsWidth;
  if (data->header.cupsBitsPerColor == 1 && data->header.cupsNumColors == 1)
  {
    buf = convertcspace(src, buf, row, pixels, data);
    dst = reverseOneBitLine(buf, dst, pixels, data->bytesPerLine);
  }
  else if (data->header.cupsBitsPerColor == 8 &&
	   data->header.cupsColorOrder == CUPS_ORDER_CHUNKED)
  {
    unsigned char *dp = dst;
    // Assign each pixel of buf to dst in the reverse order.
    buf = convertcspace(src, buf, row, pixels, data) +
      (data->header.cupsWidth - 1)*data->header.cupsNumColors;
    for (unsigned int i = 0; i < pixels; i++, buf-=data->header.cupsNumColors,
	   dp+=data->header.cupsNumColors)
    {
      for (unsigned int j = 0; j < data->header.cupsNumColors; j++)
      {
	dp[j] = buf[j];
      }
    }
  }
  else
  {
    for (unsigned int i = 0;i < pixels;i++)
    {
      unsigned char pixelBuf1[MAX_BYTES_PER_PIXEL];
      unsigned char pixelBuf2[MAX_BYTES_PER_PIXEL];
      unsigned char *pb;
      pb = convertcspace(src + (pixels - i - 1)*(data->numcolors), pixelBuf1,
			 row, 1, data);
      pb = convertbits(pb, pixelBuf2, i, row, data->header.cupsNumColors,
		       data->header.cupsBitsPerColor);
      writepixel(dst, plane, i, pb, data->header.cupsNumColors,
		 data->header.cupsBitsPerColor, data->header.cupsColorOrder);
    }
  }
  return dst;
}

static void					 /* O - Exit status */
selectConvertFunc(int			pgno,	 /* I - Page number */
		  filter_logfunc_t	log,	 /* I - Log function */
		  void			*ld,	 /* I - Aux. data for log
						        function */
		  pclmtoraster_data_t	*data,	 /* I - pclmtoraster filter
						        data */
		  conversion_function_t	*convert)/* I - Conversion functions */
{
  /* Set rowsize and numcolors based on colorspace of raster data */
  cups_page_header2_t header = data->header;
  std::string colorspace = data->colorspace;
  if (colorspace == "/DeviceRGB")
  {
    data->rowsize = header.cupsWidth*3;
    data->numcolors = 3;
  }
  else if (colorspace == "/DeviceCMYK")
  {
    data->rowsize = header.cupsWidth*4;
    data->numcolors = 4;
  }
  else if (colorspace == "/DeviceGray")
  {
    data->rowsize = header.cupsWidth;
    data->numcolors = 1;
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pclmtoraster: Colorspace %s not supported, "
		 "defaulting to /deviceRGB",
		 colorspace.c_str());
    data->colorspace = "/DeviceRGB";
    data->rowsize = header.cupsWidth*3;
    data->numcolors = 3;
  }

  convert->convertcspace = convertcspaceNoop; //Default function
  /* Select convertcspace function */
  switch (header.cupsColorSpace)
  {
    case CUPS_CSPACE_K:
     if (colorspace == "/DeviceRGB") convert->convertcspace = RGBtoBlackLine;
     else if (colorspace == "/DeviceCMYK") convert->convertcspace =
					     CMYKtoBlackLine;
     else if (colorspace == "/DeviceGray") convert->convertcspace =
					     GraytoBlackLine;
     break;
    case CUPS_CSPACE_W:
    case CUPS_CSPACE_SW:
     if (colorspace == "/DeviceRGB") convert->convertcspace = RGBtoWhiteLine;
     else if (colorspace == "/DeviceCMYK") convert->convertcspace =
					     CMYKtoWhiteLine;
     break;
    case CUPS_CSPACE_CMY:
     if (colorspace == "/DeviceRGB") convert->convertcspace = RGBtoCMYLine;
     else if (colorspace == "/DeviceCMYK") convert->convertcspace =
					     CMYKtoCMYLine;
     else if (colorspace == "/DeviceGray") convert->convertcspace =
					     GraytoCMYLine;
     break;
    case CUPS_CSPACE_CMYK:
     if (colorspace == "/DeviceRGB") convert->convertcspace = RGBtoCMYKLine;
     else if (colorspace == "/DeviceGray") convert->convertcspace =
					     GraytoCMYKLine;
     break;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_ADOBERGB:
    case CUPS_CSPACE_SRGB:
    default:
     if (colorspace == "/DeviceCMYK") convert->convertcspace = CMYKtoRGBLine;
     else if (colorspace == "/DeviceGray") convert->convertcspace =
					     GraytoRGBLine;
     break;
   }

  /* Select convertline function */
  if (header.Duplex && (pgno & 1) && data->swap_image_x)
  {
    convert->convertline = convertReverseLine;
  }
  else
  {
    convert->convertline = convertLine;
  }

}

/*
 * 'outPage()' - Function to convert a single page of raster-only PDF/PCLm
 *               input to CUPS/PWG Raster.
 */

static int				/* O - Exit status */
outPage(cups_raster_t*	 raster, 	/* I - Raster stream */
	QPDFObjectHandle page,		/* I - QPDF Page Object */
	int		 pgno,		/* I - Page number */
	filter_logfunc_t log,		/* I - Log function */
	void*		 ld,		/* I - Aux. data for log function */
	pclmtoraster_data_t *data,	/* I - pclmtoraster filter data */
	conversion_function_t *convert) /* I - Conversion functions */
{
  long long		rotate = 0,
			height,
			width;
  double		paperdimensions[2], margins[4], l, swap;
  int			bufsize = 0, pixel_count = 0,
			temp = 0;
  float 		mediaBox[4];
  unsigned char 	*bitmap = NULL,
			*colordata = NULL,
			*lineBuf = NULL,
			*line = NULL,
			*dp = NULL;
  QPDFObjectHandle	image;
  QPDFObjectHandle	imgdict;
  QPDFObjectHandle	colorspace_obj;
  ppd_file_t		*ppd = data->ppd;

  // Check if page is rotated.
  if (page.getKey("/Rotate").isInteger())
    rotate = page.getKey("/Rotate").getIntValueAsInt();

  // Get pagesize by the mediabox key of the page.
  if (!mediaboxlookup(page, mediaBox))
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pclmtoraster: PDF page %d doesn't contain a valid mediaBox",
		 pgno + 1);
    return (1);
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "pclmtoraster: mediaBox = [%f %f %f %f]: ",
		 mediaBox[0], mediaBox[1], mediaBox[2], mediaBox[3]);
    l = mediaBox[2] - mediaBox[0];
    if (l < 0) l = -l;
    if (rotate == 90 || rotate == 270)
      data->header.PageSize[1] = (unsigned)l;
    else
      data->header.PageSize[0] = (unsigned)l;
    l = mediaBox[3] - mediaBox[1];
    if (l < 0) l = -l;
    if (rotate == 90 || rotate == 270)
      data->header.PageSize[0] = (unsigned)l;
    else
      data->header.PageSize[1] = (unsigned)l;
  }

  // Adjust header page size and margins according to the ppd file.
  if (ppd)
  {
    ppdRasterMatchPPDSize(&(data->header), ppd, margins, paperdimensions, NULL,
			  NULL);
    if (data->pwgraster == 1)
      memset(margins, 0, sizeof(margins));
  }
  else
  {
    for (int i = 0; i < 2; i ++)
      paperdimensions[i] = data->header.PageSize[i];
    if (data->header.cupsImagingBBox[3] > 0.0)
    {
      /* Set margins if we have a bounding box defined ... */
      if (data->pwgraster == 0)
      {
	margins[0] = data->header.cupsImagingBBox[0];
	margins[1] = data->header.cupsImagingBBox[1];
	margins[2] = paperdimensions[0] - data->header.cupsImagingBBox[2];
	margins[3] = paperdimensions[1] - data->header.cupsImagingBBox[3];
      }
    }
    else
      /* ... otherwise use zero margins */
      for (int i = 0; i < 4; i ++)
	margins[i] = 0.0;
  }

  if (data->header.Duplex && (pgno & 1))
  {
    /* backside: change margin if needed */
    if (data->swap_margin_x)
    {
      swap = margins[2]; margins[2] = margins[0]; margins[0] = swap;
    }
    if (data->swap_margin_y)
    {
      swap = margins[3]; margins[3] = margins[1]; margins[1] = swap;
    }
  }

  /* write page header */
  for (int i = 0; i < 2; i ++)
  {
    data->header.cupsPageSize[i] = paperdimensions[i];
    data->header.PageSize[i] = (unsigned int)(data->header.cupsPageSize[i] +
					      0.5);
    if (data->pwgraster == 0)
      data->header.Margins[i] = margins[i] + 0.5;
    else
      data->header.Margins[i] = 0;
  }
  if (data->pwgraster == 0)
  {
    data->header.cupsImagingBBox[0] = margins[0];
    data->header.cupsImagingBBox[1] = margins[1];
    data->header.cupsImagingBBox[2] = paperdimensions[0] - margins[2];
    data->header.cupsImagingBBox[3] = paperdimensions[1] - margins[3];
    for (int i = 0; i < 4; i ++)
      data->header.ImagingBoundingBox[i] =
	(unsigned int)(data->header.cupsImagingBBox[i] + 0.5);
  }
  else
  {
    for (int i = 0; i < 4; i ++)
    {
      data->header.cupsImagingBBox[i] = 0.0;
      data->header.ImagingBoundingBox[i] = 0;
    }
  }

  data->header.cupsWidth = 0;
  data->header.cupsHeight = 0;

  /* Loop over all raster images in a page and store them in bitmap. */
  std::map<std::string, QPDFObjectHandle> images = page.getPageImages();
  for (auto const& iter: images)
  {
    image = iter.second;
    imgdict = image.getDict(); //XObject dictionary

    PointerHolder<Buffer> actual_data = image.getStreamData(qpdf_dl_all);
    width = imgdict.getKey("/Width").getIntValue();
    height = imgdict.getKey("/Height").getIntValue();
    colorspace_obj = imgdict.getKey("/ColorSpace");
    data->header.cupsHeight += height;
    bufsize = actual_data->getSize();

    if(!pixel_count)
    {
      bitmap = (unsigned char *) malloc(bufsize);
    }
    else
    {
      bitmap = (unsigned char *) realloc(bitmap, pixel_count + bufsize);
    }
    memcpy(bitmap + pixel_count, actual_data->getBuffer(), bufsize);
    pixel_count += bufsize;

    if (width > data->header.cupsWidth)
      data->header.cupsWidth = width;
  }

  // Swap width and height in landscape images
  if (rotate == 270 || rotate == 90)
  {
    temp = data->header.cupsHeight;
    data->header.cupsHeight = data->header.cupsWidth;
    data->header.cupsWidth = temp;
  }

  data->bytesPerLine = data->header.cupsBytesPerLine =
    (data->header.cupsBitsPerPixel * data->header.cupsWidth + 7) / 8;
  if (data->header.cupsColorOrder == CUPS_ORDER_BANDED)
    data->header.cupsBytesPerLine *= data->header.cupsNumColors;

  if (!cupsRasterWriteHeader2(raster,&(data->header)))
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pclmtoraster: Can't write page %d header", pgno + 1);
    return (1);
  }

  data->colorspace = (colorspace_obj.isName() ?
		      colorspace_obj.getName() : "/DeviceRGB");
                         // Default for pclm files in DeviceRGB

  /* Select convertline and convertscpace function */
  selectConvertFunc(pgno, log, ld, data, convert);

  // If page is to be swapped in both x and y, rotate it by 180 degress
  if (data->header.Duplex && (pgno & 1) && data->swap_image_y &&
      data->swap_image_x)
  {
    rotate = (rotate + 180) % 360;
    data->swap_image_y = false;
    data->swap_image_x = false;
  }

  /* Rotate Bitmap */
  if (rotate)
  {
    unsigned char *bitmap2 = (unsigned char *) malloc(pixel_count);
    bitmap2 = rotatebitmap(bitmap, bitmap2, rotate, data->header.cupsHeight,
			   data->header.cupsWidth, data->rowsize,
			   data->colorspace, log, ld);
    free(bitmap);
    bitmap = bitmap2;
  }

  colordata = bitmap;

  /* Write page image */
  lineBuf = new unsigned char [data->bytesPerLine];
  line = new unsigned char [data->bytesPerLine];
  if (data->header.Duplex && (pgno & 1) && data->swap_image_y)
  {
    for (unsigned int plane = 0; plane < data->nplanes ; plane++)
    {
      unsigned char *bp = colordata +
	(data->header.cupsHeight - 1) * (data->rowsize);
      for (unsigned int h = data->header.cupsHeight; h > 0; h--)
      {
        for (unsigned int band = 0; band < data->nbands; band++)
	{
          dp = convert->convertline(bp, line, lineBuf, h - 1, plane + band,
				    data, convert->convertcspace);
          cupsRasterWritePixels(raster, dp, data->bytesPerLine);
        }
        bp -= data->rowsize;
      }
    }
  }
  else
  {
    for (unsigned int plane = 0; plane < data->nplanes ; plane++)
    {
      unsigned char *bp = colordata;
      for (unsigned int h = 0; h < data->header.cupsHeight; h++)
      {
        for (unsigned int band = 0; band < data->nbands; band++)
	{
          dp = convert->convertline(bp, line, lineBuf, h, plane + band,
				    data, convert->convertcspace);
          cupsRasterWritePixels(raster, dp, data->bytesPerLine);
        }
        bp += data->rowsize;
      }
    }
  }
  delete[] lineBuf;
  delete[] line;
  free(bitmap);

  return (0);
}

/*
 * 'pclmtoraster()' - Filter function to convert raster-only PDF/PCLm input to
 *		      CUPS/PWG Raster output.
 */

int				  /* O - Error status */
pclmtoraster(int inputfd,         /* I - File descriptor input stream */
	     int outputfd,        /* I - File descriptor output stream */
	     int inputseekable,   /* I - Is input stream seekable? (unused) */
	     filter_data_t *data, /* I - Job and printer data */
	     void *parameters)    /* I - Filter-specific parameters (unused) */
{
  FILE			*inputfp;		/* Input file pointer */
  int			fd = 0;			/* Copy file descriptor */
  char			*filename,		/* PDF file to convert */
			tempfile[1024];		/* Temporary file */
  char			buffer[8192];		/* Copy buffer */
  int			bytes;			/* Bytes copied */
  int			npages = 0;
  QPDF			*pdf = new QPDF();
  cups_raster_t		*raster;
  pclmtoraster_data_t	pclmtoraster_data;
  conversion_function_t convert;
  filter_logfunc_t	log = data->logfunc;
  void			*ld = data->logdata;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void                  *icd = data->iscanceleddata;

 /*
  * Open the input data stream specified by the inputfd...
  */

  if ((inputfp = fdopen(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "pclmtoraster: Unable to open input data stream.");
    }

    return (1);
  }

  if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "pclmtoraster: Unable to copy PDF file: %s",
		 strerror(errno));
    fclose(inputfp);
    return (1);
  }

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "pclmtoraster: Copying input to temp file \"%s\"",
	       tempfile);

  while ((bytes = fread(buffer, 1, sizeof(buffer), inputfp)) > 0)
    bytes = write(fd, buffer, bytes);

  fclose(inputfp);
  close(fd);

  filename = tempfile;
  pdf->processFile(filename);

  if (parseOpts(data, &pclmtoraster_data) != 0)
  {
    delete(pdf);
    unlink(tempfile);
    return (1);
  }

  if (pclmtoraster_data.header.cupsBitsPerColor != 1
     && pclmtoraster_data.header.cupsBitsPerColor != 2
     && pclmtoraster_data.header.cupsBitsPerColor != 4
     && pclmtoraster_data.header.cupsBitsPerColor != 8
     && pclmtoraster_data.header.cupsBitsPerColor != 16)
  {
    if(log) log(ld, FILTER_LOGLEVEL_ERROR,
		"pclmtoraster: Specified color format is not supported: %s",
		strerror(errno));
    delete(pdf);
    unlink(tempfile);
    if (pclmtoraster_data.ppd != NULL)  ppdClose(pclmtoraster_data.ppd);
    return (1);
  }

  if (pclmtoraster_data.header.cupsColorOrder == CUPS_ORDER_PLANAR)
  {
    pclmtoraster_data.nplanes = pclmtoraster_data.header.cupsNumColors;
  }
  else
  {
    pclmtoraster_data.nplanes = 1;
  }
  if (pclmtoraster_data.header.cupsColorOrder == CUPS_ORDER_BANDED)
  {
    pclmtoraster_data.nbands = pclmtoraster_data.header.cupsNumColors;
  }
  else
  {
    pclmtoraster_data.nbands = 1;
  }

  if ((raster = cupsRasterOpen(outputfd, pclmtoraster_data.pwgraster ?
			       CUPS_RASTER_WRITE_PWG :
                               CUPS_RASTER_WRITE)) == 0)
  {
    if(log) log(ld, FILTER_LOGLEVEL_ERROR,
		"pclmtoraster: Can't open raster stream: %s",
		strerror(errno));
    delete(pdf);
    unlink(tempfile);
    if (pclmtoraster_data.ppd != NULL)  ppdClose(pclmtoraster_data.ppd);
    return (1);
  }

  std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
  npages = pages.size();

  for (int i = 0; i < npages; ++i)
  {
    if (iscanceled && iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "pclmtoraster: Job canceled");
      break;
    }

    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "pclmtoraster: Starting page %d.", i+1);
    if (outPage(raster, pages[i], i, log, ld, &pclmtoraster_data,
		&convert) != 0)
      break;
  }

  cupsRasterClose(raster);
  delete pdf;
  unlink(tempfile);
  if (pclmtoraster_data.ppd != NULL)  ppdClose(pclmtoraster_data.ppd);
  return 0;
}

void operator delete[](void *p) throw ()
{
  free(p);
}

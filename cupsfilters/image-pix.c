/*
 *   Alias PIX image routines for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   _cfImageReadPIX() - Read a PIX image file.
 *   read_short()        - Read a 16-bit integer.
 */

/*
 * Include necessary headers...
 */

#include "image-private.h"


/*
 * Local functions...
 */

static short	read_short(FILE *fp);


/*
 * '_cfImageReadPIX()' - Read a PIX image file.
 */

int					/* O - Read status */
_cfImageReadPIX(
    cf_image_t    *img,		/* IO - Image */
    FILE            *fp,		/* I - Image file */
    cf_icspace_t  primary,		/* I - Primary choice for colorspace */
    cf_icspace_t  secondary,		/* I - Secondary choice for colorspace */
    int             saturation,		/* I - Color saturation (%) */
    int             hue,		/* I - Color hue (degrees) */
    const cf_ib_t *lut)		/* I - Lookup table for gamma/brightness */
{
  short		width,			/* Width of image */
		height,			/* Height of image */
		depth;			/* Depth of image (bits) */
  int		count,			/* Repetition count */
		bpp,			/* Bytes per pixel */
		x, y;			/* Looping vars */
  cf_ib_t	r, g, b;		/* Red, green/gray, blue values */
  cf_ib_t	*in,			/* Input pixels */
		*out,			/* Output pixels */
		*ptr;			/* Pointer into pixels */


 /*
  * Get the image dimensions and setup the image...
  */

  width  = read_short(fp);
  height = read_short(fp);
  read_short(fp);
  read_short(fp);
  depth  = read_short(fp);

 /*
  * Check the dimensions of the image.  Since the short values used for the
  * width and height cannot exceed CF_IMAGE_MAX_WIDTH or
  * CF_IMAGE_MAX_HEIGHT, we just need to verify they are positive integers.
  */

  if (width <= 0 || height <= 0 ||
      (depth != 8 && depth != 24))
  {
    DEBUG_printf(("DEBUG: Bad PIX image dimensions %dx%dx%d\n",
		  width, height, depth));
    fclose(fp);
    return (1);
  }

  if (depth == 8)
    img->colorspace = secondary;
  else
    img->colorspace = (primary == CF_IMAGE_RGB_CMYK) ? CF_IMAGE_RGB : primary;

  img->xsize = width;
  img->ysize = height;

  cfImageSetMaxTiles(img, 0);

  bpp = cfImageGetDepth(img);

  if ((in = malloc(img->xsize * (depth / 8))) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    fclose(fp);
    return (1);
  }

  if ((out = malloc(img->xsize * bpp)) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    fclose(fp);
    free(in);
    return (1);
  }

 /*
  * Read the image data...
  */

  if (depth == 8)
  {
    for (count = 0, y = 0, g = 0; y < img->ysize; y ++)
    {
      if (img->colorspace == CF_IMAGE_WHITE)
        ptr = out;
      else
        ptr = in;

      for (x = img->xsize; x > 0; x --, count --)
      {
        if (count == 0)
	{
          count = getc(fp);
	  g     = getc(fp);
	}

        *ptr++ = g;
      }

      if (img->colorspace != CF_IMAGE_WHITE)
	switch (img->colorspace)
	{
	  default :
	      cfImageWhiteToRGB(in, out, img->xsize);
	      break;
	  case CF_IMAGE_BLACK :
	      cfImageWhiteToBlack(in, out, img->xsize);
	      break;
	  case CF_IMAGE_CMY :
	      cfImageWhiteToCMY(in, out, img->xsize);
	      break;
	  case CF_IMAGE_CMYK :
	      cfImageWhiteToCMYK(in, out, img->xsize);
	      break;
	}

      if (lut)
	cfImageLut(out, img->xsize * bpp, lut);

      _cfImagePutRow(img, 0, y, img->xsize, out);
    }
  }
  else
  {
    for (count = 0, y = 0, r = 0, g = 0, b = 0; y < img->ysize; y ++)
    {
      ptr = in;

      for (x = img->xsize; x > 0; x --, count --)
      {
        if (count == 0)
	{
          count = getc(fp);
	  b     = getc(fp);
	  g     = getc(fp);
	  r     = getc(fp);
	}

        *ptr++ = r;
        *ptr++ = g;
        *ptr++ = b;
      }

      if (saturation != 100 || hue != 0)
	cfImageRGBAdjust(in, img->xsize, saturation, hue);

      switch (img->colorspace)
      {
	default :
	    break;

	case CF_IMAGE_WHITE :
	    cfImageRGBToWhite(in, out, img->xsize);
	    break;
	case CF_IMAGE_RGB :
	    cfImageRGBToWhite(in, out, img->xsize);
	    break;
	case CF_IMAGE_BLACK :
	    cfImageRGBToBlack(in, out, img->xsize);
	    break;
	case CF_IMAGE_CMY :
	    cfImageRGBToCMY(in, out, img->xsize);
	    break;
	case CF_IMAGE_CMYK :
	    cfImageRGBToCMYK(in, out, img->xsize);
	    break;
      }

      if (lut)
	cfImageLut(out, img->xsize * bpp, lut);

      _cfImagePutRow(img, 0, y, img->xsize, out);
    }
  }

  fclose(fp);
  free(in);
  free(out);

  return (0);
}


/*
 * 'read_short()' - Read a 16-bit integer.
 */

static short				/* O - Value from file */
read_short(FILE *fp)			/* I - File to read from */
{
  int	ch;				/* Character from file */


  ch = getc(fp);
  return ((ch << 8) | getc(fp));
}


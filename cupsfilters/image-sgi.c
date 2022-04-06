/*
 *   SGI image file routines for CUPS.
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
 *   _cfImageReadSGI() - Read a SGI image file.
 */

/*
 * Include necessary headers...
 */

#include "image-private.h"
#include "image-sgi.h"


/*
 * '_cfImageReadSGI()' - Read a SGI image file.
 */

int					/* O - Read status */
_cfImageReadSGI(
    cf_image_t    *img,		/* IO - Image */
    FILE            *fp,		/* I - Image file */
    cf_icspace_t  primary,		/* I - Primary choice for colorspace */
    cf_icspace_t  secondary,		/* I - Secondary choice for colorspace */
    int             saturation,		/* I - Color saturation (%) */
    int             hue,		/* I - Color hue (degrees) */
    const cf_ib_t *lut)		/* I - Lookup table for gamma/brightness */
{
  int		i, y;			/* Looping vars */
  int		bpp;			/* Bytes per pixel */
  cf_sgi_t		*sgip;			/* SGI image file */
  cf_ib_t	*in,			/* Input pixels */
		*inptr,			/* Current input pixel */
		*out;			/* Output pixels */
  unsigned short *rows[4],		/* Row pointers for image data */
		*red,
		*green,
		*blue,
		*gray,
		*alpha;


 /*
  * Setup the SGI file...
  */

  sgip = cfSGIOpenFile(fp, CF_SGI_READ, 0, 0, 0, 0, 0);

 /*
  * Get the image dimensions and load the output image...
  */

 /*
  * Check the image dimensions; since xsize and ysize are unsigned shorts,
  * just check if they are 0 since they can't exceed CF_IMAGE_MAX_WIDTH or
  * CF_IMAGE_MAX_HEIGHT...
  */

  if (sgip->xsize == 0 || sgip->ysize == 0 ||
      sgip->zsize == 0 || sgip->zsize > 4)
  {
    DEBUG_printf(("DEBUG: Bad SGI image dimensions %ux%ux%u!\n",
		  sgip->xsize, sgip->ysize, sgip->zsize));
    cfSGIClose(sgip);
    return (1);
  }

  if (sgip->zsize < 3)
    img->colorspace = secondary;
  else
    img->colorspace = (primary == CF_IMAGE_RGB_CMYK) ? CF_IMAGE_RGB : primary;

  img->xsize = sgip->xsize;
  img->ysize = sgip->ysize;

  cfImageSetMaxTiles(img, 0);

  bpp = cfImageGetDepth(img);

  if ((in = malloc(img->xsize * sgip->zsize)) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    cfSGIClose(sgip);
    return (1);
  }

  if ((out = malloc(img->xsize * bpp)) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    cfSGIClose(sgip);
    free(in);
    return (1);
  }

  if ((rows[0] = calloc(img->xsize * sgip->zsize,
                        sizeof(unsigned short))) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    cfSGIClose(sgip);
    free(in);
    free(out);
    return (1);
  }

  for (i = 1; i < sgip->zsize; i ++)
    rows[i] = rows[0] + i * img->xsize;

 /*
  * Read the SGI image file...
  */

  for (y = 0; y < img->ysize; y ++)
  {
    for (i = 0; i < sgip->zsize; i ++)
      cfSGIGetRow(sgip, rows[i], img->ysize - 1 - y, i);

    switch (sgip->zsize)
    {
      case 1 :
          if (sgip->bpp == 1)
	    for (i = img->xsize - 1, gray = rows[0], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = *gray++;
            }
          else
	    for (i = img->xsize - 1, gray = rows[0], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = (*gray++) / 256 + 128;
            }
          break;
      case 2 :
          if (sgip->bpp == 1)
	    for (i = img->xsize - 1, gray = rows[0], alpha = rows[1], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = (*gray++) * (*alpha++) / 255;
            }
          else
	    for (i = img->xsize - 1, gray = rows[0], alpha = rows[1], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = ((*gray++) / 256 + 128) * (*alpha++) / 32767;
            }
          break;
      case 3 :
          if (sgip->bpp == 1)
	    for (i = img->xsize - 1, red = rows[0], green = rows[1],
	             blue = rows[2], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = *red++;
              *inptr++ = *green++;
              *inptr++ = *blue++;
            }
          else
	    for (i = img->xsize - 1, red = rows[0], green = rows[1],
	             blue = rows[2], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = (*red++) / 256 + 128;
              *inptr++ = (*green++) / 256 + 128;
              *inptr++ = (*blue++) / 256 + 128;
            }
          break;
      case 4 :
          if (sgip->bpp == 1)
	    for (i = img->xsize - 1, red = rows[0], green = rows[1],
	             blue = rows[2], alpha = rows[3], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = (*red++) * (*alpha) / 255;
              *inptr++ = (*green++) * (*alpha) / 255;
              *inptr++ = (*blue++) * (*alpha++) / 255;
            }
          else
	    for (i = img->xsize - 1, red = rows[0], green = rows[1],
	             blue = rows[2], alpha = rows[3], inptr = in;
		 i >= 0;
		 i --)
            {
              *inptr++ = ((*red++) / 256 + 128) * (*alpha) / 32767;
              *inptr++ = ((*green++) / 256 + 128) * (*alpha) / 32767;
              *inptr++ = ((*blue++) / 256 + 128) * (*alpha++) / 32767;
            }
          break;
    }

    if (sgip->zsize < 3)
    {
      if (img->colorspace == CF_IMAGE_WHITE)
      {
        if (lut)
	  cfImageLut(in, img->xsize, lut);

        _cfImagePutRow(img, 0, y, img->xsize, in);
      }
      else
      {
	switch (img->colorspace)
	{
	  default :
	      break;

	  case CF_IMAGE_RGB :
	  case CF_IMAGE_RGB_CMYK :
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
      if ((saturation != 100 || hue != 0) && bpp > 1)
	cfImageRGBAdjust(in, img->xsize, saturation, hue);

      switch (img->colorspace)
      {
	default :
	    break;

	case CF_IMAGE_WHITE :
	    cfImageRGBToWhite(in, out, img->xsize);
	    break;
	case CF_IMAGE_RGB :
	    cfImageRGBToRGB(in, out, img->xsize);
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

  free(in);
  free(out);
  free(rows[0]);

  cfSGIClose(sgip);

  return (0);
}


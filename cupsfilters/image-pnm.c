/*
 *   Portable Any Map file routines for CUPS.
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
 *   _cfImageReadPNM() - Read a PNM image file.
 */

/*
 * Include necessary headers...
 */

#include "image-private.h"


/*
 * '_cfImageReadPNM()' - Read a PNM image file.
 */

int					/* O - Read status */
_cfImageReadPNM(
    cf_image_t    *img,		/* IO - Image */
    FILE            *fp,		/* I - Image file */
    cf_icspace_t  primary,		/* I - Primary choice for colorspace */
    cf_icspace_t  secondary,		/* I - Secondary choice for colorspace */
    int             saturation,		/* I - Color saturation (%) */
    int             hue,		/* I - Color hue (degrees) */
    const cf_ib_t *lut)		/* I - Lookup table for gamma/brightness */
{
  int		x, y;			/* Looping vars */
  int		bpp;			/* Bytes per pixel */
  cf_ib_t	*in,			/* Input pixels */
		*inptr,			/* Current input pixel */
		*out,			/* Output pixels */
		*outptr,		/* Current output pixel */
		bit;			/* Bit in input line */
  char		line[255],		/* Input line */
		*lineptr;		/* Pointer in line */
  int		format,			/* Format of PNM file */
		val,			/* Pixel value */
		maxval;			/* Maximum pixel value */


 /*
  * Read the file header in the format:
  *
  *   Pformat
  *   # comment1
  *   # comment2
  *   ...
  *   # commentN
  *   width
  *   height
  *   max sample
  */

  if ((lineptr = fgets(line, sizeof(line), fp)) == NULL)
  {
    DEBUG_puts("DEBUG: Bad PNM header!\n");
    fclose(fp);
    return (1);
  }

  lineptr ++;

  format = atoi(lineptr);
  while (isdigit(*lineptr & 255))
    lineptr ++;

  while (lineptr != NULL && img->xsize == 0)
  {
    if (*lineptr == '\0' || *lineptr == '#')
      lineptr = fgets(line, sizeof(line), fp);
    else if (isdigit(*lineptr & 255))
    {
      img->xsize = atoi(lineptr);
      while (isdigit(*lineptr & 255))
	lineptr ++;
    }
    else
      lineptr ++;
  }

  while (lineptr != NULL && img->ysize == 0)
  {
    if (*lineptr == '\0' || *lineptr == '#')
      lineptr = fgets(line, sizeof(line), fp);
    else if (isdigit(*lineptr & 255))
    {
      img->ysize = atoi(lineptr);
      while (isdigit(*lineptr & 255))
	lineptr ++;
    }
    else
      lineptr ++;
  }

  if (format != 1 && format != 4)
  {
    maxval = 0;

    while (lineptr != NULL && maxval == 0)
    {
      if (*lineptr == '\0' || *lineptr == '#')
	lineptr = fgets(line, sizeof(line), fp);
      else if (isdigit(*lineptr & 255))
      {
	maxval = atoi(lineptr);
	while (isdigit(*lineptr & 255))
	  lineptr ++;
      }
      else
	lineptr ++;
    }
  }
  else
    maxval = 1;

  if (img->xsize == 0 || img->xsize > CF_IMAGE_MAX_WIDTH ||
      img->ysize == 0 || img->ysize > CF_IMAGE_MAX_HEIGHT)
  {
    DEBUG_printf(("DEBUG: Bad PNM dimensions %dx%d!\n",
		  img->xsize, img->ysize));
    fclose(fp);
    return (1);
  }

  if (maxval == 0)
  {
    DEBUG_printf(("DEBUG: Bad PNM max value %d!\n", maxval));
    fclose(fp);
    return (1);
  }

  if (format == 1 || format == 2 || format == 4 || format == 5)
    img->colorspace = secondary;
  else
    img->colorspace = (primary == CF_IMAGE_RGB_CMYK) ? CF_IMAGE_RGB : primary;

  cfImageSetMaxTiles(img, 0);

  bpp = cfImageGetDepth(img);

  if ((in = malloc(img->xsize * 3)) == NULL)
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
  * Read the image file...
  */

  for (y = 0; y < img->ysize; y ++)
  {
    switch (format)
    {
      case 1 :
          for (x = img->xsize, inptr = in; x > 0; x --, inptr ++)
            if (fscanf(fp, "%d", &val) == 1)
              *inptr = val ? 0 : 255;
          break;

      case 2 :
          for (x = img->xsize, inptr = in; x > 0; x --, inptr ++)
            if (fscanf(fp, "%d", &val) == 1)
              *inptr = 255 * val / maxval;
          break;

      case 3 :
          for (x = img->xsize, inptr = in; x > 0; x --, inptr += 3)
          {
            if (fscanf(fp, "%d", &val) == 1)
              inptr[0] = 255 * val / maxval;
            if (fscanf(fp, "%d", &val) == 1)
              inptr[1] = 255 * val / maxval;
            if (fscanf(fp, "%d", &val) == 1)
              inptr[2] = 255 * val / maxval;
          }
          break;

      case 4 :
          if (fread(out, (img->xsize + 7) / 8, 1, fp) == 0 && ferror(fp))
	    DEBUG_printf(("Error reading file!"));
          for (x = img->xsize, inptr = in, outptr = out, bit = 128;
               x > 0;
               x --, inptr ++)
          {
            if (*outptr & bit)
              *inptr = 0;
            else
              *inptr = 255;

            if (bit > 1)
              bit >>= 1;
            else
            {
              bit = 128;
              outptr ++;
            }
          }
          break;

      case 5 :
          if (fread(in, img->xsize, 1, fp) == 0 && ferror(fp))
	    DEBUG_printf(("Error reading file!"));
          break;

      case 6 :
          if (fread(in, img->xsize, 3, fp) == 0 && ferror(fp))
	    DEBUG_printf(("Error reading file!"));
          break;
    }

    switch (format)
    {
      case 1 :
      case 2 :
      case 4 :
      case 5 :
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
	  break;

      default :
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
  	  break;
    }
  }

  free(in);
  free(out);

  fclose(fp);

  return (0);
}


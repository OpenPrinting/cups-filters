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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * Constants...
 */

#  define CF_SGI_MAGIC		474	/* Magic number in image file */

#  define CF_SGI_COMP_NONE	0	/* No compression */
#  define CF_SGI_COMP_RLE	1	/* Run-length encoding */
#  define CF_SGI_COMP_ARLE	2	/* Agressive run-length encoding */


/*
 * Image structure...
 */

typedef struct
{
  FILE			*file;		/* Image file */
  int			bpp,		/* Bytes per pixel/channel */
			comp;		/* Compression */
  unsigned short	xsize,		/* Width in pixels */
			ysize,		/* Height in pixels */
			zsize;		/* Number of channels */
  long			firstrow,	/* File offset for first row */
			nextrow,	/* File offset for next row */
			**table,	/* Offset table for compression */
			**length;	/* Length table for compression */
  unsigned short	*arle_row;	/* Advanced RLE compression buffer */
  long			arle_offset,	/* Advanced RLE buffer offset */
			arle_length;	/* Advanced RLE buffer length */
} cf_sgi_t;


/*
 * Local functions...
 */

static int	sgi_close(cf_sgi_t *sgip);
static int	sgi_get_row(cf_sgi_t *sgip, unsigned short *row, int y, int z);
static cf_sgi_t	*sgi_open_file(FILE *file, int comp, int bpp,
			       int xsize, int ysize, int zsize);
static int	get_long(FILE *);
static int	get_short(FILE *);
static int	read_rle8(FILE *, unsigned short *, int);
static int	read_rle16(FILE *, unsigned short *, int);


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

  sgip = sgi_open_file(fp, 0, 0, 0, 0, 0);

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
    sgi_close(sgip);
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
    sgi_close(sgip);
    return (1);
  }

  if ((out = malloc(img->xsize * bpp)) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    sgi_close(sgip);
    free(in);
    return (1);
  }

  if ((rows[0] = calloc(img->xsize * sgip->zsize,
                        sizeof(unsigned short))) == NULL)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory!\n");
    sgi_close(sgip);
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
      sgi_get_row(sgip, rows[i], img->ysize - 1 - y, i);

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

  sgi_close(sgip);

  return (0);
}


/*
 * 'sgi_close()' - Close an SGI image file.
 */

static int				/* O - 0 on success, -1 on error */
sgi_close(cf_sgi_t *sgip)		/* I - SGI image */
{
  int	i;				/* Return status */


  if (sgip == NULL)
    return (-1);

  if (sgip->table != NULL)
  {
    free(sgip->table[0]);
    free(sgip->table);
  }

  if (sgip->length != NULL)
  {
    free(sgip->length[0]);
    free(sgip->length);
  }

  if (sgip->comp == CF_SGI_COMP_ARLE)
    free(sgip->arle_row);

  i = fclose(sgip->file);
  free(sgip);

  return (i);
}


/*
 * 'sgi_get_row()' - Get a row of image data from a file.
 */

static int				/* O - 0 on success, -1 on error */
sgi_get_row(cf_sgi_t          *sgip,	/* I - SGI image */
          unsigned short *row,		/* O - Row to read */
          int            y,		/* I - Line to read */
          int            z)		/* I - Channel to read */
{
  int	x;				/* X coordinate */
  long	offset;				/* File offset */


  if (sgip == NULL ||
      row == NULL ||
      y < 0 || y >= sgip->ysize ||
      z < 0 || z >= sgip->zsize)
    return (-1);

  switch (sgip->comp)
  {
    case CF_SGI_COMP_NONE :
       /*
        * Seek to the image row - optimize buffering by only seeking if
        * necessary...
        */

        offset = 512 + (y + z * sgip->ysize) * sgip->xsize * sgip->bpp;
        if (offset != ftell(sgip->file))
          fseek(sgip->file, offset, SEEK_SET);

        if (sgip->bpp == 1)
        {
          for (x = sgip->xsize; x > 0; x --, row ++)
            *row = getc(sgip->file);
        }
        else
        {
          for (x = sgip->xsize; x > 0; x --, row ++)
            *row = get_short(sgip->file);
        }
        break;

    case CF_SGI_COMP_RLE :
        offset = sgip->table[z][y];
        if (offset != ftell(sgip->file))
          fseek(sgip->file, offset, SEEK_SET);

        if (sgip->bpp == 1)
          return (read_rle8(sgip->file, row, sgip->xsize));
        else
          return (read_rle16(sgip->file, row, sgip->xsize));
  }

  return (0);
}


/*
 * 'sgi_open_file()' - Open an SGI image file for reading or writing.
 */

static cf_sgi_t *			/* O - New image */
sgi_open_file(FILE *file,		/* I - File to open */
              int  comp,		/* I - Type of compression */
              int  bpp,			/* I - Bytes per pixel */
              int  xsize,		/* I - Width of image in pixels */
              int  ysize,		/* I - Height of image in pixels */
              int  zsize)		/* I - Number of channels */
{
  int	i, j;				/* Looping var */
  short	magic;				/* Magic number */
  cf_sgi_t	*sgip;			/* New image pointer */


  if ((sgip = calloc(sizeof(cf_sgi_t), 1)) == NULL)
    return (NULL);

  sgip->file = file;

  magic = get_short(sgip->file);
  if (magic != CF_SGI_MAGIC)
  {
    free(sgip);
    return (NULL);
  }

  sgip->comp  = getc(sgip->file);
  sgip->bpp   = getc(sgip->file);
  get_short(sgip->file);		/* Dimensions */
  sgip->xsize = get_short(sgip->file);
  sgip->ysize = get_short(sgip->file);
  sgip->zsize = get_short(sgip->file);
  get_long(sgip->file);		/* Minimum pixel */
  get_long(sgip->file);		/* Maximum pixel */

  if (sgip->comp)
  {
   /*
    * This file is compressed; read the scanline tables...
    */

    fseek(sgip->file, 512, SEEK_SET);

    if ((sgip->table = calloc(sgip->zsize, sizeof(long *))) == NULL)
    {
      free(sgip);
      return (NULL);
    }

    if ((sgip->table[0] = calloc(sgip->ysize * sgip->zsize,
				 sizeof(long))) == NULL)
    {
      free(sgip->table);
      free(sgip);
      return (NULL);
    }

    for (i = 1; i < sgip->zsize; i ++)
      sgip->table[i] = sgip->table[0] + i * sgip->ysize;

    for (i = 0; i < sgip->zsize; i ++)
      for (j = 0; j < sgip->ysize; j ++)
	sgip->table[i][j] = get_long(sgip->file);
  }

  return (sgip);
}


/*
 * 'get_long()' - Get a 32-bit big-endian integer.
 */

static int				/* O - Long value */
get_long(FILE *fp)			/* I - File to read from */
{
  unsigned char	b[4];			/* Bytes from file */


  if (fread(b, 4, 1, fp) == 0 && ferror(fp))
    DEBUG_printf(("Error reading file!"));
  return ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}


/*
 * 'get_short()' - Get a 16-bit big-endian integer.
 */

static int				/* O - Short value */
get_short(FILE *fp)			/* I - File to read from */
{
  unsigned char	b[2];			/* Bytes from file */


  if (fread(b, 2, 1, fp) == 0 && ferror(fp))
    DEBUG_printf(("Error reading file!"));
  return ((b[0] << 8) | b[1]);
}


/*
 * 'read_rle8()' - Read 8-bit RLE data.
 */

static int				/* O - Value on success, -1 on error */
read_rle8(FILE           *fp,		/* I - File to read from */
          unsigned short *row,		/* O - Data */
          int            xsize)		/* I - Width of data in pixels */
{
  int	i,				/* Looping var */
	ch,				/* Current character */
	count,				/* RLE count */
	length;				/* Number of bytes read... */


  length = 0;

  while (xsize > 0)
  {
    if ((ch = getc(fp)) == EOF)
      return (-1);
    length ++;

    count = ch & 127;
    if (count == 0)
      break;

    if (ch & 128)
    {
      for (i = 0; i < count; i ++, row ++, xsize --, length ++)
        if (xsize > 0)
	  *row = getc(fp);
    }
    else
    {
      ch = getc(fp);
      length ++;
      for (i = 0; i < count && xsize > 0; i ++, row ++, xsize --)
        *row = ch;
    }
  }

  return (xsize > 0 ? -1 : length);
}


/*
 * 'read_rle16()' - Read 16-bit RLE data.
 */

static int				/* O - Value on success, -1 on error */
read_rle16(FILE           *fp,		/* I - File to read from */
           unsigned short *row,		/* O - Data */
           int            xsize)	/* I - Width of data in pixels */
{
  int	i,				/* Looping var */
	ch,				/* Current character */
	count,				/* RLE count */
	length;				/* Number of bytes read... */


  length = 0;

  while (xsize > 0)
  {
    if ((ch = get_short(fp)) == EOF)
      return (-1);
    length ++;

    count = ch & 127;
    if (count == 0)
      break;

    if (ch & 128)
    {
      for (i = 0; i < count; i ++, row ++, xsize --, length ++)
        if (xsize > 0)
	  *row = get_short(fp);
    }
    else
    {
      ch = get_short(fp);
      length ++;
      for (i = 0; i < count && xsize > 0; i ++, row ++, xsize --)
	*row = ch;
    }
  }

  return (xsize > 0 ? -1 : length * 2);
}

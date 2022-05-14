/*
 *   Private image library definitions for CUPS Filters.
 *
 *   Copyright 2007-2010 by Apple Inc.
 *   Copyright 1993-2006 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_IMAGE_PRIVATE_H_
#  define _CUPS_IMAGE_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include <config.h>
#  include "image.h"
#  include <cups/cups.h>
#  define DEBUG_printf(x)
#  define DEBUG_puts(x)
#  include <stdlib.h>
#  include <string.h>
#  include <ctype.h>
#  ifdef WIN32
#    include <io.h>
#  else
#    include <unistd.h>
#  endif /* WIN32 */
#  include <errno.h>
#  include <math.h>	

#ifdef HAVE_EXIF
#	include <libexif/exif-data.h>
#endif

/*
 * Constants...
 */

#  define CF_IMAGE_MAX_WIDTH	0x07ffffff
					/* 2^27-1 to allow for 15-channel data */
#  define CF_IMAGE_MAX_HEIGHT	0x3fffffff
					/* 2^30-1 */

#  define CF_TILE_SIZE		256	/* 256x256 pixel tiles */
#  define CF_TILE_MINIMUM	10	/* Minimum number of tiles */


/*
 * min/max/abs macros...
 */

#  ifndef max
#    define 	max(a,b)	((a) > (b) ? (a) : (b))
#  endif /* !max */
#  ifndef min
#    define 	min(a,b)	((a) < (b) ? (a) : (b))
#  endif /* !min */
#  ifndef abs
#    define	abs(a)		((a) < 0 ? -(a) : (a))
#  endif /* !abs */


/*
 * Types and structures...
 */

typedef enum cf_iztype_e		/**** Image zoom type ****/
{
  CF_IZOOM_FAST,			/* Use nearest-neighbor sampling */
  CF_IZOOM_NORMAL,			/* Use bilinear interpolation */
  CF_IZOOM_BEST				/* Use bicubic interpolation */
} cf_iztype_t;

struct cf_ic_s;

typedef struct cf_itile_s		/**** Image tile ****/
{
  int			dirty;		/* True if tile is dirty */
  off_t			pos;		/* Position of tile on disk (-1 if not
					   written) */
  struct cf_ic_s	*ic;		/* Pixel data */
} cf_itile_t;

typedef struct cf_ic_s		/**** Image tile cache ****/
{
  struct cf_ic_s	*prev,		/* Previous tile in cache */
			*next;		/* Next tile in cache */
  cf_itile_t		*tile;		/* Tile this is attached to */
  cf_ib_t		*pixels;	/* Pixel data */
} cf_ic_t;

struct cf_image_s			/**** Image file data ****/
{
  cf_icspace_t		colorspace;	/* Colorspace of image */
  unsigned		xsize,		/* Width of image in pixels */
			ysize,		/* Height of image in pixels */
			xppi,		/* X resolution in pixels-per-inch */
			yppi,		/* Y resolution in pixels-per-inch */
			num_ics,	/* Number of cached tiles */
			max_ics;	/* Maximum number of cached tiles */
  cf_itile_t		**tiles;	/* Tiles in image */
  cf_ic_t		*first,		/* First cached tile in image */
			*last;		/* Last cached tile in image */
  int			cachefile;	/* Tile cache file */
  char			cachename[256];	/* Tile cache filename */
};

struct cf_izoom_s			/**** Image zoom data ****/
{
  cf_image_t		*img;		/* Image to zoom */
  cf_iztype_t		type;		/* Type of zooming */
  unsigned		xorig,		/* X origin */
			yorig,		/* Y origin */
			width,		/* Width of input area */
			height,		/* Height of input area */
			depth,		/* Number of bytes per pixel */
			rotated,	/* Non-zero if image needs to be
					   rotated */
			xsize,		/* Width of output image */
			ysize,		/* Height of output image */
			xmax,		/* Maximum input image X position */
			ymax,		/* Maximum input image Y position */
			xmod,		/* Threshold for Bresenheim rounding */
			ymod;		/* ... */
  int			xstep,		/* Amount to step for each pixel along
					   X */
			xincr,
			instep,		/* Amount to step pixel pointer along
					   X */
			inincr,
			ystep,		/* Amount to step for each pixel along
					   Y */
			yincr,
			row;		/* Current row */
  cf_ib_t		*rows[2],	/* Horizontally scaled pixel data */
			*in;		/* Unscaled input pixel data */
};


/*
 * Prototypes...
 */

extern int		_cfImagePutCol(cf_image_t *img, int x, int y,
				       int height, const cf_ib_t *pixels);
extern int		_cfImagePutRow(cf_image_t *img, int x, int y,
				       int width, const cf_ib_t *pixels);
extern int		_cfImageReadBMP(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadFPX(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadGIF(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadJPEG(cf_image_t *img, FILE *fp,
					 cf_icspace_t primary,
					 cf_icspace_t secondary,
					 int saturation, int hue,
					 const cf_ib_t *lut);
extern int		_cfImageReadPIX(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadPNG(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadPNM(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadPhotoCD(cf_image_t *img, FILE *fp,
					    cf_icspace_t primary,
					    cf_icspace_t secondary,
					    int saturation, int hue,
					    const cf_ib_t *lut);
extern int		_cfImageReadSGI(cf_image_t *img, FILE *fp,
					cf_icspace_t primary,
					cf_icspace_t secondary,
					int saturation, int hue,
					const cf_ib_t *lut);
extern int		_cfImageReadSunRaster(cf_image_t *img, FILE *fp,
					      cf_icspace_t primary,
					      cf_icspace_t secondary,
					      int saturation, int hue,
					      const cf_ib_t *lut);
extern int		_cfImageReadTIFF(cf_image_t *img, FILE *fp,
					 cf_icspace_t primary,
					 cf_icspace_t secondary,
					 int saturation, int hue,
					 const cf_ib_t *lut);
extern void		_cfImageZoomDelete(cf_izoom_t *z);
extern void		_cfImageZoomFill(cf_izoom_t *z, int iy);
extern cf_izoom_t	*_cfImageZoomNew(cf_image_t *img, int xc0, int yc0,
					 int xc1, int yc1, int xsize,
					 int ysize, int rotated,
					 cf_iztype_t type);

#ifdef HAVE_EXIF
int		_cupsImageReadEXIF(cf_image_t *img, FILE *fp);
#endif

#endif /* !_CUPS_IMAGE_PRIVATE_H_ */


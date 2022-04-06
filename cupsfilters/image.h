/*
 *   Image library definitions for CUPS Filters.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2006 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_FILTERS_IMAGE_H_
#  define _CUPS_FILTERS_IMAGE_H_

/*
 * Include necessary headers...
 */

#  include <stdio.h>
#  include <cups/raster.h>

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/*
 * Constants...
 */

typedef enum cf_icspace_e	/**** Image colorspaces ****/
{
  CF_IMAGE_CMYK = -4,		/* Cyan, magenta, yellow, and black */
  CF_IMAGE_CMY = -3,		/* Cyan, magenta, and yellow */
  CF_IMAGE_BLACK = -1,		/* Black */
  CF_IMAGE_WHITE = 1,		/* White (luminance) */
  CF_IMAGE_RGB = 3,		/* Red, green, and blue */
  CF_IMAGE_RGB_CMYK = 4		/* Use RGB or CMYK */
} cf_icspace_t;


/*
 * Types and structures...
 */

typedef unsigned char cf_ib_t;        /**** Image byte ****/

struct cf_image_s;
typedef struct cf_image_s cf_image_t; /**** Image file data ****/

struct cf_izoom_s;
typedef struct cf_izoom_s cf_izoom_t; /**** Image zoom data ****/


/*
 * Prototypes...
 */

extern void		cfImageClose(cf_image_t *img);
extern void		cfImageCMYKToBlack(const cf_ib_t *in,
					   cf_ib_t *out, int count);
extern void		cfImageCMYKToCMY(const cf_ib_t *in,
					 cf_ib_t *out, int count);
extern void		cfImageCMYKToCMYK(const cf_ib_t *in,
					  cf_ib_t *out, int count);
extern void		cfImageCMYKToRGB(const cf_ib_t *in,
					 cf_ib_t *out, int count);
extern void		cfImageCMYKToWhite(const cf_ib_t *in,
					   cf_ib_t *out, int count);
extern int		cfImageGetCol(cf_image_t *img, int x, int y,
				      int height, cf_ib_t *pixels);
extern cf_icspace_t	cfImageGetColorSpace(cf_image_t *img);
extern int		cfImageGetDepth(cf_image_t *img);
extern unsigned		cfImageGetHeight(cf_image_t *img);
extern int		cfImageGetRow(cf_image_t *img, int x, int y,
				      int width, cf_ib_t *pixels);
extern unsigned		cfImageGetWidth(cf_image_t *img);
extern unsigned		cfImageGetXPPI(cf_image_t *img);
extern unsigned		cfImageGetYPPI(cf_image_t *img);
extern void		cfImageLut(cf_ib_t *pixels, int count,
				   const cf_ib_t *lut);
extern cf_image_t	*cfImageOpen(const char *filename,
				     cf_icspace_t primary,
				     cf_icspace_t secondary,
				     int saturation, int hue,
				     const cf_ib_t *lut);
extern cf_image_t	*cfImageOpenFP(FILE *fp,
				       cf_icspace_t primary,
				       cf_icspace_t secondary,
				       int saturation, int hue,
				       const cf_ib_t *lut);
extern void		cfImageRGBAdjust(cf_ib_t *pixels, int count,
					 int saturation, int hue);
extern void		cfImageRGBToBlack(const cf_ib_t *in,
					  cf_ib_t *out, int count);
extern void		cfImageRGBToCMY(const cf_ib_t *in,
					cf_ib_t *out, int count);
extern void		cfImageRGBToCMYK(const cf_ib_t *in,
					 cf_ib_t *out, int count);
extern void		cfImageRGBToRGB(const cf_ib_t *in,
					cf_ib_t *out, int count);
extern void		cfImageRGBToWhite(const cf_ib_t *in,
					  cf_ib_t *out, int count);
extern void		cfImageSetMaxTiles(cf_image_t *img, int max_tiles);
extern void		cfImageSetProfile(float d, float g,
					  float matrix[3][3]);
extern void		cfImageSetRasterColorSpace(cups_cspace_t cs);
extern void		cfImageWhiteToBlack(const cf_ib_t *in,
					    cf_ib_t *out, int count);
extern void		cfImageWhiteToCMY(const cf_ib_t *in,
					  cf_ib_t *out, int count);
extern void		cfImageWhiteToCMYK(const cf_ib_t *in,
					   cf_ib_t *out, int count);
extern void		cfImageWhiteToRGB(const cf_ib_t *in,
					  cf_ib_t *out, int count);
extern void		cfImageWhiteToWhite(const cf_ib_t *in,
					    cf_ib_t *out, int count);
extern cf_image_t* 	cfImageCrop(cf_image_t* img,int posw,
				    int posh,int width,int height);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_IMAGE_H_ */


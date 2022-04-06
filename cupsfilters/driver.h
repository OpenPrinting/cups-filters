/*
 *   Printer driver utilities header file for CUPS.
 *
 *   Copyright 2007 by Apple Inc.
 *   Copyright 1993-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_FILTERS_DRIVER_H_
#  define _CUPS_FILTERS_DRIVER_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/*
 * Include necessary headers...
 */

#  include <stdio.h>
#  include <stdlib.h>
#  include <time.h>
#  include <math.h>
#  include "filter.h"
#  include <ppd/ppd.h>

#  if defined(WIN32) || defined(__EMX__)
#    include <io.h>
#  else
#    include <unistd.h>
#    include <fcntl.h>
#  endif /* WIN32 || __EMX__ */

#  include <cups/cups.h>
#  include <cups/raster.h>


/*
 * Common macros...
 */

#  ifndef min
#    define min(a,b)	((a) < (b) ? (a) : (b))
#    define max(a,b)	((a) > (b) ? (a) : (b))
#  endif /* !min */


/*
 * Constants...
 */

#define CF_MAX_CHAN	15		/* Maximum number of color components */
#define CF_MAX_LUT	4095		/* Maximum LUT value */
#define CF_MAX_RGB	4		/* Maximum number of sRGB components */


/*
 * Types/structures for the various routines.
 */

typedef struct cf_lut_s			/**** Lookup Table for Dithering ****/
{
  short		intensity;		/* Adjusted intensity */
  short		pixel;			/* Output pixel value */
  int		error;			/* Error from desired value */
} cf_lut_t;

typedef struct cf_dither_s		/**** Dithering State ****/
{
  int		width;			/* Width of buffer */
  int		row;			/* Current row */
  int		errors[96];		/* Error values */
} cf_dither_t;

typedef struct cf_sample_s		/**** Color sample point ****/
{
  unsigned char	rgb[3];			/* sRGB values */
  unsigned char	colors[CF_MAX_RGB];	/* Color values */
} cf_sample_t;

typedef struct cf_rgb_s			/*** Color separation lookup table ***/
{
  int		cube_size;		/* Size of color cube (2-N) on a side */
  int		num_channels;		/* Number of colors per sample */
  unsigned char	****colors;		/* 4-D array of sample values */
  int		cube_index[256];	/* Index into cube for a given sRGB value */
  int		cube_mult[256];		/* Multiplier value for a given sRGB value */
  int		cache_init;		/* Are cached values initialized? */
  unsigned char	black[CF_MAX_RGB];	/* Cached black (sRGB = 0,0,0) */
  unsigned char	white[CF_MAX_RGB];	/* Cached white (sRGB = 255,255,255) */
} cf_rgb_t;

typedef struct cf_cmyk_s		/**** Simple CMYK lookup table ****/
{
  unsigned char	black_lut[256];		/* Black generation LUT */
  unsigned char	color_lut[256];		/* Color removal LUT */
  int		ink_limit;		/* Ink limit */
  int		num_channels;		/* Number of components */
  short		*channels[CF_MAX_CHAN];
					/* Lookup tables */
} cf_cmyk_t;


/*
 * Globals...
 */

extern const unsigned char
			cf_srgb_lut[256];
					/* sRGB gamma lookup table */
extern const unsigned char
			cf_scmy_lut[256];
					/* sRGB gamma lookup table (inverted) */


/*
 * Prototypes...
 */

/*
 * Attribute function...
 */

extern ppd_attr_t	*cfFindAttr(ppd_file_t *ppd, const char *name,
				    const char *colormodel,
				    const char *media,
				    const char *resolution,
				    char *spec, int specsize,
				    cf_logfunc_t log,
				    void *ld);
			       
/*
 * Byte checking functions...
 */

extern int		cfCheckBytes(const unsigned char *, int);
extern int		cfCheckValue(const unsigned char *, int,
				     const unsigned char);

/*
 * Dithering functions...
 */

extern void		cfDitherLine(cf_dither_t *d, const cf_lut_t *lut,
				     const short *data, int num_channels,
				     unsigned char *p);
extern cf_dither_t	*cfDitherNew(int width);
extern void		cfDitherDelete(cf_dither_t *);

/*
 * Lookup table functions for dithering...
 */

extern cf_lut_t		*cfLutNew(int num_vals, const float *vals,
				  cf_logfunc_t log, void *ld);
extern void		cfLutDelete(cf_lut_t *lut);
extern cf_lut_t		*cfLutLoad(ppd_file_t *ppd,
				   const char *colormodel,
				   const char *media,
				   const char *resolution,
				   const char *ink,
				   cf_logfunc_t log,
				   void *ld);


/*
 * Bit packing functions...
 */

extern void		cfPackHorizontal(const unsigned char *,
					 unsigned char *, int,
					 const unsigned char, const int);
  extern void		cfPackHorizontal2(const unsigned char *,
					  unsigned char *, int, const int);
extern void		cfPackHorizontalBit(const unsigned char *,
					    unsigned char *, int,
					    const unsigned char,
					    const unsigned char);
extern void		cfPackVertical(const unsigned char *, unsigned char *,
				       int, const unsigned char, const int);

/*
 * Color separation functions...
 */

extern void		cfRGBDelete(cf_rgb_t *rgb);
extern void		cfRGBDoGray(cf_rgb_t *rgb,
				    const unsigned char *input,
				    unsigned char *output, int num_pixels);
extern void		cfRGBDoRGB(cf_rgb_t *rgb,
				   const unsigned char *input,
				   unsigned char *output, int num_pixels);
extern cf_rgb_t		*cfRGBLoad(ppd_file_t *ppd,
				   const char *colormodel,
				   const char *media,
				   const char *resolution,
				   cf_logfunc_t log,
				   void *ld);
extern cf_rgb_t		*cfRGBNew(int num_samples, cf_sample_t *samples,
				  int cube_size, int num_channels);

/*
 * CMYK separation functions...
 */

extern cf_cmyk_t	*cfCMYKNew(int num_channels);
extern void		cfCMYKDelete(cf_cmyk_t *cmyk);
extern void		cfCMYKDoBlack(const cf_cmyk_t *cmyk,
				      const unsigned char *input,
				      short *output, int num_pixels);
extern void		cfCMYKDoCMYK(const cf_cmyk_t *cmyk,
				     const unsigned char *input,
				     short *output, int num_pixels);
extern void		cfCMYKDoGray(const cf_cmyk_t *cmyk,
				     const unsigned char *input,
				     short *output, int num_pixels);
extern void		cfCMYKDoRGB(const cf_cmyk_t *cmyk,
				    const unsigned char *input,
				    short *output, int num_pixels);
extern cf_cmyk_t	*cfCMYKLoad(ppd_file_t *ppd,
				    const char *colormodel,
				    const char *media,
				    const char *resolution,
				    cf_logfunc_t log,
				    void *ld);
  extern void		cfCMYKSetBlack(cf_cmyk_t *cmyk,
				       float lower, float upper,
				       cf_logfunc_t log, void *ld);
extern void		cfCMYKSetCurve(cf_cmyk_t *cmyk, int channel,
				       int num_xypoints,
				       const float *xypoints,
				       cf_logfunc_t log, void *ld);
extern void		cfCMYKSetGamma(cf_cmyk_t *cmyk, int channel,
				       float gamval, float density,
				       cf_logfunc_t log, void *ld);
extern void		cfCMYKSetInkLimit(cf_cmyk_t *cmyk, float limit);
  extern void		cfCMYKSetLtDk(cf_cmyk_t *cmyk, int channel,
				      float light, float dark,
				      cf_logfunc_t log, void *ld);


/*
 * Convenience macro for writing print data...
 */

#  define cfWritePrintData(s,n) fwrite((s), 1, (n), stdout)

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_DRIVER_H_ */

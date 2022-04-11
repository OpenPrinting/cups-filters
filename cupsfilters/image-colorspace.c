/*
 *   Colorspace conversions for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2006 by Easy Software Products.
 *
 *   The color saturation/hue matrix stuff is provided thanks to Mr. Paul
 *   Haeberli at "http://www.sgi.com/grafica/matrix/index.html".
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   cfImageCMYKToBlack()         - Convert CMYK data to black.
 *   cfImageCMYKToCMY()           - Convert CMYK colors to CMY.
 *   cfImageCMYKToCMYK()          - Convert CMYK colors to CMYK.
 *   cfImageCMYKToRGB()           - Convert CMYK colors to device-dependent
 *                                    RGB.
 *   cfImageCMYKToWhite()         - Convert CMYK colors to luminance.
 *   cfImageLut()                 - Adjust all pixel values with the given
 *                                    LUT.
 *   cfImageRGBAdjust()           - Adjust the hue and saturation of the
 *                                    given RGB colors.
 *   cfImageRGBToBlack()          - Convert RGB data to black.
 *   cfImageRGBToCMY()            - Convert RGB colors to CMY.
 *   cfImageRGBToCMYK()           - Convert RGB colors to CMYK.
 *   cfImageRGBToRGB()            - Convert RGB colors to device-dependent
 *                                    RGB.
 *   cfImageRGBToWhite()          - Convert RGB colors to luminance.
 *   cfImageSetProfile()          - Set the device color profile.
 *   cfImageSetRasterColorSpace() - Set the destination colorspace.
 *   cfImageWhiteToBlack()        - Convert luminance colors to black.
 *   cfImageWhiteToCMY()          - Convert luminance colors to CMY.
 *   cfImageWhiteToCMYK()         - Convert luminance colors to CMYK.
 *   cfImageWhiteToRGB()          - Convert luminance data to RGB.
 *   cfImageWhiteToWhite()        - Convert luminance colors to device-
 *                                  dependent luminance.
 *   cie_lab()                    - Map CIE Lab transformation...
 *   hue_rotate()                 - Rotate the hue, maintaining luminance.
 *   ident()                      - Make an identity matrix.
 *   mult()                       - Multiply two matrices.
 *   rgb_to_lab()                 - Convert an RGB color to CIE Lab.
 *   rgb_to_xyz()                 - Convert an RGB color to CIE XYZ.
 *   saturate()                   - Make a saturation matrix.
 *   x_form()                     - Transform a 3D point using a matrix...
 *   x_rotate()                   - Rotate about the x (red) axis...
 *   y_rotate()                   - Rotate about the y (green) axis...
 *   z_rotate()                   - Rotate about the z (blue) axis...
 *   z_shear()                    - Shear z using x and y...
 */

/*
 * Include necessary headers...
 */

#include "image-private.h"


/*
 * Define some math constants that are required...
 */

#ifndef M_PI
#  define M_PI		3.14159265358979323846
#endif /* !M_PI */

#ifndef M_SQRT2
#  define M_SQRT2	1.41421356237309504880
#endif /* !M_SQRT2 */

#ifndef M_SQRT1_2
#  define M_SQRT1_2	0.70710678118654752440
#endif /* !M_SQRT1_2 */

/*
 * CIE XYZ whitepoint...
 */

#define D65_X	(0.412453 + 0.357580 + 0.180423)
#define D65_Y	(0.212671 + 0.715160 + 0.072169)
#define D65_Z	(0.019334 + 0.119193 + 0.950227)


/*
 * Lookup table structure...
 */

typedef int cups_clut_t[3][256];


/*
 * Local globals...
 */

static int		cfImageHaveProfile = 0;
					/* Do we have a color profile? */
static int		*cfImageDensity;
					/* Ink/marker density LUT */
static cups_clut_t	*cfImageMatrix;
					/* Color transform matrix LUT */
static cups_cspace_t	cfImageColorSpace = CUPS_CSPACE_RGB;
					/* Destination colorspace */


/*
 * Local functions...
 */

static float	cie_lab(float x, float xn);
static void	hue_rotate(float [3][3], float);
static void	ident(float [3][3]);
static void	mult(float [3][3], float [3][3], float [3][3]);
static void	rgb_to_lab(cf_ib_t *val);
static void	rgb_to_xyz(cf_ib_t *val);
static void	saturate(float [3][3], float);
static void	x_form(float [3][3], float, float, float, float *, float *, float *);
static void	x_rotate(float [3][3], float, float);
static void	y_rotate(float [3][3], float, float);
static void	z_rotate(float [3][3], float, float);
static void	z_shear(float [3][3], float, float);


/*
 * 'cfImageCMYKToBlack()' - Convert CMYK data to black.
 */

void
cfImageCMYKToBlack(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	k;				/* Black value */


  if (cfImageHaveProfile)
    while (count > 0)
    {
      k = (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100 + in[3];

      if (k < 255)
        *out++ = cfImageDensity[k];
      else
        *out++ = cfImageDensity[255];

      in += 4;
      count --;
    }
  else
    while (count > 0)
    {
      k = (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100 + in[3];

      if (k < 255)
        *out++ = k;
      else
        *out++ = 255;

      in += 4;
      count --;
    }
}


/*
 * 'cfImageCMYKToCMY()' - Convert CMYK colors to CMY.
 */

void
cfImageCMYKToCMY(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	c, m, y, k;			/* CMYK values */
  int	cc, cm, cy;			/* Calibrated CMY values */


  if (cfImageHaveProfile)
    while (count > 0)
    {
      c = *in++;
      m = *in++;
      y = *in++;
      k = *in++;

      cc = cfImageMatrix[0][0][c] +
           cfImageMatrix[0][1][m] +
	   cfImageMatrix[0][2][y] + k;
      cm = cfImageMatrix[1][0][c] +
           cfImageMatrix[1][1][m] +
	   cfImageMatrix[1][2][y] + k;
      cy = cfImageMatrix[2][0][c] +
           cfImageMatrix[2][1][m] +
	   cfImageMatrix[2][2][y] + k;

      if (cc < 0)
        *out++ = 0;
      else if (cc > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cc];

      if (cm < 0)
        *out++ = 0;
      else if (cm > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cm];

      if (cy < 0)
        *out++ = 0;
      else if (cy > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cy];

      count --;
    }
  else
    while (count > 0)
    {
      c = *in++;
      m = *in++;
      y = *in++;
      k = *in++;

      c += k;
      m += k;
      y += k;

      if (c < 255)
        *out++ = c;
      else
        *out++ = 255;

      if (m < 255)
        *out++ = y;
      else
        *out++ = 255;

      if (y < 255)
        *out++ = y;
      else
        *out++ = 255;

      count --;
    }
}


/*
 * 'cfImageCMYKToCMYK()' - Convert CMYK colors to CMYK.
 */

void
cfImageCMYKToCMYK(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	c, m, y, k;			/* CMYK values */
  int	cc, cm, cy;			/* Calibrated CMY values */


  if (cfImageHaveProfile)
    while (count > 0)
    {
      c = *in++;
      m = *in++;
      y = *in++;
      k = *in++;

      cc = (cfImageMatrix[0][0][c] +
            cfImageMatrix[0][1][m] +
	    cfImageMatrix[0][2][y]);
      cm = (cfImageMatrix[1][0][c] +
            cfImageMatrix[1][1][m] +
	    cfImageMatrix[1][2][y]);
      cy = (cfImageMatrix[2][0][c] +
            cfImageMatrix[2][1][m] +
	    cfImageMatrix[2][2][y]);

      if (cc < 0)
        *out++ = 0;
      else if (cc > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cc];

      if (cm < 0)
        *out++ = 0;
      else if (cm > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cm];

      if (cy < 0)
        *out++ = 0;
      else if (cy > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cy];

      *out++ = cfImageDensity[k];

      count --;
    }
  else if (in != out)
  {
    while (count > 0)
    {
      *out++ = *in++;
      *out++ = *in++;
      *out++ = *in++;
      *out++ = *in++;

      count --;
    }
  }
}


/*
 * 'cfImageCMYKToRGB()' - Convert CMYK colors to device-dependent RGB.
 */

void
cfImageCMYKToRGB(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	c, m, y, k;			/* CMYK values */
  int	cr, cg, cb;			/* Calibrated RGB values */


  if (cfImageHaveProfile)
  {
    while (count > 0)
    {
      c = *in++;
      m = *in++;
      y = *in++;
      k = *in++;

      cr = cfImageMatrix[0][0][c] +
           cfImageMatrix[0][1][m] +
           cfImageMatrix[0][2][y] + k;
      cg = cfImageMatrix[1][0][c] +
           cfImageMatrix[1][1][m] +
	   cfImageMatrix[1][2][y] + k;
      cb = cfImageMatrix[2][0][c] +
           cfImageMatrix[2][1][m] +
	   cfImageMatrix[2][2][y] + k;

      if (cr < 0)
        *out++ = 255;
      else if (cr > 255)
        *out++ = 255 - cfImageDensity[255];
      else
        *out++ = 255 - cfImageDensity[cr];

      if (cg < 0)
        *out++ = 255;
      else if (cg > 255)
        *out++ = 255 - cfImageDensity[255];
      else
        *out++ = 255 - cfImageDensity[cg];

      if (cb < 0)
        *out++ = 255;
      else if (cb > 255)
        *out++ = 255 - cfImageDensity[255];
      else
        *out++ = 255 - cfImageDensity[cb];

      count --;
    }
  }
  else
  {
    while (count > 0)
    {
      c = 255 - *in++;
      m = 255 - *in++;
      y = 255 - *in++;
      k = *in++;

      c -= k;
      m -= k;
      y -= k;

      if (c > 0)
	*out++ = c;
      else
        *out++ = 0;

      if (m > 0)
	*out++ = m;
      else
        *out++ = 0;

      if (y > 0)
	*out++ = y;
      else
        *out++ = 0;

      if (cfImageColorSpace == CUPS_CSPACE_CIELab ||
          cfImageColorSpace >= CUPS_CSPACE_ICC1)
        rgb_to_lab(out - 3);
      else if (cfImageColorSpace == CUPS_CSPACE_CIEXYZ)
        rgb_to_xyz(out - 3);

      count --;
    }
  }
}


/*
 * 'cfImageCMYKToWhite()' - Convert CMYK colors to luminance.
 */

void
cfImageCMYKToWhite(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	w;				/* White value */


  if (cfImageHaveProfile)
  {
    while (count > 0)
    {
      w = 255 - (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100 - in[3];

      if (w > 0)
        *out++ = cfImageDensity[w];
      else
        *out++ = cfImageDensity[0];

      in += 4;
      count --;
    }
  }
  else
  {
    while (count > 0)
    {
      w = 255 - (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100 - in[3];

      if (w > 0)
        *out++ = w;
      else
        *out++ = 0;

      in += 4;
      count --;
    }
  }
}


/*
 * 'cfImageLut()' - Adjust all pixel values with the given LUT.
 */

void
cfImageLut(cf_ib_t       *pixels,	/* IO - Input/output pixels */
             int             count,	/* I  - Number of pixels/bytes to adjust */
             const cf_ib_t *lut)	/* I  - Lookup table */
{
  while (count > 0)
  {
    *pixels = lut[*pixels];
    pixels ++;
    count --;
  }
}


/*
 * 'cfImageRGBAdjust()' - Adjust the hue and saturation of the given RGB colors.
 */

void
cfImageRGBAdjust(cf_ib_t *pixels,	/* IO - Input/output pixels */
        	   int       count,	/* I - Number of pixels to adjust */
        	   int       saturation,/* I - Color saturation (%) */
        	   int       hue)	/* I - Color hue (degrees) */
{
  int			i, j, k;	/* Looping vars */
  float			mat[3][3];	/* Color adjustment matrix */
  static int		last_sat = 100,	/* Last saturation used */
			last_hue = 0;	/* Last hue used */
  static cups_clut_t	*lut = NULL;	/* Lookup table for matrix */


  if (saturation != last_sat || hue != last_hue || !lut)
  {
   /*
    * Build the color adjustment matrix...
    */

    ident(mat);
    saturate(mat, saturation * 0.01);
    hue_rotate(mat, (float)hue);

   /*
    * Allocate memory for the lookup table...
    */

    if (lut == NULL)
      lut = calloc(3, sizeof(cups_clut_t));

    if (lut == NULL)
      return;

   /*
    * Convert the matrix into a 3x3 array of lookup tables...
    */

    for (i = 0; i < 3; i ++)
      for (j = 0; j < 3; j ++)
        for (k = 0; k < 256; k ++)
          lut[i][j][k] = mat[i][j] * k + 0.5;

   /*
    * Save the saturation and hue to compare later...
    */

    last_sat = saturation;
    last_hue = hue;
  }

 /*
  * Adjust each pixel in the given buffer.
  */

  while (count > 0)
  {
    i = lut[0][0][pixels[0]] +
        lut[1][0][pixels[1]] +
        lut[2][0][pixels[2]];
    if (i < 0)
      pixels[0] = 0;
    else if (i > 255)
      pixels[0] = 255;
    else
      pixels[0] = i;

    i = lut[0][1][pixels[0]] +
        lut[1][1][pixels[1]] +
        lut[2][1][pixels[2]];
    if (i < 0)
      pixels[1] = 0;
    else if (i > 255)
      pixels[1] = 255;
    else
      pixels[1] = i;

    i = lut[0][2][pixels[0]] +
        lut[1][2][pixels[1]] +
        lut[2][2][pixels[2]];
    if (i < 0)
      pixels[2] = 0;
    else if (i > 255)
      pixels[2] = 255;
    else
      pixels[2] = i;

    count --;
    pixels += 3;
  }
}


/*
 * 'cfImageRGBToBlack()' - Convert RGB data to black.
 */

void
cfImageRGBToBlack(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
    while (count > 0)
    {
      *out++ = cfImageDensity[255 - (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100];
      in += 3;
      count --;
    }
  else
    while (count > 0)
    {
      *out++ = 255 - (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100;
      in += 3;
      count --;
    }
}


/*
 * 'cfImageRGBToCMY()' - Convert RGB colors to CMY.
 */

void
cfImageRGBToCMY(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	c, m, y, k;			/* CMYK values */
  int	cc, cm, cy;			/* Calibrated CMY values */


  if (cfImageHaveProfile)
    while (count > 0)
    {
      c = 255 - *in++;
      m = 255 - *in++;
      y = 255 - *in++;
      k = min(c, min(m, y));
      c -= k;
      m -= k;
      y -= k;

      cc = cfImageMatrix[0][0][c] +
           cfImageMatrix[0][1][m] +
	   cfImageMatrix[0][2][y] + k;
      cm = cfImageMatrix[1][0][c] +
           cfImageMatrix[1][1][m] +
	   cfImageMatrix[1][2][y] + k;
      cy = cfImageMatrix[2][0][c] +
           cfImageMatrix[2][1][m] +
	   cfImageMatrix[2][2][y] + k;

      if (cc < 0)
        *out++ = 0;
      else if (cc > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cc];

      if (cm < 0)
        *out++ = 0;
      else if (cm > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cm];

      if (cy < 0)
        *out++ = 0;
      else if (cy > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cy];

      count --;
    }
  else
    while (count > 0)
    {
      c    = 255 - in[0];
      m    = 255 - in[1];
      y    = 255 - in[2];
      k    = min(c, min(m, y));

      *out++ = (255 - in[1] / 4) * (c - k) / 255 + k;
      *out++ = (255 - in[2] / 4) * (m - k) / 255 + k;
      *out++ = (255 - in[0] / 4) * (y - k) / 255 + k;
      in += 3;
      count --;
    }
}


/*
 * 'cfImageRGBToCMYK()' - Convert RGB colors to CMYK.
 */

void
cfImageRGBToCMYK(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	c, m, y, k,			/* CMYK values */
	km;				/* Maximum K value */
  int	cc, cm, cy;			/* Calibrated CMY values */


  if (cfImageHaveProfile)
    while (count > 0)
    {
      c = 255 - *in++;
      m = 255 - *in++;
      y = 255 - *in++;
      k = min(c, min(m, y));

      if ((km = max(c, max(m, y))) > k)
        k = k * k * k / (km * km);

      c -= k;
      m -= k;
      y -= k;

      cc = (cfImageMatrix[0][0][c] +
            cfImageMatrix[0][1][m] +
	    cfImageMatrix[0][2][y]);
      cm = (cfImageMatrix[1][0][c] +
            cfImageMatrix[1][1][m] +
	    cfImageMatrix[1][2][y]);
      cy = (cfImageMatrix[2][0][c] +
            cfImageMatrix[2][1][m] +
	    cfImageMatrix[2][2][y]);

      if (cc < 0)
        *out++ = 0;
      else if (cc > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cc];

      if (cm < 0)
        *out++ = 0;
      else if (cm > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cm];

      if (cy < 0)
        *out++ = 0;
      else if (cy > 255)
        *out++ = cfImageDensity[255];
      else
        *out++ = cfImageDensity[cy];

      *out++ = cfImageDensity[k];

      count --;
    }
  else
    while (count > 0)
    {
      c = 255 - *in++;
      m = 255 - *in++;
      y = 255 - *in++;
      k = min(c, min(m, y));

      if ((km = max(c, max(m, y))) > k)
        k = k * k * k / (km * km);

      c -= k;
      m -= k;
      y -= k;

      *out++ = c;
      *out++ = m;
      *out++ = y;
      *out++ = k;

      count --;
    }
}


/*
 * 'cfImageRGBToRGB()' - Convert RGB colors to device-dependent RGB.
 */

void
cfImageRGBToRGB(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  int	c, m, y, k;			/* CMYK values */
  int	cr, cg, cb;			/* Calibrated RGB values */


  if (cfImageHaveProfile)
  {
    while (count > 0)
    {
      c = 255 - *in++;
      m = 255 - *in++;
      y = 255 - *in++;
      k = min(c, min(m, y));
      c -= k;
      m -= k;
      y -= k;

      cr = cfImageMatrix[0][0][c] +
           cfImageMatrix[0][1][m] +
           cfImageMatrix[0][2][y] + k;
      cg = cfImageMatrix[1][0][c] +
           cfImageMatrix[1][1][m] +
	   cfImageMatrix[1][2][y] + k;
      cb = cfImageMatrix[2][0][c] +
           cfImageMatrix[2][1][m] +
	   cfImageMatrix[2][2][y] + k;

      if (cr < 0)
        *out++ = 255;
      else if (cr > 255)
        *out++ = 255 - cfImageDensity[255];
      else
        *out++ = 255 - cfImageDensity[cr];

      if (cg < 0)
        *out++ = 255;
      else if (cg > 255)
        *out++ = 255 - cfImageDensity[255];
      else
        *out++ = 255 - cfImageDensity[cg];

      if (cb < 0)
        *out++ = 255;
      else if (cb > 255)
        *out++ = 255 - cfImageDensity[255];
      else
        *out++ = 255 - cfImageDensity[cb];

      count --;
    }
  }
  else
  {
    if (in != out)
      memcpy(out, in, count * 3);

    if (cfImageColorSpace == CUPS_CSPACE_CIELab ||
        cfImageColorSpace >= CUPS_CSPACE_ICC1)
    {
      while (count > 0)
      {
        rgb_to_lab(out);

	out += 3;
	count --;
      }
    }
    else if (cfImageColorSpace == CUPS_CSPACE_CIEXYZ)
    {
      while (count > 0)
      {
        rgb_to_xyz(out);

	out += 3;
	count --;
      }
    }
  }
}


/*
 * 'cfImageRGBToWhite()' - Convert RGB colors to luminance.
 */

void
cfImageRGBToWhite(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
  {
    while (count > 0)
    {
      *out++ = 255 - cfImageDensity[255 - (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100];
      in += 3;
      count --;
    }
  }
  else
  {
    while (count > 0)
    {
      *out++ = (31 * in[0] + 61 * in[1] + 8 * in[2]) / 100;
      in += 3;
      count --;
    }
  }
}


/*
 * 'cfImageSetProfile()' - Set the device color profile.
 */

void
cfImageSetProfile(float d,		/* I - Ink/marker density */
                    float g,		/* I - Ink/marker gamma */
                    float matrix[3][3])	/* I - Color transform matrix */
{
  int	i, j, k;			/* Looping vars */
  float	m;				/* Current matrix value */
  int	*im;				/* Pointer into cfImageMatrix */


 /*
  * Allocate memory for the profile data...
  */

  if (cfImageMatrix == NULL)
    cfImageMatrix = calloc(3, sizeof(cups_clut_t));

  if (cfImageMatrix == NULL)
    return;

  if (cfImageDensity == NULL)
    cfImageDensity = calloc(256, sizeof(int));

  if (cfImageDensity == NULL)
    return;

 /*
  * Populate the profile lookup tables...
  */

  cfImageHaveProfile  = 1;

  for (i = 0, im = cfImageMatrix[0][0]; i < 3; i ++)
    for (j = 0; j < 3; j ++)
      for (k = 0, m = matrix[i][j]; k < 256; k ++)
        *im++ = (int)(k * m + 0.5);

  for (k = 0, im = cfImageDensity; k < 256; k ++)
    *im++ = 255.0 * d * pow((float)k / 255.0, g) + 0.5;
}


/*
 * 'cfImageSetRasterColorSpace()' - Set the destination colorspace.
 */

void
cfImageSetRasterColorSpace(
    cups_cspace_t cs)			/* I - Destination colorspace */
{
 /*
  * Set the destination colorspace...
  */

  cfImageColorSpace = cs;

 /*
  * Don't use color profiles in colorimetric colorspaces...
  */

  if (cs == CUPS_CSPACE_CIEXYZ ||
      cs == CUPS_CSPACE_CIELab ||
      cs >= CUPS_CSPACE_ICC1)
    cfImageHaveProfile = 0;
}


/*
 * 'cfImageWhiteToBlack()' - Convert luminance colors to black.
 */

void
cfImageWhiteToBlack(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
    while (count > 0)
    {
      *out++ = cfImageDensity[255 - *in++];
      count --;
    }
  else
    while (count > 0)
    {
      *out++ = 255 - *in++;
      count --;
    }
}


/*
 * 'cfImageWhiteToCMY()' - Convert luminance colors to CMY.
 */

void
cfImageWhiteToCMY(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
    while (count > 0)
    {
      out[0] = cfImageDensity[255 - *in++];
      out[1] = out[0];
      out[2] = out[0];
      out += 3;
      count --;
    }
  else
    while (count > 0)
    {
      *out++ = 255 - *in;
      *out++ = 255 - *in;
      *out++ = 255 - *in++;
      count --;
    }
}


/*
 * 'cfImageWhiteToCMYK()' - Convert luminance colors to CMYK.
 */

void
cfImageWhiteToCMYK(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
    while (count > 0)
    {
      *out++ = 0;
      *out++ = 0;
      *out++ = 0;
      *out++ = cfImageDensity[255 - *in++];
      count --;
    }
  else
    while (count > 0)
    {
      *out++ = 0;
      *out++ = 0;
      *out++ = 0;
      *out++ = 255 - *in++;
      count --;
    }
}


/*
 * 'cfImageWhiteToRGB()' - Convert luminance data to RGB.
 */

void
cfImageWhiteToRGB(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
  {
    while (count > 0)
    {
      out[0] = 255 - cfImageDensity[255 - *in++];
      out[1] = out[0];
      out[2] = out[0];
      out += 3;
      count --;
    }
  }
  else
  {
    while (count > 0)
    {
      *out++ = *in;
      *out++ = *in;
      *out++ = *in++;

      if (cfImageColorSpace == CUPS_CSPACE_CIELab ||
          cfImageColorSpace >= CUPS_CSPACE_ICC1)
        rgb_to_lab(out - 3);
      else if (cfImageColorSpace == CUPS_CSPACE_CIEXYZ)
        rgb_to_xyz(out - 3);

      count --;
    }
  }
}


/*
 * 'cfImageWhiteToWhite()' - Convert luminance colors to device-dependent
 *                             luminance.
 */

void
cfImageWhiteToWhite(
    const cf_ib_t *in,		/* I - Input pixels */
    cf_ib_t       *out,		/* I - Output pixels */
    int             count)		/* I - Number of pixels */
{
  if (cfImageHaveProfile)
    while (count > 0)
    {
      *out++ = 255 - cfImageDensity[255 - *in++];
      count --;
    }
  else if (in != out)
    memcpy(out, in, count);
}


/*
 * 'cie_lab()' - Map CIE Lab transformation...
 */

static float				/* O - Adjusted color value */
cie_lab(float x,				/* I - Raw color value */
       float xn)			/* I - Whitepoint color value */
{
  float x_xn;				/* Fraction of whitepoint */


  x_xn = x / xn;

  if (x_xn > 0.008856)
    return (cbrt(x_xn));
  else
    return (7.787 * x_xn + 16.0 / 116.0);
}


/* 
 * 'hue_rotate()' - Rotate the hue, maintaining luminance.
 */

static void
hue_rotate(float mat[3][3],		/* I - Matrix to append to */
          float rot)			/* I - Hue rotation in degrees */
{
  float hmat[3][3];			/* Hue matrix */
  float lx, ly, lz;			/* Luminance vector */
  float xrs, xrc;			/* X rotation sine/cosine */
  float yrs, yrc;			/* Y rotation sine/cosine */
  float zrs, zrc;			/* Z rotation sine/cosine */
  float zsx, zsy;			/* Z shear x/y */


 /*
  * Load the identity matrix...
  */

  ident(hmat);

 /*
  * Rotate the grey vector into positive Z...
  */

  xrs = M_SQRT1_2;
  xrc = M_SQRT1_2;
  x_rotate(hmat,xrs,xrc);

  yrs = -1.0 / sqrt(3.0);
  yrc = -M_SQRT2 * yrs;
  y_rotate(hmat,yrs,yrc);

 /*
  * Shear the space to make the luminance plane horizontal...
  */

  x_form(hmat, 0.3086, 0.6094, 0.0820, &lx, &ly, &lz);
  zsx = lx / lz;
  zsy = ly / lz;
  z_shear(hmat, zsx, zsy);

 /*
  * Rotate the hue...
  */

  zrs = sin(rot * M_PI / 180.0);
  zrc = cos(rot * M_PI / 180.0);

  z_rotate(hmat, zrs, zrc);

 /*
  * Unshear the space to put the luminance plane back...
  */

  z_shear(hmat, -zsx, -zsy);

 /*
  * Rotate the grey vector back into place...
  */

  y_rotate(hmat, -yrs, yrc);
  x_rotate(hmat, -xrs, xrc);

 /*
  * Append it to the current matrix...
  */

  mult(hmat, mat, mat);
}


/* 
 * 'ident()' - Make an identity matrix.
 */

static void
ident(float mat[3][3])			/* I - Matrix to identify */
{
  mat[0][0] = 1.0;
  mat[0][1] = 0.0;
  mat[0][2] = 0.0;
  mat[1][0] = 0.0;
  mat[1][1] = 1.0;
  mat[1][2] = 0.0;
  mat[2][0] = 0.0;
  mat[2][1] = 0.0;
  mat[2][2] = 1.0;
}


/* 
 * 'mult()' - Multiply two matrices.
 */

static void
mult(float a[3][3],			/* I - First matrix */
     float b[3][3],			/* I - Second matrix */
     float c[3][3])			/* I - Destination matrix */
{
  int	x, y;				/* Looping vars */
  float	temp[3][3];			/* Temporary matrix */


 /*
  * Multiply a and b, putting the result in temp...
  */

  for (y = 0; y < 3; y ++)
    for (x = 0; x < 3; x ++)
      temp[y][x] = b[y][0] * a[0][x] +
                   b[y][1] * a[1][x] +
                   b[y][2] * a[2][x];

 /*
  * Copy temp to c (that way c can be a pointer to a or b).
  */

  memcpy(c, temp, sizeof(temp));
}


/*
 * 'rgb_to_lab()' - Convert an RGB color to CIE Lab.
 */

static void
rgb_to_lab(cf_ib_t *val)		/* IO - Color value */
{
  float	r,				/* Red value */
	g,				/* Green value */
	b,				/* Blue value */
	ciex,				/* CIE X value */
	ciey,				/* CIE Y value */
	ciez,				/* CIE Z value */
	ciey_yn,			/* Normalized luminance */
	ciel,				/* CIE L value */
	ciea,				/* CIE a value */
	cieb;				/* CIE b value */


 /*
  * Convert sRGB to linear RGB...
  */

  r = pow((val[0] / 255.0 + 0.055) / 1.055, 2.4);
  g = pow((val[1] / 255.0 + 0.055) / 1.055, 2.4);
  b = pow((val[2] / 255.0 + 0.055) / 1.055, 2.4);

 /*
  * Convert to CIE XYZ...
  */

  ciex = 0.412453 * r + 0.357580 * g + 0.180423 * b; 
  ciey = 0.212671 * r + 0.715160 * g + 0.072169 * b;
  ciez = 0.019334 * r + 0.119193 * g + 0.950227 * b;

 /*
  * Normalize and convert to CIE Lab...
  */

  ciey_yn = ciey / D65_Y;

  if (ciey_yn > 0.008856)
    ciel = 116 * cbrt(ciey_yn) - 16;
  else
    ciel = 903.3 * ciey_yn;

/*ciel = ciel;*/
  ciea = 500 * (cie_lab(ciex, D65_X) - cie_lab(ciey, D65_Y));
  cieb = 200 * (cie_lab(ciey, D65_Y) - cie_lab(ciez, D65_Z));

 /*
  * Scale the L value and bias the a and b values by 128 so that all
  * numbers are from 0 to 255.
  */

  ciel = ciel * 2.55 + 0.5;
  ciea += 128.5;
  cieb += 128.5;

 /*
  * Output 8-bit values...
  */

  if (ciel < 0.0)
    val[0] = 0;
  else if (ciel < 255.0)
    val[0] = (int)ciel;
  else
    val[0] = 255;

  if (ciea < 0.0)
    val[1] = 0;
  else if (ciea < 255.0)
    val[1] = (int)ciea;
  else
    val[1] = 255;

  if (cieb < 0.0)
    val[2] = 0;
  else if (cieb < 255.0)
    val[2] = (int)cieb;
  else
    val[2] = 255;
}


/*
 * 'rgb_to_xyz()' - Convert an RGB color to CIE XYZ.
 */

static void
rgb_to_xyz(cf_ib_t *val)		/* IO - Color value */
{
  float	r,				/* Red value */
	g,				/* Green value */
	b,				/* Blue value */
	ciex,				/* CIE X value */
	ciey,				/* CIE Y value */
	ciez;				/* CIE Z value */


 /*
  * Convert sRGB to linear RGB...
  */

  r = pow((val[0] / 255.0 + 0.055) / 1.055, 2.4);
  g = pow((val[1] / 255.0 + 0.055) / 1.055, 2.4);
  b = pow((val[2] / 255.0 + 0.055) / 1.055, 2.4);

 /*
  * Convert to CIE XYZ...
  */

  ciex = 0.412453 * r + 0.357580 * g + 0.180423 * b; 
  ciey = 0.212671 * r + 0.715160 * g + 0.072169 * b;
  ciez = 0.019334 * r + 0.119193 * g + 0.950227 * b;

 /*
  * Encode as 8-bit XYZ...
  */

  if (ciex < 0.0f)
    val[0] = 0;
  else if (ciex < 1.1f)
    val[0] = (int)(231.8181f * ciex + 0.5);
  else
    val[0] = 255;

  if (ciey < 0.0f)
    val[1] = 0;
  else if (ciey < 1.1f)
    val[1] = (int)(231.8181f * ciey + 0.5);
  else
    val[1] = 255;

  if (ciez < 0.0f)
    val[2] = 0;
  else if (ciez < 1.1f)
    val[2] = (int)(231.8181f * ciez + 0.5);
  else
    val[2] = 255;
}


/* 
 * 'saturate()' - Make a saturation matrix.
 */

static void
saturate(float mat[3][3],		/* I - Matrix to append to */
         float sat)			/* I - Desired color saturation */
{
  float	smat[3][3];			/* Saturation matrix */


  smat[0][0] = (1.0 - sat) * 0.3086 + sat;
  smat[0][1] = (1.0 - sat) * 0.3086;
  smat[0][2] = (1.0 - sat) * 0.3086;
  smat[1][0] = (1.0 - sat) * 0.6094;
  smat[1][1] = (1.0 - sat) * 0.6094 + sat;
  smat[1][2] = (1.0 - sat) * 0.6094;
  smat[2][0] = (1.0 - sat) * 0.0820;
  smat[2][1] = (1.0 - sat) * 0.0820;
  smat[2][2] = (1.0 - sat) * 0.0820 + sat;

  mult(smat, mat, mat);
}


/* 
 * 'x_form()' - Transform a 3D point using a matrix...
 */

static void
x_form(float mat[3][3],			/* I - Matrix */
      float x,				/* I - Input X coordinate */
      float y,				/* I - Input Y coordinate */
      float z,				/* I - Input Z coordinate */
      float *tx,			/* O - Output X coordinate */
      float *ty,			/* O - Output Y coordinate */
      float *tz)			/* O - Output Z coordinate */
{
  *tx = x * mat[0][0] + y * mat[1][0] + z * mat[2][0];
  *ty = x * mat[0][1] + y * mat[1][1] + z * mat[2][1];
  *tz = x * mat[0][2] + y * mat[1][2] + z * mat[2][2];
}


/* 
 * 'x_rotate()' - Rotate about the x (red) axis...
 */

static void
x_rotate(float mat[3][3],		/* I - Matrix */
        float rs,			/* I - Rotation angle sine */
        float rc)			/* I - Rotation angle cosine */
{
  float rmat[3][3];			/* I - Rotation matrix */


  rmat[0][0] = 1.0;
  rmat[0][1] = 0.0;
  rmat[0][2] = 0.0;

  rmat[1][0] = 0.0;
  rmat[1][1] = rc;
  rmat[1][2] = rs;

  rmat[2][0] = 0.0;
  rmat[2][1] = -rs;
  rmat[2][2] = rc;

  mult(rmat, mat, mat);
}


/* 
 * 'y_rotate()' - Rotate about the y (green) axis...
 */

static void
y_rotate(float mat[3][3],		/* I - Matrix */
        float rs,			/* I - Rotation angle sine */
        float rc)			/* I - Rotation angle cosine */
{
  float rmat[3][3];			/* I - Rotation matrix */


  rmat[0][0] = rc;
  rmat[0][1] = 0.0;
  rmat[0][2] = -rs;

  rmat[1][0] = 0.0;
  rmat[1][1] = 1.0;
  rmat[1][2] = 0.0;

  rmat[2][0] = rs;
  rmat[2][1] = 0.0;
  rmat[2][2] = rc;

  mult(rmat,mat,mat);
}


/* 
 * 'z_rotate()' - Rotate about the z (blue) axis...
 */

static void
z_rotate(float mat[3][3],		/* I - Matrix */
        float rs,			/* I - Rotation angle sine */
        float rc)			/* I - Rotation angle cosine */
{
  float rmat[3][3];			/* I - Rotation matrix */


  rmat[0][0] = rc;
  rmat[0][1] = rs;
  rmat[0][2] = 0.0;

  rmat[1][0] = -rs;
  rmat[1][1] = rc;
  rmat[1][2] = 0.0;

  rmat[2][0] = 0.0;
  rmat[2][1] = 0.0;
  rmat[2][2] = 1.0;

  mult(rmat,mat,mat);
}


/* 
 * 'z_shear()' - Shear z using x and y...
 */

static void
z_shear(float mat[3][3],			/* I - Matrix */
       float dx,			/* I - X shear */
       float dy)			/* I - Y shear */
{
  float smat[3][3];			/* Shear matrix */


  smat[0][0] = 1.0;
  smat[0][1] = 0.0;
  smat[0][2] = dx;

  smat[1][0] = 0.0;
  smat[1][1] = 1.0;
  smat[1][2] = dy;

  smat[2][0] = 0.0;
  smat[2][1] = 0.0;
  smat[2][2] = 1.0;

  mult(smat, mat, mat);
}


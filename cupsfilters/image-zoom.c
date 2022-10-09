//
//   Image zoom routines for libcupsfilters.
//
//   Copyright 2007-2011 by Apple Inc.
//   Copyright 1993-2006 by Easy Software Products.
//
//   These coded instructions, statements, and computer programs are the
//   property of Apple Inc. and are protected by Federal copyright
//   law.  Distribution and use rights are outlined in the file "COPYING"
//   which should have been included with this file.
//
// Contents:
//
//   _cfImageZoomDelete()   - Free a zoom record...
//   _cfImageZoomFill()     - Fill a zoom record...
//   _cfImageZoomNew()      - Allocate a pixel zoom record...
//   zoom_bilinear()        - Fill a zoom record with image data utilizing
//                            bilinear interpolation.
//   zoom_nearest()         - Fill a zoom record quickly using nearest-neighbor
//                            sampling.

//
// Include necessary headers...
//

#include "image-private.h"


//
// Local functions...
//

static void	zoom_bilinear(cf_izoom_t *z, int iy);
static void	zoom_nearest(cf_izoom_t *z, int iy);


//
// '_cfImageZoomDelete()' - Free a zoom record...
//

void
_cfImageZoomDelete(cf_izoom_t *z)	// I - Zoom record to free
{
  free(z->rows[0]);
  free(z->rows[1]);
  free(z->in);
  free(z);
}


//
// '_cfImageZoomFill()' - Fill a zoom record with image data utilizing bilinear
//                        interpolation.
//

void
_cfImageZoomFill(cf_izoom_t *z,		// I - Zoom record to fill
		 int        iy)		// I - Zoom image row
{
  switch (z->type)
  {
    case CF_IZOOM_FAST :
        zoom_nearest(z, iy);
	break;

    default :
        zoom_bilinear(z, iy);
	break;
  }
}


//
// '_cfImageZoomNew()' - Allocate a pixel zoom record...
//

cf_izoom_t *
_cfImageZoomNew(
    cf_image_t    *img,			// I - Image to zoom
    int           xc0,			// I - Upper-lefthand corner
    int           yc0,			// I - ...
    int           xc1,			// I - Lower-righthand corner
    int           yc1,			// I - ...
    int           xsize,		// I - Final width of image
    int           ysize,		// I - Final height of image
    int           rotated,		// I - Non-zero if image is rotated 90
                                        //     degrees
    cf_iztype_t type)			// I - Zoom type
{
  cf_izoom_t	*z;			// New zoom record
  int		flip;			// Flip on X axis?


  if (xsize > CF_IMAGE_MAX_WIDTH ||
      ysize > CF_IMAGE_MAX_HEIGHT ||
      (xc1 - xc0) > CF_IMAGE_MAX_WIDTH ||
      (yc1 - yc0) > CF_IMAGE_MAX_HEIGHT)
    return (NULL);		// Protect against integer overflow

  if ((z = (cf_izoom_t *)calloc(1, sizeof(cf_izoom_t))) == NULL)
    return (NULL);

  z->img     = img;
  z->row     = 0;
  z->depth   = cfImageGetDepth(img);
  z->rotated = rotated;
  z->type    = type;

  if (xsize < 0)
  {
    flip  = 1;
    xsize = -xsize;
  }
  else
  {
    flip  = 0;
  }

  if (rotated)
  {
    z->xorig   = xc1;
    z->yorig   = yc0;
    z->width   = yc1 - yc0 + 1;
    z->height  = xc1 - xc0 + 1;
    z->xsize   = xsize;
    z->ysize   = ysize;
    z->xmod    = z->width % z->xsize;
    z->xstep   = z->width / z->xsize;
    z->xincr   = 1;
    z->ymod    = z->height % z->ysize;
    z->ystep   = z->height / z->ysize;
    z->yincr   = 1;
    z->instep  = z->xstep * z->depth;
    z->inincr  = /* z->xincr * */ z->depth; // z->xincr is always 1

    if (z->width < img->ysize)
      z->xmax = z->width;
    else
      z->xmax = z->width - 1;

    if (z->height < img->xsize)
      z->ymax = z->height;
    else
      z->ymax = z->height - 1;
  }
  else
  {
    z->xorig   = xc0;
    z->yorig   = yc0;
    z->width   = xc1 - xc0 + 1;
    z->height  = yc1 - yc0 + 1;
    z->xsize   = xsize;
    z->ysize   = ysize;
    z->xmod    = z->width % z->xsize;
    z->xstep   = z->width / z->xsize;
    z->xincr   = 1;
    z->ymod    = z->height % z->ysize;
    z->ystep   = z->height / z->ysize;
    z->yincr   = 1;
    z->instep  = z->xstep * z->depth;
    z->inincr  = /* z->xincr * */ z->depth; // z->xincr is always 1

    if (z->width < img->xsize)
      z->xmax = z->width;
    else
      z->xmax = z->width - 1;

    if (z->height < img->ysize)
      z->ymax = z->height;
    else
      z->ymax = z->height - 1;
  }

  if (flip)
  {
    z->instep = -z->instep;
    z->inincr = -z->inincr;
  }

  if ((z->rows[0] = (cf_ib_t *)malloc(z->xsize * z->depth)) == NULL)
  {
    free(z);
    return (NULL);
  }

  if ((z->rows[1] = (cf_ib_t *)malloc(z->xsize * z->depth)) == NULL)
  {
    free(z->rows[0]);
    free(z);
    return (NULL);
  }

  if ((z->in = (cf_ib_t *)malloc(z->width * z->depth)) == NULL)
  {
    free(z->rows[0]);
    free(z->rows[1]);
    free(z);
    return (NULL);
  }

  return (z);
}


//
// 'zoom_bilinear()' - Fill a zoom record with image data utilizing bilinear
//                     interpolation.
//

static void
zoom_bilinear(cf_izoom_t   *z,		// I - Zoom record to fill
              int          iy)		// I - Zoom image row
{
  cf_ib_t	*r,			// Row pointer
		*inptr;			// Pixel pointer
  int		xerr0,			// X error counter
		xerr1;			// ...
  int		ix,
		x,
		count,
		z_depth,
		z_xstep,
		z_xincr,
		z_instep,
		z_inincr,
		z_xmax,
		z_xmod,
		z_xsize;


  if (iy > z->ymax)
    iy = z->ymax;

  z->row ^= 1;

  z_depth  = z->depth;
  z_xsize  = z->xsize;
  z_xmax   = z->xmax;
  z_xmod   = z->xmod;
  z_xstep  = z->xstep;
  z_xincr  = z->xincr;
  z_instep = z->instep;
  z_inincr = z->inincr;

  if (z->rotated)
    cfImageGetCol(z->img, z->xorig - iy, z->yorig, z->width, z->in);
  else
    cfImageGetRow(z->img, z->xorig, z->yorig + iy, z->width, z->in);

  if (z_inincr < 0)
    inptr = z->in + (z->width - 1) * z_depth;
  else
    inptr = z->in;

  for (x = z_xsize, xerr0 = z_xsize, xerr1 = 0, ix = 0, r = z->rows[z->row];
       x > 0;
       x --)
  {
    if (ix < z_xmax)
    {
      for (count = 0; count < z_depth; count ++)
        *r++ = (inptr[count] * xerr0 + inptr[z_depth + count] * xerr1) / z_xsize;
    }
    else
    {
      for (count = 0; count < z_depth; count ++)
        *r++ = inptr[count];
    }

    ix    += z_xstep;
    inptr += z_instep;
    xerr0 -= z_xmod;
    xerr1 += z_xmod;

    if (xerr0 <= 0)
    {
      xerr0 += z_xsize;
      xerr1 -= z_xsize;
      ix    += z_xincr;
      inptr += z_inincr;
    }
  }
}


//
// 'zoom_nearest()' - Fill a zoom record quickly using nearest-neighbor
//                    sampling.
//

static void
zoom_nearest(cf_izoom_t   *z,		// I - Zoom record to fill
             int          iy)		// I - Zoom image row
{
  cf_ib_t	*r,			// Row pointer
		*inptr;			// Pixel pointer
  int		xerr0;			// X error counter
  int		ix,
		x,
		count,
		z_depth,
		z_xstep,
		z_xincr,
		z_instep,
		z_inincr,
		z_xmod,
		z_xsize;


  if (iy > z->ymax)
    iy = z->ymax;

  z->row ^= 1;

  z_depth  = z->depth;
  z_xsize  = z->xsize;
  z_xmod   = z->xmod;
  z_xstep  = z->xstep;
  z_xincr  = z->xincr;
  z_instep = z->instep;
  z_inincr = z->inincr;

  if (z->rotated)
    cfImageGetCol(z->img, z->xorig - iy, z->yorig, z->width, z->in);
  else
    cfImageGetRow(z->img, z->xorig, z->yorig + iy, z->width, z->in);

  if (z_inincr < 0)
    inptr = z->in + (z->width - 1) * z_depth;
  else
    inptr = z->in;

  for (x = z_xsize, xerr0 = z_xsize, ix = 0, r = z->rows[z->row];
       x > 0;
       x --)
  {
    for (count = 0; count < z_depth; count ++)
      *r++ = inptr[count];

    ix    += z_xstep;
    inptr += z_instep;
    xerr0 -= z_xmod;

    if (xerr0 <= 0)
    {
      xerr0 += z_xsize;
      ix    += z_xincr;
      inptr += z_inincr;
    }
  }
}

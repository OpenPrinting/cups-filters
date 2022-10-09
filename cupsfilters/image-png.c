//
//   PNG image routines for CUPS.
//
//   Copyright 2007-2011 by Apple Inc.
//   Copyright 1993-2007 by Easy Software Products.
//
//   These coded instructions, statements, and computer programs are the
//   property of Apple Inc. and are protected by Federal copyright
//   law.  Distribution and use rights are outlined in the file "COPYING"
//   which should have been included with this file.
//
// Contents:
//
//   _cfImageReadPNG() - Read a PNG image file.
//

//
// Include necessary headers...
//

#include "image-private.h"

#if defined(HAVE_LIBPNG) && defined(HAVE_LIBZ)
#  include <png.h>	// Portable Network Graphics (PNG) definitions


//
// '_cfImageReadPNG()' - Read a PNG image file.
//

int					// O - Read status
_cfImageReadPNG(
    cf_image_t      *img,		// IO - Image
    FILE            *fp,		// I - Image file
    cf_icspace_t    primary,		// I - Primary choice for colorspace
    cf_icspace_t    secondary,		// I - Secondary choice for colorspace
    int             saturation,		// I - Color saturation (%)
    int             hue,		// I - Color hue (degrees)
    const cf_ib_t   *lut)		// I - Lookup table for gamma/brightness
{
  int		y;			// Looping var
  png_structp	pp;			// PNG read pointer
  png_infop	info;			// PNG info pointers
  png_uint_32	width,			// Width of image
		height;			// Height of image
  int		bit_depth,		// Bit depth
		color_type,		// Color type
		interlace_type,		// Interlace type
		compression_type,	// Compression type
		filter_type;		// Filter type
  png_uint_32	xppm,			// X pixels per meter
		yppm;			// Y pixels per meter
  int		bpp;			// Bytes per pixel
  int		pass,			// Current pass
		passes;			// Number of passes required
  cf_ib_t	*in,			// Input pixels
		*inptr,			// Pointer into pixels
		*out;			// Output pixels
  png_color_16	bg;			// Background color


  //
  // Setup the PNG data structures...
  //

  pp   = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = png_create_info_struct(pp);

  //
  // Initialize the PNG read "engine"...
  //

  png_init_io(pp, fp);

  //
  // Get the image dimensions and load the output image...
  //

  png_read_info(pp, info);

  png_get_IHDR(pp, info, &width, &height, &bit_depth, &color_type,
               &interlace_type, &compression_type, &filter_type);

  DEBUG_printf(("DEBUG: PNG image: %dx%dx%d, color_type=%x (%s%s%s)\n",
		(int)width, (int)height, bit_depth, color_type,
		(color_type & PNG_COLOR_MASK_COLOR) ? "RGB" : "GRAYSCALE",
		(color_type & PNG_COLOR_MASK_ALPHA) ? "+ALPHA" : "",
		(color_type & PNG_COLOR_MASK_PALETTE) ? "+PALETTE" : ""));

  if (color_type & PNG_COLOR_MASK_PALETTE)
    png_set_expand(pp);
  else if (bit_depth < 8)
  {
    png_set_packing(pp);
    png_set_expand(pp);
  }
  else if (bit_depth == 16)
    png_set_strip_16(pp);

  if (color_type & PNG_COLOR_MASK_COLOR)
    img->colorspace = (primary == CF_IMAGE_RGB_CMYK) ? CF_IMAGE_RGB :
                                                         primary;
  else
    img->colorspace = secondary;

  if (width == 0 || width > CF_IMAGE_MAX_WIDTH ||
      height == 0 || height > CF_IMAGE_MAX_HEIGHT)
  {
    DEBUG_printf(("DEBUG: PNG image has invalid dimensions %ux%u!\n",
		  (unsigned)width, (unsigned)height));
    fclose(fp);
    return (1);
  }

  img->xsize = width;
  img->ysize = height;

  
  int temp = -1;

#ifdef HAVE_EXIF
  //
  // Scan image file for EXIF data
  //

  temp = _cfImageReadEXIF(img, fp);
#endif

  //
  // Check headers only if EXIF contains no info about ppi
  //

  if (temp != 1 && (xppm = png_get_x_pixels_per_meter(pp, info)) != 0 &&
      (yppm = png_get_y_pixels_per_meter(pp, info)) != 0)
  {
    img->xppi = (int)((float)xppm * 0.0254);
    img->yppi = (int)((float)yppm * 0.0254);

    if (img->xppi == 0 || img->yppi == 0)
    {
      DEBUG_printf(("DEBUG: PNG image has invalid resolution %dx%d PPI\n",
		    img->xppi, img->yppi));

      img->xppi = img->yppi = 200;
    }
  }

  cfImageSetMaxTiles(img, 0);

  passes = png_set_interlace_handling(pp);

  //
  // Handle transparency...
  //

  if (png_get_valid(pp, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(pp);

  bg.red   = 65535;
  bg.green = 65535;
  bg.blue  = 65535;

  png_set_background(pp, &bg, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);

  if (passes == 1)
  {
    //
    // Load one row at a time...
    //

    if (color_type == PNG_COLOR_TYPE_GRAY ||
	color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
      in = malloc(img->xsize);
    else
      in = malloc(img->xsize * 3);
  }
  else
  {
    //
    // Interlaced images must be loaded all at once...
    //

    size_t bufsize;			// Size of buffer


    if (color_type == PNG_COLOR_TYPE_GRAY ||
	color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
      bufsize = img->xsize * img->ysize;

      if ((bufsize / img->xsize) != img->ysize)
      {
	DEBUG_printf(("DEBUG: PNG image dimensions (%ux%u) too large!\n",
		      (unsigned)width, (unsigned)height));
	fclose(fp);
	return (1);
      }
    }
    else
    {
      bufsize = img->xsize * img->ysize * 3;

      if ((bufsize / (img->xsize * 3)) != img->ysize)
      {
	DEBUG_printf(("DEBUG: PNG image dimensions (%ux%u) too large!\n",
		      (unsigned)width, (unsigned)height));
	fclose(fp);
	return (1);
      }
    }

    in = malloc(bufsize);
  }

  bpp = cfImageGetDepth(img);
  out = malloc(img->xsize * bpp);

  if (!in || !out)
  {
    DEBUG_puts("DEBUG: Unable to allocate memory for PNG image!\n");

    if (in)
      free(in);

    if (out)
      free(out);

    fclose(fp);

    return (1);
  }

  //
  // Read the image, interlacing as needed...
  //

  for (pass = 1; pass <= passes; pass ++)
    for (inptr = in, y = 0; y < img->ysize; y ++)
    {
      png_read_row(pp, (png_bytep)inptr, NULL);

      if (pass == passes)
      {
	//
	// Output this row...
	//

	if (color_type & PNG_COLOR_MASK_COLOR)
	{
	  if ((saturation != 100 || hue != 0) && bpp > 1)
	    cfImageRGBAdjust(inptr, img->xsize, saturation, hue);

	  switch (img->colorspace)
	  {
	    case CF_IMAGE_WHITE :
		cfImageRGBToWhite(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_RGB :
	    case CF_IMAGE_RGB_CMYK :
		cfImageRGBToRGB(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_BLACK :
		cfImageRGBToBlack(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_CMY :
		cfImageRGBToCMY(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_CMYK :
		cfImageRGBToCMYK(inptr, out, img->xsize);
		break;
	  }
	}
	else
	{
	  switch (img->colorspace)
	  {
	    case CF_IMAGE_WHITE :
		memcpy(out, inptr, img->xsize);
		break;
	    case CF_IMAGE_RGB :
	    case CF_IMAGE_RGB_CMYK :
		cfImageWhiteToRGB(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_BLACK :
		cfImageWhiteToBlack(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_CMY :
		cfImageWhiteToCMY(inptr, out, img->xsize);
		break;
	    case CF_IMAGE_CMYK :
		cfImageWhiteToCMYK(inptr, out, img->xsize);
		break;
	  }
	}

	if (lut)
	  cfImageLut(out, img->xsize * bpp, lut);

	_cfImagePutRow(img, 0, y, img->xsize, out);
      }

      if (passes > 1)
      {
	if (color_type & PNG_COLOR_MASK_COLOR)
          inptr += img->xsize * 3;
	else
          inptr += img->xsize;
      }
    }

  png_read_end(pp, info);
  png_destroy_read_struct(&pp, &info, NULL);

  fclose(fp);
  free(in);
  free(out);

  return (0);
}
#endif // HAVE_LIBPNG && HAVE_LIBZ

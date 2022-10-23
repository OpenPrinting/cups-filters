//
// JPEG image routines for libcupsfilters.
//
// Copyright 2007-2011 by Apple Inc.
// Copyright 1993-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Contents:
//
//   _cfImageReadJPEG() - Read a JPEG image file.
//

//
// Include necessary headers...
//

#include "image-private.h"

#ifdef HAVE_LIBJPEG
#  include <jpeglib.h> // JPEG/JFIF image definitions

#define JPEG_APP0 0xE0 // APP0 marker code

//
// '_cfImageReadJPEG()' - Read a JPEG image file.
//

int					// O  - Read status
_cfImageReadJPEG(
    cf_image_t      *img,		// IO - Image
    FILE            *fp,		// I  - Image file
    cf_icspace_t    primary,		// I  - Primary choice for colorspace
    cf_icspace_t    secondary,		// I  - Secondary choice for colorspace
    int             saturation,		// I  - Color saturation (%)
    int             hue,		// I  - Color hue (degrees)
    const cf_ib_t   *lut)		// I  - Lookup table for
                                        //      gamma/brightness
{
  struct jpeg_decompress_struct	cinfo;	// Decompressor info
  struct jpeg_error_mgr	jerr;		// Error handler info
  cf_ib_t		*in,		// Input pixels
			*out;		// Output pixels
  jpeg_saved_marker_ptr	marker;		// Pointer to marker data
  int			psjpeg = 0;	// Non-zero if Photoshop CMYK JPEG
  static const char	*cspaces[] =
			{		// JPEG colorspaces...
			  "JCS_UNKNOWN",
			  "JCS_GRAYSCALE",
			  "JCS_RGB",
			  "JCS_YCbCr",
			  "JCS_CMYK",
			  "JCS_YCCK"
			};

  (void)cspaces;

  //
  // Read the JPEG header...
  //

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_save_markers(&cinfo, JPEG_APP0 + 14, 0xffff); // Adobe JPEG
  jpeg_stdio_src(&cinfo, fp);
  jpeg_read_header(&cinfo, 1);

  //
  // Parse any Adobe APPE data embedded in the JPEG file.  Since Adobe doesn't
  // bother following standards, we have to invert the CMYK JPEG data written by
  // Adobe apps...
  //

  for (marker = cinfo.marker_list; marker; marker = marker->next)
    if (marker->marker == (JPEG_APP0 + 14) && marker->data_length >= 12 &&
        !memcmp(marker->data, "Adobe", 5))
    {
      DEBUG_puts("DEBUG: Adobe CMYK JPEG detected (inverting color values)\n");
      psjpeg = 1;
    }

  cinfo.quantize_colors = 0;

  DEBUG_printf(("DEBUG: num_components = %d\n", cinfo.num_components));
  DEBUG_printf(("DEBUG: jpeg_color_space = %s\n",
		cspaces[cinfo.jpeg_color_space]));

  if (cinfo.num_components == 1)
  {
    DEBUG_puts("DEBUG: Converting image to grayscale...\n");

    cinfo.out_color_space      = JCS_GRAYSCALE;
    cinfo.out_color_components = 1;
    cinfo.output_components    = 1;

    img->colorspace = secondary;
  }
  else if (cinfo.num_components == 4)
  {
    DEBUG_puts("DEBUG: Converting image to CMYK...\n");

    cinfo.out_color_space      = JCS_CMYK;
    cinfo.out_color_components = 4;
    cinfo.output_components    = 4;

    img->colorspace = (primary == CF_IMAGE_RGB_CMYK) ? CF_IMAGE_CMYK : primary;
  }
  else
  {
    DEBUG_puts("DEBUG: Converting image to RGB...\n");

    cinfo.out_color_space      = JCS_RGB;
    cinfo.out_color_components = 3;
    cinfo.output_components    = 3;

    img->colorspace = (primary == CF_IMAGE_RGB_CMYK) ? CF_IMAGE_RGB : primary;
  }

  jpeg_calc_output_dimensions(&cinfo);

  if (cinfo.output_width <= 0 || cinfo.output_width > CF_IMAGE_MAX_WIDTH ||
      cinfo.output_height <= 0 || cinfo.output_height > CF_IMAGE_MAX_HEIGHT)
  {
    DEBUG_printf(("DEBUG: Bad JPEG dimensions %dx%d!\n",
		  cinfo.output_width, cinfo.output_height));

    jpeg_destroy_decompress(&cinfo);

    fclose(fp);
    return (1);
  }

  img->xsize      = cinfo.output_width;
  img->ysize      = cinfo.output_height;
  
  int temp = -1;

#ifdef HAVE_EXIF
  //
  // Scan image file for exif data
  //

  temp = _cfImageReadEXIF(img, fp);
#endif

  //
  // Check headers only if EXIF contains no info about ppi
  //

  if (temp != 1 && cinfo.X_density > 0 && cinfo.Y_density > 0 && cinfo.density_unit > 0)
  {
    if (cinfo.density_unit == 1)
    {
      img->xppi = cinfo.X_density;
      img->yppi = cinfo.Y_density;
    }
    else
    {
      img->xppi = (int)((float)cinfo.X_density * 2.54);
      img->yppi = (int)((float)cinfo.Y_density * 2.54);
    }

    if (img->xppi == 0 || img->yppi == 0)
    {
      DEBUG_printf(("DEBUG: Bad JPEG image resolution %dx%d PPI.\n",
		    img->xppi, img->yppi));
      img->xppi = img->yppi = 200;
    }
  }

  DEBUG_printf(("DEBUG: JPEG image %dx%dx%d, %dx%d PPI\n",
		img->xsize, img->ysize, cinfo.output_components,
		img->xppi, img->yppi));

  cfImageSetMaxTiles(img, 0);

  in  = malloc(img->xsize * cinfo.output_components);
  out = malloc(img->xsize * cfImageGetDepth(img));

  jpeg_start_decompress(&cinfo);

  while (cinfo.output_scanline < cinfo.output_height)
  {
    jpeg_read_scanlines(&cinfo, (JSAMPROW *)&in, (JDIMENSION)1);

    if (psjpeg && cinfo.output_components == 4)
    {
     //
     // Invert CMYK data from Photoshop...
     

      cf_ib_t	*ptr;	// Pointer into buffer
      int	i;	// Looping var


      for (ptr = in, i = img->xsize * 4; i > 0; i --, ptr ++)
        *ptr = 255 - *ptr;
    }

    if ((saturation != 100 || hue != 0) && cinfo.output_components == 3)
      cfImageRGBAdjust(in, img->xsize, saturation, hue);

    if ((img->colorspace == CF_IMAGE_WHITE && cinfo.out_color_space == JCS_GRAYSCALE) ||
	(img->colorspace == CF_IMAGE_CMYK && cinfo.out_color_space == JCS_CMYK))
    {
#ifdef DEBUG
      int	i, j;
      cf_ib_t	*ptr;


      DEBUG_puts("DEBUG: Direct Data...\n");

      DEBUG_puts("DEBUG:");

      for (i = 0, ptr = in; i < img->xsize; i ++)
      {
        DEBUG_puts(" ");
	for (j = 0; j < cinfo.output_components; j ++, ptr ++)
	  DEBUG_printf(("%02X", *ptr & 255));
      }

      DEBUG_puts("\n");
#endif // DEBUG

      if (lut)
        cfImageLut(in, img->xsize * cfImageGetDepth(img), lut);

      _cfImagePutRow(img, 0, cinfo.output_scanline - 1, img->xsize, in);
    }
    else if (cinfo.out_color_space == JCS_GRAYSCALE)
    {
      switch (img->colorspace)
      {
        default :
	    break;

        case CF_IMAGE_BLACK :
            cfImageWhiteToBlack(in, out, img->xsize);
            break;
        case CF_IMAGE_RGB :
            cfImageWhiteToRGB(in, out, img->xsize);
            break;
        case CF_IMAGE_CMY :
            cfImageWhiteToCMY(in, out, img->xsize);
            break;
        case CF_IMAGE_CMYK :
            cfImageWhiteToCMYK(in, out, img->xsize);
            break;
      }

      if (lut)
        cfImageLut(out, img->xsize * cfImageGetDepth(img), lut);

      _cfImagePutRow(img, 0, cinfo.output_scanline - 1, img->xsize, out);
    }
    else if (cinfo.out_color_space == JCS_RGB)
    {
      switch (img->colorspace)
      {
        default :
	    break;

        case CF_IMAGE_RGB :
            cfImageRGBToRGB(in, out, img->xsize);
	    break;
        case CF_IMAGE_WHITE :
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
        cfImageLut(out, img->xsize * cfImageGetDepth(img), lut);

      _cfImagePutRow(img, 0, cinfo.output_scanline - 1, img->xsize, out);
    }
    else // JCS_CMYK
    {
      DEBUG_puts("DEBUG: JCS_CMYK\n");

      switch (img->colorspace)
      {
        default :
	    break;

        case CF_IMAGE_WHITE :
            cfImageCMYKToWhite(in, out, img->xsize);
            break;
        case CF_IMAGE_BLACK :
            cfImageCMYKToBlack(in, out, img->xsize);
            break;
        case CF_IMAGE_CMY :
            cfImageCMYKToCMY(in, out, img->xsize);
            break;
        case CF_IMAGE_RGB :
            cfImageCMYKToRGB(in, out, img->xsize);
            break;
      }

      if (lut)
        cfImageLut(out, img->xsize * cfImageGetDepth(img), lut);

      _cfImagePutRow(img, 0, cinfo.output_scanline - 1, img->xsize, out);
    }
  }

  free(in);
  free(out);

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  fclose(fp);

  return (0);
}
#endif // HAVE_LIBJPEG

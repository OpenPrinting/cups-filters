//
// Lookup table routines for libcupsfilters.
//
// Copyright 2007 by Apple Inc.
// Copyright 1993-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Contents:
//
//   cfLutDelete() - Free the memory used by a lookup table.
//   cfLutNew()    - Make a lookup table from a list of pixel values.
//

//
// Include necessary headers.
//

#include "driver.h"
#include <math.h>


//
// 'cfLutDelete()' - Free the memory used by a lookup table.
//

void
cfLutDelete(cf_lut_t *lut)		// I - Lookup table to free
{
  if (lut != NULL)
    free(lut);
}


//
// 'cfLutNew()' - Make a lookup table from a list of pixel values.
//
// Returns a pointer to the lookup table on success, NULL on failure.
//

cf_lut_t *				// O - New lookup table
cfLutNew(int          num_values,	// I - Number of values
	 const float  *values,		// I - Lookup table values
	 cf_logfunc_t log,		// I - Log function
	 void         *ld)		// I - Log function data
{
  int		pixel;			// Pixel value
  cf_lut_t	*lut;			// Lookup table
  int		start,			// Start value
		end,			// End value
		maxval;			// Maximum value


  //
  // Range check...
  //

  if (!num_values || !values)
    return (NULL);

  //
  // Allocate memory for the lookup table...
  //

  if ((lut = (cf_lut_t *)calloc((CF_MAX_LUT + 1),
                                  sizeof(cf_lut_t))) == NULL)
    return (NULL);

  //
  // Generate the dither lookup table.  The pixel values are roughly
  // defined by a piecewise linear curve that has an intensity value
  // at each output pixel.  This isn't perfectly accurate, but it's
  // close enough for jazz.
  //

  maxval = CF_MAX_LUT / values[num_values - 1];

  for (start = 0; start <= CF_MAX_LUT; start ++)
    lut[start].intensity = start * maxval / CF_MAX_LUT;

  for (pixel = 0; pixel < num_values; pixel ++)
  {
    //
    // Select start and end values for this pixel...
    //

    if (pixel == 0)
      start = 0;
    else
      start = (int)(0.5 * maxval * (values[pixel - 1] +
                                    values[pixel])) + 1;

    if (start < 0)
      start = 0;
    else if (start > CF_MAX_LUT)
      start = CF_MAX_LUT;

    if (pixel == (num_values - 1))
      end = CF_MAX_LUT;
    else
      end = (int)(0.5 * maxval * (values[pixel] + values[pixel + 1]));

    if (end < 0)
      end = 0;
    else if (end > CF_MAX_LUT)
      end = CF_MAX_LUT;

    if (start == end)
      break;

    //
    // Generate lookup values and errors for each pixel.
    //

    while (start <= end)
    {
      lut[start].pixel = pixel;
      if (start == 0)
        lut[0].error = 0;
      else
        lut[start].error = start - maxval * values[pixel];

      start ++;
    }
  }

  //
  // Show the lookup table...
  //

  if (log)
    for (start = 0; start <= CF_MAX_LUT; start += CF_MAX_LUT / 15)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "%d = %d/%d/%d", start, lut[start].intensity,
	  lut[start].pixel, lut[start].error);

  //
  // Return the lookup table...
  //

  return (lut);
}

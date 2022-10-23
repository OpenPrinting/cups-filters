//
// CMYK color separation code for libcupsfilters.
//
// Copyright 2007-2011 by Apple Inc.
// Copyright 1993-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Contents:
//
//   cfCMYKDelete()      - Delete a color separation.
//   cfCMYKDoBlack()     - Do a black separation...
//   cfCMYKDoCMYK()      - Do a CMYK separation...
//   cfCMYKDoGray()      - Do a grayscale separation...
//   cfCMYKDoRGB()       - Do an sRGB separation...
//   cfCMYKNew()         - Create a new CMYK color separation.
//   cfCMYKSetBlack()    - Set the transition range for CMY to black.
//   cfCMYKSetCurve()    - Set a color transform curve using points.
//   cfCMYKSetGamma()    - Set a color transform curve using gamma and
//                         density.
//   cfCMYKSetInkLimit() - Set the limit on the amount of ink.
//   cfCMYKSetLtDk()     - Set light/dark ink transforms.
//

//
// Include necessary headers.
//

#include <config.h>
#include "driver.h"
#include <string.h>
#include <ctype.h>


//
// 'cfCMYKDelete()' - Delete a color separation.
//

void
cfCMYKDelete(cf_cmyk_t *cmyk)	// I - Color separation
{
  //
  // Range check input...
  //

  if (cmyk == NULL)
    return;

  //
  // Free memory used...
  //

  free(cmyk->channels[0]);
  free(cmyk);
}


//
// 'cfCMYKDoBlack()' - Do a black separation...
//

void
cfCMYKDoBlack(const cf_cmyk_t     *cmyk,
					// I - Color separation
	      const unsigned char *input,
					// I - Input grayscale pixels
	      short               *output,
					// O - Output Device-N pixels
	      int                 num_pixels)
					// I - Number of pixels
{
  int			k;		// Current black value
  const short		**channels;	// Copy of channel LUTs
  int			ink,		// Amount of ink
			ink_limit;	// Ink limit from separation


  //
  // Range check input...
  //

  if (cmyk == NULL || input == NULL || output == NULL || num_pixels <= 0)
    return;

  //
  // Loop through it all...
  //

  channels  = (const short **)cmyk->channels;
  ink_limit = cmyk->ink_limit;

  switch (cmyk->num_channels)
  {
    case 1 : // Black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = *input++;
	  *output++ = channels[0][k];

          num_pixels --;
        }
	break;

    case 2 : // Black, light black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = *input++;
	  output[0] = channels[0][k];
	  output[1] = channels[1][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	    }
	  }

          output += 2;
          num_pixels --;
        }
	break;

    case 3 : // CMY
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = *input++;
	  output[0] = channels[0][k];
	  output[1] = channels[1][k];
	  output[2] = channels[2][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	    }
	  }

          output += 3;
          num_pixels --;
        }
	break;

    case 4 : // CMYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = *input++;
	  *output++ = 0;
	  *output++ = 0;
	  *output++ = 0;
	  *output++ = channels[3][k];

          num_pixels --;
        }
	break;

    case 6 : // CcMmYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = *input++;
	  *output++ = 0;
	  *output++ = 0;
	  *output++ = 0;
	  *output++ = 0;
	  *output++ = 0;
	  *output++ = channels[5][k];

          num_pixels --;
        }
	break;

    case 7 : // CcMmYKk
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = *input++;
	  output[0] = 0;
	  output[1] = 0;
	  output[2] = 0;
	  output[3] = 0;
	  output[4] = 0;
	  output[5] = channels[5][k];
	  output[6] = channels[6][k];

          if (ink_limit)
	  {
	    ink = output[5] + output[6];

	    if (ink > ink_limit)
	    {
	      output[5] = ink_limit * output[5] / ink;
	      output[6] = ink_limit * output[6] / ink;
	    }
	  }

          output += 7;
          num_pixels --;
        }
	break;
  }
}


//
// 'cfCMYKDoCMYK()' - Do a CMYK separation...
//

void
cfCMYKDoCMYK(const cf_cmyk_t     *cmyk,
					// I - Color separation
	     const unsigned char *input,
					// I - Input grayscale pixels
	     short               *output,
					// O - Output Device-N pixels
	     int                 num_pixels)
					// I - Number of pixels
{
  int			c,		// Current cyan value
			m,		// Current magenta value
			y,		// Current yellow value
			k;		// Current black value
  const short		**channels;	// Copy of channel LUTs
  int			ink,		// Amount of ink
			ink_limit;	// Ink limit from separation


  //
  // Range check input...
  //

  if (cmyk == NULL || input == NULL || output == NULL || num_pixels <= 0)
    return;

  //
  // Loop through it all...
  //

  channels  = (const short **)cmyk->channels;
  ink_limit = cmyk->ink_limit;

  switch (cmyk->num_channels)
  {
    case 1 : // Black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c = *input++;
	  m = *input++;
	  y = *input++;
	  k = *input++ + (c * 31 + m * 61 + y * 8) / 100;

	  if (k < 255)
	    *output++ = channels[0][k];
	  else
	    *output++ = channels[0][255];

          num_pixels --;
        }
	break;

    case 2 : // Black, light black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c = *input++;
	  m = *input++;
	  y = *input++;
	  k = *input++ + (c * 31 + m * 61 + y * 8) / 100;

	  if (k < 255)
	  {
	    output[0] = channels[0][k];
	    output[1] = channels[1][k];
	  }
	  else
	  {
	    output[0] = channels[0][255];
	    output[1] = channels[1][255];
	  }

          if (ink_limit)
	  {
	    ink = output[0] + output[1];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	    }
	  }

          output += 2;
          num_pixels --;
        }
	break;

    case 3 : // CMY
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c = *input++;
	  m = *input++;
	  y = *input++;
	  k = *input++;
	  c += k;
	  m += k;
	  y += k;

	  if (c < 255)
	    output[0] = channels[0][c];
	  else
	    output[0] = channels[0][255];

	  if (m < 255)
	    output[1] = channels[1][m];
	  else
	    output[1] = channels[1][255];

	  if (y < 255)
	    output[2] = channels[2][y];
	  else
	    output[2] = channels[2][255];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	    }
	  }

          output += 3;
          num_pixels --;
        }
	break;

    case 4 : // CMYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c         = *input++;
	  m         = *input++;
	  y         = *input++;
	  k         = *input++;

	  output[0] = channels[0][c];
	  output[1] = channels[1][m];
	  output[2] = channels[2][y];
	  output[3] = channels[3][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	    }
	  }

          output += 4;
          num_pixels --;
        }
	break;

    case 6 : // CcMmYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c         = *input++;
	  m         = *input++;
	  y         = *input++;
	  k         = *input++;

	  output[0] = channels[0][c];
	  output[1] = channels[1][c];
	  output[2] = channels[2][m];
	  output[3] = channels[3][m];
	  output[4] = channels[4][y];
	  output[5] = channels[5][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3] +
	          output[4] + output[5];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	      output[4] = ink_limit * output[4] / ink;
	      output[5] = ink_limit * output[5] / ink;
	    }
	  }

          output += 6;
          num_pixels --;
        }
	break;

    case 7 : // CcMmYKk
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c         = *input++;
	  m         = *input++;
	  y         = *input++;
	  k         = *input++;

	  output[0] = channels[0][c];
	  output[1] = channels[1][c];
	  output[2] = channels[2][m];
	  output[3] = channels[3][m];
	  output[4] = channels[4][y];
	  output[5] = channels[5][k];
	  output[6] = channels[6][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3] +
	          output[4] + output[5] + output[6];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	      output[4] = ink_limit * output[4] / ink;
	      output[5] = ink_limit * output[5] / ink;
	      output[6] = ink_limit * output[6] / ink;
	    }
	  }

          output += 7;
          num_pixels --;
        }
	break;
  }
}


//
// 'cfCMYKDoGray()' - Do a grayscale separation...
//

void
cfCMYKDoGray(const cf_cmyk_t     *cmyk,
					// I - Color separation
	     const unsigned char *input,
					// I - Input grayscale pixels
	     short               *output,
					// O - Output Device-N pixels
	     int                 num_pixels)
					// I - Number of pixels
{
  int			k,		// Current black value
			kc;		// Current black color value
  const short		**channels;	// Copy of channel LUTs
  int			ink,		// Amount of ink
			ink_limit;	// Ink limit from separation


  //
  // Range check input...
  //

  if (cmyk == NULL || input == NULL || output == NULL || num_pixels <= 0)
    return;

  //
  // Loop through it all...
  //

  channels  = (const short **)cmyk->channels;
  ink_limit = cmyk->ink_limit;

  switch (cmyk->num_channels)
  {
    case 1 : // Black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = cf_scmy_lut[*input++];
	  *output++ = channels[0][k];

          num_pixels --;
        }
	break;

    case 2 : // Black, light black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = cf_scmy_lut[*input++];
	  output[0] = channels[0][k];
	  output[1] = channels[1][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	    }
	  }

          output += 2;
          num_pixels --;
        }
	break;

    case 3 : // CMY
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = cf_scmy_lut[*input++];
	  output[0] = channels[0][k];
	  output[1] = channels[1][k];
	  output[2] = channels[2][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	    }
	  }

          output += 3;
          num_pixels --;
        }
	break;

    case 4 : // CMYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = cf_scmy_lut[*input++];
	  kc        = cmyk->color_lut[k];
	  k         = cmyk->black_lut[k];
	  output[0] = channels[0][kc];
	  output[1] = channels[1][kc];
	  output[2] = channels[2][kc];
	  output[3] = channels[3][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	    }
	  }

          output += 4;
          num_pixels --;
        }
	break;

    case 6 : // CcMmYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = cf_scmy_lut[*input++];
	  kc        = cmyk->color_lut[k];
	  k         = cmyk->black_lut[k];
	  output[0] = channels[0][kc];
	  output[1] = channels[1][kc];
	  output[2] = channels[2][kc];
	  output[3] = channels[3][kc];
	  output[4] = channels[4][kc];
	  output[5] = channels[5][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3] +
	          output[4] + output[5];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	      output[4] = ink_limit * output[4] / ink;
	      output[5] = ink_limit * output[5] / ink;
	    }
	  }

          output += 6;
          num_pixels --;
        }
	break;

    case 7 : // CcMmYKk
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  k         = cf_scmy_lut[*input++];
	  kc        = cmyk->color_lut[k];
	  k         = cmyk->black_lut[k];
	  output[0] = channels[0][kc];
	  output[1] = channels[1][kc];
	  output[2] = channels[2][kc];
	  output[3] = channels[3][kc];
	  output[4] = channels[4][kc];
	  output[5] = channels[5][k];
	  output[6] = channels[6][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3] +
	          output[4] + output[5] + output[6];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	      output[4] = ink_limit * output[4] / ink;
	      output[5] = ink_limit * output[5] / ink;
	      output[6] = ink_limit * output[6] / ink;
	    }
	  }

          output += 7;
          num_pixels --;
        }
	break;
  }
}


//
// 'cfCMYKDoRGB()' - Do an sRGB separation...
//

void
cfCMYKDoRGB(const cf_cmyk_t     *cmyk,
					// I - Color separation
	    const unsigned char *input,
					// I - Input grayscale pixels
	    short               *output,
					// O - Output Device-N pixels
	    int                 num_pixels)
					// I - Number of pixels
{
  int			c,		// Current cyan value
			m,		// Current magenta value
			y,		// Current yellow value
			k,		// Current black value
			kc,		// Current black color value
			km;		// Maximum black value
  const short		**channels;	// Copy of channel LUTs
  int			ink,		// Amount of ink
			ink_limit;	// Ink limit from separation


  //
  // Range check input...
  //

  if (cmyk == NULL || input == NULL || output == NULL || num_pixels <= 0)
    return;

  //
  // Loop through it all...
  //

  channels  = (const short **)cmyk->channels;
  ink_limit = cmyk->ink_limit;

  switch (cmyk->num_channels)
  {
    case 1 : // Black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c = cf_scmy_lut[*input++];
	  m = cf_scmy_lut[*input++];
	  y = cf_scmy_lut[*input++];
	  k = (c * 31 + m * 61 + y * 8) / 100;

          *output++ = channels[0][k];

          num_pixels --;
        }
	break;

    case 2 : // Black, light black
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c = cf_scmy_lut[*input++];
	  m = cf_scmy_lut[*input++];
	  y = cf_scmy_lut[*input++];
	  k = (c * 31 + m * 61 + y * 8) / 100;

          output[0] = channels[0][k];
          output[1] = channels[1][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	    }
	  }

          output += 2;
          num_pixels --;
        }
	break;

    case 3 : // CMY
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c = cf_scmy_lut[*input++];
	  m = cf_scmy_lut[*input++];
	  y = cf_scmy_lut[*input++];

	  output[0] = channels[0][c];
          output[1] = channels[1][m];
	  output[2] = channels[2][y];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	    }
	  }

          output += 3;
          num_pixels --;
        }
	break;

    case 4 : // CMYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c  = cf_scmy_lut[*input++];
	  m  = cf_scmy_lut[*input++];
	  y  = cf_scmy_lut[*input++];
	  k  = min(c, min(m, y));

	  if ((km = max(c, max(m, y))) > k)
            k = k * k * k / (km * km);

	  kc = cmyk->color_lut[k] - k;
	  k  = cmyk->black_lut[k];
	  c  += kc;
	  m  += kc;
	  y  += kc;

	  output[0] = channels[0][c];
          output[1] = channels[1][m];
	  output[2] = channels[2][y];
	  output[3] = channels[3][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	    }
	  }

          output += 4;
          num_pixels --;
        }
	break;

    case 6 : // CcMmYK
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c  = cf_scmy_lut[*input++];
	  m  = cf_scmy_lut[*input++];
	  y  = cf_scmy_lut[*input++];
	  k  = min(c, min(m, y));

	  if ((km = max(c, max(m, y))) > k)
            k = k * k * k / (km * km);

	  kc = cmyk->color_lut[k] - k;
	  k  = cmyk->black_lut[k];
	  c  += kc;
	  m  += kc;
	  y  += kc;

	  output[0] = channels[0][c];
	  output[1] = channels[1][c];
	  output[2] = channels[2][m];
	  output[3] = channels[3][m];
	  output[4] = channels[4][y];
	  output[5] = channels[5][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3] +
	          output[4] + output[5];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	      output[4] = ink_limit * output[4] / ink;
	      output[5] = ink_limit * output[5] / ink;
	    }
	  }

          output += 6;
          num_pixels --;
        }
	break;

    case 7 : // CcMmYKk
        while (num_pixels > 0)
        {
	  //
	  // Get the input black value and then set the corresponding color
	  // channel values...
	  //

	  c  = cf_scmy_lut[*input++];
	  m  = cf_scmy_lut[*input++];
	  y  = cf_scmy_lut[*input++];
	  k  = min(c, min(m, y));

	  if ((km = max(c, max(m, y))) > k)
            k = k * k * k / (km * km);

	  kc = cmyk->color_lut[k] - k;
	  k  = cmyk->black_lut[k];
	  c  += kc;
	  m  += kc;
	  y  += kc;

	  output[0] = channels[0][c];
	  output[1] = channels[1][c];
	  output[2] = channels[2][m];
	  output[3] = channels[3][m];
	  output[4] = channels[4][y];
	  output[5] = channels[5][k];
	  output[6] = channels[6][k];

          if (ink_limit)
	  {
	    ink = output[0] + output[1] + output[2] + output[3] +
	          output[4] + output[5] + output[6];

	    if (ink > ink_limit)
	    {
	      output[0] = ink_limit * output[0] / ink;
	      output[1] = ink_limit * output[1] / ink;
	      output[2] = ink_limit * output[2] / ink;
	      output[3] = ink_limit * output[3] / ink;
	      output[4] = ink_limit * output[4] / ink;
	      output[5] = ink_limit * output[5] / ink;
	      output[6] = ink_limit * output[6] / ink;
	    }
	  }

          output += 7;
          num_pixels --;
        }
	break;
  }
}


//
// 'cfCMYKNew()' - Create a new CMYK color separation.
//

cf_cmyk_t *				// O - New CMYK separation or NULL
cfCMYKNew(int num_channels)		// I - Number of color components
{
  cf_cmyk_t	*cmyk;			// New color separation
  int		i;			// Looping var


  //
  // Range-check the input...
  //

  if (num_channels < 1)
    return (NULL);

  //
  // Allocate memory for the separation...
  //

  if ((cmyk = calloc(1, sizeof(cf_cmyk_t))) == NULL)
    return (NULL);

  //
  // Allocate memory for the LUTs...
  //

  cmyk->num_channels = num_channels;

  if ((cmyk->channels[0] = calloc(num_channels * 256, sizeof(short))) == NULL)
  {
    free(cmyk);
    return (NULL);
  }

  for (i = 1; i < num_channels; i ++)
    cmyk->channels[i] = cmyk->channels[0] + i * 256;

  //
  // Fill in the LUTs with unity transitions...
  //

  for (i = 0; i < 256; i ++)
    cmyk->black_lut[i] = i;

  switch (num_channels)
  {
    case 1 : // K
    case 2 : // Kk
	for (i = 0; i < 256; i ++)
	{
	  cmyk->channels[0][i] = CF_MAX_LUT * i / 255;
	}
	break;
    case 3 : // CMY
	for (i = 0; i < 256; i ++)
	{
	  cmyk->channels[0][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[1][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[2][i] = CF_MAX_LUT * i / 255;
	}
	break;
    case 4 : // CMYK
	for (i = 0; i < 256; i ++)
	{
	  cmyk->channels[0][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[1][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[2][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[3][i] = CF_MAX_LUT * i / 255;
	}
	break;
    case 6 : // CcMmYK
    case 7 : // CcMmYKk
	for (i = 0; i < 256; i ++)
	{
	  cmyk->channels[0][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[2][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[4][i] = CF_MAX_LUT * i / 255;
	  cmyk->channels[5][i] = CF_MAX_LUT * i / 255;
	}
	break;
  }

  //
  // Return the separation...
  //

  return (cmyk);
}


//
// 'cfCMYKSetBlack()' - Set the transition range for CMY to black.
//

void
cfCMYKSetBlack(cf_cmyk_t    *cmyk,	// I - CMYK color separation
	       float        lower,	// I - No black ink
	       float        upper,	// I - Only black ink
	       cf_logfunc_t log,	// I - Log function
	       void         *ld)	// I - Log function data
{
  int	i,				// Looping var
	delta,				// Difference between lower and upper
	ilower,				// Lower level from 0 to 255
	iupper;				// Upper level from 0 to 255


  //
  // Range check input...
  //

  if (cmyk == NULL || lower < 0.0 || lower > 1.0 || upper < 0.0 || upper > 1.0 ||
      lower > upper)
    return;

  //
  // Convert lower and upper to integers from 0 to 255...
  //

  ilower  = (int)(255.0 * lower + 0.5);
  iupper  = (int)(255.0 * upper + 0.5);
  delta   = iupper - ilower;

  //
  // Generate the CMY-only data...
  //

  for (i = 0; i < ilower; i ++)
  {
    cmyk->black_lut[i] = 0;
    cmyk->color_lut[i] = i;
  }

  //
  // Then the transition data...
  //

  for (; i < iupper; i ++)
  {
    cmyk->black_lut[i] = iupper * (i - ilower) / delta;
    cmyk->color_lut[i] = ilower - ilower * (i - ilower) / delta;
  }

  //
  // Then the K-only data...
  //

  for (; i < 256; i ++)
  {
    cmyk->black_lut[i] = i;
    cmyk->color_lut[i] = 0;
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfCMYKSetBlack(cmyk, lower=%.3f, upper=%.3f)",
	       lower, upper);

  if (log)
    for (i = 0; i < 256; i += 17)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "   %3d = %3dk + %3dc", i,
	  cmyk->black_lut[i], cmyk->color_lut[i]);
}


//
// 'cfCMYKSetCurve()' - Set a color transform curve using points.
//

void
cfCMYKSetCurve(cf_cmyk_t    *cmyk,	// I - CMYK color separation
	       int          channel,	// I - Color channel
	       int          num_xypoints,
					// I - Number of X,Y points
	       const float  *xypoints,	// I - X,Y points
	       cf_logfunc_t log,	// I - Log function
	       void         *ld)	// I - Log function data
{
  int	i;				// Looping var
  int	xstart;				// Start position
  int	xend;				// End position
  int	xdelta;				// Difference in position
  int	ystart;				// Start value
  int	yend;				// End value
  int	ydelta;				// Difference in value


  //
  // Range check input...
  //

  if (cmyk == NULL || channel < 0 || channel >= cmyk->num_channels ||
      num_xypoints < 1 || xypoints == NULL)
    return;

  //
  // Initialize the lookup table for the specified channel...
  //

  for (xstart = xend = 0, ystart = yend = 0;
       num_xypoints > 0;
       num_xypoints --, xypoints += 2, xstart = xend, ystart = yend)
  {
    xend   = (int)(255.0 * xypoints[1] + 0.5);
    yend   = (int)(CF_MAX_LUT * xypoints[0] + 0.5);
    xdelta = xend - xstart;
    ydelta = yend - ystart;

    for (i = xstart; i < xend; i ++)
      cmyk->channels[channel][i] = ystart + ydelta * (i - xstart) / xdelta;
  }

  //
  // Initialize any trailing values to the maximum of the last data point...
  //

  for (i = xend; i < 256; i ++)
    cmyk->channels[channel][i] = yend;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cupsCMYKSetXY(cmyk, channel=%d, num_xypoints=%d, "
	       "xypoints=[%.3f %.3f %.3f %.3f ...])", channel,
	       num_xypoints, xypoints[0], xypoints[1], xypoints[2],
	       xypoints[3]);

  if (log)
    for (i = 0; i < 256; i += 17)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "    %3d = %4d", i,
	  cmyk->channels[channel + 0][i]);
}


//
// 'cfCMYKSetGamma()' - Set a color transform curve using gamma and density.
//

void
cfCMYKSetGamma(cf_cmyk_t    *cmyk,	// I - CMYK color separation
	       int          channel,	// I - Ink channel
	       float        gamval,	// I - Gamma correction
	       float        density,	// I - Maximum density
	       cf_logfunc_t log,	// I - Log function
	       void         *ld)        // I - Log function data
{
  int	i;				// Looping var


  //
  // Range check input...
  //

  if (cmyk == NULL || channel < 0 || channel >= cmyk->num_channels ||
      gamval <= 0.0 || density <= 0.0 || density > 1.0)
    return;

  //
  // Initialize the lookup table for the specified channel...
  //

  for (i = 0; i < 256; i ++)
    cmyk->channels[channel][i] = (int)(density * CF_MAX_LUT *
                                       pow((float)i / 255.0, gamval) + 0.5);

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfCMYKSetGamma(cmyk, channel=%d, gamval=%.3f, "
	       "density=%.3f)", channel, gamval, density);

  if (log)
    for (i = 0; i < 256; i += 17)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "    %3d = %4d", i,
	  cmyk->channels[channel + 0][i]);
}


//
// 'cfCMYKSetInkLimit()' - Set the limit on the amount of ink.
//

void
cfCMYKSetInkLimit(cf_cmyk_t   *cmyk,	// I - CMYK color separation
		  float       limit)	// I - Limit of ink
{
  if (!cmyk || limit < 0.0)
    return;

  cmyk->ink_limit = limit * CF_MAX_LUT;
}


//
// 'cfCMYKSetLtDk()' - Set light/dark ink transforms.
//

void
cfCMYKSetLtDk(cf_cmyk_t    *cmyk,	// I - CMYK color separation
	      int          channel,	// I - Dark ink channel (+1 for light)
	      float        light,	// I - Light ink only level
	      float        dark,	// I - Dark ink only level
	      cf_logfunc_t log,		// I - Log function
	      void         *ld)         // I - Log function data
{
  int	i,				// Looping var
	delta,				// Difference between lower and upper
	ilight,				// Light level from 0 to 255
	idark;				// Dark level from 0 to 255
  short	lut[256];			// Original LUT data


  //
  // Range check input...
  //

  if (cmyk == NULL || light < 0.0 || light > 1.0 || dark < 0.0 || dark > 1.0 ||
      light > dark || channel < 0 || channel > (cmyk->num_channels - 2))
    return;

  //
  // Convert lower and upper to integers from 0 to 255...
  //

  ilight = (int)(255.0 * light + 0.5);
  idark  = (int)(255.0 * dark + 0.5);
  delta  = idark - ilight;

  //
  // Copy the dark ink LUT...
  //

  memcpy(lut, cmyk->channels[channel], sizeof(lut));

  //
  // Generate the light-only data...
  //

  for (i = 0; i < ilight; i ++)
  {
    cmyk->channels[channel + 0][i] = 0;
    cmyk->channels[channel + 1][i] = CF_MAX_LUT * i / ilight;
  }

  //
  // Then the transition data...
  //

  for (; i < idark; i ++)
  {
    cmyk->channels[channel + 0][i] = CF_MAX_LUT * idark * (i - ilight) /
                                     delta / 255;
    cmyk->channels[channel + 1][i] = CF_MAX_LUT - CF_MAX_LUT *
                                     (i - ilight) / delta;
  }

  //
  // Then the K-only data...
  //

  for (; i < 256; i ++)
  {
    cmyk->channels[channel + 0][i] = CF_MAX_LUT * i / 255;
    cmyk->channels[channel + 1][i] = 0;
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfCMYKSetLtDk(cmyk, channel=%d, light=%.3f, "
	       "dark=%.3f)", channel, light, dark);

  if (log)
    for (i = 0; i < 256; i += 17)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "    %3d = %4dlt + %4ddk", i,
	  cmyk->channels[channel + 0][i], cmyk->channels[channel + 1][i]);
}

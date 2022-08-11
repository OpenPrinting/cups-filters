/*
 *   PPD color profile attribute lookup functions for libppd.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   ppdFindColorAttr() - Find a PPD attribute based on the colormodel,
 *                        media, and resolution.
 *   ppdLutLoad()       - Load a LUT from a PPD file.
 *   ppdRGBLoad()       - Load a RGB color profile from a PPD file.
 *   ppdCMYKLoad()      - Load a CMYK color profile from PPD attributes.
 */

/*
 * Include necessary headers.
 */

#include <config.h>
#include "ppd.h"
#include <cupsfilters/filter.h>
#include <cupsfilters/driver.h>
#include <string.h>
#include <ctype.h>


/*
 * 'ppdFindColorAttr()' - Find a PPD attribute based on the colormodel,
 *                        media, and resolution.
 */

ppd_attr_t *					/* O - Matching attribute or
						       NULL */
ppdFindColorAttr(ppd_file_t *ppd,		/* I - PPD file */
		 const char *name,		/* I - Attribute name */
		 const char *colormodel,	/* I - Color model */
		 const char *media,		/* I - Media type */
		 const char *resolution,	/* I - Resolution */
		 char       *spec,		/* O - Final selection string */
		 int        specsize,		/* I - Size of string buffer */
		 cf_logfunc_t log,      	/* I - Log function */
		 void       *ld)		/* I - Log function data */
{
  ppd_attr_t	*attr;			/* Attribute */


 /*
  * Range check input...
  */

  if (!ppd || !name || !colormodel || !media || !resolution || !spec ||
      specsize < IPP_MAX_NAME)
    return (NULL);

 /*
  * Look for the attribute with the following keywords:
  *
  *     ColorModel.MediaType.Resolution
  *     ColorModel.Resolution
  *     ColorModel
  *     MediaType.Resolution
  *     MediaType
  *     Resolution
  *     ""
  */

  snprintf(spec, specsize, "%s.%s.%s", colormodel, media, resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s.%s", colormodel, resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s", colormodel);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s.%s", media, resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s", media);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s", resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  spec[0] = '\0';
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s\"...", name);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "No instance of \"*%s\" found...", name);

  return (NULL);
}

 
/*
 * 'ppdLutLoad()' - Load a LUT from a PPD file.
 */

cf_lut_t *				/* O - New lookup table */
ppdLutLoad(ppd_file_t *ppd,		/* I - PPD file */
            const char *colormodel,	/* I - Color model */
            const char *media,		/* I - Media type */
            const char *resolution,	/* I - Resolution */
	    const char *ink,		/* I - Ink name */
	    cf_logfunc_t log,       /* I - Log function */
	    void       *ld)             /* I - Log function data */
{
  char		name[PPD_MAX_NAME],	/* Attribute name */
		spec[PPD_MAX_NAME];	/* Attribute spec */
  ppd_attr_t	*attr;			/* Attribute */
  int		nvals;			/* Number of values */
  float		vals[4];		/* Values */


 /*
  * Range check input...
  */

  if (!ppd || !colormodel || !media || !resolution || !ink)
    return (NULL);

 /*
  * Try to find the LUT values...
  */

  snprintf(name, sizeof(name), "cups%sDither", ink);

  if ((attr = ppdFindColorAttr(ppd, name, colormodel, media, resolution, spec,
                           sizeof(spec), log, ld)) == NULL)
    attr = ppdFindColorAttr(ppd, "cupsAllDither", colormodel, media,
                        resolution, spec, sizeof(spec), log, ld);

  if (!attr)
    return (NULL);

  vals[0] = 0.0;
  vals[1] = 0.0;
  vals[2] = 0.0;
  vals[3] = 0.0;
  nvals   = sscanf(attr->value, "%f%f%f", vals + 1, vals + 2, vals + 3) + 1;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Loaded LUT %s from PPD with values [%.3f %.3f %.3f %.3f]",
	       name, vals[0], vals[1], vals[2], vals[3]);

  return (cfLutNew(nvals, vals, log, ld));
}


/*
 * 'ppdRGBLoad()' - Load a RGB color profile from a PPD file.
 */

cf_rgb_t *				/* O - New color profile */
ppdRGBLoad(ppd_file_t *ppd,		/* I - PPD file */
            const char *colormodel,	/* I - Color model */
            const char *media,		/* I - Media type */
            const char *resolution,	/* I - Resolution */
	    cf_logfunc_t log,       /* I - Log function */
	    void       *ld)             /* I - Log function data */
{
  int		i,			/* Looping var */
		cube_size,		/* Size of color lookup cube */
		num_channels,		/* Number of color channels */
		num_samples;		/* Number of color samples */
  cf_sample_t	*samples;		/* Color samples */
  float		values[7];		/* Color sample values */
  char		spec[IPP_MAX_NAME];	/* Profile name */
  ppd_attr_t	*attr;			/* Attribute from PPD file */
  cf_rgb_t	*rgbptr;		/* RGB color profile */


 /*
  * Find the following attributes:
  *
  *    cupsRGBProfile  - Specifies the cube size, number of channels, and
  *                      number of samples
  *    cupsRGBSample   - Specifies an RGB to CMYK color sample
  */

  if ((attr = ppdFindColorAttr(ppd, "cupsRGBProfile", colormodel, media,
                           resolution, spec, sizeof(spec), log, ld)) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "No cupsRGBProfile attribute found for the current settings!");
    return (NULL);
  }

  if (!attr->value || sscanf(attr->value, "%d%d%d", &cube_size, &num_channels,
                             &num_samples) != 3)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "Bad cupsRGBProfile attribute \'%s\'!",
		 attr->value ? attr->value : "(null)");
    return (NULL);
  }

  if (cube_size < 2 || cube_size > 16 ||
      num_channels < 1 || num_channels > CF_MAX_RGB ||
      num_samples != (cube_size * cube_size * cube_size))
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "Bad cupsRGBProfile attribute \'%s\'!",
		 attr->value);
    return (NULL);
  }

 /*
  * Allocate memory for the samples and read them...
  */

  if ((samples = calloc(num_samples, sizeof(cf_sample_t))) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "Unable to allocate memory for RGB profile!");
    return (NULL);
  }

 /*
  * Read all of the samples...
  */

  for (i = 0; i < num_samples; i ++)
    if ((attr = ppdFindNextAttr(ppd, "cupsRGBSample", spec)) == NULL)
      break;
    else if (!attr->value)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Bad cupsRGBSample value!");
      break;
    }
    else if (sscanf(attr->value, "%f%f%f%f%f%f%f", values + 0,
                    values + 1, values + 2, values + 3, values + 4, values + 5,
                    values + 6) != (3 + num_channels))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Bad cupsRGBSample value!");
      break;
    }
    else
    {
      samples[i].rgb[0]    = (int)(255.0 * values[0] + 0.5);
      samples[i].rgb[1]    = (int)(255.0 * values[1] + 0.5);
      samples[i].rgb[2]    = (int)(255.0 * values[2] + 0.5);
      samples[i].colors[0] = (int)(255.0 * values[3] + 0.5);
      if (num_channels > 1)
	samples[i].colors[1] = (int)(255.0 * values[4] + 0.5);
      if (num_channels > 2)
	samples[i].colors[2] = (int)(255.0 * values[5] + 0.5);
      if (num_channels > 3)
	samples[i].colors[3] = (int)(255.0 * values[6] + 0.5);
    }

 /*
  * If everything went OK, create the color profile...
  */

  if (i == num_samples)
    rgbptr = cfRGBNew(num_samples, samples, cube_size, num_channels);
  else
    rgbptr = NULL;

 /*
  * Free the temporary sample array and return...
  */

  free(samples);

  return (rgbptr);
}


/*
 * 'ppdCMYKLoad()' - Load a CMYK color profile from PPD attributes.
 */

cf_cmyk_t *				/* O - CMYK color separation */
ppdCMYKLoad(ppd_file_t *ppd,		/* I - PPD file */
	     const char *colormodel,	/* I - ColorModel value */
	     const char *media,		/* I - MediaType value */
	     const char *resolution,	/* I - Resolution value */
	     cf_logfunc_t log,      /* I - Log function */
	     void       *ld)            /* I - Log function data */
{
  cf_cmyk_t	*cmyk;			/* CMYK color separation */
  char		spec[IPP_MAX_NAME];	/* Profile name */
  ppd_attr_t	*attr;			/* Attribute from PPD file */
  int		num_channels;		/* Number of color components */
  float		gamval,			/* Gamma correction value */
		density,		/* Density value */
		light,			/* Light ink limit */
		dark,			/* Light ink cut-off */
		lower,			/* Start of black ink */
		upper;			/* End of color ink */
  int		num_xypoints;		/* Number of X,Y points */
  float		xypoints[100 * 2],	/* X,Y points */
		*xyptr;			/* Current X,Y point */


 /*
  * Range check input...
  */

  if (ppd == NULL || colormodel == NULL || resolution == NULL || media == NULL)
    return (NULL);

 /*
  * Find the following attributes:
  *
  *     cupsAllGamma          - Set default curve using gamma + density
  *     cupsAllXY             - Set default curve using XY points
  *     cupsBlackGamma        - Set black curve using gamma + density
  *     cupsBlackGeneration   - Set black generation
  *     cupsBlackLightDark    - Set black light/dark transition
  *     cupsBlackXY           - Set black curve using XY points
  *     cupsCyanGamma         - Set cyan curve using gamma + density
  *     cupsCyanLightDark     - Set cyan light/dark transition
  *     cupsCyanXY            - Set cyan curve using XY points
  *     cupsInkChannels       - Set number of color channels
  *     cupsInkLimit          - Set total ink limit
  *     cupsLightBlackGamma   - Set light black curve using gamma + density
  *     cupsLightBlackXY      - Set light black curve using XY points
  *     cupsLightCyanGamma    - Set light cyan curve using gamma + density
  *     cupsLightCyanXY       - Set light cyan curve using XY points
  *     cupsLightMagentaGamma - Set light magenta curve using gamma + density
  *     cupsLightMagentaXY    - Set light magenta curve using XY points
  *     cupsMagentaGamma      - Set magenta curve using gamma + density
  *     cupsMagentaLightDark  - Set magenta light/dark transition
  *     cupsMagentaXY         - Set magenta curve using XY points
  *     cupsYellowGamma       - Set yellow curve using gamma + density
  *     cupsYellowXY          - Set yellow curve using XY points
  *
  * The only required attribute is cupsInkChannels.
  *
  * The *XY attributes have precedence over the *Gamma attributes, and
  * the *Light* attributes have precedence over the corresponding
  * *LightDark* attributes.
  */

 /*
  * Get the required cupsInkChannels attribute...
  */

  if ((attr = ppdFindColorAttr(ppd, "cupsInkChannels", colormodel, media,
                           resolution, spec, sizeof(spec), log, ld)) == NULL)
    return (NULL);

  num_channels = atoi(attr->value);

  if (num_channels < 1 || num_channels > 7 || num_channels == 5)
    return (NULL);

  if ((cmyk = cfCMYKNew(num_channels)) == NULL)
    return (NULL);

 /*
  * Get the optional cupsInkLimit attribute...
  */

  if ((attr = ppdFindColorAttr(ppd, "cupsInkLimit", colormodel, media,
                           resolution, spec, sizeof(spec), log, ld)) != NULL)
    cfCMYKSetInkLimit(cmyk, atof(attr->value));

 /*
  * Get the optional cupsBlackGeneration attribute...
  */

  if ((attr = ppdFindColorAttr(ppd, "cupsBlackGeneration", colormodel, media,
                           resolution, spec, sizeof(spec), log, ld)) != NULL)
  {
    if (sscanf(attr->value, "%f%f", &lower, &upper) == 2)
      cfCMYKSetBlack(cmyk, lower, upper, log, ld);
  }

 /*
  * Get the optional cupsBlackXY or cupsBlackGamma attributes...
  */

  if (num_channels != 3)
  {
    if ((attr = ppdFindColorAttr(ppd, "cupsBlackXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsBlackXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 1 :
	case 2 :
            cfCMYKSetCurve(cmyk, 0, num_xypoints, xypoints, log, ld);
	    break;
	case 4 :
            cfCMYKSetCurve(cmyk, 3, num_xypoints, xypoints, log, ld);
	    break;
	case 6 :
	case 7 :
            cfCMYKSetCurve(cmyk, 5, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsBlackGamma", colormodel,
                                  media, resolution, spec,
				  sizeof(spec), log, ld)) != NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 1 :
	  case 2 :
              cfCMYKSetGamma(cmyk, 0, gamval, density, log, ld);
	      break;
	  case 4 :
              cfCMYKSetGamma(cmyk, 3, gamval, density, log, ld);
	      break;
	  case 6 :
	  case 7 :
              cfCMYKSetGamma(cmyk, 5, gamval, density, log, ld);
	      break;
	}
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllXY", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsAllXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 1 :
	case 2 :
            cfCMYKSetCurve(cmyk, 0, num_xypoints, xypoints, log, ld);
	    break;
	case 4 :
            cfCMYKSetCurve(cmyk, 3, num_xypoints, xypoints, log, ld);
	    break;
	case 6 :
	case 7 :
            cfCMYKSetCurve(cmyk, 5, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllGamma", colormodel,
                                  media, resolution, spec,
				  sizeof(spec), log, ld)) != NULL &&
             num_channels != 3)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 1 :
	  case 2 :
              cfCMYKSetGamma(cmyk, 0, gamval, density, log, ld);
	      break;
	  case 4 :
              cfCMYKSetGamma(cmyk, 3, gamval, density, log, ld);
	      break;
	  case 6 :
	  case 7 :
              cfCMYKSetGamma(cmyk, 5, gamval, density, log, ld);
	      break;
	}
    }
  }

  if (num_channels > 2)
  {
   /*
    * Get the optional cupsCyanXY or cupsCyanGamma attributes...
    */

    if ((attr = ppdFindColorAttr(ppd, "cupsCyanXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsCyanXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      cfCMYKSetCurve(cmyk, 0, num_xypoints, xypoints, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsCyanGamma", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	cfCMYKSetGamma(cmyk, 0, gamval, density, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllXY", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsAllXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      cfCMYKSetCurve(cmyk, 0, num_xypoints, xypoints, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllGamma", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	cfCMYKSetGamma(cmyk, 0, gamval, density, log, ld);
    }

   /*
    * Get the optional cupsMagentaXY or cupsMagentaGamma attributes...
    */

    if ((attr = ppdFindColorAttr(ppd, "cupsMagentaXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsMagentaXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 3 :
	case 4 :
            cfCMYKSetCurve(cmyk, 1, num_xypoints, xypoints, log, ld);
	    break;
	case 6 :
	case 7 :
            cfCMYKSetCurve(cmyk, 2, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsMagentaGamma", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 3 :
	  case 4 :
              cfCMYKSetGamma(cmyk, 1, gamval, density, log, ld);
	      break;
	  case 6 :
	  case 7 :
              cfCMYKSetGamma(cmyk, 2, gamval, density, log, ld);
	      break;
	}
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllXY", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsAllXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 3 :
	case 4 :
            cfCMYKSetCurve(cmyk, 1, num_xypoints, xypoints, log, ld);
	    break;
	case 6 :
	case 7 :
            cfCMYKSetCurve(cmyk, 2, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllGamma", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 3 :
	  case 4 :
              cfCMYKSetGamma(cmyk, 1, gamval, density, log, ld);
	      break;
	  case 6 :
	  case 7 :
              cfCMYKSetGamma(cmyk, 2, gamval, density, log, ld);
	      break;
	}
    }

   /*
    * Get the optional cupsYellowXY or cupsYellowGamma attributes...
    */

    if ((attr = ppdFindColorAttr(ppd, "cupsYellowXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsYellowXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 3 :
	case 4 :
            cfCMYKSetCurve(cmyk, 2, num_xypoints, xypoints, log, ld);
	    break;
	case 6 :
	case 7 :
            cfCMYKSetCurve(cmyk, 4, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsYellowGamma", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 3 :
	  case 4 :
              cfCMYKSetGamma(cmyk, 2, gamval, density, log, ld);
	      break;
	  case 6 :
	  case 7 :
              cfCMYKSetGamma(cmyk, 4, gamval, density, log, ld);
	      break;
	}
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllXY", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsAllXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 3 :
	case 4 :
            cfCMYKSetCurve(cmyk, 2, num_xypoints, xypoints, log, ld);
	    break;
	case 6 :
	case 7 :
            cfCMYKSetCurve(cmyk, 4, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsAllGamma", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 3 :
	  case 4 :
              cfCMYKSetGamma(cmyk, 2, gamval, density, log, ld);
	      break;
	  case 6 :
	  case 7 :
              cfCMYKSetGamma(cmyk, 4, gamval, density, log, ld);
	      break;
	}
    }
  }

 /*
  * Get the optional cupsLightBlackXY, cupsLightBlackGamma, or
  * cupsBlackLtDk attributes...
  */

  if (num_channels == 2 || num_channels == 7)
  {
    if ((attr = ppdFindColorAttr(ppd, "cupsLightBlackXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsLightBlackXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      switch (num_channels)
      {
	case 2 :
            cfCMYKSetCurve(cmyk, 1, num_xypoints, xypoints, log, ld);
	    break;
	case 7 :
            cfCMYKSetCurve(cmyk, 6, num_xypoints, xypoints, log, ld);
	    break;
      }
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsLightBlackGamma", colormodel,
                                  media, resolution, spec,
				  sizeof(spec), log, ld)) != NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	switch (num_channels)
	{
	  case 2 :
              cfCMYKSetGamma(cmyk, 1, gamval, density, log, ld);
	      break;
	  case 7 :
              cfCMYKSetGamma(cmyk, 6, gamval, density, log, ld);
	      break;
	}
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsBlackLtDk", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &light, &dark) == 2)
	switch (num_channels)
	{
	  case 2 :
              cfCMYKSetLtDk(cmyk, 0, light, dark, log, ld);
	      break;
	  case 7 :
              cfCMYKSetLtDk(cmyk, 5, light, dark, log, ld);
	      break;
	}
      else
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "Bad cupsBlackLtDk value \"%s\"!",
		     attr->value);
    }
    else
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "No light black attribute found for %s!",
		   spec);
  }

  if (num_channels >= 6)
  {
   /*
    * Get the optional cupsLightCyanXY, cupsLightCyanGamma, or
    * cupsCyanLtDk attributes...
    */

    if ((attr = ppdFindColorAttr(ppd, "cupsLightCyanXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsLightCyanXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      cfCMYKSetCurve(cmyk, 1, num_xypoints, xypoints, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsLightCyanGamma", colormodel,
                                  media, resolution, spec,
				  sizeof(spec), log, ld)) != NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	cfCMYKSetGamma(cmyk, 1, gamval, density, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsCyanLtDk", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &light, &dark) == 2)
	cfCMYKSetLtDk(cmyk, 0, light, dark, log, ld);
      else
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "Bad cupsCyanLtDk value \"%s\"!",
		     attr->value);
    }
    else
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "No light cyan attribute found for %s!",
		   spec);

   /*
    * Get the optional cupsLightMagentaXY, cupsLightMagentaGamma, or
    * cupsMagentaLtDk attributes...
    */

    if ((attr = ppdFindColorAttr(ppd, "cupsLightMagentaXY", colormodel, media,
                             resolution, spec, sizeof(spec), log, ld)) != NULL)
    {
      for (num_xypoints = 0, xyptr = xypoints;
           attr != NULL && attr->value != NULL && num_xypoints < 100;
	   attr = ppdFindNextAttr(ppd, "cupsLightMagentaXY", spec))
	if (sscanf(attr->value, "%f%f", xyptr, xyptr + 1) == 2)
	{
          num_xypoints ++;
	  xyptr += 2;
	}

      cfCMYKSetCurve(cmyk, 3, num_xypoints, xypoints, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsLightMagentaGamma", colormodel,
                                  media, resolution, spec,
				  sizeof(spec), log, ld)) != NULL)
    {
      if (sscanf(attr->value, "%f%f", &gamval, &density) == 2)
	cfCMYKSetGamma(cmyk, 3, gamval, density, log, ld);
    }
    else if ((attr = ppdFindColorAttr(ppd, "cupsMagentaLtDk", colormodel, media,
                                  resolution, spec, sizeof(spec), log, ld)) !=
	     NULL)
    {
      if (sscanf(attr->value, "%f%f", &light, &dark) == 2)
	cfCMYKSetLtDk(cmyk, 2, light, dark, log, ld);
      else
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "Bad cupsMagentaLtDk value \"%s\"!",
		     attr->value);
    }
    else
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "No light magenta attribute found for %s!",
		   spec);
  }

 /*
  * Return the new profile...
  */

  return (cmyk);
}

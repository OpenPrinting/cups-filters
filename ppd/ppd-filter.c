/*
 * Filter functions support for libppd.
 *
 * Copyright © 2020-2022 by Till Kamppeter.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include "ppd-filter.h"
#include "ppd.h"
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/wait.h>
#include <cups/file.h>
#include <cups/array.h>

extern char **environ;

/*
 * 'ppdFilterCUPSWrapper()' - Wrapper function to use a filter function as
 *                            classic CUPS filter
 */

int					/* O - Exit status */
ppdFilterCUPSWrapper(
     int  argc,				/* I - Number of command-line args */
     char *argv[],			/* I - Command-line arguments */
     cf_filter_function_t filter,       /* I - Filter function */
     void *parameters,                  /* I - Filter function parameters */
     int *JobCanceled)                  /* I - Var set to 1 when job canceled */
{
  int	        inputfd;		/* Print file descriptor*/
  int           inputseekable;          /* Is the input seekable (actual file
					   not stdin)? */
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  cf_filter_data_t filter_data;
  const char    *val;
  char          buf[256];
  int           retval = 0;


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Check command-line...
  */

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n",
	    argv[0]);
    return (1);
  }

 /*
  * If we have 7 arguments, print the file named on the command-line.
  * Otherwise, send stdin instead...
  */

  if (argc == 6)
  {
    inputfd = 0; /* stdin */
    inputseekable = 0;
  }
  else
  {
   /*
    * Try to open the print file...
    */

    if ((inputfd = open(argv[6], O_RDONLY)) < 0)
    {
      if (!JobCanceled)
      {
        fprintf(stderr, "DEBUG: Unable to open \"%s\": %s\n", argv[6],
		strerror(errno));
	fprintf(stderr, "ERROR: Unable to open print file");
      }

      return (1);
    }

    inputseekable = 1;
  }

 /*
  * Process command-line options...
  */

  options     = NULL;
  num_options = cupsParseOptions(argv[5], 0, &options);

  if ((filter_data.printer = getenv("PRINTER")) == NULL)
    filter_data.printer = argv[0];
  filter_data.job_id = atoi(argv[1]);
  filter_data.job_user = argv[2];
  filter_data.job_title = argv[3];
  filter_data.copies = atoi(argv[4]);
  filter_data.content_type = getenv("CONTENT_TYPE");
  filter_data.final_content_type = getenv("FINAL_CONTENT_TYPE");
  filter_data.job_attrs = NULL;        /* We use command line options */
  /* The following two will get populated by ppdFilterLoadPPD() */
  filter_data.printer_attrs = NULL;    /* We use the queue's PPD file */
  filter_data.header = NULL;           /* CUPS Raster header of queue's PPD */
  filter_data.num_options = num_options;
  filter_data.options = options;       /* Command line options from 5th arg */
  filter_data.back_pipe[0] = 3;        /* CUPS uses file descriptor 3 for */
  filter_data.back_pipe[1] = 3;        /* the back channel */
  filter_data.side_pipe[0] = 4;        /* CUPS uses file descriptor 4 for */
  filter_data.side_pipe[1] = 4;        /* the side channel */
  filter_data.extension = NULL;
  filter_data.logfunc = cfCUPSLogFunc;  /* Logging scheme of CUPS */
  filter_data.logdata = NULL;
  filter_data.iscanceledfunc = cfCUPSIsCanceledFunc; /* Job-is-canceled
						       function */
  filter_data.iscanceleddata = JobCanceled;

 /*
  * CUPS_FONTPATH (Usually /usr/share/cups/fonts)
  */

  if (cupsGetOption("cups-fontpath",
		    filter_data.num_options, filter_data.options) == NULL)
  {
    if ((val = getenv("CUPS_FONTPATH")) == NULL)
    {
      val = CUPS_DATADIR;
      snprintf(buf, sizeof(buf), "%s/fonts", val);
      val = buf;
    }
    if (val[0] != '\0')
      filter_data.num_options =
	cupsAddOption("cups-fontpath", val,
		      filter_data.num_options, &(filter_data.options));
  }

 /*
  * Load and prepare the PPD file, also attach it as extension "libppd"
  * to the filter_data structure
  */

  retval = ppdFilterLoadPPDFile(&filter_data, getenv("PPD"));

 /*
  * Fire up the filter function (output to stdout, file descriptor 1)
  */

  if (!retval)
    retval = filter(inputfd, 1, inputseekable, &filter_data, parameters);

 /*
  * Clean up
  */

  cupsFreeOptions(filter_data.num_options, filter_data.options);
  ppdFilterFreePPDFile(&filter_data);

  return retval;
}


/*
 * 'ppdFilterLoadPPDFile()' - When preparing the filter data structure
 *                            for calling one or more filter
 *                            functions, load the PPD file specified
 *                            by its file name.  If the file name is
 *                            NULL or empty, do nothing. If the PPD
 *                            got successfully loaded add its data to
 *                            the filter data structure as extension
 *                            named "libppd", so that filters
 *                            functions designed for using PPDs can
 *                            access it. Then read out all the
 *                            relevant data with the
 *                            ppdFilterLoadPPD() function.
 */

int					     /* O   - Error status */
ppdFilterLoadPPDFile(cf_filter_data_t *data, /* I/O - Job and printer data */
		     const char *ppdfile)    /* I   - PPD file name */
{
  ppd_filter_data_ext_t *filter_data_ext; /* Record for "libppd" extension */
  ppd_file_t       *ppd;                  /* PPD data */
  cf_logfunc_t     log = data->logfunc;   /* Log function */
  void             *ld = data->logdata;   /* log function data */

  if (!ppdfile || !ppdfile[0])
    return (-1);

  if ((ppd = ppdOpenFile(ppdfile)) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterLoadPPDFile: Could not load PPD file %s: %s",
		 ppdfile, strerror(errno));
    return (-1);
  }

  filter_data_ext =
    (ppd_filter_data_ext_t *)calloc(1, sizeof(ppd_filter_data_ext_t));

  filter_data_ext->ppdfile = strdup(ppdfile); /* PPD file name */
  filter_data_ext->ppd = ppd;                 /* PPD data */
  cfFilterDataAddExt(data, PPD_FILTER_DATA_EXT, filter_data_ext);

  return ppdFilterLoadPPD(data);
}


/*
 * 'ppdFilterLoadPPD()' - When preparing the filter data structure for
 *                        calling one or more filter functions, and a
 *                        PPD file is attached as "libppd" extension,
 *                        set up the PPD's cache, mark default
 *                        settings and if supplied in the data
 *                        structure, also option settings. Then
 *                        convert the capability and option info in
 *                        the PPD file into printer IPP attributes and
 *                        what cannot be represented in IPP as option
 *                        settings and add these results to the filter
 *                        data structure. This allows to use the (by
 *                        itself not PPD-supporting) filter function
 *                        to do its work for the printer represented
 *                        by the PPD file. Add the CUPS PPD file data
 *                        structure with embedded CUPS PPD cache data
 *                        structure (including PPD option setting
 *                        presets for all possible print-color-mode,
 *                        print-quality, and print-content-optimize
 *                        settings) to the filter data structure for
 *                        use by libppd's PPD-requiring filter
 *                        functions.
 */

int					 /* O   - Error status */
ppdFilterLoadPPD(cf_filter_data_t *data) /* I/O - Job and printer data */
{
  int              i;
  ppd_filter_data_ext_t *filter_data_ext =
    (ppd_filter_data_ext_t *)cfFilterDataGetExt(data,
						PPD_FILTER_DATA_EXT);
  ppd_file_t       *ppd;
  int              num_job_attr_options = 0;
  cups_option_t    *job_attr_options = NULL;
  cups_option_t    *opt;
  ppd_attr_t       *ppd_attr;
  ppd_choice_t     *choice;
  ipp_attribute_t  *attr;
  ipp_t            *col;
  const char       *val;
  char             *lastfilter = NULL;
  bool             hw_copies = false,
                   hw_collate = false;
  const char       *page_size, *media;
  const char       *q1_choice, *q2_choice, *q3_choice;
  char             buf[1024];
  char             cm_qualifier_tmp[1024];
  const char       *cm_profile_key;
  char		   *resolution,		  /* Output resolution */
		   *media_type;		  /* Media type */
  ppd_profile_t	   *profile;		  /* Color profile */
  cf_logfunc_t     log = data->logfunc;   /* Log function */
  void             *ld = data->logdata;   /* log function data */

  if (!filter_data_ext || !filter_data_ext->ppd)
    return (-1);

 /*
  * Prepare PPD file and mark options
  */

  ppd = filter_data_ext->ppd;
  ppd->cache = ppdCacheCreateWithPPD(ppd);
  ppdMarkDefaults(ppd);
  ppdMarkOptions(ppd, data->num_options, data->options);
  num_job_attr_options = ppdGetOptions(&job_attr_options, data->printer_attrs,
				       data->job_attrs, ppd);
  for(i = 0, opt = job_attr_options; i < num_job_attr_options; i++, opt++)
    data->num_options = cupsAddOption(opt->name, opt->value,
				      data->num_options, &(data->options));
  cupsFreeOptions(num_job_attr_options, job_attr_options);
  ppdMarkOptions(ppd, data->num_options, data->options);
  ppdHandleMedia(ppd);

 /*
  * Pass on PPD attributes
  */

  /* Pass on "PWGRaster" PPD attribute (for PWG Raster output) */
  if ((ppd_attr = ppdFindAttr(ppd, "PWGRaster", 0)) != 0 &&
      (!strcasecmp(ppd_attr->value, "true") ||
       !strcasecmp(ppd_attr->value, "on") ||
       !strcasecmp(ppd_attr->value, "yes")))
    data->num_options = cupsAddOption("media-class", "PwgRaster",
				      data->num_options, &(data->options));

  /* Pass on "cupsEvenDuplex" PPD attribute */
  if ((ppd_attr = ppdFindAttr(ppd, "cupsEvenDuplex", 0)) != NULL)
    data->num_options = cupsAddOption("even-duplex", ppd_attr->value,
				      data->num_options, &(data->options));

  /* Pass on "cupsBackSide" (or "cupsFlipDuplex") PPD attribute */
  if ((ppd_attr = ppdFindAttr(ppd, "cupsBackSide", 0)) != NULL)
  {
    ppd->flip_duplex = 0; /* "cupsBackSide" has priority */
    data->num_options = cupsAddOption("back-side-orientation", ppd_attr->value,
				      data->num_options, &(data->options));
  }
  else if (ppd->flip_duplex) /* "cupsFlipDuplex" same as "Rotated" */
    data->num_options = cupsAddOption("back-side-orientation", "Rotated",
				      data->num_options, &(data->options));

  /* Pass on "APDuplexRequiresFlippedMargin" PPD attribute */
  if ((ppd_attr = ppdFindAttr(ppd, "APDuplexRequiresFlippedMargin", 0)) != NULL)
    data->num_options = cupsAddOption("duplex-requires-flipped-margin",
				      ppd_attr->value,
				      data->num_options, &(data->options));

  /* Pass on "cupsRasterVersion" PPD attribute */
  if ((ppd_attr = ppdFindAttr(ppd,"cupsRasterVersion", 0)) != NULL)
    data->num_options = cupsAddOption("cups-raster-version",
				      ppd_attr->value,
				      data->num_options, &(data->options));

  /* Pass on "DefaultCenterOfPixel" PPD attribute (for Ghostscript) */
  if ((ppd_attr = ppdFindAttr(ppd,"DefaultCenterOfPixel", 0)) != NULL)
    data->num_options = cupsAddOption("center-of-pixel",
				      ppd_attr->value,
				      data->num_options, &(data->options));

  /* Set short-edge Duplex when booklet printing is selected */
  if ((val = cupsGetOption("booklet",
			   data->num_options, data->options)) != NULL &&
      (!strcasecmp(val, "on") || !strcasecmp(val, "yes") ||
       !strcasecmp(val, "true")) &&
      ppd->cache->sides_option &&
      ppd->cache->sides_2sided_short &&
      ppdFindOption(ppd, ppd->cache->sides_option))
    ppdMarkOption(ppd, ppd->cache->sides_option,
		  ppd->cache->sides_2sided_short);

  /* Let the PDF filter do mirrored printing */
  if ((choice = ppdFindMarkedChoice(ppd, "MirrorPrint")) != NULL)
  {
    choice->marked = 0;
    data->num_options = cupsAddOption("mirror", "true",
				      data->num_options, &(data->options));
  }

 /*
  * Find out whether we can do hardware copies/collate
  */

  if (data->copies == 1)
  {
    /* 1 copy, hardware copies/collate do not make difference */
    hw_copies = false;
    hw_collate = false;
  }
  else if (!ppd->manual_copies)
  {
    /* Hardware copy generation available */
    hw_copies = true;
    /* Check output format (FINAL_CONTENT_TYPE env variable) whether it is
       of a driverless IPP printer (PDF, Apple Raster, PWG Raster, PCLm).
       These printers do always hardware collate if they do hardware copies.
       https://github.com/apple/cups/issues/5433
       This also assumes that if a classic PDF printer (non-IPP printer printing
       with JCL/PJL-controlled PDF) supports hardware copies that it also does
       hardware collate */
    if (data->final_content_type &&
	(strcasestr(data->final_content_type, "/pdf") ||
	 strcasestr(data->final_content_type, "/vnd.cups-pdf") ||
	 strcasestr(data->final_content_type, "/pwg-raster") ||
	 strcasestr(data->final_content_type, "/urf") ||
	 strcasestr(data->final_content_type, "/PCLm")))
    {
      /* If PPD has "Collate" option set to not collate, do not set
	 hw_collate, especially important to set correct JCL/PJL on
	 "classic" PDF printers (they will always collate otherwise) */
      if ((choice = ppdFindMarkedChoice(ppd, "Collate")) != NULL &&
	  (!strcasecmp(choice->choice, "off") ||
	   !strcasecmp(choice->choice, "no") ||
	   !strcasecmp(choice->choice, "false")))
	hw_collate = false;
      else
	hw_collate = true;
    }
    else
    {
      /* Check whether printer hardware-collates with current PPD settings */
      if ((choice = ppdFindMarkedChoice(ppd, "Collate")) != NULL &&
	  (!strcasecmp(choice->choice, "on") ||
	   !strcasecmp(choice->choice, "yes") ||
	   !strcasecmp(choice->choice, "true")))
      {
	/* Printer can collate, but also for the currently marked PPD
	   features? */
	ppd_option_t *opt = ppdFindOption(ppd, "Collate");
	hw_collate = (opt && !opt->conflicted);
      }
      else
	hw_collate = false;
    }
  }
  else
  {
    /* We have "*cupsManualCopies: True" =>
       Software copies/collate */
    hw_copies = false;
    hw_collate = false;
  }

  if (!hw_copies)
  {
    /* Software copies */
    /* Make sure any hardware copying is disabled */
    ppdMarkOption(ppd, "Copies", "1");
    ppdMarkOption(ppd, "JCLCopies", "1");
  }
  else
  {
    /* Hardware copies */
    /* If there is a "Copies" option in the PPD file, assure that hardware
       copies are implemented as described by this option */
    snprintf(buf, sizeof(buf), "%d", hw_copies);
    ppdMarkOption(ppd, "Copies", buf);
  }

  /* Software collate */
  if (!hw_collate)
    /* Disable any hardware collate (in JCL) */
    ppdMarkOption(ppd, "Collate", "False");

  /* Add options telling whether we want hardware copies/collate or not */
  data->num_options = cupsAddOption("hardware-copies",
				    (hw_copies ? "true" : "false"),
				    data->num_options, &(data->options));
  data->num_options = cupsAddOption("hardware-collate",
				    (hw_collate ? "true" : "false"),
				    data->num_options, &(data->options));

 /*
  * Pass on color management attributes
  */

  /* Get color space */
  if ((ppd_attr = ppdFindAttr (ppd, "cupsICCQualifier1", NULL)) != NULL &&
      ppd_attr->value && ppd_attr->value[0])
    choice = ppdFindMarkedChoice(ppd, ppd_attr->value);
  else if ((choice = ppdFindMarkedChoice(ppd, "ColorModel")) == NULL)
    choice = ppdFindMarkedChoice(ppd, "ColorSpace");
  if (choice && choice->choice && choice->choice[0])
    q1_choice = choice->choice;
  else
    q1_choice = "";

  /* Get media type */
  if ((ppd_attr = ppdFindAttr(ppd, "cupsICCQualifier2", NULL)) != NULL &&
      ppd_attr->value && ppd_attr->value[0])
    choice = ppdFindMarkedChoice(ppd, ppd_attr->value);
  else
    choice = ppdFindMarkedChoice(ppd, "MediaType");
  if (choice && choice->choice && choice->choice[0])
    q2_choice = choice->choice;
  else
    q2_choice = "";

  /* Get resolution */
  if ((ppd_attr = ppdFindAttr(ppd, "cupsICCQualifier3", NULL)) != NULL &&
      ppd_attr->value && ppd_attr->value[0])
    choice = ppdFindMarkedChoice(ppd, ppd_attr->value);
  else
    choice = ppdFindMarkedChoice(ppd, "Resolution");
  if (choice && choice->choice && choice->choice[0])
    q3_choice = choice->choice;
  else
  {
    ppd_attr = ppdFindAttr(ppd, "DefaultResolution", NULL);
    if (ppd_attr && ppd_attr->value && ppd_attr->value[0])
      q3_choice = ppd_attr->value;
    else
      q3_choice = "";
  }

  /* create a string for the option */
  snprintf(cm_qualifier_tmp, sizeof(cm_qualifier_tmp),
           "%s.%s.%s", q1_choice, q2_choice, q3_choice);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "ppdFilterLoadPPD: Color profile qualifier determined from job and PPD data '%s'",
	       cm_qualifier_tmp);

  /* Supply qualifier as option */
  data->num_options = cupsAddOption("cm-profile-qualifier", cm_qualifier_tmp,
				    data->num_options, &(data->options));

  /* get profile attr, falling back to CUPS */
  cm_profile_key = "APTiogaProfile";
  ppd_attr = ppdFindAttr(ppd, cm_profile_key, NULL);
  if (ppd_attr == NULL)
  {
    cm_profile_key = "cupsICCProfile";
    ppd_attr = ppdFindAttr(ppd, cm_profile_key, NULL);
  }

  if (ppd_attr == NULL)
  {
    /* neither */
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		"ppdFilterLoadPPD: No ICC profiles specified in PPD");
  }
  else
  {
    /* Try to find a profile that matches the qualifier exactly */
    for (;ppd_attr != NULL;
	 ppd_attr = ppdFindNextAttr(ppd, cm_profile_key, NULL))
    {
      /* Invalid entry */
      if (ppd_attr->spec == NULL || ppd_attr->value == NULL)
	continue;

      /* Matches the qualifier */
      if (strcmp(cm_qualifier_tmp, ppd_attr->spec) == 0)
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "ppdFilterLoadPPD: Found ICC profile %s in PPD for qualifier '%s'",
		     ppd_attr->value, ppd_attr->spec);
	break;
      }
    }

    /* No match */
    if (ppd_attr == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "ppdFilterLoadPPD: No ICC profile in PPD for qualifier '%s'",
		   cm_qualifier_tmp);
    }
    else
    {
      /* expand to a full path if not already specified */
      if (ppd_attr->value[0] != '/')
      {
	if ((val = getenv("CUPS_DATADIR")) == NULL)
	  val = CUPS_DATADIR;
	snprintf(buf, sizeof(buf),
		 "%s/profiles/%s", val, ppd_attr->value);
      }
      else
      {
	strncpy(buf, ppd_attr->value, sizeof(buf) - 1);
	if (strlen(ppd_attr->value) > 1023)
	  buf[1023] = '\0';
      }

      /* check the file exists */
      if (access(buf, 0))
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterLoadPPD: ICC profile %s in PPD does not exist",
		     buf);
      }
      else
      {
	/* Supply path as fallback profile option */
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "ppdFilterLoadPPD: Using ICC profile %s as fallback if colord does not supply another one",
		     buf);
	data->num_options =
	  cupsAddOption("cm-fallback-profile", buf,
			data->num_options, &(data->options));
      }
    }
  }

 /*
  * Find a color profile matching the current options...
  */

  if (cupsGetOption("profile", data->num_options, data->options) == NULL)
  {
    if ((choice = ppdFindMarkedChoice(ppd, "Resolution")) != NULL)
      resolution = choice->choice;
    else
      resolution = "-";
    if ((choice = ppdFindMarkedChoice(ppd, "MediaType")) != NULL)
      media_type = choice->choice;
    else
      media_type = "-";

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "ppdFilterLoadPPD: Searching for profile \"%s/%s\"...",
		 resolution, media_type);

    for (i = 0, profile = ppd->profiles; i < ppd->num_profiles;
	 i ++, profile ++)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "ppdFilterLoadPPD: \"%s/%s\" = ", profile->resolution,
		   profile->media_type);

      if ((strcmp(profile->resolution, resolution) == 0 ||
           profile->resolution[0] == '-') &&
          (strcmp(profile->media_type, media_type) == 0 ||
           profile->media_type[0] == '-'))
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "ppdFilterLoadPPD:    MATCH");
	break;
      }
      else
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "ppdFilterLoadPPD:    no.");
    }

   /*
    * If we found a color profile, use it!
    */

    if (i >= ppd->num_profiles)
      profile = NULL;
  }
  else
    profile = NULL;

  if (profile)
  {
    snprintf(buf, sizeof(buf),
	     "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
	     (int)(profile->density * 1000.0),
	     (int)(profile->gamma * 1000.0),
	     (int)(profile->matrix[0][0] * 1000.0),
	     (int)(profile->matrix[0][1] * 1000.0),
	     (int)(profile->matrix[0][2] * 1000.0),
	     (int)(profile->matrix[1][0] * 1000.0),
	     (int)(profile->matrix[1][1] * 1000.0),
	     (int)(profile->matrix[1][2] * 1000.0),
	     (int)(profile->matrix[2][0] * 1000.0),
	     (int)(profile->matrix[2][1] * 1000.0),
	     (int)(profile->matrix[2][2] * 1000.0));
    data->num_options = cupsAddOption("profile", buf,
				      data->num_options, &(data->options));
  }

 /*
  * Convert the settings and properties in the PPD into printer IPP
  * attributes
  */

  data->printer_attrs = ppdLoadAttributes(ppd);

 /*
  * Generate a CUPS Raster sample header, some filters can easily take
  * data from it or make it the base for actual Raster headers. The
  * header is based on the pseudo-PostScript code at the option
  * settings in the PPD file, this is usually the more reliable way to
  * obtain the correct resolution.
  *
  * This only works if the PPD file is actually for a CUPS Raster
  * driver or generated by CUPS for driverless printers via the
  * "everywhere" pseudo-driver, as other PPD files do not contain all
  * resolution, page geometry and color space/depth info in
  * pseudo-PostScript code and the ppdRasterInterpretPPD() function
  * only parses the pseudo-PostScript code not the names of the
  * options and choices.
  *
  * Therefore we create the sample header only if the PPD is actually
  * for a CUPS Raster driver or an "everywhere" PPD from CUPS.
  */

  data->header = NULL;
  for (i = 0; i < ppd->num_filters; i ++)
  {
    if (!strncasecmp(ppd->filters[i], "application/vnd.cups-raster", 27) ||
	(!strncasecmp(ppd->filters[i], "image/urf", 9) &&
	 !ppdFindAttr(ppd, "cupsUrfSupported", NULL)) ||
	(!strncasecmp(ppd->filters[i], "image/pwg-raster", 16) &&
	 !ppdFindAttr(ppd, "pwg-raster-document-type-supported", NULL)))
    {
      // We have a CUPS Raster driver PPD file
      data->header =
	(cups_page_header2_t *)calloc(1, sizeof(cups_page_header2_t));
      if (ppdRasterInterpretPPD(data->header, ppd,
				data->num_options, data->options, NULL) < 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterLoadPPD: Unable to generate CUPS Raster sample header.");
	free(data->header);
	data->header = NULL;
      }
      break;
    }
  }

 /*
  * Replace the "PageSize" option by a media-col attribute, as this
  * one selects the media much more reliably by numeric size
  * dimensions and margins and not by name.
  *
  * PPDs have special page size names for page size variants (A4,
  * A4.Borderless, A4.Duplex, A4.Transverse) which cannot get mapped
  * into the printer IPP attributes (media-col-database) and so cannot
  * get easily selected by the PPD-support-free core filter functions
  * in libcupsfilters.
  *
  * Usually, the media-col attribute does not need to get copied from
  * printer attributes to job attributes, but having it in the job
  * attributes tells the filters that the user has requested a page
  * size, as at least some filters overtake the sizes of the input
  * pages when the user does not request a page size.
  */

  page_size = cupsGetOption("PageSize", data->num_options, data->options);
  media = cupsGetOption("media", data->num_options, data->options);
  if ((page_size || media) &&
      (attr = ippFindAttribute(data->printer_attrs, "media-col-default",
			       IPP_TAG_ZERO)) != NULL)
  {
    /* We have already applied the settings of these options to the
       PPD file and converted the PPD option settings into the printer
       IPP attributes. The media size and margins corresponding to the
       name supplied with these options is now included in the printer
       IPP attributes as "media-col-default". Here we remove the
       "PageSize" and "media" options and add "media-col" to the job
       attributes as a copy of "media-col-default" in the printer
       attributes. We do this only if the size specified by "PageSize"
       or "media" is not a custom size ("Custom.XXxYYunit") as a
       custom size cannot get marked in the PPD file and so not set as
       default page size, so it does not make it into
       media-col-default. In this case we only remove "media-col". */

    int is_custom = 0;

    if (page_size)
    {
      if (strncasecmp(page_size, "Custom.", 7) != 0)
	data->num_options = cupsRemoveOption("PageSize", data->num_options,
					     &(data->options));
      else
	is_custom = 1;
    }

    if (media)
    {
      if ((val = strcasestr(media, "Custom.")) == NULL ||
	  (val != media && *(val - 1) != ','))
	data->num_options = cupsRemoveOption("media", data->num_options,
					     &(data->options));
      else
	is_custom = 1;
    }

    data->num_options = cupsRemoveOption("media-col", data->num_options,
                                         &(data->options));
    ippDeleteAttribute(data->job_attrs,
		       ippFindAttribute(data->job_attrs, "media-col",
					IPP_TAG_ZERO));

    if (!is_custom)
    {
      /* String from IPP attribute for option list */
      ippAttributeString(attr, buf, sizeof(buf));
      data->num_options = cupsAddOption("media-col", buf,
					data->num_options, &(data->options));

      /* Copy the default media-col */
      col = ippGetCollection(attr, 0);
      ippAddCollection(data->job_attrs, IPP_TAG_PRINTER, "media-col", col);
    }
  }

 /*
  * Find out whether the PDF filter (cfFilterPDFToPDF() or
  * cfFilterImageToPDF()) should log the pages or whether the last
  * filter (usually the printer driver) should do it.
  */

  if ((val = cupsGetOption("pdf-filter-page-logging",
			   data->num_options, data->options)) == NULL ||
      strcasecmp(val, "auto") == 0)
  {
    int page_logging = -1;
    if (data->final_content_type == NULL)
    {
      /* Final data MIME type not known, we cannot determine
	 whether we have to log pages, so do not log. */
      page_logging = 0;
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "ppdFilterLoadPPD: No final data MIME type known, so we "
		   "cannot determine whether the PDF filter has to log pages "
		   "or not, so turning off page logging for the PDF filter.");
    }
    /* Proceed depending on number of cupsFilter(2) lines in PPD */
    else if (ppd->num_filters == 0)
    {
      /* No filter line, manufacturer-supplied PostScript PPD
	 In this case cfFilterPSToPS, called by cfFilterPDFToPS, does the
	 logging */
      page_logging = 0;
    }
    else
    {
      /* Filter(s) specified by filter line(s), determine the one which got
	 actually used via final data MIME type */
      bool cupsfilter2 = (ppdFindAttr(ppd, "cupsFilter2", NULL) != NULL);
      for (lastfilter = (char *)cupsArrayFirst(ppd->cache->filters);
	   lastfilter;
	   lastfilter = (char *)cupsArrayNext(ppd->cache->filters))
      {
	char *p = lastfilter;
	if (cupsfilter2)
	{
	  /* Skip first word as the final content type is the second */
	  while (!isspace(*p)) p ++;
	  while (isspace(*p)) p ++;
	}
	if (strlen(p) >= strlen(data->final_content_type) &&
	    !strncasecmp(data->final_content_type, p,
			 strlen(data->final_content_type)) &&
	    !isalnum(p[strlen(data->final_content_type)])) {
	  break;
	}
      }
    }
    if (page_logging == -1)
    {
      if (lastfilter)
      {
	/* Get the name of the last filter, without mime type and cost */
	char *p = lastfilter;
	char *q = p + strlen(p) - 1;
	while (!isspace(*q) && *q != '/') q --;
	lastfilter = q + 1;
	/* Check whether the PDF filter has to log */
	if (!strcasecmp(lastfilter, "-"))
	{
	  /* No filter defined in the PPD
	     If output data is PDF, cfFilterPDFToPDF() is last
	     filter (PDF printer) and has to log
	     If output data is Apple/PWG Raster or PCLm, cfFilter*ToRaster() is
	     last filter (Driverless IPP printer) and cfFilterPDFToPDF()
	     also has to log */
	  if (strcasestr(data->final_content_type, "/pdf") ||
	      strcasestr(data->final_content_type, "/vnd.cups-pdf") ||
	      strcasestr(data->final_content_type, "/pwg-raster") ||
	      strcasestr(data->final_content_type, "/urf") ||
	      strcasestr(data->final_content_type, "/pclm"))
	    page_logging = 1;
	  else
	    page_logging = 0;
	}
	else if (!strcasecmp(lastfilter, "pdftopdf") ||
		 !strcasecmp(lastfilter, "imagetopdf"))
	{
	  /* cfFilterPDFToPDF() is last filter (PDF printer) */
	  page_logging = 1;
	}
	else if (!strcasecmp(lastfilter, "gstopxl"))
	{
	  /* cfFilterGhostscript() with PCL-XL output is last filter,
	     this is a Ghostscript-based filter without access to the
	     pages of the file to be printed, so the PDF filter has to
	     log the pages */
	  page_logging = 1;
	}
	else if (!strcasecmp(lastfilter + strlen(lastfilter) - 8,
			     "toraster") ||
		 !strcasecmp(lastfilter + strlen(lastfilter) - 5,
			     "topwg"))
	{
	  /* On IPP Everywhere printers which accept PWG Raster data one
	     of cfFilterGhostscript(), cfFilterPDFToRaster(), or
	     cfFilterMuPDFToPWG() is the last filter. These filters do not
	     log pages so the PDF filter has to do it */
	  page_logging = 1;
	}
	else if (!strcasecmp(lastfilter, "foomatic-rip"))
	{
	  /* foomatic-rip is last filter, foomatic-rip is mainly used as
	     Ghostscript wrapper to use Ghostscript's built-in printer
	     drivers. Here there is also no access to the pages so that we
	     delegate the logging to the PDF filter */
	  page_logging = 1;
	}
	else if (!strcasecmp(lastfilter, "hpps"))
	{
	  /* hpps is last filter, hpps is part of HPLIP and it is a bug that
	     it does not do the page logging. */
	  page_logging = 1;
	}
	else
	{
	  /* All the other filters (printer drivers) log pages as expected. */
	  page_logging = 0;
	}
      }
      else
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterLoadPPD: Last filter could not get determined, "
		     "page logging by the PDF filter turned off.");
	  page_logging = 0;
      }
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "ppdFilterLoadPPD: Last filter determined by the PPD: %s; "
		   "Final data MIME type: %s => PDF filter will %slog pages in "
		   "page_log.",
		   (lastfilter ? lastfilter : "None"),
		   (data->final_content_type ? data->final_content_type :
		    "(not supplied)"),
		   (page_logging == 0 ? "not " : ""));
    }

    /* Pass on the result as "pdf-filter-page-logging" option */
    data->num_options = cupsAddOption("pdf-filter-page-logging",
				      (page_logging == 1 ? "On" : "Off"),
				      data->num_options, &(data->options));
  }

  return (0);
}


/*
 * 'ppdFilterFreePPDFile()' - After being done with the filter
 *                            functions free the memory used by the
 *                            PPD file data in the data structure. If
 *                            the pointer to the "libppd" is NULL, do
 *                            nothing.
 */

void
ppdFilterFreePPDFile(cf_filter_data_t *data) /* I - Job and printer data */
{
  ppd_filter_data_ext_t *filter_data_ext =
    (ppd_filter_data_ext_t *)cfFilterDataRemoveExt(data,
						   PPD_FILTER_DATA_EXT);

  if (filter_data_ext)
  {
    if (filter_data_ext->ppd)
      /* ppdClose() frees not only the main data structure but also the cache */
      ppdClose(filter_data_ext->ppd);

    if (filter_data_ext->ppdfile)
      free(filter_data_ext->ppdfile);

    free(filter_data_ext);

    ppdFilterFreePPD(data);
  }
}


/*
 * 'ppdFilterFreePPD()' - After being done with the filter functions
 *                        free the memory used by the PPD file data in
 *                        the data structure. If the pointers to the
 *                        data extracted from the PPD are NULL, do
 *                        nothing.
 */

void
ppdFilterFreePPD(cf_filter_data_t *data) /* I - Job and printer data */
{
  if (data->printer_attrs)
  {
    ippDelete(data->printer_attrs);
    data->printer_attrs = NULL;
  }

  if (data->header)
  {
    free(data->header);
    data->header = NULL;
  }
}


/*
 * 'get_env_var()' - Auxiliary function for ppdFilterExternalCUPS(), gets value of
 *                   an environment variable in a list of environment variables
 *                   as used by the execve() function
 */

static char *             /* O - The value, NULL if variable is not in list */
get_env_var(char *name,   /* I - Name of environment variable to read */
	    char **env)   /* I - List of environment variable serttings */
{
  int i = 0;


  if (env)
    for (i = 0; env[i]; i ++)
      if (strncmp(env[i], name, strlen(name)) == 0 &&
	  strlen(env[i]) > strlen(name) &&
	  env[i][strlen(name)] == '=')
	return (env[i] + strlen(name) + 1);

  return (NULL);
}


/*
 * 'add_env_var()' - Auxiliary function for ppdFilterExternalCUPS(), adds/sets
 *                   an environment variable in a list of environment variables
 *                   as used by the execve() function
 */

static int                /* O - Index of where the new value got inserted in
			         the list */
add_env_var(char *name,   /* I - Name of environment variable to set */
	    char *value,  /* I - Value of environment variable to set */
	    char ***env)  /* I - List of environment variable serttings */
{
  char *p;
  int i = 0,
      name_len;


  if (!name || !env || !name[0])
    return (-1);

  /* Assemble a "VAR=VALUE" string and the string length of "VAR" */
  if ((p = strchr(name, '=')) != NULL)
  {
    /* User supplied "VAR=VALUE" as name and NULL as value */
    if (value)
      return (-1);
    name_len = p - name;
    p = strdup(name);
  }
  else
  {
    /* User supplied variable name and value as the name and as the value */
    name_len = strlen(name);
    p = (char *)calloc(strlen(name) + (value ? strlen(value) : 0) + 2,
		       sizeof(char));
    sprintf(p, "%s=%s", name, (value ? value : ""));
  }

  /* Check whether we already have this variable in the list and update its
     value if it is there */
  if (*env)
    for (i = 0; (*env)[i]; i ++)
      if (strncmp((*env)[i], p, name_len) == 0 && (*env)[i][name_len] == '=')
      {
	free((*env)[i]);
	(*env)[i] = p;
	return (i);
      }

  /* Add the variable as new item to the list */
  *env = (char **)realloc(*env, (i + 2) * sizeof(char *));
  (*env)[i] = p;
  (*env)[i + 1] = NULL;
  return (i);
}


/*
 * 'sanitize_device_uri()' - Remove authentication info from a device URI
 */

static char *                           /* O - Sanitized URI */
sanitize_device_uri(const char *uri,	/* I - Device URI */
		    char *buf,          /* I - Buffer for output */
		    size_t bufsize)     /* I - Size of buffer */
{
  char	*start,				/* Start of data after scheme */
	*slash,				/* First slash after scheme:// */
	*ptr;				/* Pointer into user@host:port part */


  /* URI not supplied */
  if (!uri)
    return (NULL);

  /* Copy the device URI to a temporary buffer so we can sanitize any auth
   * info in it... */
  strncpy(buf, uri, bufsize);

  /* Find the end of the scheme:// part... */
  if ((ptr = strchr(buf, ':')) != NULL)
  {
    for (start = ptr + 1; *start; start ++)
      if (*start != '/')
        break;

    /* Find the next slash (/) in the URI... */
    if ((slash = strchr(start, '/')) == NULL)
      slash = start + strlen(start);	/* No slash, point to the end */

    /* Check for an @ sign before the slash... */
    if ((ptr = strchr(start, '@')) != NULL && ptr < slash)
    {
      /* Found an @ sign and it is before the resource part, so we have
	 an authentication string.  Copy the remaining URI over the
	 authentication string... */
      memmove(start, ptr + 1, strlen(ptr + 1) + 1);
    }
  }

  /* Return the sanitized URI... */
  return (buf);
}


/*
 * 'fcntl_add_cloexec()' - Add FD_CLOEXEC flag to the flags
 *                         of a given file descriptor.
 */

static int                /* Return value of fcntl() */
fcntl_add_cloexec(int fd) /* File descriptor to add FD_CLOEXEC to */
{
  return fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}


/*
 * 'fcntl_add_nonblock()' - Add O_NONBLOCK flag to the flags
 *                          of a given file descriptor.
 */

static int                 /* Return value of fcntl() */
fcntl_add_nonblock(int fd) /* File descriptor to add O_NONBLOCK to */
{
  return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}


/*
 * 'ppdFilterExternalCUPS()' - Filter function which calls an external,
 *                          classic CUPS filter, for example a
 *                          (proprietary) printer driver which cannot
 *                          be converted to a filter function or is to
 *                          awkward or risky to convert for example
 *                          when the printer hardware is not available
 *                          for testing
 */

int                                     /* O - Error status */
ppdFilterExternalCUPS(int inputfd,      /* I - File descriptor input stream */
		   int outputfd,        /* I - File descriptor output stream */
		   int inputseekable,   /* I - Is input stream seekable? */
		   cf_filter_data_t *data, /* I - Job and printer data */
		   void *parameters)    /* I - Filter-specific parameters */
{
  ppd_filter_data_ext_t *filter_data_ext =
    (ppd_filter_data_ext_t *)cfFilterDataGetExt(data,
						PPD_FILTER_DATA_EXT);
  ppd_filter_external_cups_t *params = (ppd_filter_external_cups_t *)parameters;
  int           i;
  int           is_backend = 0;      /* Do we call a CUPS backend? */
  int		pid,		     /* Process ID of filter */
                stderrpid,           /* Process ID for stderr logging process */
                wpid;                /* PID reported as terminated */
  int		fd;		     /* Temporary file descriptor */
  int           backfd, sidefd;      /* file descriptors for back and side
                                        channels */
  int           stderrpipe[2];       /* Pipe to log stderr */
  cups_file_t   *fp;                 /* File pointer to read log lines */
  char          buf[2048];           /* Log line buffer */
  cf_loglevel_t log_level;           /* Log level of filter's log message */
  char          *ptr1, *ptr2,
                *msg,                /* Filter log message */
                *filter_name;        /* Filter name for logging */
  char          filter_path[1024];   /* Full path of the filter */
  char          **argv,		     /* Command line args for filter */
                **envp = NULL;       /* Environment variables for filter */
  int           num_all_options = 0;
  cups_option_t *all_options = NULL;
  char          job_id_str[16],
                copies_str[16],
                *options_str = NULL;
  cups_option_t *opt;
  int status = 65536;
  int wstatus;
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  if (!params->filter || !params->filter[0]) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterExternalCUPS: Filter executable path/command not specified");
    return (1);
  }

  /* Check whether back/side channel FDs are valid and not all-zero
     from calloc'ed filter_data */
  if (data->back_pipe[0] == 0 && data->back_pipe[1] == 0)
    data->back_pipe[0] = data->back_pipe[1] = -1;
  if (data->side_pipe[0] == 0 && data->side_pipe[1] == 0)
    data->side_pipe[0] = data->side_pipe[1] = -1;

  /* Select the correct end of the back/side channel pipes:
     [0] for filters, [1] for backends */
  is_backend = (params->is_backend ? 1 : 0);
  backfd = data->back_pipe[is_backend];
  sidefd = data->side_pipe[is_backend];

  /* Filter name for logging */
  if ((filter_name = strrchr(params->filter, '/')) != NULL)
    filter_name ++;
  else
    filter_name = (char *)params->filter;

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Copy the current environment variables and add some important ones
  * needed for correct execution of the CUPS filter (which is not running
  * out of CUPS here)
  */

  /* Some default environment variables from CUPS, will get overwritten
     if also defined in the environment in which the caller is started
     or in the parameters */
  add_env_var("CUPS_DATADIR", CUPS_DATADIR, &envp);
  add_env_var("CUPS_SERVERBIN", CUPS_SERVERBIN, &envp);
  add_env_var("CUPS_SERVERROOT", CUPS_SERVERROOT, &envp);
  add_env_var("CUPS_STATEDIR", CUPS_STATEDIR, &envp);
  add_env_var("SOFTWARE", "CUPS/2.5.99", &envp); /* Last CUPS with PPDs */
  if (data->content_type)
    add_env_var("CONTENT_TYPE", data->content_type, &envp);
  if (data->final_content_type)
    add_env_var("FINAL_CONTENT_TYPE", data->final_content_type, &envp);

  /* Copy the environment in which the caller got started */
  if (environ)
    for (i = 0; environ[i]; i ++)
      add_env_var(environ[i], NULL, &envp);

  /* Set the environment variables given by the parameters */
  if (params->envp)
    for (i = 0; params->envp[i]; i ++)
      add_env_var(params->envp[i], NULL, &envp);

  /* Add CUPS_SERVERBIN to the beginning of PATH */
  ptr1 = get_env_var("PATH", envp);
  ptr2 = get_env_var("CUPS_SERVERBIN", envp);
  if (ptr2 && ptr2[0])
  {
    if (ptr1 && ptr1[0])
    {
      snprintf(buf, sizeof(buf), "%s/%s:%s",
	       ptr2, params->is_backend ? "backend" : "filter", ptr1);
      ptr1 = buf;
    }
    else
      ptr1 = ptr2;
    add_env_var("PATH", ptr1, &envp);
  }

  if (params->is_backend < 2) /* Not needed in discovery mode of backend */
  {
    /* Print queue name from filter data */
    if (data->printer)
      add_env_var("PRINTER", data->printer, &envp);
    else
      add_env_var("PRINTER", "Unknown", &envp);

    /* PPD file path/name from filter data, required for most CUPS filters */
    if (filter_data_ext && filter_data_ext->ppdfile)
      add_env_var("PPD", filter_data_ext->ppdfile, &envp);

    /* Device URI from parameters */
    if (params->is_backend && params->device_uri)
      add_env_var("DEVICE_URI", (char *)params->device_uri, &envp);
  }

  /* Determine full path for the filter */
  if (params->filter[0] == '/' ||
      (ptr1 = get_env_var("CUPS_SERVERBIN", envp)) == NULL || !ptr1[0])
    strncpy(filter_path, params->filter, sizeof(filter_path) - 1);
  else
    snprintf(filter_path, sizeof(filter_path), "%s/%s/%s", ptr1,
	     params->is_backend ? "backend" : "filter", params->filter);

  /* Log the resulting list of environment variable settings
     (with any authentication info removed)*/
  if (log)
  {
    for (i = 0; envp[i]; i ++)
      if (!strncmp(envp[i], "AUTH_", 5))
	log(ld, CF_LOGLEVEL_DEBUG, "ppdFilterExternalCUPS (%s): envp[%d]: AUTH_%c****",
	    filter_name, i, envp[i][5]);
      else if (!strncmp(envp[i], "DEVICE_URI=", 11))
	log(ld, CF_LOGLEVEL_DEBUG, "ppdFilterExternalCUPS (%s): envp[%d]: DEVICE_URI=%s",
	    filter_name, i, sanitize_device_uri(envp[i] + 11,
						buf, sizeof(buf)));
      else
	log(ld, CF_LOGLEVEL_DEBUG, "ppdFilterExternalCUPS (%s): envp[%d]: %s",
	    filter_name, i, envp[i]);
  }

  if (params->is_backend < 2) {
   /*
    * Filter or backend for job execution
    */

   /*
    * Join the options from the filter data and from the parameters
    * If an option is present in both filter data and parameters, the
    * value in the filter data has priority
    */

    for (i = 0, opt = params->options; i < params->num_options; i ++, opt ++)
      num_all_options = cupsAddOption(opt->name, opt->value, num_all_options,
				      &all_options);
    for (i = 0, opt = data->options; i < data->num_options; i ++, opt ++)
      num_all_options = cupsAddOption(opt->name, opt->value, num_all_options,
				      &all_options);

   /*
    * Create command line arguments for the CUPS filter
    */

    argv = (char **)calloc(7, sizeof(char *));

    /* Numeric parameters */
    snprintf(job_id_str, sizeof(job_id_str) - 1, "%d",
	     data->job_id > 0 ? data->job_id : 1);
    snprintf(copies_str, sizeof(copies_str) - 1, "%d",
	     data->copies > 0 ? data->copies : 1);

    /* Options, build string of "Name1=Value1 Name2=Value2 ..." but use
       "Name" and "noName" instead for boolean options */
    for (i = 0, opt = all_options; i < num_all_options; i ++, opt ++) {
      if (strcasecmp(opt->value, "true") == 0 ||
	  strcasecmp(opt->value, "false") == 0) {
	options_str =
	  (char *)realloc(options_str,
			  ((options_str ? strlen(options_str) : 0) +
			   strlen(opt->name) +
			   (strcasecmp(opt->value, "false") == 0 ? 2 : 0) + 2) *
			  sizeof(char));
	if (i == 0)
	  options_str[0] = '\0';
	sprintf(options_str + strlen(options_str), " %s%s",
		(strcasecmp(opt->value, "false") == 0 ? "no" : ""), opt->name);
      } else {
	options_str =
	  (char *)realloc(options_str,
			  ((options_str ? strlen(options_str) : 0) +
			   strlen(opt->name) + strlen(opt->value) + 3) *
			  sizeof(char));
	if (i == 0)
	  options_str[0] = '\0';
	sprintf(options_str + strlen(options_str), " %s=%s", opt->name, opt->value);
      }
    }

    /* Find DEVICE_URI environment variable */
    if (params->is_backend && !params->device_uri)
      for (i = 0; envp[i]; i ++)
	if (strncmp(envp[i], "DEVICE_URI=", 11) == 0)
	  break;

    /* Add items to array */
    argv[0] = strdup((params->is_backend && params->device_uri ?
		      (char *)sanitize_device_uri(params->device_uri,
						  buf, sizeof(buf)) :
		      (params->is_backend && envp[i] ?
		       (char *)sanitize_device_uri(envp[i] + 11,
						   buf, sizeof(buf)) :
		       (data->printer ? data->printer :
			(char *)params->filter))));
    argv[1] = job_id_str;
    argv[2] = data->job_user ? data->job_user : "Unknown";
    argv[3] = data->job_title ? data->job_title : "Untitled";
    argv[4] = copies_str;
    argv[5] = options_str ? options_str + 1 : "";
    argv[6] = NULL;

    /* Log the arguments */
    if (log)
      for (i = 0; argv[i]; i ++)
	log(ld, CF_LOGLEVEL_DEBUG, "ppdFilterExternalCUPS (%s): argv[%d]: %s",
	    filter_name, i, argv[i]);
  } else {
   /*
    * Backend in device discovery mode
    */

    argv = (char **)calloc(2, sizeof(char *));
    argv[0] = strdup((char *)params->filter);
    argv[1] = NULL;
  }

 /*
  * Execute the filter
  */

  if (pipe(stderrpipe) < 0) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterExternalCUPS (%s): Could not create pipe for stderr: %s",
		 filter_name, strerror(errno));
    return (1);
  }

  if ((pid = fork()) == 0) {
   /*
    * Child process goes here...
    *
    * Update stdin/stdout/stderr as needed...
    */

    if (inputfd != 0) {
      if (inputfd < 0) {
        inputfd = open("/dev/null", O_RDONLY);
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterExternalCUPS (%s): No input file descriptor supplied for CUPS filter - %s",
		   filter_name, strerror(errno));
      }

      if (inputfd > 0) {
	fcntl_add_cloexec(inputfd);
        if (dup2(inputfd, 0) < 0) {
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "ppdFilterExternalCUPS (%s): Failed to connect input file descriptor with CUPS filter's stdin - %s",
		       filter_name, strerror(errno));
	  goto fd_error;
	} else
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "ppdFilterExternalCUPS (%s): Connected input file descriptor %d to CUPS filter's stdin.",
		       filter_name, inputfd);
	close(inputfd);
      }
    } else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "ppdFilterExternalCUPS (%s): Input comes from stdin, letting the filter grab stdin directly",
		   filter_name);

    if (outputfd != 1) {
      if (outputfd < 0)
        outputfd = open("/dev/null", O_WRONLY);

      if (outputfd > 1) {
	fcntl_add_cloexec(outputfd);
	dup2(outputfd, 1);
	close(outputfd);
      }
    }

    if (strcasestr(params->filter, "gziptoany")) {
      /* Send stderr to the Nirwana if we are running gziptoany, as
	 gziptoany emits a false "PAGE: 1 1" */
      if ((fd = open("/dev/null", O_RDWR)) > 2) {
	fcntl_add_cloexec(fd);
	dup2(fd, 2);
	close(fd);
      } else
        close(fd);
    } else {
      /* Send stderr into pipe for logging */
      fcntl_add_cloexec(stderrpipe[1]);
      dup2(stderrpipe[1], 2);
      fcntl_add_nonblock(2);
    }
    close(stderrpipe[0]);
    close(stderrpipe[1]);

    if (params->is_backend < 2) { /* Not needed in discovery mode of backend */
      /* Back channel */
      if (backfd != 3 && backfd >= 0) {
	dup2(backfd, 3);
	close(backfd);
	fcntl_add_nonblock(3);
      } else if (backfd < 0) {
	if ((backfd = open("/dev/null", O_RDWR)) > 3) {
	  dup2(backfd, 3);
	  close(backfd);
	} else
	  close(backfd);
	fcntl_add_nonblock(3);
      }

      /* Side channel */
      if (sidefd != 4 && sidefd >= 0) {
	dup2(sidefd, 4);
	close(sidefd);
	fcntl_add_nonblock(4);
      } else if (sidefd < 0) {
	if ((sidefd = open("/dev/null", O_RDWR)) > 4) {
	  dup2(sidefd, 4);
	  close(sidefd);
	} else
	  close(sidefd);
	fcntl_add_nonblock(4);
      }
    }

   /*
    * Execute command...
    */

    execve(filter_path, argv, envp);

    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterExternalCUPS (%s): Execution of %s %s failed - %s",
		 filter_name, params->is_backend ? "backend" : "filter",
		 filter_path, strerror(errno));

  fd_error:
    exit(errno);
  } else if (pid > 0) {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "ppdFilterExternalCUPS (%s): %s (PID %d) started.",
		 filter_name, filter_path, pid);
  } else {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterExternalCUPS (%s): Unable to fork process for %s %s",
		 filter_name, params->is_backend ? "backend" : "filter",
		 filter_path);
    close(stderrpipe[0]);
    close(stderrpipe[1]);
    status = 1;
    goto out;
  }
  if (inputfd >= 0)
    close(inputfd);
  if (outputfd >= 0)
    close(outputfd);

 /*
  * Log the filter's stderr
  */

  if ((stderrpid = fork()) == 0) {
   /*
    * Child process goes here...
    */

    close(stderrpipe[1]);
    fp = cupsFileOpenFd(stderrpipe[0], "r");
    while (cupsFileGets(fp, buf, sizeof(buf)))
      if (log) {
	if (strncmp(buf, "DEBUG: ", 7) == 0) {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 7;
	} else if (strncmp(buf, "DEBUG2: ", 8) == 0) {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 8;
	} else if (strncmp(buf, "INFO: ", 6) == 0) {
	  log_level = CF_LOGLEVEL_INFO;
	  msg = buf + 6;
	} else if (strncmp(buf, "WARNING: ", 9) == 0) {
	  log_level = CF_LOGLEVEL_WARN;
	  msg = buf + 9;
	} else if (strncmp(buf, "ERROR: ", 7) == 0) {
	  log_level = CF_LOGLEVEL_ERROR;
	  msg = buf + 7;
	} else if (strncmp(buf, "PAGE: ", 6) == 0 ||
		   strncmp(buf, "ATTR: ", 6) == 0 ||
		   strncmp(buf, "STATE: ", 7) == 0 ||
		   strncmp(buf, "PPD: ", 5) == 0) {
	  log_level = CF_LOGLEVEL_CONTROL;
	  msg = buf;
	} else {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf;
	}
	if (log_level == CF_LOGLEVEL_CONTROL)
	  log(ld, log_level, msg);
	else
	  log(ld, log_level, "ppdFilterExternalCUPS (%s): %s", filter_name, msg);
      }
    cupsFileClose(fp);
    /* No need to close the fd stderrpipe[0], as cupsFileClose(fp) does this
       already */
    /* Ignore errors of the logging process */
    exit(0);
  } else if (stderrpid > 0) {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "ppdFilterExternalCUPS (%s): Logging (PID %d) started.",
		 filter_name, stderrpid);
  } else {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterExternalCUPS (%s): Unable to fork process for logging",
		 filter_name);
    close(stderrpipe[0]);
    close(stderrpipe[1]);
    status = 1;
    goto out;
  }

  close(stderrpipe[0]);
  close(stderrpipe[1]);

 /*
  * Wait for filter and logging processes to finish
  */

  status = 0;

  while (pid > 0 || stderrpid > 0) {
    if ((wpid = wait(&wstatus)) < 0) {
      if (errno == EINTR && iscanceled && iscanceled(icd)) {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "ppdFilterExternalCUPS (%s): Job canceled, killing %s ...",
		     filter_name, params->is_backend ? "backend" : "filter");
	kill(pid, SIGTERM);
	pid = -1;
	kill(stderrpid, SIGTERM);
	stderrpid = -1;
	break;
      } else
	continue;
    }

    /* How did the filter terminate */
    if (wstatus) {
      if (WIFEXITED(wstatus)) {
	/* Via exit() anywhere or return() in the main() function */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterExternalCUPS (%s): %s (PID %d) stopped with status %d",
		     filter_name,
		     (wpid == pid ?
		      (params->is_backend ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WEXITSTATUS(wstatus));
      } else {
	/* Via signal */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterExternalCUPS (%s): %s (PID %d) crashed on signal %d",
		     filter_name,
		     (wpid == pid ?
		      (params->is_backend ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WTERMSIG(wstatus));
      }
      status = 1;
    } else {
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "ppdFilterExternalCUPS (%s): %s (PID %d) exited with no errors.",
		   filter_name,
		   (wpid == pid ?
		    (params->is_backend ? "Backend" : "Filter") : "Logging"),
		   wpid);
    }
    if (wpid == pid)
      pid = -1;
    else  if (wpid == stderrpid)
      stderrpid = -1;
  }

 /*
  * Clean up
  */

 out:
  cupsFreeOptions(num_all_options, all_options);
  if (options_str)
    free(options_str);
  free(argv[0]);
  free(argv);
  for (i = 0; envp[i]; i ++)
    free(envp[i]);
  free(envp);

  return (status);
}


/*
 * 'ppdFilterEmitJCL()' - Wrapper for the PDF-generating filter
 *                        functions to emit JCL (PJL) before and after
 *                        the PDF output.
 */

int                                   /* O - Error status */
ppdFilterEmitJCL(int inputfd,         /* I - File descriptor input stream */
		 int outputfd,        /* I - File descriptor output stream */
		 int inputseekable,   /* I - Is input stream seekable? */
		 cf_filter_data_t *data, /* I - Job and printer data */
		 void *parameters,    /* I - Filter-specific parameters */
		 cf_filter_function_t orig_filter)
{
  ppd_filter_data_ext_t *filter_data_ext =
    (ppd_filter_data_ext_t *)cfFilterDataGetExt(data,
						PPD_FILTER_DATA_EXT);
  const char    *val;
  int           streaming = 0;
  int           ret = 1;
  size_t        bytes;
  char          buf[8192];
  int           outfds[2], pid = -1;
  FILE          *fp;
  int           hw_copies = 1;
  bool          hw_collate = false;
  int		status,		 /* Exit status */
                retval = 1;	 /* Return value */
  cf_logfunc_t  log = data->logfunc;
  void          *ld = data->logdata;


 /*
  * Check whether we are in streaming mode (cfFilterPDFToPDF() only)
  *
  * If we are in streaming mode of cfFilterPDFToPDF() we only apply
  * JCL and do not run the job through cfFilterPDFToPDF() itself (so
  * no page management, form flattening, page size/orientation
  * adjustment, ...)
  */

  if (orig_filter == cfFilterPDFToPDF &&
      (val = cupsGetOption("filter-streaming-mode",
			   data->num_options, data->options)) != NULL &&
      (strcasecmp(val, "false") && strcasecmp(val, "off") &
       strcasecmp(val, "no")))
  {
    streaming = 1;
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "ppdFilterEmitJCL: Streaming mode: No PDF processing, only adding of JCL");
  }

 /*
  * Call the original filter function without forking if we suppress
  * JCL output via option...
  */

  if ((val = cupsGetOption("emit-jcl",
			   data->num_options, data->options)) != NULL &&
      (strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0 ||
       strcasecmp(val, "no") == 0))
  {
    if (!streaming)
      /* Call actual filter function from libcupsfilters */
      ret = orig_filter(inputfd, outputfd, inputseekable, data, parameters);
    else
    {
      /* Just pass through the data unchanged... */
      fp = fdopen(outputfd, "w");
      while ((bytes = read(inputfd, buf, sizeof(buf))) > 0)
	fwrite(buf, 1, bytes, fp);
      close(inputfd);
      fclose(fp);
      ret = 0;
    }
    return (ret);
  }

  if (!streaming)
  {
   /*
    * Create pipe for output of original filter function...
    */

    if (pipe(outfds) < 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "ppdFilterEmitJCL: Could not create pipe for ouput: %s",
		   strerror(errno));
      return (1);
    }

   /*
    * Fork child process for original filter function...
    */

    if ((pid = fork()) == 0)
    {
      /* Send output into pipe for adding JCL */
      fcntl_add_cloexec(outfds[1]);
      close(outfds[0]);

      /* Call actual filter function from libcupsfilters */
      ret = orig_filter(inputfd, outfds[1], inputseekable, data, parameters);

      exit(ret);
    }
    else if (pid > 0)
    {
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "ppdFilterEmitJCL: Filter function (PID %d) started.",
		   pid);
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterEmitJCL: Unable to fork process for filter function.");
      close(outfds[0]);
      close(outfds[1]);
      retval = 1;
      goto out;
    }

    if (inputfd >= 0)
      close(inputfd);
    close(outfds[1]);
  }
  else

   /*
    * In Streaming mode we simply copy the input
    */

    outfds[0] = inputfd;

 /*
  * Check options for caller's instructions about hardware copies/collate
  */

  hw_copies =
    ((val = cupsGetOption("hardware-copies",
			  data->num_options, data->options)) != NULL &&
     (strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0 ||
      strcasecmp(val, "yes") == 0)) ?
    data->copies : 1;

  hw_collate =
    (hw_copies > 1 &&
     (val = cupsGetOption("hardware-collate",
			  data->num_options, data->options)) != NULL &&
     (strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0 ||
      strcasecmp(val, "yes") == 0));

 /*
  * Assemble the output: Exit server, JCL preamble, PDF output, JCL postamble
  */

  fp = fdopen(outputfd, "w");
  if (filter_data_ext)
  {
    ppdEmit(filter_data_ext->ppd, fp, PPD_ORDER_EXIT);
    ppdEmitJCLPDF(filter_data_ext->ppd, fp,
		  data->job_id, data->job_user, data->job_title,
		  hw_copies, hw_collate);
  }
  while ((bytes = read(outfds[0], buf, sizeof(buf))) > 0)
    fwrite(buf, 1, bytes, fp);
  close(outfds[0]);
  if (filter_data_ext)
    ppdEmitJCLEnd(filter_data_ext->ppd, fp);
  fclose(fp);

  if (!streaming)
  {
   /*
    * Wait for filter process to finish
    */

  retry_wait:
    if (waitpid (pid, &status, 0) == -1)
    {
      if (errno == EINTR)
	goto retry_wait;
      if (log)
	log(ld, CF_LOGLEVEL_DEBUG,
	    "ppdFilterEmitJCL: Filter function (PID %d) stopped with an error: %s!",
	    pid, strerror(errno));
      goto out;
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "ppdFilterEmitJCL: Filter function (PID %d) exited with no errors.",
		 pid);

    /* How did the filter function terminate */
    if (WIFEXITED(status))
      /* Via exit() anywhere or return() in the main() function */
      retval = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      /* Via signal */
      retval = 256 * WTERMSIG(status);
  }
  else
    retval = 0;

 out:
  return (retval);
}


/*
 * 'ppdFilterImageToPDF()' - Wrapper for the filter function
 *                           cfFilterImageToPDF() to add PPD file
 *                           support to it.
 */

int                                       /* O - Error status */
ppdFilterImageToPDF(int inputfd,          /* I - File descriptor input stream */
		    int outputfd,         /* I - File descriptor output stream*/
		    int inputseekable,    /* I - Is input stream seekable? */
		    cf_filter_data_t *data, /* I - Job and printer data */
		    void *parameters)     /* I - Filter-specific parameters */
{
  return ppdFilterEmitJCL(inputfd, outputfd, inputseekable, data,
			  parameters, cfFilterImageToPDF);
}


/*
 * 'ppdFilterPDFtoPDF()' - Wrapper for the filter function 
 *                         cfFilterPDFtoPDF() to add PPD file
 *                         support to it.
 */

int                                    /* O - Error status */
ppdFilterPDFToPDF(int inputfd,         /* I - File descriptor input stream */
		  int outputfd,        /* I - File descriptor output stream */
		  int inputseekable,   /* I - Is input stream seekable? */
		  cf_filter_data_t *data, /* I - Job and printer data */
		  void *parameters)    /* I - Filter-specific parameters */
{
  return ppdFilterEmitJCL(inputfd, outputfd, inputseekable, data,
			  parameters, cfFilterPDFToPDF);
}


/*
 * 'ppdFilterUniversal()' - Wrapper for the filter function
 *                          cfFilterUniversal() to add PPD file
 *                          support to it.
 */

int                                     /* O - Error status */
ppdFilterUniversal(int inputfd,         /* I - File descriptor input stream */
		   int outputfd,        /* I - File descriptor output stream*/
		   int inputseekable,   /* I - Is input stream seekable? */
		   cf_filter_data_t *data, /* I - Job and printer data */
		   void *parameters)    /* I - Filter-specific parameters */
{
  ppd_filter_data_ext_t *filter_data_ext =
    (ppd_filter_data_ext_t *)cfFilterDataGetExt(data,
						PPD_FILTER_DATA_EXT);
  char *input;
  char *final_output;
  char output[256];
  cf_filter_universal_parameter_t universal_parameters;
  ppd_file_t *ppd = NULL;
  ppd_cache_t *cache;
  cups_array_t *filter_chain;
  cf_filter_filter_in_chain_t extra_filter, universal_filter;
  int ret;
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;


  universal_parameters = *(cf_filter_universal_parameter_t *)parameters;
  input = data->content_type;
  if (input == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdFilterUniversal: No input data format supplied.");
    return (1);
  }

  final_output = data->final_content_type;
  if (final_output == NULL)
  {
    final_output = universal_parameters.actual_output_type;
    if (final_output == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "ppdFilterUniversal: No output data format supplied.");
      return (1);
    }
  }

  if (filter_data_ext)
    ppd = filter_data_ext->ppd;

  if (universal_parameters.actual_output_type)
    strncpy(output, universal_parameters.actual_output_type,
	    sizeof(output) - 1);
  else if (ppd)
  {
    strncpy(output, data->final_content_type, sizeof(output) - 1);

    cache = ppd ? ppd->cache : NULL;

    /* Check whether our output format (under CUPS it is taken from
       the FINAL_CONTENT_TYPE env variable) is the destination format
       (2nd word) of a "*cupsFilter2: ..." line (string has 4 words),
       in this case the specified filter (4th word) does the last
       step, converting from the input format (1st word) of the line
       to the destination format and so we only need to convert to the
       input format. In this case we need to correct our output
       format.

       If there is more than one line with the given output format and
       an inpout format we can produce, we select the one with the
       lowest cost value (3rd word) as this is the one which CUPS
       should have chosen for this job.

       If we have "*cupsFilter: ..." lines (without "2", string with 3
       words) we do not need to do anything special, as the input
       format specified is the FIMAL_CONTENT_TYPE which CUPS supplies
       to us and into which we have to convert. So we quit parsing if
       the first line has only 3 words, as if CUPS uses the
       "*cupsFilter: ..."  lines only if there is no "*cupsFilter2:
       ..." line min the PPD, so if we encounter a line with only 3
       words the other lines will have only 3 words, too and nothing
       has to be done. */

    if (ppd && ppd->num_filters && cache)
    {
      int lowest_cost = INT_MAX;
      char *filter;

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "ppdFilterUniversal: \"*cupsFilter(2): ...\" lines in the PPD file:");

      for (filter = (char *)cupsArrayFirst(cache->filters);
	   filter;
	   filter = (char *)cupsArrayNext(cache->filters))
      {
	char buf[256];
	char *ptr,
	  *in = NULL,
	   *out = NULL,
	  *coststr = NULL;
	int cost;

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "ppdFilterUniversal:    %s", filter);

	/* String of the "*cupsfilter:" or "*cupsfilter2:" line */
	strncpy(buf, filter, sizeof(buf) - 1);

	/* Separate the words */
	in = ptr = buf;
	while (*ptr && !isspace(*ptr)) ptr ++;
	if (!*ptr) goto error;
	*ptr = '\0';
	ptr ++;
	while (*ptr && isspace(*ptr)) ptr ++;
	if (!*ptr) goto error;
	out = ptr;
	while (*ptr && !isspace(*ptr)) ptr ++;
	if (!*ptr) goto error;
	*ptr = '\0';
	ptr ++;
	while (*ptr && isspace(*ptr)) ptr ++;
	if (!*ptr) goto error;
	coststr = ptr;
	if (!isdigit(*ptr)) goto error;
	while (*ptr && !isspace(*ptr)) ptr ++;
	if (!*ptr) goto error;
	*ptr = '\0';
	ptr ++;
	while (*ptr && isspace(*ptr)) ptr ++;
	if (!*ptr) goto error;
	cost = atoi(coststr);

	/* Valid "*cupsFilter2: ..." line ... */
	if (/* Must be of lower cost than what we selected before */
	    cost < lowest_cost &&
	    /* Must have our FINAL_CONTENT_TYPE as output */
	    strcasecmp(out, final_output) == 0 &&
	    /* Must have as input a format we are able to produce */
	    (strcasecmp(in, "application/vnd.cups-raster") == 0 ||
	     strcasecmp(in, "application/vnd.cups-pdf") == 0 ||
	     strcasecmp(in, "application/vnd.cups-postscript") == 0 ||
	     strcasecmp(in, "application/pdf") == 0 ||
	     strcasecmp(in, "image/pwg-raster") == 0 ||
	     strcasecmp(in, "image/urf") == 0 ||
	     strcasecmp(in, "application/PCLm") == 0 ||
	     strcasecmp(in, "application/postscript") == 0))
	{
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "ppdFilterUniversal:       --> Selecting this line");
	  /* Take the input format of the line as output format for us */
	  strncpy(output, in, sizeof(output));
	  /* Update the minimum cost found */
	  lowest_cost = cost;
	  /* We cannot find a "better" solution ... */
	  if (lowest_cost == 0)
	  {
	    if (log) log(ld, CF_LOGLEVEL_DEBUG,
			 "ppdFilterUniversal:    Cost value is down to zero, stopping reading further lines");
	    break;
	  }
	}

	continue;

      error:
	if (lowest_cost == INT_MAX && coststr)
	{
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "ppdFilterUniversal: PPD uses \"*cupsFilter: ...\" lines, so we always convert to format given by FINAL_CONTENT_TYPE");
	  break;
	}
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "ppdFilterUniversal: Invalid \"*cupsFilter2: ...\" line in PPD: %s",
		     filter);
      }

    }
  }

  if (strcasecmp(output, final_output) != 0)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "ppdFilterUniversal: Converting from %s to %s, final output will be %s",
		 input, output, final_output);
    universal_parameters.actual_output_type = output;
  }
  else
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "ppdFilterUniversal: Converting from %s to %s", input, output);
  }

  if (strstr(output, "postscript"))
  {
    /* PostScript output, we need to add libppd's ppdFilterPDFToPS() to
       the end of the chain */
    universal_parameters.actual_output_type = "application/vnd.cups-pdf";
    extra_filter.function = ppdFilterPDFToPS;
    extra_filter.parameters = NULL;
    extra_filter.name = "pdftops";
  }
#if HAVE_CUPS_3_X
  else if (ppd && ppd->jcl_pdf && strstr(output, "pdf"))
#else
  else if (ppd && ppdFindAttr(ppd, "JCLToPDFInterpreter", NULL) &&
	   strstr(output, "pdf"))
#endif
  {
    /* Classic PDF printer, with job control via JCL/PJL, needs
       libppd's ppdFilterPDFToPDF() filter function which adds PPD's JCL/PJL*/
    if (!strncmp(input, "image/", 6) &&
	strcmp(input + 6, "urf") && strcmp(input + 6, "pwg-raster"))
      /* Input is an image file: call only ppdFilterImageToPDF() as
	 cfFilterImageToPDF() + ppdFilterPDFToPDF() would apply the margins
	 twice */
      return ppdFilterImageToPDF(inputfd, outputfd, inputseekable, data, NULL);
    else
    {
      /* Any other input format: Replace the cfFilterPDFToPDF() call by
	 cfFilterUniversal() with a call of ppdFilterPDFToPDF(), so that
	 JCL/PJL from the PPD gets added. */
      universal_parameters.actual_output_type = "application/pdf";
      extra_filter.function = ppdFilterPDFToPDF;
      extra_filter.parameters = NULL;
      extra_filter.name = "pdftopdf+JCL";
    }
  }
  else
    /* No extra filter needed, cfFilterUniversal() does the job
       correctly with only the filter functions of libcupsfilters */
    extra_filter.function = NULL;

  if (extra_filter.function)
  {
    /* Chain cfFilterUniversal() with the extra filter function */
    universal_filter.function = cfFilterUniversal;
    universal_filter.parameters = &universal_parameters;
    universal_filter.name = "universal";
    filter_chain = cupsArrayNew(NULL, NULL);
    cupsArrayAdd(filter_chain, &universal_filter);
    cupsArrayAdd(filter_chain, &extra_filter);
    ret = cfFilterChain(inputfd, outputfd, inputseekable, data,
			filter_chain);
    cupsArrayDelete(filter_chain);
  }
  else
    ret = cfFilterUniversal(inputfd, outputfd, inputseekable, data,
			    &universal_parameters);

  return (ret);
}


/*
 * 'ppdFilterSetCommonOptions()' - Set common filter options for media size,
 *                                 etc. based on PPD file
 */

void
ppdFilterSetCommonOptions(
    ppd_file_t    *ppd,			/* I - PPD file */
    int           num_options,          /* I - Number of options */
    cups_option_t *options,             /* I - Options */
    int           change_size,		/* I - Change page size? */
    int           *Orientation,         /* I/O - Basic page parameters */
    int           *Duplex,
    int           *LanguageLevel,
    int           *ColorDevice,
    float         *PageLeft,
    float         *PageRight,
    float         *PageTop,
    float         *PageBottom,
    float         *PageWidth,
    float         *PageLength,
    cf_logfunc_t log,                   /* I - Logging function,
					       NULL for no logging */
    void *ld)                           /* I - User data for logging function,
					       can be NULL */
{
  ppd_size_t	*pagesize;		/* Current page size */
  const char	*val;			/* Option value */


  *Orientation = 0;		/* 0 = portrait, 1 = landscape, etc. */
  *Duplex = 0;			/* Duplexed? */
  *LanguageLevel = 1;		/* Language level of printer */
  *ColorDevice = 1;		/* Color printer? */
  *PageLeft = 18.0f;		/* Left margin */
  *PageRight = 594.0f;		/* Right margin */
  *PageBottom = 36.0f;		/* Bottom margin */
  *PageTop = 756.0f;		/* Top margin */
  *PageWidth = 612.0f;		/* Total page width */
  *PageLength = 792.0f;		/* Total page length */

  if ((pagesize = ppdPageSize(ppd, NULL)) != NULL)
  {
    int corrected = 0;
    if (pagesize->width > 0) 
      *PageWidth = pagesize->width;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page width: %.0f",
		   pagesize->width);
      corrected = 1;
    }
    if (pagesize->length > 0) 
      *PageLength = pagesize->length;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page length: %.0f",
		   pagesize->length);
      corrected = 1;
    }
    if (pagesize->top >= 0 && pagesize->top <= *PageLength) 
      *PageTop = pagesize->top;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page top margin: %.0f",
		   pagesize->top);
      if (*PageLength >= *PageBottom)
	*PageTop = *PageLength - *PageBottom;
      else
	*PageTop = *PageLength;
      corrected = 1;
    }
    if (pagesize->bottom >= 0 && pagesize->bottom <= *PageLength) 
      *PageBottom = pagesize->bottom;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page bottom margin: %.0f",
		   pagesize->bottom);
      if (*PageLength <= *PageBottom)
	*PageBottom = 0.0f;
      corrected = 1;
    }
    if (*PageBottom == *PageTop)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Bottom: %.0f; Top: %.0f",
		   *PageBottom, *PageTop);
      *PageTop = *PageLength - *PageBottom;
      if (*PageBottom == *PageTop)
      {
	*PageBottom = 0.0f;
	*PageTop = *PageLength;
      }
      corrected = 1;
    }
    if (*PageBottom > *PageTop)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Bottom: %.0f; Top: %.0f",
		   *PageBottom, *PageTop);
      float swap = *PageBottom;
      *PageBottom = *PageTop;
      *PageTop = swap;
      corrected = 1;
    }

    if (pagesize->left >= 0 && pagesize->left <= *PageWidth) 
      *PageLeft = pagesize->left;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page left margin: %.0f",
		   pagesize->left);
      if (*PageWidth <= *PageLeft)
	*PageLeft = 0.0f;
      corrected = 1;
    }
    if (pagesize->right >= 0 && pagesize->right <= *PageWidth) 
      *PageRight = pagesize->right;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page right margin: %.0f",
		   pagesize->right);
      if (*PageWidth >= *PageLeft)
	*PageRight = *PageWidth - *PageLeft;
      else
	*PageRight = *PageWidth;
      corrected = 1;
    }
    if (*PageLeft == *PageRight)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f",
		   *PageLeft, *PageRight);
      *PageRight = *PageWidth - *PageLeft;
      if (*PageLeft == *PageRight)
      {
	*PageLeft = 0.0f;
	*PageRight = *PageWidth;
      }
      corrected = 1;
    }
    if (*PageLeft > *PageRight)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f",
		   *PageLeft, *PageRight);
      float swap = *PageLeft;
      *PageLeft = *PageRight;
      *PageRight = swap;
      corrected = 1;
    }

    if (corrected)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "PPD Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   pagesize->width, pagesize->length, pagesize->left,
		   pagesize->bottom, pagesize->right, pagesize->top);
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Corrected Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   *PageWidth, *PageLength, *PageLeft,
		   *PageBottom, *PageRight, *PageTop);
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   pagesize->width, pagesize->length, pagesize->left,
		   pagesize->bottom, pagesize->right, pagesize->top);
  }

  if (ppd != NULL)
  {
    *ColorDevice   = ppd->color_device;
    *LanguageLevel = ppd->language_level;
  }

  if ((val = cupsGetOption("landscape", num_options, options)) != NULL)
  {
    if (strcasecmp(val, "no") != 0 && strcasecmp(val, "off") != 0 &&
        strcasecmp(val, "false") != 0)
    {
      if (ppd && ppd->landscape > 0)
        *Orientation = 1;
      else
        *Orientation = 3;
    }
  }
  else if ((val = cupsGetOption("orientation-requested",
				num_options, options)) != NULL)
  {
   /*
    * Map IPP orientation values to 0 to 3:
    *
    *   3 = 0 degrees   = 0
    *   4 = 90 degrees  = 1
    *   5 = -90 degrees = 3
    *   6 = 180 degrees = 2
    */

    *Orientation = atoi(val) - 3;
    if (*Orientation >= 2)
      *Orientation ^= 1;
  }

  if ((val = cupsGetOption("page-left", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageLeft = (float)atof(val);
	  break;
      case 1 :
          *PageBottom = (float)atof(val);
	  break;
      case 2 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
      case 3 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-right", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
      case 1 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
      case 2 :
          *PageLeft = (float)atof(val);
	  break;
      case 3 :
          *PageBottom = (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-bottom", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageBottom = (float)atof(val);
	  break;
      case 1 :
          *PageLeft = (float)atof(val);
	  break;
      case 2 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
      case 3 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-top", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
      case 1 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
      case 2 :
          *PageBottom = (float)atof(val);
	  break;
      case 3 :
          *PageLeft = (float)atof(val);
	  break;
    }
  }

  if (change_size)
    ppdFilterUpdatePageVars(*Orientation, PageLeft, PageRight,
			 PageTop, PageBottom, PageWidth, PageLength);

  if (ppdIsMarked(ppd, "Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "Duplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "EFDuplexing", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "EFDuplexing", "DuplexTumble") ||
      ppdIsMarked(ppd, "ARDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "ARDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexTumble"))
    *Duplex = 1;

  return;
}


/*
 * 'ppdFilterUpdatePageVars()' - Update the page variables for the orientation.
 */

void
ppdFilterUpdatePageVars(int Orientation,
		     float *PageLeft, float *PageRight,
		     float *PageTop, float *PageBottom,
		     float *PageWidth, float *PageLength)
{
  float		temp;			/* Swapping variable */


  switch (Orientation & 3)
  {
    case 0 : /* Portait */
        break;

    case 1 : /* Landscape */
	temp        = *PageLeft;
	*PageLeft   = *PageBottom;
	*PageBottom = temp;

	temp        = *PageRight;
	*PageRight  = *PageTop;
	*PageTop    = temp;

	temp        = *PageWidth;
	*PageWidth  = *PageLength;
	*PageLength = temp;
	break;

    case 2 : /* Reverse Portrait */
	temp        = *PageWidth - *PageLeft;
	*PageLeft   = *PageWidth - *PageRight;
	*PageRight  = temp;

	temp        = *PageLength - *PageBottom;
	*PageBottom = *PageLength - *PageTop;
	*PageTop    = temp;
        break;

    case 3 : /* Reverse Landscape */
	temp        = *PageWidth - *PageLeft;
	*PageLeft   = *PageWidth - *PageRight;
	*PageRight  = temp;

	temp        = *PageLength - *PageBottom;
	*PageBottom = *PageLength - *PageTop;
	*PageTop    = temp;

	temp        = *PageLeft;
	*PageLeft   = *PageBottom;
	*PageBottom = temp;

	temp        = *PageRight;
	*PageRight  = *PageTop;
	*PageTop    = temp;

	temp        = *PageWidth;
	*PageWidth  = *PageLength;
	*PageLength = temp;
	break;
  }
}

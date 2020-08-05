/*
 * Filter functions support for cups-filters.
 *
 * Copyright © 2020 by Till Kamppeter.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "filter.h"
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <cups/file.h>
#include <cups/array.h>
#include <ppd/ppd.h>


/*
 * 'cups_logfunc()' - Output log messages on stderr, compatible to CUPS,
 *                    meaning that the debug level is represented by a
 *                    prefix like "DEBUG: ", "INFO: ", ...
 */

void
cups_logfunc(void *data,
	     filter_loglevel_t level,
	     const char *message,
	     ...)
{
  va_list arglist;

  (void)data; /* No extra data needed */

  switch(level)
  {
    case FILTER_LOGLEVEL_UNSPEC:
    case FILTER_LOGLEVEL_DEBUG:
    default:
      fprintf(stderr, "DEBUG: ");
      break;
    case FILTER_LOGLEVEL_INFO:
      fprintf(stderr, "INFO: ");
      break;
    case FILTER_LOGLEVEL_WARN:
      fprintf(stderr, "WARN: ");
      break;
    case FILTER_LOGLEVEL_ERROR:
    case FILTER_LOGLEVEL_FATAL:
      fprintf(stderr, "ERROR: ");
      break;
    case FILTER_LOGLEVEL_CONTROL:
      break;
  }      
  va_start(arglist, message);
  vfprintf(stderr, message, arglist);
  fflush(stderr);
  va_end(arglist);
}


/*
 * 'filterCUPSWrapper()' - Wrapper function to use a filter function as
 *                         classic CUPS filter
 */

int					/* O - Exit status */
filterCUPSWrapper(
     int  argc,				/* I - Number of command-line args */
     char *argv[],			/* I - Command-line arguments */
     filter_function_t filter,          /* I - Filter function */
     void *parameters,                  /* I - Filter function parameters */
     int *JobCanceled)                  /* I - Var set to 1 when job canceled */
{
  int	        inputfd;		/* Print file descriptor*/
  int           inputseekable;          /* Is the input seekable (actual file
					   not stdin)? */
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  filter_data_t filter_data;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


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
    fprintf(stderr, "Usage: %s job-id user title copies options [file]",
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

 /*
  * Create data record to call filter function
  */

  filter_data.job_id = atoi(argv[1]);
  filter_data.job_user = argv[2];
  filter_data.job_title = argv[3];
  filter_data.copies = atoi(argv[4]);
  filter_data.job_attrs = NULL;      /* We use command line options */
  filter_data.printer_attrs = NULL;  /* We use the queue's PPD file */
  filter_data.num_options = num_options;
  filter_data.options = options; /* Command line options from 5th arg */
  filter_data.ppdfile = NULL;
  filter_data.ppd = NULL;  /* Filter function will load PPD according
				     to "PPD" environment variable. */
  filter_data.logfunc = cups_logfunc; /* Logging scheme of CUPS */
  filter_data.logdata = NULL;

 /*
  * Fire up the filter function (output to stdout, file descriptor 1)
  */

  return filter(inputfd, 1, inputseekable, JobCanceled, &filter_data, NULL);
}


/*
 * 'filterSetCommonOptions()' - Set common filter options for media size, etc.
 *                              based on PPD file
 */

void
filterSetCommonOptions(
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
    filter_logfunc_t log,               /* I - Logging function,
					       NULL for no logging */
    void *ld)                           /* I - User data for logging function,
					       can be NULL */
{
  ppd_size_t	*pagesize;		/* Current page size */
  const char	*val;			/* Option value */


  *Orientation = 0;		/* 0 = portrait, 1 = landscape, etc. */
  *Duplex = 0;			/* Duplexed? */
  *LanguageLevel = 1;		/* Language level of printer */
  *ColorDevice = 1;		/* Do color text? */
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
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page width: %.0f\n",
		   pagesize->width);
      corrected = 1;
    }
    if (pagesize->length > 0) 
      *PageLength = pagesize->length;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page length: %.0f\n",
		   pagesize->length);
      corrected = 1;
    }
    if (pagesize->top >= 0 && pagesize->top <= *PageLength) 
      *PageTop = pagesize->top;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page top margin: %.0f\n",
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
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page bottom margin: %.0f\n",
		   pagesize->bottom);
      if (*PageLength <= *PageBottom)
	*PageBottom = 0.0f;
      corrected = 1;
    }
    if (*PageBottom == *PageTop)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Bottom: %.0f; Top: %.0f\n",
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
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Bottom: %.0f; Top: %.0f\n",
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
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page left margin: %.0f\n",
		   pagesize->left);
      if (*PageWidth <= *PageLeft)
	*PageLeft = 0.0f;
      corrected = 1;
    }
    if (pagesize->right >= 0 && pagesize->right <= *PageWidth) 
      *PageRight = pagesize->right;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page right margin: %.0f\n",
		   pagesize->right);
      if (*PageWidth >= *PageLeft)
	*PageRight = *PageWidth - *PageLeft;
      else
	*PageRight = *PageWidth;
      corrected = 1;
    }
    if (*PageLeft == *PageRight)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f\n",
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
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f\n",
		   *PageLeft, *PageRight);
      float swap = *PageLeft;
      *PageLeft = *PageRight;
      *PageRight = swap;
      corrected = 1;
    }

    if (corrected)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "PPD Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f\n",
		   pagesize->width, pagesize->length, pagesize->left,
		   pagesize->bottom, pagesize->right, pagesize->top);
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Corrected Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f\n",
		   *PageWidth, *PageLength, *PageLeft,
		   *PageBottom, *PageRight, *PageTop);
    }
    else
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f\n",
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
  else if ((val = cupsGetOption("orientation-requested", num_options, options)) != NULL)
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
    filterUpdatePageVars(*Orientation, PageLeft, PageRight,
			 PageTop, PageBottom, PageWidth, PageLength);

  if (ppdIsMarked(ppd, "Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "Duplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexTumble"))
    *Duplex = 1;

  return;
}


/*
 * 'filterUpdatePageVars()' - Update the page variables for the orientation.
 */

void
filterUpdatePageVars(int Orientation,
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


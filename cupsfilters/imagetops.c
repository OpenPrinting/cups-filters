/*
 *   Image file to PostScript filter function for cups-filters.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   imagetops()        - imagetops filter function
 *   WriteCommon()      - Write common procedures...
 *   WriteLabelProlog() - Write the prolog with the classification
 *                        and page label.
 *   WriteLabels()      - Write the actual page labels.
 *   WriteTextComment() - Write a DSC comment.
 *   ps_hex()           - Print binary data as a series of hexadecimal numbers.
 *   ps_ascii85()       - Print binary data as a series of base-85 numbers.
 */

/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
#include <cupsfilters/image.h>
#include <cupsfilters/raster.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <errno.h>


/*
 * Types...
 */

typedef struct {                	/**** Document information ****/
  int   	Orientation,    	/* 0 = portrait, 1 = landscape, etc. */
        	Duplex,         	/* Duplexed? */
        	LanguageLevel,  	/* Language level of printer */
        	Color;    	 	/* Print in color? */
  float 	PageLeft,       	/* Left margin */
        	PageRight,      	/* Right margin */
        	PageBottom,     	/* Bottom margin */
        	PageTop,        	/* Top margin */
        	PageWidth,      	/* Total page width */
        	PageLength;     	/* Total page length */
  FILE		*outputfp;
} imagetops_doc_t;


/*
 * Local functions...
 */

static void	WriteCommon(imagetops_doc_t *doc);
static void	WriteLabelProlog(imagetops_doc_t *doc,
				 const char *label, float bottom,
		                 float top, float width);
static void	WriteLabels(imagetops_doc_t *doc, int orient);
static void	WriteTextComment(imagetops_doc_t *doc,
				 const char *name, const char *value);
static void	ps_hex(FILE *outputfp, cups_ib_t *, int, int);
static void	ps_ascii85(FILE *outputfp, cups_ib_t *, int, int);


/*
 * 'imagetops()' - Filter function to convert many common image file
 *                 formats into PostScript
 */

int                             /* O - Error status */
imagetops(int inputfd,         /* I - File descriptor input stream */
	  int outputfd,        /* I - File descriptor output stream */
	  int inputseekable,   /* I - Is input stream seekable? (unused) */
	  filter_data_t *data, /* I - Job and printer data */
	  void *parameters)    /* I - Filter-specific parameters (unused) */
{
  imagetops_doc_t	doc;		/* Document information */
  cups_image_t	*img;			/* Image to print */
  float		xprint,			/* Printable area */
		yprint,
		xinches,		/* Total size in inches */
		yinches;
  float		xsize,			/* Total size in points */
		ysize,
		xsize2,
		ysize2;
  float		aspect;			/* Aspect ratio */
  int		xpages,			/* # x pages */
		ypages,			/* # y pages */
		xpage,			/* Current x page */
		ypage,			/* Current y page */
		page;			/* Current page number */
  int		xc0, yc0,			/* Corners of the page in image coords */
		xc1, yc1;
  cups_ib_t	*row;			/* Current row */
  int		y;			/* Current Y coordinate in image */
  int		colorspace;		/* Output colorspace */
  int		out_offset,		/* Offset into output buffer */
		out_length;		/* Length of output buffer */
  ppd_file_t	*ppd;			/* PPD file */
  ppd_choice_t	*choice;		/* PPD option choice */
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  const char	*val;			/* Option value */
  int		slowcollate;		/* Collate copies the slow way */
  float		g;			/* Gamma correction value */
  float		b;			/* Brightness factor */
  float		zoom;			/* Zoom facter */
  int		xppi, yppi;		/* Pixels-per-inch */
  int		hue, sat;		/* Hue and saturation adjustment */
  int		realcopies,		/* Real copies being printed */
		emit_jcl;		/* Emit JCL? */
  float		left, top;		/* Left and top of image */
  time_t	curtime;		/* Current time */
  struct tm	*curtm;			/* Current date */
  char		curdate[255];		/* Current date string */
  int		fillprint = 0;		/* print-scaling = fill */
  int		cropfit = 0;		/* -o crop-to-fit = true */
  cups_page_header2_t h;                /* CUPS Raster page header, to */
                                        /* accommodate results of command */
                                        /* line parsing for PPD-less queue */
  int		Flip,			/* Flip/mirror pages */
		XPosition,		/* Horizontal position on page */
		YPosition,		/* Vertical position on page */
		Collate,		/* Collate copies? */
		Copies;			/* Number of copies */
  char		tempfile[1024];		/* Name of file to print */
  FILE          *inputfp;		/* Input file */
  int           fd;			/* File descriptor for temp file */
  char          buf[BUFSIZ];
  int           bytes;
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Initialize data structure
  */

  Flip = 0;
  XPosition = 0;
  YPosition = 0;
  Collate = 0;
  Copies = 1;

 /*
  * Open the input data stream specified by the inputfd ...
  */

  if ((inputfp = fdopen(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "imagetops: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Copy input into temporary file if needed ...
  */

  if (!inputseekable) {
    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "imagetops: Unable to copy input: %s",
		   strerror(errno));
      return (1);
    }

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Copying input to temp file \"%s\"",
		 tempfile);

    while ((bytes = fread(buf, 1, sizeof(buf), inputfp)) > 0)
      bytes = write(fd, buf, bytes);

    fclose(inputfp);
    close(fd);

   /*
    * Open the temporary file to read it instead of the original input ...
    */

    if ((inputfp = fopen(tempfile, "r")) == NULL)
    {
      if (!iscanceled || !iscanceled(icd))
      {
	if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		     "imagetops: Unable to open temporary file.");
      }

      unlink(tempfile);
      return (1);
    }
  }

 /*
  * Open the output data stream specified by the outputfd...
  */

  if ((doc.outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "imagetops: Unable to open output data stream.");
    }

    fclose(inputfp);

    if (!inputseekable)
      unlink(tempfile);
    return (1);
  }

 /*
  * Process command-line options and write the prolog...
  */

  zoom = 1.0;
  xppi = 0;
  yppi = 0;
  hue  = 0;
  sat  = 100;
  g    = 1.0;
  b    = 1.0;

  Copies = data->copies;

 /*
  * Option list...
  */

  options     = data->options;
  num_options = data->num_options;

 /*
  * Process job options...
  */

  ppd = data->ppd;
  filterSetCommonOptions(ppd, num_options, options, 0,
			 &doc.Orientation, &doc.Duplex,
			 &doc.LanguageLevel, &doc.Color,
			 &doc.PageLeft, &doc.PageRight,
			 &doc.PageTop, &doc.PageBottom,
			 &doc.PageWidth, &doc.PageLength,
			 log, ld);

  /* The filterSetCommonOptions() does not set doc.Color
     according to option settings (user's demand for color/gray),
     so we parse the options and set the mode here */
  cupsRasterParseIPPOptions(&h, num_options, options, 0, 1);
  if (doc.Color)
    doc.Color = h.cupsNumColors <= 1 ? 0 : 1;
  if (!ppd) {
    /* Without PPD use also the other findings of cupsRasterParseIPPOptions() */
    doc.Orientation = h.Orientation;
    doc.Duplex = h.Duplex;
    doc.LanguageLevel = 2;
    doc.PageWidth = h.cupsPageSize[0] != 0.0 ? h.cupsPageSize[0] :
      (float)h.PageSize[0];
    doc.PageLength = h.cupsPageSize[1] != 0.0 ? h.cupsPageSize[1] :
      (float)h.PageSize[1];
    doc.PageLeft = h.cupsImagingBBox[0] != 0.0 ? h.cupsImagingBBox[0] :
      (float)h.ImagingBoundingBox[0];
    doc.PageBottom = h.cupsImagingBBox[1] != 0.0 ? h.cupsImagingBBox[1] :
      (float)h.ImagingBoundingBox[1];
    doc.PageRight = h.cupsImagingBBox[2] != 0.0 ? h.cupsImagingBBox[2] :
      (float)h.ImagingBoundingBox[2];
    doc.PageTop = h.cupsImagingBBox[3] != 0.0 ? h.cupsImagingBBox[3] :
      (float)h.ImagingBoundingBox[3];
    Flip = h.MirrorPrint ? 1 : 0;
    Collate = h.Collate ? 1 : 0;
    Copies = h.NumCopies;
  }

  if ((val = cupsGetOption("multiple-document-handling", num_options, options)) != NULL)
  {
   /*
    * This IPP attribute is unnecessarily complicated...
    *
    *   single-document, separate-documents-collated-copies, and
    *   single-document-new-sheet all require collated copies.
    *
    *   separate-documents-uncollated-copies allows for uncollated copies.
    */

    Collate = strcasecmp(val, "separate-documents-uncollated-copies") != 0;
  }

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL &&
      strcasecmp(val, "True") == 0)
    Collate = 1;

  if ((val = cupsGetOption("gamma", num_options, options)) != NULL)
  {
   /*
    * Get gamma value from 1 to 10000...
    */

    g = atoi(val) * 0.001f;

    if (g < 0.001f)
      g = 0.001f;
    else if (g > 10.0f)
      g = 10.0f;
  }

  if ((val = cupsGetOption("brightness", num_options, options)) != NULL)
  {
   /*
    * Get brightness value from 10 to 1000.
    */

    b = atoi(val) * 0.01f;

    if (b < 0.1f)
      b = 0.1f;
    else if (b > 10.0f)
      b = 10.0f;
  }

  if ((val = cupsGetOption("ppi", num_options, options)) != NULL)
  {
    sscanf(val, "%d", &xppi);
    yppi = xppi;
    zoom = 0.0;
  }

  if ((val = cupsGetOption("position", num_options, options)) != NULL)
  {
    if (strcasecmp(val, "center") == 0)
    {
      XPosition = 0;
      YPosition = 0;
    }
    else if (strcasecmp(val, "top") == 0)
    {
      XPosition = 0;
      YPosition = 1;
    }
    else if (strcasecmp(val, "left") == 0)
    {
      XPosition = -1;
      YPosition = 0;
    }
    else if (strcasecmp(val, "right") == 0)
    {
      XPosition = 1;
      YPosition = 0;
    }
    else if (strcasecmp(val, "top-left") == 0)
    {
      XPosition = -1;
      YPosition = 1;
    }
    else if (strcasecmp(val, "top-right") == 0)
    {
      XPosition = 1;
      YPosition = 1;
    }
    else if (strcasecmp(val, "bottom") == 0)
    {
      XPosition = 0;
      YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-left") == 0)
    {
      XPosition = -1;
      YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-right") == 0)
    {
      XPosition = 1;
      YPosition = -1;
    }
  }

  if ((val = cupsGetOption("saturation", num_options, options)) != NULL)
    sat = atoi(val);

  if ((val = cupsGetOption("hue", num_options, options)) != NULL)
    hue = atoi(val);

  if ((choice = ppdFindMarkedChoice(ppd, "MirrorPrint")) != NULL)
  {
    val = choice->choice;
    choice->marked = 0;
  }
  else
    val = cupsGetOption("mirror", num_options, options);

  if (val && (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
              !strcasecmp(val, "yes")))
    Flip = 1;

  if ((val = cupsGetOption("emit-jcl", num_options, options)) != NULL &&
      (!strcasecmp(val, "false") || !strcasecmp(val, "off") ||
       !strcasecmp(val, "no") || !strcmp(val, "0")))
    emit_jcl = 0;
  else
    emit_jcl = 1;

 /*
  * Open the input image to print...
  */

  colorspace = doc.Color ? CUPS_IMAGE_RGB_CMYK : CUPS_IMAGE_WHITE;

  img = cupsImageOpenFP(inputfp, colorspace, CUPS_IMAGE_WHITE, sat, hue, NULL);
  if (img != NULL) {

    int margin_defined = 0;
    int fidelity = 0;
    int document_large = 0;

    if (ppd && (ppd->custom_margins[0] || ppd->custom_margins[1] ||
		ppd->custom_margins[2] || ppd->custom_margins[3]))
      /* In case of custom margins */
      margin_defined = 1;
    if (doc.PageLength != doc.PageTop - doc.PageBottom ||
	doc.PageWidth != doc.PageRight - doc.PageLeft)
      margin_defined = 1;

    if((val = cupsGetOption("ipp-attribute-fidelity", num_options, options))
       != NULL)
    {
      if(!strcasecmp(val, "true") || !strcasecmp(val, "yes") ||
	 !strcasecmp(val, "on")) {
	fidelity = 1;
      }
    }

    float w = (float)cupsImageGetWidth(img);
    float h = (float)cupsImageGetHeight(img);
    float pw = doc.PageRight - doc.PageLeft;
    float ph = doc.PageTop - doc.PageBottom;
    int tempOrientation = doc.Orientation;
    if ((val = cupsGetOption("orientation-requested", num_options, options)) !=
	NULL)
      tempOrientation = atoi(val);
    else if ((val = cupsGetOption("landscape", num_options, options)) != NULL)
    {
      if(!strcasecmp(val, "true") || !strcasecmp(val, "yes"))
	tempOrientation = 4;
    }
    if (tempOrientation == 0) {
      if (((pw > ph) && (w < h)) || ((pw < ph) && (w > h)))
	tempOrientation = 4;
    }
    if (tempOrientation == 4 || tempOrientation == 5)
    {
      int tmp = pw;
      pw = ph;
      ph = tmp;
    }
    if (w > pw || h > ph)
      document_large = 1;

    if ((val = cupsGetOption("print-scaling", num_options, options)) != NULL)
    {
      if (!strcasecmp(val, "auto"))
      {
	if (fidelity || document_large) {
	  if (margin_defined)
	    zoom = 1.0;       // fit method
	  else
	    fillprint = 1;    // fill method
	}
	else
	  cropfit = 1;        // none method
      }
      else if (!strcasecmp(val, "auto-fit"))
      {
	if (fidelity || document_large)
	  zoom = 1.0;         // fit method
	else
	  cropfit = 1;        // none method
      }
      else if (!strcasecmp(val, "fill"))
	fillprint = 1;        // fill method
      else if (!strcasecmp(val, "fit"))
	zoom = 1.0;           // fitplot = 1 or fit method
      else
	cropfit = 1;            // none or crop-to-fit
    }
    else
    {       // print-scaling is not defined, look for alternate options.
      if ((val = cupsGetOption("scaling", num_options, options)) != NULL)
	zoom = atoi(val) * 0.01;
      else if (((val =
		 cupsGetOption("fit-to-page", num_options, options)) != NULL) ||
	       ((val = cupsGetOption("fitplot", num_options, options)) != NULL))
      {
	if (!strcasecmp(val, "yes") || !strcasecmp(val, "on") ||
	    !strcasecmp(val, "true"))
	  zoom = 1.0;
	else
	  zoom = 0.0;
      }
      else if ((val = cupsGetOption("natural-scaling", num_options, options)) !=
	       NULL)
	zoom = 0.0;

      if ((val = cupsGetOption("fill", num_options,options)) != NULL)
      {
	if (!strcasecmp(val, "true") || !strcasecmp(val, "yes"))
	  fillprint = 1;
      }

      if ((val = cupsGetOption("crop-to-fit", num_options,options)) != NULL)
      {
	if (!strcasecmp(val, "true") || !strcasecmp(val, "yes"))
	  cropfit=1;
      }
    }
    if (fillprint || cropfit)
    {
      tempOrientation = doc.Orientation;
      int flag = 3;
      if ((val = cupsGetOption("orientation-requested", num_options,
			       options)) != NULL)
	tempOrientation = atoi(val);
      else if ((val = cupsGetOption("landscape", num_options, options)) != NULL)
      {
	if (!strcasecmp(val,"true") || !strcasecmp(val,"yes"))
	  tempOrientation = 4;
      }
      if (tempOrientation > 0)
      {
	if (tempOrientation == 4 || tempOrientation == 5)
	{
	  float temp = pw;
	  pw = ph;
	  ph = temp;
	  flag = 4;
	}
      }
      if (tempOrientation==0)
      { 
	if (((pw > ph) && (w < h)) || ((pw < ph) && (w > h)))
	{
	  int temp = pw;
	  pw = ph;
	  ph = temp;
	  flag = 4;
	}
      }
      if (fillprint) {
	float final_w, final_h;
	if (w * ph / pw <= h) {
	  final_w = w;
	  final_h = w * ph / pw; 
	}
	else
	{
	  final_w = h * pw / ph;
	  final_h = h;
	}
	float posw = (w - final_w) / 2, posh = (h - final_h) / 2;
	posw = (1 + XPosition) * posw;
	posh = (1 - YPosition) * posh;
	cups_image_t *img2 = cupsImageCrop(img, posw, posh, final_w, final_h);
	cupsImageClose(img);
	img = img2;
      }
      else
      {
	float final_w = w, final_h = h;
	if(final_w > pw)
	  final_w = pw;
	if(final_h > ph)
	  final_h = ph;
	if ((fabs(final_w - w) > 0.5 * w) || (fabs(final_h - h) > 0.5 * h))
	{
	  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		       "imagetops: Ignoring crop-to-fit option!");
	  cropfit = 0;
	}
	else
	{
	  float posw = (w - final_w) / 2, posh = (h - final_h) / 2;
	  posw = (1 + XPosition) * posw;
	  posh = (1 - YPosition) * posh;
	  cups_image_t *img2 = cupsImageCrop(img, posw, posh, final_w, final_h);
	  cupsImageClose(img);
	  img = img2;
	  if (flag == 4)
	  {
	    doc.PageBottom += (doc.PageTop - doc.PageBottom - final_w) / 2;
	    doc.PageTop = doc.PageBottom + final_w;
	    doc.PageLeft += (doc.PageRight - doc.PageLeft - final_h) / 2;
	    doc.PageRight = doc.PageLeft + final_h;
	  }
	  else
	  {
	    doc.PageBottom += (doc.PageTop - doc.PageBottom - final_h) / 2;
	    doc.PageTop = doc.PageBottom + final_h;
	    doc.PageLeft += (doc.PageRight - doc.PageLeft - final_w) / 2;
	    doc.PageRight = doc.PageLeft + final_w;
	  }
	  if (doc.PageBottom < 0) doc.PageBottom = 0;
	  if (doc.PageLeft < 0) doc.PageLeft = 0;
	}
      }
    }
  }

  if (!inputseekable)
    unlink(tempfile);

  if (img == NULL)
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "imagetops: The print file could not be opened - %s",
		 strerror(errno));
    ppdClose(ppd);
    return (1);
  }

  colorspace = cupsImageGetColorSpace(img);

 /*
  * Scale as necessary...
  */

  if (zoom == 0.0 && xppi == 0)
  {
    xppi = cupsImageGetXPPI(img);
    yppi = cupsImageGetYPPI(img);
  }

  if (yppi == 0)
    yppi = xppi;

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "imagetops: Before scaling: xppi=%d, yppi=%d, zoom=%.2f",
	       xppi, yppi, zoom);

  if (xppi > 0)
  {
   /*
    * Scale the image as neccesary to match the desired pixels-per-inch.
    */

    if (doc.Orientation & 1)
    {
      xprint = (doc.PageTop - doc.PageBottom) / 72.0;
      yprint = (doc.PageRight - doc.PageLeft) / 72.0;
    }
    else
    {
      xprint = (doc.PageRight - doc.PageLeft) / 72.0;
      yprint = (doc.PageTop - doc.PageBottom) / 72.0;
    }

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Before scaling: xprint=%.1f, yprint=%.1f",
		 xprint, yprint);

    xinches = (float)cupsImageGetWidth(img) / (float)xppi;
    yinches = (float)cupsImageGetHeight(img) / (float)yppi;

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Image size is %.1f x %.1f inches...",
		 xinches, yinches);

    if ((val = cupsGetOption("natural-scaling", num_options, options)) != NULL)
    {
      xinches = xinches * atoi(val) / 100;
      yinches = yinches * atoi(val) / 100;
    }

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Rotate the image if it will fit landscape but not portrait...
      */

      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "imagetops: Auto orientation...");

      if ((xinches > xprint || yinches > yprint) &&
          xinches <= yprint && yinches <= xprint)
      {
       /*
	* Rotate the image as needed...
	*/

	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "imagetops: Using landscape orientation...");

	doc.Orientation = (doc.Orientation + 1) & 3;
	xsize       = yprint;
	yprint      = xprint;
	xprint      = xsize;
      }
    }
  }
  else
  {
   /*
    * Scale percentage of page size...
    */

    xprint = (doc.PageRight - doc.PageLeft) / 72.0;
    yprint = (doc.PageTop - doc.PageBottom) / 72.0;
    aspect = (float)cupsImageGetYPPI(img) / (float)cupsImageGetXPPI(img);

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Before scaling: xprint=%.1f, yprint=%.1f",
		 xprint, yprint);

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: cupsImageGetXPPI(img) = %d, "
		 "cupsImageGetYPPI(img) = %d, aspect = %f",
		 cupsImageGetXPPI(img), cupsImageGetYPPI(img), aspect);

    xsize = xprint * zoom;
    ysize = xsize * cupsImageGetHeight(img) / cupsImageGetWidth(img) / aspect;

    if (ysize > (yprint * zoom))
    {
      ysize = yprint * zoom;
      xsize = ysize * cupsImageGetWidth(img) * aspect /
	cupsImageGetHeight(img);
    }

    xsize2 = yprint * zoom;
    ysize2 = xsize2 * cupsImageGetHeight(img) / cupsImageGetWidth(img) /
      aspect;

    if (ysize2 > (xprint * zoom))
    {
      ysize2 = xprint * zoom;
      xsize2 = ysize2 * cupsImageGetWidth(img) * aspect /
	cupsImageGetHeight(img);
    }

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Portrait size is %.2f x %.2f inches",
		 xsize, ysize);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Landscape size is %.2f x %.2f inches",
		 xsize2, ysize2);

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Choose the rotation with the largest area, but prefer
      * portrait if they are equal...
      */

      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "imagetops: Auto orientation...");

      if ((xsize * ysize) < (xsize2 * xsize2))
      {
       /*
	* Do landscape orientation...
	*/

	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "imagetops: Using landscape orientation...");

	doc.Orientation = 1;
	xinches     = xsize2;
	yinches     = ysize2;
	xprint      = (doc.PageTop - doc.PageBottom) / 72.0;
	yprint      = (doc.PageRight - doc.PageLeft) / 72.0;
      }
      else
      {
       /*
	* Do portrait orientation...
	*/

	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "imagetops: Using portrait orientation...");

	doc.Orientation = 0;
	xinches     = xsize;
	yinches     = ysize;
      }
    }
    else if (doc.Orientation & 1)
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "imagetops: Using landscape orientation...");

      xinches     = xsize2;
      yinches     = ysize2;
      xprint      = (doc.PageTop - doc.PageBottom) / 72.0;
      yprint      = (doc.PageRight - doc.PageLeft) / 72.0;
    }
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "imagetops: Using portrait orientation...");

      xinches     = xsize;
      yinches     = ysize;
      xprint      = (doc.PageRight - doc.PageLeft) / 72.0;
      yprint      = (doc.PageTop - doc.PageBottom) / 72.0;
    }
  }

 /*
  * Compute the number of pages to print and the size of the image on each
  * page...
  */

  xpages = ceil(xinches / xprint);
  ypages = ceil(yinches / yprint);

  xprint = xinches / xpages;
  yprint = yinches / ypages;

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "imagetops: xpages = %dx%.2fin, ypages = %dx%.2fin",
	       xpages, xprint, ypages, yprint);

 /*
  * Update the page size for custom sizes...
  */

  if ((choice = ppdFindMarkedChoice(ppd, "PageSize")) != NULL &&
      strcasecmp(choice->choice, "Custom") == 0)
  {
    float	width,		/* New width in points */
		length;		/* New length in points */
    char	s[255];		/* New custom page size... */


   /*
    * Use the correct width and length for the current orientation...
    */

    if (doc.Orientation & 1)
    {
      width  = yprint * 72.0;
      length = xprint * 72.0;
    }
    else
    {
      width  = xprint * 72.0;
      length = yprint * 72.0;
    }

   /*
    * Add margins to page size...
    */

    width  += ppd->custom_margins[0] + ppd->custom_margins[2];
    length += ppd->custom_margins[1] + ppd->custom_margins[3];

   /*
    * Enforce minimums...
    */

    if (width < ppd->custom_min[0])
      width = ppd->custom_min[0];

    if (length < ppd->custom_min[1])
      length = ppd->custom_min[1];

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "imagetops: Updated custom page size to %.2f x %.2f inches...",
		 width / 72.0, length / 72.0);

   /*
    * Set the new custom size...
    */

    sprintf(s, "Custom.%.0fx%.0f", width, length);
    ppdMarkOption(ppd, "PageSize", s);

   /*
    * Update page variables...
    */

    doc.PageWidth  = width;
    doc.PageLength = length;
    doc.PageLeft   = ppd->custom_margins[0];
    doc.PageRight  = width - ppd->custom_margins[2];
    doc.PageBottom = ppd->custom_margins[1];
    doc.PageTop    = length - ppd->custom_margins[3];
  }

 /*
  * See if we need to collate, and if so how we need to do it...
  */

  if (xpages == 1 && ypages == 1)
    Collate = 0;

  slowcollate = Collate && ppdFindOption(ppd, "Collate") == NULL;

  if (Copies > 1 && !slowcollate)
  {
    realcopies = Copies;
    Copies     = 1;
  }
  else
    realcopies = 1;

 /*
  * Write any "exit server" options that have been selected...
  */

  ppdEmit(ppd, doc.outputfp, PPD_ORDER_EXIT);

 /*
  * Write any JCL commands that are needed to print PostScript code...
  */

  if (emit_jcl)
    ppdEmitJCL(ppd, doc.outputfp, data->job_id, data->job_user,
	       data->job_title);

 /*
  * Start sending the document with any commands needed...
  */

  curtime = time(NULL);
  curtm   = localtime(&curtime);

  fputs("%!PS-Adobe-3.0\n", doc.outputfp);
  fprintf(doc.outputfp, "%%%%BoundingBox: %.0f %.0f %.0f %.0f\n",
	  doc.PageLeft, doc.PageBottom, doc.PageRight, doc.PageTop);
  fprintf(doc.outputfp, "%%%%LanguageLevel: %d\n", doc.LanguageLevel);
  fprintf(doc.outputfp, "%%%%Pages: %d\n", xpages * ypages * Copies);
  fputs("%%DocumentData: Clean7Bit\n", doc.outputfp);
  fputs("%%DocumentNeededResources: font Helvetica-Bold\n", doc.outputfp);
  fputs("%%Creator: imagetops\n", doc.outputfp);
  strftime(curdate, sizeof(curdate), "%c", curtm);
  fprintf(doc.outputfp, "%%%%CreationDate: %s\n", curdate);
  WriteTextComment(&doc, "Title", data->job_title);
  WriteTextComment(&doc, "For", data->job_user);
  if (doc.Orientation & 1)
    fputs("%%Orientation: Landscape\n", doc.outputfp);
  else
    fputs("%%Orientation: Portrait\n", doc.outputfp);
  fputs("%%EndComments\n", doc.outputfp);
  fputs("%%BeginProlog\n", doc.outputfp);

  if (ppd != NULL && ppd->patches != NULL)
  {
    fputs(ppd->patches, doc.outputfp);
    fputc('\n', doc.outputfp);
  }

  ppdEmit(ppd, doc.outputfp, PPD_ORDER_DOCUMENT);
  ppdEmit(ppd, doc.outputfp, PPD_ORDER_ANY);
  ppdEmit(ppd, doc.outputfp, PPD_ORDER_PROLOG);

  if (g != 1.0 || b != 1.0)
    fprintf(doc.outputfp,
	    "{ neg 1 add dup 0 lt { pop 1 } { %.3f exp neg 1 add } "
	    "ifelse %.3f mul } bind settransfer\n", g, b);

  WriteCommon(&doc);
  switch (doc.Orientation)
  {
    case 0 :
        WriteLabelProlog(&doc, cupsGetOption("page-label", num_options,
					     options),
			 doc.PageBottom, doc.PageTop, doc.PageWidth);
        break;

    case 1 :
        WriteLabelProlog(&doc, cupsGetOption("page-label", num_options,
					     options),
                	 doc.PageLeft, doc.PageRight, doc.PageLength);
        break;

    case 2 :
        WriteLabelProlog(&doc, cupsGetOption("page-label", num_options,
					     options),
			 doc.PageLength - doc.PageTop,
			 doc.PageLength - doc.PageBottom,
			 doc.PageWidth);
        break;

    case 3 :
	WriteLabelProlog(&doc, cupsGetOption("page-label", num_options,
					     options),
			 doc.PageWidth - doc.PageRight,
			 doc.PageWidth - doc.PageLeft,
			 doc.PageLength);
        break;
  }

  if (realcopies > 1)
  {
    if (ppd == NULL || ppd->language_level == 1)
      fprintf(doc.outputfp, "/#copies %d def\n", realcopies);
    else
      fprintf(doc.outputfp, "<</NumCopies %d>>setpagedevice\n", realcopies);
  }

  fputs("%%EndProlog\n", doc.outputfp);

 /*
  * Output the pages...
  */

  row = malloc(cupsImageGetWidth(img) * abs(colorspace) + 3);
  if (row == NULL)
  {
    log(ld, FILTER_LOGLEVEL_ERROR,
	"imagetops: Could not allocate memory.");
    cupsImageClose(img);
    ppdClose(ppd);
    return (2);
  }

  if (log)
  {
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"imagetops: XPosition=%d, YPosition=%d, Orientation=%d",
	XPosition, YPosition, doc.Orientation);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"imagetops: xprint=%.1f, yprint=%.1f", xprint, yprint);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"imagetops: PageLeft=%.0f, PageRight=%.0f, PageWidth=%.0f",
	doc.PageLeft, doc.PageRight, doc.PageWidth);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"imagetops: PageBottom=%.0f, PageTop=%.0f, PageLength=%.0f",
	doc.PageBottom, doc.PageTop, doc.PageLength);
  }

  switch (doc.Orientation)
  {
    default :
	switch (XPosition)
	{
	  case -1 :
              left = doc.PageLeft;
	      break;
	  default :
              left = (doc.PageRight + doc.PageLeft - xprint * 72) / 2;
	      break;
	  case 1 :
              left = doc.PageRight - xprint * 72;
	      break;
	}

	switch (YPosition)
	{
	  case -1 :
	      top = doc.PageBottom + yprint * 72;
	      break;
	  default :
	      top = (doc.PageTop + doc.PageBottom + yprint * 72) / 2;
	      break;
	  case 1 :
	      top = doc.PageTop;
	      break;
	}
	break;

    case 1 :
	switch (XPosition)
	{
	  case -1 :
              left = doc.PageBottom;
	      break;
	  default :
              left = (doc.PageTop + doc.PageBottom - xprint * 72) / 2;
	      break;
	  case 1 :
              left = doc.PageTop - xprint * 72;
	      break;
	}

	switch (YPosition)
	{
	  case -1 :
	      top = doc.PageLeft + yprint * 72;
	      break;
	  default :
	      top = (doc.PageRight + doc.PageLeft + yprint * 72) / 2;
	      break;
	  case 1 :
	      top = doc.PageRight;
	      break;
	}
	break;

    case 2 :
	switch (XPosition)
	{
	  case 1 :
              left = doc.PageLeft;
	      break;
	  default :
              left = (doc.PageRight + doc.PageLeft - xprint * 72) / 2;
	      break;
	  case -1 :
              left = doc.PageRight - xprint * 72;
	      break;
	}

	switch (YPosition)
	{
	  case 1 :
	      top = doc.PageBottom + yprint * 72;
	      break;
	  default :
	      top = (doc.PageTop + doc.PageBottom + yprint * 72) / 2;
	      break;
	  case -1 :
	      top = doc.PageTop;
	      break;
	}
	break;

    case 3 :
	switch (XPosition)
	{
	  case 1 :
              left = doc.PageBottom;
	      break;
	  default :
              left = (doc.PageTop + doc.PageBottom - xprint * 72) / 2;
	      break;
	  case -1 :
              left = doc.PageTop - xprint * 72;
	      break;
	}

	switch (YPosition)
	{
	  case 1 :
	      top = doc.PageLeft + yprint * 72;
	      break;
	  default :
	      top = (doc.PageRight + doc.PageLeft + yprint * 72) / 2;
	      break;
	  case -1 :
	      top = doc.PageRight;
	      break;
	}
	break;
  }

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "imagetops: left=%.2f, top=%.2f", left, top);

  for (page = 1; Copies > 0; Copies --)
    for (xpage = 0; xpage < xpages; xpage ++)
      for (ypage = 0; ypage < ypages; ypage ++, page ++)
      {
	if (iscanceled && iscanceled(icd))
	{
	  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		       "imagetops: Job canceled");
	  goto canceled;
	}

        if (log && ppd && ppd->num_filters == 0)
	  log(ld, FILTER_LOGLEVEL_CONTROL,
	      "PAGE: %d %d", page, realcopies);

	if (log) log(ld, FILTER_LOGLEVEL_INFO,
		     "imagetops: Printing page %d.", page);

        fprintf(doc.outputfp, "%%%%Page: %d %d\n", page, page);

        ppdEmit(ppd, doc.outputfp, PPD_ORDER_PAGE);

	fputs("gsave\n", doc.outputfp);

	if (Flip)
	  fprintf(doc.outputfp, "%.0f 0 translate -1 1 scale\n", doc.PageWidth);

	switch (doc.Orientation)
	{
	  case 1 : /* Landscape */
	      fprintf(doc.outputfp, "%.0f 0 translate 90 rotate\n",
		      doc.PageWidth);
              break;
	  case 2 : /* Reverse Portrait */
	      fprintf(doc.outputfp, "%.0f %.0f translate 180 rotate\n",
		      doc.PageWidth, doc.PageLength);
	      break;
	  case 3 : /* Reverse Landscape */
	      fprintf(doc.outputfp, "0 %.0f translate -90 rotate\n",
		      doc.PageLength);
              break;
	}

        fputs("gsave\n", doc.outputfp);

	xc0 = cupsImageGetWidth(img) * xpage / xpages;
	xc1 = cupsImageGetWidth(img) * (xpage + 1) / xpages - 1;
	yc0 = cupsImageGetHeight(img) * ypage / ypages;
	yc1 = cupsImageGetHeight(img) * (ypage + 1) / ypages - 1;

        fprintf(doc.outputfp, "%.1f %.1f translate\n", left, top);

	fprintf(doc.outputfp, "%.3f %.3f scale\n\n",
		xprint * 72.0 / (xc1 - xc0 + 1),
		yprint * 72.0 / (yc1 - yc0 + 1));

	if (doc.LanguageLevel == 1)
	{
	  fprintf(doc.outputfp, "/picture %d string def\n",
		  (xc1 - xc0 + 1) * abs(colorspace));
	  fprintf(doc.outputfp, "%d %d 8[1 0 0 -1 0 1]",
		  (xc1 - xc0 + 1), (yc1 - yc0 + 1));

          if (colorspace == CUPS_IMAGE_WHITE)
            fputs("{currentfile picture readhexstring pop} image\n",
		  doc.outputfp);
          else
            fprintf(doc.outputfp,
		    "{currentfile picture readhexstring pop} false %d "
		    "colorimage\n",
		    abs(colorspace));

          for (y = yc0; y <= yc1; y ++)
          {
            cupsImageGetRow(img, xc0, y, xc1 - xc0 + 1, row);
            ps_hex(doc.outputfp, row, (xc1 - xc0 + 1) * abs(colorspace),
		   y == yc1);
          }
	}
	else
	{
          switch (colorspace)
	  {
	    case CUPS_IMAGE_WHITE :
	        fputs("/DeviceGray setcolorspace\n", doc.outputfp);
		break;
            case CUPS_IMAGE_RGB :
	        fputs("/DeviceRGB setcolorspace\n", doc.outputfp);
		break;
            case CUPS_IMAGE_CMYK :
	        fputs("/DeviceCMYK setcolorspace\n", doc.outputfp);
		break;
          }

          fprintf(doc.outputfp,
		  "<<"
		  "/ImageType 1"
		  "/Width %d"
		  "/Height %d"
		  "/BitsPerComponent 8",
		  xc1 - xc0 + 1, yc1 - yc0 + 1);

          switch (colorspace)
	  {
	    case CUPS_IMAGE_WHITE :
                fputs("/Decode[0 1]", doc.outputfp);
		break;
            case CUPS_IMAGE_RGB :
                fputs("/Decode[0 1 0 1 0 1]", doc.outputfp);
		break;
            case CUPS_IMAGE_CMYK :
                fputs("/Decode[0 1 0 1 0 1 0 1]", doc.outputfp);
		break;
          }

          fputs("\n/DataSource currentfile/ASCII85Decode filter", doc.outputfp);

          if (((xc1 - xc0 + 1) / xprint) < 100.0)
            fputs("/Interpolate true", doc.outputfp);

          fputs("/ImageMatrix[1 0 0 -1 0 1]>>image\n", doc.outputfp);

          for (y = yc0, out_offset = 0; y <= yc1; y ++)
          {
            cupsImageGetRow(img, xc0, y, xc1 - xc0 + 1, row + out_offset);

            out_length = (xc1 - xc0 + 1) * abs(colorspace) + out_offset;
            out_offset = out_length & 3;

            ps_ascii85(doc.outputfp, row, out_length, y == yc1);

            if (out_offset > 0)
              memcpy(row, row + out_length - out_offset, out_offset);
          }
	}

	fputs("grestore\n", doc.outputfp);
	WriteLabels(&doc, 0);
	fputs("grestore\n", doc.outputfp);
	fputs("showpage\n", doc.outputfp);
      }

 canceled:
  fputs("%%EOF\n", doc.outputfp);

  free(row);
  
 /*
  * End the job with the appropriate JCL command or CTRL-D otherwise.
  */

  if (emit_jcl)
  {
    if (ppd && ppd->jcl_end)
      ppdEmitJCLEnd(ppd, doc.outputfp);
    else
      fputc(0x04, doc.outputfp);
  }

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "imagetops: Printing completed.", page);

 /*
  * Close files...
  */

  cupsImageClose(img);
  ppdClose(ppd);
  fclose(doc.outputfp);
  close(outputfd);

  return (0);
}


/*
 * 'WriteCommon()' - Write common procedures...
 */

void
WriteCommon(imagetops_doc_t *doc)
{
  fputs("% x y w h ESPrc - Clip to a rectangle.\n"
        "userdict/ESPrc/rectclip where{pop/rectclip load}\n"
        "{{newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
        "neg 0 rlineto closepath clip newpath}bind}ifelse put\n",
	doc->outputfp);
  fputs("% x y w h ESPrf - Fill a rectangle.\n"
        "userdict/ESPrf/rectfill where{pop/rectfill load}\n"
        "{{gsave newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
        "neg 0 rlineto closepath fill grestore}bind}ifelse put\n",
	doc->outputfp);
  fputs("% x y w h ESPrs - Stroke a rectangle.\n"
        "userdict/ESPrs/rectstroke where{pop/rectstroke load}\n"
        "{{gsave newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
        "neg 0 rlineto closepath stroke grestore}bind}ifelse put\n",
	doc->outputfp);
}


/*
 * 'WriteLabelProlog()' - Write the prolog with the classification
 *                        and page label.
 */

void
WriteLabelProlog(imagetops_doc_t *doc,
		 const char *label,	/* I - Page label */
		 float      bottom,	/* I - Bottom position in points */
		 float      top,	/* I - Top position in points */
		 float      width)	/* I - Width in points */
{
  const char	*classification;	/* CLASSIFICATION environment variable*/
  const char	*ptr;			/* Temporary string pointer */


 /*
  * First get the current classification...
  */

  if ((classification = getenv("CLASSIFICATION")) == NULL)
    classification = "";
  if (strcmp(classification, "none") == 0)
    classification = "";

 /*
  * If there is nothing to show, bind an empty 'write labels' procedure
  * and return...
  */

  if (!classification[0] && (label == NULL || !label[0]))
  {
    fputs("userdict/ESPwl{}bind put\n", doc->outputfp);
    return;
  }

 /*
  * Set the classification + page label string...
  */

  fprintf(doc->outputfp, "userdict");
  if (strcmp(classification, "confidential") == 0)
    fprintf(doc->outputfp, "/ESPpl(CONFIDENTIAL");
  else if (strcmp(classification, "classified") == 0)
    fprintf(doc->outputfp, "/ESPpl(CLASSIFIED");
  else if (strcmp(classification, "secret") == 0)
    fprintf(doc->outputfp, "/ESPpl(SECRET");
  else if (strcmp(classification, "topsecret") == 0)
    fprintf(doc->outputfp, "/ESPpl(TOP SECRET");
  else if (strcmp(classification, "unclassified") == 0)
    fprintf(doc->outputfp, "/ESPpl(UNCLASSIFIED");
  else
  {
    fprintf(doc->outputfp, "/ESPpl(");

    for (ptr = classification; *ptr; ptr ++)
      if (*ptr < 32 || *ptr > 126)
        fprintf(doc->outputfp, "\\%03o", *ptr);
      else if (*ptr == '_')
        fputc(' ', doc->outputfp);
      else
      {
	if (*ptr == '(' || *ptr == ')' || *ptr == '\\')
	  fputc('\\', doc->outputfp);

	fputc(*ptr, doc->outputfp);
      }
  }

  if (label)
  {
    if (classification[0])
      fprintf(doc->outputfp, " - ");

   /*
    * Quote the label string as needed...
    */

    for (ptr = label; *ptr; ptr ++)
      if (*ptr < 32 || *ptr > 126)
        fprintf(doc->outputfp, "\\%03o", *ptr);
      else
      {
	if (*ptr == '(' || *ptr == ')' || *ptr == '\\')
	  fputc('\\', doc->outputfp);

	fputc(*ptr, doc->outputfp);
      }
  }

  fputs(")put\n", doc->outputfp);

 /*
  * Then get a 14 point Helvetica-Bold font...
  */

  fputs("userdict/ESPpf /Helvetica-Bold findfont 14 scalefont put\n",
	doc->outputfp);

 /*
  * Finally, the procedure to write the labels on the page...
  */

  fputs("userdict/ESPwl{\n", doc->outputfp);
  fputs("  ESPpf setfont\n", doc->outputfp);
  fprintf(doc->outputfp,
	  "  ESPpl stringwidth pop dup 12 add exch -0.5 mul %.0f add\n",
	  width * 0.5f);
  fputs("  1 setgray\n", doc->outputfp);
  fprintf(doc->outputfp, "  dup 6 sub %.0f 3 index 20 ESPrf\n", bottom - 2.0);
  fprintf(doc->outputfp, "  dup 6 sub %.0f 3 index 20 ESPrf\n", top - 18.0);
  fputs("  0 setgray\n", doc->outputfp);
  fprintf(doc->outputfp, "  dup 6 sub %.0f 3 index 20 ESPrs\n", bottom - 2.0);
  fprintf(doc->outputfp, "  dup 6 sub %.0f 3 index 20 ESPrs\n", top - 18.0);
  fprintf(doc->outputfp, "  dup %.0f moveto ESPpl show\n", bottom + 2.0);
  fprintf(doc->outputfp, "  %.0f moveto ESPpl show\n", top - 14.0);
  fputs("pop\n", doc->outputfp);
  fputs("}bind put\n", doc->outputfp);
}


/*
 * 'WriteLabels()' - Write the actual page labels.
 */

void
WriteLabels(imagetops_doc_t *doc,
            int orient)	/* I - Orientation of the page */
{
  float	width,		/* Width of page */
	length;		/* Length of page */


  fputs("gsave\n", doc->outputfp);

  if ((orient ^ doc->Orientation) & 1)
  {
    width  = doc->PageLength;
    length = doc->PageWidth;
  }
  else
  {
    width  = doc->PageWidth;
    length = doc->PageLength;
  }

  switch (orient & 3)
  {
    case 1 : /* Landscape */
        fprintf(doc->outputfp, "%.1f 0.0 translate 90 rotate\n", length);
        break;
    case 2 : /* Reverse Portrait */
        fprintf(doc->outputfp, "%.1f %.1f translate 180 rotate\n", width,
		length);
        break;
    case 3 : /* Reverse Landscape */
        fprintf(doc->outputfp, "0.0 %.1f translate -90 rotate\n", width);
        break;
  }

  fputs("ESPwl\n", doc->outputfp);
  fputs("grestore\n", doc->outputfp);
}


/*
 * 'WriteTextComment()' - Write a DSC text comment.
 */

void
WriteTextComment(imagetops_doc_t *doc,
                 const char *name,	/* I - Comment name ("Title", etc.) */
                 const char *value)	/* I - Comment value */
{
  int	len;				/* Current line length */


 /*
  * DSC comments are of the form:
  *
  *   %%name: value
  *
  * The name and value must be limited to 7-bit ASCII for most printers,
  * so we escape all non-ASCII and ASCII control characters as described
  * in the Adobe Document Structuring Conventions specification.
  */

  fprintf(doc->outputfp, "%%%%%s: (", name);
  len = 5 + strlen(name);

  while (*value)
  {
    if (*value < ' ' || *value >= 127)
    {
     /*
      * Escape this character value...
      */

      if (len >= 251)			/* Keep line < 254 chars */
        break;

      fprintf(doc->outputfp, "\\%03o", *value & 255);
      len += 4;
    }
    else if (*value == '\\')
    {
     /*
      * Escape the backslash...
      */

      if (len >= 253)			/* Keep line < 254 chars */
        break;

      fputc('\\', doc->outputfp);
      fputc('\\', doc->outputfp);
      len += 2;
    }
    else
    {
     /*
      * Put this character literally...
      */

      if (len >= 254)			/* Keep line < 254 chars */
        break;

      fputc(*value, doc->outputfp);
      len ++;
    }

    value ++;
  }

  fputs(")\n", doc->outputfp);
}


/*
 * 'ps_hex()' - Print binary data as a series of hexadecimal numbers.
 */

static void
ps_hex(FILE *outputfp,
       cups_ib_t *data,			/* I - Data to print */
       int       length,		/* I - Number of bytes to print */
       int       last_line)		/* I - Last line of raster data? */
{
  static int	col = 0;		/* Current column */
  static char	*hex = "0123456789ABCDEF";
					/* Hex digits */


  while (length > 0)
  {
   /*
    * Put the hex chars out to the file; note that we don't use fprintf()
    * for speed reasons...
    */

    fputc(hex[*data >> 4], outputfp);
    fputc(hex[*data & 15], outputfp);

    data ++;
    length --;

    col += 2;
    if (col > 78)
    {
      fputc('\n', outputfp);
      col = 0;
    }
  }

  if (last_line && col)
  {
    fputc('\n', outputfp);
    col = 0;
  }
}


/*
 * 'ps_ascii85()' - Print binary data as a series of base-85 numbers.
 */

static void
ps_ascii85(FILE *outputfp,
	   cups_ib_t *data,		/* I - Data to print */
	   int       length,		/* I - Number of bytes to print */
	   int       last_line)		/* I - Last line of raster data? */
{
  unsigned	b;			/* Binary data word */
  unsigned char	c[5];			/* ASCII85 encoded chars */
  static int	col = 0;		/* Current column */


  while (length > 3)
  {
    b = (((((data[0] << 8) | data[1]) << 8) | data[2]) << 8) | data[3];

    if (b == 0)
    {
      fputc('z', outputfp);
      col ++;
    }
    else
    {
      c[4] = (b % 85) + '!';
      b /= 85;
      c[3] = (b % 85) + '!';
      b /= 85;
      c[2] = (b % 85) + '!';
      b /= 85;
      c[1] = (b % 85) + '!';
      b /= 85;
      c[0] = b + '!';

      fwrite(c, 5, 1, outputfp);
      col += 5;
    }

    data += 4;
    length -= 4;

    if (col >= 75)
    {
      fputc('\n', outputfp);
      col = 0;
    }
  }

  if (last_line)
  {
    if (length > 0)
    {
      memset(data + length, 0, 4 - length);
      b = (((((data[0] << 8) | data[1]) << 8) | data[2]) << 8) | data[3];

      c[4] = (b % 85) + '!';
      b /= 85;
      c[3] = (b % 85) + '!';
      b /= 85;
      c[2] = (b % 85) + '!';
      b /= 85;
      c[1] = (b % 85) + '!';
      b /= 85;
      c[0] = b + '!';

      fwrite(c, length + 1, 1, outputfp);
    }

    fputs("~>\n", outputfp);
    col = 0;
  }
}

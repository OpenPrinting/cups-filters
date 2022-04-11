/*
 *   Image file to raster filter function for cups-filters
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   cfFilterImageToRaster() - The image conversion filter function
 *   blank_line()    - Clear a line buffer to the blank value...
 *   format_cmy()    - Convert image data to CMY.
 *   format_cmyk()   - Convert image data to CMYK.
 *   format_k()      - Convert image data to black.
 *   format_kcmy()   - Convert image data to KCMY.
 *   format_kcmycm() - Convert image data to KCMYcm.
 *   format_rgba()   - Convert image data to RGBA/RGBW.
 *   format_w()      - Convert image data to luminance.
 *   format_ymc()    - Convert image data to YMC.
 *   format_ymck()   - Convert image data to YMCK.
 *   make_lut()      - Make a lookup table given gamma and brightness values.
 *   raster_cb()     - Validate the page header.
 */

/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
#include <cupsfilters/raster.h>
#include <cupsfilters/colormanager.h>
#include <cupsfilters/ppdgenerator.h>
#include <cupsfilters/image-private.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <string.h>


/*
 * Types...
 */

typedef struct {                /**** Document information ****/
  int	Flip,			/* Flip/mirror pages */
        XPosition,		/* Horizontal position on page */
	YPosition,		/* Vertical position on page */
	Collate,		/* Collate copies? */
	Copies;			/* Number of copies */
  int   Orientation,    	/* 0 = portrait, 1 = landscape, etc. */
        Duplex,         	/* Duplexed? */
        LanguageLevel,  	/* Language level of printer */
        Color;    		/* Print in color? */
  float PageLeft,       	/* Left margin */
        PageRight,      	/* Right margin */
        PageBottom,     	/* Bottom margin */
        PageTop,        	/* Top margin */
        PageWidth,      	/* Total page width */
        PageLength;     	/* Total page length */
  cf_ib_t OnPixels[256],	/* On-pixel LUT */
	    OffPixels[256];	/* Off-pixel LUT */
  cf_logfunc_t logfunc;     /* Logging function, NULL for no
				   logging */
  void  *logdata;               /* User data for logging function, can
				   be NULL */
} imagetoraster_doc_t;


/*
 * Constants...
 */

int	Floyd16x16[16][16] =		/* Traditional Floyd ordered dither */
	{
	  { 0,   128, 32,  160, 8,   136, 40,  168,
	    2,   130, 34,  162, 10,  138, 42,  170 },
	  { 192, 64,  224, 96,  200, 72,  232, 104,
	    194, 66,  226, 98,  202, 74,  234, 106 },
	  { 48,  176, 16,  144, 56,  184, 24,  152,
	    50,  178, 18,  146, 58,  186, 26,  154 },
	  { 240, 112, 208, 80,  248, 120, 216, 88,
	    242, 114, 210, 82,  250, 122, 218, 90 },
	  { 12,  140, 44,  172, 4,   132, 36,  164,
	    14,  142, 46,  174, 6,   134, 38,  166 },
	  { 204, 76,  236, 108, 196, 68,  228, 100,
	    206, 78,  238, 110, 198, 70,  230, 102 },
	  { 60,  188, 28,  156, 52,  180, 20,  148,
	    62,  190, 30,  158, 54,  182, 22,  150 },
	  { 252, 124, 220, 92,  244, 116, 212, 84,
	    254, 126, 222, 94,  246, 118, 214, 86 },
	  { 3,   131, 35,  163, 11,  139, 43,  171,
	    1,   129, 33,  161, 9,   137, 41,  169 },
	  { 195, 67,  227, 99,  203, 75,  235, 107,
	    193, 65,  225, 97,  201, 73,  233, 105 },
	  { 51,  179, 19,  147, 59,  187, 27,  155,
	    49,  177, 17,  145, 57,  185, 25,  153 },
	  { 243, 115, 211, 83,  251, 123, 219, 91,
	    241, 113, 209, 81,  249, 121, 217, 89 },
	  { 15,  143, 47,  175, 7,   135, 39,  167,
	    13,  141, 45,  173, 5,   133, 37,  165 },
	  { 207, 79,  239, 111, 199, 71,  231, 103,
	    205, 77,  237, 109, 197, 69,  229, 101 },
	  { 63,  191, 31,  159, 55,  183, 23,  151,
	    61,  189, 29,  157, 53,  181, 21,  149 },
	  { 255, 127, 223, 95,  247, 119, 215, 87,
	    253, 125, 221, 93,  245, 117, 213, 85 }
	};
int	Floyd8x8[8][8] =
	{
	  {  0, 32,  8, 40,  2, 34, 10, 42 },
	  { 48, 16, 56, 24, 50, 18, 58, 26 },
	  { 12, 44,  4, 36, 14, 46,  6, 38 },
	  { 60, 28, 52, 20, 62, 30, 54, 22 },
	  {  3, 35, 11, 43,  1, 33,  9, 41 },
	  { 51, 19, 59, 27, 49, 17, 57, 25 },
	  { 15, 47,  7, 39, 13, 45,  5, 37 },
	  { 63, 31, 55, 23, 61, 29, 53, 21 }
	};
int	Floyd4x4[4][4] =
	{
	  {  0,  8,  2, 10 },
	  { 12,  4, 14,  6 },
	  {  3, 11,  1,  9 },
	  { 15,  7, 13,  5 }
	};


/*
 * Local functions...
 */

static void	blank_line(cups_page_header2_t *header, unsigned char *row);
static void	format_cmy(imagetoraster_doc_t *doc,
			   cups_page_header2_t *header, unsigned char *row,
			   int y, int z, int xsize, int ysize, int yerr0,
			   int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_cmyk(imagetoraster_doc_t *doc,
			    cups_page_header2_t *header, unsigned char *row,
			    int y, int z, int xsize, int ysize, int yerr0,
			    int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_K(imagetoraster_doc_t *doc,
			 cups_page_header2_t *header, unsigned char *row,
			 int y, int z, int xsize, int ysize, int yerr0,
			 int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_kcmycm(imagetoraster_doc_t *doc,
			      cups_page_header2_t *header, unsigned char *row,
			      int y, int z, int xsize, int ysize, int yerr0,
			      int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_kcmy(imagetoraster_doc_t *doc,
			    cups_page_header2_t *header, unsigned char *row,
			    int y, int z, int xsize, int ysize, int yerr0,
			    int yerr1, cf_ib_t *r0, cf_ib_t *r1);
#define		format_RGB format_cmy
static void	format_rgba(imagetoraster_doc_t *doc,
			    cups_page_header2_t *header, unsigned char *row,
			    int y, int z, int xsize, int ysize, int yerr0,
			    int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_w(imagetoraster_doc_t *doc,
			 cups_page_header2_t *header, unsigned char *row,
			 int y, int z, int xsize, int ysize, int yerr0,
			 int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_ymc(imagetoraster_doc_t *doc,
			   cups_page_header2_t *header, unsigned char *row,
			   int y, int z, int xsize, int ysize, int yerr0,
			   int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	format_ymck(imagetoraster_doc_t *doc,
			    cups_page_header2_t *header, unsigned char *row,
			    int y, int z, int xsize, int ysize, int yerr0,
			    int yerr1, cf_ib_t *r0, cf_ib_t *r1);
static void	make_lut(cf_ib_t *, int, float, float);


/*
 * 'cfFilterImageToRaster()' - Filter function to convert many common image file
 *                     formats into CUPS Raster
 */

int                                /* O - Error status */
cfFilterImageToRaster(int inputfd,         /* I - File descriptor input stream */
	      int outputfd,        /* I - File descriptor output stream */
	      int inputseekable,   /* I - Is input stream seekable? (unused) */
	      cf_filter_data_t *data, /* I - Job and printer data */
	      void *parameters)    /* I - Filter-specific parameters (unused) */
{
  imagetoraster_doc_t	doc;		/* Document information */
  int			i;		/* Looping var */
  cf_image_t		*img;		/* Image to print */
  float			xprint,		/* Printable area */
			yprint,
			xinches,	/* Total size in inches */
			yinches;
  float			xsize,		/* Total size in points */
			ysize,
			xsize2,
			ysize2;
  float			aspect;		/* Aspect ratio */
  int			xpages,		/* # x pages */
			ypages,		/* # y pages */
			xpage,		/* Current x page */
			ypage,		/* Current y page */
			xtemp,		/* Bitmap width in pixels */
			ytemp,		/* Bitmap height in pixels */
			page;		/* Current page number */
  int			xc0, yc0,	/* Corners of the page in image
					   coords */
			xc1, yc1;
  ppd_file_t		*ppd;		/* PPD file */
  ppd_choice_t		*choice;	/* PPD option choice */
  cups_cspace_t         cspace = -1;    /* CUPS color space */
  char			*resolution,	/* Output resolution */
			*media_type;	/* Media type */
  ppd_profile_t		*profile;	/* Color profile */
  ppd_profile_t		userprofile;	/* User-specified profile */
  cups_raster_t		*ras;		/* Raster stream */
  cups_page_header2_t	header;		/* Page header */
  int			num_options = 0;	/* Number of print options */
  cups_option_t		*options = NULL;	/* Print options */
  const char		*val;		/* Option value */
  int			slowcollate,	/* Collate copies the slow way */
			slowcopies;	/* Make copies the "slow" way? */
  float			g;		/* Gamma correction value */
  float			b;		/* Brightness factor */
  float			zoom;		/* Zoom facter */
  int			xppi, yppi;	/* Pixels-per-inch */
  int			hue, sat;	/* Hue and saturation adjustment */
  cf_izoom_t		*z;		/* Image zoom buffer */
  cf_iztype_t		zoom_type;	/* Image zoom type */
  int			primary,	/* Primary image colorspace */
			secondary;	/* Secondary image colorspace */
  cf_ib_t		*row,		/* Current row */
			*r0,		/* Top row */
			*r1;		/* Bottom row */
  int			y,		/* Current Y coordinate on page */
			iy,		/* Current Y coordinate in image */
			last_iy,	/* Previous Y coordinate in image */
			yerr0,		/* Top Y error value */
			yerr1;		/* Bottom Y error value */
  cf_ib_t		lut[256];	/* Gamma/brightness LUT */
  int			plane,		/* Current color plane */
			num_planes;	/* Number of color planes */
  char			tempfile[1024];	/* Name of temporary file */
  FILE                  *fp;		/* Input file */
  int                   fd;		/* File descriptor for temp file */
  char                  buf[BUFSIZ];
  int                   bytes;
  cf_cm_calibration_t      cm_calibrate;   /* Are we color calibrating the
					   device? */
  int                   cm_disabled;    /* Color management disabled? */
  int                   fillprint = 0;	/* print-scaling = fill */
  int                   cropfit = 0;	/* -o crop-to-fit */
  cf_logfunc_t      log = data->logfunc;
  void                  *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void                  *icd = data->iscanceleddata;
  ipp_t                 *printer_attrs = data->printer_attrs;
  ipp_t                 *job_attrs = data->job_attrs;
  ipp_attribute_t *ipp;
  int 			min_length = __INT32_MAX__,       /*  ppd->custom_min[1]	*/
      			min_width = __INT32_MAX__,        /*  ppd->custom_min[0]	*/
      			max_length = 0, 		  /*  ppd->custom_max[1]	*/
      			max_width=0;			/*  ppd->custom_max[0]	*/
  float 		customLeft = 0.0,		/*  ppd->custom_margin[0]  */
        		customBottom = 0.0,	        /*  ppd->custom_margin[1]  */
			customRight = 0.0,	        /*  ppd->custom_margin[2]  */
			customTop = 0.0;	        /*  ppd->custom_margin[3]  */
  char 			defSize[41];
  cf_filter_out_format_t   outformat;

  /* Note: With the CF_FILTER_OUT_FORMAT_APPLE_RASTER,
     CF_FILTER_OUT_FORMAT_PWG_RASTER, or CF_FILTER_OUT_FORMAT_PCLM selections the
     output is actually CUPS Raster but information about available
     color spaces and depths are taken from the urf-supported or
     pwg-raster-document-type-supported printer IPP attributes or
     appropriate PPD file attributes (PCLM is always sRGB
     8-bit). These modes are for further processing with rastertopwg
     or rastertopclm. This can change in the future when we add Apple
     Raster and PWG Raster output support to this filter. */

  if (parameters) {
    outformat = *(cf_filter_out_format_t *)parameters;
    if (outformat != CF_FILTER_OUT_FORMAT_PCLM &&
	outformat != CF_FILTER_OUT_FORMAT_CUPS_RASTER &&
	outformat != CF_FILTER_OUT_FORMAT_PWG_RASTER &&
	outformat != CF_FILTER_OUT_FORMAT_APPLE_RASTER)
      outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
  } else
    outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToRaster: Final output format: %s",
	       (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ? "CUPS Raster" :
		(outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ? "PWG Raster" :
		 (outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER ? "Apple Raster" :
		  "PCLm"))));

  if (printer_attrs != NULL) {
    int left, right, top, bottom;
    cfGenerateSizes(printer_attrs, &ipp, &min_length, &min_width,
		    &max_length, &max_width, &bottom, &left, &right, &top,
		    defSize);
    customLeft = left*72.0/2540.0;
    customRight = right*72.0/2540.0;
    customTop = top*72.0/2540.0;
    customBottom = bottom*72.0/2540.0;
  }

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

  doc.Flip = 0;
  doc.XPosition = 0;
  doc.YPosition = 0;
  doc.Collate = 0;
  doc.Copies = 1;
  doc.logfunc = data->logfunc;
  doc.logdata = data->logdata;

 /*
  * Option list...
  */

  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

 /*
  * Open the input data stream specified by the inputfd ...
  */

  if ((fp = fdopen(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToRaster: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Copy input into temporary file if needed ...
  */

  if (!inputseekable) {
    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToRaster: Unable to copy input: %s",
		   strerror(errno));
      fclose(fp);
      return (1);
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Copying input to temp file \"%s\"",
		 tempfile);

    while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0)
      bytes = write(fd, buf, bytes);

    fclose(fp);
    close(fd);

   /*
    * Open the temporary file to read it instead of the original input ...
    */

    if ((fp = fopen(tempfile, "r")) == NULL)
    {
      if (!iscanceled || !iscanceled(icd))
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster: Unable to open temporary file.");
      }

      unlink(tempfile);
      return (1);
    }
  }

 /*
  * Process options and write the prolog...
  */

  zoom = 1.0;
  xppi = 0;
  yppi = 0;
  hue  = 0;
  sat  = 100;
  g    = 1.0;
  b    = 1.0;

  doc.Copies = data->copies;

 /*
  * Process job options...
  */

  cfRasterPrepareHeader(&header, data, outformat,
			  CF_FILTER_OUT_FORMAT_CUPS_RASTER, 1, &cspace);
  ppd = data->ppd;
  doc.Orientation = header.Orientation;
  doc.Duplex = header.Duplex;
  doc.LanguageLevel = 1;
  doc.Color = header.cupsNumColors>1?1:0;
  doc.PageLeft = header.cupsImagingBBox[0] != 0.0 ?
    header.cupsImagingBBox[0] :
    (float)header.ImagingBoundingBox[0];
  doc.PageRight = header.cupsImagingBBox[2] != 0.0 ?
    header.cupsImagingBBox[2] :
    (float)header.ImagingBoundingBox[2];
  doc.PageTop = header.cupsImagingBBox[3] != 0.0 ?
    header.cupsImagingBBox[3] :
    (float)header.ImagingBoundingBox[3];
  doc.PageBottom = header.cupsImagingBBox[1] != 0.0 ?
    header.cupsImagingBBox[1] :
    (float)header.ImagingBoundingBox[1];
  doc.PageWidth = header.cupsPageSize[0] != 0.0 ?
    header.cupsPageSize[0] :
    (float)header.PageSize[0];
  doc.PageLength = header.cupsPageSize[1] != 0.0 ?
    header.cupsPageSize[1] :
    (float)header.PageSize[1];

  if (log)
  {
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.color = %d", doc.Color);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.Orientation = %d", doc.Orientation);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.Duplex = %d", doc.Duplex);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.PageWidth = %.1f", doc.PageWidth);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.PageLength = %.1f", doc.PageLength);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.PageLeft = %.1f", doc.PageLeft);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.PageRight = %.1f", doc.PageRight);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.PageTop = %.1f", doc.PageTop);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: doc.PageBottom = %.1f", doc.PageBottom);
  }

  /*  Find print-rendering-intent */

  cfGetPrintRenderIntent(data, &header);
  if(log) log(ld, CF_LOGLEVEL_DEBUG,
	      "cfFilterImageToRaster: Print rendering intent = %s",
	      header.cupsRenderingIntent);

  if ((val = cupsGetOption("multiple-document-handling",
			   num_options, options)) != NULL)
  {
   /*
    * This IPP attribute is unnecessarily complicated...
    *
    *   single-document, separate-documents-collated-copies, and
    *   single-document-new-sheet all require collated copies.
    *
    *   separate-documents-collated-copies allows for uncollated copies.
    */

    doc.Collate = strcasecmp(val, "separate-documents-collated-copies") != 0;
  }

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL &&
      strcasecmp(val, "True") == 0)
    doc.Collate = 1;

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
      doc.XPosition = 0;
      doc.YPosition = 0;
    }
    else if (strcasecmp(val, "top") == 0)
    {
      doc.XPosition = 0;
      doc.YPosition = 1;
    }
    else if (strcasecmp(val, "left") == 0)
    {
      doc.XPosition = -1;
      doc.YPosition = 0;
    }
    else if (strcasecmp(val, "right") == 0)
    {
      doc.XPosition = 1;
      doc.YPosition = 0;
    }
    else if (strcasecmp(val, "top-left") == 0)
    {
      doc.XPosition = -1;
      doc.YPosition = 1;
    }
    else if (strcasecmp(val, "top-right") == 0)
    {
      doc.XPosition = 1;
      doc.YPosition = 1;
    }
    else if (strcasecmp(val, "bottom") == 0)
    {
      doc.XPosition = 0;
      doc.YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-left") == 0)
    {
      doc.XPosition = -1;
      doc.YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-right") == 0)
    {
      doc.XPosition = 1;
      doc.YPosition = -1;
    }
  }

  if ((val = cupsGetOption("saturation", num_options, options)) != NULL)
    sat = atoi(val);

  if ((val = cupsGetOption("hue", num_options, options)) != NULL)
    hue = atoi(val);

  if ((choice = ppdFindMarkedChoice(ppd, "MirrorPrint")) != NULL ||
      ((val = cupsGetOption("MirrorPrint", num_options, options)) != NULL) ||
      (ipp = ippFindAttribute(job_attrs, "mirror-print", IPP_TAG_ZERO)) !=
      NULL ||
      (ipp = ippFindAttribute(printer_attrs, "mirror-print-default",
			      IPP_TAG_ZERO))!=NULL)
  {
    if (val != NULL)
    {
      /*  We already found the value  */
    }
    else if (ipp != NULL) {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    else
    {
      val = choice->choice;
      choice->marked = 0;
    }
  }
  else
    val = cupsGetOption("mirror", num_options, options);

  if (val && (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
              !strcasecmp(val, "yes")))
    doc.Flip = 1;

 /*
  * Get the media type and resolution that have been chosen...
  */

  if ((choice = ppdFindMarkedChoice(ppd, "MediaType")) != NULL ||
      (val = cupsGetOption("MediaType", num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "media-type", IPP_TAG_ZERO)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "media-type-supported",
			      IPP_TAG_ZERO))!=NULL)
  {
    if (val != NULL)
    {
      media_type = strdup(val);
    }
    else if(choice!=NULL)
      media_type = strdup(choice->choice);
    else if(ipp!=NULL){
      media_type = strdup(ippGetString(ipp, 0, NULL));
    }
  }
  else
    media_type = strdup("");

  if ((choice = ppdFindMarkedChoice(ppd, "Resolution")) != NULL)
    resolution = strdup(choice->choice);
  else if ((val = cupsGetOption("Resolution", num_options, options)) != NULL ||
	   (ipp = ippFindAttribute(job_attrs, "printer-resolution",
				   IPP_TAG_ZERO))!=NULL)
  {
    if (val == NULL) {
      ippAttributeString(ipp, buf, sizeof(buf));
      resolution = strdup(buf);
    }
    else
    {
      resolution = strdup(val);
    }
  }
  else if ((ipp = ippFindAttribute(printer_attrs, "printer-resolution-default",
				   IPP_TAG_ZERO)) != NULL)
  {
    ippAttributeString(ipp, buf, sizeof(buf));
    resolution = strdup(buf);
  }
  else if((ipp = ippFindAttribute(printer_attrs,
				  "printer-resolution-supported",
				  IPP_TAG_ZERO)) != NULL)
  {
    ippAttributeString(ipp, buf, sizeof(buf));
    resolution = strdup(buf);
    for (i = 0; resolution[i] != '\0'; i++)
    {
      if (resolution[i]==' ' ||
	  resolution[i]==',')
      {
	resolution[i] = '\0';
	break;
      }
    }
  }
  else
    resolution = strdup("300dpi");
  if(log) log(ld, CF_LOGLEVEL_DEBUG, "Resolution = %s", resolution);

  /* support the "cm-calibration" option */
  cm_calibrate = cfCmGetCupsColorCalibrateMode(data, options, num_options);

  if (cm_calibrate == CF_CM_CALIBRATION_ENABLED)
    cm_disabled = 1;
  else
    cm_disabled = cfCmIsPrinterCmDisabled(data);

 /*
  * Choose the appropriate colorspace...
  */

  switch (header.cupsColorSpace)
  {
    case CUPS_CSPACE_W :
    case CUPS_CSPACE_SW :
        if (header.cupsBitsPerColor >= 8)
	{
          primary   = CF_IMAGE_WHITE;
	  secondary = CF_IMAGE_WHITE;
        }
	else
	{
          primary   = CF_IMAGE_BLACK;
	  secondary = CF_IMAGE_BLACK;
	}
	break;

    default :
    case CUPS_CSPACE_RGB :
    case CUPS_CSPACE_RGBA :
    case CUPS_CSPACE_RGBW :
    case CUPS_CSPACE_SRGB :
    case CUPS_CSPACE_ADOBERGB :
        if (header.cupsBitsPerColor >= 8)
	{
          primary   = CF_IMAGE_RGB;
	  secondary = CF_IMAGE_RGB;
        }
	else
	{
          primary   = CF_IMAGE_CMY;
	  secondary = CF_IMAGE_CMY;
	}
	break;

    case CUPS_CSPACE_K :
    case CUPS_CSPACE_WHITE :
    case CUPS_CSPACE_GOLD :
    case CUPS_CSPACE_SILVER :
        primary   = CF_IMAGE_BLACK;
	secondary = CF_IMAGE_BLACK;
	break;

    case CUPS_CSPACE_CMYK :
    case CUPS_CSPACE_YMCK :
    case CUPS_CSPACE_KCMY :
    case CUPS_CSPACE_KCMYcm :
    case CUPS_CSPACE_GMCK :
    case CUPS_CSPACE_GMCS :
        if (header.cupsBitsPerColor == 1)
	{
          primary   = CF_IMAGE_CMY;
	  secondary = CF_IMAGE_CMY;
	}
	else
	{
          primary   = CF_IMAGE_CMYK;
	  secondary = CF_IMAGE_CMYK;
	}
	break;

    case CUPS_CSPACE_CMY :
    case CUPS_CSPACE_YMC :
        primary   = CF_IMAGE_CMY;
	secondary = CF_IMAGE_CMY;
	break;

    case CUPS_CSPACE_CIEXYZ :
    case CUPS_CSPACE_CIELab :
    case CUPS_CSPACE_ICC1 :
    case CUPS_CSPACE_ICC2 :
    case CUPS_CSPACE_ICC3 :
    case CUPS_CSPACE_ICC4 :
    case CUPS_CSPACE_ICC5 :
    case CUPS_CSPACE_ICC6 :
    case CUPS_CSPACE_ICC7 :
    case CUPS_CSPACE_ICC8 :
    case CUPS_CSPACE_ICC9 :
    case CUPS_CSPACE_ICCA :
    case CUPS_CSPACE_ICCB :
    case CUPS_CSPACE_ICCC :
    case CUPS_CSPACE_ICCD :
    case CUPS_CSPACE_ICCE :
    case CUPS_CSPACE_ICCF :
    case CUPS_CSPACE_DEVICE1 :
    case CUPS_CSPACE_DEVICE2 :
    case CUPS_CSPACE_DEVICE3 :
    case CUPS_CSPACE_DEVICE4 :
    case CUPS_CSPACE_DEVICE5 :
    case CUPS_CSPACE_DEVICE6 :
    case CUPS_CSPACE_DEVICE7 :
    case CUPS_CSPACE_DEVICE8 :
    case CUPS_CSPACE_DEVICE9 :
    case CUPS_CSPACE_DEVICEA :
    case CUPS_CSPACE_DEVICEB :
    case CUPS_CSPACE_DEVICEC :
    case CUPS_CSPACE_DEVICED :
    case CUPS_CSPACE_DEVICEE :
    case CUPS_CSPACE_DEVICEF :
        if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster: Colorspace %d not supported.",
		     header.cupsColorSpace);
	if (!inputseekable)
	  unlink(tempfile);
	return(1);
	break;
  }

 /*
  * Find a color profile matching the current options...
  */
   
  if ((val = cupsGetOption("profile", num_options, options)) != NULL &&
      !cm_disabled)
  {
    profile = &userprofile;
    sscanf(val, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
           &(userprofile.density), &(userprofile.gamma),
	   userprofile.matrix[0] + 0, userprofile.matrix[0] + 1,
	   userprofile.matrix[0] + 2,
	   userprofile.matrix[1] + 0, userprofile.matrix[1] + 1,
	   userprofile.matrix[1] + 2,
	   userprofile.matrix[2] + 0, userprofile.matrix[2] + 1,
	   userprofile.matrix[2] + 2);

    userprofile.density      *= 0.001f;
    userprofile.gamma        *= 0.001f;
    userprofile.matrix[0][0] *= 0.001f;
    userprofile.matrix[0][1] *= 0.001f;
    userprofile.matrix[0][2] *= 0.001f;
    userprofile.matrix[1][0] *= 0.001f;
    userprofile.matrix[1][1] *= 0.001f;
    userprofile.matrix[1][2] *= 0.001f;
    userprofile.matrix[2][0] *= 0.001f;
    userprofile.matrix[2][1] *= 0.001f;
    userprofile.matrix[2][2] *= 0.001f;
  }
  else if (ppd != NULL && !cm_disabled)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Searching for profile \"%s/%s\"...",
		 resolution, media_type);

    for (i = 0, profile = ppd->profiles; i < ppd->num_profiles;
	 i ++, profile ++)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToRaster: \"%s/%s\" = ", profile->resolution,
		   profile->media_type);

      if ((strcmp(profile->resolution, resolution) == 0 ||
           profile->resolution[0] == '-') &&
          (strcmp(profile->media_type, media_type) == 0 ||
           profile->media_type[0] == '-'))
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster:    MATCH");
	break;
      }
      else
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster:    no.");
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
    cfImageSetProfile(profile->density, profile->gamma, profile->matrix);

  cfImageSetRasterColorSpace(header.cupsColorSpace);

 /*
  * Create a gamma/brightness LUT...
  */

  make_lut(lut, primary, g, b);

 /*
  * Open the input image to print...
  */

  if (log) log(ld, CF_LOGLEVEL_INFO,
	       "cfFilterImageToRaster: Loading print file.");

  if (header.cupsColorSpace == CUPS_CSPACE_CIEXYZ ||
      header.cupsColorSpace == CUPS_CSPACE_CIELab ||
      header.cupsColorSpace >= CUPS_CSPACE_ICC1)
    img = cfImageOpenFP(fp, primary, secondary, sat, hue, NULL);
  else
    img = cfImageOpenFP(fp, primary, secondary, sat, hue, lut);

  if(img!=NULL){

  int margin_defined = 0;
  int fidelity = 0;
  int document_large = 0;

  if (ppd != NULL && (ppd->custom_margins[0] || ppd->custom_margins[1] ||
                      ppd->custom_margins[2] || ppd->custom_margins[3]))
    margin_defined = 1;
	else{
		if(customLeft!=0 || customRight!=0 || customBottom!=0 || customTop!=0)
			margin_defined = 1;
	}

  if (doc.PageLength != doc.PageTop - doc.PageBottom ||
      doc.PageWidth != doc.PageRight - doc.PageLeft)
  {
    margin_defined = 1;
  }

  if ((val = cupsGetOption("ipp-attribute-fidelity",
			   num_options, options)) != NULL)
  {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes")||
        !strcasecmp(val,"on"))
    {
      fidelity = 1;
    }
  }

  float w = (float)cfImageGetWidth(img);
  float h = (float)cfImageGetHeight(img);
  float pw = doc.PageRight-doc.PageLeft;
  float ph = doc.PageTop-doc.PageBottom;
  int tempOrientation = doc.Orientation;
  if((val = cupsGetOption("orientation-requested",num_options,options))!=NULL)
  {
    tempOrientation = atoi(val);
  }
  else if((val = cupsGetOption("landscape",num_options,options))!=NULL)
  {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
    {
      tempOrientation = 4;
    }
  }
  if(tempOrientation==0)
  {
    if(((pw > ph) && (w < h)) || ((pw < ph) && (w > h)))
    {
      tempOrientation = 4;
    }
  }
  if(tempOrientation==4||tempOrientation==5)
  {
    int tmp = pw;
    pw = ph;
    ph = tmp;
  }
  if (w * 72.0 / img->xppi > pw || h * 72.0 / img->yppi > ph)
    document_large = 1;

  if((val = cupsGetOption("print-scaling",num_options,options)) != NULL)
  {
    if(!strcasecmp(val,"auto"))
    {
      if(fidelity||document_large)
      {
        if(margin_defined)
          zoom = 1.0;       // fit method
        else
          fillprint = 1;    // fill method
      }
      else
        cropfit = 1;        // none method
    }
    else if(!strcasecmp(val,"auto-fit"))
    {
      if(fidelity||document_large)
        zoom = 1.0;         // fit method
      else
        cropfit = 1;        // none method
    }
    else if(!strcasecmp(val,"fill"))
      fillprint = 1;        // fill method
    else if(!strcasecmp(val,"fit"))
      zoom = 1.0;           // fitplot = 1 or fit method
    else
      cropfit=1;            // none or crop-to-fit
  }
  else{       // print-scaling is not defined, look for alternate options.
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
  else if ((val = cupsGetOption("natural-scaling", num_options, options))
	   != NULL)
    zoom = 0.0;

  if((val = cupsGetOption("fill",num_options,options))!=0) {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
    {
      fillprint = 1;
    }
  }
  if((val = cupsGetOption("crop-to-fit",num_options,options))!= NULL){
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
    {
      cropfit=1;
    }
  } }
  }

  if(img!=NULL)
  {
    if(fillprint||cropfit)
    {
      float w = (float)cfImageGetWidth(img);
      float h = (float)cfImageGetHeight(img);
      /* For cropfit do the math without the unprintable margins to get correct
	 centering, for fillprint, fill the printable area */
      float pw = (cropfit ? doc.PageWidth : doc.PageRight-doc.PageLeft);
      float ph = (cropfit ? doc.PageLength : doc.PageTop-doc.PageBottom);
      const char *val;
      int tempOrientation = doc.Orientation;
      int flag =3;
      if ((val = cupsGetOption("orientation-requested",
			       num_options, options)) != NULL)
      {
        tempOrientation = atoi(val);
      }
      else if((val = cupsGetOption("landscape",num_options,options))!=NULL)
      {
        if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
        {
          tempOrientation = 4;
        }
      }
      if(tempOrientation>0)
      {
        if(tempOrientation==4||tempOrientation==5)
        {
          float temp = pw;
          pw = ph;
          ph = temp;
          flag = 4;
        }
      }
      if(tempOrientation==0)
      {
        if(((pw > ph) && (w < h)) || ((pw < ph) && (w > h)))
        {
          int temp = pw;
          pw = ph;
          ph = temp;
          flag = 4;
        }
      }
      if(fillprint)
      {
        // Final width and height of cropped image.
        float final_w,final_h;
        if(w*ph/pw <=h){
          final_w =w;
          final_h =w*ph/pw; 
        }
        else{
          final_w = h*pw/ph;
          final_h = h;
        }
        // posw and posh are position of the cropped image along width and
	// height.
        float posw=(w-final_w)/2,posh=(h-final_h)/2;
        posw = (1+doc.XPosition)*posw;
        posh = (1-doc.YPosition)*posh;
        cf_image_t *img2 = cfImageCrop(img,posw,posh,final_w,final_h);
        cfImageClose(img);
        img = img2;
      }
      else {
        float final_w=w,final_h=h;
        if (w > pw * img->xppi / 72.0)
          final_w = pw * img->xppi / 72.0;
        if (h > ph * img->yppi / 72.0)
          final_h = ph * img->yppi / 72.0;
	float posw=(w-final_w)/2,posh=(h-final_h)/2;
        posw = (1+doc.XPosition)*posw;
	posh = (1-doc.YPosition)*posh;
	/* Check whether the unprintable margins hide away a part of the image,
	   if so, correct the image cut */
	if(flag==4)
	{
	  float margin, cutoff;
	  margin = (doc.PageLength - final_w * 72.0 / img->xppi) / 2;
	  if (margin >= doc.PageBottom)
	    doc.PageBottom = margin;
	  else
	  {
	    cutoff = (doc.PageBottom - margin) * img->xppi / 72.0;
	    final_w -= cutoff;
	    posw += cutoff;
	  }
	  margin = doc.PageBottom + final_w * 72.0 / img->xppi;
	  if (margin <= doc.PageTop)
	    doc.PageTop = margin;
	  else
	    final_w -= (margin - doc.PageTop) * img->xppi / 72.0;
	  margin = (doc.PageWidth - final_h * 72.0 / img->yppi) / 2;
	  if (margin >= doc.PageLeft)
	    doc.PageLeft = margin;
	  else
	  {
	    cutoff = (doc.PageLeft - margin) * img->yppi / 72.0;
	    final_h -= cutoff;
	    posh += cutoff;
	  }
	  margin = doc.PageLeft + final_h * 72.0 / img->yppi;
	  if (margin <= doc.PageRight)
	    doc.PageRight = margin;
	  else
	    final_h -= (margin - doc.PageRight) * img->yppi / 72.0;
	}
	else
	{
	  float margin, cutoff;
	  margin = (doc.PageLength - final_h * 72.0 / img->yppi) / 2;
	  if (margin >= doc.PageBottom)
	    doc.PageBottom = margin;
	  else
	  {
	    cutoff = (doc.PageBottom - margin) * img->yppi / 72.0;
	    final_h -= cutoff;
	    posh += cutoff;
	  }
	  margin = doc.PageBottom + final_h * 72.0 / img->yppi;
	  if (margin <= doc.PageTop)
	    doc.PageTop = margin;
	  else
	    final_h -= (margin - doc.PageTop) * img->yppi / 72.0;
	  margin = (doc.PageWidth - final_w * 72.0 / img->xppi) / 2;
	  if (margin >= doc.PageLeft)
	    doc.PageLeft = margin;
	  else
	  {
	    cutoff = (doc.PageLeft - margin) * img->xppi / 72.0;
	    final_w -= cutoff;
	    posw += cutoff;
	  }
	  margin = doc.PageLeft + final_w * 72.0 / img->xppi;
	  if (margin <= doc.PageRight)
	    doc.PageRight = margin;
	  else
	    final_w -= (margin - doc.PageRight) * img->xppi / 72.0;
	}
	if(doc.PageBottom<0) doc.PageBottom = 0;
	if(doc.PageLeft<0) doc.PageLeft = 0;
	cf_image_t *img2 = cfImageCrop(img,posw,posh,final_w,final_h);
	cfImageClose(img);
	img = img2;
      }	
    }
  }
  if (!inputseekable)
    unlink(tempfile);

  if (img == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterImageToRaster: The print file could not be opened.");
    return (1);
  }

 /*
  * Scale as necessary...
  */

  if (zoom == 0.0 && xppi == 0)
  {
    xppi = img->xppi;
    yppi = img->yppi;
  }

  if (yppi == 0)
    yppi = xppi;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToRaster: Before scaling: xppi=%d, yppi=%d, zoom=%.2f",
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

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Before scaling: xprint=%.1f, yprint=%.1f",
		 xprint, yprint);

    xinches = (float)img->xsize / (float)xppi;
    yinches = (float)img->ysize / (float)yppi;

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Image size is %.1f x %.1f inches...",
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

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToRaster: Auto orientation...");

      if ((xinches > xprint || yinches > yprint) &&
          xinches <= yprint && yinches <= xprint)
      {
       /*
	* Rotate the image as needed...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster: Using landscape orientation...");

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
    aspect = (float)img->yppi / (float)img->xppi;

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Before scaling: xprint=%.1f, yprint=%.1f",
		 xprint, yprint);

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: img->xppi = %d, img->yppi = %d, aspect = %f",
		 img->xppi, img->yppi, aspect);

    xsize = xprint * zoom;
    ysize = xsize * img->ysize / img->xsize / aspect;

    if (ysize > (yprint * zoom))
    {
      ysize = yprint * zoom;
      xsize = ysize * img->xsize * aspect / img->ysize;
    }

    xsize2 = yprint * zoom;
    ysize2 = xsize2 * img->ysize / img->xsize / aspect;

    if (ysize2 > (xprint * zoom))
    {
      ysize2 = xprint * zoom;
      xsize2 = ysize2 * img->xsize * aspect / img->ysize;
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Portrait size is %.2f x %.2f inches",
		 xsize, ysize);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToRaster: Landscape size is %.2f x %.2f inches",
		 xsize2, ysize2);

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Choose the rotation with the largest area, but prefer
      * portrait if they are equal...
      */

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToRaster: Auto orientation...");

      if ((xsize * ysize) < (xsize2 * ysize2))
      {
       /*
	* Do landscape orientation...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster: Using landscape orientation...");

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

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToRaster: Using portrait orientation...");

	doc.Orientation = 0;
	xinches     = xsize;
	yinches     = ysize;
      }
    }
    else if (doc.Orientation & 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToRaster: Using landscape orientation...");

      xinches     = xsize2;
      yinches     = ysize2;
      xprint      = (doc.PageTop - doc.PageBottom) / 72.0;
      yprint      = (doc.PageRight - doc.PageLeft) / 72.0;
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToRaster: Using portrait orientation...");

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

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToRaster: xpages = %dx%.2fin, ypages = %dx%.2fin",
	       xpages, xprint, ypages, yprint);

 /*
  * Compute the bitmap size...
  */

  /* If size if specified by user, use it, else default size from
     printer_attrs*/
  if ((ipp = ippFindAttribute(job_attrs, "media-size", IPP_TAG_ZERO)) != NULL ||
      (val = cupsGetOption("MediaSize", num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-size", IPP_TAG_ZERO)) != NULL ||
      (val = cupsGetOption("PageSize", num_options, options)) != NULL ) {
    if (val == NULL) {
      ippAttributeString(ipp, buf, sizeof(buf));
      strcpy(defSize, buf);
    }
    else
	snprintf(defSize, sizeof(defSize), "%s", val);
  }

  if (((choice = ppdFindMarkedChoice(ppd, "PageSize")) != NULL &&
       strcasecmp(choice->choice, "Custom") == 0) ||
      (strncasecmp(defSize, "Custom", 6)) == 0)
  {
    float	width,		/* New width in points */
		length;		/* New length in points */


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
   if(ppd!=NULL){
    	width  += ppd->custom_margins[0] + ppd->custom_margins[2];
    	length += ppd->custom_margins[1] + ppd->custom_margins[3];
	}
	else{
	  width  += customLeft + customRight;
	  length += customBottom + customTop;
	}
   /*
    * Enforce minimums...
    */
   if (ppd != NULL)
   {
     if (width < ppd->custom_min[0])
       width = ppd->custom_min[0];
   }
   else
   {
     if (width < min_width)
       width = min_width;
   }
   if (ppd != NULL) {
     if (length < ppd->custom_min[1])
       length = ppd->custom_min[1];
   }
   else
   {
     if(length < min_length)
       length = min_length;
   }

   if (log) log(ld, CF_LOGLEVEL_DEBUG,
		"cfFilterImageToRaster: Updated custom page size to %.2f x %.2f "
		"inches...",
		width / 72.0, length / 72.0);

   /*
    * Set the new custom size...
    */

    strcpy(header.cupsPageSizeName, "Custom");

    header.cupsPageSize[0] = width + 0.5;
    header.cupsPageSize[1] = length + 0.5;
    header.PageSize[0]     = width + 0.5;
    header.PageSize[1]     = length + 0.5;

   /*
    * Update page variables...
    */

    doc.PageWidth  = width;
    doc.PageLength = length;
    if (ppd != NULL)
      doc.PageLeft   = ppd->custom_margins[0];
    else
      doc.PageLeft = customLeft;
    if (ppd != NULL)
      doc.PageRight  = width - ppd->custom_margins[2];
    else
      doc.PageRight = width - customRight;
    if (ppd != NULL)
      doc.PageBottom = ppd->custom_margins[1];
    else
      doc.PageBottom = customBottom;
    if (ppd != NULL)
      doc.PageTop    = length - ppd->custom_margins[3];
    else
      doc.PageTop = length - customTop;

   /*
    * Remove margins from page size...
    */
   if (ppd != NULL)
   {
     width  -= ppd->custom_margins[0] + ppd->custom_margins[2];
     length -= ppd->custom_margins[1] + ppd->custom_margins[3];
   }
   else
   {
     width -= customLeft + customRight;
     length -= customTop + customBottom;
   }

   /*
    * Set the bitmap size...
    */

    header.cupsWidth  = width * header.HWResolution[0] / 72.0;
    header.cupsHeight = length * header.HWResolution[1] / 72.0;
  } else {
   /*
    * Set the bitmap size...
    */

    header.cupsWidth  = (doc.Orientation & 1 ? yprint : xprint) *
      header.HWResolution[0];
    header.cupsHeight = (doc.Orientation & 1 ? xprint : yprint) *
      header.HWResolution[1];
  }
  header.cupsBytesPerLine = (header.cupsBitsPerPixel *
			     header.cupsWidth + 7) / 8;

  if (header.cupsColorOrder == CUPS_ORDER_BANDED)
    header.cupsBytesPerLine *= header.cupsNumColors;

  header.Margins[0] = doc.PageLeft;
  header.Margins[1] = doc.PageBottom;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToRaster: PageSize = [%d %d]", header.PageSize[0],
	       header.PageSize[1]);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToRaster: PageLeft = %f, PageRight = %f, "
	       "PageBottom = %f, PageTop = %f",
	       doc.PageLeft, doc.PageRight, doc.PageBottom, doc.PageTop);

  switch (doc.Orientation)
  {
    default :
	switch (doc.XPosition)
	{
	  case -1 :
              header.cupsImagingBBox[0] = doc.PageLeft;
	      header.cupsImagingBBox[2] = doc.PageLeft + xprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[0] = (doc.PageRight + doc.PageLeft -
					   xprint * 72) / 2;
	      header.cupsImagingBBox[2] = (doc.PageRight + doc.PageLeft +
					   xprint * 72) / 2;
	      break;
	  case 1 :
              header.cupsImagingBBox[0] = doc.PageRight - xprint * 72;
	      header.cupsImagingBBox[2] = doc.PageRight;
	      break;
	}

	switch (doc.YPosition)
	{
	  case -1 :
              header.cupsImagingBBox[1] = doc.PageBottom;
	      header.cupsImagingBBox[3] = doc.PageBottom + yprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[1] = (doc.PageTop + doc.PageBottom -
					   yprint * 72) / 2;
	      header.cupsImagingBBox[3] = (doc.PageTop + doc.PageBottom +
					   yprint * 72) / 2;
	      break;
	  case 1 :
              header.cupsImagingBBox[1] = doc.PageTop - yprint * 72;
	      header.cupsImagingBBox[3] = doc.PageTop;
	      break;
	}
	break;

    case 1 :
	switch (doc.XPosition)
	{
	  case -1 :
              header.cupsImagingBBox[0] = doc.PageLeft;
	      header.cupsImagingBBox[2] = doc.PageLeft + yprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[0] = (doc.PageRight + doc.PageLeft -
					   yprint * 72) / 2;
	      header.cupsImagingBBox[2] = (doc.PageRight + doc.PageLeft +
					   yprint * 72) / 2;
	      break;
	  case 1 :
              header.cupsImagingBBox[0] = doc.PageRight - yprint * 72;
	      header.cupsImagingBBox[2] = doc.PageRight;
	      break;
	}

	switch (doc.YPosition)
	{
	  case -1 :
              header.cupsImagingBBox[1] = doc.PageBottom;
	      header.cupsImagingBBox[3] = doc.PageBottom + xprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[1] = (doc.PageTop + doc.PageBottom -
					   xprint * 72) / 2;
	      header.cupsImagingBBox[3] = (doc.PageTop + doc.PageBottom +
					   xprint * 72) / 2;
	      break;
	  case 1 :
              header.cupsImagingBBox[1] = doc.PageTop - xprint * 72;
	      header.cupsImagingBBox[3] = doc.PageTop;
	      break;
	}
	break;

    case 2 :
	switch (doc.XPosition)
	{
	  case 1 :
              header.cupsImagingBBox[0] = doc.PageLeft;
	      header.cupsImagingBBox[2] = doc.PageLeft + xprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[0] = (doc.PageRight + doc.PageLeft -
					   xprint * 72) / 2;
	      header.cupsImagingBBox[2] = (doc.PageRight + doc.PageLeft +
					   xprint * 72) / 2;
	      break;
	  case -1 :
              header.cupsImagingBBox[0] = doc.PageRight - xprint * 72;
	      header.cupsImagingBBox[2] = doc.PageRight;
	      break;
	}

	switch (doc.YPosition)
	{
	  case 1 :
              header.cupsImagingBBox[1] = doc.PageBottom;
	      header.cupsImagingBBox[3] = doc.PageBottom + yprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[1] = (doc.PageTop + doc.PageBottom -
					   yprint * 72) / 2;
	      header.cupsImagingBBox[3] = (doc.PageTop + doc.PageBottom +
					   yprint * 72) / 2;
	      break;
	  case -1 :
              header.cupsImagingBBox[1] = doc.PageTop - yprint * 72;
	      header.cupsImagingBBox[3] = doc.PageTop;
	      break;
	}
	break;

    case 3 :
	switch (doc.XPosition)
	{
	  case 1 :
              header.cupsImagingBBox[0] = doc.PageLeft;
	      header.cupsImagingBBox[2] = doc.PageLeft + yprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[0] = (doc.PageRight + doc.PageLeft -
					   yprint * 72) / 2;
	      header.cupsImagingBBox[2] = (doc.PageRight + doc.PageLeft +
					   yprint * 72) / 2;
	      break;
	  case -1 :
              header.cupsImagingBBox[0] = doc.PageRight - yprint * 72;
	      header.cupsImagingBBox[2] = doc.PageRight;
	      break;
	}

	switch (doc.YPosition)
	{
	  case 1 :
              header.cupsImagingBBox[1] = doc.PageBottom;
	      header.cupsImagingBBox[3] = doc.PageBottom + xprint * 72;
	      break;
	  default :
              header.cupsImagingBBox[1] = (doc.PageTop + doc.PageBottom -
					   xprint * 72) / 2;
	      header.cupsImagingBBox[3] = (doc.PageTop + doc.PageBottom +
					   xprint * 72) / 2;
	      break;
	  case -1 :
              header.cupsImagingBBox[1] = doc.PageTop - xprint * 72;
	      header.cupsImagingBBox[3] = doc.PageTop;
	      break;
	}
	break;
  }

  header.ImagingBoundingBox[0] = header.cupsImagingBBox[0];
  header.ImagingBoundingBox[1] = header.cupsImagingBBox[1];
  header.ImagingBoundingBox[2] = header.cupsImagingBBox[2];
  header.ImagingBoundingBox[3] = header.cupsImagingBBox[3];

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToRaster: Orientation: %d, XPosition: %d, YPosition: %d, "
	       "ImagingBoundingBox = [%d %d %d %d]",
	       doc.Orientation, doc.XPosition, doc.YPosition,
	       header.ImagingBoundingBox[0], header.ImagingBoundingBox[1],
	       header.ImagingBoundingBox[2], header.ImagingBoundingBox[3]);

  if (header.cupsColorOrder == CUPS_ORDER_PLANAR)
    num_planes = header.cupsNumColors;
  else
    num_planes = 1;

  if (header.cupsBitsPerColor >= 8)
    zoom_type = CF_IZOOM_NORMAL;
  else
    zoom_type = CF_IZOOM_FAST;

 /*
  * See if we need to collate, and if so how we need to do it...
  */

  if (xpages == 1 && ypages == 1)
    doc.Collate = 0;

  slowcollate = doc.Collate && ppdFindOption(ppd, "Collate") == NULL;
  if (ppd != NULL)
    slowcopies = ppd->manual_copies;
  else
    slowcopies = 1;

  if (doc.Copies > 1 && !slowcollate && !slowcopies)
  {
    header.Collate   = (cups_bool_t)doc.Collate;
    header.NumCopies = doc.Copies;

    doc.Copies = 1;
  }
  else
    header.NumCopies = 1;

 /*
  * Create the dithering lookup tables...
  */

  doc.OnPixels[0]    = 0x00;
  doc.OnPixels[255]  = 0xff;
  doc.OffPixels[0]   = 0x00;
  doc.OffPixels[255] = 0xff;

  switch (header.cupsBitsPerColor)
  {
    case 2 :
        for (i = 1; i < 255; i ++)
        {
          doc.OnPixels[i]  = 0x55 * (i / 85 + 1);
          doc.OffPixels[i] = 0x55 * (i / 64);
        }
        break;
    case 4 :
        for (i = 1; i < 255; i ++)
        {
          doc.OnPixels[i]  = 17 * (i / 17 + 1);
          doc.OffPixels[i] = 17 * (i / 16);
        }
        break;
  }

 /*
  * Output the pages...
  */

  if (log) {
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsWidth = %d", header.cupsWidth);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsHeight = %d", header.cupsHeight);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsBitsPerColor = %d", header.cupsBitsPerColor);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsBitsPerPixel = %d", header.cupsBitsPerPixel);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsBytesPerLine = %d", header.cupsBytesPerLine);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsColorOrder = %d", header.cupsColorOrder);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: cupsColorSpace = %d", header.cupsColorSpace);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToRaster: img->colorspace = %d", img->colorspace);
  }

  row = malloc(2 * header.cupsBytesPerLine);
  ras = cupsRasterOpen(outputfd, CUPS_RASTER_WRITE);

  for (i = 0, page = 1; i < doc.Copies; i ++)
    for (xpage = 0; xpage < xpages; xpage ++)
      for (ypage = 0; ypage < ypages; ypage ++, page ++)
      {
	if (iscanceled && iscanceled(icd))
        {
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterImageToRaster: Job canceled");
	  goto canceled;
	}

	if (log) log(ld, CF_LOGLEVEL_INFO,
		     "cfFilterImageToRaster: Formatting page %d.", page);

	if (doc.Orientation & 1)
	{
	  xc0    = img->xsize * ypage / ypages;
	  xc1    = img->xsize * (ypage + 1) / ypages - 1;
	  yc0    = img->ysize * xpage / xpages;
	  yc1    = img->ysize * (xpage + 1) / xpages - 1;

	  xtemp = header.HWResolution[0] * yprint;
	  ytemp = header.HWResolution[1] * xprint;
	}
	else
	{
	  xc0    = img->xsize * xpage / xpages;
	  xc1    = img->xsize * (xpage + 1) / xpages - 1;
	  yc0    = img->ysize * ypage / ypages;
	  yc1    = img->ysize * (ypage + 1) / ypages - 1;

	  xtemp = header.HWResolution[0] * xprint;
	  ytemp = header.HWResolution[1] * yprint;
        }

        cupsRasterWriteHeader2(ras, &header);

        for (plane = 0; plane < num_planes; plane ++)
	{
	 /*
	  * Initialize the image "zoom" engine...
	  */

          if (doc.Flip)
	    z = _cfImageZoomNew(img, xc0, yc0, xc1, yc1, -xtemp, ytemp,
	                          doc.Orientation & 1, zoom_type);
          else
	    z = _cfImageZoomNew(img, xc0, yc0, xc1, yc1, xtemp, ytemp,
	                          doc.Orientation & 1, zoom_type);

         /*
	  * Write leading blank space as needed...
	  */

          if (header.cupsHeight > z->ysize && doc.YPosition <= 0)
	  {
	    blank_line(&header, row);

            y = header.cupsHeight - z->ysize;
	    if (doc.YPosition == 0)
	      y /= 2;

	    if (log) log(ld, CF_LOGLEVEL_DEBUG,
			 "cfFilterImageToRaster: Writing %d leading blank lines...",
			 y);

	    for (; y > 0; y --)
	    {
	      if (cupsRasterWritePixels(ras, row, header.cupsBytesPerLine) <
	              header.cupsBytesPerLine)
	      {
		if (log) log(ld, CF_LOGLEVEL_ERROR,
			     "cfFilterImageToRaster: Unable to send raster data.");
		cfImageClose(img);
		return (1);
	      }
            }
	  }

         /*
	  * Then write image data...
	  */

	  for (y = z->ysize, yerr0 = 0, yerr1 = z->ysize, iy = 0, last_iy = -2;
               y > 0;
               y --)
	  {
	    if (iy != last_iy)
	    {
	      if (zoom_type != CF_IZOOM_FAST && (iy - last_iy) > 1)
        	_cfImageZoomFill(z, iy);

              _cfImageZoomFill(z, iy + z->yincr);

              last_iy = iy;
	    }

           /*
	    * Format this line of raster data for the printer...
	    */

    	    blank_line(&header, row);

            r0 = z->rows[z->row];
            r1 = z->rows[1 - z->row];

            switch (header.cupsColorSpace)
	    {
	      case CUPS_CSPACE_W :
	      case CUPS_CSPACE_SW :
		  format_w(&doc, &header, row, y, plane, z->xsize, z->ysize,
		           yerr0, yerr1, r0, r1);
		  break;
              default :
	      case CUPS_CSPACE_RGB :
	      case CUPS_CSPACE_SRGB :
	      case CUPS_CSPACE_ADOBERGB :
	          format_RGB(&doc, &header, row, y, plane, z->xsize, z->ysize,
		             yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_RGBA :
	      case CUPS_CSPACE_RGBW :
	          format_rgba(&doc, &header, row, y, plane, z->xsize, z->ysize,
		              yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_K :
	      case CUPS_CSPACE_WHITE :
	      case CUPS_CSPACE_GOLD :
	      case CUPS_CSPACE_SILVER :
	          format_K(&doc, &header, row, y, plane, z->xsize, z->ysize,
		           yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_CMY :
	          format_cmy(&doc, &header, row, y, plane, z->xsize, z->ysize,
		             yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_YMC :
	          format_ymc(&doc, &header, row, y, plane, z->xsize, z->ysize,
		             yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_CMYK :
	          format_cmyk(&doc, &header, row, y, plane, z->xsize, z->ysize,
		              yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_YMCK :
	      case CUPS_CSPACE_GMCK :
	      case CUPS_CSPACE_GMCS :
	          format_ymck(&doc, &header, row, y, plane, z->xsize, z->ysize,
		              yerr0, yerr1, r0, r1);
		  break;
	      case CUPS_CSPACE_KCMYcm :
	          if (header.cupsBitsPerColor == 1)
		  {
	            format_kcmycm(&doc, &header, row, y, plane, z->xsize,
				  z->ysize, yerr0, yerr1, r0, r1);
		    break;
		  }
	      case CUPS_CSPACE_KCMY :
	          format_kcmy(&doc, &header, row, y, plane, z->xsize, z->ysize,
		              yerr0, yerr1, r0, r1);
		  break;
	    }

           /*
	    * Write the raster data ...
	    */

	    if (cupsRasterWritePixels(ras, row, header.cupsBytesPerLine) <
	                              header.cupsBytesPerLine)
	    {
	      if (log) log(ld, CF_LOGLEVEL_DEBUG,
			   "cfFilterImageToRaster: Unable to send raster data.");
	      cfImageClose(img);
	      return (1);
	    }

           /*
	    * Compute the next scanline in the image...
	    */

	    iy    += z->ystep;
	    yerr0 += z->ymod;
	    yerr1 -= z->ymod;
	    if (yerr1 <= 0)
	    {
              yerr0 -= z->ysize;
              yerr1 += z->ysize;
              iy    += z->yincr;
	    }
	  }

         /*
	  * Write trailing blank space as needed...
	  */

          if (header.cupsHeight > z->ysize && doc.YPosition >= 0)
	  {
	    blank_line(&header, row);

            y = header.cupsHeight - z->ysize;
	    if (doc.YPosition == 0)
	      y = y - y / 2;

	    if (log) log(ld, CF_LOGLEVEL_DEBUG,
			 "cfFilterImageToRaster: Writing %d trailing blank lines...",
			 y);

	    for (; y > 0; y --)
	    {
	      if (cupsRasterWritePixels(ras, row, header.cupsBytesPerLine) <
	              header.cupsBytesPerLine)
	      {
		if (log) log(ld, CF_LOGLEVEL_ERROR,
			     "cfFilterImageToRaster: Unable to send raster data.");
		cfImageClose(img);
		return (1);
	      }
            }
	  }

         /*
	  * Free memory used for the "zoom" engine...
	  */
          _cfImageZoomDelete(z);
        }
      }
 /*
  * Close files...
  */

 canceled:
  free(resolution);
  free(media_type);
  free(row);
  cupsRasterClose(ras);
  cfImageClose(img);
  close(outputfd);

  return (0);
}


/*
 * 'blank_line()' - Clear a line buffer to the blank value...
 */

static void
blank_line(cups_page_header2_t *header,	/* I - Page header */
           unsigned char       *row)	/* I - Row buffer */
{
  int	count;				/* Remaining bytes */


  count = header->cupsBytesPerLine;

  switch (header->cupsColorSpace)
  {
    case CUPS_CSPACE_CIEXYZ :
        while (count > 2)
	{
	  *row++ = 242;
	  *row++ = 255;
	  *row++ = 255;
	  count -= 3;
	}
	break;

    case CUPS_CSPACE_CIELab :
    case CUPS_CSPACE_ICC1 :
    case CUPS_CSPACE_ICC2 :
    case CUPS_CSPACE_ICC3 :
    case CUPS_CSPACE_ICC4 :
    case CUPS_CSPACE_ICC5 :
    case CUPS_CSPACE_ICC6 :
    case CUPS_CSPACE_ICC7 :
    case CUPS_CSPACE_ICC8 :
    case CUPS_CSPACE_ICC9 :
    case CUPS_CSPACE_ICCA :
    case CUPS_CSPACE_ICCB :
    case CUPS_CSPACE_ICCC :
    case CUPS_CSPACE_ICCD :
    case CUPS_CSPACE_ICCE :
    case CUPS_CSPACE_ICCF :
        while (count > 2)
	{
	  *row++ = 255;
	  *row++ = 128;
	  *row++ = 128;
	  count -= 3;
	}
        break;

    case CUPS_CSPACE_K :
    case CUPS_CSPACE_CMY :
    case CUPS_CSPACE_CMYK :
    case CUPS_CSPACE_YMC :
    case CUPS_CSPACE_YMCK :
    case CUPS_CSPACE_KCMY :
    case CUPS_CSPACE_KCMYcm :
    case CUPS_CSPACE_GMCK :
    case CUPS_CSPACE_GMCS :
    case CUPS_CSPACE_WHITE :
    case CUPS_CSPACE_GOLD :
    case CUPS_CSPACE_SILVER :
        memset(row, 0, count);
	break;

    default :
        memset(row, 255, count);
	break;
  }
}


/*
 * 'format_cmy()' - Convert image data to CMY.
 */

static void
format_cmy(imagetoraster_doc_t *doc,
	   cups_page_header2_t *header,	/* I - Page header */
	   unsigned char      *row,	/* IO - Bitmap data for device */
	   int                y,	/* I - Current row */
	   int                z,	/* I - Current plane */
	   int                xsize,	/* I - Width of image data */
	   int	               ysize,	/* I - Height of image data */
	   int                yerr0,	/* I - Top Y error */
	   int                yerr1,	/* I - Bottom Y error */
	   cf_ib_t          *r0,	/* I - Primary image data */
	   cf_ib_t          *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 3;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 64 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize ; x > 0; x --)
              {
	        if (*r0++ > dither[x & 15])
		  *ptr ^= bitmask;
		bitmask >>= 1;

	        if (*r0++ > dither[x & 15])
		  *ptr ^= bitmask;
		bitmask >>= 1;

	        if (*r0++ > dither[x & 15])
		  *ptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 2;
		else
        	{
        	  bitmask = 64;
        	  ptr ++;
        	}
              }
              break;

          case 2 :
	      dither = Floyd8x8[y & 7];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
	       	if ((r0[0] & 63) > dither[x & 7])
        	  *ptr ^= (0x30 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0x30 & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 63) > dither[x & 7])
        	  *ptr ^= (0x0c & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0x0c & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 63) > dither[x & 7])
        	  *ptr++ ^= (0x03 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr++ ^= (0x03 & doc->OffPixels[r0[2]]);
              }
              break;

          case 4 :
	      dither = Floyd4x4[y & 3];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
        	if ((r0[0] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[0]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[2]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[2]]);
              }
              break;

          case 8 :
              for (x = xsize  * 3; x > 0; x --, r0 ++, r1 ++)
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              break;
        }
        break;

    case CUPS_ORDER_BANDED :
	cptr = ptr;
	mptr = ptr + bandwidth;
	yptr = ptr + 2 * bandwidth;

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
        	if (*r0++ > dither[x & 15])
        	  *cptr ^= bitmask;
        	if (*r0++ > dither[x & 15])
        	  *mptr ^= bitmask;
        	if (*r0++ > dither[x & 15])
        	  *yptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (r0[0] == r1[0])
                  *cptr++ = r0[0];
        	else
                  *cptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *mptr++ = r0[1];
        	else
                  *mptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *yptr++ = r0[2];
        	else
                  *yptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_PLANAR :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              switch (z)
	      {
	        case 0 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[0] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;

	        case 1 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[1] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;

	        case 2 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[2] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];
              r0 += z;

              for (x = xsize; x > 0; x --, r0 += 3)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  ptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];
              r0 += z;

              for (x = xsize; x > 0; x --, r0 += 3)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  ptr ++;
        	}
	      }
              break;

          case 8 :
              r0 += z;
	      r1 += z;

              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              }
              break;
        }
        break;
  }
}


/*
 * 'format_cmyk()' - Convert image data to CMYK.
 */

static void
format_cmyk(imagetoraster_doc_t *doc,
	    cups_page_header2_t *header,/* I - Page header */
            unsigned char       *row,	/* IO - Bitmap data for device */
	    int                 y,	/* I - Current row */
	    int                 z,	/* I - Current plane */
	    int                 xsize,	/* I - Width of image data */
	    int	                ysize,	/* I - Height of image data */
	    int                 yerr0,	/* I - Top Y error */
	    int                 yerr1,	/* I - Bottom Y error */
	    cf_ib_t           *r0,	/* I - Primary image data */
	    cf_ib_t           *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		*kptr,			/* Pointer into black */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */
  int		pc, pm, py;		/* CMY pixels */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 4;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 128 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize ; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if (pc && pm && py)
		{
		  bitmask >>= 3;
		  *ptr ^= bitmask;
		}
		else
		{
		  if (pc)
		    *ptr ^= bitmask;
		  bitmask >>= 1;

		  if (pm)
		    *ptr ^= bitmask;
		  bitmask >>= 1;

		  if (py)
		    *ptr ^= bitmask;
		  bitmask >>= 1;
                }

                if (bitmask > 1)
		  bitmask >>= 1;
		else
        	{
        	  bitmask = 128;
        	  ptr ++;
        	}
              }
              break;

          case 2 :
	      dither = Floyd8x8[y & 7];

              for (x = xsize ; x > 0; x --, r0 += 4)
              {
	       	if ((r0[0] & 63) > dither[x & 7])
        	  *ptr ^= (0xc0 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0xc0 & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 63) > dither[x & 7])
        	  *ptr ^= (0x30 & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0x30 & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 63) > dither[x & 7])
        	  *ptr ^= (0x0c & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0x0c & doc->OffPixels[r0[2]]);

        	if ((r0[3] & 63) > dither[x & 7])
        	  *ptr++ ^= (0x03 & doc->OnPixels[r0[3]]);
        	else
        	  *ptr++ ^= (0x03 & doc->OffPixels[r0[3]]);
              }
              break;

          case 4 :
	      dither = Floyd4x4[y & 3];

              for (x = xsize ; x > 0; x --, r0 += 4)
              {
        	if ((r0[0] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[1]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[2]]);

        	if ((r0[3] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[3]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[3]]);
              }
              break;

          case 8 :
              for (x = xsize  * 4; x > 0; x --, r0 ++, r1 ++)
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              break;
        }
        break;

    case CUPS_ORDER_BANDED :
	cptr = ptr;
	mptr = ptr + bandwidth;
	yptr = ptr + 2 * bandwidth;
	kptr = ptr + 3 * bandwidth;

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if (pc && pm && py)
		  *kptr ^= bitmask;
		else
		{
		  if (pc)
        	    *cptr ^= bitmask;
		  if (pm)
        	    *mptr ^= bitmask;
		  if (py)
        	    *yptr ^= bitmask;
                }

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *kptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *kptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *kptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *kptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (r0[0] == r1[0])
                  *cptr++ = r0[0];
        	else
                  *cptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *mptr++ = r0[1];
        	else
                  *mptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *yptr++ = r0[2];
        	else
                  *yptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;

        	if (r0[3] == r1[3])
                  *kptr++ = r0[3];
        	else
                  *kptr++ = (r0[3] * yerr0 + r1[3] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_PLANAR :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if ((pc && pm && py && z == 3) ||
		    (pc && z == 0) || (pm && z == 1) || (py && z == 2))
        	  *ptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  ptr ++;
        	}
	      }
	      break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];
              r0      += z;

              for (x = xsize; x > 0; x --, r0 += 4)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  ptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];
              r0 += z;

              for (x = xsize; x > 0; x --, r0 += 4)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  ptr ++;
        	}
	      }
              break;

          case 8 :
              r0 += z;
	      r1 += z;

              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              }
              break;
        }
        break;
  }
}


/*
 * 'format_K()' - Convert image data to black.
 */

static void
format_K(imagetoraster_doc_t *doc,
	 cups_page_header2_t *header,	/* I - Page header */
         unsigned char       *row,	/* IO - Bitmap data for device */
	 int                 y,		/* I - Current row */
	 int                 z,		/* I - Current plane */
	 int                 xsize,	/* I - Width of image data */
	 int	             ysize,	/* I - Height of image data */
	 int                 yerr0,	/* I - Top Y error */
	 int                 yerr1,	/* I - Bottom Y error */
	 cf_ib_t           *r0,	/* I - Primary image data */
	 cf_ib_t           *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */


  (void)z;

  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr = row + bitoffset / 8;

  switch (header->cupsBitsPerColor)
  {
    case 1 :
        bitmask = 0x80 >> (bitoffset & 7);
        dither  = Floyd16x16[y & 15];

        for (x = xsize; x > 0; x --)
        {
          if (*r0++ > dither[x & 15])
            *ptr ^= bitmask;

          if (bitmask > 1)
	    bitmask >>= 1;
	  else
	  {
	    bitmask = 0x80;
	    ptr ++;
          }
	}
        break;

    case 2 :
        bitmask = 0xc0 >> (bitoffset & 7);
        dither  = Floyd8x8[y & 7];

        for (x = xsize; x > 0; x --)
        {
          if ((*r0 & 63) > dither[x & 7])
            *ptr ^= (bitmask & doc->OnPixels[*r0++]);
          else
            *ptr ^= (bitmask & doc->OffPixels[*r0++]);

          if (bitmask > 3)
	    bitmask >>= 2;
	  else
	  {
	    bitmask = 0xc0;

	    ptr ++;
          }
	}
        break;

    case 4 :
        bitmask = 0xf0 >> (bitoffset & 7);
        dither  = Floyd4x4[y & 3];

        for (x = xsize; x > 0; x --)
        {
          if ((*r0 & 15) > dither[x & 3])
            *ptr ^= (bitmask & doc->OnPixels[*r0++]);
          else
            *ptr ^= (bitmask & doc->OffPixels[*r0++]);

          if (bitmask == 0xf0)
	    bitmask = 0x0f;
	  else
	  {
	    bitmask = 0xf0;

	    ptr ++;
          }
	}
        break;

    case 8 :
        for (x = xsize; x > 0; x --, r0 ++, r1 ++)
	{
          if (*r0 == *r1)
            *ptr++ = *r0;
          else
            *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
        }
        break;
  }
}


/*
 * 'format_kcmy()' - Convert image data to KCMY.
 */

static void
format_kcmy(imagetoraster_doc_t *doc,
	    cups_page_header2_t *header,/* I - Page header */
            unsigned char       *row,	/* IO - Bitmap data for device */
	    int                 y,	/* I - Current row */
	    int                 z,	/* I - Current plane */
	    int                 xsize,	/* I - Width of image data */
	    int	                ysize,	/* I - Height of image data */
	    int                 yerr0,	/* I - Top Y error */
	    int                 yerr1,	/* I - Bottom Y error */
	    cf_ib_t           *r0,	/* I - Primary image data */
	    cf_ib_t           *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		*kptr,			/* Pointer into black */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */
  int		pc, pm, py;		/* CMY pixels */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 4;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 128 >> (bitoffset & 7);
              dither  = Floyd16x16[y & 15];

              for (x = xsize ; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if (pc && pm && py)
		{
		  *ptr ^= bitmask;
		  bitmask >>= 3;
		}
		else
		{
		  bitmask >>= 1;
		  if (pc)
		    *ptr ^= bitmask;

		  bitmask >>= 1;
		  if (pm)
		    *ptr ^= bitmask;

		  bitmask >>= 1;
		  if (py)
		    *ptr ^= bitmask;
                }

                if (bitmask > 1)
		  bitmask >>= 1;
		else
        	{
        	  bitmask = 128;
        	  ptr ++;
        	}
              }
              break;

          case 2 :
              dither = Floyd8x8[y & 7];

              for (x = xsize ; x > 0; x --, r0 += 4)
              {
	       	if ((r0[3] & 63) > dither[x & 7])
        	  *ptr ^= (0xc0 & doc->OnPixels[r0[3]]);
        	else
        	  *ptr ^= (0xc0 & doc->OffPixels[r0[3]]);

        	if ((r0[0] & 63) > dither[x & 7])
        	  *ptr ^= (0x30 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0x30 & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 63) > dither[x & 7])
        	  *ptr ^= (0x0c & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0x0c & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 63) > dither[x & 7])
        	  *ptr++ ^= (0x03 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr++ ^= (0x03 & doc->OffPixels[r0[2]]);
              }
              break;

          case 4 :
              dither = Floyd4x4[y & 3];

              for (x = xsize ; x > 0; x --, r0 += 4)
              {
        	if ((r0[3] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[3]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[3]]);

        	if ((r0[0] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[0]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[2]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[2]]);
              }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (r0[3] == r1[3])
                  *ptr++ = r0[3];
        	else
                  *ptr++ = (r0[3] * yerr0 + r1[3] * yerr1) / ysize;

        	if (r0[0] == r1[0])
                  *ptr++ = r0[0];
        	else
                  *ptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *ptr++ = r0[1];
        	else
                  *ptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *ptr++ = r0[2];
        	else
                  *ptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_BANDED :
	kptr = ptr;
	cptr = ptr + bandwidth;
	mptr = ptr + 2 * bandwidth;
	yptr = ptr + 3 * bandwidth;

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
              dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if (pc && pm && py)
		  *kptr ^= bitmask;
		else
		{
		  if (pc)
        	    *cptr ^= bitmask;
		  if (pm)
        	    *mptr ^= bitmask;
		  if (py)
        	    *yptr ^= bitmask;
                }

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
              dither  = Floyd8x8[y & 7];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *kptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *kptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
              dither  = Floyd4x4[y & 3];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *kptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *kptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (r0[0] == r1[0])
                  *cptr++ = r0[0];
        	else
                  *cptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *mptr++ = r0[1];
        	else
                  *mptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *yptr++ = r0[2];
        	else
                  *yptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;

        	if (r0[3] == r1[3])
                  *kptr++ = r0[3];
        	else
                  *kptr++ = (r0[3] * yerr0 + r1[3] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_PLANAR :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
              dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if ((pc && pm && py && z == 0) ||
		    (pc && z == 1) || (pm && z == 2) || (py && z == 3))
        	  *ptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  ptr ++;
        	}
	      }
	      break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
              dither  = Floyd8x8[y & 7];
              if (z == 0)
	        r0 += 3;
	      else
	        r0 += z - 1;

              for (x = xsize; x > 0; x --, r0 += 4)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  ptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
              dither  = Floyd4x4[y & 3];
              if (z == 0)
	        r0 += 3;
	      else
	        r0 += z - 1;

              for (x = xsize; x > 0; x --, r0 += 4)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  ptr ++;
        	}
	      }
              break;

          case 8 :
              if (z == 0)
	      {
	        r0 += 3;
	        r1 += 3;
	      }
	      else
	      {
	        r0 += z - 1;
	        r1 += z - 1;
	      }

              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              }
              break;
        }
        break;
  }
}


/*
 * 'format_kcmycm()' - Convert image data to KCMYcm.
 */

static void
format_kcmycm(
    imagetoraster_doc_t *doc,
    cups_page_header2_t *header,	/* I - Page header */
    unsigned char       *row,		/* IO - Bitmap data for device */
    int                 y,		/* I - Current row */
    int                 z,		/* I - Current plane */
    int                 xsize,		/* I - Width of image data */
    int                 ysize,		/* I - Height of image data */
    int                 yerr0,		/* I - Top Y error */
    int                 yerr1,		/* I - Bottom Y error */
    cf_ib_t           *r0,		/* I - Primary image data */
    cf_ib_t           *r1)		/* I - Image data for interpolation */
{
  int		pc, pm, py, pk;		/* Cyan, magenta, yellow, and
					   black values */
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		*kptr,			/* Pointer into black */
		*lcptr,			/* Pointer into light cyan */
		*lmptr,			/* Pointer into light magenta */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 6;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        dither = Floyd16x16[y & 15];

        for (x = xsize ; x > 0; x --)
        {
	  pc = *r0++ > dither[x & 15];
	  pm = *r0++ > dither[x & 15];
	  py = *r0++ > dither[x & 15];
	  pk = pc && pm && py;

	  if (pk)
	    *ptr++ ^= 32;	/* Black */
	  else if (pc && pm)
	    *ptr++ ^= 17;	/* Blue (cyan + light magenta) */
	  else if (pc && py)
	    *ptr++ ^= 6;	/* Green (light cyan + yellow) */
	  else if (pm && py)
	    *ptr++ ^= 12;	/* Red (magenta + yellow) */
	  else if (pc)
	    *ptr++ ^= 16;
	  else if (pm)
	    *ptr++ ^= 8;
	  else if (py)
	    *ptr++ ^= 4;
	  else
	    ptr ++;
        }
        break;

    case CUPS_ORDER_BANDED :
	kptr  = ptr;
	cptr  = ptr + bandwidth;
	mptr  = ptr + 2 * bandwidth;
	yptr  = ptr + 3 * bandwidth;
	lcptr = ptr + 4 * bandwidth;
	lmptr = ptr + 5 * bandwidth;

        bitmask = 0x80 >> (bitoffset & 7);
        dither  = Floyd16x16[y & 15];

        for (x = xsize; x > 0; x --)
        {
	  pc = *r0++ > dither[x & 15];
	  pm = *r0++ > dither[x & 15];
	  py = *r0++ > dither[x & 15];
	  pk = pc && pm && py;

	  if (pk)
	    *kptr ^= bitmask;	/* Black */
	  else if (pc && pm)
	  {
	    *cptr ^= bitmask;	/* Blue (cyan + light magenta) */
	    *lmptr ^= bitmask;
	  }
	  else if (pc && py)
	  {
	    *lcptr ^= bitmask;	/* Green (light cyan + yellow) */
	    *yptr  ^= bitmask;
	  }
	  else if (pm && py)
	  {
	    *mptr ^= bitmask;	/* Red (magenta + yellow) */
	    *yptr ^= bitmask;
	  }
	  else if (pc)
	    *cptr ^= bitmask;
	  else if (pm)
	    *mptr ^= bitmask;
	  else if (py)
	    *yptr ^= bitmask;

          if (bitmask > 1)
	    bitmask >>= 1;
	  else
	  {
	    bitmask = 0x80;
	    cptr ++;
	    mptr ++;
	    yptr ++;
	    kptr ++;
	    lcptr ++;
	    lmptr ++;
          }
	}
        break;

    case CUPS_ORDER_PLANAR :
        bitmask = 0x80 >> (bitoffset & 7);
        dither  = Floyd16x16[y & 15];

        for (x = xsize; x > 0; x --)
        {
	  pc = *r0++ > dither[x & 15];
	  pm = *r0++ > dither[x & 15];
	  py = *r0++ > dither[x & 15];
	  pk = pc && pm && py;

          if (pk && z == 0)
            *ptr ^= bitmask;
	  else if (pc && pm && (z == 1 || z == 5))
	    *ptr ^= bitmask;	/* Blue (cyan + light magenta) */
	  else if (pc && py && (z == 3 || z == 4))
	    *ptr ^= bitmask;	/* Green (light cyan + yellow) */
	  else if (pm && py && (z == 2 || z == 3))
	    *ptr ^= bitmask;	/* Red (magenta + yellow) */
	  else if (pc && z == 1)
	    *ptr ^= bitmask;
	  else if (pm && z == 2)
	    *ptr ^= bitmask;
	  else if (py && z == 3)
	    *ptr ^= bitmask;

          if (bitmask > 1)
	    bitmask >>= 1;
	  else
	  {
	    bitmask = 0x80;
	    ptr ++;
          }
	}
        break;
  }
}


/*
 * 'format_rgba()' - Convert image data to RGBA/RGBW.
 */

static void
format_rgba(imagetoraster_doc_t *doc,
	    cups_page_header2_t *header,/* I - Page header */
            unsigned char       *row,	/* IO - Bitmap data for device */
	    int                 y,	/* I - Current row */
	    int                 z,	/* I - Current plane */
	    int                 xsize,	/* I - Width of image data */
	    int	                ysize,	/* I - Height of image data */
	    int                 yerr0,	/* I - Top Y error */
	    int                 yerr1,	/* I - Bottom Y error */
	    cf_ib_t           *r0,	/* I - Primary image data */
	    cf_ib_t           *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 4;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 128 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize ; x > 0; x --)
              {
	        if (*r0++ > dither[x & 15])
		  *ptr ^= bitmask;
		bitmask >>= 1;

	        if (*r0++ > dither[x & 15])
		  *ptr ^= bitmask;
		bitmask >>= 1;

	        if (*r0++ > dither[x & 15])
		  *ptr ^= bitmask;

                if (bitmask > 2)
		  bitmask >>= 2;
		else
        	{
        	  bitmask = 128;
        	  ptr ++;
        	}
              }
              break;

          case 2 :
	      dither = Floyd8x8[y & 7];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
	       	if ((r0[0] & 63) > dither[x & 7])
        	  *ptr ^= (0xc0 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0xc0 & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 63) > dither[x & 7])
        	  *ptr ^= (0x30 & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0x30 & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 63) > dither[x & 7])
        	  *ptr ^= (0x0c & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0x0c & doc->OffPixels[r0[2]]);

                ptr ++;
              }
              break;

          case 4 :
	      dither = Floyd4x4[y & 3];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
        	if ((r0[0] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[0]]);

        	if ((r0[1] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[1]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[1]]);

        	if ((r0[2] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[2]]);

                ptr ++;
              }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (r0[0] == r1[0])
                  *ptr++ = r0[0];
        	else
                  *ptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *ptr++ = r0[1];
        	else
                  *ptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *ptr++ = r0[2];
        	else
                  *ptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;

                ptr ++;
              }
	      break;
        }
        break;

    case CUPS_ORDER_BANDED :
	cptr = ptr;
	mptr = ptr + bandwidth;
	yptr = ptr + 2 * bandwidth;

        memset(ptr + 3 * bandwidth, 255, bandwidth);

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
        	if (*r0++ > dither[x & 15])
        	  *cptr ^= bitmask;
        	if (*r0++ > dither[x & 15])
        	  *mptr ^= bitmask;
        	if (*r0++ > dither[x & 15])
        	  *yptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (r0[0] == r1[0])
                  *cptr++ = r0[0];
        	else
                  *cptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *mptr++ = r0[1];
        	else
                  *mptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *yptr++ = r0[2];
        	else
                  *yptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_PLANAR :
        if (z == 3)
	{
          memset(row, 255, header->cupsBytesPerLine);
	  break;
        }

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              switch (z)
	      {
	        case 0 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[0] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;

	        case 1 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[1] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;

	        case 2 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[2] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];
              r0 += z;

              for (x = xsize; x > 0; x --, r0 += 3)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  ptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];
              r0 += z;

              for (x = xsize; x > 0; x --, r0 += 3)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  ptr ++;
        	}
	      }
              break;

          case 8 :
              r0 += z;
	      r1 += z;

              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              }
              break;
        }
        break;
  }
}


/*
 * 'format_w()' - Convert image data to luminance.
 */

static void
format_w(imagetoraster_doc_t *doc,
	 cups_page_header2_t *header,	/* I - Page header */
	 unsigned char    *row,	/* IO - Bitmap data for device */
	 int              y,		/* I - Current row */
	 int              z,		/* I - Current plane */
	 int              xsize,	/* I - Width of image data */
	 int	             ysize,	/* I - Height of image data */
	 int              yerr0,	/* I - Top Y error */
	 int              yerr1,	/* I - Bottom Y error */
	 cf_ib_t        *r0,	/* I - Primary image data */
	 cf_ib_t        *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */


  (void)z;

  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr = row + bitoffset / 8;

  switch (header->cupsBitsPerColor)
  {
    case 1 :
        bitmask = 0x80 >> (bitoffset & 7);
        dither  = Floyd16x16[y & 15];

        for (x = xsize; x > 0; x --)
        {
          if (*r0++ > dither[x & 15])
            *ptr ^= bitmask;

          if (bitmask > 1)
	    bitmask >>= 1;
	  else
	  {
	    bitmask = 0x80;
	    ptr ++;
          }
	}
        break;

    case 2 :
        bitmask = 0xc0 >> (bitoffset & 7);
        dither  = Floyd8x8[y & 7];

        for (x = xsize; x > 0; x --)
        {
          if ((*r0 & 63) > dither[x & 7])
            *ptr ^= (bitmask & doc->OnPixels[*r0++]);
          else
            *ptr ^= (bitmask & doc->OffPixels[*r0++]);

          if (bitmask > 3)
	    bitmask >>= 2;
	  else
	  {
	    bitmask = 0xc0;

	    ptr ++;
          }
	}
        break;

    case 4 :
        bitmask = 0xf0 >> (bitoffset & 7);
        dither  = Floyd4x4[y & 3];

        for (x = xsize; x > 0; x --)
        {
          if ((*r0 & 15) > dither[x & 3])
            *ptr ^= (bitmask & doc->OnPixels[*r0++]);
          else
            *ptr ^= (bitmask & doc->OffPixels[*r0++]);

          if (bitmask == 0xf0)
	    bitmask = 0x0f;
	  else
	  {
	    bitmask = 0xf0;

	    ptr ++;
          }
	}
        break;

    case 8 :
        for (x = xsize; x > 0; x --, r0 ++, r1 ++)
	{
          if (*r0 == *r1)
            *ptr++ = *r0;
          else
            *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
        }
        break;
  }
}


/*
 * 'format_ymc()' - Convert image data to YMC.
 */

static void
format_ymc(imagetoraster_doc_t *doc,
	   cups_page_header2_t *header,	/* I - Page header */
	   unsigned char      *row,	/* IO - Bitmap data for device */
	   int                y,	/* I - Current row */
	   int                z,	/* I - Current plane */
	   int                xsize,	/* I - Width of image data */
	   int	               ysize,	/* I - Height of image data */
	   int                yerr0,	/* I - Top Y error */
	   int                yerr1,	/* I - Bottom Y error */
	   cf_ib_t          *r0,	/* I - Primary image data */
	   cf_ib_t          *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 3;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 64 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
	        if (r0[2] > dither[x & 15])
		  *ptr ^= bitmask;
		bitmask >>= 1;

	        if (r0[1] > dither[x & 15])
		  *ptr ^= bitmask;
		bitmask >>= 1;

	        if (r0[0] > dither[x & 15])
		  *ptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 2;
		else
        	{
        	  bitmask = 64;
        	  ptr ++;
        	}
              }
              break;

          case 2 :
	      dither = Floyd8x8[y & 7];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
	       	if ((r0[2] & 63) > dither[x & 7])
        	  *ptr ^= (0x30 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0x30 & doc->OffPixels[r0[2]]);

        	if ((r0[1] & 63) > dither[x & 7])
        	  *ptr ^= (0x0c & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0x0c & doc->OffPixels[r0[1]]);

        	if ((r0[0] & 63) > dither[x & 7])
        	  *ptr++ ^= (0x03 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr++ ^= (0x03 & doc->OffPixels[r0[0]]);
              }
              break;

          case 4 :
	      dither = Floyd4x4[y & 3];

              for (x = xsize ; x > 0; x --, r0 += 3)
              {
        	if ((r0[2] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[2]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[2]]);

        	if ((r0[1] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[1]]);

        	if ((r0[0] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[0]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[0]]);
              }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (r0[2] == r1[2])
                  *ptr++ = r0[2];
        	else
                  *ptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *ptr++ = r0[1];
        	else
                  *ptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[0] == r1[0])
                  *ptr++ = r0[0];
        	else
                  *ptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;
              }
	      break;
        }
        break;

    case CUPS_ORDER_BANDED :
	yptr = ptr;
	mptr = ptr + bandwidth;
	cptr = ptr + 2 * bandwidth;

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
        	if (*r0++ > dither[x & 15])
        	  *cptr ^= bitmask;
        	if (*r0++ > dither[x & 15])
        	  *mptr ^= bitmask;
        	if (*r0++ > dither[x & 15])
        	  *yptr ^= bitmask;

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
        	}
	      }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (r0[0] == r1[0])
                  *cptr++ = r0[0];
        	else
                  *cptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *mptr++ = r0[1];
        	else
                  *mptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *yptr++ = r0[2];
        	else
                  *yptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_PLANAR :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
	      dither  = Floyd16x16[y & 15];

              switch (z)
	      {
	        case 2 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[0] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;

	        case 1 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[1] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;

	        case 0 :
        	    for (x = xsize; x > 0; x --, r0 += 3)
        	    {
        	      if (r0[2] > dither[x & 15])
        		*ptr ^= bitmask;

                      if (bitmask > 1)
			bitmask >>= 1;
		      else
		      {
			bitmask = 0x80;
			ptr ++;
        	      }
	            }
		    break;
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
	      dither  = Floyd8x8[y & 7];
              z       = 2 - z;
              r0      += z;

              for (x = xsize; x > 0; x --, r0 += 3)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  ptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
	      dither  = Floyd4x4[y & 3];
              z       = 2 - z;
              r0      += z;

              for (x = xsize; x > 0; x --, r0 += 3)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  ptr ++;
        	}
	      }
              break;

          case 8 :
              z  = 2 - z;
              r0 += z;
	      r1 += z;

              for (x = xsize; x > 0; x --, r0 += 3, r1 += 3)
	      {
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              }
              break;
        }
        break;
  }
}


/*
 * 'format_ymck()' - Convert image data to YMCK.
 */

static void
format_ymck(imagetoraster_doc_t *doc,
	    cups_page_header2_t *header,/* I - Page header */
            unsigned char       *row,	/* IO - Bitmap data for device */
	    int                 y,	/* I - Current row */
	    int                 z,	/* I - Current plane */
	    int                 xsize,	/* I - Width of image data */
	    int	                ysize,	/* I - Height of image data */
	    int                 yerr0,	/* I - Top Y error */
	    int                 yerr1,	/* I - Bottom Y error */
	    cf_ib_t           *r0,	/* I - Primary image data */
	    cf_ib_t           *r1)	/* I - Image data for interpolation */
{
  cf_ib_t	*ptr,			/* Pointer into row */
		*cptr,			/* Pointer into cyan */
		*mptr,			/* Pointer into magenta */
		*yptr,			/* Pointer into yellow */
		*kptr,			/* Pointer into black */
		bitmask;		/* Current mask for pixel */
  int		bitoffset;		/* Current offset in line */
  int		bandwidth;		/* Width of a color band */
  int		x,			/* Current X coordinate on page */
		*dither;		/* Pointer into dither array */
  int		pc, pm, py;		/* CMY pixels */


  switch (doc->XPosition)
  {
    case -1 :
        bitoffset = 0;
	break;
    default :
        bitoffset = header->cupsBitsPerPixel *
	  ((header->cupsWidth - xsize) / 2);
	break;
    case 1 :
        bitoffset = header->cupsBitsPerPixel * (header->cupsWidth - xsize);
	break;
  }

  ptr       = row + bitoffset / 8;
  bandwidth = header->cupsBytesPerLine / 4;

  switch (header->cupsColorOrder)
  {
    case CUPS_ORDER_CHUNKED :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 128 >> (bitoffset & 7);
              dither  = Floyd16x16[y & 15];

              for (x = xsize ; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if (pc && pm && py)
		{
		  bitmask >>= 3;
		  *ptr ^= bitmask;
		}
		else
		{
		  if (py)
		    *ptr ^= bitmask;
		  bitmask >>= 1;

		  if (pm)
		    *ptr ^= bitmask;
		  bitmask >>= 1;

		  if (pc)
		    *ptr ^= bitmask;
		  bitmask >>= 1;
                }

                if (bitmask > 1)
		  bitmask >>= 1;
		else
        	{
        	  bitmask = 128;

        	  ptr ++;
        	}
              }
              break;

          case 2 :
              dither = Floyd8x8[y & 7];

              for (x = xsize ; x > 0; x --, r0 += 4)
              {
	       	if ((r0[2] & 63) > dither[x & 7])
        	  *ptr ^= (0xc0 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0xc0 & doc->OffPixels[r0[2]]);

        	if ((r0[1] & 63) > dither[x & 7])
        	  *ptr ^= (0x30 & doc->OnPixels[r0[1]]);
        	else
        	  *ptr ^= (0x30 & doc->OffPixels[r0[1]]);

        	if ((r0[0] & 63) > dither[x & 7])
        	  *ptr ^= (0x0c & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0x0c & doc->OffPixels[r0[0]]);

        	if ((r0[3] & 63) > dither[x & 7])
        	  *ptr++ ^= (0x03 & doc->OnPixels[r0[3]]);
        	else
        	  *ptr++ ^= (0x03 & doc->OffPixels[r0[3]]);
              }
              break;

          case 4 :
              dither = Floyd4x4[y & 3];

              for (x = xsize ; x > 0; x --, r0 += 4)
              {
        	if ((r0[2] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[2]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[2]]);

        	if ((r0[1] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[1]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[1]]);

        	if ((r0[0] & 15) > dither[x & 3])
        	  *ptr ^= (0xf0 & doc->OnPixels[r0[0]]);
        	else
        	  *ptr ^= (0xf0 & doc->OffPixels[r0[0]]);

        	if ((r0[3] & 15) > dither[x & 3])
        	  *ptr++ ^= (0x0f & doc->OnPixels[r0[3]]);
        	else
        	  *ptr++ ^= (0x0f & doc->OffPixels[r0[3]]);
              }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (r0[2] == r1[2])
                  *ptr++ = r0[2];
        	else
                  *ptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *ptr++ = r0[1];
        	else
                  *ptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[0] == r1[0])
                  *ptr++ = r0[0];
        	else
                  *ptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[3] == r1[3])
                  *ptr++ = r0[3];
        	else
                  *ptr++ = (r0[3] * yerr0 + r1[3] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_BANDED :
	yptr = ptr;
	mptr = ptr + bandwidth;
	cptr = ptr + 2 * bandwidth;
	kptr = ptr + 3 * bandwidth;

        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
              dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if (pc && pm && py)
		  *kptr ^= bitmask;
		else
		{
		  if (pc)
        	    *cptr ^= bitmask;
		  if (pm)
        	    *mptr ^= bitmask;
		  if (py)
        	    *yptr ^= bitmask;
                }

                if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
              dither  = Floyd8x8[y & 7];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 63) > dither[x & 7])
        	  *kptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *kptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
              dither  = Floyd4x4[y & 3];

              for (x = xsize; x > 0; x --)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *cptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *cptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *mptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *mptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *yptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *yptr ^= (bitmask & doc->OffPixels[*r0++]);

        	if ((*r0 & 15) > dither[x & 3])
        	  *kptr ^= (bitmask & doc->OnPixels[*r0++]);
        	else
        	  *kptr ^= (bitmask & doc->OffPixels[*r0++]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  cptr ++;
		  mptr ++;
		  yptr ++;
		  kptr ++;
        	}
	      }
              break;

          case 8 :
              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (r0[0] == r1[0])
                  *cptr++ = r0[0];
        	else
                  *cptr++ = (r0[0] * yerr0 + r1[0] * yerr1) / ysize;

        	if (r0[1] == r1[1])
                  *mptr++ = r0[1];
        	else
                  *mptr++ = (r0[1] * yerr0 + r1[1] * yerr1) / ysize;

        	if (r0[2] == r1[2])
                  *yptr++ = r0[2];
        	else
                  *yptr++ = (r0[2] * yerr0 + r1[2] * yerr1) / ysize;

        	if (r0[3] == r1[3])
                  *kptr++ = r0[3];
        	else
                  *kptr++ = (r0[3] * yerr0 + r1[3] * yerr1) / ysize;
              }
              break;
        }
        break;

    case CUPS_ORDER_PLANAR :
        switch (header->cupsBitsPerColor)
        {
          case 1 :
              bitmask = 0x80 >> (bitoffset & 7);
              dither  = Floyd16x16[y & 15];

              for (x = xsize; x > 0; x --)
              {
	        pc = *r0++ > dither[x & 15];
		pm = *r0++ > dither[x & 15];
		py = *r0++ > dither[x & 15];

		if ((pc && pm && py && z == 3) ||
		    (pc && z == 2) || (pm && z == 1) || (py && z == 0))
        	  *ptr ^= bitmask;

        	if (bitmask > 1)
		  bitmask >>= 1;
		else
		{
		  bitmask = 0x80;
		  ptr ++;
        	}
	      }
              break;

          case 2 :
              bitmask = 0xc0 >> (bitoffset & 7);
              dither  = Floyd8x8[y & 7];
              if (z == 3)
	        r0 += 3;
	      else
	        r0 += 2 - z;

              for (x = xsize; x > 0; x --, r0 += 4)
              {
        	if ((*r0 & 63) > dither[x & 7])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask > 3)
		  bitmask >>= 2;
		else
		{
		  bitmask = 0xc0;

		  ptr ++;
        	}
	      }
              break;

          case 4 :
              bitmask = 0xf0 >> (bitoffset & 7);
              dither  = Floyd4x4[y & 3];
              if (z == 3)
	        r0 += 3;
	      else
	        r0 += 2 - z;

              for (x = xsize; x > 0; x --, r0 += 4)
              {
        	if ((*r0 & 15) > dither[x & 3])
        	  *ptr ^= (bitmask & doc->OnPixels[*r0]);
        	else
        	  *ptr ^= (bitmask & doc->OffPixels[*r0]);

                if (bitmask == 0xf0)
		  bitmask = 0x0f;
		else
		{
		  bitmask = 0xf0;

		  ptr ++;
        	}
	      }
              break;

          case 8 :
              if (z == 3)
	      {
	        r0 += 3;
	        r1 += 3;
	      }
	      else
	      {
	        r0 += 2 - z;
	        r1 += 2 - z;
	      }

              for (x = xsize; x > 0; x --, r0 += 4, r1 += 4)
	      {
        	if (*r0 == *r1)
                  *ptr++ = *r0;
        	else
                  *ptr++ = (*r0 * yerr0 + *r1 * yerr1) / ysize;
              }
              break;
        }
        break;
  }
}


/*
 * 'make_lut()' - Make a lookup table given gamma and brightness values.
 */

static void
make_lut(cf_ib_t  *lut,		/* I - Lookup table */
	 int        colorspace,		/* I - Colorspace */
         float      g,			/* I - Image gamma */
         float      b)			/* I - Image brightness */
{
  int	i;				/* Looping var */
  int	v;				/* Current value */


  g = 1.0 / g;
  b = 1.0 / b;

  for (i = 0; i < 256; i ++)
  {
    if (colorspace < 0)
      v = 255.0 * b * (1.0 - pow(1.0 - (float)i / 255.0, g)) + 0.5;
    else
      v = 255.0 * (1.0 - b * (1.0 - pow((float)i / 255.0, g))) + 0.5;

    if (v < 0)
      *lut++ = 0;
    else if (v > 255)
      *lut++ = 255;
    else
      *lut++ = v;
  }
}

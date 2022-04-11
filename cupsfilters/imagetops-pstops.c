/*
 * PostScript filter function and image file to PostScript filter function
 * for cups-filters.
 *
 * Copyright © 2020 by Till Kamppeter
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1993-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
#include <cupsfilters/image.h>
#include <cupsfilters/raster.h>
#include <cupsfilters/image-private.h>
#include <ppd/ppd.h>
#include <cups/file.h>
#include <cups/array.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>


/*
 * Constants...
 */

#define PSTOPS_BORDERNONE	0	/* No border or hairline border */
#define PSTOPS_BORDERTHICK	1	/* Think border */
#define PSTOPS_BORDERSINGLE	2	/* Single-line hairline border */
#define PSTOPS_BORDERSINGLE2	3	/* Single-line thick border */
#define PSTOPS_BORDERDOUBLE	4	/* Double-line hairline border */
#define PSTOPS_BORDERDOUBLE2	5	/* Double-line thick border */

#define PSTOPS_LAYOUT_LRBT	0	/* Left to right, bottom to top */
#define PSTOPS_LAYOUT_LRTB	1	/* Left to right, top to bottom */
#define PSTOPS_LAYOUT_RLBT	2	/* Right to left, bottom to top */
#define PSTOPS_LAYOUT_RLTB	3	/* Right to left, top to bottom */
#define PSTOPS_LAYOUT_BTLR	4	/* Bottom to top, left to right */
#define PSTOPS_LAYOUT_TBLR	5	/* Top to bottom, left to right */
#define PSTOPS_LAYOUT_BTRL	6	/* Bottom to top, right to left */
#define PSTOPS_LAYOUT_TBRL	7	/* Top to bottom, right to left */

#define PSTOPS_LAYOUT_NEGATEY	1	/* The bits for the layout */
#define PSTOPS_LAYOUT_NEGATEX	2	/* definitions above... */
#define PSTOPS_LAYOUT_VERTICAL	4


/*
 * Types...
 */

typedef struct				/**** Page information ****/
{
  char		*label;			/* Page label */
  int		bounding_box[4];	/* PageBoundingBox */
  off_t		offset;			/* Offset to start of page */
  ssize_t	length;			/* Number of bytes for page */
  int		num_options;		/* Number of options for this page */
  cups_option_t	*options;		/* Options for this page */
} pstops_page_t;

typedef struct				/**** Document information ****/
{
  int		page;			/* Current page */
  int		bounding_box[4];	/* BoundingBox from header */
  int		new_bounding_box[4];	/* New composite bounding box */
  int		num_options;		/* Number of document-wide options */
  cups_option_t	*options;		/* Document-wide options */
  int		normal_landscape,	/* Normal rotation for landscape? */
		saw_eof,		/* Saw the %%EOF comment? */
		slow_collate,		/* Collate copies by hand? */
		slow_duplex,		/* Duplex pages slowly? */
		slow_order,		/* Reverse pages slowly? */
		use_ESPshowpage;	/* Use ESPshowpage? */
  cups_array_t	*pages;			/* Pages in document */
  cups_file_t	*temp;			/* Temporary file, if any */
  char		tempfile[1024];		/* Temporary filename */
  int		job_id;			/* Job ID */
  const char	*user,			/* User name */
		*title;			/* Job name */
  int		copies;			/* Number of copies */
  const char	*ap_input_slot,		/* AP_FIRSTPAGE_InputSlot value */
		*ap_manual_feed,	/* AP_FIRSTPAGE_ManualFeed value */
		*ap_media_color,	/* AP_FIRSTPAGE_MediaColor value */
		*ap_media_type,		/* AP_FIRSTPAGE_MediaType value */
		*ap_page_region,	/* AP_FIRSTPAGE_PageRegion value */
		*ap_page_size;		/* AP_FIRSTPAGE_PageSize value */
  int		collate,		/* Collate copies? */
		emit_jcl,		/* Emit JCL commands? */
		fit_to_page;		/* Fit pages to media */
  const char	*input_slot,		/* InputSlot value */
		*manual_feed,		/* ManualFeed value */
		*media_color,		/* MediaColor value */
		*media_type,		/* MediaType value */
		*page_region,		/* PageRegion value */
		*page_size;		/* PageSize value */
  int		mirror,			/* doc->mirror/mirror pages */
		number_up,		/* Number of pages on each sheet */
		number_up_layout,	/* doc->number_up_layout of N-up
					   pages */
		output_order,		/* Requested reverse output order? */
		page_border;		/* doc->page_border around pages */
  const char	*page_label,		/* page-label option, if any */
		*page_ranges,		/* page-ranges option, if any */
    *inputPageRange, /* input-page-ranges option, if any */
		*page_set;		/* page-set option, if any */
  /* Basic settings from PPD defaults/options, global vars of original pstops */
  int           Orientation,            /* 0 = portrait, 1 = landscape, etc. */
                Duplex,                 /* Duplexed? */
                LanguageLevel,          /* PS Language level of printer */
                Color;                  /* Color printer? */
  float         PageLeft,               /* Left margin */
                PageRight,              /* Right margin */
                PageBottom,             /* Bottom margin */
                PageTop,                /* Top margin */
                PageWidth,              /* Total page width */
                PageLength;             /* Total page length */
  cups_file_t	*inputfp;		/* Temporary file, if any */
  FILE		*outputfp;		/* Temporary file, if any */
  cf_logfunc_t logfunc;             /* Logging function, NULL for no
					   logging */
  void          *logdata;               /* User data for logging function, can
					   be NULL */
  cf_filter_iscanceledfunc_t iscanceledfunc; /* Function returning 1 when
                                           job is canceled, NULL for not
                                           supporting stop on cancel */
  void *iscanceleddata;                 /* User data for is-canceled function,
					   can be NULL */
} pstops_doc_t;


/*
 * Convenience macros...
 */

#define	is_first_page(p)	(doc->number_up == 1 || \
				 ((p) % doc->number_up) == 1)
#define	is_last_page(p)		(doc->number_up == 1 || \
				 ((p) % doc->number_up) == 0)
#define is_not_last_page(p)	(doc->number_up > 1 && \
				 ((p) % doc->number_up) != 0)


/*
 * Local functions...
 */

static pstops_page_t	*add_page(pstops_doc_t *doc, const char *label);
static int		check_range(int page ,const char *Ranges,const char *pageset);
static void		copy_bytes(pstops_doc_t *doc,
				   off_t offset, size_t length);
static ssize_t		copy_comments(pstops_doc_t *doc,
			              ppd_file_t *ppd, char *line,
				      ssize_t linelen, size_t linesize);
static void		copy_dsc(pstops_doc_t *doc,
			         ppd_file_t *ppd, char *line, ssize_t linelen,
				 size_t linesize);
static void		copy_non_dsc(pstops_doc_t *doc,
			             ppd_file_t *ppd, char *line,
				     ssize_t linelen, size_t linesize);
static ssize_t		copy_page(pstops_doc_t *doc,
			          ppd_file_t *ppd, int number, char *line,
				  ssize_t linelen, size_t linesize);
static ssize_t		copy_prolog(pstops_doc_t *doc,
			            ppd_file_t *ppd, char *line,
				    ssize_t linelen, size_t linesize);
static ssize_t		copy_setup(pstops_doc_t *doc,
			           ppd_file_t *ppd, char *line,
				   ssize_t linelen, size_t linesize);
static ssize_t		copy_trailer(pstops_doc_t *doc,
			             ppd_file_t *ppd, int number, char *line,
				     ssize_t linelen, size_t linesize);
static void		do_prolog(pstops_doc_t *doc, ppd_file_t *ppd);
static void 		do_setup(pstops_doc_t *doc, ppd_file_t *ppd);
static void		doc_printf(pstops_doc_t *doc, const char *format, ...);
static void		doc_putc(pstops_doc_t *doc, const char c);
static void		doc_puts(pstops_doc_t *doc, const char *s);
static void		doc_write(pstops_doc_t *doc, const char *s, size_t len);
static void		end_nup(pstops_doc_t *doc, int number);
static int		include_feature(pstops_doc_t *doc, ppd_file_t *ppd,
					const char *line, int num_options,
					cups_option_t **options);
static char		*parse_text(const char *start, char **end, char *buffer,
			            size_t bufsize);
static void		ps_hex(pstops_doc_t *doc, cf_ib_t *data, int length,
			       int last_line);
static void		ps_ascii85(pstops_doc_t *doc, cf_ib_t *data, int length,
				   int last_line);
static int		set_pstops_options(pstops_doc_t *doc, ppd_file_t *ppd,
			                   int job_id, char *job_user,
					   char *job_title, int copies,
					   int num_options,
					   cups_option_t *options,
					   cf_logfunc_t logfunc,
					   void *logdata,
					   cf_filter_iscanceledfunc_t
					   iscanceledfunc,
					   void *iscanceleddata);
static ssize_t		skip_page(pstops_doc_t *doc,
				  char *line, ssize_t linelen, size_t linesize);
static void		start_nup(pstops_doc_t *doc, int number,
				  int show_border, const int *bounding_box);
static void		write_common(pstops_doc_t *doc);
static void		write_label_prolog(pstops_doc_t *doc, const char *label,
			                   float bottom, float top,
					   float width);
static void		write_labels(pstops_doc_t *doc, int orient);
static void		write_labels_outputfile_only(pstops_doc_t *doc, int orient);
static void		write_options(pstops_doc_t  *doc, ppd_file_t *ppd,
			              int num_options, cups_option_t *options);
static void		write_text_comment(pstops_doc_t *doc,
					   const char *name, const char *value);

/*
 * 'cfFilterPSToPS()' - Filter function to insert PostScript code from
 *              PPD file (PostScript printer driver) into a 
 *              PostScript data stream
 */

int                         /* O - Error status */
cfFilterPSToPS(int inputfd,         /* I - File descriptor input stream */
       int outputfd,        /* I - File descriptor output stream */
       int inputseekable,   /* I - Is input stream seekable? (unused) */
       cf_filter_data_t *data, /* I - Job and printer data */
       void *parameters)    /* I - Filter-specific parameters (unused) */
{
  pstops_doc_t	doc;			/* Document information */
  cups_file_t	*inputfp,*fp;		/* Print file */
  FILE          *outputfp;              /* Output data stream */
  char		line[8192],
          buffer[8192];		/* Line buffers */
  ssize_t	len,bytes;			/* Length of line buffers */
  pstops_page_t *pageinfo;
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;
   int proc_pipe[2];
   int pstops_pid = 0; 
   int childStatus;
   int status = 0;

  (void)inputseekable;
  (void)parameters;

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

/*
  * Process job options...
  */

  if (set_pstops_options(&doc, data->ppd, data->job_id, data->job_user,
			 data->job_title, data->copies,
			 data->num_options, data->options,
			 log, ld, iscanceled, icd) == 1)
  {
    close(inputfd);
    close(outputfd);

    return (1);
  }
  
   if(doc.inputPageRange)
 {   
     if(pipe(proc_pipe))
     {
        if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterPSToPS: Unable to create pipe for input-page-ranges");
		   return (1);
     }
    if ((pstops_pid = fork()) == 0)
    {  
     close(proc_pipe[0]);

      if ((fp = cupsFileOpenFd(inputfd, "r")) == NULL)
     {
        if (!iscanceled || !iscanceled(icd))
        {
        if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPSToPS: Unable to open input data stream.");
        }

     exit (1);
    }

    bytes = cupsFileGetLine(fp,buffer,sizeof(buffer));

      while (strncmp(buffer, "%%Page:", 7) && strncmp(buffer, "%%Trailer", 9))
    {
      bytes = write(proc_pipe[1], buffer, bytes);
      if ((bytes = cupsFileGetLine(fp, buffer, sizeof(buffer))) == 0)
        break;
    }

    int input_page_number=0;

    while (!strncmp(buffer, "%%Page:", 7))
    {
      input_page_number ++;

      if(check_range(input_page_number,doc.inputPageRange,NULL))
      {
          bytes = write(proc_pipe[1], buffer, bytes);
          bytes = cupsFileGetLine(fp,buffer,sizeof(buffer));
        while(strncmp(buffer, "%%Page:", 7) && strncmp(buffer, "%%Trailer", 9))
        { bytes = write(proc_pipe[1], buffer, bytes);
          bytes = cupsFileGetLine(fp,buffer,sizeof(buffer));
        }
      }
      else
      {
        bytes = cupsFileGetLine(fp,buffer,sizeof(buffer)); 
        while(strncmp(buffer, "%%Page:", 7) && strncmp(buffer, "%%Trailer", 9))
        {
          bytes = cupsFileGetLine(fp,buffer,sizeof(buffer)); 
        }
      }
    }
    while (bytes)
    {
        bytes = write(proc_pipe[1], buffer, bytes);
        bytes= cupsFileGetLine(fp,buffer,sizeof(buffer));
    }
    close(proc_pipe[1]);
    cupsFileClose(fp);
    exit(0);
   }
   else
   {
     close(proc_pipe[1]);
     close(inputfd);
     inputfd=proc_pipe[0];
   }
  }

 /*
  * Open the input data stream specified by the inputfd...
  */
  if ((inputfp = cupsFileOpenFd(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Open the output data stream specified by the outputfd...
  */

  if ((outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: Unable to open output data stream.");
    }

    cupsFileClose(inputfp);

    return (1);
  }

 /*
  * Read the first line to see if we have DSC comments...
  */

  if ((len = (ssize_t)cupsFileGetLine(inputfp, line, sizeof(line))) == 0)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPSToPS: The print file is empty.");
    /* Do not treat this an error, if a previous filter eliminated all
       pages the job should get dequeued without anything printed. */
    return (0);
  }

  doc.inputfp = inputfp;
  doc.outputfp = outputfp;
  
 /*
  * Write any "exit server" options that have been selected...
  */

  ppdEmit(data->ppd, outputfp, PPD_ORDER_EXIT);

 /*
  * Write any JCL commands that are needed to print PostScript code...
  */

  if (doc.emit_jcl)
    ppdEmitJCL(data->ppd, outputfp, doc.job_id, doc.user, doc.title);

 /*
  * Start with a DSC header...
  */

  doc_puts(&doc, "%!PS-Adobe-3.0\n");

 /*
  * Skip leading PJL in the document...
  */

  while (!strncmp(line, "\033%-12345X", 9) || !strncmp(line, "@PJL ", 5))
  {
   /*
    * Yup, we have leading PJL fun, so skip it until we hit the line
    * with "ENTER LANGUAGE"...
    */

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPSToPS: Skipping PJL header...");

    while (strstr(line, "ENTER LANGUAGE") == NULL && strncmp(line, "%!", 2))
      if ((len = (ssize_t)cupsFileGetLine(inputfp, line, sizeof(line))) == 0)
        break;

    if (!strncmp(line, "%!", 2))
      break;

    if ((len = (ssize_t)cupsFileGetLine(inputfp, line, sizeof(line))) == 0)
      break;
  }

 /*
  * Now see if the document conforms to the Adobe Document Structuring
  * Conventions...
  */

  if (!strncmp(line, "%!PS-Adobe-", 11))
  {
   /*
    * Yes, filter the document...
    */

    copy_dsc(&doc, data->ppd, line, len, sizeof(line));
  }
  else
  {
   /*
    * No, display an error message and treat the file as if it contains
    * a single page...
    */

    copy_non_dsc(&doc, data->ppd, line, len, sizeof(line));
  }

 /*
  * Send %%EOF as needed...
  */

  if (!doc.saw_eof)
    doc_puts(&doc, "%%EOF\n");

 /*
  * End the job with the appropriate JCL command or CTRL-D...
  */

  if (doc.emit_jcl)
  {
    if (data->ppd && data->ppd->jcl_end)
      ppdEmitJCLEnd(data->ppd, doc.outputfp);
    else
      doc_putc(&doc, 0x04);
  }

 /*
  * Close files and remove the temporary file if needed...
  */

  if (doc.temp)
  {
    cupsFileClose(doc.temp);
    unlink(doc.tempfile);
  }

  if (doc.pages)
  {
    for (pageinfo = (pstops_page_t *)cupsArrayFirst(doc.pages);
         pageinfo; pageinfo = (pstops_page_t *)cupsArrayNext(doc.pages))
    {
      if (pageinfo->label)
	free(pageinfo->label);
      if (pageinfo->num_options && pageinfo->options)
	cupsFreeOptions(pageinfo->num_options, pageinfo->options);
      free(pageinfo);
    }
    cupsArrayDelete(doc.pages);
    doc.pages = NULL;
  }

  if(doc.inputPageRange)
    { 
      retry_wait:
        if (waitpid (pstops_pid, &childStatus, 0) == -1) {
          if (errno == EINTR)
            goto retry_wait;
          if (log) log(ld, CF_LOGLEVEL_ERROR,
          "cfFilterPSToPS: Error while waiting for input_page_ranges to finish - %s.",
          strerror(errno));
        }
        /* How did the sub-process terminate */
    if (childStatus) {
      if (WIFEXITED(childStatus)) {
	/* Via exit() anywhere or return() in the main() function */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterPSToPS: input-page-ranges filter (PID %d) stopped with status %d",
		     pstops_pid, WEXITSTATUS(childStatus));
      } else {
	/* Via signal */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterPSToPS: imput-page-ranges filter (PID %d) crashed on signal %d",
		     pstops_pid, WTERMSIG(childStatus));
      }
      status=1;
    } else {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: input-page-ranges-filter (PID %d) exited with no errors.",
		   pstops_pid);
    }
  }

  cupsFileClose(inputfp);
  close(inputfd);

  fclose(outputfp);
  close(outputfd);

  return (status);
}


/*
 * 'cfFilterImageToPS()' - Filter function to convert many common image file
 *                 formats into PostScript
 */

int                                       /* O - Error status */
cfFilterImageToPS(int inputfd,            /* I - File descriptor input stream */
		  int outputfd,           /* I - File descriptor output stream*/
		  int inputseekable,      /* I - Is input stream seekable?
					     (unused) */
		  cf_filter_data_t *data, /* I - Job and printer data */
		  void *parameters)       /* I - Filter-specific parameters
					     (unused) */
{
  pstops_doc_t	doc;			/* Document information */
  cf_image_t	*img;			/* Image to print */
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
  int		xc0, yc0,		/* Corners of the page in image coords*/
		xc1, yc1;
  cf_ib_t	*row;			/* Current row */
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
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
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
  * Initialize document information structure...
  */

  memset(&doc, 0, sizeof(pstops_doc_t));

 /*
  * Open the input data stream specified by the inputfd ...
  */

  if ((inputfp = fdopen(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPS: Unable to open input data stream.");
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
		   "cfFilterImageToPS: Unable to copy input: %s",
		   strerror(errno));
      return (1);
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: Copying input to temp file \"%s\"",
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
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterImageToPS: Unable to open temporary file.");
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPS: Unable to open output data stream.");
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
  cfFilterSetCommonOptions(ppd, num_options, options, 0,
			 &doc.Orientation, &doc.Duplex,
			 &doc.LanguageLevel, &doc.Color,
			 &doc.PageLeft, &doc.PageRight,
			 &doc.PageTop, &doc.PageBottom,
			 &doc.PageWidth, &doc.PageLength,
			 log, ld);

  /* The cfFilterSetCommonOptions() does not set doc.Color
     according to option settings (user's demand for color/gray),
     so we parse the options and set the mode here */
  cfRasterParseIPPOptions(&h, data, 0, 1);
  if (doc.Color)
    doc.Color = h.cupsNumColors <= 1 ? 0 : 1;
  if (!ppd) {
    /* Without PPD use also the other findings of cfRasterParseIPPOptions() */
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

  colorspace = doc.Color ? CF_IMAGE_RGB_CMYK : CF_IMAGE_WHITE;

  img = cfImageOpenFP(inputfp, colorspace, CF_IMAGE_WHITE, sat, hue, NULL);
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

    float w = (float)cfImageGetWidth(img);
    float h = (float)cfImageGetHeight(img);
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
    if (w * 72.0 / img->xppi > pw || h * 72.0 / img->yppi > ph)
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
      /* For cropfit do the math without the unprintable margins to get correct
         centering */
      if (cropfit)
      {
	pw = doc.PageWidth;
	ph = doc.PageLength;
	doc.PageBottom = 0.0;
	doc.PageTop = doc.PageLength;
	doc.PageLeft = 0.0;
	doc.PageRight = doc.PageWidth;
      }
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
	cf_image_t *img2 = cfImageCrop(img, posw, posh, final_w, final_h);
	cfImageClose(img);
	img = img2;
      }
      else
      {
	float final_w = w, final_h = h;
        if (w > pw * img->xppi / 72.0)
          final_w = pw * img->xppi / 72.0;
        if (h > ph * img->yppi / 72.0)
          final_h = ph * img->yppi / 72.0;
	float posw = (w - final_w) / 2, posh = (h - final_h) / 2;
	posw = (1 + XPosition) * posw;
	posh = (1 - YPosition) * posh;
	cf_image_t *img2 = cfImageCrop(img, posw, posh, final_w, final_h);
	cfImageClose(img);
	img = img2;
	if (flag == 4)
	{
	  doc.PageBottom += (doc.PageLength - final_w * 72.0 / img->xppi) / 2;
	  doc.PageTop = doc.PageBottom + final_w * 72.0 / img->xppi;
	  doc.PageLeft += (doc.PageWidth - final_h * 72.0 / img->yppi) / 2;
	  doc.PageRight = doc.PageLeft + final_h * 72.0 / img->yppi;
	}
	else
	{
	  doc.PageBottom += (doc.PageLength - final_h * 72.0 / img->yppi) / 2;
	  doc.PageTop = doc.PageBottom + final_h * 72.0 / img->yppi;
	  doc.PageLeft += (doc.PageWidth - final_w * 72.0 / img->xppi) / 2;
	  doc.PageRight = doc.PageLeft + final_w * 72.0 / img->xppi;
	}
	if (doc.PageBottom < 0) doc.PageBottom = 0;
	if (doc.PageLeft < 0) doc.PageLeft = 0;
      }
    }
  }

  if (!inputseekable)
    unlink(tempfile);

  if (img == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterImageToPS: The print file could not be opened - %s",
		 strerror(errno));
    return (1);
  }

  colorspace = cfImageGetColorSpace(img);

 /*
  * Scale as necessary...
  */

  if (zoom == 0.0 && xppi == 0)
  {
    xppi = cfImageGetXPPI(img);
    yppi = cfImageGetYPPI(img);
  }

  if (yppi == 0)
    yppi = xppi;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToPS: Before scaling: xppi=%d, yppi=%d, zoom=%.2f",
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
		 "cfFilterImageToPS: Before scaling: xprint=%.1f, yprint=%.1f",
		 xprint, yprint);

    xinches = (float)cfImageGetWidth(img) / (float)xppi;
    yinches = (float)cfImageGetHeight(img) / (float)yppi;

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: Image size is %.1f x %.1f inches...",
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
		   "cfFilterImageToPS: Auto orientation...");

      if ((xinches > xprint || yinches > yprint) &&
          xinches <= yprint && yinches <= xprint)
      {
       /*
	* Rotate the image as needed...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToPS: Using landscape orientation...");

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
    aspect = (float)cfImageGetYPPI(img) / (float)cfImageGetXPPI(img);

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: Before scaling: xprint=%.1f, yprint=%.1f",
		 xprint, yprint);

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: cfImageGetXPPI(img) = %d, "
		 "cfImageGetYPPI(img) = %d, aspect = %f",
		 cfImageGetXPPI(img), cfImageGetYPPI(img), aspect);

    xsize = xprint * zoom;
    ysize = xsize * cfImageGetHeight(img) / cfImageGetWidth(img) / aspect;

    if (ysize > (yprint * zoom))
    {
      ysize = yprint * zoom;
      xsize = ysize * cfImageGetWidth(img) * aspect /
	cfImageGetHeight(img);
    }

    xsize2 = yprint * zoom;
    ysize2 = xsize2 * cfImageGetHeight(img) / cfImageGetWidth(img) /
      aspect;

    if (ysize2 > (xprint * zoom))
    {
      ysize2 = xprint * zoom;
      xsize2 = ysize2 * cfImageGetWidth(img) * aspect /
	cfImageGetHeight(img);
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: Portrait size is %.2f x %.2f inches",
		 xsize, ysize);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: Landscape size is %.2f x %.2f inches",
		 xsize2, ysize2);

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Choose the rotation with the largest area, but prefer
      * portrait if they are equal...
      */

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPS: Auto orientation...");

      if ((xsize * ysize) < (xsize2 * xsize2))
      {
       /*
	* Do landscape orientation...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToPS: Using landscape orientation...");

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
		     "cfFilterImageToPS: Using portrait orientation...");

	doc.Orientation = 0;
	xinches     = xsize;
	yinches     = ysize;
      }
    }
    else if (doc.Orientation & 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPS: Using landscape orientation...");

      xinches     = xsize2;
      yinches     = ysize2;
      xprint      = (doc.PageTop - doc.PageBottom) / 72.0;
      yprint      = (doc.PageRight - doc.PageLeft) / 72.0;
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPS: Using portrait orientation...");

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
	       "cfFilterImageToPS: xpages = %dx%.2fin, ypages = %dx%.2fin",
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

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPS: Updated custom page size to %.2f x %.2f inches...",
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

  doc_puts(&doc, "%!PS-Adobe-3.0\n");
  doc_printf(&doc, "%%%%BoundingBox: %.0f %.0f %.0f %.0f\n",
	  doc.PageLeft, doc.PageBottom, doc.PageRight, doc.PageTop);
  doc_printf(&doc, "%%%%LanguageLevel: %d\n", doc.LanguageLevel);
  doc_printf(&doc, "%%%%Pages: %d\n", xpages * ypages * Copies);
  doc_puts(&doc, "%%DocumentData: Clean7Bit\n");
  doc_puts(&doc, "%%DocumentNeededResources: font Helvetica-Bold\n");
  doc_puts(&doc, "%%Creator: imagetops\n");
  strftime(curdate, sizeof(curdate), "%c", curtm);
  doc_printf(&doc, "%%%%CreationDate: %s\n", curdate);
  write_text_comment(&doc, "Title", data->job_title);
  write_text_comment(&doc, "For", data->job_user);
  if (doc.Orientation & 1)
    doc_puts(&doc, "%%Orientation: Landscape\n");
  else
    doc_puts(&doc, "%%Orientation: Portrait\n");
  doc_puts(&doc, "%%EndComments\n");
  doc_puts(&doc, "%%BeginProlog\n");

  if (ppd != NULL && ppd->patches != NULL)
  {
    doc_puts(&doc, ppd->patches);
    doc_putc(&doc, '\n');
  }

  ppdEmit(ppd, doc.outputfp, PPD_ORDER_DOCUMENT);
  ppdEmit(ppd, doc.outputfp, PPD_ORDER_ANY);
  ppdEmit(ppd, doc.outputfp, PPD_ORDER_PROLOG);

  if (g != 1.0 || b != 1.0)
    doc_printf(&doc,
	    "{ neg 1 add dup 0 lt { pop 1 } { %.3f exp neg 1 add } "
	    "ifelse %.3f mul } bind settransfer\n", g, b);

  write_common(&doc);
  switch (doc.Orientation)
  {
    case 0 :
        write_label_prolog(&doc, cupsGetOption("page-label", num_options,
					     options),
			 doc.PageBottom, doc.PageTop, doc.PageWidth);
        break;

    case 1 :
        write_label_prolog(&doc, cupsGetOption("page-label", num_options,
					     options),
                	 doc.PageLeft, doc.PageRight, doc.PageLength);
        break;

    case 2 :
        write_label_prolog(&doc, cupsGetOption("page-label", num_options,
					     options),
			 doc.PageLength - doc.PageTop,
			 doc.PageLength - doc.PageBottom,
			 doc.PageWidth);
        break;

    case 3 :
	write_label_prolog(&doc, cupsGetOption("page-label", num_options,
					     options),
			 doc.PageWidth - doc.PageRight,
			 doc.PageWidth - doc.PageLeft,
			 doc.PageLength);
        break;
  }

  if (realcopies > 1)
  {
    if (ppd == NULL || ppd->language_level == 1)
      doc_printf(&doc, "/#copies %d def\n", realcopies);
    else
      doc_printf(&doc, "<</NumCopies %d>>setpagedevice\n", realcopies);
  }

  doc_puts(&doc, "%%EndProlog\n");

 /*
  * Output the pages...
  */

  row = malloc(cfImageGetWidth(img) * abs(colorspace) + 3);
  if (row == NULL)
  {
    log(ld, CF_LOGLEVEL_ERROR,
	"cfFilterImageToPS: Could not allocate memory.");
    cfImageClose(img);
    return (2);
  }

  if (log)
  {
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPS: XPosition=%d, YPosition=%d, Orientation=%d",
	XPosition, YPosition, doc.Orientation);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPS: xprint=%.1f, yprint=%.1f", xprint, yprint);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPS: PageLeft=%.0f, PageRight=%.0f, PageWidth=%.0f",
	doc.PageLeft, doc.PageRight, doc.PageWidth);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPS: PageBottom=%.0f, PageTop=%.0f, PageLength=%.0f",
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

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToPS: left=%.2f, top=%.2f", left, top);

  for (page = 1; Copies > 0; Copies --)
    for (xpage = 0; xpage < xpages; xpage ++)
      for (ypage = 0; ypage < ypages; ypage ++, page ++)
      {
	if (iscanceled && iscanceled(icd))
	{
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterImageToPS: Job canceled");
	  goto canceled;
	}

        if (log && ppd && ppd->num_filters == 0)
	  log(ld, CF_LOGLEVEL_CONTROL,
	      "PAGE: %d %d", page, realcopies);

	if (log) log(ld, CF_LOGLEVEL_INFO,
		     "cfFilterImageToPS: Printing page %d.", page);

        doc_printf(&doc, "%%%%Page: %d %d\n", page, page);

        ppdEmit(ppd, doc.outputfp, PPD_ORDER_PAGE);

	doc_puts(&doc, "gsave\n");

	if (Flip)
	  doc_printf(&doc, "%.0f 0 translate -1 1 scale\n", doc.PageWidth);

	switch (doc.Orientation)
	{
	  case 1 : /* Landscape */
	      doc_printf(&doc, "%.0f 0 translate 90 rotate\n",
		      doc.PageWidth);
              break;
	  case 2 : /* Reverse Portrait */
	      doc_printf(&doc, "%.0f %.0f translate 180 rotate\n",
		      doc.PageWidth, doc.PageLength);
	      break;
	  case 3 : /* Reverse Landscape */
	      doc_printf(&doc, "0 %.0f translate -90 rotate\n",
		      doc.PageLength);
              break;
	}

        doc_puts(&doc, "gsave\n");

	xc0 = cfImageGetWidth(img) * xpage / xpages;
	xc1 = cfImageGetWidth(img) * (xpage + 1) / xpages - 1;
	yc0 = cfImageGetHeight(img) * ypage / ypages;
	yc1 = cfImageGetHeight(img) * (ypage + 1) / ypages - 1;

        doc_printf(&doc, "%.1f %.1f translate\n", left, top);

	doc_printf(&doc, "%.3f %.3f scale\n\n",
		xprint * 72.0 / (xc1 - xc0 + 1),
		yprint * 72.0 / (yc1 - yc0 + 1));

	if (doc.LanguageLevel == 1)
	{
	  doc_printf(&doc, "/picture %d string def\n",
		  (xc1 - xc0 + 1) * abs(colorspace));
	  doc_printf(&doc, "%d %d 8[1 0 0 -1 0 1]",
		  (xc1 - xc0 + 1), (yc1 - yc0 + 1));

          if (colorspace == CF_IMAGE_WHITE)
            doc_puts(&doc, "{currentfile picture readhexstring pop} image\n");
          else
            doc_printf(&doc,
		       "{currentfile picture readhexstring pop} false %d "
		       "colorimage\n",
		       abs(colorspace));

          for (y = yc0; y <= yc1; y ++)
          {
            cfImageGetRow(img, xc0, y, xc1 - xc0 + 1, row);
            ps_hex(&doc, row, (xc1 - xc0 + 1) * abs(colorspace),
		   y == yc1);
          }
	}
	else
	{
          switch (colorspace)
	  {
	    case CF_IMAGE_WHITE :
	        doc_puts(&doc, "/DeviceGray setcolorspace\n");
		break;
            case CF_IMAGE_RGB :
	        doc_puts(&doc, "/DeviceRGB setcolorspace\n");
		break;
            case CF_IMAGE_CMYK :
	        doc_puts(&doc, "/DeviceCMYK setcolorspace\n");
		break;
          }

          doc_printf(&doc,
		  "<<"
		  "/ImageType 1"
		  "/Width %d"
		  "/Height %d"
		  "/BitsPerComponent 8",
		  xc1 - xc0 + 1, yc1 - yc0 + 1);

          switch (colorspace)
	  {
	    case CF_IMAGE_WHITE :
                doc_puts(&doc, "/Decode[0 1]");
		break;
            case CF_IMAGE_RGB :
                doc_puts(&doc, "/Decode[0 1 0 1 0 1]");
		break;
            case CF_IMAGE_CMYK :
                doc_puts(&doc, "/Decode[0 1 0 1 0 1 0 1]");
		break;
          }

          doc_puts(&doc, "\n/DataSource currentfile/ASCII85Decode filter");

          if (((xc1 - xc0 + 1) / xprint) < 100.0)
            doc_puts(&doc, "/Interpolate true");

          doc_puts(&doc, "/ImageMatrix[1 0 0 -1 0 1]>>image\n");

          for (y = yc0, out_offset = 0; y <= yc1; y ++)
          {
            cfImageGetRow(img, xc0, y, xc1 - xc0 + 1, row + out_offset);

            out_length = (xc1 - xc0 + 1) * abs(colorspace) + out_offset;
            out_offset = out_length & 3;

            ps_ascii85(&doc, row, out_length, y == yc1);

            if (out_offset > 0)
              memcpy(row, row + out_length - out_offset, out_offset);
          }
	}

	doc_puts(&doc, "grestore\n");
	write_labels(&doc, 0);
	doc_puts(&doc, "grestore\n");
	doc_puts(&doc, "showpage\n");
      }

 canceled:
  doc_puts(&doc, "%%EOF\n");

  free(row);
  
 /*
  * End the job with the appropriate JCL command or CTRL-D otherwise.
  */

  if (emit_jcl)
  {
    if (ppd && ppd->jcl_end)
      ppdEmitJCLEnd(ppd, doc.outputfp);
    else
      doc_putc(&doc, 0x04);
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToPS: Printing completed.", page);

 /*
  * Close files...
  */

  cfImageClose(img);
  fclose(doc.outputfp);
  close(outputfd);

  return (0);
}


/*
 * 'add_page()' - Add a page to the pages array.
 */

static pstops_page_t *			/* O - New page info object */
add_page(pstops_doc_t *doc,		/* I - Document information */
         const char   *label)		/* I - Page label */
{
  pstops_page_t	*pageinfo;		/* New page info object */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  if (!doc->pages)
    doc->pages = cupsArrayNew(NULL, NULL);

  if (!doc->pages)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPSToPS: Unable to allocate memory for pages array");
    return (NULL);
  }

  if ((pageinfo = calloc(1, sizeof(pstops_page_t))) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPSToPS: Unable to allocate memory for page info");
    return (NULL);
  }

  pageinfo->label  = strdup(label);
  pageinfo->offset = cupsFileTell(doc->temp);

  cupsArrayAdd(doc->pages, pageinfo);

  doc->page ++;

  return (pageinfo);
}


/*
 * 'check_range()' - Check to see if the current page is selected for
 *                   printing.
 */

static int				/* O - 1 if selected, 0 otherwise */
check_range(int          page,    	/* I - Page number */
            const char *Ranges,    /* I - page_ranges or inputPageRange */	
            const char *pageset ) /* I - Only provided with page_ranges else NULL */  
{
  const char	*range;			/* Pointer into range string */
  int		lower, upper;		/* Lower and upper page numbers */


  if (pageset)
  {
   /*
    * See if we only print even or odd pages...
    */

    if (!strcasecmp(pageset, "even") && (page & 1))
      return (0);

    if (!strcasecmp(pageset, "odd") && !(page & 1))
      return (0);
  }

  if (!Ranges)
    return (1);				/* No range, print all pages... */

  for (range = Ranges; *range != '\0';)
  {
    if (*range == '-')
    {
      lower = 1;
      range ++;
      upper = (int)strtol(range, (char **)&range, 10);
    }
    else
    {
      lower = (int)strtol(range, (char **)&range, 10);

      if (*range == '-')
      {
        range ++;
	if (!isdigit(*range & 255))
	  upper = 65535;
	else
	  upper = (int)strtol(range, (char **)&range, 10);
      }
      else
        upper = lower;
    }

    if (page >= lower && page <= upper)
      return (1);

    if (*range == ',')
      range ++;
    else
      break;
  }

  return (0);
}


/*
 * 'copy_bytes()' - Copy bytes from the temporary file to the output file.
 */

static void
copy_bytes(pstops_doc_t *doc,		/* I - Document info */
           off_t       offset,		/* I - Offset to page data */
           size_t      length)		/* I - Length of page data */
{
  char		buffer[8192];		/* Data buffer */
  ssize_t	nbytes;			/* Number of bytes read */
  size_t	nleft;			/* Number of bytes left/remaining */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  nleft = length;

  if (cupsFileSeek(doc->temp, offset) < 0)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "Unable to seek in file");
    return;
  }

  while (nleft > 0 || length == 0)
  {
    if (nleft > sizeof(buffer) || length == 0)
      nbytes = sizeof(buffer);
    else
      nbytes = (ssize_t)nleft;

    if ((nbytes = cupsFileRead(doc->temp, buffer, (size_t)nbytes)) < 1)
      return;

    nleft -= (size_t)nbytes;

    doc_write(doc, buffer, (size_t)nbytes);
  }
}


/*
 * 'copy_comments()' - Copy all of the comments section.
 *
 * This function expects "line" to be filled with a comment line.
 * On return, "line" will contain the next line in the file, if any.
 */

static ssize_t				/* O - Length of next line */
copy_comments(pstops_doc_t *doc,	/* I - Document info */
	      ppd_file_t   *ppd,	/* I - PPD file */
              char         *line,	/* I - Line buffer */
	      ssize_t      linelen,	/* I - Length of initial line */
	      size_t       linesize)	/* I - Size of line buffer */
{
  int	saw_bounding_box,		/* Saw %%BoundingBox: comment? */
	saw_for,			/* Saw %%For: comment? */
	saw_pages,			/* Saw %%Pages: comment? */
	saw_title;			/* Saw %%Title: comment? */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


 /*
  * Loop until we see %%EndComments or a non-comment line...
  */

  saw_bounding_box = 0;
  saw_for          = 0;
  saw_pages        = 0;
  saw_title        = 0;

  while (line[0] == '%')
  {
   /*
    * Strip trailing whitespace...
    */

    while (linelen > 0)
    {
      linelen --;

      if (!isspace(line[linelen] & 255))
        break;
      else
        line[linelen] = '\0';
    }

   /*
    * Log the header...
    */

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPSToPS: %s", line);

   /*
    * Pull the headers out...
    */

    if (!strncmp(line, "%%Pages:", 8))
    {
      int	pages;			/* Number of pages */

      if (saw_pages && log)
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterPSToPS: A duplicate %%Pages: comment was seen.");

      saw_pages = 1;

      if (doc->Duplex &&
	  (pages = atoi(line + 8)) > 0 && pages <= doc->number_up)
      {
       /*
        * Since we will only be printing on a single page, disable duplexing.
	*/

	doc->Duplex      = 0;
	doc->slow_duplex = 0;

	if (cupsGetOption("sides", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("sides", "one-sided",
	                                   doc->num_options, &(doc->options));

	if (cupsGetOption("Duplex", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("Duplex", "None",
	                                   doc->num_options, &(doc->options));

	if (cupsGetOption("EFDuplex", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("EFDuplex", "None",
	                                   doc->num_options, &(doc->options));

	if (cupsGetOption("EFDuplexing", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("EFDuplexing", "False",
	                                   doc->num_options, &(doc->options));

	if (cupsGetOption("ARDuplex", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("ARDuplex", "None",
	                                   doc->num_options, &(doc->options));

	if (cupsGetOption("KD03Duplex", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("KD03Duplex", "None",
	                                   doc->num_options, &(doc->options));

	if (cupsGetOption("JCLDuplex", doc->num_options, doc->options))
	  doc->num_options = cupsAddOption("JCLDuplex", "None",
	                                   doc->num_options, &(doc->options));

	ppdMarkOption(ppd, "Duplex", "None");
	ppdMarkOption(ppd, "EFDuplex", "None");
	ppdMarkOption(ppd, "EFDuplexing", "False");
	ppdMarkOption(ppd, "ARDuplex", "None");
	ppdMarkOption(ppd, "KD03Duplex", "None");
	ppdMarkOption(ppd, "JCLDuplex", "None");
      }
    }
    else if (!strncmp(line, "%%BoundingBox:", 14))
    {
      if (saw_bounding_box)
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPSToPS: A duplicate %%BoundingBox: comment was seen.");
      }
      else if (strstr(line + 14, "(atend)"))
      {
       /*
        * Do nothing for now but use the default imageable area...
	*/
      }
      else if (sscanf(line + 14, "%d%d%d%d", doc->bounding_box + 0,
	              doc->bounding_box + 1, doc->bounding_box + 2,
		      doc->bounding_box + 3) != 4)
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPSToPS: A bad %%BoundingBox: comment was seen.");

	doc->bounding_box[0] = (int)(doc->PageLeft);
	doc->bounding_box[1] = (int)(doc->PageBottom);
	doc->bounding_box[2] = (int)(doc->PageRight);
	doc->bounding_box[3] = (int)(doc->PageTop);
      }

      saw_bounding_box = 1;
    }
    else if (!strncmp(line, "%%For:", 6))
    {
      saw_for = 1;
      doc_printf(doc, "%s\n", line);
    }
    else if (!strncmp(line, "%%Title:", 8))
    {
      saw_title = 1;
      doc_printf(doc, "%s\n", line);
    }
    else if (!strncmp(line, "%cupsRotation:", 14))
    {
     /*
      * Reset orientation of document?
      */

      int orient = (atoi(line + 14) / 90) & 3;

      if (orient != doc->Orientation)
      {
       /*
        * Yes, update things so that the pages come out right...
	*/

	doc->Orientation = (4 - doc->Orientation + orient) & 3;
	cfFilterUpdatePageVars(doc->Orientation,
			     &doc->PageLeft, &doc->PageRight,
			     &doc->PageTop, &doc->PageBottom,
			     &doc->PageWidth, &doc->PageLength);
	doc->Orientation = orient;
      }
    }
    else if (!strcmp(line, "%%EndComments"))
    {
      linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize);
      break;
    }
    else if (strncmp(line, "%!", 2) && strncmp(line, "%cups", 5))
      doc_printf(doc, "%s\n", line);

    if ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) == 0)
      break;
  }

  if (!saw_bounding_box && log)
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: There wasn't a %%BoundingBox: comment in the header.");

  if (!saw_pages && log)
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: There wasn't a %%Pages: comment in the header.");

  if (!saw_for)
    write_text_comment(doc, "For", doc->user);

  if (!saw_title)
    write_text_comment(doc, "Title", doc->title);

  if (doc->copies != 1 && (!doc->collate || !doc->slow_collate))
  {
   /*
    * Tell the document processor the copy and duplex options
    * that are required...
    */

    doc_printf(doc, "%%%%Requirements: numcopies(%d)%s%s\n", doc->copies,
               doc->collate ? " collate" : "",
	       doc->Duplex ? " duplex" : "");

   /*
    * Apple uses RBI comments for various non-PPD options...
    */

    doc_printf(doc, "%%RBINumCopies: %d\n", doc->copies);
  }
  else
  {
   /*
    * Tell the document processor the duplex option that is required...
    */

    if (doc->Duplex)
      doc_puts(doc, "%%Requirements: duplex\n");

   /*
    * Apple uses RBI comments for various non-PPD options...
    */

    doc_puts(doc, "%RBINumCopies: 1\n");
  }

  doc_puts(doc, "%%Pages: (atend)\n");
  doc_puts(doc, "%%BoundingBox: (atend)\n");
  doc_puts(doc, "%%EndComments\n");

  return (linelen);
}


/*
 * 'copy_dsc()' - Copy a DSC-conforming document.
 *
 * This function expects "line" to be filled with the %!PS-Adobe comment line.
 */

static void
copy_dsc(pstops_doc_t *doc,		/* I - Document info */
         ppd_file_t   *ppd,		/* I - PPD file */
	 char         *line,		/* I - Line buffer */
	 ssize_t      linelen,		/* I - Length of initial line */
	 size_t       linesize)		/* I - Size of line buffer */
{
  int		number;			/* Page number */
  pstops_page_t	*pageinfo;		/* Page information */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;
  cf_filter_iscanceledfunc_t iscanceled = doc->iscanceledfunc;
  void          *icd = doc->iscanceleddata;


 /*
  * Make sure we use ESPshowpage for EPS files...
  */

  if (strstr(line, "EPSF"))
  {
    doc->use_ESPshowpage = 1;
    doc->number_up       = 1;
  }

 /*
  * Start sending the document with any commands needed...
  */

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: Before copy_comments - %s", line);
  linelen = copy_comments(doc, ppd, line, linelen, linesize);

 /*
  * Now find the prolog section, if any...
  */

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: Before copy_prolog - %s", line);
  linelen = copy_prolog(doc, ppd, line, linelen, linesize);

 /*
  * Then the document setup section...
  */

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: Before copy_setup - %s", line);
  linelen = copy_setup(doc, ppd, line, linelen, linesize);

 /*
  * Copy until we see %%Page:...
  */

  while (strncmp(line, "%%Page:", 7) && strncmp(line, "%%Trailer", 9))
  {
    doc_write(doc, line, (size_t)linelen);

    if ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) == 0)
      break;
  }

 /*
  * Then process pages until we have no more...
  */

  number = 0;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: Before page loop - %s", line);
  while (!strncmp(line, "%%Page:", 7))
  {
    if (iscanceled && iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
                  "cfFilterPSToPS: Job canceled");
      break;
    }

    number ++;

    if (check_range((number - 1) / doc->number_up + 1,doc->page_ranges,doc->page_set))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: Copying page %d...", number);
      linelen = copy_page(doc, ppd, number, line, linelen, linesize);
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: Skipping page %d...", number);
      linelen = skip_page(doc, line, linelen, linesize);
    }
  }

 /*
  * Finish up the last page(s)...
  */

  if (number && is_not_last_page(number) && cupsArrayLast(doc->pages) &&
      check_range((number - 1) / doc->number_up + 1,doc->page_ranges,doc->page_set))
  {
    pageinfo = (pstops_page_t *)cupsArrayLast(doc->pages);

    start_nup(doc, doc->number_up, 0, doc->bounding_box);
    doc_puts(doc, "showpage\n");
    end_nup(doc, doc->number_up);

    pageinfo->length = (ssize_t)(cupsFileTell(doc->temp) - pageinfo->offset);
  }

  if (doc->slow_duplex && (doc->page & 1))
  {
   /*
    * Make sure we have an even number of pages...
    */

    pageinfo = add_page(doc, "(filler)");
    if (pageinfo == NULL)
      return;

    if (!doc->slow_order)
    {
      if ((!ppd || !ppd->num_filters) && log)
	log(ld, CF_LOGLEVEL_CONTROL,
	    "PAGE: %d %d", doc->page,
	    doc->slow_collate ? 1 : doc->copies);

      doc_printf(doc, "%%%%Page: (filler) %d\n", doc->page);
    }

    start_nup(doc, doc->number_up, 0, doc->bounding_box);
    doc_puts(doc, "showpage\n");
    end_nup(doc, doc->number_up);

    pageinfo->length = (ssize_t)(cupsFileTell(doc->temp) - pageinfo->offset);
  }

 /*
  * Make additional copies as necessary...
  */

  number = doc->slow_order ? 0 : doc->page;

  if (doc->temp && (!iscanceled || !iscanceled(icd)) &&
      cupsArrayCount(doc->pages) > 0)
  {
    int	copy;				/* Current copy */


   /*
    * Reopen the temporary file for reading...
    */

    cupsFileClose(doc->temp);

    doc->temp = cupsFileOpen(doc->tempfile, "r");

   /*
    * Make the copies...
    */

    if (doc->slow_collate)
      copy = !doc->slow_order;
    else
      copy = doc->copies - 1;

    for (; copy < doc->copies; copy ++)
    {
      if (iscanceled && iscanceled(icd))
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPSToPS: Job canceled");
	break;
      }

     /*
      * Send end-of-job stuff followed by any start-of-job stuff required
      * for the JCL options...
      */

      if (number && doc->emit_jcl && ppd && ppd->jcl_end)
      {
       /*
        * Send the trailer...
	*/

        doc_puts(doc, "%%Trailer\n");
	doc_printf(doc, "%%%%Pages: %d\n", cupsArrayCount(doc->pages));
	if (doc->number_up > 1 || doc->fit_to_page)
	  doc_printf(doc, "%%%%BoundingBox: %.0f %.0f %.0f %.0f\n",
		 doc->PageLeft, doc->PageBottom, doc->PageRight, doc->PageTop);
	else
	  doc_printf(doc, "%%%%BoundingBox: %d %d %d %d\n",
		 doc->new_bounding_box[0], doc->new_bounding_box[1],
		 doc->new_bounding_box[2], doc->new_bounding_box[3]);
        doc_puts(doc, "%%EOF\n");

       /*
        * Start a new document...
	*/

        ppdEmitJCLEnd(ppd, doc->outputfp);
        ppdEmitJCL(ppd, doc->outputfp, doc->job_id, doc->user, doc->title);

	doc_puts(doc, "%!PS-Adobe-3.0\n");

	number = 0;
      }

     /*
      * Copy the prolog as needed...
      */

      if (!number)
      {
        pageinfo = (pstops_page_t *)cupsArrayFirst(doc->pages);
	copy_bytes(doc, 0, (size_t)pageinfo->offset);
      }

     /*
      * Then copy all of the pages...
      */

      pageinfo = doc->slow_order ? (pstops_page_t *)cupsArrayLast(doc->pages) :
                                   (pstops_page_t *)cupsArrayFirst(doc->pages);

      while (pageinfo)
      {

	if (iscanceled && iscanceled(icd))
	  {
	    if (log) log(ld, CF_LOGLEVEL_DEBUG,
			 "cfFilterPSToPS: Job canceled");
	    break;
	  }

        number ++;

	if ((!ppd || !ppd->num_filters) && log)
	  log(ld, CF_LOGLEVEL_CONTROL,
	      "PAGE: %d %d", number,
	      doc->slow_collate ? 1 : doc->copies);

	if (doc->number_up > 1)
	{
	  doc_printf(doc, "%%%%Page: (%d) %d\n", number, number);
	  doc_printf(doc, "%%%%PageBoundingBox: %.0f %.0f %.0f %.0f\n",
		 doc->PageLeft, doc->PageBottom, doc->PageRight, doc->PageTop);
	}
	else
	{
          doc_printf(doc, "%%%%Page: %s %d\n", pageinfo->label, number);
	  doc_printf(doc, "%%%%PageBoundingBox: %d %d %d %d\n",
		  pageinfo->bounding_box[0], pageinfo->bounding_box[1],
		  pageinfo->bounding_box[2], pageinfo->bounding_box[3]);
	}

	copy_bytes(doc, pageinfo->offset, (size_t)pageinfo->length);

	pageinfo = doc->slow_order ?
	  (pstops_page_t *)cupsArrayPrev(doc->pages) :
	  (pstops_page_t *)cupsArrayNext(doc->pages);
      }
    }
  }

 /*
  * Restore the old showpage operator as needed...
  */

  if (doc->use_ESPshowpage)
    doc_puts(doc, "userdict/showpage/ESPshowpage load put\n");

 /*
  * Write/copy the trailer...
  */

  if (!iscanceled || !iscanceled(icd))
    copy_trailer(doc, ppd, number, line, linelen, linesize);
}


/*
 * 'copy_non_dsc()' - Copy a document that does not conform to the DSC.
 *
 * This function expects "line" to be filled with the %! comment line.
 */

static void
copy_non_dsc(pstops_doc_t *doc,		/* I - Document info */
             ppd_file_t   *ppd,		/* I - PPD file */
	     char         *line,	/* I - Line buffer */
	     ssize_t      linelen,	/* I - Length of initial line */
	     size_t       linesize)	/* I - Size of line buffer */
{
  int		copy;			/* Current copy */
  char		buffer[8192];		/* Copy buffer */
  ssize_t	bytes;			/* Number of bytes copied */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;
  cf_filter_iscanceledfunc_t iscanceled = doc->iscanceledfunc;
  void          *icd = doc->iscanceleddata;


  (void)linesize;

 /*
  * First let the user know that they are attempting to print a file
  * that may not print correctly...
  */

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: This document does not conform to the Adobe Document "
	       "Structuring Conventions and may not print correctly.");

 /*
  * Then write a standard DSC comment section...
  */

  doc_printf(doc, "%%%%BoundingBox: %.0f %.0f %.0f %.0f\n",
	  doc->PageLeft, doc->PageBottom, doc->PageRight, doc->PageTop);

  if (doc->slow_collate && doc->copies > 1)
    doc_printf(doc, "%%%%Pages: %d\n", doc->copies);
  else
    doc_puts(doc, "%%Pages: 1\n");

  write_text_comment(doc, "For", doc->user);
  write_text_comment(doc, "Title", doc->title);

  if (doc->copies != 1 && (!doc->collate || !doc->slow_collate))
  {
   /*
    * Tell the document processor the copy and duplex options
    * that are required...
    */

    doc_printf(doc, "%%%%Requirements: numcopies(%d)%s%s\n", doc->copies,
	    doc->collate ? " collate" : "",
	    doc->Duplex ? " duplex" : "");

   /*
    * Apple uses RBI comments for various non-PPD options...
    */

    doc_printf(doc, "%%RBINumCopies: %d\n", doc->copies);
  }
  else
  {
   /*
    * Tell the document processor the duplex option that is required...
    */

    if (doc->Duplex)
      doc_puts(doc, "%%Requirements: duplex\n");

   /*
    * Apple uses RBI comments for various non-PPD options...
    */

    doc_puts(doc, "%RBINumCopies: 1\n");
  }

  doc_puts(doc, "%%EndComments\n");

 /*
  * Then the prolog...
  */

  doc_puts(doc, "%%BeginProlog\n");

  do_prolog(doc, ppd);

  doc_puts(doc, "%%EndProlog\n");

 /*
  * Then the setup section...
  */

  doc_puts(doc, "%%BeginSetup\n");

  do_setup(doc, ppd);

  doc_puts(doc, "%%EndSetup\n");

 /*
  * Finally, embed a copy of the file inside a %%Page...
  */

  if ((!ppd || !ppd->num_filters) && log)
    log(ld, CF_LOGLEVEL_CONTROL,
	"PAGE: 1 %d", doc->temp ? 1 : doc->copies);

  doc_puts(doc, "%%Page: 1 1\n");
  doc_puts(doc, "%%BeginPageSetup\n");
  ppdEmit(ppd, doc->outputfp, PPD_ORDER_PAGE);
  doc_puts(doc, "%%EndPageSetup\n");
  doc_puts(doc, "%%BeginDocument: nondsc\n");

  doc_write(doc, line, (size_t)linelen);

  if (doc->temp)
    cupsFileWrite(doc->temp, line, (size_t)linelen);

  while ((bytes = cupsFileRead(doc->inputfp, buffer, sizeof(buffer))) > 0)
  {
    doc_write(doc, buffer, (size_t)bytes);

    if (doc->temp)
      cupsFileWrite(doc->temp, buffer, (size_t)bytes);
  }

  doc_puts(doc, "%%EndDocument\n");

  if (doc->use_ESPshowpage)
  {
    write_labels_outputfile_only(doc, doc->Orientation);
    doc_puts(doc, "ESPshowpage\n");
  }

  if (doc->temp && (!iscanceled || !iscanceled(icd)))
  {
   /*
    * Reopen the temporary file for reading...
    */

    cupsFileClose(doc->temp);

    doc->temp = cupsFileOpen(doc->tempfile, "r");

   /*
    * Make the additional copies as needed...
    */

    for (copy = 1; copy < doc->copies; copy ++)
    {
      if (iscanceled && iscanceled(icd))
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPSToPS: Job canceled");
	break;
      }

      if ((!ppd || !ppd->num_filters) && log)
	log(ld, CF_LOGLEVEL_CONTROL,
	    "PAGE: 1 1");

      doc_printf(doc, "%%%%Page: %d %d\n", copy + 1, copy + 1);
      doc_puts(doc, "%%BeginPageSetup\n");
      ppdEmit(ppd, doc->outputfp, PPD_ORDER_PAGE);
      doc_puts(doc, "%%EndPageSetup\n");
      doc_puts(doc, "%%BeginDocument: nondsc\n");

      copy_bytes(doc, 0, 0);

      doc_puts(doc, "%%EndDocument\n");

      if (doc->use_ESPshowpage)
      {
	write_labels_outputfile_only(doc, doc->Orientation);
        doc_puts(doc, "ESPshowpage\n");
      }
    }
  }

 /*
  * Restore the old showpage operator as needed...
  */

  if (doc->use_ESPshowpage)
    doc_puts(doc, "userdict/showpage/ESPshowpage load put\n");
}


/*
 * 'copy_page()' - Copy a page description.
 *
 * This function expects "line" to be filled with a %%Page comment line.
 * On return, "line" will contain the next line in the file, if any.
 */

static ssize_t				/* O - Length of next line */
copy_page(pstops_doc_t *doc,		/* I - Document info */
          ppd_file_t   *ppd,		/* I - PPD file */
	  int          number,		/* I - Current page number */
	  char         *line,		/* I - Line buffer */
	  ssize_t      linelen,		/* I - Length of initial line */
	  size_t       linesize)	/* I - Size of line buffer */
{
  char		label[256],		/* Page label string */
		*ptr;			/* Pointer into line */
  int		level;			/* Embedded document level */
  pstops_page_t	*pageinfo;		/* Page information */
  int		first_page;		/* First page on N-up output? */
  int		has_page_setup = 0;	/* Does the page have
					   %%Begin/EndPageSetup? */
  int		bounding_box[4];	/* PageBoundingBox */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


 /*
  * Get the page label for this page...
  */

  first_page = is_first_page(number);

  if (!parse_text(line + 7, &ptr, label, sizeof(label)))
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPSToPS: There was a bad %%Page: comment in the file.");
    label[0] = '\0';
    number   = doc->page;
  }
  else if (strtol(ptr, &ptr, 10) == LONG_MAX || !isspace(*ptr & 255))
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPSToPS: There was a bad %%Page: comment in the file.");
    number = doc->page;
  }

 /*
  * Create or update the current output page...
  */

  if (first_page)
  {
    pageinfo = add_page(doc, label);
    if (pageinfo == NULL)
      return (0);
  }
  else
    pageinfo = (pstops_page_t *)cupsArrayLast(doc->pages);

 /*
  * Handle first page override...
  */

  if (doc->ap_input_slot || doc->ap_manual_feed)
  {
    if ((doc->page == 1 && (!doc->slow_order || !doc->Duplex)) ||
        (doc->page == 2 && doc->slow_order && doc->Duplex))
    {
     /*
      * First page/sheet gets AP_FIRSTPAGE_* options...
      */

      pageinfo->num_options = cupsAddOption("InputSlot", doc->ap_input_slot,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("ManualFeed",
                                            doc->ap_input_slot ? "False" :
					        doc->ap_manual_feed,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("MediaColor", doc->ap_media_color,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("MediaType", doc->ap_media_type,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("PageRegion", doc->ap_page_region,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("PageSize", doc->ap_page_size,
                                            pageinfo->num_options,
					    &(pageinfo->options));
    }
    else if (doc->page == (doc->Duplex + 2))
    {
     /*
      * Second page/sheet gets default options...
      */

      pageinfo->num_options = cupsAddOption("InputSlot", doc->input_slot,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("ManualFeed",
                                            doc->input_slot ? "False" :
					        doc->manual_feed,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("MediaColor", doc->media_color,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("MediaType", doc->media_type,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("PageRegion", doc->page_region,
                                            pageinfo->num_options,
					    &(pageinfo->options));
      pageinfo->num_options = cupsAddOption("PageSize", doc->page_size,
                                            pageinfo->num_options,
					    &(pageinfo->options));
    }
  }

 /*
  * Scan comments until we see something other than %%Page*: or
  * %%Include*...
  */

  memcpy(bounding_box, doc->bounding_box, sizeof(bounding_box));

  while ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) > 0)
  {
    if (!strncmp(line, "%%PageBoundingBox:", 18))
    {
     /*
      * %%PageBoundingBox: llx lly urx ury
      */

      if (sscanf(line + 18, "%d%d%d%d", bounding_box + 0,
                 bounding_box + 1, bounding_box + 2,
		 bounding_box + 3) != 4)
      {
	if (log)
	  log(ld, CF_LOGLEVEL_DEBUG,
	      "cfFilterPSToPS: There was a bad %%PageBoundingBox: comment in the "
	      "file.");
        memcpy(bounding_box, doc->bounding_box,
	       sizeof(bounding_box));
      }
      else if (doc->number_up == 1 && !doc->fit_to_page && doc->Orientation)
      {
        int	temp_bbox[4];		/* Temporary bounding box */


        memcpy(temp_bbox, bounding_box, sizeof(temp_bbox));

	if (log)
	{
	  log(ld, CF_LOGLEVEL_DEBUG,
	      "cfFilterPSToPS: Orientation = %d", doc->Orientation);
	  log(ld, CF_LOGLEVEL_DEBUG,
	      "cfFilterPSToPS: original bounding_box = [ %d %d %d %d ]",
	      bounding_box[0], bounding_box[1],
	      bounding_box[2], bounding_box[3]);
	  log(ld, CF_LOGLEVEL_DEBUG,
	      "cfFilterPSToPS: PageWidth = %.1f, PageLength = %.1f",
	      doc->PageWidth, doc->PageLength);
	}

        switch (doc->Orientation)
	{
	  case 1 : /* Landscape */
	      bounding_box[0] = (int)(doc->PageLength - temp_bbox[3]);
	      bounding_box[1] = temp_bbox[0];
	      bounding_box[2] = (int)(doc->PageLength - temp_bbox[1]);
	      bounding_box[3] = temp_bbox[2];
              break;

	  case 2 : /* Reverse Portrait */
	      bounding_box[0] = (int)(doc->PageWidth - temp_bbox[2]);
	      bounding_box[1] = (int)(doc->PageLength - temp_bbox[3]);
	      bounding_box[2] = (int)(doc->PageWidth - temp_bbox[0]);
	      bounding_box[3] = (int)(doc->PageLength - temp_bbox[1]);
              break;

	  case 3 : /* Reverse Landscape */
	      bounding_box[0] = temp_bbox[1];
	      bounding_box[1] = (int)(doc->PageWidth - temp_bbox[2]);
	      bounding_box[2] = temp_bbox[3];
	      bounding_box[3] = (int)(doc->PageWidth - temp_bbox[0]);
              break;
	}

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPSToPS: updated bounding_box = [ %d %d %d %d ]",
		     bounding_box[0], bounding_box[1],
		     bounding_box[2], bounding_box[3]);
      }
    }
#if 0
    else if (!strncmp(line, "%%PageCustomColors:", 19) ||
             !strncmp(line, "%%PageMedia:", 12) ||
	     !strncmp(line, "%%PageOrientation:", 18) ||
	     !strncmp(line, "%%PageProcessColors:", 20) ||
	     !strncmp(line, "%%PageRequirements:", 18) ||
	     !strncmp(line, "%%PageResources:", 16))
    {
     /*
      * Copy literal...
      */
    }
#endif /* 0 */
    else if (!strncmp(line, "%%PageCustomColors:", 19))
    {
     /*
      * %%PageCustomColors: ...
      */
    }
    else if (!strncmp(line, "%%PageMedia:", 12))
    {
     /*
      * %%PageMedia: ...
      */
    }
    else if (!strncmp(line, "%%PageOrientation:", 18))
    {
     /*
      * %%PageOrientation: ...
      */
    }
    else if (!strncmp(line, "%%PageProcessColors:", 20))
    {
     /*
      * %%PageProcessColors: ...
      */
    }
    else if (!strncmp(line, "%%PageRequirements:", 18))
    {
     /*
      * %%PageRequirements: ...
      */
    }
    else if (!strncmp(line, "%%PageResources:", 16))
    {
     /*
      * %%PageResources: ...
      */
    }
    else if (!strncmp(line, "%%IncludeFeature:", 17))
    {
     /*
      * %%IncludeFeature: *MainKeyword OptionKeyword
      */

      if (doc->number_up == 1 &&!doc->fit_to_page)
	pageinfo->num_options = include_feature(doc, ppd, line,
	                                        pageinfo->num_options,
                                        	&(pageinfo->options));
    }
    else if (!strncmp(line, "%%BeginPageSetup", 16))
    {
      has_page_setup = 1;
      break;
    }
    else
      break;
  }

  if (doc->number_up == 1)
  {
   /*
    * Update the document's composite and page bounding box...
    */

    memcpy(pageinfo->bounding_box, bounding_box,
           sizeof(pageinfo->bounding_box));

    if (bounding_box[0] < doc->new_bounding_box[0])
      doc->new_bounding_box[0] = bounding_box[0];
    if (bounding_box[1] < doc->new_bounding_box[1])
      doc->new_bounding_box[1] = bounding_box[1];
    if (bounding_box[2] > doc->new_bounding_box[2])
      doc->new_bounding_box[2] = bounding_box[2];
    if (bounding_box[3] > doc->new_bounding_box[3])
      doc->new_bounding_box[3] = bounding_box[3];
  }

 /*
  * Output the page header as needed...
  */

  if (!doc->slow_order && first_page)
  {
    if ((!ppd || !ppd->num_filters) && log)
      log(ld, CF_LOGLEVEL_CONTROL,
	  "PAGE: %d %d", doc->page,
	  doc->slow_collate ? 1 : doc->copies);

    if (doc->number_up > 1)
    {
      doc_printf(doc, "%%%%Page: (%d) %d\n", doc->page, doc->page);
      doc_printf(doc, "%%%%PageBoundingBox: %.0f %.0f %.0f %.0f\n",
	      doc->PageLeft, doc->PageBottom, doc->PageRight, doc->PageTop);
    }
    else
    {
      doc_printf(doc, "%%%%Page: %s %d\n", pageinfo->label, doc->page);
      doc_printf(doc, "%%%%PageBoundingBox: %d %d %d %d\n",
	      pageinfo->bounding_box[0], pageinfo->bounding_box[1],
	      pageinfo->bounding_box[2], pageinfo->bounding_box[3]);
    }
  }

 /*
  * Copy any page setup commands...
  */

  if (first_page)
    doc_puts(doc, "%%BeginPageSetup\n");

  if (has_page_setup)
  {
    int	feature = 0;			/* In a Begin/EndFeature block? */

    while ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) >
	   0)
    {
      if (!strncmp(line, "%%EndPageSetup", 14))
	break;
      else if (!strncmp(line, "%%BeginFeature:", 15))
      {
	feature = 1;

	if (doc->number_up > 1 || doc->fit_to_page)
	  continue;
      }
      else if (!strncmp(line, "%%EndFeature", 12))
      {
	feature = 0;

	if (doc->number_up > 1 || doc->fit_to_page)
	  continue;
      }
      else if (!strncmp(line, "%%IncludeFeature:", 17))
      {
	pageinfo->num_options = include_feature(doc, ppd, line,
						pageinfo->num_options,
						&(pageinfo->options));
	continue;
      }
      else if (!strncmp(line, "%%Include", 9))
	continue;

      if (line[0] != '%' && !feature)
        break;

      if (!feature || (doc->number_up == 1 && !doc->fit_to_page))
	doc_write(doc, line, (size_t)linelen);
    }

   /*
    * Skip %%EndPageSetup...
    */

    if (linelen > 0 && !strncmp(line, "%%EndPageSetup", 14))
      linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize);
  }

  if (first_page)
  {
    char	*page_setup;		/* PageSetup commands to send */


    if (pageinfo->num_options > 0)
      write_options(doc, ppd, pageinfo->num_options, pageinfo->options);

   /*
    * Output commands for the current page...
    */

    page_setup = ppdEmitString(ppd, PPD_ORDER_PAGE, 0);

    if (page_setup)
    {
      doc_puts(doc, page_setup);
      free(page_setup);
    }
  }

 /*
  * Prep for the start of the page description...
  */

  start_nup(doc, number, 1, bounding_box);

  if (first_page)
    doc_puts(doc, "%%EndPageSetup\n");

 /*
  * Read the rest of the page description...
  */

  level = 0;

  do
  {
    if (level == 0 &&
        (!strncmp(line, "%%Page:", 7) ||
	 !strncmp(line, "%%Trailer", 9) ||
	 !strncmp(line, "%%EOF", 5)))
      break;
    else if (!strncmp(line, "%%BeginDocument", 15) ||
	     !strncmp(line, "%ADO_BeginApplication", 21))
    {
      doc_write(doc, line, (size_t)linelen);

      level ++;
    }
    else if ((!strncmp(line, "%%EndDocument", 13) ||
	      !strncmp(line, "%ADO_EndApplication", 19)) && level > 0)
    {
      doc_write(doc, line, (size_t)linelen);

      level --;
    }
    else if (!strncmp(line, "%%BeginBinary:", 14) ||
             (!strncmp(line, "%%BeginData:", 12) &&
	      !strstr(line, "ASCII") && !strstr(line, "Hex")))
    {
     /*
      * Copy binary data...
      */

      int	bytes;			/* Bytes of data */


      doc_write(doc, line, (size_t)linelen);

      bytes = atoi(strchr(line, ':') + 1);

      while (bytes > 0)
      {
	if ((size_t)bytes > linesize)
	  linelen = cupsFileRead(doc->inputfp, line, linesize);
	else
	  linelen = cupsFileRead(doc->inputfp, line, (size_t)bytes);

	if (linelen < 1)
	{
	  line[0] = '\0';
	  if (log)
	    log(ld, CF_LOGLEVEL_ERROR,
		"cfFilterPSToPS: Early end-of-file while reading binary data: %s",
		strerror(errno));
	  return (0);
	}

        doc_write(doc, line, (size_t)linelen);

	bytes -= linelen;
      }
    }
    else
      doc_write(doc, line, (size_t)linelen);
  }
  while ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) >
	 0);

 /*
  * Finish up this page and return...
  */

  end_nup(doc, number);

  pageinfo->length = (ssize_t)(cupsFileTell(doc->temp) - pageinfo->offset);

  return (linelen);
}


/*
 * 'copy_prolog()' - Copy the document prolog section.
 *
 * This function expects "line" to be filled with a %%BeginProlog comment line.
 * On return, "line" will contain the next line in the file, if any.
 */

static ssize_t				/* O - Length of next line */
copy_prolog(pstops_doc_t *doc,		/* I - Document info */
            ppd_file_t   *ppd,		/* I - PPD file */
	    char         *line,		/* I - Line buffer */
	    ssize_t      linelen,	/* I - Length of initial line */
	    size_t       linesize)	/* I - Size of line buffer */
{
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  while (strncmp(line, "%%BeginProlog", 13))
  {
    if (!strncmp(line, "%%BeginSetup", 12) || !strncmp(line, "%%Page:", 7))
      break;

    doc_write(doc, line, (size_t)linelen);

    if ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) == 0)
      break;
  }

  doc_puts(doc, "%%BeginProlog\n");

  do_prolog(doc, ppd);

  if (!strncmp(line, "%%BeginProlog", 13))
  {
    while ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) >
	   0)
    {
      if (!strncmp(line, "%%EndProlog", 11) ||
          !strncmp(line, "%%BeginSetup", 12) ||
          !strncmp(line, "%%Page:", 7))
        break;

      doc_write(doc, line, (size_t)linelen);
    }

    if (!strncmp(line, "%%EndProlog", 11))
      linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize);
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: The %%EndProlog comment is missing.");
  }

  doc_puts(doc, "%%EndProlog\n");

  return (linelen);
}


/*
 * 'copy_setup()' - Copy the document setup section.
 *
 * This function expects "line" to be filled with a %%BeginSetup comment line.
 * On return, "line" will contain the next line in the file, if any.
 */

static ssize_t				/* O - Length of next line */
copy_setup(pstops_doc_t *doc,		/* I - Document info */
           ppd_file_t   *ppd,		/* I - PPD file */
	   char         *line,		/* I - Line buffer */
	   ssize_t      linelen,	/* I - Length of initial line */
	   size_t       linesize)	/* I - Size of line buffer */
{
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  while (strncmp(line, "%%BeginSetup", 12))
  {
    if (!strncmp(line, "%%Page:", 7))
      break;

    doc_write(doc, line, (size_t)linelen);

    if ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) == 0)
      break;
  }

  doc_puts(doc, "%%BeginSetup\n");

  do_setup(doc, ppd);

  num_options = 0;
  options     = NULL;

  if (!strncmp(line, "%%BeginSetup", 12))
  {
    while (strncmp(line, "%%EndSetup", 10))
    {
      if (!strncmp(line, "%%Page:", 7))
        break;
      else if (!strncmp(line, "%%IncludeFeature:", 17))
      {
       /*
	* %%IncludeFeature: *MainKeyword OptionKeyword
	*/

        if (doc->number_up == 1 && !doc->fit_to_page)
	  num_options = include_feature(doc, ppd, line, num_options, &options);
      }
      else if (strncmp(line, "%%BeginSetup", 12))
        doc_write(doc, line, (size_t)linelen);

      if ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) ==
	  0)
	break;
    }

    if (!strncmp(line, "%%EndSetup", 10))
      linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize);
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPSToPS: The %%EndSetup comment is missing.");
  }

  if (num_options > 0)
  {
    write_options(doc, ppd, num_options, options);
    cupsFreeOptions(num_options, options);
  }

  doc_puts(doc, "%%EndSetup\n");

  return (linelen);
}


/*
 * 'copy_trailer()' - Copy the document trailer.
 *
 * This function expects "line" to be filled with a %%Trailer comment line.
 * On return, "line" will contain the next line in the file, if any.
 */

static ssize_t				/* O - Length of next line */
copy_trailer(pstops_doc_t *doc,		/* I - Document info */
             ppd_file_t   *ppd,		/* I - PPD file */
	     int          number,	/* I - Number of pages */
	     char         *line,	/* I - Line buffer */
	     ssize_t      linelen,	/* I - Length of initial line */
	     size_t       linesize)	/* I - Size of line buffer */
{
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


 /*
  * Write the trailer comments...
  */

  (void)ppd;

  doc_puts(doc, "%%Trailer\n");

  while (linelen > 0)
  {
    if (!strncmp(line, "%%EOF", 5))
      break;
    else if (strncmp(line, "%%Trailer", 9) &&
             strncmp(line, "%%Pages:", 8) &&
             strncmp(line, "%%BoundingBox:", 14))
      doc_write(doc, line, (size_t)linelen);

    linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize);
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: Wrote %d pages...", number);

  doc_printf(doc, "%%%%Pages: %d\n", number);
  if (doc->number_up > 1 || doc->fit_to_page)
    doc_printf(doc, "%%%%BoundingBox: %.0f %.0f %.0f %.0f\n",
	    doc->PageLeft, doc->PageBottom, doc->PageRight, doc->PageTop);
  else
    doc_printf(doc, "%%%%BoundingBox: %d %d %d %d\n",
	    doc->new_bounding_box[0], doc->new_bounding_box[1],
	    doc->new_bounding_box[2], doc->new_bounding_box[3]);

  return (linelen);
}


/*
 * 'do_prolog()' - Send the necessary document prolog commands.
 */

static void
do_prolog(pstops_doc_t *doc,		/* I - Document information */
          ppd_file_t   *ppd)		/* I - PPD file */
{
  char	*ps;				/* PS commands */


 /*
  * Send the document prolog commands...
  */

  if (ppd && ppd->patches)
  {
    doc_puts(doc, "%%BeginFeature: *JobPatchFile 1\n");
    doc_puts(doc, ppd->patches);
    doc_puts(doc, "\n%%EndFeature\n");
  }

  if ((ps = ppdEmitString(ppd, PPD_ORDER_PROLOG, 0.0)) != NULL)
  {
    doc_puts(doc, ps);
    free(ps);
  }

 /*
  * Define ESPshowpage here so that applications that define their
  * own procedure to do a showpage pick it up...
  */

  if (doc->use_ESPshowpage)
    doc_puts(doc, "userdict/ESPshowpage/showpage load put\n"
	          "userdict/showpage{}put\n");
}


/*
 * 'do_setup()' - Send the necessary document setup commands.
 */

static void
do_setup(pstops_doc_t *doc,		/* I - Document information */
         ppd_file_t   *ppd)		/* I - PPD file */
{
  char	*ps;				/* PS commands */


 /*
  * Disable CTRL-D so that embedded files don't cause printing
  * errors...
  */

  doc_puts(doc, "% Disable CTRL-D as an end-of-file marker...\n");
  doc_puts(doc, "userdict dup(\\004)cvn{}put (\\004\\004)cvn{}put\n");

 /*
  * Send all the printer-specific setup commands...
  */

  if ((ps = ppdEmitString(ppd, PPD_ORDER_DOCUMENT, 0.0)) != NULL)
  {
    doc_puts(doc, ps);
    free(ps);
  }

  if ((ps = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0)) != NULL)
  {
    doc_puts(doc, ps);
    free(ps);
  }

 /*
  * Set the number of copies for the job...
  */

  if (doc->copies != 1 && (!doc->collate || !doc->slow_collate))
  {
    doc_printf(doc, "%%RBIBeginNonPPDFeature: *NumCopies %d\n", doc->copies);
    doc_printf(doc,
               "%d/languagelevel where{pop languagelevel 2 ge}{false}ifelse\n"
               "{1 dict begin/NumCopies exch def currentdict end "
	       "setpagedevice}\n"
	       "{userdict/#copies 3 -1 roll put}ifelse\n", doc->copies);
    doc_puts(doc, "%RBIEndNonPPDFeature\n");
  }

 /*
  * If we are doing N-up printing, disable setpagedevice...
  */

  if (doc->number_up > 1)
  {
    doc_puts(doc, "userdict/CUPSsetpagedevice/setpagedevice load put\n");
    doc_puts(doc, "userdict/setpagedevice{pop}bind put\n");
  }

 /*
  * Make sure we have rectclip and rectstroke procedures of some sort...
  */

  write_common(doc);

 /*
  * Write the page and label prologs...
  */

  if (doc->number_up == 2 || doc->number_up == 6)
  {
   /*
    * For 2- and 6-up output, rotate the labels to match the orientation
    * of the pages...
    */

    if (doc->Orientation & 1)
      write_label_prolog(doc, doc->page_label, doc->PageBottom,
                         doc->PageWidth - doc->PageLength +
			 doc->PageTop, doc->PageLength);
    else
      write_label_prolog(doc, doc->page_label, doc->PageLeft, doc->PageRight,
                         doc->PageLength);
  }
  else
    write_label_prolog(doc, doc->page_label, doc->PageBottom, doc->PageTop,
		       doc->PageWidth);
}


/*
 * 'doc_printf()' - Send a formatted string to the output file and/or the
 *                  temp file.
 *
 * This function should be used for all page-level output that is affected
 * by ordering, collation, etc.
 */

static void
doc_printf(pstops_doc_t *doc,		/* I - Document information */
           const char   *format,	/* I - Printf-style format string */
	   ...)				/* I - Additional arguments as needed */
{
  va_list	ap;			/* Pointer to arguments */
  char		buffer[1024];		/* Output buffer */
  ssize_t	bytes;			/* Number of bytes to write */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  va_start(ap, format);
  bytes = vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  if ((size_t)bytes > sizeof(buffer))
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPSToPS: Buffer overflow detected, truncating.");
    bytes = sizeof(buffer);
  }

  doc_write(doc, buffer, (size_t)bytes);
}


/*
 * 'doc_putc()' - Send a single character to the output file and/or the
 *                temp file.
 *
 * This function should be used for all page-level output that is affected
 * by ordering, collation, etc.
 */

static void
doc_putc(pstops_doc_t *doc,		/* I - Document information */
         const char   c)		/* I - Character to send */
{
  doc_write(doc, &c, 1);
}


/*
 * 'doc_puts()' - Send a nul-terminated string to the output file and/or the
 *                temp file.
 *
 * This function should be used for all page-level output that is affected
 * by ordering, collation, etc.
 */

static void
doc_puts(pstops_doc_t *doc,		/* I - Document information */
         const char   *s)		/* I - String to send */
{
  doc_write(doc, s, strlen(s));
}


/*
 * 'doc_write()' - Send data to the output file and/or the temp file.
 */

static void
doc_write(pstops_doc_t *doc,		/* I - Document information */
          const char   *s,		/* I - Data to send */
	  size_t       len)		/* I - Number of bytes to send */
{
  if (!doc->slow_order)
    fwrite(s, 1, len, doc->outputfp);

  if (doc->temp)
    cupsFileWrite(doc->temp, s, len);
}


/*
 * 'end_nup()' - End processing for N-up printing.
 */

static void
end_nup(pstops_doc_t *doc,		/* I - Document information */
        int          number)		/* I - Page number */
{
  if (doc->number_up > 1)
    doc_puts(doc, "userdict/ESPsave get restore\n");

  switch (doc->number_up)
  {
    case 1 :
	if (doc->use_ESPshowpage)
	{
	  write_labels(doc, doc->Orientation);
          doc_puts(doc, "ESPshowpage\n");
	}
	break;

    case 2 :
    case 6 :
	if (is_last_page(number) && doc->use_ESPshowpage)
	{
	  if (doc->Orientation & 1)
	  {
	   /*
	    * Rotate the labels back to portrait...
	    */

	    write_labels(doc, doc->Orientation - 1);
	  }
	  else if (doc->Orientation == 0)
	  {
	   /*
	    * Rotate the labels to landscape...
	    */

	    write_labels(doc, doc->normal_landscape ? 1 : 3);
	  }
	  else
	  {
	   /*
	    * Rotate the labels to landscape...
	    */

	    write_labels(doc, doc->normal_landscape ? 3 : 1);
	  }

          doc_puts(doc, "ESPshowpage\n");
	}
        break;

    default :
	if (is_last_page(number) && doc->use_ESPshowpage)
	{
	  write_labels(doc, doc->Orientation);
          doc_puts(doc, "ESPshowpage\n");
	}
        break;
  }

  fflush(doc->outputfp);
}


/*
 * 'include_feature()' - Include a printer option/feature command.
 */

static int				/* O  - New number of options */
include_feature(
    pstops_doc_t  *doc,			/* I  - Document information */
    ppd_file_t    *ppd,			/* I  - PPD file */
    const char    *line,		/* I  - DSC line */
    int           num_options,		/* I  - Number of options */
    cups_option_t **options)		/* IO - Options */
{
  char		name[255],		/* Option name */
		value[255];		/* Option value */
  ppd_option_t	*option;		/* Option in file */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


 /*
  * Get the "%%IncludeFeature: *Keyword OptionKeyword" values...
  */

  if (sscanf(line + 17, "%254s%254s", name, value) != 2)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPSToPS: The %%IncludeFeature: comment is not valid.");
    return (num_options);
  }

 /*
  * Find the option and choice...
  */

  if ((option = ppdFindOption(ppd, name + 1)) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_WARN,
		 "cfFilterPSToPS: Unknown option \"%s\".", name + 1);
    return (num_options);
  }

  if (option->section == PPD_ORDER_EXIT ||
      option->section == PPD_ORDER_JCL)
  {
    if (log) log(ld, CF_LOGLEVEL_WARN,
		 "cfFilterPSToPS: Option \"%s\" cannot be included via "
		 "%%%%IncludeFeature.", name + 1);
    return (num_options);
  }

  if (!ppdFindChoice(option, value))
  {
    if (log) log(ld, CF_LOGLEVEL_WARN,
		 "cfFilterPSToPS: Unknown choice \"%s\" for option \"%s\".",
		 value, name + 1);
    return (num_options);
  }

 /*
  * Add the option to the option array and return...
  */

  return (cupsAddOption(name + 1, value, num_options, options));
}


/*
 * 'parse_text()' - Parse a text value in a comment.
 *
 * This function parses a DSC text value as defined on page 36 of the
 * DSC specification.  Text values are either surrounded by parenthesis
 * or whitespace-delimited.
 *
 * The value returned is the literal characters for the entire text
 * string, including any parenthesis and escape characters.
 */

static char *				/* O - Value or NULL on error */
parse_text(const char *start,		/* I - Start of text value */
           char       **end,		/* O - End of text value */
	   char       *buffer,		/* I - Buffer */
           size_t     bufsize)		/* I - Size of buffer */
{
  char	*bufptr,			/* Pointer in buffer */
	*bufend;			/* End of buffer */
  int	level;				/* Parenthesis level */


 /*
  * Skip leading whitespace...
  */

  while (isspace(*start & 255))
    start ++;

 /*
  * Then copy the value...
  */

  level  = 0;
  bufptr = buffer;
  bufend = buffer + bufsize - 1;

  while (*start && bufptr < bufend)
  {
    if (isspace(*start & 255) && !level)
      break;

    *bufptr++ = *start;

    if (*start == '(')
      level ++;
    else if (*start == ')')
    {
      if (!level)
      {
        start ++;
        break;
      }
      else
        level --;
    }
    else if (*start == '\\')
    {
     /*
      * Copy escaped character...
      */

      int	i;			/* Looping var */


      for (i = 1;
           i <= 3 && isdigit(start[i] & 255) && bufptr < bufend;
	   *bufptr++ = start[i], i ++);
    }

    start ++;
  }

  *bufptr = '\0';

 /*
  * Return the value and new pointer into the line...
  */

  if (end)
    *end = (char *)start;

  if (bufptr == bufend)
    return (NULL);
  else
    return (buffer);
}


/*
 * 'ps_hex()' - Print binary data as a series of hexadecimal numbers.
 */

static void
ps_hex(pstops_doc_t *doc,
       cf_ib_t *data,			/* I - Data to print */
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

    doc_putc(doc, hex[*data >> 4]);
    doc_putc(doc, hex[*data & 15]);

    data ++;
    length --;

    col += 2;
    if (col > 78)
    {
      doc_putc(doc, '\n');
      col = 0;
    }
  }

  if (last_line && col)
  {
    doc_putc(doc, '\n');
    col = 0;
  }
}


/*
 * 'ps_ascii85()' - Print binary data as a series of base-85 numbers.
 */

static void
ps_ascii85(pstops_doc_t *doc,
	   cf_ib_t *data,		/* I - Data to print */
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
      doc_putc(doc, 'z');
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

      doc_write(doc, (const char *)c, 5);
      col += 5;
    }

    data += 4;
    length -= 4;

    if (col >= 75)
    {
      doc_putc(doc, '\n');
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

      doc_write(doc, (const char *)c, length + 1);
    }

    doc_puts(doc, "~>\n");
    col = 0;
  }
}


/*
 * 'set_pstops_options()' - Set pstops options.
 */

static int
set_pstops_options(
    pstops_doc_t  *doc,			/* I - Document information */
    ppd_file_t    *ppd,			/* I - PPD file */
    int           job_id,               /* I - Job ID */
    char          *job_user,            /* I - Job User */
    char          *job_title,           /* I - Job Title */
    int           copies,               /* I - Number of copies */
    int           num_options,		/* I - Number of options */
    cups_option_t *options,		/* I - Options */
    cf_logfunc_t logfunc,           /* I - Logging function,
					       NULL for no logging */
    void *logdata,                      /* I - User data for logging function,
					       can be NULL */
    cf_filter_iscanceledfunc_t iscanceledfunc, /* I - Function returning 1 when
					       job is canceled, NULL for not
					       supporting stop on cancel */
    void *iscanceleddata)               /* I - User data for is-canceled
					       function, can be NULL */
{
  const char	*val;			/* Option value */
  int		intval;			/* Integer option value */
  ppd_attr_t	*attr;			/* PPD attribute */
  ppd_option_t	*option;		/* PPD option */
  ppd_choice_t	*choice;		/* PPD choice */
  const char	*content_type;		/* Original content type */
  int		max_copies;		/* Maximum number of copies supported */
  cf_logfunc_t log = logfunc;
  void          *ld = logdata;
  cf_filter_iscanceledfunc_t iscanceled = iscanceledfunc;
  void          *icd = iscanceleddata;


 /*
  * Initialize document information structure...
  */

  memset(doc, 0, sizeof(pstops_doc_t));

  /* Job info, to appear on the printer's front panel while printing */
  doc->job_id = (job_id > 0 ? job_id : 0);
  doc->user = (job_user ? job_user : "Unknown");
  doc->title = (job_title ? job_title : "");

  /* Number of copies, 1 if this filter should not handle copies */
  doc->copies = (copies > 0 ? copies : 1);

  /* Logging function */
  doc->logfunc = log;
  doc->logdata = ld;

  /* Job-is-canceled function */
  doc->iscanceledfunc = iscanceled;
  doc->iscanceleddata = icd;

  /* Set some common values */
  cfFilterSetCommonOptions(ppd, num_options, options, 1,
			 &doc->Orientation, &doc->Duplex,
			 &doc->LanguageLevel, &doc->Color,
			 &doc->PageLeft, &doc->PageRight,
			 &doc->PageTop, &doc->PageBottom,
			 &doc->PageWidth, &doc->PageLength,
			 log, ld);

  if (ppd && ppd->landscape > 0)
    doc->normal_landscape = 1;

  doc->bounding_box[0] = (int)(doc->PageLeft);
  doc->bounding_box[1] = (int)(doc->PageBottom);
  doc->bounding_box[2] = (int)(doc->PageRight);
  doc->bounding_box[3] = (int)(doc->PageTop);

  doc->new_bounding_box[0] = INT_MAX;
  doc->new_bounding_box[1] = INT_MAX;
  doc->new_bounding_box[2] = INT_MIN;
  doc->new_bounding_box[3] = INT_MIN;

 /*
  * AP_FIRSTPAGE_* and the corresponding non-first-page options.
  */

  doc->ap_input_slot  = cupsGetOption("AP_FIRSTPAGE_InputSlot", num_options,
                                      options);
  doc->ap_manual_feed = cupsGetOption("AP_FIRSTPAGE_ManualFeed", num_options,
                                      options);
  doc->ap_media_color = cupsGetOption("AP_FIRSTPAGE_MediaColor", num_options,
                                      options);
  doc->ap_media_type  = cupsGetOption("AP_FIRSTPAGE_MediaType", num_options,
                                      options);
  doc->ap_page_region = cupsGetOption("AP_FIRSTPAGE_PageRegion", num_options,
                                      options);
  doc->ap_page_size   = cupsGetOption("AP_FIRSTPAGE_PageSize", num_options,
                                      options);

  if ((choice = ppdFindMarkedChoice(ppd, "InputSlot")) != NULL)
    doc->input_slot = choice->choice;
  if ((choice = ppdFindMarkedChoice(ppd, "ManualFeed")) != NULL)
    doc->manual_feed = choice->choice;
  if ((choice = ppdFindMarkedChoice(ppd, "MediaColor")) != NULL)
    doc->media_color = choice->choice;
  if ((choice = ppdFindMarkedChoice(ppd, "MediaType")) != NULL)
    doc->media_type = choice->choice;
  if ((choice = ppdFindMarkedChoice(ppd, "PageRegion")) != NULL)
    doc->page_region = choice->choice;
  if ((choice = ppdFindMarkedChoice(ppd, "PageSize")) != NULL)
    doc->page_size = choice->choice;

 /*
  * collate, multiple-document-handling
  */

  if ((val = cupsGetOption("multiple-document-handling",
			   num_options, options)) != NULL)
  {
   /*
    * This IPP attribute is unnecessarily complicated...
    *
    *   single-document, separate-documents-collated-copies, and
    *   single-document-new-sheet all require collated copies.
    *
    *   separate-documents-uncollated-copies allows for uncollated copies.
    */

    doc->collate = strcasecmp(val, "separate-documents-uncollated-copies") != 0;
  }

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL &&
      (!strcasecmp(val, "true") ||!strcasecmp(val, "on") ||
       !strcasecmp(val, "yes")))
    doc->collate = 1;

 /*
  * emit-jcl
  */

  if ((val = cupsGetOption("emit-jcl", num_options, options)) != NULL &&
      (!strcasecmp(val, "false") || !strcasecmp(val, "off") ||
       !strcasecmp(val, "no") || !strcmp(val, "0")))
    doc->emit_jcl = 0;
  else
    doc->emit_jcl = 1;

 /*
  * fit-to-page/ipp-attribute-fidelity
  *
  * (Only for original PostScript content)
  */

  if ((content_type = getenv("CONTENT_TYPE")) == NULL)
    content_type = "application/postscript";

  if (!strcasecmp(content_type, "application/postscript"))
  {
    if ((val = cupsGetOption("fit-to-page", num_options, options)) != NULL &&
	!strcasecmp(val, "true"))
      doc->fit_to_page = 1;
    else if ((val = cupsGetOption("ipp-attribute-fidelity", num_options,
                                  options)) != NULL &&
	     !strcasecmp(val, "true"))
      doc->fit_to_page = 1;
  }

 /*
  * mirror/MirrorPrint
  */

  if ((choice = ppdFindMarkedChoice(ppd, "MirrorPrint")) != NULL)
  {
    val = choice->choice;
    choice->marked = 0;
  }
  else
    val = cupsGetOption("mirror", num_options, options);

  if (val && (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
              !strcasecmp(val, "yes")))
    doc->mirror = 1;

 /*
  * number-up
  */

  if ((val = cupsGetOption("number-up", num_options, options)) != NULL)
  {
    switch (intval = atoi(val))
    {
      case 1 :
      case 2 :
      case 4 :
      case 6 :
      case 9 :
      case 16 :
          doc->number_up = intval;
	  break;
      default :
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterPSToPS: Unsupported number-up value %d, using "
		       "number-up=1.", intval);
          doc->number_up = 1;
	  break;
    }
  }
  else
    doc->number_up = 1;

 /*
  * number-up-layout
  */

  if ((val = cupsGetOption("number-up-layout", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "lrtb"))
      doc->number_up_layout = PSTOPS_LAYOUT_LRTB;
    else if (!strcasecmp(val, "lrbt"))
      doc->number_up_layout = PSTOPS_LAYOUT_LRBT;
    else if (!strcasecmp(val, "rltb"))
      doc->number_up_layout = PSTOPS_LAYOUT_RLTB;
    else if (!strcasecmp(val, "rlbt"))
      doc->number_up_layout = PSTOPS_LAYOUT_RLBT;
    else if (!strcasecmp(val, "tblr"))
      doc->number_up_layout = PSTOPS_LAYOUT_TBLR;
    else if (!strcasecmp(val, "tbrl"))
      doc->number_up_layout = PSTOPS_LAYOUT_TBRL;
    else if (!strcasecmp(val, "btlr"))
      doc->number_up_layout = PSTOPS_LAYOUT_BTLR;
    else if (!strcasecmp(val, "btrl"))
      doc->number_up_layout = PSTOPS_LAYOUT_BTRL;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterPSToPS: Unsupported number-up-layout value %s, using "
		   "number-up-layout=lrtb.", val);
      doc->number_up_layout = PSTOPS_LAYOUT_LRTB;
    }
  }
  else
    doc->number_up_layout = PSTOPS_LAYOUT_LRTB;

 /*
  * OutputOrder
  */

  if ((val = cupsGetOption("OutputOrder", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "Reverse"))
      doc->output_order = 1;
  }
  else if (ppd)
  {
   /*
    * Figure out the right default output order from the PPD file...
    */

    if ((choice = ppdFindMarkedChoice(ppd, "OutputBin")) != NULL &&
        (attr = ppdFindAttr(ppd, "PageStackOrder", choice->choice)) != NULL &&
	attr->value)
      doc->output_order = !strcasecmp(attr->value, "Reverse");
    else if ((attr = ppdFindAttr(ppd, "DefaultOutputOrder", NULL)) != NULL &&
             attr->value)
      doc->output_order = !strcasecmp(attr->value, "Reverse");
  }

 /*
  * page-border
  */

  if ((val = cupsGetOption("page-border", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "none"))
      doc->page_border = PSTOPS_BORDERNONE;
    else if (!strcasecmp(val, "single"))
      doc->page_border = PSTOPS_BORDERSINGLE;
    else if (!strcasecmp(val, "single-thick"))
      doc->page_border = PSTOPS_BORDERSINGLE2;
    else if (!strcasecmp(val, "double"))
      doc->page_border = PSTOPS_BORDERDOUBLE;
    else if (!strcasecmp(val, "double-thick"))
      doc->page_border = PSTOPS_BORDERDOUBLE2;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterPSToPS: Unsupported page-border value %s, using "
		   "page-border=none.", val);
      doc->page_border = PSTOPS_BORDERNONE;
    }
  }
  else
    doc->page_border = PSTOPS_BORDERNONE;

 /*
  * page-label
  */

  doc->page_label = cupsGetOption("page-label", num_options, options);
 
 /*
 *  input-page-ranges
 */

  doc->inputPageRange=cupsGetOption("input-page-ranges",num_options,options);
  
 /*
  * page-ranges
  */

  doc->page_ranges = cupsGetOption("page-ranges", num_options, options);

 /*
  * page-set
  */

  doc->page_set = cupsGetOption("page-set", num_options, options);

 /*
  * Now figure out if we have to force collated copies, etc.
  */

  if ((attr = ppdFindAttr(ppd, "cupsMaxCopies", NULL)) != NULL)
    max_copies = atoi(attr->value);
  else if (ppd && ppd->manual_copies)
    max_copies = 1;
  else
    max_copies = 9999;

  if (doc->copies > max_copies)
    doc->collate = 1;
  else if (ppd && ppd->manual_copies && doc->Duplex && doc->copies > 1)
  {
   /*
    * Force collated copies when printing a duplexed document to
    * a non-PS printer that doesn't do hardware copy generation.
    * Otherwise the copies will end up on the front/back side of
    * each page.
    */

    doc->collate = 1;
  }

 /*
  * See if we have to filter the fast or slow way...
  */

  if (doc->collate && doc->copies > 1)
  {
   /*
    * See if we need to manually collate the pages...
    */

    doc->slow_collate = 1;

    if (doc->copies <= max_copies &&
        (choice = ppdFindMarkedChoice(ppd, "Collate")) != NULL &&
        !strcasecmp(choice->choice, "True"))
    {
     /*
      * Hardware collate option is selected, see if the option is
      * conflicting - if not, collate in hardware.  Otherwise,
      * turn the hardware collate option off...
      */

      if ((option = ppdFindOption(ppd, "Collate")) != NULL &&
          !option->conflicted)
	doc->slow_collate = 0;
      else
        ppdMarkOption(ppd, "Collate", "False");
    }
  }
  else
    doc->slow_collate = 0;

  if (!ppdFindOption(ppd, "OutputOrder") && doc->output_order)
    doc->slow_order = 1;
  else
    doc->slow_order = 0;

  if (doc->Duplex &&
       (doc->slow_collate || doc->slow_order ||
        ((attr = ppdFindAttr(ppd, "cupsEvenDuplex", NULL)) != NULL &&
	 attr->value && !strcasecmp(attr->value, "true"))))
    doc->slow_duplex = 1;
  else
    doc->slow_duplex = 0;

 /*
  * Create a temporary file for page data if we need to filter slowly...
  */

  if (doc->slow_order || doc->slow_collate)
  {
    if ((doc->temp = cupsTempFile2(doc->tempfile,
                                   sizeof(doc->tempfile))) == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterPSToPS: Unable to create temporary file: %s",
		   strerror(errno));
      return (1);
    }
  }

 /*
  * Figure out if we should use ESPshowpage or not...
  */

  if (doc->page_label || getenv("CLASSIFICATION") || doc->number_up > 1 ||
      doc->page_border)
  {
   /*
    * Yes, use ESPshowpage...
    */

    doc->use_ESPshowpage = 1;
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterPSToPS: slow_collate=%d, slow_duplex=%d, slow_order=%d",
	       doc->slow_collate, doc->slow_duplex, doc->slow_order);

  return(0);
}


/*
 * 'skip_page()' - Skip past a page that won't be printed.
 */

static ssize_t				/* O - Length of next line */
skip_page(pstops_doc_t *doc,		/* I - Document information */
          char        *line,		/* I - Line buffer */
	  ssize_t     linelen,		/* I - Length of initial line */
          size_t      linesize)		/* I - Size of line buffer */
{
  int	level;				/* Embedded document level */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  level = 0;

  while ((linelen = (ssize_t)cupsFileGetLine(doc->inputfp, line, linesize)) > 0)
  {
    if (level == 0 &&
        (!strncmp(line, "%%Page:", 7) || !strncmp(line, "%%Trailer", 9)))
      break;
    else if (!strncmp(line, "%%BeginDocument", 15) ||
	     !strncmp(line, "%ADO_BeginApplication", 21))
      level ++;
    else if ((!strncmp(line, "%%EndDocument", 13) ||
	      !strncmp(line, "%ADO_EndApplication", 19)) && level > 0)
      level --;
    else if (!strncmp(line, "%%BeginBinary:", 14) ||
             (!strncmp(line, "%%BeginData:", 12) &&
	      !strstr(line, "ASCII") && !strstr(line, "Hex")))
    {
     /*
      * Skip binary data...
      */

      ssize_t	bytes;			/* Bytes of data */

      bytes = atoi(strchr(line, ':') + 1);

      while (bytes > 0)
      {
	if ((size_t)bytes > linesize)
	  linelen = (ssize_t)cupsFileRead(doc->inputfp, line, linesize);
	else
	  linelen = (ssize_t)cupsFileRead(doc->inputfp, line, (size_t)bytes);

	if (linelen < 1)
	{
	  line[0] = '\0';
	  if (log)
	    log(ld, CF_LOGLEVEL_ERROR,
		"cfFilterPSToPS: Early end-of-file while reading binary data: %s",
		strerror(errno));
	  return (0);
	}

	bytes -= linelen;
      }
    }
  }

  return (linelen);
}


/*
 * 'start_nup()' - Start processing for N-up printing.
 */

static void
start_nup(pstops_doc_t *doc,		/* I - Document information */
          int          number,		/* I - Page number */
	  int          show_border,	/* I - Show the border? */
	  const int    *bounding_box)	/* I - BoundingBox value */
{
  int		pos;			/* Position on page */
  int		x, y;			/* Relative position of subpage */
  double	w, l,			/* Width and length of subpage */
		tx, ty;			/* Translation values for subpage */
  double	pagew,			/* Printable width of page */
		pagel;			/* Printable height of page */
  int		bboxx,			/* BoundingBox X origin */
		bboxy,			/* BoundingBox Y origin */
		bboxw,			/* BoundingBox width */
		bboxl;			/* BoundingBox height */
  double	margin = 0;		/* Current margin for border */
  cf_logfunc_t log = doc->logfunc;
  void          *ld = doc->logdata;


  if (doc->number_up > 1)
    doc_puts(doc, "userdict/ESPsave save put\n");

  pos   = (number - 1) % doc->number_up;
  pagew = doc->PageRight - doc->PageLeft;
  pagel = doc->PageTop - doc->PageBottom;

  if (doc->fit_to_page)
  {
    bboxx = bounding_box[0];
    bboxy = bounding_box[1];
    bboxw = bounding_box[2] - bounding_box[0];
    bboxl = bounding_box[3] - bounding_box[1];
  }
  else
  {
    bboxx = 0;
    bboxy = 0;
    bboxw = (int)(doc->PageWidth);
    bboxl = (int)(doc->PageLength);
  }

  if (log)
  {
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: pagew = %.1f, pagel = %.1f", pagew, pagel);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: bboxx = %d, bboxy = %d, bboxw = %d, bboxl = %d",
	bboxx, bboxy, bboxw, bboxl);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: PageLeft = %.1f, PageRight = %.1f",
	doc->PageLeft, doc->PageRight);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: PageTop = %.1f, PageBottom = %.1f",
	doc->PageTop, doc->PageBottom);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterPSToPS: PageWidth = %.1f, PageLength = %.1f",
	doc->PageWidth, doc->PageLength);
  }

  switch (doc->Orientation)
  {
    case 1 : /* Landscape */
        doc_printf(doc, "%.1f 0.0 translate 90 rotate\n", doc->PageLength);
        break;
    case 2 : /* Reverse Portrait */
        doc_printf(doc, "%.1f %.1f translate 180 rotate\n", doc->PageWidth,
	           doc->PageLength);
        break;
    case 3 : /* Reverse Landscape */
        doc_printf(doc, "0.0 %.1f translate -90 rotate\n", doc->PageWidth);
        break;
  }

 /*
  * Mirror the page as needed...
  */

  if (doc->mirror)
    doc_printf(doc, "%.1f 0.0 translate -1 1 scale\n", doc->PageWidth);

 /*
  * Offset and scale as necessary for fit_to_page/fit-to-page/number-up...
  */

  if (doc->Duplex && doc->number_up > 1 && ((number / doc->number_up) & 1))
    doc_printf(doc, "%.1f %.1f translate\n", doc->PageWidth - doc->PageRight,
	       doc->PageBottom);
  else if (doc->number_up > 1 || doc->fit_to_page)
    doc_printf(doc, "%.1f %.1f translate\n", doc->PageLeft, doc->PageBottom);

  switch (doc->number_up)
  {
    default :
        if (doc->fit_to_page)
	{
          w = pagew;
          l = w * bboxl / bboxw;

          if (l > pagel)
          {
            l = pagel;
            w = l * bboxw / bboxl;
          }

          tx = 0.5 * (pagew - w);
          ty = 0.5 * (pagel - l);

	  doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n", tx, ty,
	             w / bboxw, l / bboxl);
	}
	else
          w = doc->PageWidth;
	break;

    case 2 :
        if (doc->Orientation & 1)
	{
          x = pos & 1;

          if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	    x = 1 - x;

          w = pagel;
          l = w * bboxl / bboxw;

          if (l > (pagew * 0.5))
          {
            l = pagew * 0.5;
            w = l * bboxw / bboxl;
          }

          tx = 0.5 * (pagew * 0.5 - l);
          ty = 0.5 * (pagel - w);

          if (doc->normal_landscape)
            doc_printf(doc, "0.0 %.1f translate -90 rotate\n", pagel);
	  else
	    doc_printf(doc, "%.1f 0.0 translate 90 rotate\n", pagew);

          doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
                     ty, tx + pagew * 0.5 * x, w / bboxw, l / bboxl);
        }
	else
	{
          x = pos & 1;

          if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	    x = 1 - x;

          l = pagew;
          w = l * bboxw / bboxl;

          if (w > (pagel * 0.5))
          {
            w = pagel * 0.5;
            l = w * bboxl / bboxw;
          }

          tx = 0.5 * (pagel * 0.5 - w);
          ty = 0.5 * (pagew - l);

          if (doc->normal_landscape)
	    doc_printf(doc, "%.1f 0.0 translate 90 rotate\n", pagew);
	  else
            doc_printf(doc, "0.0 %.1f translate -90 rotate\n", pagel);

          doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
                     tx + pagel * 0.5 * x, ty, w / bboxw, l / bboxl);
        }
        break;

    case 4 :
        if (doc->number_up_layout & PSTOPS_LAYOUT_VERTICAL)
	{
	  x = (pos / 2) & 1;
          y = pos & 1;
        }
	else
	{
          x = pos & 1;
	  y = (pos / 2) & 1;
        }

        if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	  x = 1 - x;

	if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	  y = 1 - y;

        w = pagew * 0.5;
	l = w * bboxl / bboxw;

	if (l > (pagel * 0.5))
	{
	  l = pagel * 0.5;
	  w = l * bboxw / bboxl;
	}

        tx = 0.5 * (pagew * 0.5 - w);
        ty = 0.5 * (pagel * 0.5 - l);

	doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
	           tx + x * pagew * 0.5, ty + y * pagel * 0.5,
	           w / bboxw, l / bboxl);
        break;

    case 6 :
        if (doc->Orientation & 1)
	{
	  if (doc->number_up_layout & PSTOPS_LAYOUT_VERTICAL)
	  {
	    x = pos / 3;
	    y = pos % 3;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	      x = 1 - x;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	      y = 2 - y;
	  }
	  else
	  {
	    x = pos & 1;
	    y = pos / 2;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	      x = 1 - x;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	      y = 2 - y;
	  }

          w = pagel * 0.5;
          l = w * bboxl / bboxw;

          if (l > (pagew * 0.333))
          {
            l = pagew * 0.333;
            w = l * bboxw / bboxl;
          }

          tx = 0.5 * (pagel - 2 * w);
          ty = 0.5 * (pagew - 3 * l);

          if (doc->normal_landscape)
            doc_printf(doc, "0 %.1f translate -90 rotate\n", pagel);
	  else
	    doc_printf(doc, "%.1f 0 translate 90 rotate\n", pagew);

          doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
                     tx + x * w, ty + y * l, l / bboxl, w / bboxw);
        }
	else
	{
	  if (doc->number_up_layout & PSTOPS_LAYOUT_VERTICAL)
	  {
	    x = pos / 2;
	    y = pos & 1;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	      x = 2 - x;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	      y = 1 - y;
	  }
	  else
	  {
	    x = pos % 3;
	    y = pos / 3;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	      x = 2 - x;

            if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	      y = 1 - y;
	  }

          l = pagew * 0.5;
          w = l * bboxw / bboxl;

          if (w > (pagel * 0.333))
          {
            w = pagel * 0.333;
            l = w * bboxl / bboxw;
          }

	  tx = 0.5 * (pagel - 3 * w);
	  ty = 0.5 * (pagew - 2 * l);

          if (doc->normal_landscape)
	    doc_printf(doc, "%.1f 0 translate 90 rotate\n", pagew);
	  else
            doc_printf(doc, "0 %.1f translate -90 rotate\n", pagel);

          doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
                     tx + w * x, ty + l * y, w / bboxw, l / bboxl);

        }
        break;

    case 9 :
        if (doc->number_up_layout & PSTOPS_LAYOUT_VERTICAL)
	{
	  x = (pos / 3) % 3;
          y = pos % 3;
        }
	else
	{
          x = pos % 3;
	  y = (pos / 3) % 3;
        }

        if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	  x = 2 - x;

	if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	  y = 2 - y;

        w = pagew * 0.333;
	l = w * bboxl / bboxw;

	if (l > (pagel * 0.333))
	{
	  l = pagel * 0.333;
	  w = l * bboxw / bboxl;
	}

        tx = 0.5 * (pagew * 0.333 - w);
        ty = 0.5 * (pagel * 0.333 - l);

	doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
	           tx + x * pagew * 0.333, ty + y * pagel * 0.333,
	           w / bboxw, l / bboxl);
        break;

    case 16 :
        if (doc->number_up_layout & PSTOPS_LAYOUT_VERTICAL)
	{
	  x = (pos / 4) & 3;
          y = pos & 3;
        }
	else
	{
          x = pos & 3;
	  y = (pos / 4) & 3;
        }

        if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEX)
	  x = 3 - x;

	if (doc->number_up_layout & PSTOPS_LAYOUT_NEGATEY)
	  y = 3 - y;

        w = pagew * 0.25;
	l = w * bboxl / bboxw;

	if (l > (pagel * 0.25))
	{
	  l = pagel * 0.25;
	  w = l * bboxw / bboxl;
	}

        tx = 0.5 * (pagew * 0.25 - w);
        ty = 0.5 * (pagel * 0.25 - l);

	doc_printf(doc, "%.1f %.1f translate %.3f %.3f scale\n",
	           tx + x * pagew * 0.25, ty + y * pagel * 0.25,
	           w / bboxw, l / bboxl);
        break;
  }

 /*
  * Draw borders as necessary...
  */

  if (doc->page_border && show_border)
  {
    int		rects;			/* Number of border rectangles */
    double	fscale;			/* Scaling value for points */


    rects  = (doc->page_border & PSTOPS_BORDERDOUBLE) ? 2 : 1;
    fscale = doc->PageWidth / w;
    margin = 2.25 * fscale;

   /*
    * Set the line width and color...
    */

    doc_puts(doc, "gsave\n");
    doc_printf(doc, "%.3f setlinewidth 0 setgray newpath\n",
               (doc->page_border & PSTOPS_BORDERTHICK) ? 0.5 * fscale :
	                                                 0.24 * fscale);

   /*
    * Draw border boxes...
    */

    for (; rects > 0; rects --, margin += 2 * fscale)
      if (doc->number_up > 1)
	doc_printf(doc, "%.1f %.1f %.1f %.1f ESPrs\n",
		   margin,
		   margin,
		   bboxw - 2 * margin,
		   bboxl - 2 * margin);
      else
	doc_printf(doc, "%.1f %.1f %.1f %.1f ESPrs\n",
        	   doc->PageLeft + margin,
		   doc->PageBottom + margin,
		   doc->PageRight - doc->PageLeft - 2 * margin,
		   doc->PageTop - doc->PageBottom - 2 * margin);

   /*
    * Restore pen settings...
    */

    doc_puts(doc, "grestore\n");
  }

  if (doc->fit_to_page)
  {
   /*
    * Offset the page by its bounding box...
    */

    doc_printf(doc, "%d %d translate\n", -bounding_box[0],
               -bounding_box[1]);
  }

  if (doc->fit_to_page || doc->number_up > 1)
  {
   /*
    * Clip the page to the page's bounding box...
    */

    doc_printf(doc, "%.1f %.1f %.1f %.1f ESPrc\n",
               bboxx + margin, bboxy + margin,
               bboxw - 2 * margin, bboxl - 2 * margin);
  }
}


/*
 * 'write_common()' - Write common procedures...
 */

static void
write_common(pstops_doc_t *doc)
{
  doc_puts(doc,
           "% x y w h ESPrc - Clip to a rectangle.\n"
	   "userdict/ESPrc/rectclip where{pop/rectclip load}\n"
	   "{{newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
	   "neg 0 rlineto closepath clip newpath}bind}ifelse put\n");
  doc_puts(doc,
           "% x y w h ESPrf - Fill a rectangle.\n"
	   "userdict/ESPrf/rectfill where{pop/rectfill load}\n"
	   "{{gsave newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
	   "neg 0 rlineto closepath fill grestore}bind}ifelse put\n");
  doc_puts(doc,
           "% x y w h ESPrs - Stroke a rectangle.\n"
	   "userdict/ESPrs/rectstroke where{pop/rectstroke load}\n"
	   "{{gsave newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
	   "neg 0 rlineto closepath stroke grestore}bind}ifelse put\n");
}


/*
 * 'write_label_prolog()' - Write the prolog with the classification
 *                          and page label.
 */

static void
write_label_prolog(pstops_doc_t *doc,	/* I - Document info */
                   const char   *label,	/* I - Page label */
		   float        bottom,	/* I - Bottom position in points */
		   float        top,	/* I - Top position in points */
		   float        width)	/* I - Width in points */
{
  const char	*classification;	/* CLASSIFICATION environment
					   variable */
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
    doc_puts(doc, "userdict/ESPwl{}bind put\n");
    return;
  }

 /*
  * Set the classification + page label string...
  */

  doc_puts(doc, "userdict");
  if (!strcmp(classification, "confidential"))
    doc_puts(doc, "/ESPpl(CONFIDENTIAL");
  else if (!strcmp(classification, "classified"))
    doc_puts(doc, "/ESPpl(CLASSIFIED");
  else if (!strcmp(classification, "secret"))
    doc_puts(doc, "/ESPpl(SECRET");
  else if (!strcmp(classification, "topsecret"))
    doc_puts(doc, "/ESPpl(TOP SECRET");
  else if (!strcmp(classification, "unclassified"))
    doc_puts(doc, "/ESPpl(UNCLASSIFIED");
  else
  {
    doc_puts(doc, "/ESPpl(");

    for (ptr = classification; *ptr; ptr ++)
    {
      if (*ptr < 32 || *ptr > 126)
        doc_printf(doc, "\\%03o", *ptr);
      else if (*ptr == '_')
        doc_puts(doc, " ");
      else if (*ptr == '(' || *ptr == ')' || *ptr == '\\')
	doc_printf(doc, "\\%c", *ptr);
      else
        doc_printf(doc, "%c", *ptr);
    }
  }

  if (label)
  {
    if (classification[0])
      doc_puts(doc, " - ");

   /*
    * Quote the label string as needed...
    */

    for (ptr = label; *ptr; ptr ++)
    {
      if (*ptr < 32 || *ptr > 126)
        doc_printf(doc, "\\%03o", *ptr);
      else if (*ptr == '(' || *ptr == ')' || *ptr == '\\')
	doc_printf(doc, "\\%c", *ptr);
      else
        doc_printf(doc, "%c", *ptr);
    }
  }

  doc_puts(doc, ")put\n");

 /*
  * Then get a 14 point Helvetica-Bold font...
  */

  doc_puts(doc, "userdict/ESPpf /Helvetica-Bold findfont 14 scalefont put\n");

 /*
  * Finally, the procedure to write the labels on the page...
  */

  doc_puts(doc, "userdict/ESPwl{\n");
  doc_puts(doc, "  ESPpf setfont\n");
  doc_printf(doc, "  ESPpl stringwidth pop dup 12 add exch -0.5 mul %.0f add\n",
             width * 0.5f);
  doc_puts(doc, "  1 setgray\n");
  doc_printf(doc, "  dup 6 sub %.0f 3 index 20 ESPrf\n", bottom - 2.0);
  doc_printf(doc, "  dup 6 sub %.0f 3 index 20 ESPrf\n", top - 18.0);
  doc_puts(doc, "  0 setgray\n");
  doc_printf(doc, "  dup 6 sub %.0f 3 index 20 ESPrs\n", bottom - 2.0);
  doc_printf(doc, "  dup 6 sub %.0f 3 index 20 ESPrs\n", top - 18.0);
  doc_printf(doc, "  dup %.0f moveto ESPpl show\n", bottom + 2.0);
  doc_printf(doc, "  %.0f moveto ESPpl show\n", top - 14.0);
  doc_puts(doc, "pop\n");
  doc_puts(doc, "}bind put\n");
}


/*
 * 'write_labels()' - Write the actual page labels.
 *
 * This function is a copy of the one in common.c since we need to
 * use doc_puts/doc_printf instead of puts/printf...
 */

static void
write_labels(pstops_doc_t *doc,		/* I - Document information */
             int          orient)	/* I - Orientation of the page */
{
  float	width,				/* Width of page */
	length;				/* Length of page */


  doc_puts(doc, "gsave\n");

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
        doc_printf(doc, "%.1f 0.0 translate 90 rotate\n", length);
        break;
    case 2 : /* Reverse Portrait */
        doc_printf(doc, "%.1f %.1f translate 180 rotate\n", width, length);
        break;
    case 3 : /* Reverse Landscape */
        doc_printf(doc, "0.0 %.1f translate -90 rotate\n", width);
        break;
  }

  doc_puts(doc, "ESPwl\n");
  doc_puts(doc, "grestore\n");
}


/*
 * 'write_labels_outputfile_only()' - Write the actual page labels to the
 *                                    output file.
 *
 * This function is a copy of the one in common.c. Since we use it only
 * in this filter, we have moved it to here.
 */

static void
write_labels_outputfile_only(pstops_doc_t *doc,/* I - Document information */
		             int        orient)/* I - Orientation of the page */
{
  float	width,				/* Width of page */
	length;				/* Length of page */


  doc_puts(doc, "gsave\n");

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
        doc_printf(doc, "%.1f 0.0 translate 90 rotate\n", length);
        break;
    case 2 : /* Reverse Portrait */
        doc_printf(doc, "%.1f %.1f translate 180 rotate\n",
		width, length);
        break;
    case 3 : /* Reverse Landscape */
        doc_printf(doc, "0.0 %.1f translate -90 rotate\n", width);
        break;
  }

  doc_puts(doc, "ESPwl\n");
  doc_puts(doc, "grestore\n");
}


/*
 * 'write_options()' - Write options provided via %%IncludeFeature.
 */

static void
write_options(
    pstops_doc_t  *doc,		/* I - Document */
    ppd_file_t    *ppd,		/* I - PPD file */
    int           num_options,	/* I - Number of options */
    cups_option_t *options)	/* I - Options */
{
  int		i;		/* Looping var */
  ppd_option_t	*option;	/* PPD option */
  float		min_order;	/* Minimum OrderDependency value */
  char		*doc_setup,	/* DocumentSetup commands to send */
		*any_setup;	/* AnySetup commands to send */


 /*
  * Figure out the minimum OrderDependency value...
  */

  if ((option = ppdFindOption(ppd, "PageRegion")) != NULL)
    min_order = option->order;
  else
    min_order = 999.0f;

  for (i = 0; i < num_options; i ++)
    if ((option = ppdFindOption(ppd, options[i].name)) != NULL &&
	option->order < min_order)
      min_order = option->order;

 /*
  * Mark and extract them...
  */

  ppdMarkOptions(ppd, num_options, options);

  doc_setup = ppdEmitString(ppd, PPD_ORDER_DOCUMENT, min_order);
  any_setup = ppdEmitString(ppd, PPD_ORDER_ANY, min_order);

 /*
  * Then send them out...
  */

  if (doc->number_up > 1)
  {
   /*
    * Temporarily restore setpagedevice so we can set the options...
    */

    doc_puts(doc, "userdict/setpagedevice/CUPSsetpagedevice load put\n");
  }

  if (doc_setup)
  {
    doc_puts(doc, doc_setup);
    free(doc_setup);
  }

  if (any_setup)
  {
    doc_puts(doc, any_setup);
    free(any_setup);
  }

  if (doc->number_up > 1)
  {
   /*
    * Disable setpagedevice again...
    */

    doc_puts(doc, "userdict/setpagedevice{pop}bind put\n");
  }
}


/*
 * 'write_text_comment()' - Write a DSC text comment.
 */

static void
write_text_comment(pstops_doc_t *doc,	/* I - Document */
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

  doc_printf(doc, "%%%%%s: (", name);
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

      doc_printf(doc, "\\%03o", *value & 255);
      len += 4;
    }
    else if (*value == '\\')
    {
     /*
      * Escape the backslash...
      */

      if (len >= 253)			/* Keep line < 254 chars */
        break;

      doc_putc(doc, '\\');
      doc_putc(doc, '\\');
      len += 2;
    }
    else
    {
     /*
      * Put this character literally...
      */

      if (len >= 254)			/* Keep line < 254 chars */
        break;

      doc_putc(doc, *value);
      len ++;
    }

    value ++;
  }

  doc_puts(doc, ")\n");
}

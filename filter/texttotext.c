/*
 *   texttotext
 *
 *   Filter to print text files on text-only printers. The filter has
 *   several configuration options controlled by a PPD file so that it
 *   should work with most printer models.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2006 by Easy Software Products.
 *   Copyright 2011-2016 by Till Kamppeter
 *
 *   Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

/*
 * Include necessary headers...
 */

#include <config.h>
#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/file.h>
#include <signal.h>
#include <sys/wait.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <iconv.h>
#include <cupsfilters/image-private.h>

/*
 * Type definitions
 */

typedef enum overlong_line_e {
  TRUNCATE = 0,
  WORDWRAP = 1,
  WRAPATWIDTH = 2
} overlong_line_t;

typedef enum newline_char_e {
  LF = 0,
  CR = 1,
  CRLF = 2
} newline_char_t;


/*
 * Local functions...
 */

static int              is_true(const char *value);
static int              is_false(const char *value);
static int		check_range(char *page_ranges, int even_pages,
				    int odd_pages, int page);
static void		cancel_job(int sig);


/*
 * Local globals...
 */

static int		job_canceled = 0;


/*
 * 'main()' - Main entry for filter...
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i, j;			/* Looping vars */
  char          *p;
  int		exit_status = 0;	/* Exit status */
  int		fd = 0;			/* Copy file descriptor */
  char		*filename,		/* Text file to convert */
		tempfile[1024];		/* Temporary file */
  char		buffer[8192];		/* Copy buffer */
  int		bytes;			/* Bytes copied */
  int           num_copies;             /* Number of copies */
  ppd_file_t	*ppd;			/* PPD file */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  const char	*val, *val2;			/* Option value */
  ppd_attr_t    *ppd_attr;              /* Attribute taken from the PPD */
  int           num_lines = 66,         /* Lines per page */
                num_columns = 80;       /* Characters per line */
  int           page_left = 0,          /* Page margins */
                page_right = 0,
                page_top = 0,
                page_bottom = 0;
  int           text_width,             /* Width of the text area on the page */
                text_height;            /* Height of the text area on the
					   page */
  char          encoding[64];           /* The printer'a encoding, to which
					   the incoming UTF-8 is converted,
					   must be 1-byte-per-character */
  overlong_line_t overlong_lines = WRAPATWIDTH;
                                        /* How to treat overlong lines */
  int           tab_width = 8;          /* Turn tabs to spaces with given
					   width */
  int           pagination = 1;         /* Paginate text to allow margins
                                           and page management */
  int           send_ff = 1;            /* Send form-feed char at the end of
					   each page */
  newline_char_t newline_char = CRLF;   /* Character to send at end of line */
  char          *newline_char_str;
  char          *page_ranges = NULL;    /* Selection of pages to print */
  int           even_pages = 1;         /* Print the even pages */
  int           odd_pages = 1;          /* Print the odd pages */
  int           reverse_order = 0;      /* Ouput pages in reverse order? */
  int           collate = 1;            /* Collate multiple copies? */
  int           page_size;              /* Number of bytes needed for a page,
					   to allocate the memory for the
					   output page buffer */
  char          *out_page = NULL;       /* Output page buffer */
  cups_array_t  *page_array = NULL;     /* Array to hold the output pages
					   for collated copies and reverse
					   output order */
  iconv_t       cd;                     /* Conversion descriptor, describes
					   between which encodings iconv
					   should convert */
  char          outbuf[4096];           /* Output buffer for iconv */
  char          inbuf[2048];            /* Input buffer for iconv */
  size_t        outsize;                /* Space left in outbuf */
  size_t        insize = 0;             /* Bytes still to be converted in
					   inbuf */
  char          *outptr = outbuf;       /* Pointer for next converted
					   character to be dropped */
  char          *inptr = inbuf;         /* Pointer to next character to be
					   converted */
  size_t        nread;                  /* Number of bytes read from file */
  size_t        nconv;                  /* -1 on conversion error */
  int           incomplete_char = 0;    /* Was last character in input buffer
					   incomplete */
  int           result = 0;             /* Conversion result (-1 on error) */
  char          *procptr,               /* Pointer into conversion output
                                           to indicate next byte to process
					   for output page creation */
                *destptr;               /* Pointer into output page buffer
                                           where next character will be put */
  int           page,                   /* Number of current output page */
                line, column;           /* Character coordiantes on output
					   page */
  int           page_empty;             /* Is the current output page still
					   empty (no visible characters)? */
  int           previous_is_cr;         /* Is the previous character processed
					   a Carriage Return? */
  int           skip_rest_of_line = 0;  /* Are we truncating an overlong
					   line? */
  int           skip_spaces = 0;        /* Are we skipping spaces at a line
					   break when word-wrapping? */
  char          *wrapped_word = NULL;   /* Section of a word wrapped over
					   to the next line */
  int           num_pages = 0;          /* Number of pages which get actually
					   printed */
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
  * Make sure we have the right number of arguments for CUPS!
  */

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job user title copies options [file]\n",
	    argv[0]);
    return (1);
  }

 /*
  * Register a signal handler to cleanly cancel a job.
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif /* HAVE_SIGSET */

 /*
  * Copy stdin if needed...
  */

  if (argc == 6)
  {
   /*
    * Copy stdin to a temp file...
    */

    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      fprintf(stderr, "ERROR: Unable to copy input text file");
      goto error;
    }

    fprintf(stderr, "DEBUG: sys5ippprinter - copying to temp print file \"%s\"\n",
            tempfile);

    while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
      bytes = write(fd, buffer, bytes);

    close(fd);

    filename = tempfile;
  }
  else
  {
   /*
    * Use the filename on the command-line...
    */

    filename    = argv[6];
    tempfile[0] = '\0';
  }

 /*
  * Number of copies
  */

  num_copies = atoi(argv[4]);
  if (num_copies < 1)
    num_copies = 1;

 /*
  * Get the options from the fifth command line argument
  */

  num_options = cupsParseOptions(argv[5], 0, &options);

 /*
  * Load the PPD file and mark options...
  */

  ppd = ppdOpenFile(getenv("PPD"));
  if (ppd)
  {
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, num_options, options);
  }

 /*
  * Parse the options
  */

  /* With the "PageSize"/"PageRegion" options we only determine the number
     of lines and columns of a page, we do not use the geometry defined by
     "PaperDimension" and "ImageableArea" in the PPD */
  if ((val = cupsGetOption("PageSize", num_options, options)) != NULL ||
      (val = cupsGetOption("PageRegion", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultPageSize", NULL)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultPageRegion", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    fprintf(stderr, "DEBUG: PageSize: %s\n", val);
    snprintf(buffer, sizeof(buffer), "Default%sNumLines", val);
    if ((val2 = cupsGetOption(buffer + 7, num_options, options)) != NULL ||
	(ppd_attr = ppdFindAttr(ppd, buffer, NULL)) != NULL) {
      if (val2 == NULL)
	val2 = ppd_attr->value;
      if (!strncasecmp(val2, "Custom.", 7))
	val2 += 7;
      num_lines = atoi(val2);
    }
    snprintf(buffer, sizeof(buffer), "Default%sNumColumns", val);
    if ((val2 = cupsGetOption(buffer + 7, num_options, options)) != NULL ||
	(ppd_attr = ppdFindAttr(ppd, buffer, NULL)) != NULL) {
      if (val2 == NULL)
	val2 = ppd_attr->value;
      if (!strncasecmp(val2, "Custom.", 7))
	val2 += 7;
      num_columns = atoi(val2);
    }
    if (num_lines <= 0) {
      fprintf(stderr, "DEBUG: Invalid number of lines %d, using default: 66\n",
	      num_lines);
      num_lines = 66;
    }
    if (num_columns <= 0) {
      fprintf(stderr, "DEBUG: Invalid number of columns %d, using default: 80\n",
	      num_columns);
      num_columns = 80;
    }
  }

  /* Direct specification of number of lines/columns, mainly for debugging
     and development */
  if ((val = cupsGetOption("page-height", num_options, options)) != NULL) {
    i = atoi(val);
    if (i > 0)
      num_lines = i;
    else
      fprintf(stderr, "DEBUG: Invalid number of lines %d, using default value: %d\n",
	      i, num_lines);
  }
  if ((val = cupsGetOption("page-width", num_options, options)) != NULL) {
    i = atoi(val);
    if (i > 0)
      num_columns = i;
    else
      fprintf(stderr, "DEBUG: Invalid number of columns %d, using default value: %d\n",
	      i, num_columns);
  }

  fprintf(stderr, "DEBUG: Lines per page: %d; Characters per line: %d\n",
	  num_lines, num_columns);
  
  if ((val = cupsGetOption("page-left", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "Defaultpage-left", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strncasecmp(val, "Custom.", 7))
      val += 7;
    page_left = atoi(val);
    if (page_left < 0 || page_left > num_columns - 1) {
      fprintf(stderr, "DEBUG: Invalid left margin %d, setting to 0.\n",
	      page_left);
      page_left = 0;
    }
  }
  if ((val = cupsGetOption("page-right", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "Defaultpage-right", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strncasecmp(val, "Custom.", 7))
      val += 7;
    page_right = atoi(val);
    if (page_right < 0 || page_right > num_columns - page_left - 1) {
      fprintf(stderr, "DEBUG: Invalid right margin %d, setting to 0.\n",
	      page_right);
      page_right = 0;
    }
  }
  if ((val = cupsGetOption("page-top", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "Defaultpage-top", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strncasecmp(val, "Custom.", 7))
      val += 7;
    page_top = atoi(val);
    if (page_top < 0 || page_top > num_lines - 1) {
      fprintf(stderr, "DEBUG: Invalid top margin %d, setting to 0.\n",
	      page_top);
      page_top = 0;
    }
  }
  if ((val = cupsGetOption("page-bottom", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "Defaultpage-bottom", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strncasecmp(val, "Custom.", 7))
      val += 7;
    page_bottom = atoi(val);
    if (page_bottom < 0 || page_bottom > num_lines - page_top - 1) {
      fprintf(stderr, "DEBUG: Invalid bottom margin %d, setting to 0.\n",
	      page_bottom);
      page_bottom = 0;
    }
  }
  fprintf(stderr, "DEBUG: Margins: Left (Columns): %d; Right (Columns): %d; Top (Lines): %d; Bottom (Lines): %d\n",
	  page_left, page_right, page_top, page_bottom);

  text_width = num_columns - page_left - page_right;
  text_height = num_lines - page_top - page_bottom;
  fprintf(stderr, "DEBUG: Text area: Lines per page: %d; Characters per line: %d\n",
	  text_height, text_width);

  strcpy(encoding, "ASCII//IGNORE");
  if ((val = cupsGetOption("PrinterEncoding", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultPrinterEncoding", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strncasecmp(val, "Custom.", 7))
      val += 7;
    if (val[0] != '\0') {
      snprintf(encoding, sizeof(encoding), "%s//IGNORE", val);
      for (p = encoding; *p; p ++)
	*p = toupper(*p);
    }
  }
  fprintf(stderr, "DEBUG: Output encoding: %s\n", encoding);
  
  if ((val = cupsGetOption("OverlongLines", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultOverlongLines", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strcasecmp(val, "Truncate"))
      overlong_lines = TRUNCATE;
    else if (!strcasecmp(val, "WordWrap"))
      overlong_lines = WORDWRAP;
    else if (!strcasecmp(val, "WrapAtWidth"))
      overlong_lines = WRAPATWIDTH;
    else
      fprintf(stderr, "DEBUG: Invalid value for OverlongLines: %s, using default value.\n",
	      val);
  }
  fprintf(stderr, "DEBUG: Handling of overlong lines: %s\n",
	  (overlong_lines == TRUNCATE ? "Truncate at maximum width" :
	   (overlong_lines == WORDWRAP ? "Word-wrap" :
	    "Wrap exactly at maximum width")));

  if ((val = cupsGetOption("TabWidth", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultTabWidth", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strncasecmp(val, "Custom.", 7))
      val += 7;
    i = atoi(val);
    if (i > 0)
      tab_width = i;
    else
      fprintf(stderr, "DEBUG: Invalid tab width %d, using default value: %d\n",
	      i, tab_width);
  }
  fprintf(stderr, "DEBUG: Tab width: %d\n", tab_width);

  if ((val = cupsGetOption("Pagination", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultPagination", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (is_true(val))
      pagination = 1;
    else if (is_false(val))
      pagination = 0;
    else
      fprintf(stderr, "DEBUG: Invalid value for Pagination: %s, using default value.\n",
	      val);
  }
  fprintf(stderr, "DEBUG: Pagination (Print in defined pages): %s\n",
	  (pagination ? "Yes" : "No"));

  if ((val = cupsGetOption("SendFF", num_options, options)) != NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultSendFF", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (is_true(val))
      send_ff = 1;
    else if (is_false(val))
      send_ff = 0;
    else
      fprintf(stderr, "DEBUG: Invalid value for SendFF: %s, using default value.\n",
	      val);
  }
  fprintf(stderr, "DEBUG: Send Form Feed character at end of page: %s\n",
	  (send_ff ? "Yes" : "No"));

  if ((val = cupsGetOption("NewlineCharacters", num_options, options)) !=
      NULL ||
      (ppd_attr = ppdFindAttr(ppd, "DefaultNewlineCharacters", NULL)) != NULL) {
    if (val == NULL)
      val = ppd_attr->value;
    if (!strcasecmp(val, "LF"))
      newline_char = LF;
    else if (!strcasecmp(val, "CR"))
      newline_char = CR;
    else if (!strcasecmp(val, "CRLF"))
      newline_char = CRLF;
    else
      fprintf(stderr, "DEBUG: Invalid value for NewlineCharacters: %s, using default value.\n",
	      val);
  }
  fprintf(stderr, "DEBUG: Characters sent to make printer start a new line: %s\n",
	  (newline_char == LF ? "Line Feed (LF)" :
	   (newline_char == CR ? "Carriage Return (CR)" :
	    "Carriage Return (CR) and Line Feed (LF)")));

  if ((val = cupsGetOption("page-ranges", num_options, options)) !=
      NULL) {
    if (val[0] != '\0')
      page_ranges = strdup(val);
  }
  if (page_ranges)
    fprintf(stderr, "DEBUG: Page selection: %s\n", page_ranges);

  if ((val = cupsGetOption("page-set", num_options, options)) !=
      NULL) {
    if (!strcasecmp(val, "even")) {
      even_pages = 1;
      odd_pages = 0;
    } else if (!strcasecmp(val, "odd")) {
      even_pages = 0;
      odd_pages = 1;
    } else if (!strcasecmp(val, "all")) {
      even_pages = 1;
      odd_pages = 1;
    } else
      fprintf(stderr, "DEBUG: Invalid value for page-set: %s, using default value.\n",
	      val);
  }
  if (!even_pages || !odd_pages)
    fprintf(stderr, "DEBUG: Print %s.\n",
	    (even_pages ? "only the even pages" :
	     (odd_pages ? "only the odd pages" :
	      "no pages")));
  
  if ((val = cupsGetOption("output-order", num_options, options)) !=
      NULL) {
    if (!strcasecmp(val, "reverse"))
      reverse_order = 1;
    else
      fprintf(stderr, "DEBUG: Invalid value for OutputOrder: %s, using default value.\n",
	      val);
  }
  fprintf(stderr, "DEBUG: Print pages in reverse order: %s\n",
	  (reverse_order ? "Yes" : "No"));

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL) {
    if (is_true(val))
      collate = 1;
    else if (is_false(val))
      collate = 0;
    else
      fprintf(stderr, "DEBUG: Invalid value for Collate: %s, using default value.\n",
	      val);
  }
  fprintf(stderr, "DEBUG: Collate copies: %s\n",
	  (collate ? "Yes" : "No"));

  /* Create a string to insert as the newline mark */
  if (newline_char == LF)
    newline_char_str = "\n";
  else if (newline_char == CR)
    newline_char_str = "\r";
  else if (newline_char == CRLF)
    newline_char_str = "\r\n";

  /* Size of the output page in bytes (only 1-byte-per-character supported)
     in the worst case, no blank lines at top and bottom, no blank right
     margin, 2 characters for newline (CR + LF) plus form feed plus closing
     zero */
  page_size = (num_columns + 2) * num_lines + 2;

  /* Allocate output page buffer */
  out_page = calloc(page_size, sizeof(char));

  /* Set conversion mode of iconv */
  cd = iconv_open(encoding, "UTF-8");
  if (cd == (iconv_t) -1) {
    /* Something went wrong.  */
    if (errno == EINVAL)
      fprintf(stderr, "ERROR: Conversion from UTF-8 to %s not available\n",
	      encoding);
    else
      fprintf(stderr, "ERROR: Error setting up conversion from UTF-8 to %s\n",
	      encoding);
    goto error;
  }

  /* Open the input file */
  fd = open(filename, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "ERROR: Unable to open input text file %s\n", filename);
    goto error;
  }

  /* Create an array to hold the output pages for collated copies or
     reverse output order (only when printing paginated) */
  if (pagination &&
      ((num_copies != 1 && collate) || reverse_order)) {
    /* Create page array */
    page_array = cupsArrayNew(NULL, NULL);
  }

  /* Main loop for readin the input file, converting the encoding, formatting
     the output, and printing the pages */
  destptr = out_page;
  page_empty = 1;
  page = 1;
  line = 0;
  column = 0;
  previous_is_cr = 0;
  insize = 0;
  do {
    /* Reset input pointer */
    inptr = inbuf;
    
    /* Mark the output buffer empty */
    outsize = sizeof(outbuf);
    outptr = outbuf;

    /* Read from the input file */
    nread = read (fd, inbuf + insize, sizeof (inbuf) - insize);
    if (nread == 0) {
      /* When we come here the file is completely read.
	 This still could mean there are some unused
	 characters in the inbuf, meaning that the file
         ends with an incomplete UTF-8 character. Log
         this fact. */
      if (insize > 0 && incomplete_char)
	fprintf(stderr, "DEBUG: Input text file ends with incomplete UTF-8 character sequence, file possibly incomplete, but printing the successfully read part anyway.\n");

      /* Now write out the byte sequence to get into the
	 initial state if this is necessary.  */
      iconv (cd, NULL, NULL, &outptr, &outsize);
    }

    insize += nread;

    /* Convert the incoming UTF-8-encoded text to the printer's encoding */
    if (insize > 0) { /* Do we have data to convert? */
      /* Do the conversion.  */
      nconv = iconv (cd, &inptr, &insize, &outptr, &outsize);
      if (nconv == (size_t) -1) {
	/* Not everything went right. It might only be
	   an unfinished byte sequence at the end of the
	   buffer. Or it is a real problem. */
	if (errno == EINVAL || errno == E2BIG) {
	  /* This is harmless.  Simply move the unused
	     bytes to the beginning of the buffer so that
	     they can be used in the next round. */
	  if (errno == EINVAL)
	    incomplete_char = 1;
	  memmove (inbuf, inptr, insize);
	} else {
	  /* We found an illegal UTF-8 byte sequence here,
	     so error out at this point. */
	  fprintf(stderr, "ERROR: Illegal UTF-8 sequence found. Input file perhaps not UTF-8-encoded\n");
	  result = -1;
	  break;
	}
      }
    }

    /* Process the output to generate the output pages */
    if (outptr == outbuf) /* End of input file */
      *(outptr ++) = '\0';
    for (procptr = outbuf; procptr < outptr; procptr ++) {
      if ((column >= text_width && /* Current line is full */
	   *procptr != '\n' && *procptr != '\r' && *procptr != '\f') ||
	                           /* Next character is not newline or
				      formfeed */
	  (outbuf[0] == '\0' && column > 0)) {    /* End of input file */
	if (overlong_lines == TRUNCATE && outbuf[0] != '\0')
	  skip_rest_of_line = 1;
	else {
	  if (overlong_lines == WORDWRAP && outbuf[0] != '\0') {
	    if (*procptr > ' ') {
	      *destptr = '\0';
	      for (p = destptr - 1, i = column - 1; *p != ' ' && i >= 0;
		   p --, i--);
	      if (i >= 0 && i < column - 1) {
		wrapped_word = strdup(p + 1);
		for (; *p == ' ' && i >= 0; p --, i--);
		if (*p != ' ' && i >= 0)
		  destptr = p + 1;
		else {
		  free(wrapped_word);
		  wrapped_word = NULL;
		}
	      }
	    } else
	      skip_spaces = 1;
	  }
	  /* Remove trailing whitespace */
	  while (destptr > out_page && *(destptr - 1) == ' ')
	    destptr --;
	  /* Put newline character(s) */
	  for (j = 0; newline_char_str[j]; j ++)
	    *(destptr ++) = newline_char_str[j];
	  /* Position cursor in next line */
	  line ++;
	  column = 0;
	}
      }
      if ((line >= text_height && /* Current page is full */
	   *procptr != '\f') ||   /* Next character is not formfeed */
	  outbuf[0] == '\0' ) {   /* End of input file */
	/* Do we actually print this page? */
	if (!pagination ||
	    check_range(page_ranges, even_pages, odd_pages, page)) {
	  /* Finalize the page */
	  if (pagination) {
	    if (send_ff) {
	      if (page_empty)
		destptr = out_page; /* Remove unneeded white space */
	      /* Send Form Feed */
	      *(destptr ++) = '\f';
	    } else if (outbuf[0] != '\0') {
	      /* Fill up page with blank lines */
	      for (i = 0; i < page_bottom; i ++)
		for (j = 0; newline_char_str[j]; j ++)
		  *(destptr ++) = newline_char_str[j];
	    }
	  }
	  /* Allow to handle the finished page as a C string */
	  *(destptr ++) = '\0';
	  /* Count pages which will actually get printed */
	  num_pages ++;
	  if (!pagination) {
	    /* Send out the output page buffer content */
	    printf("%s", out_page);
	    /* Log the page output (only once, when printing the first buffer
	       load) */
	    if (num_pages == 1)
	      fprintf(stderr, "PAGE: 1 1\n");
	  } else if ((num_copies == 1 || !collate) && !reverse_order) {
	    /* Send out the page */
	    for (i = 0; i < num_copies; i ++)
	      printf("%s", out_page);
	    /* Log the page output */
	    fprintf(stderr, "PAGE: %d %d\n", num_pages, num_copies);
	  } else {
	    /* Save the page in the page array */
	    cupsArrayAdd(page_array, strdup(out_page));
	  }
	}
	/* Reset for next page */
	destptr = out_page;
	page_empty = 1;
	line = 0;
	column = 0;
	page ++;
      }
      if (outbuf[0] == '\0') /* End of input file */
	break;
      if (column == 0) { /* Start of new line */ 
	if (line == 0 && pagination)   /* Start of new page */
	  for (i = 0; i < page_top; i ++)
	    for (j = 0; newline_char_str[j]; j ++)
	      *(destptr ++) = newline_char_str[j];
	for (i = 0; i < page_left; i ++)
	  *(destptr ++) = ' ';
	/* Did we wrap a word from the previous line? */
	if (wrapped_word) {
	  for (p = wrapped_word; *p != '\0'; p ++, column ++)
	    *(destptr ++) = *p;
	  free(wrapped_word);
	  wrapped_word = NULL;
	  page_empty = 0;
	  skip_spaces = 0;
	}
      }
      if (*procptr == '\r' || *procptr == '\n') { /* CR or LF */
	/* Only write newline if we are not on the LF of a CR+LF */
	if (*procptr == '\r' || previous_is_cr == 0) {
	  /* Remove trailing whitespace */
	  while (destptr > out_page && *(destptr - 1) == ' ')
	    destptr --;
	  /* Put newline character(s) */
	  for (j = 0; newline_char_str[j]; j ++)
	    *(destptr ++) = newline_char_str[j];
	  /* Position cursor in next line */
	  line ++;
	  column = 0;
	  /* Finished truncating an overlong line */
	  skip_rest_of_line = 0;
	  skip_spaces = 0;
	}
	if (*procptr == '\r')
	  previous_is_cr = 1;
	else
	  previous_is_cr = 0;
      } else {
	previous_is_cr = 0;
	if (*procptr == '\t') { /* Tab character */
	  if (!skip_rest_of_line && !skip_spaces) {
	    *(destptr ++) = ' '; /* Always at least one space */
	    column ++;
	    /* Add spaces to reach next multiple of the tab width */
	    for (; column % tab_width != 0 && column < text_width; column ++)
	      *(destptr ++) = ' ';
	  }
	} else if (*procptr == '\f') { /* Form feed */
	  /* Skip to end of page */	  
	  if (send_ff)
	    /* Mark page full */
	    line = text_height;
	  else if (pagination)
	    /* Fill page with newlines */
	    for (; line < text_height; line ++)
	      for (j = 0; newline_char_str[j]; j ++)
		*(destptr ++) = newline_char_str[j];
	  column = 0;
	  /* Finished truncating an overlong line */
	  skip_rest_of_line = 0;
	  skip_spaces = 0;
	} else if (*procptr == ' ') { /* Space */
	  if (!skip_rest_of_line && !skip_spaces) {
	    *(destptr ++) = *procptr;
	    column ++;
	  }
	} else if (*procptr > ' ' || *procptr < '\0') { /* Regular character */
	  if (!skip_rest_of_line) {
	    *(destptr ++) = *procptr;
	    column ++;
	    page_empty = 0;
	    skip_spaces = 0;
	  }
	}
      }
    }
  } while (outbuf[0] != '\0'); /* End of input file */

  close(fd);
  
  if (iconv_close (cd) != 0)
    fprintf (stderr, "DEBUG: Error closing iconv encoding conversion session\n");

  /* Error out on an illegal UTF-8 sequence in the input file */
  if (result < 0)
    goto error;

  /* Print out the page array if we created one */
  if (pagination &&
      ((num_copies != 1 && collate) || reverse_order)) {
    /* If we print collated copies, the outer loop (i) goes through the
       copies, if we do not collate, the inner loop (j) goes through the
       copies. The other loop only runs for one cycle. */
    for (i = 0; i < (collate ? num_copies : 1); i ++)
      for (page = (reverse_order ? num_pages : 1);
	   (reverse_order ? (page >= 1) : (page <= num_pages));
	   page += (reverse_order ? -1 : 1)) {
	p = (char *)cupsArrayIndex(page_array, page - 1);
	for (j = 0; j < (collate ? 1 : num_copies); j ++)
	  printf("%s", p);
	fprintf(stderr, "PAGE: %d %d\n", page, (collate ? 1 : num_copies));
      }
    /* Clean up */
    for (page = 0; page < num_pages; page ++) {
      p = (char *)cupsArrayIndex(page_array, page);
      free(p);
    }
    cupsArrayDelete(page_array);
  }

 /*
  * Cleanup and exit...
  */

 error:

  free(page_ranges);
  free(out_page);
  
  if (tempfile[0])
    unlink(tempfile);

  return (exit_status);
}


/*
 * 'is_true()' - Check option value for boolean true
 */

static int is_true(const char *value)
{
  if (!value)
    return 0;
  return (strcasecmp(value, "yes") == 0) ||
    (strcasecmp(value, "on") == 0) ||
    (strcasecmp(value, "true") == 0) ||
    (strcasecmp(value, "1") == 0);
}


/*
 * 'is_false()' - Check option value for boolean false
 */

static int is_false(const char *value)
{
  if (!value)
    return 0;
  return (strcasecmp(value, "no") == 0) ||
    (strcasecmp(value, "off") == 0) ||
    (strcasecmp(value, "false") == 0) ||
    (strcasecmp(value, "0") == 0);
}


/*
 * 'check_range()' - Check to see if the current page is selected for
 *                   printing.
 */

static int				/* O - 1 if selected, 0 otherwise */
check_range(char *page_ranges,          /* I - Selection of pages to print */
	    int even_pages,             /* I - Print the even pages */
	    int odd_pages,              /* I - Print the odd pages */
            int page)		        /* I - Page number */
{
  const char	*range;			/* Pointer into range string */
  int		lower, upper;		/* Lower and upper page numbers */

 /*
  * See if we only print even or odd pages...
  */

  if (!odd_pages && (page & 1))
    return (0);

  if (!even_pages && !(page & 1))
    return (0);

 /*
  * page-ranges option
  */
  
  if (!page_ranges || page_ranges[0] == '\0')
    return (1);				/* No range, print all pages... */

  for (range = page_ranges; *range != '\0';)
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
 * 'cancel_job()' - Flag the job as canceled.
 */

static void
cancel_job(int sig)			/* I - Signal number (unused) */
{
  (void)sig;

  job_canceled = 1;
}


/*
 * End
 */

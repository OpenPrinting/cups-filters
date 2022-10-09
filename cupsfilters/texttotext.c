//
//   texttotext
//
//   Filter to print text files on text-only printers. The filter has
//   several configuration options so that it should work with most
//   printer models.
//
//   Copyright 2007-2011 by Apple Inc.
//   Copyright 1997-2006 by Easy Software Products.
//   Copyright 2011-2016 by Till Kamppeter
//
//   Distribution and use rights are outlined in the file "COPYING"
//   which should have been included with this file.
//

//
// Include necessary headers...
//

#include <config.h>
#include <cups/cups.h>
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
#include "ipp.h"
#include "filter.h"


//
// Type definitions
//

typedef enum overlong_line_e
{
  TRUNCATE = 0,
  WORDWRAP = 1,
  WRAPATWIDTH = 2
} overlong_line_t;

typedef enum newline_char_e
{
  LF = 0,
  CR = 1,
  CRLF = 2
} newline_char_t;


//
// Local functions...
//

static int              is_true(const char *value);
static int              is_false(const char *value);
static int		check_range(char *page_ranges, int even_pages,
				    int odd_pages, int page);

int
cfFilterTextToText(int inputfd,         // I - File descriptor input stream
		   int outputfd,        // I - File descriptor output stream
		   int inputseekable,   // I - Is input stream seekable?
					//     (unused)
		   cf_filter_data_t *data, // I - Job and printer data
		   void *parameters)    // I - Filter-specific parameters
{
  int		i, j;			// Looping vars
  char          *p;
  int		exit_status = 0;	// Exit status
  int		fd;			// Copy file descriptor

  int           num_copies;             // Number of copies
  int		num_options;		// Number of options
  cups_option_t	*options;		// Options
  const char	*val;			// Option value
  int           num_lines = 66,         // Lines per page
                num_columns = 80;       // Characters per line
  int           page_left = 0,          // Page margins
                page_right = 0,
                page_top = 0,
                page_bottom = 0,
                num_lines_per_inch = 0,
                num_chars_per_inch = 0;
  int           text_width,             // Width of the text area on the page
                text_height;            // Height of the text area on the
					// page
  char          encoding[64];           // The printer'a encoding, to which
					// the incoming UTF-8 is converted,
					// must be 1-byte-per-character
  overlong_line_t overlong_lines = WRAPATWIDTH;
                                        // How to treat overlong lines
  int           tab_width = 8;          // Turn tabs to spaces with given
					// width
  int           pagination = 1;         // Paginate text to allow margins
					// and page management
  int           send_ff = 1;            // Send form-feed char at the end of
					// each page
  newline_char_t newline_char = CRLF;   // Character to send at end of line
  char          *newline_char_str;
  char          *page_ranges = NULL;    // Selection of pages to print
  int           even_pages = 1;         // Print the even pages
  int           odd_pages = 1;          // Print the odd pages
  int           reverse_order = 0;      // Ouput pages in reverse order?
  int           collate = 1;            // Collate multiple copies?
  int           page_size;              // Number of bytes needed for a page,
					// to allocate the memory for the
					// output page buffer
  char          *out_page = NULL;       // Output page buffer
  cups_array_t  *page_array = NULL;     // Array to hold the output pages
					// for collated copies and reverse
					// output order
  iconv_t       cd;                     // Conversion descriptor, describes
					// between which encodings iconv
					// should convert
  char          outbuf[4096];           // Output buffer for iconv
  char          inbuf[2048];            // Input buffer for iconv
  size_t        outsize;                // Space left in outbuf
  size_t        insize = 0;             // Bytes still to be converted in
					// inbuf
  char          *outptr = outbuf;       // Pointer for next converted
					// character to be dropped
  char          *inptr = inbuf;         // Pointer to next character to be
					// converted
  size_t        nread;                  // Number of bytes read from file
  size_t        nconv;                  // -1 on conversion error
  int           incomplete_char = 0;    // Was last character in input buffer
					// incomplete
  int           result = 0;             // Conversion result (-1 on error)
  char          *procptr,               // Pointer into conversion output
					// to indicate next byte to process
					// for output page creation
                *destptr;               // Pointer into output page buffer
					// where next character will be put
  int           page,                   // Number of current output page
                line, column;           // Character coordiantes on output
					// page
  int           page_empty;             // Is the current output page still
					// empty (no visible characters)?
  int           previous_is_cr;         // Is the previous character processed
					// a Carriage Return?
  int           new_line_started = 0;   // Is the proceeding of starting a
					// new line (left margin, new page's
					// top margin, wrapped word) already
					// done for this line?
  int           skip_rest_of_line = 0;  // Are we truncating an overlong
					// line?
  int           skip_spaces = 0;        // Are we skipping spaces at a line
					// break when word-wrapping?
  char          *wrapped_word = NULL;   // Section of a word wrapped over
					// to the next line
  int           num_pages = 0;          // Number of pages which get actually
					// printed
 
  ipp_t *printer_attrs = data->printer_attrs;
  ipp_t *job_attrs = data->job_attrs;
  ipp_attribute_t *ipp;
  ipp_t *media_col_entry;
  float paperdimensions[2];
  char buf[2048];

  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;

  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void *icd = data->iscanceleddata;
  cups_file_t *outputfp;


  //
  // Make sure status messages are not buffered...
  //

  setbuf(stderr, NULL);

  if ((outputfp = cupsFileOpenFd(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
                    "cfFilterBannerToPDF: Unable to open output data stream.");
    }
    return (1);
  }

  //
  // Number of copies
  //

  num_copies = data->copies;
  if (num_copies < 1)
    num_copies = 1;

  //
  // Parse the options and IPP attributes
  //

  num_options = data->num_options;
  options = data->options;

  // Number of lines and columns per inch This is mainly for
  // development and debugging. With these set and the page size given
  // as width and length dimension, the number of lines and columns on
  // the page gets calculated. A way for using custom sizes.  With
  // these not set, the media-col-database entry for the page size is
  // looked up and if it has the extra integer entries "num-lines" and
  // "num-columns", the numbers here are used.
  if ((val = cupsGetOption("NumLinesPerInch",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("num-lines-per-inch",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "num-lines-per-inch",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "num-lines-per-inch-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
      num_lines_per_inch = atoi(val);
    if (log)
    {
      if (num_lines_per_inch <= 0)
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterTextToText: Number of lines per inch not set, if page dimensions are given, find number of lines per page via pre-defined page sizes");
      else
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterTextToText: Number of lines per inch: %d",
	    num_lines_per_inch);
    }
  }

  if ((val = cupsGetOption("NumCharsPerInch",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("num-chars-per-inch",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs,"num-chars-per-inch",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "num-chars-per-inch-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
      num_chars_per_inch = atoi(val);
    if (log)
    {
      if (num_chars_per_inch <= 0)
	log(ld, CF_LOGLEVEL_DEBUG,
	     "cfFilterTextToText: Number of charcters per inch not set, if page dimensions are given, find number of characters per line via pre-defined page sizes");
      else
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterTextToText: Number of characters per inch = %d",
	    num_chars_per_inch);
    }
  }

  cfGetPageDimensions(printer_attrs, job_attrs, num_options, options,
		      NULL, 0,
		      &(paperdimensions[0]), &(paperdimensions[1]),
		      NULL, NULL, NULL, NULL, NULL, &media_col_entry);
  if (media_col_entry)
  {
    // We have a media-col-database and the requested/default page size
    // is contained in it (= size is supported), check whether we have
    // a number of lines/columns registered via extra interger entries
    // "num-columns" and "num-lines".
    num_columns = ippGetInteger(ippFindAttribute(media_col_entry,
						 "num-columns",
						 IPP_TAG_ZERO), 0);
    num_lines = ippGetInteger(ippFindAttribute(media_col_entry,
					       "num-lines",
					       IPP_TAG_ZERO), 0);
  }

  if (num_chars_per_inch > 0 && paperdimensions[0] > 0)
    num_columns = (int)((paperdimensions[0] / 72.0) * (num_chars_per_inch));
  if (num_lines_per_inch > 0 && paperdimensions[1] > 0)
    num_lines = (int)((paperdimensions[1] / 72.0) * (num_lines_per_inch));

  if (num_lines <= 0)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterTextToText: Invalid number of lines %d, using default: 66",
		 num_lines);
    num_lines = 66;
  }
  if (num_columns <= 0)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		"cfFilterTextToText: Invalid number of columns %d, using default: 80",
		num_columns);
    num_columns = 80;
  }

  // Direct specification of number of lines/columns, mainly for debugging
  // and development
  if ((val = cupsGetOption("PageHeight", num_options, options)) != NULL ||
      (val = cupsGetOption("page-height", num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-height", IPP_TAG_INTEGER)) !=
      NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    i = atoi(val);
    if (i > 0)
      num_lines = i;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid number of lines %d, using default value: %d",
		   i, num_lines);
  }
  if ((val = cupsGetOption("PageWidth", num_options, options)) != NULL ||
      (val = cupsGetOption("page-width", num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-width", IPP_TAG_INTEGER)) !=
      NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    i = atoi(val);
    if (i > 0)
      num_columns = i;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid number of columns %d, using default value: %d",
		   i, num_columns);
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Lines per page: %d; Characters per line: %d",
	       num_lines, num_columns);
  
  if ((val = cupsGetOption("PageLeft",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("page-left",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-left",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "page-left-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
    {
      if (!strncasecmp(val, "Custom", 6))
	val += 7;
      page_left = atoi(val);
    }
    if (page_left < 0 || page_left > num_columns - 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Left margin not set or invalid, use 0 columns");
      page_left = 0;
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Left margin: %d columns",
		   page_left);
  }

  if ((val = cupsGetOption("PageRight",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("page-right",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-right",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "page-right-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
    {
      if (!strncasecmp(val, "Custom", 6))
	val += 7;
      page_right = atoi(val);
    }
    if (page_right < 0 || page_right > num_columns - 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Right margin not set or invalid, use 0 columns");
      page_right = 0;
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Right margin: %d columns",
		   page_right);
  }

  if ((val = cupsGetOption("PageTop",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("page-top",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-top",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "page-top-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
    {
      if (!strncasecmp(val, "Custom", 6))
	val += 7;
      page_top = atoi(val);
    }
    if (page_top < 0 || page_top > num_lines - 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Top margin not set or invalid, use 0 lines");
      page_top = 0;
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Top margin: %d lines",
		   page_top);
  }

  if ((val = cupsGetOption("PageBottom",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("page-bottom",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-bottom",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "page-bottom-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
    {
      if (!strncasecmp(val, "Custom", 6))
	val += 7;
      page_bottom = atoi(val);
    }
    if (page_bottom < 0 || page_bottom > num_lines - 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Bottom margin not set or invalid, use 0 lines");
      page_bottom = 0;
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Bottom margin: %d lines",
		   page_bottom);
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Margins: Left (Columns): %d; Right (Columns): %d; Top (Lines): %d; Bottom (Lines): %d",
	       page_left, page_right, page_top, page_bottom);

  text_width = num_columns - page_left - page_right;
  text_height = num_lines - page_top - page_bottom;
  if (log) log(ld, CF_LOGLEVEL_DEBUG, "cfFilterTextToText: Text area: Lines per page: %d; Characters per line: %d",
	       text_height, text_width);

  strcpy(encoding, "ASCII//IGNORE");
  if ((val = cupsGetOption("PrinterEncoding",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("printer-encoding",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "printer-encoding",
			      IPP_TAG_ZERO)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "printer-encoding-default",
			      IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
    {
      if (!strncasecmp(val, "Custom", 6))
	val += 7;
      if (val[0] != '\0')
      {
	snprintf(encoding, sizeof(encoding), "%.55s//IGNORE", val);
	for (p = encoding; *p; p ++)
	  *p = toupper(*p);
      }
    }
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Output encoding: %s", encoding);

  if ((val = cupsGetOption("OverLongLines",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("over-long-lines",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "over-long-lines",
			      IPP_TAG_ZERO)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "over-long-lines-default",
			      IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (!strcasecmp(val, "truncate"))
      overlong_lines = TRUNCATE;
    else if (!strcasecmp(val, "word-wrap") ||
	     !strcasecmp(val, "WordWrap"))
      overlong_lines = WORDWRAP;
    else if (!strcasecmp(val, "wrap-at-width") ||
	     !strcasecmp(val, "WrapAtWidth"))
      overlong_lines = WRAPATWIDTH;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for over-long-lines: %s, using default value",
		   val);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Handling of overlong lines: %s",
	       (overlong_lines == TRUNCATE ? "Truncate at maximum width" :
		(overlong_lines == WORDWRAP ? "Word-wrap" :
		 "Wrap exactly at maximum width")));

  if ((val = cupsGetOption("TabWidth",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("tab-width",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "tab-width",
			      IPP_TAG_INTEGER)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "tab-width-default",
			      IPP_TAG_INTEGER)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val && val[0])
    {
      if (!strncasecmp(val, "Custom", 6))
	val += 7;
      tab_width = atoi(val);
    }
    if (log)
    {
      if (tab_width <= 0 || tab_width > num_columns - 1)
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterTextToText: Tab width not set or invalid, use default");
      else
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterTextToText: Tab width: %d columns",
	    tab_width);
    }
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Tab width: %d", tab_width);

  if ((val = cupsGetOption("Pagination",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("pagination",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "pagination",
			      IPP_TAG_ZERO)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "pagination-default",
			      IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (is_true(val))
      pagination = 1;
    else if (is_false(val))
      pagination = 0;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for pagination: %s, using default value",
		  val);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Pagination (Print in defined pages): %s",
	       (pagination ? "Yes" : "No"));

  if ((val = cupsGetOption("SendFF",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("send-ff",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "send-ff",
			      IPP_TAG_ZERO)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "send-ff-default",
			      IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (is_true(val))
      send_ff = 1;
    else if (is_false(val))
      send_ff = 0;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for send-ff: %s, using default value",
		   val);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Send Form Feed character at end of page: %s",
	       (send_ff ? "Yes" : "No"));

  if ((val = cupsGetOption("NewlineCharacters",
			   num_options, options)) != NULL ||
      (val = cupsGetOption("newline-characters",
			   num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "newline-characters",
			      IPP_TAG_ZERO)) != NULL ||
      (ipp = ippFindAttribute(printer_attrs, "newline-characters-default",
			      IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (!strcasecmp(val, "lf"))
      newline_char = LF;
    else if (!strcasecmp(val, "cr"))
      newline_char = CR;
    else if (!strcasecmp(val, "crlf"))
      newline_char = CRLF;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for newline-characters: %s, using default value",
		  val);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Characters sent to make printer start a new line: %s",
	       (newline_char == LF ? "Line Feed (LF)" :
		(newline_char == CR ? "Carriage Return (CR)" :
		 "Carriage Return (CR) and Line Feed (LF)")));

  if ((val = cupsGetOption("page-ranges", num_options, options)) !=
      NULL  ||
      (ipp = ippFindAttribute(job_attrs, "page-ranges", IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (val[0] != '\0')
      page_ranges = strdup(val);
  }
  if (page_ranges)
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterTextToText: Page selection: %s", page_ranges);

  if ((val = cupsGetOption("page-set", num_options, options)) !=
      NULL  ||
      (ipp = ippFindAttribute(job_attrs, "page-set", IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (!strcasecmp(val, "even"))
    {
      even_pages = 1;
      odd_pages = 0;
    }
    else if (!strcasecmp(val, "odd"))
    {
      even_pages = 0;
      odd_pages = 1;
    }
    else if (!strcasecmp(val, "all"))
    {
      even_pages = 1;
      odd_pages = 1;
    } else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for page-set: %s, using default value",
		   val);
  }
  if (!even_pages || !odd_pages)
    if (log) log(ld, CF_LOGLEVEL_DEBUG, "cfFilterTextToText: Print %s",
		 (even_pages ? "only the even pages" :
		  (odd_pages ? "only the odd pages" :
		   "no pages")));
  
  if ((val = cupsGetOption("OutputOrder", num_options, options)) !=
      NULL  ||
      (val = cupsGetOption("output-order", num_options, options)) !=
      NULL  ||
      (ipp = ippFindAttribute(job_attrs,"output-order", IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (!strcasecmp(val, "reverse"))
      reverse_order = 1;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for OutputOrder: %s, using default value",
		   val);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterTextToText: Print pages in reverse order: %s",
	       (reverse_order ? "Yes" : "No"));

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL  ||
      (ipp = ippFindAttribute(job_attrs, "collate", IPP_TAG_ZERO)) != NULL)
  {
    if (val == NULL)
    {
      ippAttributeString(ipp, buf, sizeof(buf));
      val = buf;
    }
    if (is_true(val))
      collate = 1;
    else if (is_false(val))
      collate = 0;
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterTextToText: Invalid value for Collate: %s, using default value",
		   val);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG, "cfFilterTextToText: Collate copies: %s",
	       (collate ? "Yes" : "No"));

  // Create a string to insert as the newline mark
  if (newline_char == LF)
    newline_char_str = "\n";
  else if (newline_char == CR)
    newline_char_str = "\r";
  else if (newline_char == CRLF)
    newline_char_str = "\r\n";

  // Size of the output page in bytes (up to 4-byte-per-character)
  // in the worst case, no blank lines at top and bottom, no blank right
  // margin, 2 characters for newline (CR + LF) plus form feed plus closing
  // zero
  page_size = ((num_columns + 2) * num_lines + 2) * 4;

  // Allocate output page buffer
  out_page = calloc(page_size, sizeof(char));

  // Set conversion mode of iconv
  cd = iconv_open(encoding, "UTF-8");
  if (cd == (iconv_t) -1)
  {
    // Something went wrong.
    if (errno == EINVAL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToText: Conversion from UTF-8 to %s not available",
		   encoding);
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToText: Error setting up conversion from UTF-8 to %s",
		   encoding);
    }
    goto error;
  }

  // Open the input file
  fd = inputfd;
  if (fd < 0)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterTextToText: Unable to open input text file");
    goto error;
  }

  // Create an array to hold the output pages for collated copies or
  // reverse output order (only when printing paginated)
  if (pagination &&
      ((num_copies != 1 && collate) || reverse_order))
    // Create page array
    page_array = cupsArrayNew(NULL, NULL);

  // Main loop for reading the input file, converting the encoding, formatting
  // the output, and printing the pages
  destptr = out_page;
  page_empty = 1;
  page = 1;
  line = 0;
  column = 0;
  previous_is_cr = 0;
  insize = 0;
  new_line_started = 0;
  do {
    // Reset input pointer
    inptr = inbuf;
    
    // Mark the output buffer empty
    outsize = sizeof(outbuf);
    outptr = outbuf;

    // Read from the input file
    nread = read (fd, inbuf + insize, sizeof (inbuf) - insize);
    if (nread == 0)
    {
      // When we come here the file is completely read.
      // This still could mean there are some unused
      // characters in the inbuf, meaning that the file
      // ends with an incomplete UTF-8 character. Log
      // this fact.
      if (insize > 0 && incomplete_char)
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterTextToText: Input text file ends with incomplete UTF-8 character sequence, file possibly incomplete, but printing the successfully read part anyway");
      }

      // Now write out the byte sequence to get into the
      // initial state if this is necessary.
      iconv (cd, NULL, NULL, &outptr, &outsize);
    }

    insize += nread;

    // Convert the incoming UTF-8-encoded text to the printer's encoding
    if (insize > 0) // Do we have data to convert?
    {
      // Do the conversion.
      nconv = iconv (cd, &inptr, &insize, &outptr, &outsize);
      if (log && nconv == (size_t) -1)
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterTextToText: iconv() message: %s", strerror(errno));
      if (nconv == (size_t) -1 && insize > 0)
      {
	// Not everything went right. It might only be
	// an unfinished byte sequence at the end of the
	// output buffer. Or it is a real problem.
	if (errno == EINVAL || errno == E2BIG)
	{
	  // This is harmless.  Simply move the unused
	  // bytes to the beginning of the buffer so that
	  // they can be used in the next round.
	  if (errno == EINVAL)
	    incomplete_char = 1;
	  memmove (inbuf, inptr, insize);
	}
	else
	{
	  // We found an illegal UTF-8 byte sequence here,
	  // so error out at this point.
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToText: Illegal UTF-8 sequence found. Input file perhaps not UTF-8-encoded");
	  result = -1;
	  break;
	}
      }
    }

    // Process the output to generate the output pages
    if (outptr == outbuf) // End of input file
      *(outptr ++) = '\0';
    for (procptr = outbuf; procptr < outptr; procptr ++)
    {
      if ((column >= text_width && // Current line is full
	   *procptr != '\n' && *procptr != '\r' && *procptr != '\f') ||
	                           // Next character is not newline or
	                           // formfeed
	  (outbuf[0] == '\0' && column > 0))    // End of input file
      {
	if (overlong_lines == TRUNCATE && outbuf[0] != '\0')
	  skip_rest_of_line = 1;
	else
	{
	  if (overlong_lines == WORDWRAP && outbuf[0] != '\0')
	  {
	    if (*procptr > ' ')
	    {
	      *destptr = '\0';
	      for (p = destptr - 1, i = column - 1; *p != ' ' && i >= 0;
		   p --, i--);
	      if (i >= 0 && i < column - 1)
	      {
		wrapped_word = strdup(p + 1);
		for (; *p == ' ' && i >= 0; p --, i--);
		if (*p != ' ' && i >= 0)
		  destptr = p + 1;
		else
		{
		  free(wrapped_word);
		  wrapped_word = NULL;
		}
	      }
	    } else
	      skip_spaces = 1;
	  }
	  // Remove trailing whitespace
	  while (destptr > out_page && *(destptr - 1) == ' ')
	    destptr --;
	  // Put newline character(s)
	  for (j = 0; newline_char_str[j]; j ++)
	    *(destptr ++) = newline_char_str[j];
	  // Position cursor in next line
	  line ++;
	  column = 0;
	  new_line_started = 0;
	}
      }
      if ((line >= text_height && // Current page is full
	   *procptr != '\f') ||   // Next character is not formfeed
	  outbuf[0] == '\0' )     // End of input file
      {
	// Do we actually print this page?
	if (!pagination ||
	    check_range(page_ranges, even_pages, odd_pages, page))
	{
	  // Finalize the page
	  if (pagination)
	  {
	    if (send_ff)
	    {
	      if (page_empty)
		destptr = out_page; // Remove unneeded white space
	      // Send Form Feed
	      *(destptr ++) = '\f';
	    }
	    else if (outbuf[0] != '\0')
	    {
	      // Fill up page with blank lines
	      for (i = 0; i < page_bottom; i ++)
		for (j = 0; newline_char_str[j]; j ++)
		  *(destptr ++) = newline_char_str[j];
	    }
	  }
	  // Allow to handle the finished page as a C string
	  *(destptr ++) = '\0';
	  // Count pages which will actually get printed
	  num_pages ++;
	  if (!pagination)
	  {
	    // Log the page output (only once, when printing the first buffer
	    // load)
	    if (num_pages == 1)
	    {
	      if (log) log(ld, CF_LOGLEVEL_CONTROL, "PAGE: 1 1");
	    }
	    cupsFilePuts(outputfp, out_page);
	  }
	  else if ((num_copies == 1 || !collate) && !reverse_order)
	  {
	    // Log the page output
	    if (log) log(ld, CF_LOGLEVEL_CONTROL,
			 "PAGE: %d %d", num_pages, num_copies);
	    cupsFilePuts(outputfp, out_page);
	  }
	  else
	  {
	    // Save the page in the page array
	    cupsArrayAdd(page_array, strdup(out_page));
	  }
	}
	// Reset for next page
	destptr = out_page;
	page_empty = 1;
	line = 0;
	column = 0;
	new_line_started = 0;
	page ++;
      }
      if (outbuf[0] == '\0') // End of input file
	break;
      if (column == 0 && !new_line_started) // Start of new line
      {
	new_line_started = 1;
	if (line == 0 && pagination)   // Start of new page
	  for (i = 0; i < page_top; i ++)
	    for (j = 0; newline_char_str[j]; j ++)
	      *(destptr ++) = newline_char_str[j];
	for (i = 0; i < page_left; i ++)
	  *(destptr ++) = ' ';
	// Did we wrap a word from the previous line?
	if (wrapped_word)
	{
	  for (p = wrapped_word; *p != '\0'; p ++, column ++)
	    *(destptr ++) = *p;
	  free(wrapped_word);
	  wrapped_word = NULL;
	  page_empty = 0;
	  skip_spaces = 0;
	}
      }
      if (*procptr == '\r' || *procptr == '\n') // CR or LF
      {
	// Only write newline if we are not on the LF of a CR+LF
	if (*procptr == '\r' || previous_is_cr == 0)
	{
	  // Remove trailing whitespace
	  while (destptr > out_page && *(destptr - 1) == ' ')
	    destptr --;
	  // Put newline character(s)
	  for (j = 0; newline_char_str[j]; j ++)
	    *(destptr ++) = newline_char_str[j];
	  // Position cursor in next line
	  line ++;
	  column = 0;
	  new_line_started = 0;
	  // Finished truncating an overlong line
	  skip_rest_of_line = 0;
	  skip_spaces = 0;
	}
	if (*procptr == '\r')
	  previous_is_cr = 1;
	else
	  previous_is_cr = 0;
      }
      else
      {
	previous_is_cr = 0;
	if (*procptr == '\t') // Tab character
	{
	  if (!skip_rest_of_line && !skip_spaces)
	  {
	    *(destptr ++) = ' '; // Always at least one space
	    column ++;
	    // Add spaces to reach next multiple of the tab width
	    for (; column % tab_width != 0 && column < text_width; column ++)
	      *(destptr ++) = ' ';
	  }
	}
	else if (*procptr == '\f') // Form feed
	{
	  // Skip to end of page  
	  if (send_ff)
	    // Mark page full
	    line = text_height;
	  else if (pagination)
	    // Fill page with newlines
	    for (; line < text_height; line ++)
	      for (j = 0; newline_char_str[j]; j ++)
		*(destptr ++) = newline_char_str[j];
	  column = 0;
	  new_line_started = 0;
	  // Finished truncating an overlong line
	  skip_rest_of_line = 0;
	  skip_spaces = 0;
	}
	else if (*procptr == ' ') // Space
	{
	  if (!skip_rest_of_line && !skip_spaces)
	  {
	    *(destptr ++) = *procptr;
	    column ++;
	  }
	}
	else if (*procptr > ' ' || *procptr < '\0') // Regular character
	{
	  if (!skip_rest_of_line)
	  {
	    *(destptr ++) = *procptr;
	    column ++;
	    page_empty = 0;
	    skip_spaces = 0;
	  }
	}
      }
    }
  }
  while (outbuf[0] != '\0'); // End of input file

  close(fd);
  
  if (iconv_close (cd) != 0)
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterTextToText: Error closing iconv encoding conversion session");

  // Error out on an illegal UTF-8 sequence in the input file
  if (result < 0)
    goto error;

  // Print out the page array if we created one
  if (pagination &&
      ((num_copies != 1 && collate) || reverse_order))
  {
    // If we print collated copies, the outer loop (i) goes through the
    // copies, if we do not collate, the inner loop (j) goes through the
    // copies. The other loop only runs for one cycle.
    for (i = 0; i < (collate ? num_copies : 1); i ++)
      for (page = (reverse_order ? num_pages : 1);
	   (reverse_order ? (page >= 1) : (page <= num_pages));
	   page += (reverse_order ? -1 : 1))
      {
	p = (char *)cupsArrayIndex(page_array, page - 1);
	if (log) log(ld, CF_LOGLEVEL_INFO,
		     "cfFilterTextToText: %d %d", page, (collate ? 1 : num_copies));
      }
    // Clean up
    for (page = 0; page < num_pages; page ++)
    {
      p = (char *)cupsArrayIndex(page_array, page);
      free(p);
    }
    cupsArrayDelete(page_array);
  }

  //
  // Cleanup and exit...
  //

 error:

  if (outputfp)
    cupsFileClose(outputfp);

  free(page_ranges);
  free(out_page);

  return (exit_status);
}


//
// 'is_true()' - Check option value for boolean true
//

static int
is_true(const char *value)
{
  if (!value)
    return (0);
  return ((strcasecmp(value, "yes") == 0) ||
	  (strcasecmp(value, "on") == 0) ||
	  (strcasecmp(value, "true") == 0) ||
	  (strcasecmp(value, "1") == 0));
}


//
// 'is_false()' - Check option value for boolean false
//

static int
is_false(const char *value)
{
  if (!value)
    return (0);
  return ((strcasecmp(value, "no") == 0) ||
	  (strcasecmp(value, "off") == 0) ||
	  (strcasecmp(value, "false") == 0) ||
	  (strcasecmp(value, "0") == 0));
}


//
// 'check_range()' - Check to see if the current page is selected for
//                   printing.
//

static int				// O - 1 if selected, 0 otherwise
check_range(char *page_ranges,          // I - Selection of pages to print
	    int even_pages,             // I - Print the even pages
	    int odd_pages,              // I - Print the odd pages
            int page)		        // I - Page number
{
  const char	*range;			// Pointer into range string
  int		lower, upper;		// Lower and upper page numbers

  //
  // See if we only print even or odd pages...
  //

  if (!odd_pages && (page & 1))
    return (0);

  if (!even_pages && !(page & 1))
    return (0);

  //
  // page-ranges option
  //

  if (!page_ranges || page_ranges[0] == '\0')
    return (1);				// No range, print all pages...

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

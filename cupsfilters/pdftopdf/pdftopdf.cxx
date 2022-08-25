// Copyright (c) 2012 Tobias Hoffmann
//
// Copyright (c) 2006-2011, BBR Inc.  All rights reserved.
// MIT Licensed.

#include <config.h>
#include <stdio.h>
#include "cupsfilters/debug-internal.h"
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif
#ifdef HAVE_CUPS_1_7
#include <cups/pwg.h>
#endif /* HAVE_CUPS_1_7 */
#include <iomanip>
#include <sstream>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "pdftopdf-private.h"
#include "cupsfilters/raster.h"
#include "cupsfilters/ipp.h"
#include "cupsfilters/ipp.h"
#include "pdftopdf-processor-private.h"

#include <stdarg.h>


// namespace {}

static bool optGetInt(const char *name,int num_options,cups_option_t *options,int *ret) // {{{
{
  DEBUG_assert(ret);
  const char *val=cupsGetOption(name,num_options,options);
  if (val) {
    *ret=atoi(val);
    return true;
  }
  return false;
}
// }}}

static bool optGetFloat(const char *name,int num_options,cups_option_t *options,float *ret) // {{{
{
  DEBUG_assert(ret);
  const char *val=cupsGetOption(name,num_options,options);
  if (val) {
    *ret=atof(val);
    return true;
  }
  return false;
}
// }}}

static bool is_false(const char *value) // {{{
{
  if (!value) {
    return false;
  }
  return (strcasecmp(value,"no")==0)||
    (strcasecmp(value,"off")==0)||
    (strcasecmp(value,"false")==0);
}
// }}}

static bool is_true(const char *value) // {{{
{
  if (!value) {
    return false;
  }
  return (strcasecmp(value,"yes")==0)||
    (strcasecmp(value,"on")==0)||
    (strcasecmp(value,"true")==0);
}
// }}}


static bool parsePosition(const char *value,pdftopdf_position_e &xpos,pdftopdf_position_e &ypos) // {{{
{
  // ['center','top','left','right','top-left','top-right','bottom','bottom-left','bottom-right']
  xpos=pdftopdf_position_e::CENTER;
  ypos=pdftopdf_position_e::CENTER;
  int next=0;
  if (strcasecmp(value,"center")==0) {
    return true;
  } else if (strncasecmp(value,"top",3)==0) {
    ypos=pdftopdf_position_e::TOP;
    next=3;
  } else if (strncasecmp(value,"bottom",6)==0) {
    ypos=pdftopdf_position_e::BOTTOM;
    next=6;
  }
  if (next) {
    if (value[next]==0) {
      return true;
    } else if (value[next]!='-') {
      return false;
    }
    value+=next+1;
  }
  if (strcasecmp(value,"left")==0) {
    xpos=pdftopdf_position_e::LEFT;
  } else if (strcasecmp(value,"right")==0) {
    xpos=pdftopdf_position_e::RIGHT;
  } else {
    return false;
  }
  return true;
}
// }}}

#include <ctype.h>
static void parseRanges(const char *range,_cfPDFToPDFIntervalSet &ret) // {{{
{
  ret.clear();
  if (!range) {
    ret.add(1); // everything
    ret.finish();
    return;
  }

  int lower,upper;
  while (*range) {
    if (*range=='-') {
      range++;
      upper=strtol(range,(char **)&range,10);
      if (upper>=2147483647) { // see also   cups/encode.c
        ret.add(1);
      } else {
        ret.add(1,upper+1);
      }
    } else {
      lower=strtol(range,(char **)&range,10);
      if (*range=='-') {
        range++;
        if (!isdigit(*range)) {
          ret.add(lower);
        } else {
          upper=strtol(range,(char **)&range,10);
          if (upper>=2147483647) {
            ret.add(lower);
          } else {
            ret.add(lower,upper+1);
          }
        }
      } else {
        ret.add(lower,lower+1);
      }
    }

    if (*range!=',') {
      break;
    }
    range++;
  }
  ret.finish();
}
// }}}

static bool _cfPDFToPDFParseBorder(const char *val,pdftopdf_border_type_e &ret) // {{{
{
  DEBUG_assert(val);
  if (strcasecmp(val,"none")==0) {
    ret=pdftopdf_border_type_e::NONE;
  } else if (strcasecmp(val,"single")==0) {
    ret=pdftopdf_border_type_e::ONE_THIN;
  } else if (strcasecmp(val,"single-thick")==0) {
    ret=pdftopdf_border_type_e::ONE_THICK;
  } else if (strcasecmp(val,"double")==0) {
    ret=pdftopdf_border_type_e::TWO_THIN;
  } else if (strcasecmp(val,"double-thick")==0) {
    ret=pdftopdf_border_type_e::TWO_THICK;
  } else {
    return false;
  }
  return true;
}
// }}}

void getParameters(cf_filter_data_t *data,int num_options,cups_option_t *options,_cfPDFToPDFProcessingParameters &param,pdftopdf_doc_t *doc) // {{{
{
  char *final_content_type = data->final_content_type;
  ipp_t *printer_attrs = data->printer_attrs;
  ipp_t *job_attrs = data->job_attrs;
  ipp_attribute_t *attr;
  const char *val;
   
  if ((val = cupsGetOption("copies",num_options, options)) != NULL ||
      (val = cupsGetOption("Copies", num_options, options)) != NULL ||
      (val = cupsGetOption("num-copies", num_options, options)) != NULL ||
      (val = cupsGetOption("NumCopies", num_options, options)) != NULL)
  {
    int copies = atoi(val);
    if (copies > 0)
      param.num_copies = copies;
  }

  if (param.num_copies==0) {
    param.num_copies=1;
  }

  if((val = cupsGetOption("ipp-attribute-fidelity",num_options,options))!=NULL) {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes") ||
      !strcasecmp(val,"on"))
      param.fidelity = true;
  }

  if((val = cupsGetOption("print-scaling",num_options,options)) != NULL) {
    if(!strcasecmp(val,"auto"))
      param.autoprint = true;
    else if(!strcasecmp(val,"auto-fit"))
      param.autofit = true;
    else if(!strcasecmp(val,"fill"))
      param.fillprint = true;
    else if(!strcasecmp(val,"fit"))
      param.fitplot = true;
    else
      param.cropfit = true;
  }
  else {
  if ((val=cupsGetOption("fitplot",num_options,options)) == NULL) {
    if ((val=cupsGetOption("fit-to-page",num_options,options)) == NULL) {
      val=cupsGetOption("ipp-attribute-fidelity",num_options,options);
    }
  }
  // TODO?  pstops checks =="true", pdftops !is_false  ... pstops says: fitplot only for PS (i.e. not for PDF, cmp. cgpdftopdf)
  param.fitplot=(val)&&(!is_false(val));

  if((val = cupsGetOption("fill",num_options,options))!=0) {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
    {
      param.fillprint = true;
    }
  }
  if((val = cupsGetOption("crop-to-fit",num_options,options))!= NULL){
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
    {
      param.cropfit=1;
    }
  }
  if (!param.autoprint && !param.autofit && !param.fitplot &&
      !param.fillprint && !param.cropfit)
    param.autoprint = true;
  }

  // direction the printer rotates landscape
  // (landscape-orientation-requested-preferred: 4: 90 or 5: -90)
  if (printer_attrs != NULL &&
      (attr = ippFindAttribute(printer_attrs, "landscape-orientation-requested-preferred", IPP_TAG_ZERO)) != NULL &&
      ippGetInteger(attr, 0) == 5)
    param.normal_landscape=ROT_270;
  else
    param.normal_landscape=ROT_90;

  int ipprot;
  param.orientation=ROT_0;
  if ((val=cupsGetOption("landscape",num_options,options)) != NULL) {
    if (!is_false(val)) {
      param.orientation=param.normal_landscape;
    }
  } else if (optGetInt("orientation-requested",num_options,options,&ipprot)) {
    /* IPP orientation values are:
     *   3: 0 degrees,  4: 90 degrees,  5: -90 degrees,  6: 180 degrees
     */
    if ((ipprot<3)||(ipprot>6)) {
      if (ipprot && doc->logfunc)
	doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
		     "cfFilterPDFToPDF: Bad value (%d) for "
		     "orientation-requested, using 0 degrees",
		     ipprot);
      param.no_orientation = true;
    } else {
      static const pdftopdf_rotation_e ipp2rot[4]={ROT_0, ROT_90, ROT_270, ROT_180};
      param.orientation=ipp2rot[ipprot-3];
    }
  } else {
    param.no_orientation = true;
  }

  if (printer_attrs != NULL)
    param.pagesize_requested =
      (cfGetPageDimensions(printer_attrs, job_attrs, num_options, options, NULL,
			   0,
			   &(param.page.width), &(param.page.height),
			   &(param.page.left), &(param.page.bottom),
			   &(param.page.right), &(param.page.top),
			   NULL, NULL) == 1);

  if (param.page.width <= 0 || param.page.height <= 0)
  {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_WARN,
				   "cfFilterPDFToPDF: Could not determine the output page dimensions, falling back to US Letter format");
    param.page.width = 612;
    param.page.height = 792;
  }
  if (param.page.left < 0)
  {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_WARN,
				   "cfFilterPDFToPDF: Could not determine the width of the left margin, falling back to 18 pt/6.35 mm");
    param.page.left = 18.0;
  }
  if (param.page.bottom < 0)
  {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_WARN,
				   "cfFilterPDFToPDF: Could not determine the width of the bottom margin, falling back to 36 pt/12.7 mm");
    param.page.bottom = 36.0;
  }
  if (param.page.right < 0)
  {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_WARN,
				   "cfFilterPDFToPDF: Could not determine the width of the right margin, falling back to 18 pt/6.35 mm");
    param.page.right = param.page.width - 18.0;
  }
  else
    param.page.right = param.page.width - param.page.right;
  if (param.page.top < 0)
  {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_WARN,
				   "cfFilterPDFToPDF: Could not determine the width of the top margin, falling back to 36 pt/12.7 mm");
    param.page.top = param.page.height - 36.0;
  }
  else
    param.page.top = param.page.height - param.page.top;

  param.paper_is_landscape=(param.page.width>param.page.height);

  _cfPDFToPDFPageRect tmp; // borders (before rotation)

  optGetFloat("page-top",num_options,options,&tmp.top);
  optGetFloat("page-left",num_options,options,&tmp.left);
  optGetFloat("page-right",num_options,options,&tmp.right);
  optGetFloat("page-bottom",num_options,options,&tmp.bottom);

  if ((val = cupsGetOption("media-top-margin", num_options, options))
      != NULL)
    tmp.top = atof(val) * 72.0 / 2540.0; 
  if ((val = cupsGetOption("media-left-margin", num_options, options))
      != NULL)
    tmp.left = atof(val) * 72.0 / 2540.0; 
  if ((val = cupsGetOption("media-right-margin", num_options, options))
      != NULL)
    tmp.right = atof(val) * 72.0 / 2540.0; 
  if ((val = cupsGetOption("media-bottom-margin", num_options, options))
      != NULL)
    tmp.bottom = atof(val) * 72.0 / 2540.0; 

  if ((param.orientation==ROT_90)||(param.orientation==ROT_270)) { // unrotate page
    // NaN stays NaN
    tmp.right=param.page.height-tmp.right;
    tmp.top=param.page.width-tmp.top;
    tmp.rotate_move(param.orientation,param.page.height,param.page.width);
  } else {
    tmp.right=param.page.width-tmp.right;
    tmp.top=param.page.height-tmp.top;
    tmp.rotate_move(param.orientation,param.page.width,param.page.height);
  }

  param.page.set(tmp); // replace values, where tmp.* != NaN  (because tmp needed rotation, param.page not!)

  if ((val = cfIPPAttrEnumValForPrinter(printer_attrs, job_attrs, "sides")) !=
      NULL &&
      strncmp(val, "two-sided-", 10) == 0) {
    param.duplex=true;
  } else if (is_true(cupsGetOption("Duplex", num_options, options))) {
    param.duplex=true;
    param.set_duplex=true;
  } else if ((val=cupsGetOption("sides",num_options,options)) != NULL) {
    if ((strcasecmp(val,"two-sided-long-edge")==0)||
	(strcasecmp(val,"two-sided-short-edge")==0)) {
      param.duplex=true;
      param.set_duplex=true;
    } else if (strcasecmp(val,"one-sided")!=0) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported sides value %s, "
				     "using sides=one-sided!", val);
    }
  }

  // default nup is 1
  int nup=1;
  if (optGetInt("number-up",num_options,options,&nup)) {
    if (!_cfPDFToPDFNupParameters::possible(nup)) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported number-up value "
				     "%d, using number-up=1!", nup);
      nup=1;
    }
// TODO   ;  TODO? nup enabled? ... fitplot
//    _cfPDFToPDFNupParameters::calculate(nup,param.nup);
    _cfPDFToPDFNupParameters::preset(nup,param.nup);
  }

  if ((val=cupsGetOption("number-up-layout",num_options,options)) != NULL) {
    if (!_cfPDFToPDFParseNupLayout(val,param.nup)) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported number-up-layout "
				     "%s, using number-up-layout=lrtb!" ,val);
      param.nup.first=pdftopdf_axis_e::X;
      param.nup.xstart=pdftopdf_position_e::LEFT;
      param.nup.ystart=pdftopdf_position_e::TOP;
    }
  }

  if ((val=cupsGetOption("page-border",num_options,options)) != NULL) {
    if (!_cfPDFToPDFParseBorder(val,param.border)) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported page-border value "
				     "%s, using page-border=none!", val);
      param.border=pdftopdf_border_type_e::NONE;
    }
  }

  if ((val=cupsGetOption("OutputOrder",num_options,options)) != NULL ||
      (val=cupsGetOption("output-order",num_options,options)) != NULL ||
      (val=cupsGetOption("page-delivery",num_options,options)) != NULL)
  {
    param.reverse = (strcasecmp(val, "Reverse") == 0 ||
		     strcasecmp(val, "reverse-order") == 0);
  }
  else
    param.reverse = cfIPPReverseOutput(printer_attrs, job_attrs);

  std::string rawlabel;
  char *classification = getenv("CLASSIFICATION");
  if (classification)
    rawlabel.append (classification);

  if ((val=cupsGetOption("page-label", num_options, options)) != NULL) {
    if (!rawlabel.empty())
      rawlabel.append (" - ");
    rawlabel.append(cupsGetOption("page-label",num_options,options));
  }

  std::ostringstream cookedlabel;
  for (std::string::iterator it = rawlabel.begin();
       it != rawlabel.end ();
       ++it) {
    if (*it < 32 || *it > 126)
      cookedlabel << "\\" << std::oct << std::setfill('0') << std::setw(3) << (unsigned int) *it;
    else
      cookedlabel.put (*it);
  }
  param.page_label = cookedlabel.str ();

  if ((val=cupsGetOption("page-set",num_options,options)) != NULL) {
    if (strcasecmp(val,"even")==0) {
      param.odd_pages=false;
    } else if (strcasecmp(val,"odd")==0) {
      param.even_pages=false;
    } else if (strcasecmp(val,"all")!=0) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported page-set value %s, "
				     "using page-set=all!", val);
    }
  }

  if ((val=cupsGetOption("page-ranges",num_options,options)) != NULL) {
    parseRanges(val,param.page_ranges);
  }

  if ((val=cupsGetOption("input-page-ranges",num_options,options)) !=NULL){
    parseRanges(val,param.input_page_ranges);
  }

  if((val = cupsGetOption("mirror", num_options, options)) != NULL ||
     (val = cupsGetOption("mirror-print", num_options, options)) != NULL)
    param.mirror=is_true(val);

  param.booklet=pdftopdf_booklet_mode_e::CF_PDFTOPDF_BOOKLET_OFF;
  if ((val=cupsGetOption("booklet",num_options,options)) != NULL) {
    if (strcasecmp(val,"shuffle-only")==0) {
      param.booklet=pdftopdf_booklet_mode_e::CF_PDFTOPDF_BOOKLET_JUST_SHUFFLE;
    } else if (is_true(val)) {
      param.booklet=pdftopdf_booklet_mode_e::CF_PDFTOPDF_BOOKLET_ON;
    } else if (!is_false(val)) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported booklet value %s, "
				     "using booklet=off!", val);
    }
  }
  param.book_signature=-1;
  if (optGetInt("booklet-signature",num_options,options,&param.book_signature)) {
    if (param.book_signature==0) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported booklet-signature "
				     "value, using booklet-signature=-1 "
				     "(all)!", val);
      param.book_signature=-1;
    }
  }

  if ((val=cupsGetOption("position",num_options,options)) != NULL) {
    if (!parsePosition(val,param.xpos,param.ypos)) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unrecognized position value "
				     "%s, using position=center!", val);
      param.xpos=pdftopdf_position_e::CENTER;
      param.ypos=pdftopdf_position_e::CENTER;
    }
  }

  // Collate
  if (is_true(cupsGetOption("Collate", num_options, options)))
    param.collate = true;
  else if ((val = cupsGetOption("sheet-collate", num_options, options)) != NULL)
    param.collate = (strcasecmp(val, "uncollated") != 0);
  else if (((val = cupsGetOption("multiple-document-handling",
				 num_options, options)) != NULL &&
	    (strcasecmp(val, "separate-documents-collated-copies") == 0 ||
	     strcasecmp(val, "separate-documents-uncollated-copies") == 0 ||
	     strcasecmp(val, "single-document") == 0 ||
	     strcasecmp(val, "single-document-new-sheet") == 0)) ||
	   (val = cfIPPAttrEnumValForPrinter(printer_attrs, job_attrs,
					     "multiple-document-handling")) !=
	   NULL)
   /* This IPP attribute is unnecessarily complicated:
    *   single-document, separate-documents-collated-copies, single-document-new-sheet:
    *      -> collate (true)
    *   separate-documents-uncollated-copies:
    *      -> can be uncollated (false)
    */
    param.collate =
      (strcasecmp(val, "separate-documents-uncollated-copies") != 0);

  /*
  // TODO: scaling
  // TODO: natural-scaling

  scaling


  if ((val = cupsGetOption("scaling",num_options,options)) != 0) {
    scaling = atoi(val) * 0.01;
    fitplot = true;
  } else if (fitplot) {
    scaling = 1.0;
  }
  if ((val = cupsGetOption("natural-scaling",num_options,options)) != 0) {
    naturalScaling = atoi(val) * 0.01;
  }

  */

  // make pages a multiple of two (only considered when duplex is on).
  // i.e. printer has hardware-duplex, but needs pre-inserted filler pages
  // FIXME? pdftopdf also supports it as cmdline option (via checkFeature())
  param.even_duplex =
    (param.duplex &&
     is_true(cupsGetOption("even-duplex", num_options, options)));

  // TODO? pdftopdf* ?
  // TODO?! pdftopdfAutoRotate

  // TODO?!  choose default by whether pdfautoratate filter has already been run (e.g. by mimetype)
  param.auto_rotate=(!is_false(cupsGetOption("pdfAutoRotate",num_options,options)) &&
		    !is_false(cupsGetOption("pdftopdfAutoRotate",num_options,options)));

  // Do we have to do the page logging in page_log?

  // CUPS standard is that the last filter (not the backend, usually the
  // printer driver) does page logging in the /var/log/cups/page_log file
  // by outputting "PAGE: <# of current page> <# of copies>" to stderr.

  // cfFilterPDFToPDF() would have to do this only for PDF printers as
  // in this case cfFilterPDFToPDF() is the last filter, but some of
  // the other filters are not able to do the logging because they do
  // not have access to the number of pages of the file to be printed,
  // so cfFilterPDFToPDF() overtakes their logging duty.

  // Check whether page logging is forced or suppressed by the options
  if ((val = cupsGetOption("pdf-filter-page-logging",
			   num_options, options)) != NULL) {
    if (strcasecmp(val,"auto") == 0) {
      param.page_logging = -1;
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Automatic page logging "
				     "selected by options.");
    } else if (is_true(val)) {
      param.page_logging = 1;
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Forced page logging "
				     "selected by options.");
    } else if (is_false(val)) {
      param.page_logging = 0;
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Suppressed page "
				     "logging selected by options.");
    } else {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported page "
				     "logging setting "
				     "\"pdf-filter-page-logging=%s\", "
				     "using \"auto\"!", val);
      param.page_logging = -1;
    }
  }

  if (param.page_logging == -1) {
    // We determine whether to log pages or not
    // using the output data MIME type. log pages only when the output is
    // either pdf or PWG Raster
    if (final_content_type &&
	(strcasestr(final_content_type, "/pdf") ||
	 strcasestr(final_content_type, "/vnd.cups-pdf") ||
	 strcasestr(final_content_type, "/pwg-raster")))
      param.page_logging = 1;
    else
      param.page_logging = 0;

    // If final_content_type is not clearly available we are not sure whether
    // to log pages or not
    if((char*)final_content_type==NULL ||
       sizeof(final_content_type)==0 ||
       final_content_type[0]=='\0'){
      param.page_logging = -1;
    }
    if (doc->logfunc)
    {
      doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		   "cfFilterPDFToPDF: Determined whether to "
		   "log pages or not using output data type.");
      doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		   "final_content_type = %s => page_logging = %d",
		   final_content_type ? final_content_type : "NULL",
		   param.page_logging);
    }
    if (param.page_logging == -1)
      param.page_logging = 0;
  }
}
// }}}

void calculate(int num_options,
	       cups_option_t *options,
	       _cfPDFToPDFProcessingParameters &param,
	       char *final_content_type) // {{{
{
  const char       *val;
  bool             hw_copies = false,
                   hw_collate = false;


  // Check options for caller's instructions about hardware copies/collate
  if ((val = cupsGetOption("hardware-copies",
			   num_options, options)) != NULL)
    // Use hardware copies according to the caller's instructions
    hw_copies = is_true(val);
  else
    // Caller did not tell us whether the printer does Hardware copies
    // or not, so we assume hardware copies on PDF printers, and software
    // copies on other (usually raster) printers or if we do not know the
    // final output format.
    hw_copies = (final_content_type &&
		 (strcasestr(final_content_type, "/pdf") ||
		  strcasestr(final_content_type, "/vnd.cups-pdf")));
  if (hw_copies)
  {
    if ((val = cupsGetOption("hardware-collate",
			    num_options, options)) != NULL)
      // Use hardware collate according to the caller's instructions
      hw_collate = is_true(val);
    else
      // Check output format MIME type whether it is
      // of a driverless IPP printer (PDF, Apple Raster, PWG Raster, PCLm).
      // These printers do always hardware collate if they do hardware copies.
      // https://github.com/apple/cups/issues/5433
      hw_collate = (final_content_type &&
		    (strcasestr(final_content_type, "/pdf") ||
		     strcasestr(final_content_type, "/vnd.cups-pdf") ||
		     strcasestr(final_content_type, "/pwg-raster") ||
		     strcasestr(final_content_type, "/urf") ||
		     strcasestr(final_content_type, "/PCLm")));
  }
  
  if (param.reverse && param.duplex)
    // Enable even_duplex or the first page may be empty.
    param.even_duplex=true; // disabled later, if non-duplex

  if (param.num_copies==1) {
    param.device_copies=1;
    // collate is never needed for a single copy
    param.collate=false; // (does not make a big difference for us)
  } else if (hw_copies) { // hw copy generation available
    param.device_copies=param.num_copies;
    if (param.collate) { // collate requested by user
      param.device_collate = hw_collate;
      if (!param.device_collate) {
	// printer can't hw collate -> we must copy collated in sw
	param.device_copies=1;
      }
    } // else: printer copies w/o collate and takes care of duplex/even_duplex
  }
  else { // sw copies
    param.device_copies=1;
    if (param.duplex) { // &&(num_copies>1)
      // sw collate + even_duplex must be forced to prevent copies on the backsides
      param.collate=true;
      param.device_collate=false;
    }
  }

  if (param.device_copies != 1) //hw copy
    param.num_copies=1; // disable sw copy

  if (param.duplex &&
      param.collate && !param.device_collate) // software collate
    param.even_duplex=true; // fillers always needed

  if (!param.duplex)
    param.even_duplex=false;
}
// }}}

// reads from stdin into temporary file. returns FILE *  or NULL on error
FILE *copy_fd_to_temp(int infd, pdftopdf_doc_t *doc) // {{{
{
  char buf[BUFSIZ];
  int n;

  // FIXME:  what does >buf mean here?
  int outfd=cupsTempFd(buf,sizeof(buf));
  if (outfd<0) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				   "cfFilterPDFToPDF: Can't create temporary file");
    return NULL;
  }
  // remove name
  unlink(buf);

  // copy stdin to the tmp file
  while ((n=read(infd,buf,BUFSIZ)) > 0) {
    if (write(outfd,buf,n) != n) {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Can't copy stdin to temporary "
				     "file");
      close(outfd);
      return NULL;
    }
  }
  if (lseek(outfd,0,SEEK_SET) < 0) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				   "cfFilterPDFToPDF: Can't rewind temporary file");
    close(outfd);
    return NULL;
  }

  FILE *f;
  if ((f=fdopen(outfd,"rb")) == 0) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				   "cfFilterPDFToPDF: Can't fdopen temporary file");
    close(outfd);
    return NULL;
  }
  return f;
}
// }}}

// check whether a given file is empty
bool is_empty(FILE *f) // {{{
{
  char buf[1];

  // Try to read a single byte of data
  if (fread(buf, 1, 1, f) == 0)
    return true;

  rewind(f);

  return false;
}
// }}}


int                           /* O - Error status */
cfFilterPDFToPDF(int inputfd,         /* I - File descriptor input stream */
	 int outputfd,        /* I - File descriptor output stream */
	 int inputseekable,   /* I - Is input stream seekable? */
	 cf_filter_data_t *data, /* I - Job and printer data */
	 void *parameters)    /* I - Filter-specific parameters (unused) */
{
  pdftopdf_doc_t     doc;         /* Document information */
  char               *final_content_type = data->final_content_type;
  FILE               *inputfp,
                     *outputfp;
  const char         *t;
  int                streaming = 0;
  size_t             bytes;
  char               buf[BUFSIZ];
  cf_logfunc_t   log = data->logfunc;
  void               *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void               *icd = data->iscanceleddata;
  int num_options = 0;
  cups_option_t *options = NULL;


  try {
    _cfPDFToPDFProcessingParameters param;

    param.job_id=data->job_id;
    param.user=data->job_user;
    param.title=data->job_title;
    param.num_copies=data->copies;
    param.copies_to_be_logged=data->copies;

    // TODO?! sanity checks

    doc.logfunc = log;
    doc.logdata = ld;
    doc.iscanceledfunc = iscanceled;
    doc.iscanceleddata = icd;

    num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

    getParameters(data, num_options, options, param, &doc);

    calculate(num_options, options, param, final_content_type);

#ifdef DEBUG
    param.dump(&doc);
#endif

    // If we are in streaming mode we only apply JCL and do not run the
    // job through QPDL (so no page management, form flattening,
    // page size/orientation adjustment, ...)
    if ((t = cupsGetOption("filter-streaming-mode",
			   num_options, options)) !=
	NULL &&
	(strcasecmp(t, "false") && strcasecmp(t, "off") &
	 strcasecmp(t, "no"))) {
      streaming = 1;
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPDFToPDF: Streaming mode: No PDF processing, only adding of JCL");
    }

    cupsFreeOptions(num_options, options);

    std::unique_ptr<_cfPDFToPDFProcessor> proc(_cfPDFToPDFFactory::processor());

    if ((inputseekable && inputfd > 0) || streaming) {
      if ((inputfp = fdopen(inputfd, "rb")) == NULL)
	return 1;
    } else {
      if ((inputfp = copy_fd_to_temp(inputfd, &doc)) == NULL)
	return 1;
    }

    if (!streaming) {
      if (is_empty(inputfp)) {
	fclose(inputfp);
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPDFToPDF: Input is empty, outputting empty file.");
	return 0;
      }

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterPDFToPDF: Processing PDF input with QPDF: Page-ranges, page-set, number-up, booklet, size adjustment, ...");

      // Load the PDF input data into QPDF
      if (!proc->load_file(inputfp, &doc, CF_PDFTOPDF_WILL_STAY_ALIVE, 1)) {
	fclose(inputfp);
	return 1;
      }

      // Process the PDF input data
      if (!_cfProcessPDFToPDF(*proc, param, &doc))
	return 2;

      // Pass information to subsequent filters via PDF comments
      std::vector<std::string> output;

      output.push_back("% This file was generated by pdftopdf");

      // This is not standard, but like PostScript. 
      if (param.device_copies > 0)
      {
	char buf[256];
	snprintf(buf, sizeof(buf), "%d", param.device_copies);
	output.push_back(std::string("%%PDFTOPDFNumCopies : ")+buf);

	if (param.device_collate)
	  output.push_back("%%PDFTOPDFCollate : true");
	else
	  output.push_back("%%PDFTOPDFCollate : false");
      }

      proc->set_comments(output);
    }

    outputfp = fdopen(outputfd, "w");
    if (outputfp == NULL)
      return 1;

    if (!streaming) {
      // Pass on the processed input data
      proc->emit_file(outputfp, &doc, CF_PDFTOPDF_WILL_STAY_ALIVE);
      // proc->emit_filename(NULL);
    } else {
      // Pass through the input data
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterPDFToPDF: Passing on unchanged PDF data from input");
      while ((bytes = fread(buf, 1, sizeof(buf), inputfp)) > 0)
	if (fwrite(buf, 1, bytes, outputfp) != bytes)
	  break;
      fclose(inputfp);
    }

    fclose(outputfp);
  } catch (std::exception &e) {
    // TODO? exception type
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPDFToPDF: Exception: %s",e.what());
    return 5;
  } catch (...) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPDFToPDF: Unknown exception caught. Exiting.");
    return 6;
  }

  return 0;
}

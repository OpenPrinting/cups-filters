// Copyright (c) 2012 Tobias Hoffmann
//
// Copyright (c) 2006-2011, BBR Inc.  All rights reserved.
// MIT Licensed.

#include <config.h>
#include <stdio.h>
#include <assert.h>
#include <cups/cups.h>
#include <cups/ppd.h>
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

#include "pdftopdf_processor.h"
#include "pdftopdf_jcl.h"

#include <stdarg.h>
static void error(const char *fmt,...) // {{{
{
  va_list ap;
  va_start(ap,fmt);

  fputs("ERROR: ",stderr);
  vfprintf(stderr,fmt,ap);
  fputs("\n",stderr);

  va_end(ap);
}
// }}}

// namespace {}

void setFinalPPD(ppd_file_t *ppd,const ProcessingParameters &param)
{
  if ((param.booklet==BOOKLET_ON)&&(ppdFindOption(ppd,"Duplex"))) {
    // TODO: elsewhere, better
    ppdMarkOption(ppd,"Duplex","DuplexTumble");
    // TODO? sides=two-sided-short-edge
  }

  // for compatibility
  if ((param.setDuplex)&&(ppdFindOption(ppd,"Duplex")!=NULL)) {
    ppdMarkOption(ppd,"Duplex","True");
    ppdMarkOption(ppd,"Duplex","On");
  }

  // we do it, printer should not
  ppd_choice_t *choice;
  if ((choice=ppdFindMarkedChoice(ppd,"MirrorPrint")) != NULL) {
    choice->marked=0;
  }
}

// for choice, only overwrites ret if found in ppd
static bool ppdGetInt(ppd_file_t *ppd,const char *name,int *ret) // {{{
{
  assert(ret);
  ppd_choice_t *choice=ppdFindMarkedChoice(ppd,name); // !ppd is ok.
  if (choice) {
    *ret=atoi(choice->choice);
    return true;
  }
  return false;
}
// }}}

static bool optGetInt(const char *name,int num_options,cups_option_t *options,int *ret) // {{{
{
  assert(ret);
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
  assert(ret);
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

static bool ppdGetDuplex(ppd_file_t *ppd) // {{{
{
  const char **option, **choice;
  const char *option_names[] = {
    "Duplex",
    "JCLDuplex",
    "EFDuplex",
    "KD03Duplex",
    NULL
  };
  const char *choice_names[] = {
    "DuplexNoTumble",
    "DuplexTumble",
    "LongEdge",
    "ShortEdge",
    "Top",
    "Bottom",
    NULL
  };
  for (option = option_names; *option; option ++)
    for (choice = choice_names; *choice; choice ++)
      if (ppdIsMarked(ppd, *option, *choice))
	return 1;
  return 0;
}
// }}}

// TODO: enum
static bool ppdDefaultOrder(ppd_file_t *ppd) // {{{  -- is reverse?
{
  ppd_choice_t *choice;
  ppd_attr_t *attr;
  const char *val=NULL;

  // Figure out the right default output order from the PPD file...
  if ((choice=ppdFindMarkedChoice(ppd,"OutputOrder")) != NULL) {
    val=choice->choice;
  } else if (((choice=ppdFindMarkedChoice(ppd,"OutputBin")) != NULL)&&
	     ((attr=ppdFindAttr(ppd,"PageStackOrder",choice->choice)) != NULL)) {
    val=attr->value;
  } else if ((attr=ppdFindAttr(ppd,"DefaultOutputOrder",0)) != NULL) {
    val=attr->value;
  }
  if ((!val)||(strcasecmp(val,"Normal")==0)||(strcasecmp(val,"same-order")==0)) {
    return false;
  } else if (strcasecmp(val,"Reverse")==0||(strcasecmp(val,"reverse-order")==0)) {
    return true;
  }
  error("Unsupported output-order value %s, using 'normal'!",val);
  return false;
}
// }}}

static bool optGetCollate(int num_options,cups_option_t *options) // {{{
{
  if (is_true(cupsGetOption("Collate",num_options,options))) {
    return true;
  }

  const char *val=NULL;
  if ((val=cupsGetOption("multiple-document-handling",num_options,options)) != NULL) {
   /* This IPP attribute is unnecessarily complicated:
    *   single-document, separate-documents-collated-copies, single-document-new-sheet:
    *      -> collate (true)
    *   separate-documents-uncollated-copies:
    *      -> can be uncollated (false)
    */
    return (strcasecmp(val,"separate-documents-uncollated-copies")!=0);
  }

  if ((val=cupsGetOption("sheet-collate",num_options,options)) != NULL) {
    return (strcasecmp(val,"uncollated")!=0);
  }

  return false;
}
// }}}

static bool parsePosition(const char *value,Position &xpos,Position &ypos) // {{{
{
  // ['center','top','left','right','top-left','top-right','bottom','bottom-left','bottom-right']
  xpos=Position::CENTER;
  ypos=Position::CENTER;
  int next=0;
  if (strcasecmp(value,"center")==0) {
    return true;
  } else if (strncasecmp(value,"top",3)==0) {
    ypos=Position::TOP;
    next=3;
  } else if (strncasecmp(value,"bottom",6)==0) {
    ypos=Position::BOTTOM;
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
    xpos=Position::LEFT;
  } else if (strcasecmp(value,"right")==0) {
    xpos=Position::RIGHT;
  } else {
    return false;
  }
  return true;
}
// }}}

#include <ctype.h>
static void parseRanges(const char *range,IntervalSet &ret) // {{{
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
            ret.add(1);
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

static bool parseBorder(const char *val,BorderType &ret) // {{{
{
  assert(val);
  if (strcasecmp(val,"none")==0) {
    ret=BorderType::NONE;
  } else if (strcasecmp(val,"single")==0) {
    ret=BorderType::ONE_THIN;
  } else if (strcasecmp(val,"single-thick")==0) {
    ret=BorderType::ONE_THICK;
  } else if (strcasecmp(val,"double")==0) {
    ret=BorderType::TWO_THIN;
  } else if (strcasecmp(val,"double-thick")==0) {
    ret=BorderType::TWO_THICK;
  } else {
    return false;
  }
  return true;
}
// }}}

void getParameters(ppd_file_t *ppd,int num_options,cups_option_t *options,ProcessingParameters &param) // {{{
{
  const char *val;

  if ((val = cupsGetOption("copies",num_options,options)) != NULL) {
    int copies = atoi(val);
    if (copies > 0)
      param.numCopies = copies;
  }
  // param.numCopies initially from commandline
  if (param.numCopies==1) {
    ppdGetInt(ppd,"Copies",&param.numCopies);
  }
  if (param.numCopies==0) {
    param.numCopies=1;
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
  }

  if (ppd && (ppd->landscape < 0)) { // direction the printer rotates landscape (90 or -90)
    param.normal_landscape=ROT_270;
  } else {
    param.normal_landscape=ROT_90;
  }

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
      error("Bad value (%d) for orientation-requested, using 0 degrees",ipprot);
    } else {
      static const Rotation ipp2rot[4]={ROT_0, ROT_90, ROT_270, ROT_180};
      param.orientation=ipp2rot[ipprot-3];
    }
  } else {
    param.noOrientation = true;
  }

  ppd_size_t *pagesize;
  // param.page default is letter, border 36,18
  if ((pagesize=ppdPageSize(ppd,0)) != NULL) { // "already rotated"
    param.page.top=pagesize->top;
    param.page.left=pagesize->left;
    param.page.right=pagesize->right;
    param.page.bottom=pagesize->bottom;
    param.page.width=pagesize->width;
    param.page.height=pagesize->length;
  }
#ifdef HAVE_CUPS_1_7
  else {
    if ((val = cupsGetOption("media-size", num_options, options)) != NULL ||
	(val = cupsGetOption("MediaSize", num_options, options)) != NULL ||
	(val = cupsGetOption("page-size", num_options, options)) != NULL ||
	(val = cupsGetOption("PageSize", num_options, options)) != NULL) {
      pwg_media_t *size_found = NULL;
      fprintf(stderr, "DEBUG: Page size from command line: %s\n", val);
      if ((size_found = pwgMediaForPWG(val)) == NULL)
	if ((size_found = pwgMediaForPPD(val)) == NULL)
	  size_found = pwgMediaForLegacy(val);
      if (size_found != NULL) {
	param.page.width = size_found->width * 72.0 / 2540.0;
        param.page.height = size_found->length * 72.0 / 2540.0;
	param.page.top=param.page.bottom=36.0;
	param.page.right=param.page.left=18.0;
	param.page.right=param.page.width-param.page.right;
	param.page.top=param.page.height-param.page.top;
	fprintf(stderr, "DEBUG: Width: %f, Length: %f\n", param.page.width, param.page.height);
      }
      else
	fprintf(stderr, "DEBUG: Unsupported page size %s.\n", val);
    }
  }
#endif /* HAVE_CUPS_1_7 */

  param.paper_is_landscape=(param.page.width>param.page.height);

  PageRect tmp; // borders (before rotation)

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

  if (ppdGetDuplex(ppd)) {
    param.duplex=true;
  } else if (is_true(cupsGetOption("Duplex",num_options,options))) {
    param.duplex=true;
    param.setDuplex=true;
  } else if ((val=cupsGetOption("sides",num_options,options)) != NULL) {
    if ((strcasecmp(val,"two-sided-long-edge")==0)||
	(strcasecmp(val,"two-sided-short-edge")==0)) {
      param.duplex=true;
      param.setDuplex=true;
    } else if (strcasecmp(val,"one-sided")!=0) {
      error("Unsupported sides value %s, using sides=one-sided!",val);
    }
  }

  // default nup is 1
  int nup=1;
  if (optGetInt("number-up",num_options,options,&nup)) {
    if (!NupParameters::possible(nup)) {
      error("Unsupported number-up value %d, using number-up=1!",nup);
      nup=1;
    }
// TODO   ;  TODO? nup enabled? ... fitplot
//    NupParameters::calculate(nup,param.nup);
    NupParameters::preset(nup,param.nup);
  }

  if ((val=cupsGetOption("number-up-layout",num_options,options)) != NULL) {
    if (!parseNupLayout(val,param.nup)) {
      error("Unsupported number-up-layout %s, using number-up-layout=lrtb!",val);
      param.nup.first=Axis::X;
      param.nup.xstart=Position::LEFT;
      param.nup.ystart=Position::TOP;
    }
  }

  if ((val=cupsGetOption("page-border",num_options,options)) != NULL) {
    if (!parseBorder(val,param.border)) {
      error("Unsupported page-border value %s, using page-border=none!",val);
      param.border=BorderType::NONE;
    }
  }

  if ((val=cupsGetOption("OutputOrder",num_options,options)) != NULL ||
      (val=cupsGetOption("output-order",num_options,options)) != NULL ||
      (val=cupsGetOption("page-delivery",num_options,options)) != NULL) {
    param.reverse = (strcasecmp(val, "Reverse") == 0 ||
		     strcasecmp(val, "reverse-order") == 0);
  } else if (ppd) {
    param.reverse=ppdDefaultOrder(ppd);
  }

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
  param.pageLabel = cookedlabel.str ();

  if ((val=cupsGetOption("page-set",num_options,options)) != NULL) {
    if (strcasecmp(val,"even")==0) {
      param.oddPages=false;
    } else if (strcasecmp(val,"odd")==0) {
      param.evenPages=false;
    } else if (strcasecmp(val,"all")!=0) {
      error("Unsupported page-set value %s, using page-set=all!",val);
    }
  }

  if ((val=cupsGetOption("page-ranges",num_options,options)) != NULL) {
    parseRanges(val,param.pageRange);
  }

  ppd_choice_t *choice;
  if ((choice=ppdFindMarkedChoice(ppd,"MirrorPrint")) != NULL) {
    val=choice->choice;
  } else {
    val=cupsGetOption("mirror",num_options,options);
  }
  param.mirror=is_true(val);

  if ((val=cupsGetOption("emit-jcl",num_options,options)) != NULL) {
    param.emitJCL=!is_false(val)&&(strcmp(val,"0")!=0);
  }

  param.booklet=BookletMode::BOOKLET_OFF;
  if ((val=cupsGetOption("booklet",num_options,options)) != NULL) {
    if (strcasecmp(val,"shuffle-only")==0) {
      param.booklet=BookletMode::BOOKLET_JUSTSHUFFLE;
    } else if (is_true(val)) {
      param.booklet=BookletMode::BOOKLET_ON;
    } else if (!is_false(val)) {
      error("Unsupported booklet value %s, using booklet=off!",val);
    }
  }
  param.bookSignature=-1;
  if (optGetInt("booklet-signature",num_options,options,&param.bookSignature)) {
    if (param.bookSignature==0) {
      error("Unsupported booklet-signature value, using booklet-signature=-1 (all)!",val);
      param.bookSignature=-1;
    }
  }

  if ((val=cupsGetOption("position",num_options,options)) != NULL) {
    if (!parsePosition(val,param.xpos,param.ypos)) {
      error("Unrecognized position value %s, using position=center!",val);
      param.xpos=Position::CENTER;
      param.ypos=Position::CENTER;
    }
  }

  param.collate=optGetCollate(num_options,options);
  // FIXME? pdftopdf also considers if ppdCollate is set (only when cupsGetOption is /not/ given) [and if is_true overrides param.collate=true]  -- pstops does not

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

bool checkFeature(const char *feature, int num_options, cups_option_t *options) // {{{
{
  const char *val;
  ppd_attr_t *attr;

  return ((val=cupsGetOption(feature,num_options,options)) != NULL && is_true(val)) ||
         ((attr=ppdFindAttr(ppd,feature,0)) != NULL && is_true(attr->val));
}
// }}}
*/

  // make pages a multiple of two (only considered when duplex is on).
  // i.e. printer has hardware-duplex, but needs pre-inserted filler pages
  // FIXME? pdftopdf also supports it as cmdline option (via checkFeature())
  ppd_attr_t *attr;
  if ((attr=ppdFindAttr(ppd,"cupsEvenDuplex",0)) != NULL) {
    param.evenDuplex=is_true(attr->value);
  }

  // TODO? pdftopdf* ?
  // TODO?! pdftopdfAutoRotate

  // TODO?!  choose default by whether pdfautoratate filter has already been run (e.g. by mimetype)
  param.autoRotate=(!is_false(cupsGetOption("pdfAutoRotate",num_options,options)) &&
		    !is_false(cupsGetOption("pdftopdfAutoRotate",num_options,options)));

  // Do we have to do the page logging in page_log?

  // CUPS standard is that the last filter (not the backend, usually the
  // printer driver) does page logging in the /var/log/cups/page_log file
  // by outputting "PAGE: <# of current page> <# of copies>" to stderr.

  // pdftopdf would have to do this only for PDF printers as in this case
  // pdftopdf is the last filter, but some of the other filters are not
  // able to do the logging because they do not have access to the number
  // of pages of the file to be printed, so pdftopdf overtakes their logging
  // duty.

  // The filters currently are:
  // - foomatic-rip (lets Ghostscript convert PDF to printer's format via
  //   built-in drivers, no access to the PDF content)
  // - gstopxl (uses Ghostscript, like foomatic-rip)
  // - *toraster on IPP Everywhere printers (then *toraster gets the last
  //   filter, the case if FINAL_CONTENT_TYPE env var is "image/pwg-raster")
  // - hpps (bug)

  // Check whether page logging is forced or suppressed by the command line
  if ((val=cupsGetOption("page-logging",num_options,options)) != NULL) {
    if (strcasecmp(val,"auto") == 0) {
      param.page_logging = -1;
      fprintf(stderr,
	      "DEBUG: pdftopdf: Automatic page logging selected by command line.\n");
    } else if (is_true(val)) {
      param.page_logging = 1;
      fprintf(stderr,
	      "DEBUG: pdftopdf: Forced page logging selected by command line.\n");
    } else if (is_false(val)) {
      param.page_logging = 0;
      fprintf(stderr,
	      "DEBUG: pdftopdf: Suppressed page logging selected by command line.\n");
    } else {
      error("Unsupported page-logging value %s, using page-logging=auto!",val);
      param.page_logging = -1;
    }
  }

  if (param.page_logging == -1) {
    // Determine the last filter in the chain via cupsFilter(2) lines of the
    // PPD file and FINAL_CONTENT_TYPE
    if (!ppd) {
      // If there is no PPD do not log when not requested by command line
      param.page_logging = 0;
      fprintf(stderr,
	      "DEBUG: pdftopdf: No PPD file specified, could not determine whether to log pages or not, so turned off page logging.\n");
    } else {
      char *final_content_type = getenv("FINAL_CONTENT_TYPE");
      char *lastfilter = NULL;
      if (final_content_type == NULL) {
	// No FINAL_CONTENT_TYPE env variable set, we cannot determine
	// whether we have to log pages, so do not log.
	param.page_logging = 0;
	fprintf(stderr,
		"DEBUG: pdftopdf: No FINAL_CONTENT_TYPE environment variable, could not determine whether to log pages or not, so turned off page logging.\n");
      // Proceed depending on number of cupsFilter(2) lines in PPD
      } else if (ppd->num_filters == 0) {
	// No filter line, manufacturer-supplied PostScript PPD
	// In this case pstops, called by pdftops, does the logging
	param.page_logging = 0;
      } else if (ppd->num_filters == 1) {
	// One filter line, so this one filter is the last filter
	lastfilter = ppd->filters[0];
      } else {
	// More than one filter line, determine the one which got
	// actually used via FINAL_CONTENT_TYPE
	ppd_attr_t *ppd_attr;
	if ((ppd_attr = ppdFindAttr(ppd, "cupsFilter2", NULL)) != NULL) {
	  // We have cupsFilter2 lines, use only these
	  do {
	    // Go to the second work, which is the destination MIME type
	    char *p = ppd_attr->value;
	    while (!isspace(*p)) p ++;
	    while (isspace(*p)) p ++;
	    // Compare with FINAL_CONTEN_TYPE
	    if (!strncasecmp(final_content_type, p,
			     strlen(final_content_type))) {
	      lastfilter = ppd_attr->value;
	      break;
	    }
	  } while ((ppd_attr = ppdFindNextAttr(ppd, "cupsFilter2", NULL))
		   != NULL);
	} else {
	  // We do not have cupsFilter2 lines, use the cupsFilter lines
	  int i;
	  for (i = 0; i < ppd->num_filters; i ++) {
	    // Compare source MIME type (first word) with FINAL_CONTENT_TYPE
	    if (!strncasecmp(final_content_type, ppd->filters[i],
			     strlen(final_content_type))) {
	      lastfilter = ppd->filters[i];
	      break;
	    }
	  }
	}
      }
      if (param.page_logging == -1) {
	if (lastfilter) {
	  // Get the name of the last filter, without mime type and cost
	  char *p = lastfilter;
	  char *q = p + strlen(p) - 1;
	  while(!isspace(*q) && *q != '/') q --;
	  lastfilter = q + 1;
	  // Check whether we have to log
	  if (!strcasecmp(lastfilter, "-")) {
	    // No filter defined in the PPD
	    // If output data (FINAL_CONTENT_TYPE) is PDF, pdftopdf is last
	    // filter (PDF printer) and has to log
	    // If output data (FINAL_CONTENT_TYPE) is PWG Raster, *toraster is
	    // last filter (IPP Everywhere printer) and pdftopdf has to log
	    if (strcasestr(final_content_type, "/pdf") ||
		strcasestr(final_content_type, "/vnd.cups-pdf") ||
		strcasestr(final_content_type, "/pwg-raster"))
	      param.page_logging = 1;
	    else
	      param.page_logging = 0;
	  } else if (!strcasecmp(lastfilter, "pdftopdf")) {
	    // pdftopdf is last filter (PDF printer)
	    param.page_logging = 1;
	  } else if (!strcasecmp(lastfilter, "gstopxl")) {
	    // gstopxl is last filter, this is a Ghostscript-based filter
	    // without access to the pages of the file to be printed, so we
	    // log the pages
	    param.page_logging = 1;
	  } else if (!strcasecmp(lastfilter + strlen(lastfilter) - 8,
				 "toraster")) {
	    // On IPP Everywhere printers which accept PWG Raster data one
	    // of gstoraster, pdftoraster, or mupdftoraster is the last
	    // filter. These filters do not log pages so pdftopdf has to
	    // do it
	    param.page_logging = 1;
	  } else if (!strcasecmp(lastfilter, "foomatic-rip")) {
	    // foomatic-rip is last filter, foomatic-rip is mainly used as
	    // Ghostscript wrapper to use Ghostscript's built-in printer
	    // drivers. Here there is also no access to the pages so that we
	    // delegate the logging to pdftopdf
	    param.page_logging = 1;
	  } else if (!strcasecmp(lastfilter, "hpps")) {
	    // hpps is last filter, hpps is part of HPLIP and it is a bug that
	    // it does not do the page logging.
	    param.page_logging = 1;
	  } else {
	    // All the other filters log pages as expected.
	    param.page_logging = 0;
	  }
	} else {
	  error("pdftopdf: Last filter could not get determined, page logging turned off.");
	  param.page_logging = 0;
	}
      }
      fprintf(stderr,
	      "DEBUG: pdftopdf: Last filter determined by the PPD: %s; FINAL_CONTENT_TYPE: %s => pdftopdf will %slog pages in page_log.\n",
	      (lastfilter ? lastfilter : "None"), final_content_type,
	      (param.page_logging == 0 ? "not " : ""));
    }
  }
}
// }}}

static bool printerWillCollate(ppd_file_t *ppd) // {{{
{
  ppd_choice_t *choice;

  if (((choice=ppdFindMarkedChoice(ppd,"Collate")) != NULL)&&
      (is_true(choice->choice))) {

    // printer can collate, but also for the currently marked ppd features?
    ppd_option_t *opt=ppdFindOption(ppd,"Collate");
    return (opt)&&(!opt->conflicted);
  }
  return false;
}
// }}}

void calculate(ppd_file_t *ppd,ProcessingParameters &param) // {{{
{
  if (param.reverse)
    // Enable evenDuplex or the first page may be empty.
    param.evenDuplex=true; // disabled later, if non-duplex

  setFinalPPD(ppd,param);

  if (param.numCopies==1) {
    param.deviceCopies=1;
    // collate is never needed for a single copy
    param.collate=false; // (does not make a big difference for us)
  } else if ((ppd)&&(!ppd->manual_copies)) { // hw copy generation available
    param.deviceCopies=param.numCopies;
    if (param.collate) { // collate requested by user
      // Check output format (FINAL_CONTENT_TYPE env variable) whether it is
      // of a driverless IPP printer (PDF, Apple Raster, PWG Raster, PCLm).
      // These printers do always hardware collate if they do hardware copies.
      // https://github.com/apple/cups/issues/5433
      char *final_content_type = getenv("FINAL_CONTENT_TYPE");
      if (final_content_type &&
	  (strcasestr(final_content_type, "/pdf") ||
	   strcasestr(final_content_type, "/vnd.cups-pdf") ||
	   strcasestr(final_content_type, "/pwg-raster") ||
	   strcasestr(final_content_type, "/urf") ||
	   strcasestr(final_content_type, "/PCLm"))) {
	param.deviceCollate = true;
      } else {
	// check collate device, with current/final(!) ppd settings
	param.deviceCollate=printerWillCollate(ppd);
	if (!param.deviceCollate) {
	  // printer can't hw collate -> we must copy collated in sw
	  param.deviceCopies=1;
	}
      }
    } // else: printer copies w/o collate and takes care of duplex/evenDuplex
  } else { // sw copies
    param.deviceCopies=1;
    if (param.duplex) { // &&(numCopies>1)
      // sw collate + evenDuplex must be forced to prevent copies on the backsides
      param.collate=true;
      param.deviceCollate=false;
    }
  }

  // TODO? FIXME:  unify code with emitJCLOptions, which does this "by-hand" now (and makes this code superfluous)
  if (param.deviceCopies==1) {
    // make sure any hardware copying is disabled
    ppdMarkOption(ppd,"Copies","1");
    ppdMarkOption(ppd,"JCLCopies","1");
  } else { // hw copy
    param.numCopies=1; // disable sw copy
  }

  if ((param.collate)&&(!param.deviceCollate)) { // software collate
    ppdMarkOption(ppd,"Collate","False"); // disable any hardware-collate (in JCL)
    param.evenDuplex=true; // fillers always needed
  }

  if (!param.duplex) {
    param.evenDuplex=false;
  }
}
// }}}

// reads from stdin into temporary file. returns FILE *  or NULL on error
FILE *copy_stdin_to_temp() // {{{
{
  char buf[BUFSIZ];
  int n;

  // FIXME:  what does >buf mean here?
  int fd=cupsTempFd(buf,sizeof(buf));
  if (fd<0) {
    error("Can't create temporary file");
    return NULL;
  }
  // remove name
  unlink(buf);

  // copy stdin to the tmp file
  while ((n=read(0,buf,BUFSIZ)) > 0) {
    if (write(fd,buf,n) != n) {
      error("Can't copy stdin to temporary file");
      close(fd);
      return NULL;
    }
  }
  if (lseek(fd,0,SEEK_SET) < 0) {
    error("Can't rewind temporary file");
    close(fd);
    return NULL;
  }

  FILE *f;
  if ((f=fdopen(fd,"rb")) == 0) {
    error("Can't fdopen temporary file");
    close(fd);
    return NULL;
  }
  return f;
}
// }}}

static int
sub_process_spawn (const char *filename,
          cups_array_t *sub_process_args,
          FILE *fp) // {{{
{
  char *argument;
  char buf[BUFSIZ];
  char **sub_process_argv;
  const char* apos;
  int fds[2];
  int i;
  int n;
  int numargs;
  int pid;
  int status = 65536;
  int wstatus;

  /* Put sub-process command line argument into an array for the "exec()"
     call */
  numargs = cupsArrayCount(sub_process_args);
  sub_process_argv = (char **)calloc(numargs + 1, sizeof(char *));
  for (argument = (char *)cupsArrayFirst(sub_process_args), i = 0; argument;
       argument = (char *)cupsArrayNext(sub_process_args), i++) {
    sub_process_argv[i] = argument;
  }
  sub_process_argv[i] = NULL;

  /* Debug output: Full sub-process command line */
  fprintf(stderr, "DEBUG: PDF form flattening command line:");
  for (i = 0; sub_process_argv[i]; i ++) {
    if ((strchr(sub_process_argv[i],' ')) || (strchr(sub_process_argv[i],'\t')))
      apos = "'";
    else
      apos = "";
    fprintf(stderr, " %s%s%s", apos, sub_process_argv[i], apos);
  }
  fprintf(stderr, "\n");

  /* Create a pipe for feeding the job into sub-process */
  if (pipe(fds))
  {
    fds[0] = -1;
    fds[1] = -1;
    fprintf(stderr, "ERROR: Unable to establish pipe for sub-process call\n");
    goto out;
  }

  /* Set the "close on exec" flag on each end of the pipe... */
  if (fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);
    fds[0] = -1;
    fds[1] = -1;
    fprintf(stderr, "ERROR: Unable to set \"close on exec\" flag on read end of the pipe for sub-process call\n");
    goto out;
  }
  if (fcntl(fds[1], F_SETFD, fcntl(fds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);
    fprintf(stderr, "ERROR: Unable to set \"close on exec\" flag on write end of the pipe for sub-process call\n");
    goto out;
  }

  if ((pid = fork()) == 0)
  {
    /* Couple pipe with STDIN of sub-process */
    if (fds[0] != 0) {
      close(0);
      if (fds[0] > 0) {
        if (dup(fds[0]) < 0) {
	  fprintf(stderr, "ERROR: Unable to couple pipe with STDIN of sub-process\n");
	  goto out;
	}
      } else {
        fprintf(stderr, "ERROR: Unable to couple pipe with STDIN of sub-process\n");
        goto out;
      }
    }
    close(fds[1]);

    /* Execute sub-process command line ... */
    execvp(filename, sub_process_argv);
    perror(filename);
    close(fds[0]);
    goto out;
  }

  close(fds[0]);
  /* Feed job data into the sub-process */
  while ((n = fread(buf, 1, BUFSIZ, fp)) > 0) {
    int count;
retry_write:
    count = write(fds[1], buf, n);
    if (count != n) {
      if (count == -1) {
        if (errno == EINTR) {
          goto retry_write;
	}
        fprintf(stderr, "ERROR: write failed: %s\n", strerror(errno));
      }
      fprintf(stderr, "ERROR: Can't feed job data into the sub-process\n");
      goto out;
    }
  }
  close (fds[1]);

retry_wait:
  if (waitpid (pid, &wstatus, 0) == -1) {
    if (errno == EINTR)
      goto retry_wait;
    perror ("sub-process");
    goto out;
  }

  /* How did the sub-process terminate */
  if (WIFEXITED(wstatus))
    /* Via exit() anywhere or return() in the main() function */
    status = WEXITSTATUS(wstatus);
  else if (WIFSIGNALED(wstatus))
    /* Via signal */
    status = 256 * WTERMSIG(wstatus);

out:
  free(sub_process_argv);
  return status;
}
// }}}

int main(int argc,char **argv)
{
  if ((argc<6)||(argc>7)) {
    fprintf(stderr,"Usage: %s job-id user title copies options [file]\n",argv[0]);
#ifdef DEBUG
    ProcessingParameters param;
    std::unique_ptr<PDFTOPDF_Processor> proc1(PDFTOPDF_Factory::processor());
    param.page.width=595.276; // A4
    param.page.height=841.89;

    param.page.top=param.page.bottom=36.0;
    param.page.right=param.page.left=18.0;
    param.page.right=param.page.width-param.page.right;
    param.page.top=param.page.height-param.page.top;

    //param.nup.calculate(4,0.707,0.707,param.nup);
    param.nup.nupX=2;
    param.nup.nupY=2;
    //param.nup.yalign=TOP;
    param.border=BorderType::NONE;
    //param.fillprint = true;
    //param.mirror=true;
    //param.reverse=true;
    //param.numCopies=3;
    if (!proc1->loadFilename("in.pdf",1)) return 2;
    param.dump();
    if (!processPDFTOPDF(*proc1,param)) return 3;
    emitComment(*proc1,param);
    proc1->emitFilename("out.pdf");
#endif
    return 1;
  }

  try {
    ProcessingParameters param;

    param.jobId=atoi(argv[1]);
    param.user=argv[2];
    param.title=argv[3];
    param.numCopies=atoi(argv[4]);
    param.copies_to_be_logged=atoi(argv[4]);

    // TODO?! sanity checks

    int num_options=0;
    cups_option_t *options=NULL;
    num_options=cupsParseOptions(argv[5],num_options,&options);

    ppd_file_t *ppd=NULL;
    ppd=ppdOpenFile(getenv("PPD")); // getenv (and thus ppd) may be null. This will not cause problems.
    ppdMarkDefaults(ppd);

    cupsMarkOptions(ppd,num_options,options);

    getParameters(ppd,num_options,options,param);
    calculate(ppd,param);

#ifdef DEBUG
    param.dump();
#endif

    /* Check with which method we will flatten interactive PDF forms
       and annotations so that they get printed also after page
       manipulations (scaling, N-up, ...). Flattening means to
       integrate the filled in data and the printable annotations into
       the pages themselves instead of holding them in an extra
       layer. Default method is using QPDF, alternatives are the
       external utilities pdftocairo or Ghostscript, but these make
       the processing slower, especially due to extra piping of the
       data between processes. */
    int qpdf_flatten = 1;
    int pdftocairo_flatten = 0;
    int gs_flatten = 0;
    int external_auto_flatten = 0;
    const char	*val;
    if ((val = cupsGetOption("pdftopdf-form-flattening", num_options, options)) != NULL) {
      if (strcasecmp(val, "qpdf") == 0 || strcasecmp(val, "internal") == 0 ||
	  strcasecmp(val, "auto") == 0) {
	qpdf_flatten = 1;
      } else if (strcasecmp(val, "external") == 0) {
	qpdf_flatten = 0;
	external_auto_flatten = 1;
      } else if (strcasecmp(val, "pdftocairo") == 0) {
	qpdf_flatten = 0;
	pdftocairo_flatten = 1;
      } else if (strcasecmp(val, "ghostscript") == 0 || strcasecmp(val, "gs") == 0) {
	qpdf_flatten = 0;
	gs_flatten = 1;
      } else
	fprintf(stderr,
		"WARNING: Invalid value for \"pdftopdf-form-flattening\": \"%s\"\n", val);
    }

    cupsFreeOptions(num_options,options);

    std::unique_ptr<PDFTOPDF_Processor> proc(PDFTOPDF_Factory::processor());

    FILE *tmpfile = NULL;
    if (argc==7) {
      if (!proc->loadFilename(argv[6],qpdf_flatten)) {
        ppdClose(ppd);
        return 1;
      }
    } else {
      tmpfile = copy_stdin_to_temp();
      if ((!tmpfile)||
	  (!proc->loadFile(tmpfile,WillStayAlive,qpdf_flatten))) {
        ppdClose(ppd);
        return 1;
      }
    }

    /* If the input file contains a PDF form and we opted for not
       using QPDF for flattening the form, we pipe the PDF through
       pdftocairo or Ghostscript here */
    if (!qpdf_flatten && proc->hasAcroForm()) {
      /* Prepare the input file for being read by the form flattening
	 process */
      FILE *infile = NULL;
      if (argc == 7) {
	/* We read from a named file */
	infile = fopen(argv[6], "r");
      } else {
	/* We read from a temporary file */
	if (tmpfile) rewind(tmpfile);
	infile = tmpfile;
      }
      if (infile == NULL) {
	error("Could not open the input file for flattening the PDF form!");
	return 1;
      }
      /* Create a temporary file for the output of the flattened PDF */
      char buf[BUFSIZ];
      int fd = cupsTempFd(buf,sizeof(buf));
      if (fd<0) {
	error("Can't create temporary file for flattened PDF form!");
	return 1;
      }
      FILE *outfile = NULL;
      if ((outfile=fdopen(fd,"rb")) == 0) {
	error("Can't fdopen temporary file for the flattened PDF form!");
	close(fd);
	return 1;
      }
      int flattening_done = 0;
      const char *command;
      cups_array_t *args;
      /* Choose the utility to be used and create its command line */
      if (pdftocairo_flatten || external_auto_flatten) {
	/* Try pdftocairo first, the preferred utility for form-flattening */
	command = CUPS_POPPLER_PDFTOCAIRO;
	args = cupsArrayNew(NULL, NULL);
	cupsArrayAdd(args, strdup(command));
	cupsArrayAdd(args, strdup("-pdf"));
	cupsArrayAdd(args, strdup("-"));
	cupsArrayAdd(args, strdup(buf));
	/* Run the pdftocairo form flattening process */
	rewind(infile);
	int status = sub_process_spawn (command, args, infile);
	cupsArrayDelete(args);
	if (status == 0)
	  flattening_done = 1;
	else
	  error("Unable to execute pdftocairo for form flattening!");
      }
      if (flattening_done == 0 &&
	  (gs_flatten || external_auto_flatten)) {
	/* Try Ghostscript */
	command = CUPS_GHOSTSCRIPT;
	args = cupsArrayNew(NULL, NULL);
	cupsArrayAdd(args, strdup(command));
	cupsArrayAdd(args, strdup("-dQUIET"));
	cupsArrayAdd(args, strdup("-dPARANOIDSAFER"));
	cupsArrayAdd(args, strdup("-dNOPAUSE"));
	cupsArrayAdd(args, strdup("-dBATCH"));
	cupsArrayAdd(args, strdup("-dNOINTERPOLATE"));
	cupsArrayAdd(args, strdup("-dNOMEDIAATTRS"));
	cupsArrayAdd(args, strdup("-sDEVICE=pdfwrite"));
	cupsArrayAdd(args, strdup("-dShowAcroForm"));
	cupsArrayAdd(args, strdup("-sstdout=%stderr"));
	memmove(buf + 13, buf, sizeof(buf) - 13);
	memcpy(buf, "-sOutputFile=", 13);
	cupsArrayAdd(args, strdup(buf));
	cupsArrayAdd(args, strdup("-"));
	/* Run the Ghostscript form flattening process */
	rewind(infile);
	int status = sub_process_spawn (command, args, infile);
	cupsArrayDelete(args);
	if (status == 0)
	  flattening_done = 1;
	else
	  error("Unable to execute Ghostscript for form flattening!");
      }
      if (flattening_done == 0) {
	error("No suitable utility for flattening filled PDF forms available, no flattening performed. Filled in content will possibly not be printed.");
	rewind(infile);
      }
      /* Clean up */
      if (infile != tmpfile)
	fclose(infile);
      /* Load the flattened PDF file into our PDF processor */
      if (flattening_done) {
	rewind(outfile);
	unlink(buf);
	if (!proc->loadFile(outfile,TakeOwnership,0)) {
	  error("Unable to create a PDF processor on the flattened form!"); 
	  return 1;
	}
      }
    } else if (qpdf_flatten)
      fprintf(stderr, "DEBUG: PDF interactive form and annotation flattening done via QPDF\n");

/* TODO
    // color management
--- PPD:
      copyPPDLine_(fp_dest, fp_src, "*PPD-Adobe: ");
      copyPPDLine_(fp_dest, fp_src, "*cupsICCProfile ");
      copyPPDLine_(fp_dest, fp_src, "*Manufacturer:");
      copyPPDLine_(fp_dest, fp_src, "*ColorDevice:");
      copyPPDLine_(fp_dest, fp_src, "*DefaultColorSpace:");
    if (cupsICCProfile) {
      proc.addCM(...,...);
    }
*/

    if (!processPDFTOPDF(*proc,param)) {
      ppdClose(ppd);
      return 2;
    }

    emitPreamble(ppd,param); // ppdEmit, JCL stuff
    emitComment(*proc,param); // pass information to subsequent filters via PDF comments

    //proc->emitFile(stdout);
    proc->emitFilename(NULL);

    emitPostamble(ppd,param);
    ppdClose(ppd);
    if (tmpfile)
      fclose(tmpfile);
  } catch (std::exception &e) {
    // TODO? exception type
    error("Exception: %s",e.what());
    return 5;
  } catch (...) {
    error("Unknown exception caught. Exiting.");
    return 6;
  }

  return 0;
}

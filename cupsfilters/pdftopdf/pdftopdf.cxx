// Copyright (c) 2012 Tobias Hoffmann
//
// Copyright (c) 2006-2011, BBR Inc.  All rights reserved.
// MIT Licensed.

#include <config.h>
#include <stdio.h>
#include <assert.h>
#include <cups/cups.h>
#include <ppd/ppd.h>
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
#include "cupsfilters/ppdgenerator.h"
#include "pdftopdf-processor-private.h"
#include "pdftopdf-jcl-private.h"

#include <stdarg.h>


// namespace {}

void setFinalPPD(ppd_file_t *ppd,const _cfPDFToPDFProcessingParameters &param)
{
  if ((param.booklet==CF_PDFTOPDF_BOOKLET_ON)&&(ppdFindOption(ppd,"Duplex"))) {
    // TODO: elsewhere, better
    ppdMarkOption(ppd,"Duplex","DuplexTumble");
    // TODO? sides=two-sided-short-edge
  }

  // for compatibility
  if ((param.set_duplex)&&(ppdFindOption(ppd,"Duplex")!=NULL)) {
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
static bool ppdDefaultOrder(ppd_file_t *ppd, pdftopdf_doc_t *doc) // {{{  -- is reverse?
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

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				 "cfFilterPDFToPDF: Unsupported output-order "
				 "value %s, using 'normal'!",
				 val);
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
  assert(val);
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

void getParameters(cf_filter_data_t *data,int num_options,cups_option_t *options,_cfPDFToPDFProcessingParameters &param,char *final_content_type,pdftopdf_doc_t *doc) // {{{
{

  ppd_file_t *ppd = data->ppd;
  ipp_t *printer_attrs = data->printer_attrs;
  ipp_attribute_t *ipp;
  const char *val;
   
  if ((val = cupsGetOption("copies",num_options,options)) != NULL	||
	(val = cupsGetOption("Copies", num_options, options))!=NULL	||
	(val = cupsGetOption("num-copies", num_options, options))!=NULL	||
	(val = cupsGetOption("NumCopies", num_options, options))!=NULL) {
    int copies = atoi(val);
    if (copies > 0)
      param.num_copies = copies;
  }
  // param.num_copies initially from commandline
  if (param.num_copies==1) {
    ppdGetInt(ppd,"Copies",&param.num_copies);
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
  else 
  {
	if(printer_attrs!=NULL){
	    char defSize[41];
	    int min_length = 99999,
	    	max_length = 0,
	    	min_width = 99999,
		max_width = 0,
		left, right,
		top, bottom; 
	    cfGenerateSizes(printer_attrs, &ipp, &min_length, &min_width,
			&max_length, &max_width, &bottom, &left, &right, &top,
			defSize);
	    param.page.top = top* 72.0/2540.0;
	    param.page.bottom = bottom* 72.0/2540.0;
	    param.page.right = right * 72.0/2540.0;
	    param.page.left = left* 72.0/2540.0;
	    param.page.width = min_width*72.0/2540.0;
	    param.page.height = min_length*72.0/2540.0;
  	}

    #ifdef HAVE_CUPS_1_7
   	if ((val = cupsGetOption("media-size", num_options, options)) != NULL ||
		(val = cupsGetOption("MediaSize", num_options, options)) != NULL ||
		(val = cupsGetOption("page-size", num_options, options)) != NULL ||
		(val = cupsGetOption("PageSize", num_options, options)) != NULL) {
	pwg_media_t *size_found = NULL;
	if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
					"cfFilterPDFToPDF: Page size from command "
					"line: %s", val);
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
		if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
					"cfFilterPDFToPDF: Width: %f, Length: %f",
					param.page.width, param.page.height);
	}
	else
		if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
					"cfFilterPDFToPDF: Unsupported page size %s.",
					val);
	}
    #endif /* HAVE_CUPS_1_7 */
  }
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

  if (ppdGetDuplex(ppd)) {
    param.duplex=true;
  } else if (is_true(cupsGetOption("Duplex",num_options,options))) {
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
      (val=cupsGetOption("page-delivery",num_options,options)) != NULL) {
    param.reverse = (strcasecmp(val, "Reverse") == 0 ||
		     strcasecmp(val, "reverse-order") == 0);
  } else if (ppd) {
    param.reverse=ppdDefaultOrder(ppd, doc);
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

  ppd_choice_t *choice;
  if ((choice=ppdFindMarkedChoice(ppd,"MirrorPrint")) != NULL) {
    val=choice->choice;
  } else {
    if((val = cupsGetOption("mirror", num_options, options))!=NULL  ||
	(val = cupsGetOption("mirror-print", num_options, options))!=NULL	||
	(val = cupsGetOption("MirrorPrint", num_options, options))!=NULL)
    param.mirror=is_true(val);
  }
  

  if ((val=cupsGetOption("emit-jcl",num_options,options)) != NULL) {
    param.emit_jcl=!is_false(val)&&(strcmp(val,"0")!=0);
  }

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
    param.even_duplex=is_true(attr->value);
  }

  // TODO? pdftopdf* ?
  // TODO?! pdftopdfAutoRotate

  // TODO?!  choose default by whether pdfautoratate filter has already been run (e.g. by mimetype)
  param.auto_rotate=(!is_false(cupsGetOption("pdfAutoRotate",num_options,options)) &&
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
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Automatic page logging "
				     "selected by command line.");
    } else if (is_true(val)) {
      param.page_logging = 1;
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Forced page logging selected "
				     "by command line.");
    } else if (is_false(val)) {
      param.page_logging = 0;
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Suppressed page logging "
				     "selected by command line.");
    } else {
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
				     "cfFilterPDFToPDF: Unsupported page-logging "
				     "value %s, using page-logging=auto!",val);
      param.page_logging = -1;
    }
  }

  if (param.page_logging == -1) {
    // Determine the last filter in the chain via cupsFilter(2) lines of the
    // PPD file and FINAL_CONTENT_TYPE
    if (!ppd) {
      // If PPD file is not specified, we determine whether to log pages or not
      // using FINAL_CONTENT_TYPE env variable. log pages only when FINAL_CONTENT_TYPE is
      // either pdf or raster
	
        if (final_content_type && (strcasestr(final_content_type, "/pdf") ||
	strcasestr(final_content_type, "/vnd.cups-pdf") ||
	strcasestr(final_content_type, "/pwg-raster")))
	      param.page_logging = 1;
	else
	      param.page_logging = 0;   
	// If final_content_type is not clearly available we are not sure whether to log pages or not
	if((char*)final_content_type==NULL || 
		sizeof(final_content_type)==0 || 
		final_content_type[0]=='\0'){
	    param.page_logging = -1;	 
	}
	if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		"cfFilterPDFToPDF: No PPD file specified,  "
		"determined whether to log pages or "
		"not using final_content_type env variable.");
		doc->logfunc(doc->logdata,CF_LOGLEVEL_DEBUG,"final_content_type = %s page_logging=%d",final_content_type?final_content_type:"NULL",param.page_logging);
    } else {
      char *lastfilter = NULL;
      if (final_content_type == NULL) {
	// No FINAL_CONTENT_TYPE env variable set, we cannot determine
	// whether we have to log pages, so do not log.
	param.page_logging = 0;
	if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				       "cfFilterPDFToPDF: No FINAL_CONTENT_TYPE "
				       "environment variable, could not "
				       "determine whether to log pages or "
				       "not, so turned off page logging.");
      // Proceed depending on number of cupsFilter(2) lines in PPD
      } else if (ppd->num_filters == 0) {
	// No filter line, manufacturer-supplied PostScript PPD
	// In this case cfFilterPSToPS, called by cfFilterPDFToPS, does the logging
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
				 "toraster") ||
		     !strcasecmp(lastfilter + strlen(lastfilter) - 5,
				 "topwg")) {
	    // On IPP Everywhere printers which accept PWG Raster data one
	    // of gstoraster, cfFilterPDFToRaster, or mupdftopwg is the last
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
	  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_ERROR,
					 "cfFilterPDFToPDF: Last filter could not "
					 "get determined, page logging turned "
					 "off.");
	  param.page_logging = 0;
	}
      }
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				     "cfFilterPDFToPDF: Last filter determined by the "
				     "PPD: %s; FINAL_CONTENT_TYPE: "
				     "%s => pdftopdf will %slog pages in "
				     "page_log.",
				     (lastfilter ? lastfilter : "None"),
				     (final_content_type ? final_content_type :
				      "(not supplied)"),
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

void calculate(cf_filter_data_t *data,_cfPDFToPDFProcessingParameters &param,char *final_content_type) // {{{
{
  ppd_file_t *ppd = data->ppd;
  int num_options = 0;
  cups_option_t *options = NULL;
  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);
  if (param.reverse)
    // Enable even_duplex or the first page may be empty.
    param.even_duplex=true; // disabled later, if non-duplex

  setFinalPPD(ppd,param);

  if (param.num_copies==1) {
    param.device_copies=1;
    // collate is never needed for a single copy
    param.collate=false; // (does not make a big difference for us)
  } else if ((ppd)&&(!ppd->manual_copies)) { // hw copy generation available
    param.device_copies=param.num_copies;
    if (param.collate) { // collate requested by user
      // Check output format (FINAL_CONTENT_TYPE env variable) whether it is
      // of a driverless IPP printer (PDF, Apple Raster, PWG Raster, PCLm).
      // These printers do always hardware collate if they do hardware copies.
      // https://github.com/apple/cups/issues/5433
      if (final_content_type &&
	  (strcasestr(final_content_type, "/pdf") ||
	   strcasestr(final_content_type, "/vnd.cups-pdf") ||
	   strcasestr(final_content_type, "/pwg-raster") ||
	   strcasestr(final_content_type, "/urf") ||
	   strcasestr(final_content_type, "/PCLm"))) {
	param.device_collate = true;
      } else {
	// check collate device, with current/final(!) ppd settings
	param.device_collate=printerWillCollate(ppd);
	if (!param.device_collate) {
	  // printer can't hw collate -> we must copy collated in sw
	  param.device_copies=1;
	}
      }
    } // else: printer copies w/o collate and takes care of duplex/even_duplex
  }
  else if(final_content_type &&
	((strcasestr(final_content_type, "/pdf"))  ||
	(strcasestr(final_content_type, "/vnd.cups-pdf")))){
    param.device_copies = param.num_copies;
    if(param.collate){
	param.device_collate = true;
    }
  }
 else { // sw copies
    param.device_copies=1;
    if (param.duplex) { // &&(num_copies>1)
      // sw collate + even_duplex must be forced to prevent copies on the backsides
      param.collate=true;
      param.device_collate=false;
    }
  }

  // TODO? FIXME:  unify code with emitJCLOptions, which does this "by-hand" now (and makes this code superfluous)
  if (param.device_copies==1) {
    // make sure any hardware copying is disabled
    ppdMarkOption(ppd,"Copies","1");
    ppdMarkOption(ppd,"JCLCopies","1");
  } else { // hw copy
    param.num_copies=1; // disable sw copy
  }

  if ((param.collate)&&(!param.device_collate)) { // software collate
    ppdMarkOption(ppd,"Collate","False"); // disable any hardware-collate (in JCL)
    param.even_duplex=true; // fillers always needed
  }

  if (!param.duplex) {
    param.even_duplex=false;
  }
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
	 void *parameters)    /* I - Filter-specific parameters */
{
  pdftopdf_doc_t     doc;         /* Document information */
  char               *final_content_type = NULL;
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
  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

  if (parameters)
    final_content_type = (char *)parameters;

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

    getParameters(data, num_options, options, param, final_content_type, &doc);

    calculate(data, param, final_content_type);

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
      _cfPDFToPDFEmitComment(*proc, param);
    }

    /* TODO
    // color management
    --- PPD:
      copyPPDLine_(fp_dest, fp_src, "*PPD-Adobe: ");
      copyPPDLine_(fp_dest, fp_src, "*cupsICCProfile ");
      copyPPDLine_(fp_dest, fp_src, "*Manufacturer:");
      copyPPDLine_(fp_dest, fp_src, "*ColorDevice:");
      copyPPDLine_(fp_dest, fp_src, "*DefaultColorSpace:");
    if (cupsICCProfile) {
      proc.add_cm(...,...);
    }
    */

    outputfp = fdopen(outputfd, "w");
    if (outputfp == NULL)
      return 1;

    _cfPDFToPDFEmitPreamble(outputfp, data->ppd, param); // ppdEmit, JCL stuff

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

    _cfPDFToPDFEmitPostamble(outputfp, data->ppd,param);
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

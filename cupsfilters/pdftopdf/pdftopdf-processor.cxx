#include "pdftopdf-processor-private.h"
#include "qpdf-pdftopdf-processor-private.h"
#include <stdio.h>
#include <assert.h>
#include <numeric>

void BookletMode_dump(pdftopdf_booklet_mode_e bkm,pdftopdf_doc_t *doc) // {{{
{
  static const char *bstr[3]={"Off","On","Shuffle-Only"};
  if ((bkm<CF_PDFTOPDF_BOOKLET_OFF) || (bkm>CF_PDFTOPDF_BOOKLET_JUST_SHUFFLE)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: bookletMode: (bad booklet mode: "
				   "%d)", bkm);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: bookletMode: %s", bstr[bkm]);
  }
}
// }}}

bool _cfPDFToPDFProcessingParameters::with_page(int outno) const // {{{
{
  if (outno%2 == 0) { // 1-based
    if (!even_pages) {
      return false;
    }
  } else if (!odd_pages) {
    return false;
  }
  return page_ranges.contains(outno);
}
// }}}
bool _cfPDFToPDFProcessingParameters::have_page(int pageno) const
{
  return input_page_ranges.contains(pageno);
}

void _cfPDFToPDFProcessingParameters::dump(pdftopdf_doc_t *doc) const // {{{
{
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: job_id: %d, num_copies: %d",
				 job_id,num_copies);
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: user: %s, title: %s",
				 (user)?user:"(null)",(title)?title:"(null)");
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: fitplot: %s",
				 (fitplot)?"true":"false");

  page.dump(doc);

  _cfPDFToPDFRotationDump(orientation,doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: paper_is_landscape: %s",
				 (paper_is_landscape)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: duplex: %s",
				 (duplex)?"true":"false");

  _cfPDFToPDFBorderTypeDump(border,doc);

  nup.dump(doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: reverse: %s",
				 (reverse)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: even_pages: %s, odd_pages: %s",
				 (even_pages)?"true":"false",
				 (odd_pages)?"true":"false");
  
   if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: input page range:");
  input_page_ranges.dump(doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: page range:");
  page_ranges.dump(doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: mirror: %s",
				 (mirror)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: Position:");
  _cfPDFToPDFPositionDump(xpos,pdftopdf_axis_e::X,doc);
  _cfPDFToPDFPositionDump(ypos,pdftopdf_axis_e::Y,doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: collate: %s",
				 (collate)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: even_duplex: %s",
				 (even_duplex)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: page_label: %s",
				 page_label.empty () ? "(none)" :
				 page_label.c_str());

  BookletMode_dump(booklet,doc);
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: booklet signature: %d",
				 book_signature);

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: auto_rotate: %s",
				 (auto_rotate)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: emit_jcl: %s",
				 (emit_jcl)?"true":"false");
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: device_copies: %d",
				 device_copies);
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: device_collate: %s",
				 (device_collate)?"true":"false");
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: set_duplex: %s",
				 (set_duplex)?"true":"false");
}
// }}}


_cfPDFToPDFProcessor *_cfPDFToPDFFactory::processor()
{
  return new _cfPDFToPDFQPDFProcessor();
}

// (1-based)
//   9: [*] [1] [2] [*]  [*] [3] [4] [9]  [8] [5] [6] [7]   -> signature = 12 = 3*4 = ((9+3)/4)*4
//       1   2   3   4    5   6   7   8    9   10  11  12
// NOTE: psbook always fills the sig completely (results in completely white pages (4-set), depending on the input)

// empty pages must be added for output values >=numPages
std::vector<int> _cfPDFToPDFBookletShuffle(int numPages,int signature) // {{{
{
  if (signature<0) {
    signature=(numPages+3)&~0x3;
  }
  assert(signature%4==0);

  std::vector<int> ret;
  ret.reserve(numPages+signature-1);

  int curpage=0;
  while (curpage<numPages) {
    // as long as pages to be done -- i.e. multiple times the signature
    int firstpage=curpage,
      lastpage=curpage+signature-1;
    // one signature
    while (firstpage<lastpage) {
      ret.push_back(lastpage--);
      ret.push_back(firstpage++);
      ret.push_back(firstpage++);
      ret.push_back(lastpage--);
    }
    curpage+=signature;
  }
  return ret;
}
// }}}

bool _cfProcessPDFToPDF(_cfPDFToPDFProcessor &proc,_cfPDFToPDFProcessingParameters &param,pdftopdf_doc_t *doc) // {{{
{
  if (!proc.check_print_permissions(doc)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: Not allowed to print");
    return false;
  }

  const bool dst_lscape =
    (param.paper_is_landscape ==
     ((param.orientation == ROT_0) || (param.orientation == ROT_180)));

  if (param.paper_is_landscape)
    std::swap(param.nup.nupX, param.nup.nupY);

  if (param.auto_rotate)
    proc.auto_rotate_all(dst_lscape,param.normal_landscape);

  std::vector<std::shared_ptr<_cfPDFToPDFPageHandle>> pages=proc.get_pages(doc);
  
  std::vector<std::shared_ptr<_cfPDFToPDFPageHandle>> input_page_range_list;
   
   for(int i=1;i<=(int)pages.size();i++)
      if(param.have_page(i))
      input_page_range_list.push_back(pages[i-1]);

  const int numOrigPages=input_page_range_list.size(); 

  // TODO FIXME? elsewhere
  std::vector<int> shuffle;
  if (param.booklet!=CF_PDFTOPDF_BOOKLET_OFF) {
    shuffle=_cfPDFToPDFBookletShuffle(numOrigPages,param.book_signature);
    if (param.booklet==CF_PDFTOPDF_BOOKLET_ON) { // override options
      // TODO? specifically "sides=two-sided-short-edge" / DuplexTumble
      // param.duplex=true;
      // param.set_duplex=true;  ?    currently done in setFinalPPD()
      _cfPDFToPDFNupParameters::preset(2,param.nup); // TODO?! better
    }
  } else { // 0 1 2 3 ...
    shuffle.resize(numOrigPages);
    std::iota(shuffle.begin(),shuffle.end(),0);
  }
  const int numPages=std::max(shuffle.size(),input_page_range_list.size());

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: \"print-scaling\" IPP attribute: %s",
				 (param.autoprint ? "auto" :
				  (param.autofit ? "auto-fit" :
				   (param.fitplot ? "fit" :
				    (param.fillprint ? "fill" :
				     (param.cropfit ? "none" :
				      "Not defined, should never happen"))))));

  if (param.autoprint || param.autofit) {
    bool margin_defined = true;
    bool document_large = false;
    int pw = param.page.right-param.page.left;
    int ph = param.page.top-param.page.bottom;

    if ((param.page.width == pw) && (param.page.height == ph))
      margin_defined = false;

    for (int i = 0; i < (int)input_page_range_list.size(); i ++)
    {
      _cfPDFToPDFPageRect r = input_page_range_list[i]->get_rect();
      int w = r.width * 100 / 102; // 2% of tolerance
      int h = r.height * 100 / 102;
      if ((w > param.page.width || h > param.page.height) &&
	  (h > param.page.width || w > param.page.height))
      {
	if (doc->logfunc)
	  doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		       "cfFilterPDFToPDF: Page %d too large for output page size, scaling pages to fit.",
		       i + 1);
	document_large = true;
      }
    }
    if (param.fidelity && doc->logfunc)
      doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		   "cfFilterPDFToPDF: \"ipp-attribute-fidelity\" IPP attribute is set, scaling pages to fit.");

    if (param.autoprint)
    {
      if (param.fidelity || document_large)
      {
        if (margin_defined)
          param.fitplot = true;
        else
          param.fillprint = true;
      }
      else
        param.cropfit = true;
    }
    else
    {
      if (param.fidelity || document_large)
        param.fitplot = true;
      else
        param.cropfit = true;
    }
  }

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: Print scaling mode: %s",
				 (param.fitplot ?
				  "Scale to fit printable area" :
				  (param.fillprint ?
				   "Scale to fill page and crop" :
				   (param.cropfit ?
				    "Do not scale, center, crop if needed" :
				    "Not defined, should never happen"))));

  // In Crop mode we do not scale the original document, it should keep the
  // exact same size. With N-Up it should be scaled to fit exacly the halves,
  // quarters, ... of the sheet, regardless of unprintable margins.
  // Therefore we remove the unprintable margins to do all the math without
  // them.
  if (param.cropfit)
  {
    param.page.left = 0;
    param.page.bottom = 0;
    param.page.right = param.page.width;
    param.page.top = param.page.height;
  }

  if (param.fillprint || param.cropfit)
  {
    for (int i = 0; i < (int)input_page_range_list.size(); i ++)
    {
      std::shared_ptr<_cfPDFToPDFPageHandle> page = input_page_range_list[i];
      pdftopdf_rotation_e orientation;
      if (page->is_landscape(param.orientation))
	orientation = param.normal_landscape;
      else
	orientation = ROT_0;
      page->crop(param.page, orientation, param.orientation, param.xpos, param.ypos,
		 !param.cropfit, param.auto_rotate, doc);
    }
    if (param.fillprint)
      param.fitplot = true;
  }

  std::shared_ptr<_cfPDFToPDFPageHandle> curpage;
  int outputpage=0;
  int outputno=0;

  if ((param.nup.nupX == 1) && (param.nup.nupY == 1) && !param.fitplot)
  {
    param.nup.width = param.page.width;
    param.nup.height = param.page.height;
  }
  else
  {
    param.nup.width = param.page.right - param.page.left;
    param.nup.height = param.page.top - param.page.bottom;
  }

  if ((param.orientation == ROT_90) || (param.orientation == ROT_270))
  {
    std::swap(param.nup.nupX, param.nup.nupY);
    param.nup.landscape = !param.nup.landscape;
    param.orientation = param.orientation - param.normal_landscape;
  }

  double xpos = 0, ypos = 0;
  if (param.nup.landscape)
  {
    // pages[iA]->rotate(param.normal_landscape);
    param.orientation = param.orientation + param.normal_landscape;
    // TODO? better
    if (param.nup.nupX != 1 || param.nup.nupY != 1 || param.fitplot)
    {
      xpos = param.page.height - param.page.top;
      ypos = param.page.left;
    }
    std::swap(param.page.width, param.page.height);
    std::swap(param.nup.width, param.nup.height);
  }
  else
  {
    if (param.nup.nupX != 1 || param.nup.nupY != 1 || param.fitplot)
    {
      xpos = param.page.left;
      ypos = param.page.bottom; // for whole page... TODO from position...
    }
  }

  _cfPDFToPDFNupState nupstate(param.nup);
  _cfPDFToPDFNupPageEdit pgedit;
  for (int iA = 0; iA < numPages; iA ++)
  {
    std::shared_ptr<_cfPDFToPDFPageHandle> page;
    if (shuffle[iA] >= numOrigPages)
      // add empty page as filler
      page = proc.new_page(param.page.width, param.page.height, doc);
    else
      page = input_page_range_list[shuffle[iA]];

    _cfPDFToPDFPageRect rect;
    rect = page->get_rect();
    //rect.dump(doc);

    bool newPage = nupstate.mext_page(rect.width, rect.height, pgedit);
    if (newPage)
    {
      if ((curpage) && (param.with_page(outputpage)))
      {
	curpage->rotate(param.orientation);
	if (param.mirror)
	  curpage->mirror();
	  // TODO? update rect? --- not needed any more
	proc.add_page(curpage, param.reverse); // reverse -> insert at beginning
	// Log page in /var/log/cups/page_log
	outputno ++;
	if (param.page_logging == 1)
	  if (doc->logfunc) doc->logfunc(doc->logdata,
					 CF_LOGLEVEL_CONTROL,
					 "PAGE: %d %d", outputno,
					 param.copies_to_be_logged);
      }
      curpage = proc.new_page(param.page.width, param.page.height, doc);
      outputpage++;
    }

    if (shuffle[iA] >= numOrigPages)
      continue;

    if (param.border != pdftopdf_border_type_e::NONE)
      // TODO FIXME... border gets cutted away, if orignal page had wrong size
      // page->"uncrop"(rect);  // page->setMedia()
      // Note: currently "fixed" in add_subpage(...&rect);
      page->add_border_rect(rect, param.border, 1.0 / pgedit.scale);

    if (!param.page_label.empty())
      page->add_label(param.page, param.page_label);

    if (param.cropfit)
    {
      if ((param.nup.nupX == 1) && (param.nup.nupY == 1))
      {
	double xpos2, ypos2;
	if ((param.page.height - param.page.width) *
	    (page->get_rect().height - page->get_rect().width) < 0)
	{
	  xpos2 = (param.page.width - (page->get_rect().height)) / 2;
	  ypos2 = (param.page.height - (page->get_rect().width)) / 2;
	  curpage->add_subpage(page, ypos2 + xpos, xpos2 + ypos, 1);
	}
	else
	{
	  xpos2 = (param.page.width - (page->get_rect().width)) / 2;
	  ypos2 = (param.page.height - (page->get_rect().height)) / 2;
	  curpage->add_subpage(page, xpos2 + xpos, ypos2 + ypos, 1);
	}
      }
      else
	curpage->add_subpage(page, pgedit.xpos + xpos, pgedit.ypos + ypos,
			     pgedit.scale);
    }
    else
      curpage->add_subpage(page, pgedit.xpos + xpos, pgedit.ypos + ypos,
			   pgedit.scale);

#ifdef DEBUG
    if (auto dbg=dynamic_cast<_cfPDFToPDFQPDFPageHandle *>(curpage.get()))
      dbg->debug(pgedit.sub, xpos, ypos);
#endif

    //pgedit.dump(doc);
  }
  if ((curpage) && (param.with_page(outputpage)))
  {
    curpage->rotate(param.orientation);
    if (param.mirror)
      curpage->mirror();
    proc.add_page(curpage, param.reverse); // reverse -> insert at beginning
    // Log page in /var/log/cups/page_log
    outputno ++;
    if (param.page_logging == 1)
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_CONTROL,
				     "PAGE: %d %d", outputno,
				     param.copies_to_be_logged);
  }

  if ((param.even_duplex || !param.odd_pages) && (outputno & 1)) {
    // need to output empty page to not confuse duplex
    proc.add_page(proc.new_page(param.page.width,param.page.height,doc),param.reverse);
    // Log page in /var/log/cups/page_log
    if (param.page_logging == 1)
      if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_CONTROL,
				     "PAGE: %d %d", outputno + 1,
				     param.copies_to_be_logged);
  }

  proc.multiply(param.num_copies,param.collate);

  return true;
}
// }}}

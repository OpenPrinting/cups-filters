#include "pdftopdf_processor.h"
#include "qpdf_pdftopdf_processor.h"
#include <stdio.h>
#include <assert.h>
#include <numeric>

void BookletMode_dump(BookletMode bkm) // {{{
{
  static const char *bstr[3]={"Off","On","Shuffle-Only"};
  if ((bkm<BOOKLET_OFF) || (bkm>BOOKLET_JUSTSHUFFLE)) {
    fprintf(stderr,"(bad booklet mode: %d)",bkm);
  } else {
    fputs(bstr[bkm],stderr);
  }
}
// }}}

bool ProcessingParameters::withPage(int outno) const // {{{
{
  if (outno%2 == 0) { // 1-based
    if (!evenPages) {
      return false;
    }
  } else if (!oddPages) {
    return false;
  }
  return pageRange.contains(outno);
}
// }}}

void ProcessingParameters::dump() const // {{{
{
  fprintf(stderr,"jobId: %d, numCopies: %d\n",
	  jobId,numCopies);
  fprintf(stderr,"user: %s, title: %s\n",
	  (user)?user:"(null)",(title)?title:"(null)");
  fprintf(stderr,"fitplot: %s\n",
	  (fitplot)?"true":"false");

  page.dump();

  fprintf(stderr,"Rotation(CCW): ");
  Rotation_dump(orientation);
  fprintf(stderr,"\n");

  fprintf(stderr,"paper_is_landscape: %s\n",
	  (paper_is_landscape)?"true":"false");

  fprintf(stderr,"duplex: %s\n",
	  (duplex)?"true":"false");

  fprintf(stderr,"Border: ");
  BorderType_dump(border);
  fprintf(stderr,"\n");

  nup.dump();

  fprintf(stderr,"reverse: %s\n",
	  (reverse)?"true":"false");

  fprintf(stderr,"evenPages: %s, oddPages: %s\n",
	  (evenPages)?"true":"false",
	  (oddPages)?"true":"false");

  fprintf(stderr,"page range: ");
  pageRange.dump();

  fprintf(stderr,"mirror: %s\n",
	  (mirror)?"true":"false");

  fprintf(stderr,"Position: ");
  Position_dump(xpos,Axis::X);
  fprintf(stderr,"/");
  Position_dump(ypos,Axis::Y);
  fprintf(stderr,"\n");

  fprintf(stderr,"collate: %s\n",
	  (collate)?"true":"false");

  fprintf(stderr,"evenDuplex: %s\n",
	  (evenDuplex)?"true":"false");

  fprintf(stderr,"pageLabel: %s\n",
	  pageLabel.empty () ? "(none)" : pageLabel.c_str());

  fprintf(stderr,"bookletMode: ");
  BookletMode_dump(booklet);
  fprintf(stderr,"\nbooklet signature: %d\n",
	  bookSignature);

  fprintf(stderr,"autoRotate: %s\n",
	  (autoRotate)?"true":"false");

  fprintf(stderr,"emitJCL: %s\n",
	  (emitJCL)?"true":"false");
  fprintf(stderr,"deviceCopies: %d\n",
	  deviceCopies);
  fprintf(stderr,"deviceCollate: %s\n",
	  (deviceCollate)?"true":"false");
  fprintf(stderr,"setDuplex: %s\n",
	  (setDuplex)?"true":"false");
}
// }}}


PDFTOPDF_Processor *PDFTOPDF_Factory::processor()
{
  return new QPDF_PDFTOPDF_Processor();
}

// (1-based)
//   9: [*] [1] [2] [*]  [*] [3] [4] [9]  [8] [5] [6] [7]   -> signature = 12 = 3*4 = ((9+3)/4)*4
//       1   2   3   4    5   6   7   8    9   10  11  12
// NOTE: psbook always fills the sig completely (results in completely white pages (4-set), depending on the input)

// empty pages must be added for output values >=numPages
std::vector<int> bookletShuffle(int numPages,int signature) // {{{
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

bool processPDFTOPDF(PDFTOPDF_Processor &proc,ProcessingParameters &param) // {{{
{
  if (!proc.check_print_permissions()) {
    fprintf(stderr,"Not allowed to print\n");
    return false;
  }

  const bool dst_lscape =
    (param.paper_is_landscape ==
     ((param.orientation == ROT_0) || (param.orientation == ROT_180)));

  if (param.paper_is_landscape)
    std::swap(param.nup.nupX, param.nup.nupY);

  if (param.autoRotate)
    proc.autoRotateAll(dst_lscape,param.normal_landscape);

  std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> pages=proc.get_pages();
  const int numOrigPages=pages.size();

  // TODO FIXME? elsewhere
  std::vector<int> shuffle;
  if (param.booklet!=BOOKLET_OFF) {
    shuffle=bookletShuffle(numOrigPages,param.bookSignature);
    if (param.booklet==BOOKLET_ON) { // override options
      // TODO? specifically "sides=two-sided-short-edge" / DuplexTumble
      // param.duplex=true;
      // param.setDuplex=true;  ?    currently done in setFinalPPD()
      NupParameters::preset(2,param.nup); // TODO?! better
    }
  } else { // 0 1 2 3 ...
    shuffle.resize(numOrigPages);
    std::iota(shuffle.begin(),shuffle.end(),0);
  }
  const int numPages=std::max(shuffle.size(),pages.size());

  fprintf(stderr, "DEBUG: pdftopdf: \"print-scaling\" IPP attribute: %s\n",
	  (param.autoprint ? "auto" :
	   (param.autofit ? "auto-fit" :
	    (param.fitplot ? "fit" :
	     (param.fillprint ? "fill" :
	      (param.cropfit ? "none" :
	       "Not defined, should never happen"))))));

  if(param.autoprint||param.autofit){
    bool margin_defined = true;
    bool document_large = false;
    int pw = param.page.right-param.page.left;
    int ph = param.page.top-param.page.bottom;

    if ((param.page.width == pw) && (param.page.height == ph))
      margin_defined = false;

    for (int i = 0; i < (int)pages.size(); i ++)
    {
      PageRect r = pages[i]->getRect();
      int w = r.width * 100 / 102; // 2% of tolerance
      int h = r.height * 100 / 102;
      if ((w > param.page.width || h > param.page.height) &&
	  (h > param.page.width || w > param.page.height))
      {
	fprintf(stderr,
		"DEBUG: pdftopdf: Page %d too large for output page size, scaling pages to fit.\n",
		i + 1);
	document_large = true;
      }
    }
    if (param.fidelity)
      fprintf(stderr,
	      "DEBUG: pdftopdf: \"ipp-attribute-fidelity\" IPP attribute is set, scaling pages to fit.\n");

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
    else{
      if(param.fidelity||document_large)
        param.fitplot = true;
      else
        param.cropfit = true;
    }
  }

  fprintf(stderr, "DEBUG: pdftopdf: Print scaling mode: %s\n",
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

  if(param.fillprint||param.cropfit){
    for(int i=0;i<(int)pages.size();i++)
    {
      std::shared_ptr<PDFTOPDF_PageHandle> page = pages[i];
      Rotation orientation;
      if (page->is_landscape(param.orientation))
	orientation = param.normal_landscape;
      else
	orientation = ROT_0;
      page->crop(param.page, orientation, param.orientation,
		 param.xpos, param.ypos,
		 !param.cropfit, param.autoRotate);
    }
    if (param.fillprint)
      param.fitplot = true;
  }

  std::shared_ptr<PDFTOPDF_PageHandle> curpage;
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

  NupState nupstate(param.nup);
  NupPageEdit pgedit;
  for (int iA=0;iA<numPages;iA++) {
    std::shared_ptr<PDFTOPDF_PageHandle> page;
    if (shuffle[iA] >= numOrigPages)
      // add empty page as filler
      page=proc.new_page(param.page.width,param.page.height);
    else
      page=pages[shuffle[iA]];

    PageRect rect;
    rect = page->getRect();
    //rect.dump();

    bool newPage=nupstate.nextPage(rect.width,rect.height,pgedit);
    if (newPage) {
      if ((curpage)&&(param.withPage(outputpage))) {
	curpage->rotate(param.orientation);
	if (param.mirror)
	  curpage->mirror();
	// TODO? update rect? --- not needed any more
	proc.add_page(curpage,param.reverse); // reverse -> insert at beginning
	// Log page in /var/log/cups/page_log
	outputno++;
	if (param.page_logging == 1)
	  fprintf(stderr, "PAGE: %d %d\n", outputno,
		  param.copies_to_be_logged);
      }
      curpage=proc.new_page(param.page.width,param.page.height);
      outputpage++;
    }
    if (shuffle[iA]>=numOrigPages) {
      continue;
    }

    if (param.border!=BorderType::NONE) {
      // TODO FIXME... border gets cutted away, if orignal page had wrong size
      // page->"uncrop"(rect);  // page->setMedia()
      // Note: currently "fixed" in add_subpage(...&rect);
      page->add_border_rect(rect,param.border,1.0/pgedit.scale);
    }

    if (!param.pageLabel.empty()) {
      page->add_label(param.page, param.pageLabel);
    }

    if (param.cropfit)
    {
      if ((param.nup.nupX == 1) && (param.nup.nupY == 1))
      {
	double xpos2, ypos2;
	if ((param.page.height - param.page.width) *
	    (page->getRect().height - page->getRect().width) < 0)
	{
	  xpos2 = (param.page.width - (page->getRect().height)) / 2;
	  ypos2 = (param.page.height - (page->getRect().width)) / 2;
	  curpage->add_subpage(page, ypos2 + xpos, xpos2 + ypos, 1);
	}
	else
	{
	  xpos2 = (param.page.width - (page->getRect().width)) / 2;
	  ypos2 = (param.page.height - (page->getRect().height)) / 2;
	  curpage->add_subpage(page, xpos2 + xpos, ypos2 + ypos, 1);
	}
      }
      else
	curpage->add_subpage(page,pgedit.xpos+xpos,pgedit.ypos+ypos,pgedit.scale);
    }
    else
      curpage->add_subpage(page, pgedit.xpos + xpos, pgedit.ypos + ypos,
			   pgedit.scale);

#ifdef DEBUG
    if (auto dbg=dynamic_cast<QPDF_PDFTOPDF_PageHandle *>(curpage.get())) {
      dbg->debug(pgedit.sub,xpos,ypos);
    }
#endif

    // pgedit.dump();
  }
  if ((curpage)&&(param.withPage(outputpage))) {
    curpage->rotate(param.orientation);
    if (param.mirror) {
      curpage->mirror();
    }
    proc.add_page(curpage,param.reverse); // reverse -> insert at beginning
    // Log page in /var/log/cups/page_log
    outputno ++;
    if (param.page_logging == 1)
      fprintf(stderr, "PAGE: %d %d\n", outputno, param.copies_to_be_logged);
  }

  if ((param.evenDuplex || !param.oddPages) && (outputno & 1)) {
    // need to output empty page to not confuse duplex
    proc.add_page(proc.new_page(param.page.width,param.page.height),param.reverse);
    // Log page in /var/log/cups/page_log
    if (param.page_logging == 1)
      fprintf(stderr, "PAGE: %d %d\n", outputno + 1, param.copies_to_be_logged);
  }

  proc.multiply(param.numCopies,param.collate);

  return true;
}
// }}}

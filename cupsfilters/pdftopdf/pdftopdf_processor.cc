#include "pdftopdf_processor.h"
#include "qpdf_pdftopdf_processor.h"
#include <stdio.h>
#include <assert.h>
#include <numeric>

void BookletMode_dump(BookletMode bkm,pdftopdf_doc_t *doc) // {{{
{
  static const char *bstr[3]={"Off","On","Shuffle-Only"};
  if ((bkm<BOOKLET_OFF) || (bkm>BOOKLET_JUSTSHUFFLE)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: bookletMode: (bad booklet mode: %d)\n",bkm);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: bookletMode: %s\n",bstr[bkm]);
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

void ProcessingParameters::dump(pdftopdf_doc_t *doc) const // {{{
{
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: jobId: %d, numCopies: %d\n",
	  jobId,numCopies);
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: user: %s, title: %s\n",
	  (user)?user:"(null)",(title)?title:"(null)");
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: fitplot: %s\n",
	  (fitplot)?"true":"false");

  page.dump(doc);

  Rotation_dump(orientation,doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: paper_is_landscape: %s\n",
	  (paper_is_landscape)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: duplex: %s\n",
	  (duplex)?"true":"false");

  BorderType_dump(border,doc);

  nup.dump(doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: reverse: %s\n",
	  (reverse)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: evenPages: %s, oddPages: %s\n",
	  (evenPages)?"true":"false",
	  (oddPages)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: page range: \n");
  pageRange.dump(doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: mirror: %s\n",
	  (mirror)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: Position:\n");
  Position_dump(xpos,Axis::X,doc);
  Position_dump(ypos,Axis::Y,doc);

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: collate: %s\n",
	  (collate)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: evenDuplex: %s\n",
	  (evenDuplex)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: pageLabel: %s\n",
	  pageLabel.empty () ? "(none)" : pageLabel.c_str());

  BookletMode_dump(booklet,doc);
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: booklet signature: %d\n",
	  bookSignature);

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: autoRotate: %s\n",
	  (autoRotate)?"true":"false");

  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: emitJCL: %s\n",
	  (emitJCL)?"true":"false");
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: deviceCopies: %d\n",
	  deviceCopies);
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: deviceCollate: %s\n",
	  (deviceCollate)?"true":"false");
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: setDuplex: %s\n",
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

bool processPDFTOPDF(PDFTOPDF_Processor &proc,ProcessingParameters &param,pdftopdf_doc_t *doc) // {{{
{
  if (!proc.check_print_permissions(doc)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
	      "pdftopdf: Not allowed to print\n");
    return false;
  }

  if (param.autoRotate) {
    const bool dst_lscape =
      (param.paper_is_landscape ==
       ((param.orientation == ROT_0) || (param.orientation == ROT_180)));
    proc.autoRotateAll(dst_lscape,param.normal_landscape);
  }

  std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> pages=proc.get_pages(doc);
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

  if(param.autoprint||param.autofit){
    bool margin_defined = true;
    bool document_large = false;
    int pw = param.page.right-param.page.left;
    int ph = param.page.top-param.page.bottom;
    int w=0,h=0;
    Rotation tempRot=param.orientation;
    PageRect r= pages[0]->getRect();
    w = r.width;
    h = r.height;

    if(tempRot==ROT_90||tempRot==ROT_270)
    {
      std::swap(w,h);
    }
    if(w>=pw||h>=ph)
    {
      document_large = true;
    }
    if((param.page.width==pw)&&
        (param.page.height==ph))
        margin_defined = false;
    if(param.autoprint){
      if(param.fidelity||document_large) {
        if(margin_defined)
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

  if(param.fillprint||param.cropfit){
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
	      "pdftopdf: Cropping input pdf and Enabling fitplot.\n");
    if(param.noOrientation&&pages.size())
    {
      bool land = pages[0]->is_landscape(param.orientation);
      if(land)
        param.orientation = param.normal_landscape;
    }
    for(int i=0;i<(int)pages.size();i++)
    {
      std::shared_ptr<PDFTOPDF_PageHandle> page = pages[i];
      page->crop(param.page,param.orientation,param.xpos,param.ypos,!param.cropfit,doc);
    }
    param.fitplot = 1;
  }

  std::shared_ptr<PDFTOPDF_PageHandle> curpage;
  int outputpage=0;
  int outputno=0;

  if ((param.nup.nupX==1)&&(param.nup.nupY==1)&&(!param.fitplot)) {
    // TODO? fitplot also without xobject?
    /*
      param.nup.width=param.page.width;
      param.nup.height=param.page.height;
    */

    for (int iA=0;iA<numPages;iA++) {
      if (!param.withPage(iA+1)) {
        continue;
      }

      // Log page in /var/log/cups/page_log
      if (param.page_logging == 1)
	if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
	      "pdftopdf: PAGE: %d %d\n", iA + 1, param.copies_to_be_logged);

      if (shuffle[iA]>=numOrigPages) {
        // add empty page as filler
        proc.add_page(proc.new_page(param.page.width,param.page.height,doc),param.reverse);
	outputno++;
        continue; // no border, etc.
      }
      auto page=pages[shuffle[iA]];

      page->rotate(param.orientation);

      if (param.mirror) {
        page->mirror();
      }

      if (!param.pageLabel.empty()) {
        page->add_label(param.page, param.pageLabel);
      }

      // place border
      if ((param.border!=BorderType::NONE)&&(iA<numOrigPages)) {
#if 0 // would be nice, but is not possible
        PageRect rect=page->getRect();

        rect.left+=param.page.left;
        rect.bottom+=param.page.bottom;
        rect.top-=param.page.top;
        rect.right-=param.page.right;
        // width,height not needed for add_border_rect (FIXME?)

        page->add_border_rect(rect,param.border,1.0);
#else // this is what pstops does
        page->add_border_rect(param.page,param.border,1.0);
#endif
      }

      proc.add_page(page,param.reverse); // reverse -> insert at beginning
      outputno++;
    }
  } else {
    param.nup.width=param.page.right-param.page.left;
    param.nup.height=param.page.top-param.page.bottom;

    double xpos=param.page.left,
      ypos=param.page.bottom; // for whole page... TODO from position...

    const bool origls=param.nup.landscape;
    if ((param.orientation==ROT_90)||(param.orientation==ROT_270)) {
      std::swap(param.nup.nupX,param.nup.nupY);
      param.nup.landscape=!param.nup.landscape;
      param.orientation=param.orientation-param.normal_landscape;
    }
    if (param.nup.landscape) {
      // pages[iA]->rotate(param.normal_landscape);
      param.orientation=param.orientation+param.normal_landscape;
      // TODO? better
      xpos=param.page.bottom;
      ypos=param.page.width - param.page.right;
      std::swap(param.page.width,param.page.height);
      std::swap(param.nup.width,param.nup.height);
    }

    NupState nupstate(param.nup);
    NupPageEdit pgedit;
    for (int iA=0;iA<numPages;iA++) {
      std::shared_ptr<PDFTOPDF_PageHandle> page;
      if (shuffle[iA]>=numOrigPages) {
        // add empty page as filler
        page=proc.new_page(param.page.width,param.page.height,doc);
      } else {
        page=pages[shuffle[iA]];
      }

      PageRect rect;
      if (param.fitplot) {
        rect=page->getRect();
      } else {
        rect.width=param.page.width;
        rect.height=param.page.height;

	// TODO? better
        if (origls) {
          std::swap(rect.width,rect.height);
        }

        rect.left=0;
        rect.bottom=0;
        rect.right=rect.width;
        rect.top=rect.height;
      }
      // rect.dump();

      bool newPage=nupstate.nextPage(rect.width,rect.height,pgedit);
      if (newPage) {
        if ((curpage)&&(param.withPage(outputpage))) {
          curpage->rotate(param.orientation);
          if (param.mirror) {
            curpage->mirror();
	    // TODO? update rect? --- not needed any more
          }
          proc.add_page(curpage,param.reverse); // reverse -> insert at beginning
	  // Log page in /var/log/cups/page_log
	  outputno++;
	  if (param.page_logging == 1)
	    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
	      "pdftopdf: PAGE: %d %d\n", outputno,
		    param.copies_to_be_logged);
        }
        curpage=proc.new_page(param.page.width,param.page.height,doc);
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

      if (!param.fitplot) {
        curpage->add_subpage(page,pgedit.xpos+xpos,pgedit.ypos+ypos,pgedit.scale,&rect);
      } else {
        if(param.cropfit){
          double xpos2 = (param.page.right-param.page.left-(page->getRect().width))/2;
          double ypos2 = (param.page.top-param.page.bottom-(page->getRect().height))/2;
          if(param.orientation==ROT_270||param.orientation==ROT_90)
          {
            xpos2 = (param.page.right-param.page.left-(page->getRect().height))/2;
            ypos2 = (param.page.top-param.page.bottom-(page->getRect().width))/2;
            curpage->add_subpage(page,ypos2+param.page.bottom,xpos2+param.page.left,1);
          }else{
          curpage->add_subpage(page,xpos2+param.page.left,ypos2+param.page.bottom,1);
          }
        }
        else
          curpage->add_subpage(page,pgedit.xpos+xpos,pgedit.ypos+ypos,pgedit.scale);
      }

#ifdef DEBUG
      if (auto dbg=dynamic_cast<QPDF_PDFTOPDF_PageHandle *>(curpage.get())) {
	// dbg->debug(pgedit.sub,xpos,ypos);
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
	if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
	      "pdftopdf: PAGE: %d %d\n", outputno, param.copies_to_be_logged);
    }
  }

  if ((param.evenDuplex || !param.oddPages) && (outputno & 1)) {
    // need to output empty page to not confuse duplex
    proc.add_page(proc.new_page(param.page.width,param.page.height,doc),param.reverse);
    // Log page in /var/log/cups/page_log
    if (param.page_logging == 1)
      if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
	      "pdftopdf: PAGE: %d %d\n", outputno + 1, param.copies_to_be_logged);
  }

  proc.multiply(param.numCopies,param.collate);

  return true;
}
// }}}

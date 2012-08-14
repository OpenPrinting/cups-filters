#include "pdftopdf_processor.h"
#include "qpdf_pdftopdf_processor.h"
#include <stdio.h>
#include <assert.h>

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
  printf("jobId: %d, nupCopies: %d\n",
         jobId,numCopies);
  printf("user: %s, title: %s\n",
         (user)?user:"(null)",(title)?title:"(null)");
  printf("fitplot: %s\n",
         (fitplot)?"true":"false");

  page.dump();

  printf("Rotation(CCW): ");
  Rotation_dump(orientation);
  printf("\n");

  printf("duplex: %s\n",
         (duplex)?"true":"false");

  printf("Border: ");
  BorderType_dump(border);
  printf("\n");

  nup.dump();

  printf("reverse: %s\n",
         (reverse)?"true":"false");

  printf("evenPages: %s, oddPages: %s\n",
         (evenPages)?"true":"false",
         (oddPages)?"true":"false");

  printf("page range: ");
  pageRange.dump();

  printf("mirror: %s\n",
         (mirror)?"true":"false");

  printf("Position: ");
  Position_dump(xpos,Axis::X);
  printf("/");
  Position_dump(ypos,Axis::Y);
  printf("\n");

  printf("collate: %s\n",
         (collate)?"true":"false");
/*
  // std::string pageLabel; // or NULL?  must stay/dup!
  ...
  ...

  ??? shuffle 
*/
  printf("evenDuplex: %s\n",
         (evenDuplex)?"true":"false");

  printf("emitJCL: %s\n",
         (emitJCL)?"true":"false");
  printf("deviceCopies: %d\n",
         deviceCopies);
  printf("setDuplex: %s\n",
         (setDuplex)?"true":"false");
  printf("unsetCollate: %s\n",
         (unsetCollate)?"true":"false");
}
// }}}


PDFTOPDF_Processor *PDFTOPDF_Factory::processor()
{
  return new QPDF_PDFTOPDF_Processor();
}


bool processPDFTOPDF(PDFTOPDF_Processor &proc,ProcessingParameters &param) // {{{
{
  if (!proc.check_print_permissions()) {
    fprintf(stderr,"Not allowed to print\n");
    return false;
  }

  std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> pages=proc.get_pages(); // TODO: shuffle
  const int numPages=pages.size();
  std::shared_ptr<PDFTOPDF_PageHandle> curpage;
  int outputno=0;

  if ( (param.nup.nupX==1)&&(param.nup.nupY==1)&&(!param.fitplot) ) {
    // TODO? fitplot also without xobject?
    /*
    param.nup.width=param.page.width;
    param.nup.height=param.page.height;
    */

    for (int iA=0;iA<numPages;iA++) {
      if (!param.withPage(iA+1)) {
        continue;
      }

      pages[iA]->rotate(param.orientation);

      if (param.mirror) {
        pages[iA]->mirror();
      }

      // place border
      if (param.border!=BorderType::NONE) {
#if 0 // would be nice, but is not possible
        PageRect rect=pages[iA]->getRect();

        rect.left+=param.page.left;
        rect.bottom+=param.page.bottom;
        rect.top-=param.page.top;
        rect.right-=param.page.right;
        // width,height not needed for add_border_rect (FIXME?)
        pages[iA]->add_border_rect(rect,param.border,1.0); 
#else // this is what pstops does
        pages[iA]->add_border_rect(param.page,param.border,1.0); 
#endif
      }

      proc.add_page(pages[iA],param.reverse); // reverse -> insert at beginning
    }
    outputno=numPages;
  } else {
// TODO... -left/-right needs to be subtracted from param.nup.width/height
    param.nup.width=param.page.right-param.page.left;
    param.nup.height=param.page.top-param.page.bottom;

    double xpos=param.page.left,
           ypos=param.page.bottom; // for whole page... TODO from position...

    if (param.nup.landscape) {
//      pages[iA]->rotate(param.normal_landscape);
      param.orientation=param.orientation+param.normal_landscape;
      // TODO? better
      std::swap(param.page.width,param.page.height);
      std::swap(param.nup.width,param.nup.height);
    }

    NupState nupstate(param.nup);
    NupPageEdit pgedit;
    for (int iA=0;iA<numPages;iA++) {
      PageRect rect=pages[iA]->getRect();
//      rect.dump();

      bool newPage=nupstate.nextPage(rect.width,rect.height,pgedit);
      if (newPage) {
        if ( (param.withPage(outputno))&&(curpage) ) {
          curpage->rotate(param.orientation);
          if (param.mirror) {
            curpage->mirror();
// TODO? update rect? --- not needed any more
          }
          proc.add_page(curpage,param.reverse); // reverse -> insert at beginning
        }
        outputno++;
        curpage=proc.new_page(param.page.width,param.page.height);
      }

      if (param.border!=BorderType::NONE) {
        // TODO? -left/-right needs to be added back?
        pages[iA]->add_border_rect(rect,param.border,pgedit.scale);
      }

      curpage->add_subpage(pages[iA],pgedit.xpos+xpos,pgedit.ypos+ypos,pgedit.scale);

#ifdef DEBUG
      if (auto dbg=dynamic_cast<QPDF_PDFTOPDF_PageHandle *>(curpage.get())) {
//        dbg->debug(pgedit.sub,xpos,ypos);
      }
#endif

//      pgedit.dump();
    }
    if ( (param.withPage(outputno))&&(curpage) ) {
      curpage->rotate(param.orientation);
      if (param.mirror) {
        curpage->mirror();
      }
      outputno++;
      proc.add_page(curpage,param.reverse); // reverse -> insert at beginning
    }
  }

  if ( (param.evenDuplex)&&(outputno&1) ) {
    // need to output empty page to not confuse duplex
    proc.add_page(proc.new_page(param.page.width,param.page.height),param.reverse);
  }

  proc.multiply(param.numCopies,param.collate);

//fprintf(stderr,"TODO setProcess\n");

  return true;
}
// }}}


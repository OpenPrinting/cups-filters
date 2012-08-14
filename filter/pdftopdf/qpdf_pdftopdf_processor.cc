#include "qpdf_pdftopdf_processor.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdexcept>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>
#include "qpdf_tools.h"
#include "qpdf_xobject.h"
#include "qpdf_pdftopdf.h"

// Use: content.append(debug_box(pe.sub,xpos,ypos));
static std::string debug_box(const PageRect &box,float xshift,float yshift) // {{{ 
{
  return std::string("q 1 w 0.1 G\n ")+
         QUtil::double_to_string(box.left+xshift)+" "+QUtil::double_to_string(box.top+yshift)+" m  "+
         QUtil::double_to_string(box.right+xshift)+" "+QUtil::double_to_string(box.bottom+yshift)+" l "+"S \n "+

         QUtil::double_to_string(box.right+xshift)+" "+QUtil::double_to_string(box.top+yshift)+" m  "+
         QUtil::double_to_string(box.left+xshift)+" "+QUtil::double_to_string(box.bottom+yshift)+" l "+"S \n "+

         QUtil::double_to_string(box.left+xshift)+" "+QUtil::double_to_string(box.top+yshift)+"  "+
         QUtil::double_to_string(box.right-box.left)+" "+QUtil::double_to_string(box.bottom-box.top)+" re "+"S Q\n";
}
// }}}

QPDF_PDFTOPDF_PageHandle::QPDF_PDFTOPDF_PageHandle(QPDFObjectHandle page,int orig_no) // {{{
  : page(page),no(orig_no)
{
}
// }}}

QPDF_PDFTOPDF_PageHandle::QPDF_PDFTOPDF_PageHandle(QPDF *pdf,float width,float height) // {{{
  : no(0)
{
  assert(pdf);
  page=QPDFObjectHandle::parse(
    "<<"
    "  /Type /Page"
    "  /Resources <<"
    "    /XObject null "
    "  >>"
    "  /MediaBox null "
    "  /Contents null "
    ">>");
  page.replaceKey("/MediaBox",makeBox(0,0,width,height));
  page.replaceKey("/Contents",QPDFObjectHandle::newStream(pdf));
  // xobjects: later (in get())
  content.assign("q\n");  // TODO? different/not needed

  page=pdf->makeIndirectObject(page); // stores *pdf 
}
// }}}

PageRect QPDF_PDFTOPDF_PageHandle::getRect() const // {{{
{
  page.assertInitialized();
  PageRect ret=getBoxAsRect(getTrimBox(page));
  ret.rotate(getRotate(page));
  return ret;
}
// }}}

bool QPDF_PDFTOPDF_PageHandle::isExisting() const // {{{
{
  page.assertInitialized();
  return content.empty();
}
// }}}

QPDFObjectHandle QPDF_PDFTOPDF_PageHandle::get() // {{{
{
  QPDFObjectHandle ret=page;
  if (!isExisting()) { // finish up page
    page.getKey("/Resources").replaceKey("/XObject",QPDFObjectHandle::newDictionary(xobjs));
    content.append("Q\n");
    page.getKey("/Contents").replaceStreamData(content,QPDFObjectHandle::newNull(),QPDFObjectHandle::newNull());
    page.replaceOrRemoveKey("/Rotate",makeRotate(rotation));
  } else {
    Rotation rot=getRotate(page)+rotation;
    page.replaceOrRemoveKey("/Rotate",makeRotate(rot));
  }
  page=QPDFObjectHandle(); // i.e. uninitialized
  return ret;
}
// }}}

  // TODO: factor out pre- and post-   ... also needed by mirror()!
// TODO: add TWO, etc.; remove detection(?)
// TODO? for non-existing (either drop comment or facility to create split streams needed)
void QPDF_PDFTOPDF_PageHandle::add_border_rect(const PageRect &rect,BorderType border,float fscale) // {{{
{
  assert(isExisting());
  static const char *pre="%pdftopdf q\n"
                         "q\n",
                    *post="%pdftopdf Q\n"
                          "Q\n";

// fscale:  inverse_scale (from nup, fitplot)
// margin:  2.25*fscale
// line_width:  ((thick)?0.5:0.24)*fscale
// (PageLeft+margin,PageBottom+margin) rect (PageRight-PageLeft-2*margin,...)   ... for nup>1: PageLeft=0,etc.
   //  if (double)  margin+=2*fscale ...rect...

  std::string boxcmd=std::string("q 1 w 0 G \n")+ // TODO
                     QUtil::double_to_string(rect.left)+" "+QUtil::double_to_string(rect.top)+"  "+
                     QUtil::double_to_string(rect.right-rect.left)+" "+QUtil::double_to_string(rect.bottom-rect.top)+" re S \nQ\n";
  // TODO: border.style ...

// if (!isExisting()) {
//   // TODO: only after 
//   return;
// }

  std::vector<QPDFObjectHandle> cntnt=page.getPageContents();
  if (cntnt.size()>=3) { // detect previous invocation box and replace
    // check cntnt.front() and cntnt.back()
    PointerHolder<Buffer> pbuf_f=cntnt.front().getStreamData();
    PointerHolder<Buffer> pbuf_b=cntnt.back().getStreamData();
    if ( (pbuf_f->getSize()>12)&&(strncmp((char *)pbuf_f->getBuffer(),pre,12)==0)&& 
         (pbuf_b->getSize()>12)&&(strncmp((char *)pbuf_b->getBuffer(),post,12)==0) ) {
      // leave "stm1" alone, replace stm2
      cntnt.back().replaceStreamData(std::string(post)+boxcmd,QPDFObjectHandle::newNull(),QPDFObjectHandle::newNull());
      boxcmd.clear();
    }
  }
  if (!boxcmd.empty()) {
    assert(page.getOwningQPDF()); // existing pages are always indirect
    QPDFObjectHandle stm1=QPDFObjectHandle::newStream(page.getOwningQPDF(),pre),
                     stm2=QPDFObjectHandle::newStream(page.getOwningQPDF(),std::string(post)+boxcmd);

    page.addPageContents(stm1,true); // before
    page.addPageContents(stm2,false); // after
  }
}
// }}}

void QPDF_PDFTOPDF_PageHandle::add_subpage(const std::shared_ptr<PDFTOPDF_PageHandle> &sub,float xpos,float ypos,float scale) // {{{
{
  auto qsub=dynamic_cast<QPDF_PDFTOPDF_PageHandle *>(sub.get());
  assert(qsub);

  std::string xoname="/X"+QUtil::int_to_string((qsub->no!=-1)?qsub->no:++no);
  xobjs[xoname]=makeXObject(qsub->page.getOwningQPDF(),qsub->page); // trick: should be the same as page->getOwningQPDF() [only after made indirect]

  content.append("q\n  ");
  content.append(QUtil::double_to_string(scale)+" 0 0 "+
                 QUtil::double_to_string(scale)+" "+
                 QUtil::double_to_string(xpos)+" "+
                 QUtil::double_to_string(ypos)+" cm\n  ");
  content.append(xoname+" Do\n");
  content.append("Q\n");
}
// }}} 

void QPDF_PDFTOPDF_PageHandle::mirror() // {{{
{
  PageRect orig=getRect();

  if (isExisting()) {
    // need to wrap in XObject to keep patterns correct
    // TODO? refactor into internal ..._subpage fn ?
    std::string xoname="/X"+QUtil::int_to_string(no);
    xobjs[xoname]=makeXObject(page.getOwningQPDF(),page);

    *this=QPDF_PDFTOPDF_PageHandle(page.getOwningQPDF(),orig.width,orig.height);

//    content.append(std::string("1 0 0 1 0 0 cm\n  ");
    content.append(xoname+" Do\n");

    assert(!isExisting());
  }

  static const char *pre="%pdftopdf cm\n";
  // Note: we don't change (TODO need to?) the media box
  std::string mrcmd("-1 0 0 1 "+ 
                    QUtil::double_to_string(orig.right)+" 0 cm\n");

  content.insert(0,std::string(pre)+mrcmd);
}
// }}}

void QPDF_PDFTOPDF_PageHandle::rotate(Rotation rot) // {{{
{
  rotation=rot; // "rotation += rot;" ?
}
// }}}

void QPDF_PDFTOPDF_PageHandle::debug(const PageRect &rect,float xpos,float ypos) // {{{
{
  assert(!isExisting());
  content.append(debug_box(rect,xpos,ypos));
}
// }}}


void QPDF_PDFTOPDF_Processor::closeFile() // {{{
{
  pdf.reset();
}
// }}}

void QPDF_PDFTOPDF_Processor::error(const char *fmt,...) // {{{
{
  va_list ap;

  va_start(ap,fmt);
  vfprintf(stderr,fmt,ap);
  fputs("\n",stderr);
  va_end(ap);
}
// }}}

// TODO?  try/catch for PDF parsing errors?

bool QPDF_PDFTOPDF_Processor::loadFile(FILE *f,ArgOwnership take) // {{{
{
  closeFile();
  if (!f) {
    throw std::invalid_argument("loadFile(NULL,...) not allowed");
  }
  try {
    pdf.reset(new QPDF);
  } catch (...) {
    if (take==TakeOwnership) {
      fclose(f);
    }
    throw;
  }
  switch (take) {
  case WillStayAlive:
    try {
      pdf->processFile("temp file",f,false);
    } catch (const std::exception &e) {
      error("loadFile failed: %s",e.what());
      return false;
    }
    break;
  case TakeOwnership:
    try {
      pdf->processFile("temp file",f,true);
    } catch (const std::exception &e) {
      error("loadFile failed: %s",e.what());
      return false;
    }
    break;
  case MustDuplicate:
    error("loadFile with MustDuplicate is not supported");
    return false;
  }
  start();
  return true;
}
// }}}

bool QPDF_PDFTOPDF_Processor::loadFilename(const char *name) // {{{
{
  closeFile();
  try {
    pdf.reset(new QPDF);
    pdf->processFile(name);
  } catch (const std::exception &e) {
    error("loadFilename failed: %s",e.what());
    return false;
  }
  start();
  return true;
}
// }}}


void QPDF_PDFTOPDF_Processor::start() // {{{
{
  assert(pdf);

  pdf->pushInheritedAttributesToPage();
  orig_pages=pdf->getAllPages();

  // remove them (just unlink, data still there)
  const int len=orig_pages.size();
  for (int iA=0;iA<len;iA++) {
    pdf->removePage(orig_pages[iA]);
  }

  // we remove stuff that becomes defunct (probably)  TODO
  pdf->getRoot().removeKey("/PageMode");
  pdf->getRoot().removeKey("/Outlines");
  pdf->getRoot().removeKey("/OpenAction");
  pdf->getRoot().removeKey("/PageLabels");
}
// }}}

bool QPDF_PDFTOPDF_Processor::check_print_permissions() // {{{
{
  if (!pdf) {
    error("No PDF loaded");
    return false;
  }
  return pdf->allowPrintHighRes() || pdf->allowPrintLowRes(); // from legacy pdftopdf
}
// }}}


std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> QPDF_PDFTOPDF_Processor::get_pages() // {{{
{
  std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> ret;
  if (!pdf) {
    error("No PDF loaded");
    assert(0);
    return ret;
  }
  const int len=orig_pages.size();
  ret.reserve(len);
  for (int iA=0;iA<len;iA++) {
    ret.push_back(std::shared_ptr<PDFTOPDF_PageHandle>(new QPDF_PDFTOPDF_PageHandle(orig_pages[iA],iA+1)));
  }
  return ret;
}
// }}}

std::shared_ptr<PDFTOPDF_PageHandle> QPDF_PDFTOPDF_Processor::new_page(float width,float height) // {{{
{
  if (!pdf) {
    error("No PDF loaded");
    assert(0);
    return std::shared_ptr<PDFTOPDF_PageHandle>();
  }
  return std::shared_ptr<QPDF_PDFTOPDF_PageHandle>(new QPDF_PDFTOPDF_PageHandle(pdf.get(),width,height));
  // return std::make_shared<QPDF_PDFTOPDF_PageHandle>(pdf.get(),width,height);  // problem: make_shared not friend
}
// }}}

void QPDF_PDFTOPDF_Processor::add_page(std::shared_ptr<PDFTOPDF_PageHandle> page,bool front) // {{{
{
  assert(pdf);
  auto qpage=dynamic_cast<QPDF_PDFTOPDF_PageHandle *>(page.get());
  if (qpage) {
    pdf->addPage(qpage->get(),front);
  }
}
// }}}

#if 0
  // we remove stuff now probably defunct  TODO
  pdf->getRoot().removeKey("/PageMode");
  pdf->getRoot().removeKey("/Outlines");
  pdf->getRoot().removeKey("/OpenAction");
  pdf->getRoot().removeKey("/PageLabels");
#endif

// TODO? test
void QPDF_PDFTOPDF_Processor::multiply(int copies) // {{{
{
  assert(pdf);
  assert(copies>0); 

  std::vector<QPDFObjectHandle> pages=pdf->getAllPages();
  const int len=pages.size();

  for (int iA=1;iA<copies;iA++) {
    for (int iB=0;iB<len;iB++) {
      pdf->addPage(pages[iB].shallowCopy(),false);
    }
  }
}
// }}}
 
#include "qpdf_cm.h"

// TODO
void QPDF_PDFTOPDF_Processor::addCM(const char *defaulticc,const char *outputicc) // {{{
{
  assert(pdf);

  if (hasOutputIntent(*pdf)) {
    return; // nothing to do
  }

  QPDFObjectHandle srcicc=setDefaultICC(*pdf,defaulticc);
  addDefaultRGB(*pdf,srcicc);

  addOutputIntent(*pdf,outputicc);
}
// }}}

void QPDF_PDFTOPDF_Processor::emitFile(FILE *f,ArgOwnership take) // {{{
{
  if (!pdf) {
    return;
  }
  QPDFWriter out(*pdf);
  switch (take) {
  case WillStayAlive:
    out.setOutputFile("temp file",f,false);
    break;
  case TakeOwnership:
    out.setOutputFile("temp file",f,true);
    break;
  case MustDuplicate:
    error("emitFile with MustDuplicate is not supported");
    return;
  }
  out.write();
}
// }}}

void QPDF_PDFTOPDF_Processor::emitFilename(const char *name) // {{{
{
  if (!pdf) {
    return;
  }
  // special case: name==NULL -> stdout
  QPDFWriter out(*pdf,name);
  out.write();
}
// }}}

  // TODO:
  //   loadPDF();   success?

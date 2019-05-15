#ifndef PDFTOPDF_PROCESSOR_H
#define PDFTOPDF_PROCESSOR_H

#include "pptypes.h"
#include "nup.h"
#include "intervalset.h"
#include <vector>
#include <string>

enum BookletMode { BOOKLET_OFF, BOOKLET_ON, BOOKLET_JUSTSHUFFLE };

struct ProcessingParameters {
ProcessingParameters()
: jobId(0),numCopies(1),
    user(0),title(0),
    fitplot(false),
    fillprint(false),  //print-scaling = fill
    cropfit(false),
    autoprint(false),
    autofit(false),
    fidelity(false),
    noOrientation(false),
    orientation(ROT_0),normal_landscape(ROT_270),
    paper_is_landscape(false),
    duplex(false),
    border(NONE),
    reverse(false),

    pageLabel(),
    evenPages(true),oddPages(true),

    mirror(false),

    xpos(CENTER),ypos(CENTER),

    collate(false),
    evenDuplex(false),

    booklet(BOOKLET_OFF),bookSignature(-1),

    autoRotate(false),

    emitJCL(true),deviceCopies(1),
    deviceCollate(false),setDuplex(false),

    page_logging(-1)
  {
    page.width=612.0; // letter
    page.height=792.0;
    page.top=page.height-36.0;
    page.bottom=36.0;
    page.left=18.0;
    page.right=page.width-18.0;

    // everything
    pageRange.add(1);
    pageRange.finish();
  }

  int jobId, numCopies;
  const char *user, *title; // will stay around
  bool fitplot;
  bool fillprint;   //print-scaling = fill
  bool cropfit;     // -o crop-to-fit
  bool autoprint;   // print-scaling = auto
  bool autofit;     // print-scaling = auto-fit
  bool fidelity;
  bool noOrientation;
  PageRect page;
  Rotation orientation,normal_landscape;  // normal_landscape (i.e. default direction) is e.g. needed for number-up=2
  bool paper_is_landscape;
  bool duplex;
  BorderType border;
  NupParameters nup;
  bool reverse;

  std::string pageLabel;
  bool evenPages,oddPages;
  IntervalSet pageRange;

  bool mirror;

  Position xpos,ypos;

  bool collate;

  bool evenDuplex; // make number of pages a multiple of 2

  BookletMode booklet;
  int bookSignature;

  bool autoRotate;

  // ppd/jcl changes
  bool emitJCL;
  int deviceCopies;
  bool deviceCollate;
  bool setDuplex;
  // unsetMirror  (always)

  int page_logging;
  int copies_to_be_logged;

  // helper functions
  bool withPage(int outno) const; // 1 based
  void dump() const;
};

#include <stdio.h>
#include <memory>

enum ArgOwnership { WillStayAlive,MustDuplicate,TakeOwnership };

class PDFTOPDF_PageHandle {
 public:
  virtual ~PDFTOPDF_PageHandle() {}
  virtual PageRect getRect() const =0;
  // fscale:  inverse_scale (from nup, fitplot)
  virtual void add_border_rect(const PageRect &rect,BorderType border,float fscale) =0;
  // TODO?! add standalone crop(...) method (not only for subpages)
  virtual Rotation crop(const PageRect &cropRect,Rotation orientation,Position xpos,Position ypos,bool scale) =0;
  virtual bool is_landscape(Rotation orientation) =0 ;
  virtual void add_subpage(const std::shared_ptr<PDFTOPDF_PageHandle> &sub,float xpos,float ypos,float scale,const PageRect *crop=NULL) =0;
  virtual void mirror() =0;
  virtual void rotate(Rotation rot) =0;
  virtual void add_label(const PageRect &rect, const std::string label) =0;
};

// TODO: ... error output?
class PDFTOPDF_Processor { // abstract interface
 public:
  virtual ~PDFTOPDF_Processor() {}

  // TODO: ... qpdf wants password at load time
  virtual bool loadFile(FILE *f,ArgOwnership take=WillStayAlive,int flatten_forms=1) =0;
  virtual bool loadFilename(const char *name,int flatten_forms=1) =0;

  // TODO? virtual bool may_modify/may_print/?
  virtual bool check_print_permissions() =0;

  virtual std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> get_pages() =0; // shared_ptr because of type erasure (deleter)

  virtual std::shared_ptr<PDFTOPDF_PageHandle> new_page(float width,float height) =0;

  virtual void add_page(std::shared_ptr<PDFTOPDF_PageHandle> page,bool front) =0; // at back/front -- either from get_pages() or new_page()+add_subpage()-calls  (or [also allowed]: empty)

  //  void remove_page(std::shared_ptr<PDFTOPDF_PageHandle> ph);  // not needed: we construct from scratch, at least conceptually.

  virtual void multiply(int copies,bool collate) =0;

  virtual void autoRotateAll(bool dst_lscape,Rotation normal_landscape) =0; // TODO elsewhere?!
  virtual void addCM(const char *defaulticc,const char *outputicc) =0;

  virtual void setComments(const std::vector<std::string> &comments) =0;

  virtual void emitFile(FILE *dst,ArgOwnership take=WillStayAlive) =0;
  virtual void emitFilename(const char *name) =0; // NULL -> stdout

  virtual bool hasAcroForm() =0;
};

class PDFTOPDF_Factory {
 public:
  // never NULL, but may throw.
  static PDFTOPDF_Processor *processor();
};

//bool checkBookletSignature(int signature) { return (signature%4==0); }
std::vector<int> bookletShuffle(int numPages,int signature=-1);

// This is all we want:
bool processPDFTOPDF(PDFTOPDF_Processor &proc,ProcessingParameters &param);

#endif

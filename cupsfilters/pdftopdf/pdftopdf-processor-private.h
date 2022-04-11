#ifndef _CUPS_FILTERS_PDFTOPDF_PDFTOPDF_PROCESSOR_H
#define _CUPS_FILTERS_PDFTOPDF_PDFTOPDF_PROCESSOR_H

#include "pptypes-private.h"
#include "nup-private.h"
#include "pdftopdf-private.h"
#include "intervalset-private.h"
#include <vector>
#include <string>
#include <memory>
#include <stdio.h>

enum pdftopdf_booklet_mode_e {
  CF_PDFTOPDF_BOOKLET_OFF,
  CF_PDFTOPDF_BOOKLET_ON,
  CF_PDFTOPDF_BOOKLET_JUST_SHUFFLE
};

struct _cfPDFToPDFProcessingParameters {
_cfPDFToPDFProcessingParameters()
: job_id(0),num_copies(1),
    user(0),title(0),
    fitplot(false),
    fillprint(false),  //print-scaling = fill
    cropfit(false),
    autoprint(false),
    autofit(false),
    fidelity(false),
    no_orientation(false),
    orientation(ROT_0),normal_landscape(ROT_270),
    paper_is_landscape(false),
    duplex(false),
    border(NONE),
    reverse(false),

    page_label(),
    even_pages(true),odd_pages(true),

    mirror(false),

    xpos(CENTER),ypos(CENTER),

    collate(false),
    even_duplex(false),

    booklet(CF_PDFTOPDF_BOOKLET_OFF),book_signature(-1),

    auto_rotate(false),

    emit_jcl(true),device_copies(1),
    device_collate(false),set_duplex(false),

    page_logging(-1)
  {
    page.width=612.0; // letter
    page.height=792.0;
    page.top=page.height-36.0;
    page.bottom=36.0;
    page.left=18.0;
    page.right=page.width-18.0;

    // everything
    input_page_ranges.add(1);
    input_page_ranges.finish();
    page_ranges.add(1);
    page_ranges.finish();
  }

  int job_id, num_copies;
  const char *user, *title; // will stay around
  bool fitplot;
  bool fillprint;   //print-scaling = fill
  bool cropfit;     // -o crop-to-fit
  bool autoprint;   // print-scaling = auto
  bool autofit;     // print-scaling = auto-fit
  bool fidelity;
  bool no_orientation;
  _cfPDFToPDFPageRect page;
  pdftopdf_rotation_e orientation,normal_landscape;  // normal_landscape (i.e. default direction) is e.g. needed for number-up=2
  bool paper_is_landscape;
  bool duplex;
  pdftopdf_border_type_e border;
  _cfPDFToPDFNupParameters nup;
  bool reverse;

  std::string page_label;
  bool even_pages,odd_pages;
  _cfPDFToPDFIntervalSet page_ranges;
  _cfPDFToPDFIntervalSet input_page_ranges;

  bool mirror;

  pdftopdf_position_e xpos,ypos;

  bool collate;

  bool even_duplex; // make number of pages a multiple of 2

  pdftopdf_booklet_mode_e booklet;
  int book_signature;

  bool auto_rotate;

  // ppd/jcl changes
  bool emit_jcl;
  int device_copies;
  bool device_collate;
  bool set_duplex;

  int page_logging;
  int copies_to_be_logged;

  // helper functions
  bool with_page(int outno) const; // 1 based
  bool have_page(int pageno) const; //1 based
  void dump(pdftopdf_doc_t *doc) const;
};

enum pdftopdf_arg_ownership_e {
  CF_PDFTOPDF_WILL_STAY_ALIVE,
  CF_PDFTOPDF_MUST_DUPLICATE,
  CF_PDFTOPDF_TAKE_OWNERSHIP
};

class _cfPDFToPDFPageHandle {
 public:
  virtual ~_cfPDFToPDFPageHandle() {}
  virtual _cfPDFToPDFPageRect get_rect() const =0;
  // fscale:  inverse_scale (from nup, fitplot)
  virtual void add_border_rect(const _cfPDFToPDFPageRect &rect,pdftopdf_border_type_e border,float fscale) =0;
  // TODO?! add standalone crop(...) method (not only for subpages)
  virtual pdftopdf_rotation_e crop(const _cfPDFToPDFPageRect &cropRect,pdftopdf_rotation_e orientation,pdftopdf_rotation_e param_orientation,pdftopdf_position_e xpos,pdftopdf_position_e ypos,bool scale,bool autorotate,pdftopdf_doc_t *doc) =0;
  virtual bool is_landscape(pdftopdf_rotation_e orientation) =0 ;
  virtual void add_subpage(const std::shared_ptr<_cfPDFToPDFPageHandle> &sub,float xpos,float ypos,float scale,const _cfPDFToPDFPageRect *crop=NULL) =0;
  virtual void mirror() =0;
  virtual void rotate(pdftopdf_rotation_e rot) =0;
  virtual void add_label(const _cfPDFToPDFPageRect &rect, const std::string label) =0;
};

// TODO: ... error output?
class _cfPDFToPDFProcessor { // abstract interface
 public:
  virtual ~_cfPDFToPDFProcessor() {}

  // TODO: ... qpdf wants password at load time
  virtual bool load_file(FILE *f,pdftopdf_doc_t *doc,pdftopdf_arg_ownership_e take=CF_PDFTOPDF_WILL_STAY_ALIVE,int flatten_forms=1) =0;
  virtual bool load_filename(const char *name,pdftopdf_doc_t *doc,int flatten_forms=1) =0;

  // TODO? virtual bool may_modify/may_print/?
  virtual bool check_print_permissions(pdftopdf_doc_t *doc) =0;

  virtual std::vector<std::shared_ptr<_cfPDFToPDFPageHandle>> get_pages(pdftopdf_doc_t *doc) =0; // shared_ptr because of type erasure (deleter)

  virtual std::shared_ptr<_cfPDFToPDFPageHandle> new_page(float width,float height,pdftopdf_doc_t *doc) =0;

  virtual void add_page(std::shared_ptr<_cfPDFToPDFPageHandle> page,bool front) =0; // at back/front -- either from get_pages() or new_page()+add_subpage()-calls  (or [also allowed]: empty)

  //  void remove_page(std::shared_ptr<_cfPDFToPDFPageHandle> ph);  // not needed: we construct from scratch, at least conceptually.

  virtual void multiply(int copies,bool collate) =0;

  virtual void auto_rotate_all(bool dst_lscape,pdftopdf_rotation_e normal_landscape) =0; // TODO elsewhere?!
  virtual void add_cm(const char *defaulticc,const char *outputicc) =0;

  virtual void set_comments(const std::vector<std::string> &comments) =0;

  virtual void emit_file(FILE *dst,pdftopdf_doc_t *doc,pdftopdf_arg_ownership_e take=CF_PDFTOPDF_WILL_STAY_ALIVE) =0;
  virtual void emit_filename(const char *name,pdftopdf_doc_t *doc) =0; // NULL -> stdout

  virtual bool has_acro_form() =0;
};

class _cfPDFToPDFFactory {
 public:
  // never NULL, but may throw.
  static _cfPDFToPDFProcessor *processor();
};

//bool _cfPDFToPDFCheckBookletSignature(int signature) { return (signature%4==0); }
std::vector<int> _cfPDFToPDFBookletShuffle(int numPages,int signature=-1);

// This is all we want:
bool _cfProcessPDFToPDF(_cfPDFToPDFProcessor &proc,_cfPDFToPDFProcessingParameters &param,pdftopdf_doc_t *doc);

#endif

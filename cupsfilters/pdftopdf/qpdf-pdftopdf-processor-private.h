#ifndef _CUPS_FILTERS_PDFTOPDF_QPDF_PDFTOPDF_PROCESSOR_H
#define _CUPS_FILTERS_PDFTOPDF_QPDF_PDFTOPDF_PROCESSOR_H

#include "pdftopdf-processor-private.h"
#include <qpdf/QPDF.hh>

class _cfPDFToPDFQPDFPageHandle : public _cfPDFToPDFPageHandle {
 public:
  virtual _cfPDFToPDFPageRect get_rect() const;
  virtual void add_border_rect(const _cfPDFToPDFPageRect &rect,pdftopdf_border_type_e border,float fscale);
  virtual void add_subpage(const std::shared_ptr<_cfPDFToPDFPageHandle> &sub,float xpos,float ypos,float scale,const _cfPDFToPDFPageRect *crop=NULL);
  virtual void mirror();
  virtual void rotate(pdftopdf_rotation_e rot);
  virtual void add_label(const _cfPDFToPDFPageRect &rect, const std::string label);
  virtual pdftopdf_rotation_e crop(const _cfPDFToPDFPageRect &cropRect,pdftopdf_rotation_e orientation,pdftopdf_rotation_e param_orientation,pdftopdf_position_e xpos,pdftopdf_position_e ypos,bool scale,bool autorotate,pdftopdf_doc_t *doc);
  virtual bool is_landscape(pdftopdf_rotation_e orientation);
  void debug(const _cfPDFToPDFPageRect &rect,float xpos,float ypos);
 private:
  bool is_existing() const;
  QPDFObjectHandle get(); // only once!
 private:
  friend class _cfPDFToPDFQPDFProcessor;
  // 1st mode: existing
  _cfPDFToPDFQPDFPageHandle(QPDFObjectHandle page,int orig_no=-1);
  QPDFObjectHandle page;
  int no;

  // 2nd mode: create new
  _cfPDFToPDFQPDFPageHandle(QPDF *pdf,float width,float height);
  std::map<std::string,QPDFObjectHandle> xobjs;
  std::string content;

  pdftopdf_rotation_e rotation;
};

class _cfPDFToPDFQPDFProcessor : public _cfPDFToPDFProcessor {
 public:
  virtual bool load_file(FILE *f,pdftopdf_doc_t *doc,pdftopdf_arg_ownership_e take=CF_PDFTOPDF_WILL_STAY_ALIVE,int flatten_forms=1);
  virtual bool load_filename(const char *name,pdftopdf_doc_t *doc,int flatten_forms=1);

  // TODO: virtual bool may_modify/may_print/?
  virtual bool check_print_permissions(pdftopdf_doc_t *doc);

  // virtual bool set_process(const _cfPDFToPDFProcessingParameters &param) =0;

  virtual std::vector<std::shared_ptr<_cfPDFToPDFPageHandle>> get_pages(pdftopdf_doc_t *doc);
  virtual std::shared_ptr<_cfPDFToPDFPageHandle> new_page(float width,float height,pdftopdf_doc_t *doc);

  virtual void add_page(std::shared_ptr<_cfPDFToPDFPageHandle> page,bool front);

  virtual void multiply(int copies,bool collate);

  virtual void auto_rotate_all(bool dst_lscape,pdftopdf_rotation_e normal_landscape);
  virtual void add_cm(const char *defaulticc,const char *outputicc);

  virtual void set_comments(const std::vector<std::string> &comments);

  virtual void emit_file(FILE *dst,pdftopdf_doc_t *doc,pdftopdf_arg_ownership_e take=CF_PDFTOPDF_WILL_STAY_ALIVE);
  virtual void emit_filename(const char *name,pdftopdf_doc_t *doc);

  virtual bool has_acro_form();
 private:
  void close_file();
  void start(int flatten_forms);
 private:
  std::unique_ptr<QPDF> pdf;
  std::vector<QPDFObjectHandle> orig_pages;

  bool hasCM;
  std::string extraheader;
};

#endif

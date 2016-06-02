#ifndef QPDF_PDFTOPDF_PROCESSOR_H
#define QPDF_PDFTOPDF_PROCESSOR_H

#include "pdftopdf_processor.h"
#include <qpdf/QPDF.hh>

class QPDF_PDFTOPDF_PageHandle : public PDFTOPDF_PageHandle {
 public:
  virtual PageRect getRect() const;
  virtual void add_border_rect(const PageRect &rect,BorderType border,float fscale);
  virtual void add_subpage(const std::shared_ptr<PDFTOPDF_PageHandle> &sub,float xpos,float ypos,float scale,const PageRect *crop=NULL);
  virtual void mirror();
  virtual void rotate(Rotation rot);
  virtual void add_label(const PageRect &rect, const std::string label);

  void debug(const PageRect &rect,float xpos,float ypos);
 private:
  bool isExisting() const;
  QPDFObjectHandle get(); // only once!
 private:
  friend class QPDF_PDFTOPDF_Processor;
  // 1st mode: existing
  QPDF_PDFTOPDF_PageHandle(QPDFObjectHandle page,int orig_no=-1);
  QPDFObjectHandle page;
  int no;

  // 2nd mode: create new
  QPDF_PDFTOPDF_PageHandle(QPDF *pdf,float width,float height);
  std::map<std::string,QPDFObjectHandle> xobjs;
  std::string content;

  Rotation rotation;
};

class QPDF_PDFTOPDF_Processor : public PDFTOPDF_Processor {
 public:
  virtual bool loadFile(FILE *f,ArgOwnership take=WillStayAlive);
  virtual bool loadFilename(const char *name);

  // TODO: virtual bool may_modify/may_print/?
  virtual bool check_print_permissions();

  // virtual bool setProcess(const ProcessingParameters &param) =0;

  virtual std::vector<std::shared_ptr<PDFTOPDF_PageHandle>> get_pages();
  virtual std::shared_ptr<PDFTOPDF_PageHandle> new_page(float width,float height);

  virtual void add_page(std::shared_ptr<PDFTOPDF_PageHandle> page,bool front);

  virtual void multiply(int copies,bool collate);

  virtual void autoRotateAll(bool dst_lscape,Rotation normal_landscape);
  virtual void addCM(const char *defaulticc,const char *outputicc);

  virtual void setComments(const std::vector<std::string> &comments);

  virtual void emitFile(FILE *dst,ArgOwnership take=WillStayAlive);
  virtual void emitFilename(const char *name);

 private:
  void closeFile();
  void error(const char *fmt,...);
  void start();
 private:
  std::unique_ptr<QPDF> pdf;
  std::vector<QPDFObjectHandle> orig_pages;

  bool hasCM;
  std::string extraheader;
};

#endif

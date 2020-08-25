#include "qpdf_cm.h"
#include <stdio.h>
#include <assert.h>

#include <stdexcept>

// TODO? instead use qpdf's StreamDataProvider, FileInputSource, Buffer etc.
static std::string load_file(const char *filename) // {{{
{
  if (!filename) {
    throw std::invalid_argument("NULL filename not allowed");
  }

  FILE *f=fopen(filename,"r");
  if (!f) {
    throw std::runtime_error(std::string("file ") + filename + " could not be opened");
  }

  const int bsize=2048;
  int pos=0;

  std::string ret;
  while (!feof(f)) {
    ret.resize(pos+bsize);
    int res=fread(&ret[pos],1,bsize,f);
    pos+=res;
    if (res<bsize) {
      ret.resize(pos);
      break;
    }
  }

  fclose(f);
  return ret;
}
// }}}


// TODO?
// TODO? test
bool hasOutputIntent(QPDF &pdf) // {{{
{
  auto catalog=pdf.getRoot();
  if (!catalog.hasKey("/OutputIntents")) {
    return false;
  }
  return true; // TODO?
}
// }}}

// TODO: test
// TODO? find existing , replace and return  (?)
void addOutputIntent(QPDF &pdf,const char *filename) // {{{
{
  std::string icc=load_file(filename);
  // TODO: check icc  fitness
  // ICC data, subject to "version limitations" per pdf version...

  QPDFObjectHandle outicc=QPDFObjectHandle::newStream(&pdf,icc);

  auto sdict=outicc.getDict();
  sdict.replaceKey("/N",QPDFObjectHandle::newInteger(4)); // must match ICC
  // /Range ?  // must match ICC, default [0.0 1.0 ...]
  // /Alternate ?  (/DeviceCMYK for N=4)

  auto intent=QPDFObjectHandle::parse(
    "<<"
    "  /Type /OutputIntent"       // Must be so (the standard requires).
    "  /S /GTS_PDFX"              // Must be so (the standard requires).
    "  /OutputCondition (Commercial and specialty printing)"  // TODO: Customize [optional(?)]
    "  /Info (none)"              // TODO: Customize
    "  /OutputConditionIdentifier (CGATS TR001)"  // TODO: FIXME: Customize
    "  /RegistryName (http://www.color.org)"      // Must be so (the standard requires).
    "  /DestOutputProfile null "
    ">>");
  intent.replaceKey("/DestOutputProfile",outicc);

  auto catalog=pdf.getRoot();
  if (!catalog.hasKey("/OutputIntents")) {
    catalog.replaceKey("/OutputIntents",QPDFObjectHandle::newArray());
  }
  catalog.getKey("/OutputIntents").appendItem(intent);
}
// }}}


/* for color management:
   Use /DefaultGray, /DefaultRGB, /DefaultCMYK ...  from *current* resource dictionary ...
   i.e. set 
   /Resources <<
   /ColorSpace <<    --- can use just one indirect ref for this (probably)
   /DefaultRGB [/ICCBased 5 0 R]   ... sensible use is sRGB  for DefaultRGB, etc.
   >>
   >>
   for every page  (what with form /XObjects?)  and most importantly RGB (leave CMYK, Gray for now, as this is already printer native(?))

   ? also every  form XObject, pattern, type3 font, annotation appearance stream(=form xobject +X)

   ? what if page already defines /Default?   -- probably keep!

   ? maybe we need to set /ColorSpace  in /Images ?    [gs idea is to just add the /Default-key and then reprocess...]
   
*/

// TODO? test
void addDefaultRGB(QPDF &pdf,QPDFObjectHandle srcicc) // {{{
{
  srcicc.assertStream();

  auto pages=pdf.getAllPages();
  for (auto it=pages.begin(),end=pages.end();it!=end;++it) {
    if (!it->hasKey("/Resources")) {
      it->replaceKey("/Resources",QPDFObjectHandle::newDictionary());
    }
    auto rdict=it->getKey("/Resources");

    if (!rdict.hasKey("/ColorSpace")) {
      rdict.replaceKey("/ColorSpace",QPDFObjectHandle::newDictionary());
    }
    auto cdict=rdict.getKey("/ColorSpace");

    if (!cdict.hasKey("/DefaultRGB")) {
      cdict.replaceKey("/DefaultRGB",QPDFObjectHandle::parse("[/ICCBased ]"));
      cdict.getKey("/DefaultRGB").appendItem(srcicc);
    }
  }
}
// }}}

// TODO? test
// TODO: find existing , replace and return  (?)
// TODO: check icc  fitness
QPDFObjectHandle setDefaultICC(QPDF &pdf,const char *filename) // {{{
{
  // TODO: find existing , replace and return  (?)

  std::string icc=load_file(filename);
  // TODO: check icc  fitness
  // ICC data, subject to "version limitations" per pdf version...

  QPDFObjectHandle ret=QPDFObjectHandle::newStream(&pdf,icc);

  auto sdict=ret.getDict();
  sdict.replaceKey("/N",QPDFObjectHandle::newInteger(3)); // must match ICC
  // /Range ?  // must match ICC, default [0.0 1.0 ...]
  // /Alternate ?  (/DeviceRGB for N=3)

  return ret;
}
// }}}


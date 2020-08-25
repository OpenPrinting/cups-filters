#include "qpdf_tools.h"

QPDFObjectHandle getMediaBox(QPDFObjectHandle page) // {{{
{
  return page.getKey("/MediaBox");
}
// }}}

QPDFObjectHandle getCropBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/CropBox")) {
    return page.getKey("/CropBox");
  }
  return page.getKey("/MediaBox");
}
// }}}

QPDFObjectHandle getBleedBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/BleedBox")) {
    return page.getKey("/BleedBox");
  }
  return getCropBox(page);
}
// }}}

QPDFObjectHandle getTrimBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/TrimBox")) {
    return page.getKey("/TrimBox");
  }
  return getCropBox(page);
}
// }}}

QPDFObjectHandle getArtBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/ArtBox")) {
    return page.getKey("/ArtBox");
  }
  return getCropBox(page);
}
// }}}

QPDFObjectHandle makePage(QPDF &pdf,const std::map<std::string,QPDFObjectHandle> &xobjs,QPDFObjectHandle mediabox,const std::string &content) // {{{
{
  QPDFObjectHandle ret=QPDFObjectHandle::newDictionary();
  ret.replaceKey("/Type",QPDFObjectHandle::newName("/Page"));

  auto resdict=QPDFObjectHandle::newDictionary();
  resdict.replaceKey("/XObject",QPDFObjectHandle::newDictionary(xobjs));
  ret.replaceKey("/Resources",resdict);
  ret.replaceKey("/MediaBox",mediabox);
  ret.replaceKey("/Contents",QPDFObjectHandle::newStream(&pdf,content));

  return ret;
}
// }}}

QPDFObjectHandle makeBox(double x1,double y1,double x2,double y2) // {{{
{
  QPDFObjectHandle ret=QPDFObjectHandle::newArray();
  ret.appendItem(QPDFObjectHandle::newReal(x1));
  ret.appendItem(QPDFObjectHandle::newReal(y1));
  ret.appendItem(QPDFObjectHandle::newReal(x2));
  ret.appendItem(QPDFObjectHandle::newReal(y2));
  return ret;
}
// }}}

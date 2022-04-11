#include "qpdf-tools-private.h"

QPDFObjectHandle _cfPDFToPDFGetMediaBox(QPDFObjectHandle page) // {{{
{
  return page.getKey("/MediaBox");
}
// }}}

QPDFObjectHandle _cfPDFToPDFGetCropBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/CropBox")) {
    return page.getKey("/CropBox");
  }
  return page.getKey("/MediaBox");
}
// }}}

QPDFObjectHandle _cfPDFToPDFGetBleedBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/BleedBox")) {
    return page.getKey("/BleedBox");
  }
  return _cfPDFToPDFGetCropBox(page);
}
// }}}

QPDFObjectHandle _cfPDFToPDFGetTrimBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/TrimBox")) {
    return page.getKey("/TrimBox");
  }
  return _cfPDFToPDFGetCropBox(page);
}
// }}}

QPDFObjectHandle _cfPDFToPDFGetArtBox(QPDFObjectHandle page) // {{{
{
  if (page.hasKey("/ArtBox")) {
    return page.getKey("/ArtBox");
  }
  return _cfPDFToPDFGetCropBox(page);
}
// }}}

QPDFObjectHandle _cfPDFToPDFMakePage(QPDF &pdf,const std::map<std::string,QPDFObjectHandle> &xobjs,QPDFObjectHandle mediabox,const std::string &content) // {{{
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

QPDFObjectHandle _cfPDFToPDFMakeBox(double x1,double y1,double x2,double y2) // {{{
{
  QPDFObjectHandle ret=QPDFObjectHandle::newArray();
  ret.appendItem(QPDFObjectHandle::newReal(x1));
  ret.appendItem(QPDFObjectHandle::newReal(y1));
  ret.appendItem(QPDFObjectHandle::newReal(x2));
  ret.appendItem(QPDFObjectHandle::newReal(y2));
  return ret;
}
// }}}

#ifndef _CUPS_FILTERS_PDFTOPDF_QPDF_TOOLS_H_
#define _CUPS_FILTERS_PDFTOPDF_QPDF_TOOLS_H_

#include <qpdf/QPDFObjectHandle.hh>
#include <map>
#include <string>

QPDFObjectHandle _cfPDFToPDFGetMediaBox(QPDFObjectHandle page);
QPDFObjectHandle _cfPDFToPDFGetCropBox(QPDFObjectHandle page);
QPDFObjectHandle _cfPDFToPDFGetBleedBox(QPDFObjectHandle page);
QPDFObjectHandle _cfPDFToPDFGetTrimBox(QPDFObjectHandle page);
QPDFObjectHandle _cfPDFToPDFGetArtBox(QPDFObjectHandle page);

QPDFObjectHandle _cfPDFToPDFMakePage(QPDF &pdf,const std::map<std::string,QPDFObjectHandle> &xobjs,QPDFObjectHandle mediabox,const std::string &content);

QPDFObjectHandle _cfPDFToPDFMakeBox(double x1,double y1,double x2,double y2);

#endif

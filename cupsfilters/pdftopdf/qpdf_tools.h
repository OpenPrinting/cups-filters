#ifndef QPDF_TOOLS_H_
#define QPDF_TOOLS_H_

#include <qpdf/QPDFObjectHandle.hh>
#include <map>
#include <string>

QPDFObjectHandle getMediaBox(QPDFObjectHandle page);
QPDFObjectHandle getCropBox(QPDFObjectHandle page);
QPDFObjectHandle getBleedBox(QPDFObjectHandle page);
QPDFObjectHandle getTrimBox(QPDFObjectHandle page);
QPDFObjectHandle getArtBox(QPDFObjectHandle page);

QPDFObjectHandle makePage(QPDF &pdf,const std::map<std::string,QPDFObjectHandle> &xobjs,QPDFObjectHandle mediabox,const std::string &content);

QPDFObjectHandle makeBox(double x1,double y1,double x2,double y2);

#endif

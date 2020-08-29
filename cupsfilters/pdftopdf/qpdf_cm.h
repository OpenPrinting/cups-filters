#ifndef QPDF_CM_H_
#define QPDF_CM_H_

#include <qpdf/QPDF.hh>

bool hasOutputIntent(QPDF &pdf);
void addOutputIntent(QPDF &pdf,const char *filename);

void addDefaultRGB(QPDF &pdf,QPDFObjectHandle srcicc);
QPDFObjectHandle setDefaultICC(QPDF &pdf,const char *filename);

#endif

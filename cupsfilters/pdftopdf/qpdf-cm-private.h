//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_FILTERS_PDFTOPDF_QPDF_CM_H_
#define _CUPS_FILTERS_PDFTOPDF_QPDF_CM_H_

#include <qpdf/QPDF.hh>

bool _cfPDFToPDFHasOutputIntent(QPDF &pdf);
void _cfPDFToPDFAddOutputIntent(QPDF &pdf, const char *filename);

void _cfPDFToPDFAddDefaultRGB(QPDF &pdf, QPDFObjectHandle srcicc);
QPDFObjectHandle _cfPDFToPDFSetDefaultICC(QPDF &pdf, const char *filename);

#endif // !_CUPS_FILTERS_PDFTOPDF_QPDF_CM_H_

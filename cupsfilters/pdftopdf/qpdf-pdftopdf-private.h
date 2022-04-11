#ifndef _CUPS_FILTERS_PDFTOPDF_QPDF_PDFTOPDF_H
#define _CUPS_FILTERS_PDFTOPDF_QPDF_PDFTOPDF_H

#include <qpdf/QPDFObjectHandle.hh>
#include "pptypes-private.h"

// helper functions

_cfPDFToPDFPageRect _cfPDFToPDFGetBoxAsRect(QPDFObjectHandle box);
QPDFObjectHandle _cfPDFToPDFGetRectAsBox(const _cfPDFToPDFPageRect &rect);

// Note that PDF specification is CW, but our Rotation is CCW
pdftopdf_rotation_e _cfPDFToPDFGetRotate(QPDFObjectHandle page);
QPDFObjectHandle _cfPDFToPDFMakeRotate(pdftopdf_rotation_e rot); // Integer

double _cfPDFToPDFGetUserUnit(QPDFObjectHandle page);

// PDF CTM
class _cfPDFToPDFMatrix {
 public:
  _cfPDFToPDFMatrix(); // identity
  _cfPDFToPDFMatrix(QPDFObjectHandle ar);
  
  _cfPDFToPDFMatrix &rotate(pdftopdf_rotation_e rot);
  _cfPDFToPDFMatrix &rotate_move(pdftopdf_rotation_e rot,double width,double height);
  _cfPDFToPDFMatrix &rotate(double rad);
  //  _cfPDFToPDFMatrix &rotate_deg(double deg);

  _cfPDFToPDFMatrix &translate(double tx,double ty);
  _cfPDFToPDFMatrix &scale(double sx,double sy);
  _cfPDFToPDFMatrix &scale(double s) { return scale(s,s); }

  _cfPDFToPDFMatrix &operator*=(const _cfPDFToPDFMatrix &rhs);

  QPDFObjectHandle get() const;
  std::string get_string() const;
 private:
  double ctm[6];
};

#endif

#ifndef QPDF_PDFTOPDF_H
#define QPDF_PDFTOPDF_H

#include <qpdf/QPDFObjectHandle.hh>
#include "pptypes.h"

// helper functions

PageRect getBoxAsRect(QPDFObjectHandle box);
QPDFObjectHandle getRectAsBox(const PageRect &rect);

// Note that PDF specification is CW, but our Rotation is CCW
Rotation getRotate(QPDFObjectHandle page);
QPDFObjectHandle makeRotate(Rotation rot); // Integer

double getUserUnit(QPDFObjectHandle page);

// PDF CTM
class Matrix {
public:
  Matrix(); // identity
  Matrix(QPDFObjectHandle ar);
  
  Matrix &rotate(Rotation rot);
  Matrix &rotate_move(Rotation rot,double width,double height);
  Matrix &rotate(double rad);
//  Matrix &rotate_deg(double deg);

  Matrix &translate(double tx,double ty);
  Matrix &scale(double sx,double sy);
  Matrix &scale(double s) { return scale(s,s); }

  Matrix &operator*=(const Matrix &rhs);

  QPDFObjectHandle get() const;
  std::string get_string() const;
private:
  double ctm[6];
};

#endif

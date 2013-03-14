#include "qpdf_pdftopdf.h"
#include <assert.h>
#include <stdexcept>
#include <qpdf/QUtil.hh>

PageRect getBoxAsRect(QPDFObjectHandle box) // {{{
{
  PageRect ret;

  ret.left=box.getArrayItem(0).getNumericValue();
  ret.bottom=box.getArrayItem(1).getNumericValue();
  ret.right=box.getArrayItem(2).getNumericValue();
  ret.top=box.getArrayItem(3).getNumericValue();

  ret.width=ret.right-ret.left;
  ret.height=ret.top-ret.bottom;

  return ret;
}
// }}}

Rotation getRotate(QPDFObjectHandle page) // {{{
{
  if (!page.hasKey("/Rotate")) {
    return ROT_0;
  }
  double rot=page.getKey("/Rotate").getNumericValue();
  rot=fmod(rot,360.0);
  if (rot<0) {
    rot+=360.0;
  }
  if (rot==90.0) { // CW 
    return ROT_270; // CCW
  } else if (rot==180.0) {
    return ROT_180;
  } else if (rot==270.0) {
    return ROT_90;
  } else if (rot!=0.0) {
    throw std::runtime_error("Unexpected /Rotation value: "+QUtil::double_to_string(rot));
  }
  return ROT_0;
}
// }}}

double getUserUnit(QPDFObjectHandle page) // {{{
{
  if (!page.hasKey("/UserUnit")) {
    return 1.0;
  }
  return page.getKey("/UserUnit").getNumericValue();
}
// }}}

QPDFObjectHandle makeRotate(Rotation rot) // {{{
{
  switch (rot) {
  case ROT_0:
    return QPDFObjectHandle::newNull();
  case ROT_90: // CCW
    return QPDFObjectHandle::newInteger(270); // CW
  case ROT_180:
    return QPDFObjectHandle::newInteger(180);
  case ROT_270:
    return QPDFObjectHandle::newInteger(90);
  default:
    throw std::invalid_argument("Bad rotation");
  }
}
// }}}

#include "qpdf_tools.h"

QPDFObjectHandle getRectAsBox(const PageRect &rect) // {{{
{
  return makeBox(rect.left,rect.bottom,rect.right,rect.top);
}
// }}}

#include <qpdf/QUtil.hh>

Matrix::Matrix() // {{{
  : ctm{1,0,0,1,0,0}
{
}
// }}}

Matrix::Matrix(QPDFObjectHandle ar) // {{{
{
  if (ar.getArrayNItems()!=6) {
    throw std::runtime_error("Not a ctm matrix");
  }
  for (int iA=0;iA<6;iA++) {
    ctm[iA]=ar.getArrayItem(iA).getNumericValue();
  }
}
// }}}

Matrix &Matrix::rotate(Rotation rot) // {{{
{
  switch (rot) {
  case ROT_0:
    break;
  case ROT_90:
    std::swap(ctm[0],ctm[2]);
    std::swap(ctm[1],ctm[3]);
    ctm[2]=-ctm[2];
    ctm[3]=-ctm[3];
    break;
  case ROT_180:
    ctm[0]=-ctm[0];
    ctm[3]=-ctm[3];
    break;
  case ROT_270:
    std::swap(ctm[0],ctm[2]);
    std::swap(ctm[1],ctm[3]);
    ctm[0]=-ctm[0];
    ctm[1]=-ctm[1];
    break;
  default:
    assert(0);
  }
  return *this;
}
// }}}

// TODO: test
Matrix &Matrix::rotate_move(Rotation rot,double width,double height) // {{{
{
  rotate(rot);
  switch (rot) {
  case ROT_0:
    break;
  case ROT_90:
    translate(width,0);
    break;
  case ROT_180:
    translate(width,height);
    break;
  case ROT_270:
    translate(0,height);
    break;
  }
  return *this;
}
// }}}

Matrix &Matrix::rotate(double rad) // {{{
{
  Matrix tmp;

  tmp.ctm[0]=cos(rad);
  tmp.ctm[1]=sin(rad);
  tmp.ctm[2]=-sin(rad);
  tmp.ctm[3]=cos(rad);

  return (*this*=tmp);
}
// }}}

Matrix &Matrix::translate(double tx,double ty) // {{{
{
  ctm[4]+=ctm[0]*tx+ctm[2]*ty;
  ctm[5]+=ctm[1]*tx+ctm[3]*ty;
  return *this;
}
// }}}

Matrix &Matrix::scale(double sx,double sy) // {{{
{
  ctm[0]*=sx;
  ctm[1]*=sx;
  ctm[2]*=sy;
  ctm[3]*=sy;
  return *this;
}
// }}}

Matrix &Matrix::operator*=(const Matrix &rhs) // {{{
{
  double tmp[6];
  std::copy(ctm,ctm+6,tmp);

  ctm[0] = tmp[0]*rhs.ctm[0] + tmp[2]*rhs.ctm[1];
  ctm[1] = tmp[1]*rhs.ctm[0] + tmp[3]*rhs.ctm[1];

  ctm[2] = tmp[0]*rhs.ctm[2] + tmp[2]*rhs.ctm[3];
  ctm[3] = tmp[1]*rhs.ctm[2] + tmp[3]*rhs.ctm[3];

  ctm[4] = tmp[0]*rhs.ctm[4] + tmp[2]*rhs.ctm[5] + tmp[4];
  ctm[5] = tmp[1]*rhs.ctm[4] + tmp[3]*rhs.ctm[5] + tmp[5];

  return *this;
}
// }}}

QPDFObjectHandle Matrix::get() const // {{{
{
  QPDFObjectHandle ret=QPDFObjectHandle::newArray();
  ret.appendItem(QPDFObjectHandle::newReal(ctm[0]));
  ret.appendItem(QPDFObjectHandle::newReal(ctm[1]));
  ret.appendItem(QPDFObjectHandle::newReal(ctm[2]));
  ret.appendItem(QPDFObjectHandle::newReal(ctm[3]));
  ret.appendItem(QPDFObjectHandle::newReal(ctm[4]));
  ret.appendItem(QPDFObjectHandle::newReal(ctm[5]));
  return ret;
}
// }}}

std::string Matrix::get_string() const // {{{
{
  std::string ret;
  ret.append(QUtil::double_to_string(ctm[0]));
  ret.append(" ");
  ret.append(QUtil::double_to_string(ctm[1]));
  ret.append(" ");
  ret.append(QUtil::double_to_string(ctm[2]));
  ret.append(" ");
  ret.append(QUtil::double_to_string(ctm[3]));
  ret.append(" ");
  ret.append(QUtil::double_to_string(ctm[4]));
  ret.append(" ");
  ret.append(QUtil::double_to_string(ctm[5]));
  return ret;
}
// }}}


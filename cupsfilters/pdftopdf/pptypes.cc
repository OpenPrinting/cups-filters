#include "pptypes.h"
#include <utility>
#include <stdio.h>
#include <assert.h>

void Position_dump(Position pos,pdftopdf_doc_t *doc) // {{{
{
  static const char *pstr[3]={"Left/Bottom","Center","Right/Top"};
  if ((pos < LEFT) || (pos > RIGHT)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: (bad position: %d)\n",pos);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: %s\n",pstr[pos+1]);
  }
}
// }}}

void Position_dump(Position pos,Axis axis,pdftopdf_doc_t *doc) // {{{
{
  assert((axis == Axis::X) || (axis == Axis::Y));
  if ((pos < LEFT) || (pos > RIGHT)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "(bad position: %d)",pos);
    return;
  }
  if (axis==Axis::X) {
    static const char *pxstr[3]={"Left","Center","Right"};
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "%s", pxstr[pos+1]);
  } else {
    static const char *pystr[3]={"Bottom","Center","Top"};
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "%s",pystr[pos+1]);
  }
}
// }}}

void Rotation_dump(Rotation rot,pdftopdf_doc_t *doc) // {{{
{
  static const char *rstr[4]={"0 deg","90 deg","180 deg","270 deg"}; // CCW
  if ((rot < ROT_0) || (rot > ROT_270)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "(bad rotation: %d)",rot);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "%s",rstr[rot]);
  }
}
// }}}

Rotation operator+(Rotation lhs,Rotation rhs) // {{{
{
  return (Rotation)(((int)lhs+(int)rhs)%4);
}
// }}}

Rotation operator-(Rotation lhs,Rotation rhs) // {{{
{
  return (Rotation)((((int)lhs-(int)rhs)%4+4)%4);
}
// }}}

Rotation operator-(Rotation rhs) // {{{
{
  return (Rotation)((4-(int)rhs)%4);
}
// }}}

void BorderType_dump(BorderType border,pdftopdf_doc_t *doc) // {{{
{
  if ((border < NONE) || (border == 1) || (border > TWO_THICK)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "(bad border: %d)",border);
  } else {
    static const char *bstr[6]={"None",NULL,"one thin","one thick","two thin","two thick"};
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
      "%s",bstr[border]);
  }
}
// }}}

void PageRect::rotate_move(Rotation r,float pwidth,float pheight) // {{{
{
#if 1
  if (r>=ROT_180) {
    std::swap(top,bottom);
    std::swap(left,right);
  }
  if ((r == ROT_90) || (r == ROT_270)) {
    const float tmp=bottom;
    bottom=left;
    left=top;
    top=right;
    right=tmp;

    std::swap(width,height);
    std::swap(pwidth,pheight);
  }
  if ((r == ROT_90) || (r == ROT_180)) {
    left=pwidth-left;
    right=pwidth-right;
  }
  if ((r == ROT_270) || (r == ROT_180)) {
    top=pheight-top;
    bottom=pheight-bottom;
  }
#else
  switch (r) {
  case ROT_0: // no-op
    break;
  case ROT_90:
    const float tmp0=bottom;
    bottom=left;
    left=pheight-top;
    top=right;
    right=pheight-tmp0;

    std::swap(width,height);
    break;
  case ROT_180:
    const float tmp1=left;
    left=pwidth-right;
    right=pwidth-tmp1;

    const float tmp2=top;
    top=pheight-bottom;
    bottom=pheight-tmp2;
    break;
  case ROT_270:
    const float tmp3=top;
    top=pwidth-left;
    left=bottom;
    bottom=pwidth-right;
    right=tmp3;

    std::swap(width,height);
    break;
  }
#endif
}
// }}}

void PageRect::scale(float mult) // {{{
{
  if (mult==1.0) {
    return;
  }
  assert(mult!=0.0);

  bottom*=mult;
  left*=mult;
  top*=mult;
  right*=mult;

  width*=mult;
  height*=mult;
}
// }}}

void PageRect::translate(float tx,float ty) // {{{
{
  left+=tx;
  bottom+=ty;
  right+=tx;
  top+=ty;
}
// }}}

void PageRect::set(const PageRect &rhs) // {{{
{
  if (!std::isnan(rhs.top)) top=rhs.top;
  if (!std::isnan(rhs.left)) left=rhs.left;
  if (!std::isnan(rhs.right)) right=rhs.right;
  if (!std::isnan(rhs.bottom)) bottom=rhs.bottom;
}
// }}}

void PageRect::dump(pdftopdf_doc_t *doc) const // {{{
{
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_DEBUG,
      "pdftopdf: top: %f, left: %f, right: %f, bottom: %f\n"
	  "width: %f, height: %f\n",
	  top,left,right,bottom,
	  width,height);
}
// }}}

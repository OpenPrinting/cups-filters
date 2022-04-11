#include "pptypes-private.h"
#include <utility>
#include <stdio.h>
#include <assert.h>

void _cfPDFToPDFPositionDump(pdftopdf_position_e pos,pdftopdf_doc_t *doc) // {{{
{
  static const char *pstr[3]={"Left/Bottom","Center","Right/Top"};
  if ((pos < LEFT) || (pos > RIGHT)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: (bad position: %d)",pos);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: %s",pstr[pos+1]);
  }
}
// }}}

void _cfPDFToPDFPositionDump(pdftopdf_position_e pos,pdftopdf_axis_e axis,pdftopdf_doc_t *doc) // {{{
{
  assert((axis == pdftopdf_axis_e::X) || (axis == pdftopdf_axis_e::Y));
  if ((pos < LEFT) || (pos > RIGHT)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Position %s: (bad position: %d)",
      (axis == pdftopdf_axis_e::X) ? "X" : "Y", pos);
    return;
  }
  if (axis==pdftopdf_axis_e::X) {
    static const char *pxstr[3]={"Left","Center","Right"};
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Position X: %s", pxstr[pos+1]);
  } else {
    static const char *pystr[3]={"Bottom","Center","Top"};
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Position Y: %s",pystr[pos+1]);
  }
}
// }}}

void _cfPDFToPDFRotationDump(pdftopdf_rotation_e rot,pdftopdf_doc_t *doc) // {{{
{
  static const char *rstr[4]={"0 deg","90 deg","180 deg","270 deg"}; // CCW
  if ((rot < ROT_0) || (rot > ROT_270)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Rotation(CCW): (bad rotation: %d)",rot);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Rotation(CCW): %s",rstr[rot]);
  }
}
// }}}

pdftopdf_rotation_e operator+(pdftopdf_rotation_e lhs,pdftopdf_rotation_e rhs) // {{{
{
  return (pdftopdf_rotation_e)(((int)lhs+(int)rhs)%4);
}
// }}}

pdftopdf_rotation_e operator-(pdftopdf_rotation_e lhs,pdftopdf_rotation_e rhs) // {{{
{
  return (pdftopdf_rotation_e)((((int)lhs-(int)rhs)%4+4)%4);
}
// }}}

pdftopdf_rotation_e operator-(pdftopdf_rotation_e rhs) // {{{
{
  return (pdftopdf_rotation_e)((4-(int)rhs)%4);
}
// }}}

void _cfPDFToPDFBorderTypeDump(pdftopdf_border_type_e border,pdftopdf_doc_t *doc) // {{{
{
  if ((border < NONE) || (border == 1) || (border > TWO_THICK)) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Border: (bad border: %d)",border);
  } else {
    static const char *bstr[6]={"None",NULL,"one thin","one thick","two thin","two thick"};
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: Border: %s",bstr[border]);
  }
}
// }}}

void _cfPDFToPDFPageRect::rotate_move(pdftopdf_rotation_e r,float pwidth,float pheight) // {{{
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

void _cfPDFToPDFPageRect::scale(float mult) // {{{
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

void _cfPDFToPDFPageRect::translate(float tx,float ty) // {{{
{
  left+=tx;
  bottom+=ty;
  right+=tx;
  top+=ty;
}
// }}}

void _cfPDFToPDFPageRect::set(const _cfPDFToPDFPageRect &rhs) // {{{
{
  if (!std::isnan(rhs.top)) top=rhs.top;
  if (!std::isnan(rhs.left)) left=rhs.left;
  if (!std::isnan(rhs.right)) right=rhs.right;
  if (!std::isnan(rhs.bottom)) bottom=rhs.bottom;
}
// }}}

void _cfPDFToPDFPageRect::dump(pdftopdf_doc_t *doc) const // {{{
{
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
      "cfFilterPDFToPDF: top: %f, left: %f, right: %f, bottom: %f, "
	  "width: %f, height: %f",
	  top,left,right,bottom,
	  width,height);
}
// }}}

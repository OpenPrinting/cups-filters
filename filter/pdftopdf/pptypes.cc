#include "pptypes.h"
#include <utility>
#include <stdio.h>
#include <assert.h>

void Position_dump(Position pos) // {{{
{
  static const char *pstr[3]={"Left/Bottom","Center","Right/Top"};
  if ( (pos<LEFT)||(pos>RIGHT) ) {
    fprintf(stderr,"(bad position: %d)",pos);
  } else {
    fputs(pstr[pos+1],stderr);
  }
}
// }}}

void Position_dump(Position pos,Axis axis) // {{{
{
  assert( (axis==Axis::X)||(axis==Axis::Y) );
  if ( (pos<LEFT)||(pos>RIGHT) ) {
    fprintf(stderr,"(bad position: %d)",pos);
    return;
  }
  if (axis==Axis::X) {
    static const char *pxstr[3]={"Left","Center","Right"};
    fputs(pxstr[pos+1],stderr);
  } else {
    static const char *pystr[3]={"Bottom","Center","Top"};
    fputs(pystr[pos+1],stderr);
  }
}
// }}}


void Rotation_dump(Rotation rot) // {{{
{
  static const char *rstr[4]={"0 deg","90 deg","180 deg","270 deg"}; // CCW
  if ( (rot<ROT_0)||(rot>ROT_270) ) {
    fprintf(stderr,"(bad rotation: %d)",rot);
  } else {
    fputs(rstr[rot],stderr);
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


void BorderType_dump(BorderType border) // {{{
{
  if ( (border<NONE)||(border==1)||(border>TWO_THICK) ) {
    fprintf(stderr,"(bad border: %d)",border);
  } else {
    static const char *bstr[6]={"None",NULL,"one thin","one thick","two thin","two thick"};
    fputs(bstr[border],stderr);
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
  if ( (r==ROT_90)||(r==ROT_270) ) {
    const float tmp=bottom;
    bottom=left;
    left=top;
    top=right;
    right=tmp;

    std::swap(width,height);
    std::swap(pwidth,pheight);
  }
  if ( (r==ROT_90)||(r==ROT_180) ) {
    left=pwidth-left;
    right=pwidth-right;
  }
  if ( (r==ROT_270)||(r==ROT_180) ) {
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

void PageRect::dump() const // {{{
{
  fprintf(stderr,"top: %f, left: %f, right: %f, bottom: %f\n"
                 "width: %f, height: %f\n",
                 top,left,right,bottom,
                 width,height);
}
// }}}


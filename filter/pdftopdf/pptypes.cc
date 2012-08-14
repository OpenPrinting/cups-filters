#include "pptypes.h"
#include <utility>
#include <stdio.h>
#include <assert.h>

void Position_dump(Position pos) // {{{
{
  static const char *pstr[3]={"Left/Bottom","Center","Right/Top"};
  if ( (pos<LEFT)||(pos>RIGHT) ) {
    printf("(bad position: %d)",pos);
  } else {
    fputs(pstr[pos+1],stdout);
  }
}
// }}}

void Position_dump(Position pos,Axis axis) // {{{
{
  assert( (axis==Axis::X)||(axis==Axis::Y) );
  if ( (pos<LEFT)||(pos>RIGHT) ) {
    printf("(bad position: %d)",pos);
    return;
  }
  if (axis==Axis::X) {
    static const char *pxstr[3]={"Left","Center","Right"};
    fputs(pxstr[pos+1],stdout);
  } else {
    static const char *pystr[3]={"Bottom","Center","Top"};
    fputs(pystr[pos+1],stdout);
  }
}
// }}}


void Rotation_dump(Rotation rot) // {{{
{
  static const char *rstr[4]={"0 deg","90 deg","180 deg","270 deg"}; // CCW
  if ( (rot<ROT_0)||(rot>ROT_270) ) {
    printf("(bad rotation: %d)",rot);
  } else {
    fputs(rstr[rot],stdout);
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
    printf("(bad border: %d)",border);
  } else {
    static const char *bstr[6]={"None",NULL,"one thin","one thick","two thin","two thick"};
    fputs(bstr[border],stdout);
  }
}
// }}}


void PageRect::rotate(Rotation r) // {{{
{
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
  }
}
// }}}

void PageRect::set(const PageRect &rhs) // {{{
{
  if (!isnan(rhs.top)) top=rhs.top;
  if (!isnan(rhs.left)) left=rhs.left;
  if (!isnan(rhs.right)) right=rhs.right;
  if (!isnan(rhs.bottom)) bottom=rhs.bottom;
}
// }}}

void PageRect::dump() const // {{{
{
  printf("top: %f, left: %f, right: %f, bottom: %f\n"
         "width: %f, height: %f\n",
         top,left,right,bottom,
         width,height);
}
// }}}


#ifndef PPTYPES_H_
#define PPTYPES_H_

#include <cmath> // NAN

// namespace PPTypes {}   TODO?

enum Axis { X, Y };
enum Position { CENTER=0, LEFT=-1, RIGHT=1, TOP=1, BOTTOM=-1 }; // PS order
void Position_dump(Position pos);
void Position_dump(Position pos,Axis axis);

enum Rotation { ROT_0, ROT_90, ROT_180, ROT_270 };  // CCW
void Rotation_dump(Rotation rot);
Rotation operator+(Rotation lhs,Rotation rhs);
Rotation operator-(Rotation lhs,Rotation rhs);
Rotation operator-(Rotation rhs);
//Rotation operator+=(Rotation &lhs,Rotation rhs);

enum BorderType { NONE=0, ONE_THIN=2, ONE_THICK=3, TWO_THIN=4, TWO_THICK=5,
                  ONE=0x02, TWO=0x04, THICK=0x01};
void BorderType_dump(BorderType border);

struct PageRect {
PageRect() : top(NAN),left(NAN),right(NAN),bottom(NAN),width(NAN),height(NAN) {}
  float top,left,right,bottom; // i.e. margins
  float width,height;

  void rotate_move(Rotation r,float pwidth,float pheight); // pwidth original "page size" (i.e. before rotation)
  void scale(float mult);
  void translate(float tx,float ty);

  void set(const PageRect &rhs); // only for rhs.* != NAN
  void dump() const;
};

//  bool parseBorder(const char *val,BorderType &ret); // none,single,...,double-thick

#endif

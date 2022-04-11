#ifndef _CUPS_FILTERS_PDFTOPDF_PPTYPES_H_
#define _CUPS_FILTERS_PDFTOPDF_PPTYPES_H_

#include "pdftopdf-private.h"
#include <cmath> // NAN

// namespace PPTypes {}   TODO?

enum pdftopdf_axis_e { X, Y };
enum pdftopdf_position_e { CENTER=0, LEFT=-1, RIGHT=1, TOP=1, BOTTOM=-1 }; // PS order
void _cfPDFToPDFPositionDump(pdftopdf_position_e pos,pdftopdf_doc_t *doc);
void _cfPDFToPDFPositionDump(pdftopdf_position_e pos,pdftopdf_axis_e axis,pdftopdf_doc_t *doc);

enum pdftopdf_rotation_e { ROT_0, ROT_90, ROT_180, ROT_270 };  // CCW
void _cfPDFToPDFRotationDump(pdftopdf_rotation_e rot,pdftopdf_doc_t *doc);
pdftopdf_rotation_e operator+(pdftopdf_rotation_e lhs,pdftopdf_rotation_e rhs);
pdftopdf_rotation_e operator-(pdftopdf_rotation_e lhs,pdftopdf_rotation_e rhs);
pdftopdf_rotation_e operator-(pdftopdf_rotation_e rhs);
//pdftopdf_rotation_e operator+=(pdftopdf_rotation_e &lhs,pdftopdf_rotation_e rhs);

enum pdftopdf_border_type_e { NONE=0, ONE_THIN=2, ONE_THICK=3, TWO_THIN=4, TWO_THICK=5,
                  ONE=0x02, TWO=0x04, THICK=0x01};
void _cfPDFToPDFBorderTypeDump(pdftopdf_border_type_e border,pdftopdf_doc_t *doc);

struct _cfPDFToPDFPageRect {
_cfPDFToPDFPageRect() : top(NAN),left(NAN),right(NAN),bottom(NAN),width(NAN),height(NAN) {}
  float top,left,right,bottom; // i.e. margins
  float width,height;

  void rotate_move(pdftopdf_rotation_e r,float pwidth,float pheight); // pwidth original "page size" (i.e. before rotation)
  void scale(float mult);
  void translate(float tx,float ty);

  void set(const _cfPDFToPDFPageRect &rhs); // only for rhs.* != NAN
  void dump(pdftopdf_doc_t *doc) const;
};

//  bool _cfPDFToPDFParseBorder(const char *val,pdftopdf_border_type_e &ret); // none,single,...,double-thick

#endif

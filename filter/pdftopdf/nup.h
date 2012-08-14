#ifndef NUP_H_
#define NUP_H_

#include "pptypes.h"
#include <utility>

// you have to provide this
struct NupParameters {
  NupParameters() 
    : nupX(1),nupY(1),
      width(NAN),height(NAN),
      landscape(false),
      first(X),
      xstart(LEFT),ystart(TOP),
      xalign(CENTER),yalign(CENTER)
  {}

  // --- "calculated" parameters ---
  int nupX,nupY;
  float width,height;
  bool landscape; // post-rotate!

  // --- other settings ---
  // ordering
  Axis first;
  Position xstart,ystart;

  Position xalign,yalign;

  static bool possible(int nup); // TODO?  float in_ratio,float out_ratio
  static void preset(int nup,NupParameters &ret);
  static float calculate(int nup, float in_ratio, float out_ratio,NupParameters &ret); // returns "quality", 1 is best

  void dump() const;
};

// you get this
struct NupPageEdit {
  // required transformation: first translate, then scale
  float xpos,ypos;  // TODO:  already given by sub.left,sub.bottom    [but for rotation?]
  float scale; // uniform

// ? "landscape"  e.g. to rotate labels

  // for border, clip, ...
  // also stores in_width/in_height, unscaled!
  // everything in "outer"-page coordinates
  PageRect sub;

  void dump() const;
};

/*
 This class does the number-up calculation. Example:

  NupParameters param;
  param.xyz=...; // fill it with your data!

  NupState nup(param);
  NupPageEdit edit;
  for (auto page : your_pages) {
    bool newPage=nup.nextPage(page.w,page.h,edit); // w,h from input page
    // create newPage, if required; then place current page as specified in edit
  }
*/
class NupState {
public:
  NupState(const NupParameters &param);

  void reset();

  // will overwrite ret with the new parameters
  // returns true, if a new output page should be started first
  bool nextPage(float in_width,float in_height,NupPageEdit &ret);

private:
  std::pair<int,int> convert_order(int subpage) const;
  void calculate_edit(int subx,int suby,NupPageEdit &ret) const;
private:
  NupParameters param;

  int in_pages,out_pages;
  int nup; // max. per page (==nupX*nupY)
  int subpage; // on the current output-page
};

// TODO? elsewhere
// parsing functions for cups parameters (will not calculate nupX,nupY!)
bool parseNupLayout(const char *val,NupParameters &ret); // lrtb, btlr, ...

#endif

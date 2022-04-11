#ifndef _CUPS_FILTERS_PDFTOPDF_NUP_H_
#define _CUPS_FILTERS_PDFTOPDF_NUP_H_

#include "pptypes-private.h"
#include <utility>

// you have to provide this
struct _cfPDFToPDFNupParameters {
  _cfPDFToPDFNupParameters() 
    : nupX(1), nupY(1),
      width(NAN), height(NAN),
      landscape(false),
      first(X),
      xstart(LEFT), ystart(TOP),
      xalign(CENTER), yalign(CENTER)
  {}

  // --- "calculated" parameters ---
  int nupX, nupY;
  float width, height;
  bool landscape; // post-rotate!

  // --- other settings ---
  // ordering
  pdftopdf_axis_e first;
  pdftopdf_position_e xstart, ystart;

  pdftopdf_position_e xalign, yalign;

  static bool possible(int nup); // TODO?  float in_ratio, float out_ratio
  static void preset(int nup, _cfPDFToPDFNupParameters &ret);
  static float calculate(int nup, float in_ratio, float out_ratio,
			 _cfPDFToPDFNupParameters &ret); // returns "quality", 1 is best

  void dump(pdftopdf_doc_t *doc) const;
};

// you get this
struct _cfPDFToPDFNupPageEdit {
  // required transformation: first translate, then scale
  float xpos, ypos;  // TODO:  already given by sub.left, sub.bottom
                     // [but for rotation?]
  float scale;       // uniform

  // ? "landscape"  e.g. to rotate labels

  // for border, clip, ...
  // also stores in_width/in_height, unscaled!
  // everything in "outer"-page coordinates
  _cfPDFToPDFPageRect sub;

  void dump(pdftopdf_doc_t *doc) const;
};

/*
  This class does the number-up calculation. Example:

  _cfPDFToPDFNupParameters param;
  param.xyz = ...; // fill it with your data!

  _cfPDFToPDFNupState nup(param);
  _cfPDFToPDFNupPageEdit edit;
  for (auto page : your_pages)
  {
    bool newPage = nup.mext_page(page.w, page.h, edit); // w, h from input page
    // create newPage, if required; then place current page as specified in edit
  }
*/

class _cfPDFToPDFNupState {
public:
  _cfPDFToPDFNupState(const _cfPDFToPDFNupParameters &param);

  void reset();

  // will overwrite ret with the new parameters
  // returns true, if a new output page should be started first
  bool mext_page(float in_width, float in_height, _cfPDFToPDFNupPageEdit &ret);

private:
  std::pair<int, int> convert_order(int subpage) const;
  void calculate_edit(int subx, int suby, _cfPDFToPDFNupPageEdit &ret) const;

private:
  _cfPDFToPDFNupParameters param;

  int in_pages, out_pages;
  int nup; // max. per page (== nupX * nupY)
  int subpage; // on the current output-page
};

// TODO? elsewhere
// parsing functions for cups parameters (will not calculate nupX, nupY!)
bool _cfPDFToPDFParseNupLayout(const char *val, _cfPDFToPDFNupParameters &ret); // lrtb, btlr, ...

#endif

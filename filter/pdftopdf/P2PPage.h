/*

Copyright (c) 2006-2007, BBR Inc.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/
/*
 P2PPage.h
 pdftopdf page
*/
#ifndef _P2PPAGE_H_
#define _P2PPAGE_H_

#include "Object.h"
#include "Page.h"
#include "P2PObject.h"
#include "Dict.h"
#include "P2POutputStream.h"
#include "XRef.h"
#include "P2PMatrix.h"
#include "P2PResources.h"
#include "P2PFont.h"

class P2PPageTree;

class P2PPage : public P2PObject {
public:
  P2PPage(Page *orgPageA, XRef *xrefA);
  /* Constructor for a empty page */
  P2PPage(PDFRectangle *mediaBoxA, XRef *xrefA);
  /* Construct nup page */
  P2PPage(int n, P2PPage **pages, int len, PDFRectangle *box,
    unsigned int borderFlagA, unsigned int layout, XRef *xrefA,
    int xpos, int ypos);
  virtual ~P2PPage();
  /* output self and resources */
  void output(P2POutputStream *str, P2PPageTree *tree,
    P2PObject *copiedObj = 0);
  int getNumOrgPages() { return numOrgPages; }
  void fit(PDFRectangle *box, double zoom);
  void mirror();
  void rotate(int orientation);
  void position(PDFRectangle *box, int xpos, int ypos);
  void scale(double zoom);
  void autoRotate(PDFRectangle *box);
  void setMediaBox(PDFRectangle *mediaBoxA) { mediaBox = *mediaBoxA; }
  PDFRectangle *getMediaBox() { return &mediaBox; }
private:
  struct OrgPage {
    Page *page;
    P2PResourceMap *mappingTable; /* resource name mapping table */
    P2PMatrix mat; /* translation matrix */
    GBool trans; /* translation enable */
    PDFRectangle frame; /* page frame */
    unsigned int borderFlag; /* border flag */
    GBool clipFrame; /* clipping by frame */
    /* fit contents to box */
    void fit(PDFRectangle *box, PDFRectangle *oldBox, double zoom,
      PDFRectangle *rBox);
    /* rotation */
    void rotate(PDFRectangle *box, int orientation, PDFRectangle *rBox);
    void scale(PDFRectangle *box, double zoom, PDFRectangle *rBox);
    void position(PDFRectangle *box,
      PDFRectangle *oldBox, int xpos, int ypos);
    OrgPage();
    ~OrgPage();
  };
  OrgPage *orgPages;
  int numOrgPages;

  /* dummy object for contents*/
  /* only use this for cross reference etc. */
  P2PObject contents;

  PDFRectangle mediaBox;
  PDFRectangle cropBox;
  GBool haveCropBox;
  PDFRectangle bleedBox;
  PDFRectangle trimBox;
  PDFRectangle artBox;
  P2PResources *resources;
  XRef *xref;
  P2PFontResource *fontResource;

  void outputContents(P2POutputStream *str);
  void outputSelf(P2POutputStream *str, P2PPageTree *tree,
    P2PObject *copiedObj);
  static void outputPDFRectangle(PDFRectangle *rect, P2POutputStream *str);
  static void outputPDFRectangleForPath(PDFRectangle *rect,
      P2POutputStream *str);
  static void transPDFRectangle(PDFRectangle *rect, P2PMatrix *matA);
  static GBool eqPDFRectangle(PDFRectangle *rect1, PDFRectangle *rect2) {
    return rect1->x1 == rect2->x1 && rect1->y1 == rect2->y1
      && rect1->x2 == rect2->x2 && rect1->y2 == rect2->y2;
  }
  /* nup calculate layout */
  void nupCalcLayout(int n, unsigned int layout, PDFRectangle *box,
    PDFRectangle *rects);
};

#endif

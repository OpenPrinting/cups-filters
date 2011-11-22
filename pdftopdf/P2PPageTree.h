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
 P2PPageTree.h
 pdftopdf page tree
*/
#ifndef _P2PPAGETREE_H_
#define _P2PPAGETREE_H_

#include "goo/gmem.h"
#include "goo/gtypes.h"
#include "Object.h"
#include "Array.h"
#include "Catalog.h"
#include "P2PObject.h"
#include "P2PDoc.h"
#include "P2PPage.h"
#include "P2POutputStream.h"
#include "XRef.h"
#include "Page.h"

class P2PPageTree : public P2PObject {
public:
  P2PPageTree(Catalog *orgCatalogA, XRef *xrefA);
  virtual ~P2PPageTree();
  /* output self and all pages */
  virtual void output(P2POutputStream *str, int copies, GBool collate);
  int nup(int n, PDFRectangle *box, unsigned int borderFlag,
    unsigned int layout, int xpos, int ypos);
  void select(const char *pageSet, const char *pageRanges);
  void fit(PDFRectangle *box, double zoom);
  void mirror();
  void rotate(int orientation);
  void position(PDFRectangle *box, int xpos, int ypos);
  void scale(double zoom);
  void autoRotate(PDFRectangle *box);
  void setMediaBox(PDFRectangle *mediaBoxA);
  int getNumberOfPages() { return numPages; }
private:
  P2PPage **pages;
  int numPages;
  XRef *xref;

  void cleanPages(P2PPage **pagesA, int size);
  void outputSelf(P2POutputStream *str, P2PObject **pageObjects, int len);
  GBool checkPageRange(int no, const char *pageSet, const char *pageRange);
};

#endif

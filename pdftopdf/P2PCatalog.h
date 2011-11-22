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
 P2PCatalog.h
 pdftopdf document catalog
*/
#ifndef _P2PCATALOG_H_
#define _P2PCATALOG_H_

#include "goo/gtypes.h"
#include "goo/gmem.h"
#include "Object.h"
#include "Catalog.h"
#include "P2PObject.h"
#include "P2PPageTree.h"
#include "P2POutputStream.h"
#include "XRef.h"
#include "Page.h"
#include "P2PDoc.h"

class P2PCatalog: public P2PObject {
public:
  P2PCatalog(Catalog *orgCatalogA, XRef *xrefA);
  virtual ~P2PCatalog();
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
  int getNumberOfPages();
private:
  XRef *xref;
  Catalog *orgCatalog;
  P2PPageTree *pageTree;
  void outputSelf(P2POutputStream *str);
};

#endif

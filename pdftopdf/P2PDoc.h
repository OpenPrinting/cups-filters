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
 P2PDoc.h
 pdftopdf document
*/
#ifndef _P2PDOC_H_
#define _P2PDOC_H_

#include "PDFDoc.h"
#include "P2POutputStream.h"
#include "P2PPage.h"

#define PDFTOPDF_BORDERNONE	0 /* No border */
#define PDFTOPDF_BORDERHAIRLINE	1 /* hairline border */
#define PDFTOPDF_BORDERTHICK	2 /* thick border */
#define PDFTOPDF_BORDERDOUBLE	4 /* double line border */

#define PDFTOPDF_LAYOUT_LRBT	0 /* Left to right, bottom to top */
#define PDFTOPDF_LAYOUT_LRTB	1 /* Left to right, top to bottom */
#define PDFTOPDF_LAYOUT_RLBT	2 /* Right to left, bottom to top */
#define PDFTOPDF_LAYOUT_RLTB	3 /* Right to left, top to bottom */
#define PDFTOPDF_LAYOUT_BTLR	4 /* Bottom to top, left to right */
#define PDFTOPDF_LAYOUT_TBLR	5 /* Top to bottom, left to right */
#define PDFTOPDF_LAYOUT_BTRL	6 /* Bottom to top, right to left */
#define PDFTOPDF_LAYOUT_TBRL	7 /* Top to bottom, right to left */

#define PDFTOPDF_LAYOUT_NEGATEY	1 /* The bits for the layout */
#define PDFTOPDF_LAYOUT_NEGATEX	2 /* definitions above... */
#define PDFTOPDF_LAYOUT_VERTICAL 4

class P2PCatalog;

class P2PDoc {
public:
  struct Options {
    Options() {
      duplex = gFalse;
      copies = 1;
      collate = gFalse;
      user = 0;
      title = 0;
      jobId = 0;
      pageLabel = 0;
      pageRanges = 0;
      pageSet = 0;
      reverse = gFalse;
      even = gFalse;
      contentsCompress = gFalse;
      fontEmbedding = gFalse;
      fontEmbeddingWhole = gFalse;
      fontCompress = gFalse;
      numPreFonts = 0;
      preFonts = 0;
      fontEmbeddingPreLoad = gFalse;
    };
    ~Options() {}

    int orientation;
    GBool duplex;
    GBool collate;
    int copies;
    const char *user;
    const char *title;
    int jobId;
    const char *pageLabel;
    const char *pageRanges;
    const char *pageSet;
    GBool reverse;
    GBool even;
    GBool contentsCompress;
    GBool fontEmbedding;
    GBool fontEmbeddingWhole;
    GBool fontCompress;
    GBool fontEmbeddingPreLoad;
    GBool pdfPrinter;
    int numPreFonts;
    char **preFonts;
  };

  static Options options;

  P2PDoc(PDFDoc *orgDocA);

  virtual ~P2PDoc();
  void output(P2POutputStream *str, int deviceCopies = 0,
    bool deviceCollate = false);
  int nup(int n, PDFRectangle *box, unsigned int borderFlag,
    unsigned int layout, int xpos, int ypos);
  void select();
  void fit(PDFRectangle *box, double zoom);
  void mirror();
  void rotate(int orientation);
  void position(PDFRectangle *box, int xpos, int ypos);
  void scale(double zoom);
  void autoRotate(PDFRectangle *box);
  void setMediaBox(PDFRectangle *mediaBoxA);
  int getNumberOfPages();
private:
  PDFDoc *orgDoc;
  P2PCatalog *catalog;
};

#endif

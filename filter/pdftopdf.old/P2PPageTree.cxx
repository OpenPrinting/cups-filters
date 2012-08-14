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
 P2PPageTree.cc
 pdftopdf page tree
*/

#include <config.h>
#include "goo/gmem.h"
#include "P2PPageTree.h"
#include <ctype.h>
#include <stdlib.h>

GBool P2PPageTree::checkPageRange(int no, const char *pageSet,
  const char *pageRanges)
{
  const char *range;
  int lower, upper;

  if (pageSet != 0) {
    if (!strcasecmp(pageSet,"even") && (no % 2) == 1) {
      return gFalse;
    } else if (!strcasecmp(pageSet, "odd") && (no % 2) == 0) {
      return gFalse;
    }
  }

  if (pageRanges == 0) {
    return gTrue;
  }

  for (range = pageRanges; *range != '\0';) {
    if (*range == '-') {
      lower = 1;
      range ++;
      upper = strtol(range, (char **)&range, 10);
    } else {
      lower = strtol(range, (char **)&range, 10);
      if (*range == '-') {
        range ++;
        if (!isdigit(*range)) {
          upper = 65535;
        } else {
          upper = strtol(range, (char **)&range, 10);
        }
      } else {
        upper = lower;
      }
    }
    if (no >= lower && no <= upper) {
      return gTrue;
    }

    if (*range == ',') {
      range++;
    } else {
      break;
    }
  }
  return gFalse;
}

/* Constructor  */
P2PPageTree::P2PPageTree(Catalog *orgCatalogA, XRef *xrefA)
{
  int i;

  xref = xrefA;
  numPages = orgCatalogA->getNumPages();
  pages = new P2PPage *[numPages];
  for (i = 0;i < numPages;i++) {
    pages[i] = new P2PPage(orgCatalogA->getPage(i+1),xref);
  }
}

void P2PPageTree::cleanPages(P2PPage **pagesA, int size)
{
  int i;

  for (i = 0;i < size;i++) {
    if (pagesA[i] != 0) {
      delete pagesA[i];
    }
  }
  delete[] pagesA;
}

P2PPageTree::~P2PPageTree()
{
  cleanPages(pages,numPages);
}

void P2PPageTree::outputSelf(P2POutputStream *str, P2PObject **pageObjects,
  int len)
{
  int i;

  outputBegin(str);
  str->puts("<< /Type /Pages "
            "/Kids [ ");
  for (i = 0;i < len;i++) {
    P2PXRef::put(pageObjects[i]);
    pageObjects[i]->outputRef(str);
    str->putchar('\n');
  }
  str->printf(" ] /Count %d >>\n",len);
  outputEnd(str);
}

void P2PPageTree::output(P2POutputStream *str, int copies, GBool collate)
{
  int i,j,k;
  int n = numPages;
  int len;
  P2PObject **pageObjects;
  P2PPage **outPages;

  if (P2PDoc::options.even && (numPages % 2) != 0) {
    /* make number of pages even.
      append empty page to the last */
    /* only increment allocate size here */
    n++;
  }
  outPages = new P2PPage *[n];
  for (i = 0;i < numPages;i++) {
    outPages[i] = pages[i];
  }
  if (P2PDoc::options.even && (numPages % 2) != 0) {
    /* make number of pages even.
      append a empty page to the last. */
    /* assumed n >= 2 */
    /* empty page's mediaSize is same as a preceding page's media Size */
    outPages[n-1] = new P2PPage(outPages[n-2]->getMediaBox(),xref);
  }
  if (P2PDoc::options.reverse) {
    /* reverse output pages */
    for (i = 0;i < (n+1)/2;i++) {
      P2PPage *t;

      t = outPages[i];
      outPages[i] = outPages[n-i-1];
      outPages[n-i-1] = t;
    }
  }

  len = n*copies;
  pageObjects = new P2PObject * [len];

  if (collate) {
    for (i = 0;i < n;i++) {
      pageObjects[i] = static_cast<P2PObject *>(outPages[i]);
    }
    for (i = n;i < len;i++) {
      pageObjects[i] = new P2PObject();
    }
    outputSelf(str,pageObjects,len);
    for (i = 0;i < n;i++) {
      outPages[i]->output(str,this);
      if (P2PDoc::options.pdfPrinter)
	fprintf(stderr, "PAGE: %d %d\n", i+1, 1);
    }
    for (i = 1;i < copies;i++) {
      k = i*n;
      for (j = 0;j < n;j++) {
	outPages[j]->output(str,this,pageObjects[k+j]);
	if (P2PDoc::options.pdfPrinter)
	  fprintf(stderr, "PAGE: %d %d\n", j+1, 1);
      }
    }
  } else {
    for (i = 0;i < n;i++) {
      k = i*copies;
      pageObjects[k] = static_cast<P2PObject *>(outPages[i]);
      for (j = 1;j < copies;j++) {
	pageObjects[k+j] = new P2PObject();
      }
    }
    outputSelf(str,pageObjects,len);
    for (i = 0;i < n;i++) {
      k = i*copies;
      outPages[i]->output(str,this);
      if (P2PDoc::options.pdfPrinter)
	fprintf(stderr, "PAGE: %d %d\n", i+1, 1);
      for (j = 1;j < copies;j++) {
	outPages[i]->output(str,this,pageObjects[k+j]);
	if (P2PDoc::options.pdfPrinter)
	  fprintf(stderr, "PAGE: %d %d\n", i+1, 1);
      }
    }
  }
  delete[] pageObjects;
  delete[] outPages;
}

int P2PPageTree::nup(int n, PDFRectangle *box,
      unsigned int borderFlag, unsigned int layout,
      int xpos, int ypos)
{
  P2PPage **newPages;
  int size;
  int i,j;

  size = (numPages+(n-1))/n;
  newPages = new P2PPage *[size];
  for (i = 0,j = 0;i < numPages;j++, i += n) {
    int len = numPages - i;
    int k;

    if (len > n) {
      len = n;
    }
    /* check original pages */
    for (k = 0;k < len;k++) {
      if (pages[i+k]->getNumOrgPages() != 1) {
	/* nup twice error */
	return -1;
      }
    }
    newPages[j] = new P2PPage(n,pages+i,len,box,borderFlag,layout,
      xref,xpos,ypos);
  }
  cleanPages(pages,numPages);
  pages = newPages;
  numPages = size;
  return 0;
}

void P2PPageTree::select(const char *pageSet, const char *pageRanges)
{
  P2PPage **newPages;
  int i,j;

  newPages = new P2PPage *[numPages];
  for (i = 0,j = 0;i < numPages;i++) {
    if (checkPageRange(i+1, pageSet, pageRanges)) {
      newPages[j ++] = pages[i];
    } else {
      delete pages[i];
    }
  }
  delete pages;
  pages = newPages;
  numPages = j;
}

void P2PPageTree::fit(PDFRectangle *box, double zoom)
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->fit(box,zoom);
  }
}

void P2PPageTree::mirror()
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->mirror();
  }
}

void P2PPageTree::rotate(int orientation)
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->rotate(orientation);
  }
}

void P2PPageTree::position(PDFRectangle *box, int xpos, int ypos)
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->position(box,xpos,ypos);
  }
}

void P2PPageTree::scale(double zoom)
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->scale(zoom);
  }
}

void P2PPageTree::autoRotate(PDFRectangle *box)
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->autoRotate(box);
  }
}

void P2PPageTree::setMediaBox(PDFRectangle *mediaBoxA)
{
  int i;

  for (i = 0;i < numPages;i++) {
    pages[i]->setMediaBox(mediaBoxA);
  }
}

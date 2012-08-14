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
 P2PCatalog.cc
 pdftopdf document catalog
*/

#include <config.h>
#include "goo/gmem.h"
#include "P2PCatalog.h"
#include "P2PXRef.h"


/* Constructor */
P2PCatalog::P2PCatalog(Catalog *orgCatalogA, XRef *xrefA)
{
  orgCatalog = orgCatalogA;
  pageTree = new P2PPageTree(orgCatalog,xrefA);
  xref = xrefA;
}

P2PCatalog::~P2PCatalog()
{
  delete pageTree;
}

void P2PCatalog::outputSelf(P2POutputStream *str)
{
  outputBegin(str);
  str->puts("<< /Type /Catalog /Pages ");
  P2PXRef::put(pageTree);
  pageTree->outputRef(str);
  str->puts(" >>\n");
  outputEnd(str);
}

void P2PCatalog::output(P2POutputStream *str, int copies, GBool collate)
{
  outputSelf(str);
  pageTree->output(str,copies,collate);
}

int P2PCatalog::nup(int n, PDFRectangle *box, unsigned int borderFlag,
  unsigned int layout, int xpos, int ypos)
{
  return pageTree->nup(n,box,borderFlag,layout,xpos,ypos);
}

void P2PCatalog::select(const char *pageSet, const char *pageRanges)
{
  pageTree->select(pageSet,pageRanges);
}

void P2PCatalog::fit(PDFRectangle *box, double zoom)
{
  pageTree->fit(box,zoom);
}

void P2PCatalog::mirror()
{
  pageTree->mirror();
}

void P2PCatalog::rotate(int orientation)
{
  pageTree->rotate(orientation);
}

void P2PCatalog::position(PDFRectangle *box, int xpos, int ypos)
{
  pageTree->position(box,xpos,ypos);
}

void P2PCatalog::scale(double zoom)
{
  pageTree->scale(zoom);
}

void P2PCatalog::autoRotate(PDFRectangle *box)
{
  pageTree->autoRotate(box);
}

void P2PCatalog::setMediaBox(PDFRectangle *mediaBoxA)
{
  pageTree->setMediaBox(mediaBoxA);
}

int P2PCatalog::getNumberOfPages()
{
  return pageTree->getNumberOfPages();
}

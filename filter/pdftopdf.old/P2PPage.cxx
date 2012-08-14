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
 P2PPage.cc
 pdftopdf page
*/

#include <config.h>
#include "goo/gmem.h"
#include "P2PPage.h"
#include "P2PPageTree.h"
#include "P2POutput.h"
#include "P2PGfx.h"
#include "P2PDoc.h"
#include "P2PFont.h"

#define NUP_MARGIN 3
#define NUP_PAGE_MARGIN 4


/* Constructor */
P2PPage::P2PPage(Page *orgPageA, XRef *xrefA)
{
  int rotateTag;

  numOrgPages = 1;
  orgPages = new OrgPage [1];
  orgPages[0].page = orgPageA;
  fontResource = 0;

  xref = xrefA;
  mediaBox = *orgPageA->getMediaBox();
  cropBox = *orgPageA->getCropBox();
  haveCropBox = orgPageA->isCropped();
  bleedBox = *orgPageA->getBleedBox();
  trimBox = *orgPageA->getTrimBox();
  artBox = *orgPageA->getArtBox();

  /* rotate tag */
  if ((rotateTag = orgPageA->getRotate()) != 0) {
    /* Note: rotate tag of PDF is clockwise and 
     *       orientation-requested attrobute of IPP is anti-clockwise
     */
    int orientation = 0;
    switch (rotateTag) {
    case 90:
    case -270:
	orientation = 3;
	break;
    case 180:
    case -180:
	orientation = 2;
	break;
    case 270:
    case -90:
	orientation = 1;
	break;
    case 0:
    default:
	break;
    }
    rotate(orientation);
  }

  /* when 0, use original resource dictionary of orgPages[0] */
  resources = 0;
}

/* Constructor for a empty page */
P2PPage::P2PPage(PDFRectangle *mediaBoxA, XRef *xrefA)
{
  numOrgPages = 1;
  orgPages = new OrgPage [1];
  orgPages[0].page = 0;
  fontResource = 0;

  xref = xrefA;
  if (mediaBoxA != 0) {
    mediaBox = *mediaBoxA;
  } else {
    mediaBox.x2 = mediaBox.y2 = 1;
  }
  haveCropBox = gFalse;
  bleedBox = mediaBox;
  trimBox = mediaBox;
  artBox = mediaBox;

  /* when 0, use original resource dictionary of orgPages[0] */
  resources = 0;
}

void P2PPage::nupCalcLayout(int n, unsigned int layout, PDFRectangle *box,
  PDFRectangle *rects)
{
  double aw = box->x2 - box->x1;
  double ah = box->y2 - box->y1;
  int nx, ny;
  int *nxp, *nyp;
  double advx, advy, sx, sy, x, y;
  double w,h;
  int i,ix,iy;

  if (aw > ah){
    /* landscape */
    nxp = &ny;
    nyp = &nx;
  } else {
    /* portlate */
    nxp = &nx;
    nyp = &ny;
  }
  /* should be h >= w here */
  switch (n) {
  case 2:
    *nxp = 1;
    *nyp = 2;
    break;
  case 4:
    *nxp = 2;
    *nyp = 2;
    break;
  case 6:
    *nxp = 2;
    *nyp = 3;
    break;
  case 8:
    *nxp = 2;
    *nyp = 4;
    break;
  case 9:
    *nxp = 3;
    *nyp = 3;
    break;
  default:
  case 16:
    *nxp = 4;
    *nyp = 4;
    break;
  }
  w = (box->x2 - box->x1)/nx;
  h = (box->y2 - box->y1)/ny;
  if ((layout & PDFTOPDF_LAYOUT_NEGATEX) != 0) {
    advx = -w;
    sx = box->x2 + advx;
  } else {
    advx = w;
    sx = box->x1;
  }
  if ((layout & PDFTOPDF_LAYOUT_NEGATEY) != 0) {
    advy = -h;
    sy = box->y2 + advy;
  } else {
    advy = h;
    sy = box->y1;
  }
  if ((layout & PDFTOPDF_LAYOUT_VERTICAL) != 0) {
    i = 0;
    for (x = sx, ix = 0;ix < nx;ix++) {
      for (y = sy, iy = 0;iy < ny;iy++) {
	rects[i].x1 = x+NUP_MARGIN;
	rects[i].x2 = x+w-NUP_MARGIN;
	rects[i].y1 = y+NUP_MARGIN;
	rects[i].y2 = y+h-NUP_MARGIN;
	i++;
	y += advy;
      }
      x += advx;
    }
  } else {
    i = 0;
    for (y = sy, iy = 0;iy < ny;iy++) {
      for (x = sx, ix = 0;ix < nx;ix++) {
	rects[i].x1 = x+NUP_MARGIN;
	rects[i].x2 = x+w-NUP_MARGIN;
	rects[i].y1 = y+NUP_MARGIN;
	rects[i].y2 = y+h-NUP_MARGIN;
	i++;
	x += advx;
      }
      y += advy;
    }
  }
}

/* Construct nup page */
P2PPage::P2PPage(int n, P2PPage **pages, int len, PDFRectangle *box,
      unsigned int borderFlagA,
      unsigned int layout, XRef *xrefA, int xpos, int ypos)
{
  int i;
  PDFRectangle rects[16];

  xref = xrefA;
  numOrgPages = len;
  orgPages = new OrgPage [len];
  mediaBox = *box;
  cropBox = mediaBox;
  haveCropBox = gFalse;
  bleedBox = cropBox;
  trimBox = cropBox;
  artBox = cropBox;
  fontResource = 0;

  if (n == 8 || n == 6 || n == 2) {
    /* Rotating is reqiured, layout change is needed.
       rotating is done in fit method, only change layout here */
    unsigned int t;

    t = layout ^ (PDFTOPDF_LAYOUT_NEGATEX | PDFTOPDF_LAYOUT_VERTICAL);
    /* swap x and y */
    layout = ((t << 1) & 2) | ((t >> 1) & 1) | (t & 4);
  }
  nupCalcLayout(n,layout,&mediaBox,rects);
  resources = new P2PResources(xref);
  for (i = 0;i < len;i++) {
    PDFRectangle rBox;

    orgPages[i] = pages[i]->orgPages[0]; /* copy */
    orgPages[i].frame = rects[i];
    orgPages[i].borderFlag = borderFlagA;
    /* fixup rect to fit inner rectangle */
    rects[i].x1 += NUP_PAGE_MARGIN;
    rects[i].y1 += NUP_PAGE_MARGIN;
    rects[i].x2 -= NUP_PAGE_MARGIN;
    rects[i].y2 -= NUP_PAGE_MARGIN;

    /* do fitting page contents */
    orgPages[i].fit(&rects[i],&(pages[i]->mediaBox),1.0,&rBox);
    orgPages[i].position(&rects[i],&rBox,xpos,ypos);

    /* merge resources */
    if (pages[i]->resources != 0) {
      orgPages[i].mappingTable 
        = resources->merge(pages[i]->resources);
    } else if (orgPages[i].page != 0) {
      orgPages[i].mappingTable 
        = resources->merge(orgPages[i].page->getResourceDict());
    }
  }
}

P2PPage::~P2PPage()
{
  if (fontResource != 0) {
    delete fontResource;
    fontResource = 0;
  }
  if (resources != 0) {
    delete resources;
    resources = 0;
  }
  if (orgPages != 0) {
    delete[] orgPages;
  }
}

void P2PPage::outputPDFRectangle(PDFRectangle *rect, P2POutputStream *str)
{
  str->printf("[ %f %f %f %f ]",rect->x1,rect->y1,rect->x2,rect->y2);
}

void P2PPage::outputPDFRectangleForPath(PDFRectangle *rect, P2POutputStream *str)
{
  double x,y,w,h;

  if (rect->x1 > rect->x2) {
    x = rect->x2;
    w = rect->x1 - rect->x2;
  } else {
    x = rect->x1;
    w = rect->x2 - rect->x1;
  }
  if (rect->y1 > rect->y2) {
    y = rect->y2;
    h = rect->y1 - rect->y2;
  } else {
    y = rect->y1;
    h = rect->y2 - rect->y1;
  }
  str->printf("%f %f %f %f",x,y,w,h);
}

void P2PPage::transPDFRectangle(PDFRectangle *rect, P2PMatrix *matA)
{
  matA->apply(rect->x1, rect->y1, &(rect->x1), &(rect->y1));
  matA->apply(rect->x2, rect->y2, &(rect->x2), &(rect->y2));
}

void P2PPage::outputSelf(P2POutputStream *str, P2PPageTree *tree,
  P2PObject *copiedObj)
{
  if (copiedObj == 0) {
    outputBegin(str);
  } else {
    copiedObj->outputBegin(str);
  }
  str->puts("<< /Type /Page /Parent ");
  tree->outputRef(str);
  str->puts("\n/MediaBox ");
  outputPDFRectangle(&mediaBox,str);
  if (haveCropBox && !eqPDFRectangle(&mediaBox,&cropBox)) {
    str->puts("\n/CropBox ");
    outputPDFRectangle(&cropBox,str);
  }
  if (!eqPDFRectangle(&cropBox,&bleedBox)) {
    str->puts("\n/BleedBox ");
    outputPDFRectangle(&bleedBox,str);
  }
  if (eqPDFRectangle(&trimBox,&artBox)) {
    str->puts("\n/TrimBox ");
    outputPDFRectangle(&trimBox,str);
  } else {
    str->puts("\n/ArtBox ");
    outputPDFRectangle(&artBox,str);
  }

  if (numOrgPages > 1 || orgPages[0].page != 0) {
    /* not empty page */
    P2PXRef::put(&contents);
    str->puts("\n/Contents ");
    contents.outputRef(str);

    str->puts("\n/Resources ");
    if (resources != 0) {
      resources->output(str);
    } else {
      Dict *dict = orgPages[0].page->getResourceDict();

      if (dict != 0) {
	if (fontResource != 0 && fontResource->getNDicts() > 0) {
	  /* replace font resource */
	  const char *p = "Font";
	  P2PObject *objp = fontResource;

	  P2POutput::outputDict(dict,&p,&objp,1,str,xref);
	} else {
	  P2POutput::outputDict(dict,str,xref);
	}
      } else {
	str->puts("<< >>");
      }
    }
  } else {
    /* empty page */
    str->puts("\n/Resources << >>");
  }
  str->puts(" >>\n");
  if (copiedObj == 0) {
    outputEnd(str);
  } else {
    copiedObj->outputEnd(str);
  }
}

void P2PPage::outputContents(P2POutputStream *str)
{
  int i;
  int start;
  int len;
  Object lenobj;

  /* if empty, do nothing */
  if (orgPages[0].page == 0) return;

  P2PGfx output(xref,str,fontResource,resources);
  P2PObj *pobj = new P2PObj();

  contents.outputBegin(str);
  str->puts("<< /Length ");
  P2PXRef::put(pobj);
  pobj->outputRef(str);
  if (P2PDoc::options.contentsCompress && str->canDeflate()) {
    str->puts(" /Filter /FlateDecode ");
  }
  str->puts(" >>\n");
  str->puts("stream\n");
  start = str->getPosition();
  if (P2PDoc::options.contentsCompress) str->startDeflate();
  for (i = 0;i < numOrgPages;i++) {
    Object contentsObj;
    GBool save;

    save = orgPages[i].clipFrame || orgPages[i].trans;
    if (save) {
      str->puts("q ");
    }
    if (orgPages[i].page->getContents(&contentsObj) != 0
        && !contentsObj.isNull()) {
      if (orgPages[i].clipFrame) {
	outputPDFRectangleForPath(&orgPages[i].frame,str);
	str->puts(" re W n ");
      }
      if (orgPages[i].borderFlag != 0) {
	PDFRectangle inner = *(orgPages[i].page->getMediaBox());
	PDFRectangle ident(0,0,1,1);
	double xs,ys;

	transPDFRectangle(&ident,&(orgPages[i].mat));
	xs = ident.x2-ident.x1 > 0 ? 1: -1;
	ys = ident.y2-ident.y1 > 0 ? 1: -1;

	str->puts("q ");
	if ((orgPages[i].borderFlag & PDFTOPDF_BORDERHAIRLINE) != 0) {
	  str->puts("0 w ");
	}
	if ((orgPages[i].borderFlag & PDFTOPDF_BORDERDOUBLE) != 0) {
	  PDFRectangle outer = *(orgPages[i].page->getMediaBox());

	  transPDFRectangle(&outer,&(orgPages[i].mat));
	  outer.x1 -= 3*xs;
	  outer.y1 -= 3*ys;
	  outer.x2 += 3*xs;
	  outer.y2 += 3*ys;
	  outputPDFRectangleForPath(&outer,str);
	  str->puts(" re S ");
	}

	transPDFRectangle(&inner,&(orgPages[i].mat));
	inner.x1 -= 1*xs;
	inner.y1 -= 1*ys;
	inner.x2 += 1*xs;
	inner.y2 += 1*ys;
	outputPDFRectangleForPath(&inner,str);
	str->puts(" re S Q ");
      }

      if (orgPages[i].trans) {
	orgPages[i].mat.output(str);
	str->puts(" cm ");
      }

      output.outputContents(&contentsObj,orgPages[i].mappingTable,
        orgPages[i].page->getResourceDict(),&(orgPages[i].mat));
      contentsObj.free();
    }
    if (save) {
      str->puts(" Q\n");
    }
  }
  if (P2PDoc::options.contentsCompress) str->endDeflate();
  len = str->getPosition()-start;
  str->puts("\nendstream\n");
  contents.outputEnd(str);
  /* set length object value */
  lenobj.initInt(len);
  pobj->setObj(&lenobj);
  lenobj.free();
  pobj->output(str,xref);
}

void P2PPage::output(P2POutputStream *str, P2PPageTree *tree, P2PObject *copiedObj)
{
  if (fontResource != 0) {
    delete fontResource;
    fontResource = 0;
  }
  if (resources == 0 && orgPages[0].page != 0/* not empty page */) {
    /* make P2PResource for pattern handling */
    /* when number-upped, page must have P2PResource already. 
       so, we handling one page case */
    resources = new P2PResources(xref);
    orgPages[0].mappingTable 
	= resources->merge(orgPages[0].page->getResourceDict());
  }
  if (copiedObj == 0) {
    if (P2PDoc::options.fontEmbedding && orgPages[0].page != 0) {
      /* only not empty page */
      fontResource = new P2PFontResource();
      if (resources != 0) {
	fontResource->setup(resources,xref);
      } else {
	fontResource->setup(orgPages[0].page->getResourceDict(),xref);
      }
    }
  }
  if (resources != 0) {
    /* setup pattern dict for translation */
    resources->setupPattern();

    resources->setP2PFontResource(fontResource);
  }

  outputContents(str);
  outputSelf(str,tree,copiedObj);
}

void P2PPage::fit(PDFRectangle *box, double zoom)
{
  PDFRectangle rBox;
  /* only first orginal page is fitted */
  orgPages[0].fit(box,&mediaBox,zoom,&rBox);

  mediaBox = rBox;
  cropBox = mediaBox;
  haveCropBox = gFalse;
  bleedBox = cropBox;
  trimBox = cropBox;
  artBox = cropBox;

}

void P2PPage::mirror()
{
  P2PMatrix mat(-1,0,0,1,mediaBox.x2-mediaBox.x1,0);

  /* only first orginale page is mirrored */
  orgPages[0].mat.trans(&mat);
  orgPages[0].trans = gTrue;
}

void P2PPage::rotate(int orientation)
{
  PDFRectangle rBox;

  /* only first orginal page */
  orgPages[0].rotate(&mediaBox,orientation,&rBox);

  mediaBox = rBox;
  cropBox = mediaBox;
  haveCropBox = gFalse;
  bleedBox = cropBox;
  trimBox = cropBox;
  artBox = cropBox;
}

void P2PPage::position(PDFRectangle *box, int xpos, int ypos)
{
  /* only first orginal page */
  orgPages[0].position(box,&mediaBox,xpos,ypos);

  mediaBox = *box;
  cropBox = mediaBox;
  haveCropBox = gFalse;
  bleedBox = cropBox;
  trimBox = cropBox;
  artBox = cropBox;
}

void P2PPage::scale(double zoom)
{
  PDFRectangle rBox;

  /* only first orginal page */
  orgPages[0].scale(&mediaBox,zoom,&rBox);

  mediaBox = rBox;
  cropBox = mediaBox;
  haveCropBox = gFalse;
  bleedBox = cropBox;
  trimBox = cropBox;
  artBox = cropBox;
}

void P2PPage::autoRotate(PDFRectangle *box)
{
  double mediaWidth = box->x2 - box->x1;
  if (mediaWidth < 0) mediaWidth = -mediaWidth;
  double mediaHeight = box->y2 - box->y1;
  if (mediaHeight < 0) mediaHeight = -mediaHeight;
  /* only proccess when the page has one original page. */
  double pageWidth = mediaBox.x2 - mediaBox.x1;
  if (pageWidth < 0) pageWidth = -pageWidth;
  double pageHeight = mediaBox.y2 - mediaBox.y1;
  if (pageHeight < 0) pageHeight = -pageHeight;

  if ((mediaWidth >= pageWidth && mediaHeight >= pageHeight)
    || (mediaWidth < pageHeight || mediaHeight < pageWidth)) {
       /* the page is inside the media or rotated page is not inside */
      return;
  }
  rotate(1);
  position(box,0,0);
}

P2PPage::OrgPage::OrgPage()
{
  page = 0;
  mappingTable = 0;
  trans = gFalse;
  borderFlag = 0;
  clipFrame = gFalse;
}

P2PPage::OrgPage::~OrgPage()
{
  if (mappingTable != 0) {
    delete mappingTable;
  }
}

void P2PPage::OrgPage::position(PDFRectangle *box, PDFRectangle *oldBox,
  int xpos, int ypos)
{
  double mx,my;

  mx = box->x1 - oldBox->x1;
  my = box->y1 - oldBox->y1;
  switch (xpos) {
  default:
  case 0:
    /* center */
    mx += (box->x2-box->x1 - (oldBox->x2-oldBox->x1))/2;
    break;
  case -1:
    /* left */
    break;
  case 1:
    /* right */
    mx += (box->x2-box->x1 - (oldBox->x2-oldBox->x1));
    break;
  }
  switch (ypos) {
  default:
  case 0:
    /* center */
    my += (box->y2-box->y1 - (oldBox->y2-oldBox->y1))/2;
    break;
  case -1:
    /* bottom */
    break;
  case 1:
    /* top */
    my += (box->y2-box->y1 - (oldBox->y2-oldBox->y1));
    break;
  }
  mat.move(mx,my);
  trans = gTrue;
}

void P2PPage::OrgPage::fit(PDFRectangle *box, PDFRectangle *oldBox,
  double zoom, PDFRectangle *rBox)
{
  double w,h,ow,oh;
  GBool landscape, oLandscape;
  double scalex;
  double scaley;

  w = box->x2-box->x1;
  if (w < 0) w = -w;
  h = box->y2-box->y1;
  if (h < 0) h = -h;
  ow = oldBox->x2-oldBox->x1;
  if (ow < 0) ow = -ow;
  oh = oldBox->y2-oldBox->y1;
  if (oh < 0) oh = -oh;
  landscape = (w > h);
  oLandscape = (ow > oh);
  if (landscape != oLandscape) {
    /* rotate */
    mat.move(-oldBox->x1,-oldBox->y1);
    mat.rotate(-90);
    mat.move(0,ow);
    if (rBox != 0) {
      rBox->x1 = rBox->y1 = 0;
      rBox->x2 = oh;
      rBox->y2 = ow;
    }

    scalex = w/oh*zoom;
    scaley = h/ow*zoom;
    if (scalex > scaley) {
      mat.scale(scaley);

      if (rBox != 0) {
	rBox->x2 *= scaley;
	rBox->y2 *= scaley;
      }
    } else {
      mat.scale(scalex);

      if (rBox != 0) {
	rBox->x2 *= scalex;
	rBox->y2 *= scalex;
      }
    }
    mat.move(box->x1,box->y1);
    if (rBox != 0) {
      rBox->x1 += box->x1;
      rBox->x2 += box->x1;
      rBox->y1 += box->y1;
      rBox->y2 += box->y1;
    }
  } else {
    double mx, my;

    scalex = w/ow*zoom;
    scaley = h/oh*zoom;
    if (scalex > scaley) {
      mat.scale(scaley);
      if (rBox != 0) {
	rBox->x1 = oldBox->x1*scaley;
	rBox->y1 = oldBox->y1*scaley;
	rBox->x2 = oldBox->x2*scaley;
	rBox->y2 = oldBox->y2*scaley;
      }
    } else {
      mat.scale(scalex);
      if (rBox != 0) {
	rBox->x1 = oldBox->x1*scalex;
	rBox->y1 = oldBox->y1*scalex;
	rBox->x2 = oldBox->x2*scalex;
	rBox->y2 = oldBox->y2*scalex;
      }
    }
    mx = -rBox->x1+box->x1;
    my = -rBox->y1+box->y1;
    mat.move(mx,my);
    if (rBox != 0) {
      rBox->x1 += mx;
      rBox->x2 += mx;
      rBox->y1 += my;
      rBox->y2 += my;
    }
  }
  trans = gTrue;
}

void P2PPage::OrgPage::rotate(PDFRectangle *box,
  int orientation, PDFRectangle *rBox)
{
  PDFRectangle nbox;
  double t;

  /* move to origin point */
  mat.move(-box->x1, -box->y1);
  rBox->x1 = rBox->y1 = 0;
  rBox->x2 = box->x2-box->x1;
  rBox->y2 = box->y2-box->y1;

  switch (orientation) {
  case 0:
    /* 0 degree */
    break;
  case 1:
    /* 90 degree */
    mat.rotate(90);
    mat.move(rBox->y2,0);
    t = rBox->y2;
    rBox->y2 = rBox->x2;
    rBox->x2 = t;
    break;
  case 2:
    /* 180 degree */
    mat.rotate(180);
    mat.move(rBox->x2,rBox->y2);
    break;
  case 3:
    /* -90 degree */
    mat.rotate(270);
    mat.move(0,rBox->x2);
    t = rBox->y2;
    rBox->y2 = rBox->x2;
    rBox->x2 = t;
    break;
  }
  trans = gTrue;
  return;
}

void P2PPage::OrgPage::scale(PDFRectangle *box, double zoom, PDFRectangle *rBox)
{
  mat.scale(zoom,zoom);
  rBox->x1 = box->x1*zoom;
  rBox->y1 = box->y1*zoom;
  rBox->x2 = box->x2*zoom;
  rBox->y2 = box->y2*zoom;
}

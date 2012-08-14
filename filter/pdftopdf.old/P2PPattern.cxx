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
 P2PPattern.cc
 pdftopdf pattern object
*/
#include "goo/gmem.h"
#include "Object.h"
#include "P2PObject.h"
#include "GfxState.h"
#include "Dict.h"
#include "XRef.h"
#include "P2POutputStream.h"
#include "P2PMatrix.h"
#include "P2PPattern.h"
#include "P2PError.h"
#include "P2PFont.h"
#include "P2PGfx.h"
#include "P2PDoc.h"

P2PPattern::P2PPattern(Object *objA, XRef *xrefA, P2PMatrix *matA)
{
  P2PObject *pobj;

  mat = *matA;
  if (objA->isRef()) {
    pobj = P2PObject::find(objA->getRefNum(),objA->getRefGen());
    if (pobj != 0) {
      pattern = static_cast<OrgPattern *>(pobj);
      pattern->reference();
    } else {
      pattern = new OrgPattern(objA->getRefNum(),objA->getRefGen(),xrefA);
    }
  } else if (objA->isDict()) {
    pattern = new OrgPattern(objA);
  } else {
    p2pError(-1,const_cast<char *>("Illegal Pattern object type"));
  }
}

P2PPattern::~P2PPattern()
{
  if (pattern != 0) {
    int c = pattern->free();
    if (c <= 0) delete pattern;
    pattern = 0;
  }
}

void P2PPattern::output(P2POutputStream *str, XRef *xrefA)
{
  GfxPattern *orgPattern;

  if (getOffset() >= 0) return; /* already output */
  if (pattern == 0 || (orgPattern = pattern->getPattern()) == 0) return;
  switch (orgPattern->getType()) {
  case 1: /* tiling pattern */
    outputTilingPattern(str,orgPattern, xrefA);
    break;
  case 2: /* shading pattern */
    outputShadingPattern(str,orgPattern,pattern->getOrgObject(),xrefA);
    break;
  default:
    p2pError(-1,const_cast<char *>("Unknown pattern type %d"),orgPattern->getType());
    break;
  }
}

void P2PPattern::outputTilingPattern(P2POutputStream *str,
  GfxPattern *patternA, XRef *xref)
{
  P2PFontResource fontResource;
  P2PFontResource *fr = 0;
  Dict *resDict;
  GfxTilingPattern *tilingPattern
     = static_cast<GfxTilingPattern *>(patternA);
  double *box;
  double *orgMat;
  Object lenobj;
  int start;
  int len;

  outputBegin(str);

  if (P2PDoc::options.fontEmbedding
     && (resDict = tilingPattern->getResDict()) != 0) {
    fontResource.setup(resDict,xref);
    fr = &fontResource;
  }
  /* output dict part */
  str->puts("<< /Type /Pattern /PatternType 1");
  str->printf(" /PaintType %d",tilingPattern->getPaintType());
  str->printf(" /TilingType %d",tilingPattern->getTilingType());
  box = tilingPattern->getBBox();
  str->printf(" /BBox [ %f %f %f %f ]",box[0],box[1],box[2],box[3]);
  str->printf(" /XStep %f",tilingPattern->getXStep());
  str->printf(" /YStep %f",tilingPattern->getYStep());
  str->puts(" /Resources ");
  if ((resDict = tilingPattern->getResDict()) == 0) {
    str->puts("<< /ProcSet [ /PDF ] >>");
  } else {
    if (fr != 0) {
      /* replace font resource */
      const char *p = "Font";
      P2PObject *objp = fr;

      P2POutput::outputDict(resDict,&p,&objp,1,str,xref);
    } else {
      P2POutput::outputDict(resDict,str,xref);
    }
  }
  orgMat = tilingPattern->getMatrix();
  P2PMatrix porgMat(orgMat[0],orgMat[1],orgMat[2],orgMat[3],
                   orgMat[4],orgMat[5]);
  porgMat.trans(&mat);
  str->puts(" /Matrix [ ");
  porgMat.output(str);
  str->puts(" ]\n");
  str->puts(" /Length ");
  P2PObj *pobj = new P2PObj();
  P2PXRef::put(pobj);
  pobj->outputRef(str);
  if (P2PDoc::options.contentsCompress && str->canDeflate()) {
    str->puts(" /Filter /FlateDecode ");
  }
  str->puts(" >>\n");

  /* output Contents */
  P2PGfx output(xref,str,fr,0);
  str->puts("stream\n");
  start = str->getPosition();
  if (P2PDoc::options.contentsCompress) str->startDeflate();
  output.outputContents(tilingPattern->getContentStream(),0,resDict,&porgMat);
  if (P2PDoc::options.contentsCompress) str->endDeflate();
  len = str->getPosition()-start;
  str->puts("\nendstream\n");
  /* set length object value */
  lenobj.initInt(len);
  pobj->setObj(&lenobj);
  lenobj.free();

  outputEnd(str);

  /* out length object */
  pobj->output(str,xref);
}

void P2PPattern::outputShadingPattern(P2POutputStream *str,
  GfxPattern *patternA, Object *objA, XRef *xref)
{
  GfxShadingPattern *shadingPattern
     = static_cast<GfxShadingPattern *>(patternA);
  double *orgMat;
  Object matObj;
  int i;
  Object m[6];

  outputBegin(str);

  orgMat = shadingPattern->getMatrix();
  P2PMatrix porgMat(orgMat[0],orgMat[1],orgMat[2],orgMat[3],
                   orgMat[4],orgMat[5]);
  porgMat.trans(&mat);
  /* make matrix object */
  matObj.initArray(xref);
  for (i = 0;i < 6;i++) {
    m[i].initReal(porgMat.mat[i]);
    matObj.arrayAdd(&(m[i]));
  }

  /* replace Matrix */
  const char *p = "Matrix";
  P2PObject *objp = new P2PObj(&matObj);
  P2POutput::outputDict(objA->getDict(),&p,&objp,1,str,xref);
  delete objp;

  outputEnd(str);

  matObj.free();
  for (i = 0;i < 6;i++) {
    m[i].free();
  }
}

P2PPattern::OrgPattern::OrgPattern(int orgNumA, int orgGenA, XRef *xref)
  : P2PObject(orgNumA, orgGenA)
{
  pattern = 0;
  refCount = 1;
  xref->fetch(orgNumA,orgGenA,&orgObj);
#ifdef OLD_CS_PARSE
  if ((pattern = GfxPattern::parse(&orgObj)) == 0) {
#else
  if ((pattern = GfxPattern::parse(&orgObj,NULL)) == 0) {
#endif
    p2pError(-1,const_cast<char *>("Bad Pattern"));
  }
}

P2PPattern::OrgPattern::OrgPattern(Object *objA)
{
  pattern = 0;
  refCount = 1;
  objA->copy(&orgObj);
#ifdef OLD_CS_PARSE
  if ((pattern = GfxPattern::parse(&orgObj)) == 0) {
#else
  if ((pattern = GfxPattern::parse(&orgObj,NULL)) == 0) {
#endif
    p2pError(-1,const_cast<char *>("Bad Pattern"));
  }
}

P2PPattern::OrgPattern::~OrgPattern()
{
  if (pattern != 0) delete pattern;
  orgObj.free();
}

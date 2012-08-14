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
 P2PPattern.h
 pdftopdf pattern object
*/
#ifndef _P2PPATTERN_H_
#define _P2PPATTERN_H_

#include "goo/gmem.h"
#include "Object.h"
#include "P2PObject.h"
#include "GfxState.h"
#include "Dict.h"
#include "XRef.h"
#include "P2POutputStream.h"
#include "P2PMatrix.h"


class P2PPattern: public P2PObject {
public:
  P2PPattern(Object *objA, XRef *xrefA, P2PMatrix *matA);
  ~P2PPattern();
  virtual void output(P2POutputStream *str, XRef *xrefA);
  class OrgPattern : public P2PObject {
  public:
    OrgPattern(int orgNumA, int orgGenA, XRef *xref);
    OrgPattern(Object *objA);
    ~OrgPattern();
    void reference() { refCount++; }
    int free() { return --refCount; }
    GfxPattern *getPattern() { return pattern; }
    Object *getOrgObject() { return &orgObj; }
  private:
    GfxPattern *pattern;
    int refCount;
    Object orgObj;
  };
private:
  void outputTilingPattern(P2POutputStream *str, GfxPattern *patternA,
    XRef *xref);
  void outputShadingPattern(P2POutputStream *str, GfxPattern *patternA,
    Object *objA, XRef *xref);
  OrgPattern *pattern;
  P2PMatrix mat;
};

#endif

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
 P2PResources.h
 pdftopdf resouces dictionary
*/
#ifndef _P2PRESOURCES_H_
#define _P2PRESOURCES_H_

#include "goo/gmem.h"
#include "Object.h"
#include "P2PObject.h"
#include "Dict.h"
#include "XRef.h"
#include "P2POutputStream.h"
#include "P2PPattern.h"
#include "UGooString.h"

class P2PResourceMap;
class P2PFontResource;

class P2PResources: public P2PObject {
public:
  enum {
    ExtGState = 0,
    ColorSpace,
    Pattern,
    Shading,
    XObject,
    Font,
    NDict
  };
  static const char *dictNames[NDict];
  P2PResources(XRef *xrefA);
  ~P2PResources();
  void output(P2POutputStream *str);
  void setP2PFontResource(P2PFontResource *fontResourceA) {
    fontResource = fontResourceA;
  }
  P2PFontResource *getP2PFontResource() {
    return fontResource;
  }
  /* merge resources and return mapping table */
  P2PResourceMap *merge(Dict *res);
  P2PResourceMap *merge(P2PResources *res);
  Dict *getFontResource() {
    return dictionaries[Font];
  }
  Dict *getExtGState() {
    return dictionaries[ExtGState];
  }
  void setupPattern();
  void refPattern(char *name, P2PMatrix *matA);

  class P2PPatternDict {
  public:
    P2PPatternDict() {
      name = 0;
      pattern = 0;
    }
    ~P2PPatternDict() {
      if (name != 0) delete[] name;
      if (pattern != 0) delete pattern;
    }
    char *name;
    P2PPattern *pattern;
  };
private:
  XRef *xref;
  Dict *dictionaries[NDict];
  unsigned int resourceNo;
  P2PPatternDict *patternDict;
  int nPattern;
  P2PFontResource *fontResource;
  P2PObject **oldForms;
  int nOldForms;

  void mergeOneDict(Dict *dst, Dict *src, Dict *map, GBool unify);
#ifdef HAVE_UGOOSTRING_H
  void addDict(Dict *dict, Object *obj, UGooString *srckey, Dict *map);
#else
  void addDict(Dict *dict, Object *obj, char *srckey, Dict *map);
#endif
#ifdef HAVE_UGOOSTRING_H
  void addMap(UGooString *org, char *mapped, Dict *map);
#else
  void addMap(char *org, char *mapped, Dict *map);
#endif
  void handleOldForm(P2PResourceMap *map);
};

/* resource name mapping table */
class P2PResourceMap {
public:
  P2PResourceMap();
  ~P2PResourceMap();

  Dict *tables[P2PResources::NDict];
};

#endif

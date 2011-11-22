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
 P2PFont.h
 pdftopdf Font manager
*/
#ifndef _P2PFONT_H_
#define _P2PFONT_H_

#include "goo/gmem.h"
#include "GfxFont.h"
#include "P2PObject.h"
#include "P2PResources.h"
#include "Dict.h"
#include "XRef.h"
#include "PDFFTrueTypeFont.h"
#include "P2PCMap.h"

class P2PCIDToGID;

class P2PFontFile : public P2PObject {
public:
  ~P2PFontFile();
  static P2PFontFile *getFontFile(GooString *fileNameA,
    GfxFont *fontA, GfxFontType typeA, int faceIndexA);
  GooString *getFileName() {
    return fileName;
  }
  void output(P2POutputStream *str, XRef *xref);
  void outputCIDToGID(GBool wmode, P2POutputStream *str, XRef *xref);
  GBool isIdentity();
  P2PCIDToGID *getCIDToGID(GBool wmode);
  void refChar(CharCode cid) {
    if (charRefTable == 0) return;
    if (maxRefCID < cid) maxRefCID = cid;
    /* mark reference bit */
    charRefTable[cid / 8] |= (1 << (cid & (8-1)));
  }
  UGooString *getFontName() { return fontName; }
private:
  static char *getNextTag();
  P2PFontFile(GooString *fileNameA, GfxFont *fontA,
    GfxFontType typeA, int faceIndexA);
  static P2PFontFile **fontFileList;
  static int nFontFiles;
  static int fontFileListSize;
  static char nextTag[7];

  GooString *fileName;
  GfxFont *font;
  int faceIndex;
  GfxFontType type;
  P2PCIDToGID *CIDToGID;
  P2PCIDToGID *CIDToGID_V;
  unsigned char *charRefTable;
  CharCode maxRefCID;
  PDFFTrueTypeFont tfont;
  UGooString *fontName;
  P2PCMapCache cmapCache;
};

class P2PCIDToGID : public P2PObject {
public:
  P2PCIDToGID(P2PFontFile *fontFileA, GBool wmodeA);
  ~P2PCIDToGID();
  void output(P2POutputStream *str, XRef *xref);
private:
  P2PFontFile *fontFile;
  GBool wmode;
};

class P2PFontDescriptor : public P2PObject {
public:
  P2PFontDescriptor(Object *descriptorA, GfxFont *fontA,
    GooString *fileName, GfxFontType type, int faceIndexA, XRef *xref,
    int num, int gen);
  ~P2PFontDescriptor();
  void output(P2POutputStream *str, XRef *xref);
  P2PFontFile *getFontFile() { return fontFile; }
  GfxFontType getType() { return type; }
private:
  P2PFontFile *fontFile;
  Object descriptor;
  GfxFontType type;
};

class P2PCIDFontDict : public P2PObject {
public:
  P2PCIDFontDict(Object *fontDictA, GfxFont *fontA, GfxFontType typeA,
    GfxFontType embeddedTypeA,
    P2PFontDescriptor *fontDescriptorA, int num = -1, int gen = -1);
  ~P2PCIDFontDict();
  void output(P2POutputStream *str, XRef *xref);
  P2PFontDescriptor *getFontDescriptor() { return fontDescriptor; }
private:
  Object fontDict;
  GfxFontType type;
  GfxFontType embeddedType;
  P2PFontDescriptor *fontDescriptor;
  GfxFont *font;
};

class P2PDescendantFontsWrapper : public P2PObject {
public:
  P2PDescendantFontsWrapper(P2PObject *elementA);
  ~P2PDescendantFontsWrapper();
  void output(P2POutputStream *str, XRef *xref);
private:
  P2PObject *element;
};

class P2PFontDict : public P2PObject {
public:
  P2PFontDict(Object *fontDictA, XRef *xref, int num = -1, int gen = -1);
  ~P2PFontDict();
  void output(P2POutputStream *str, XRef *xref);
  P2PCIDFontDict *getCIDFontDict() { return cidFontDict; }
  void showText(GooString *s);
  UGooString *getEmbeddingFontName();
private:
  void doReadFontDescriptor(Object *dictObj, GfxFontType type,
    const char *name, XRef *xref);
  void read8bitFontDescriptor(GfxFontType type, const char *name,
    XRef *xref);
  void readCIDFontDescriptor(GfxFontType type, const char *name,
    XRef *xref);
  P2PFontDescriptor *fontDescriptor;
  GfxFont *font;
  Object fontDict;
  GfxFontType embeddedType;
  P2PCIDFontDict *cidFontDict;
};

class P2PFontResource : public P2PObject {
public:
  void setup(P2PResources *resources, XRef *xref);
  void setup(Dict *resources, XRef *xref);
  P2PFontDict *lookup(const UGooString &key);
  P2PFontDict *lookupExtGState(const UGooString &key);
  P2PFontResource();
  ~P2PFontResource();
  void output(P2POutputStream *str, XRef *xref);
  int getNDicts() { return nDicts; }
private:
  int getNExtGState() { return nExtGState; }
  void doSetup(Dict *fontResource, XRef *xref);
  void doSetupExtGState(Dict *extGState, XRef *xref);
  int nDicts;
  UGooString **keys;
  P2PFontDict **fontDicts;
  int nExtGState;
  UGooString **extGStateKeys;
  P2PFontDict **extGStateFonts;
};

#endif

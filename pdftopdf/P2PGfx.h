/*
  P2PGfx.h
  developped by BBR Inc. 2006-2007

  This file is based on Gfx.h
  Gfx.h copyright notice is follows
  and is licensed under GPL.
*/
//========================================================================
//
// Gfx.h
//
// Copyright 1996-2003 Glyph & Cog, LLC
//
//========================================================================

#ifndef _P2PGFX_H_
#define _P2PGFX_H_

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include "goo/gtypes.h"
#include "Object.h"
#include "Gfx.h"
#include "P2POutputStream.h"
#include "P2PResources.h"
#include "P2PFont.h"
#include "P2PMatrix.h"

class GooString;
class XRef;
class Array;
class Stream;
class Parser;
class Dict;
class P2PGfx;

//------------------------------------------------------------------------
// P2PGfx
//------------------------------------------------------------------------

class P2PGfx {
public:

  // Constructor
  P2PGfx(XRef *xrefA, P2POutputStream *strA, P2PFontResource *fontResourceA,
    P2PResources *resourcesA);
  ~P2PGfx();

  // Interpret a stream or array of streams.
  void outputContents(Object *obj, P2PResourceMap *mappingTableA,
    Dict *orgResourceA, P2PMatrix *matA);

private:
  struct P2POperator {
    char name[4];
    int numArgs;
    TchkType tchk[maxArgs];
    void (P2PGfx::*func)(Object args[], int numArgs);
  };

  class P2PGfxState {
  public:
    P2PGfxState() {
      font = 0;
      next = 0;
    }

    void clean() {
        while (next != 0) {
            restore();
        }
    }

    ~P2PGfxState() {
        clean();
    }

    void copy(P2PGfxState *src) {
      font = src->font;
    }

    P2PGfxState(P2PGfxState *src, P2PGfxState *nextA) {
      copy(src);
      next = nextA;
    }

    P2PFontDict *getFont() { return font; }

    void setFont(P2PFontDict *fontA) {
      if (fontA != 0) font = fontA;
    }

    void save() {
      next = new P2PGfxState(this,next);
    }

    void restore() {
      if (next != 0) {
        /* remove next */
        P2PGfxState *oldNext = next;
	next = oldNext->next;
        oldNext->next = 0;

	copy(oldNext);
        delete oldNext;
      }
    }
  private:
    P2PFontDict *font; // current font
    P2PGfxState *next;
  };

  XRef *xref;			// the xref table for this PDF file
  P2POutputStream *str;		// output stream
  P2PResourceMap *mappingTable;	// resouce name mapping table
  P2PFontResource *fontResource; // font resource
  P2PGfxState state;
  Dict *orgCSResource; // Color Space resource dictionay of original page
  P2PResources *resources;
  P2PMatrix *mat;

  Parser *parser;		// parser for page content stream(s)

  static P2POperator opTab[];	// table of operators

  void go();
  void execOp(Object *cmd, Object args[], int numArgs);
  P2POperator *findOp(char *name);
  GBool checkArg(Object *arg, TchkType type);
  int getPos();
  void outputOp(const char *name, Object args[], int numArgs);
  void opSetStrokeColorSpace(Object args[], int numArgs);
  void opXObject(Object args[], int numArgs);
  void opSetStrokeColorN(Object args[], int numArgs);
  void opSetFont(Object args[], int numArgs);
  void opSetFillColorSpace(Object args[], int numArgs);
  void opSetExtGState(Object args[], int numArgs);
  void opSetFillColorN(Object args[], int numArgs);
  void opShFill(Object args[], int numArgs);
  void opMoveSetShowText(Object args[], int numArgs);
  void opMoveShowText(Object args[], int numArgs);
  void opShowSpaceText(Object args[], int numArgs);
  void opShowText(Object args[], int numArgs);
  void opSave(Object args[], int numArgs);
  void opRestore(Object args[], int numArgs);
  void opBeginImage(Object args[], int numArgs);
  void doImage(Stream *istr);
};

#endif

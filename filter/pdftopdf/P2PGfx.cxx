/*
  P2PGfx.cc
  developped by BBR Inc. 2006-2007

  This file is based on Gfx.cc
  Gfx.cc copyright notice is follows
  and is licensed under GPL.
*/
//========================================================================
//
// Gfx.cc
//
// Copyright 1996-2003 Glyph & Cog, LLC
//
//========================================================================

#include <config.h>


#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "goo/gmem.h"
#include "goo/GooTimer.h"
#include "goo/GooHash.h"
#include "GlobalParams.h"
#include "CharTypes.h"
#include "Object.h"
#include "Array.h"
#include "Dict.h"
#include "Stream.h"
#include "Lexer.h"
#include "Parser.h"
#include "GfxFont.h"
#include "GfxState.h"
#include "OutputDev.h"
#include "Page.h"
#include "P2PError.h"
#include "Gfx.h"
#include "ProfileData.h"
#include "UGooString.h"
#include "P2PGfx.h"
#include "P2POutputStream.h"
#include "P2POutput.h"
#include "P2PResources.h"

//------------------------------------------------------------------------
// Operator table
//------------------------------------------------------------------------

#ifdef WIN32 // this works around a bug in the VC7 compiler
#  pragma optimize("",off)
#endif

P2PGfx::P2POperator P2PGfx::opTab[] = {
  {"\"",  3, {tchkNum,    tchkNum,    tchkString},
            &P2PGfx::opMoveSetShowText},
  {"'",   1, {tchkString},
            &P2PGfx::opMoveShowText},
  {"BI",  0, {tchkNone},
	  &P2PGfx::opBeginImage},
  {"CS",  1, {tchkName},
	  &P2PGfx::opSetStrokeColorSpace},
  {"Do",  1, {tchkName},
	  &P2PGfx::opXObject},
  {"Q",   0, {tchkNone},
          &P2PGfx::opRestore},
  {"SCN", -5, {tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	       tchkSCN},
	  &P2PGfx::opSetStrokeColorN},
  {"TJ",  1, {tchkArray},
            &P2PGfx::opShowSpaceText},
  {"Tf",  2, {tchkName,   tchkNum},
	  &P2PGfx::opSetFont},
  {"Tj",  1, {tchkString},
            &P2PGfx::opShowText},
  {"cs",  1, {tchkName},
	  &P2PGfx::opSetFillColorSpace},
  {"gs",  1, {tchkName},
	  &P2PGfx::opSetExtGState},
  {"q",   0, {tchkNone},
            &P2PGfx::opSave},
  {"scn", -5, {tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	       tchkSCN},
	  &P2PGfx::opSetFillColorN},
  {"sh",  1, {tchkName},
	  &P2PGfx::opShFill},
};

#ifdef WIN32 // this works around a bug in the VC7 compiler
#  pragma optimize("",on)
#endif

#define numOps (sizeof(opTab) / sizeof(P2POperator))

//------------------------------------------------------------------------
// P2PGfx
//------------------------------------------------------------------------

P2PGfx::P2PGfx(XRef *xrefA, P2POutputStream *strA,
  P2PFontResource *fontResourceA, P2PResources *resourcesA)
{
  xref = xrefA;
  str = strA;
  fontResource = fontResourceA;
  resources = resourcesA;

  /* make dummy GfxState for initializing color space */
  PDFRectangle box;
  GfxState state(72,72,&box,0,gFalse);
}

P2PGfx::~P2PGfx() {
  state.clean();
}

void P2PGfx::outputContents(Object *obj, P2PResourceMap *mappingTableA,
  Dict *orgResourceA, P2PMatrix *matA)
{
  Object obj2;
  int i;

  mappingTable = mappingTableA;
  mat = matA;
  orgCSResource = 0;
  if (orgResourceA != 0) {
    orgResourceA->lookup(const_cast<char *>("ColorSpace"),&obj2);
    if (obj2.isDict()) {
      orgCSResource = obj2.getDict();
      orgCSResource->incRef();
    }
  }
  obj2.free();
  if (obj->isArray()) {
    for (i = 0; i < obj->arrayGetLength(); ++i) {
      obj->arrayGet(i, &obj2);
      if (!obj2.isStream()) {
	p2pError(-1, const_cast<char *>("Weird page contents"));
	obj2.free();
	return;
      }
      obj2.free();
    }
  } else if (!obj->isStream()) {
    p2pError(-1, const_cast<char *>("Weird page contents"));
    return;
  }
  parser = new Parser(xref, new Lexer(xref, obj), gTrue);
  go();
  delete parser;
  parser = NULL;
}

void P2PGfx::go()
{
  Object obj;
  Object args[maxArgs];
  int numArgs, i;

  // scan a sequence of objects
  numArgs = 0;
  parser->getObj(&obj);
  while (!obj.isEOF()) {

    // got a command - execute it
    if (obj.isCmd()) {
      // Run the operation
      execOp(&obj, args, numArgs);

      obj.free();
      for (i = 0; i < numArgs; ++i)
	args[i].free();
      numArgs = 0;
    } else if (numArgs < maxArgs) {
      args[numArgs++] = obj;

    // too many arguments - something is wrong
    } else {
      p2pError(getPos(), const_cast<char *>("Too many args in content stream"));
      obj.free();
    }

    // grab the next object
    parser->getObj(&obj);
  }
  obj.free();

  // args at end with no command
  if (numArgs > 0) {
    p2pError(getPos(), const_cast<char *>("Leftover args in content stream"));
    for (i = 0; i < numArgs; ++i)
      args[i].free();
  }
}

void P2PGfx::outputOp(const char *name, Object args[], int numArgs)
{
  int i;

  for (i = 0;i < numArgs;i++) {
    P2POutput::outputObject(&args[i],str,xref);
    str->putchar(' ');
  }
  str->printf(" %s ",name);
}

void P2PGfx::execOp(Object *cmd, Object args[], int numArgs) {
  P2POperator *op;
  char *name;
  Object *argPtr;
  int i;

  // find operator
  name = cmd->getCmd();
  if (!(op = findOp(name))) {
    outputOp(name,args,numArgs);
    return;
  }

  // type check args
  argPtr = args;
  if (op->numArgs >= 0) {
    if (numArgs < op->numArgs) {
      p2pError(getPos(), const_cast<char *>("Too few (%d) args to '%s' operator"),
         numArgs, name);
      return;
    }
    if (numArgs > op->numArgs) {
      argPtr += numArgs - op->numArgs;
      numArgs = op->numArgs;
    }
  } else {
    if (numArgs > -op->numArgs) {
      p2pError(getPos(), const_cast<char *>("Too many (%d) args to '%s' operator"),
	    numArgs, name);
      return;
    }
  }
  for (i = 0; i < numArgs; ++i) {
    if (!checkArg(&argPtr[i], op->tchk[i])) {
      p2pError(getPos(),
            const_cast<char *>("Arg #%d to '%s' operator is wrong type (%s)"),
	    i, name, argPtr[i].getTypeName());
      return;
    }
  }

  // do it
  (this->*op->func)(argPtr, numArgs);
}

P2PGfx::P2POperator *P2PGfx::findOp(char *name) {
  int a, b, m, cmp = 1;

  a = -1;
  b = numOps;
  // invariant: opTab[a] < name < opTab[b]
  while (b - a > 1) {
    m = (a + b) / 2;
    cmp = strcmp(opTab[m].name, name);
    if (cmp < 0)
      a = m;
    else if (cmp > 0)
      b = m;
    else
      a = b = m;
  }
  if (cmp != 0)
    return NULL;
  return &opTab[a];
}

GBool P2PGfx::checkArg(Object *arg, TchkType type) {
  switch (type) {
  case tchkBool:   return arg->isBool();
  case tchkInt:    return arg->isInt();
  case tchkNum:    return arg->isNum();
  case tchkString: return arg->isString();
  case tchkName:   return arg->isName();
  case tchkArray:  return arg->isArray();
  case tchkProps:  return arg->isDict() || arg->isName();
  case tchkSCN:    return arg->isNum() || arg->isName();
  case tchkNone:   return gFalse;
  }
  return gFalse;
}

int P2PGfx::getPos() {
  return parser ? parser->getPos() : -1;
}

void P2PGfx::opSetStrokeColorSpace(Object args[], int numArgs)
{
  if (mappingTable != 0) {
    P2POutput::outputObject(&args[0],str,xref,
      mappingTable->tables[P2PResources::ColorSpace]);
  } else {
    P2POutput::outputObject(&args[0],str,xref);
  }
  str->puts(" CS ");
}

void P2PGfx::opXObject(Object args[], int numArgs)
{
  if (mappingTable != 0) {
    P2POutput::outputObject(&args[0],str,xref,
      mappingTable->tables[P2PResources::XObject]);
  } else {
    P2POutput::outputObject(&args[0],str,xref);
  }
  str->puts(" Do ");
}

void P2PGfx::opSetStrokeColorN(Object args[], int numArgs)
{
  int i;

  for (i = 0;i < numArgs-1;i++) {
    P2POutput::outputObject(&args[i],str,xref);
    str->putchar(' ');
  }
  if (mappingTable != 0 && args[i].isName()) {
    P2POutput::outputObject(&args[i],str,xref,
      mappingTable->tables[P2PResources::Pattern]);
    str->putchar(' ');
  } else {
    P2POutput::outputObject(&args[i],str,xref);
    str->putchar(' ');
  }
  if (args[i].isName() && resources != 0) {
    /* check pattern */
    Object obj;

    char *name = args[i].getName();
    if (mappingTable != 0
        && mappingTable->tables[P2PResources::Pattern] != 0) {
      mappingTable->tables[P2PResources::Pattern]->lookup(name,&obj);
      name = obj.getName();
    }
    resources->refPattern(name,mat);
    obj.free();
  }
  str->puts("SCN ");
}

void P2PGfx::opSetFont(Object args[], int numArgs)
{
  if (mappingTable != 0) {
    P2POutput::outputObject(&args[0],str,xref,
      mappingTable->tables[P2PResources::Font]);
  } else {
    P2POutput::outputObject(&args[0],str,xref);
  }
  if (fontResource != 0) {
    if (mappingTable != 0) {
      Dict *map = mappingTable->tables[P2PResources::Font];
      if (map != 0) {
	Object obj;
#ifdef HAVE_UGOOSTRING_H
	UGooString nameStr(args[0].getName());

	map->lookupNF(nameStr,&obj);
#else
	map->lookupNF(args[0].getName(),&obj);
#endif
	if (obj.isName()) {
	  state.setFont(fontResource->lookup(obj.getName()));
	}
	obj.free();
      }
    } else {
      state.setFont(fontResource->lookup(args[0].getName()));
    }
  }
  str->putchar(' ');
  P2POutput::outputObject(&args[1],str,xref);
  str->puts(" Tf ");
}

void P2PGfx::opSetFillColorSpace(Object args[], int numArgs)
{
  if (mappingTable != 0) {
    P2POutput::outputObject(&args[0],str,xref,
      mappingTable->tables[P2PResources::ColorSpace]);
  } else {
    P2POutput::outputObject(&args[0],str,xref);
  }
  str->puts(" cs ");
}

void P2PGfx::opSetExtGState(Object args[], int numArgs)
{
  if (mappingTable != 0) {
    P2POutput::outputObject(&args[0],str,xref,
      mappingTable->tables[P2PResources::ExtGState]);
    if (fontResource != 0) {
      Dict *map = mappingTable->tables[P2PResources::ExtGState];
      if (map != 0) {
	Object obj;
#ifdef HAVE_UGOOSTRING_H
	UGooString nameStr(args[0].getName());

	map->lookupNF(nameStr,&obj);
#else
	map->lookupNF(args[0].getName(),&obj);
#endif
	if (obj.isName()) {
	  state.setFont(fontResource->lookupExtGState(obj.getName()));
	}
	obj.free();
      }
    }
  } else {
    P2POutput::outputObject(&args[0],str,xref);
    if (fontResource != 0) {
      if (args[0].isName()) {
	state.setFont(fontResource->lookupExtGState(args[0].getName()));
      }
    }
  }
  str->puts(" gs ");
}

void P2PGfx::opSetFillColorN(Object args[], int numArgs)
{
  int i;

  for (i = 0;i < numArgs-1;i++) {
    P2POutput::outputObject(&args[i],str,xref);
    str->putchar(' ');
  }
  if (mappingTable != 0 && args[i].isName()) {
    P2POutput::outputObject(&args[i],str,xref,
      mappingTable->tables[P2PResources::Pattern]);
    str->putchar(' ');
  } else {
    P2POutput::outputObject(&args[i],str,xref);
    str->putchar(' ');
  }
  if (args[i].isName() && resources != 0) {
    /* check pattern */
    Object obj;

    char *name = args[i].getName();
    if (mappingTable != 0
        && mappingTable->tables[P2PResources::Pattern] != 0) {
      mappingTable->tables[P2PResources::Pattern]->lookup(name,&obj);
      name = obj.getName();
    }
    resources->refPattern(name,mat);
    obj.free();
  }
  str->puts("scn ");
}

void P2PGfx::opShFill(Object args[], int numArgs)
{
  if (mappingTable != 0) {
    P2POutput::outputObject(&args[0],str,xref,
      mappingTable->tables[P2PResources::Shading]);
  } else {
    P2POutput::outputObject(&args[0],str,xref);
  }
  str->puts(" sh ");
}

void P2PGfx::opMoveSetShowText(Object args[], int numArgs)
{
  P2PFontDict *f;

  if ((f = state.getFont()) != 0) f->showText(args[2].getString());
  outputOp("\"",args,numArgs);
}

void P2PGfx::opMoveShowText(Object args[], int numArgs)
{
  P2PFontDict *f;

  if ((f = state.getFont()) != 0) f->showText(args[0].getString());
  outputOp("'",args,numArgs);
}

void P2PGfx::opShowSpaceText(Object args[], int numArgs)
{
  P2PFontDict *f;

  if ((f = state.getFont()) != 0) {
    Array *a;
    int i, n;

    a = args[0].getArray();
    n = a->getLength();
    for (i = 0;i < n;i++) {
      Object obj;

      a->get(i,&obj);
      if (obj.isString()) {
	f->showText(obj.getString());
      }
      obj.free();
    }
  }
  outputOp("TJ",args,numArgs);
}

void P2PGfx::opShowText(Object args[], int numArgs)
{
  P2PFontDict *f;

  if ((f = state.getFont()) != 0) f->showText(args[0].getString());
  outputOp("Tj",args,numArgs);
}

void P2PGfx::opSave(Object args[], int numArgs)
{
  state.save();
  str->puts(" q ");
}

void P2PGfx::opRestore(Object args[], int numArgs)
{
  state.restore();
  str->puts(" Q ");
}

void P2PGfx::opBeginImage(Object args[], int numArgs)
{
  Object dict;
  char *key;
  Stream *imageStr;
  Object obj;

  /* output BI */
  str->puts(" BI");
  /* handle dictionary */
  dict.initDict(xref);
  parser->getObj(&obj);
  while (!obj.isCmd(const_cast<char *>("ID")) && !obj.isEOF()) {
    str->putchar(' ');
    if (!obj.isName()) {
      p2pError(getPos(), const_cast<char *>("Inline image dictionary key must be a name object"));
      P2POutput::outputObject(&obj,str,xref);
      str->putchar(' ');
      obj.free();
    } else {
      key = copyString(obj.getName());
      if (strcmp("Filter",key) != 0 && strcmp("F",key) != 0
         && strcmp("DecodeParms",key) != 0 && strcmp("DP",key) != 0) {
	/* when Filter or DecodeParms, not ouput because image is decoded. */
	P2POutput::outputObject(&obj,str,xref);
	str->putchar(' ');
      }
      obj.free();
      parser->getObj(&obj);
      if (obj.isEOF() || obj.isError()) {
	gfree(key);
	break;
      }
      if (mappingTable != 0 &&
          (strcmp(key,"ColorSpace") == 0 || strcmp(key,"CS") == 0)) {
	/* colorspace */
	/* resource mapping is needed */
	P2POutput::outputObject(&obj,str,xref,
	  mappingTable->tables[P2PResources::ColorSpace]);
      } else if (strcmp("Filter",key) != 0 && strcmp("F",key) != 0
         && strcmp("DecodeParms",key) != 0 && strcmp("DP",key) != 0) {
	/* when Filter or DecodeParms, not ouput because image is decoded. */
	P2POutput::outputObject(&obj,str,xref);
      }
      dict.dictAdd(key,&obj);
    }
    parser->getObj(&obj);
  }

  if (obj.isEOF()) {
    p2pError(getPos(), const_cast<char *>("End of file in inline image"));
    obj.free();
    dict.free();
  } else if (obj.isError()) {
    p2pError(getPos(), const_cast<char *>("Error in inline image"));
    obj.free();
    dict.free();
  } else {
    int c1, c2;

    /* ouput ID */
    str->putchar(' ');
    P2POutput::outputObject(&obj,str,xref);
    str->putchar('\n');
    obj.free();

    /* make image stream */
    imageStr = new EmbedStream(parser->getStream(), &dict, gFalse,0);
    imageStr = imageStr->addFilters(&dict);
    doImage(imageStr);
    /* handle EI */
    c1 = imageStr->getBaseStream()->getChar();
    c2 = imageStr->getBaseStream()->getChar();
    while (!(c1 == 'E' && c2 == 'I') && c2 != EOF) {
      c1 = c2;
      c2 = imageStr->getBaseStream()->getChar();
    }
    delete imageStr;
    str->puts("EI\n");
  }
}

void P2PGfx::doImage(Stream *istr)
{
  Dict *dict;
  int width, height;
  int bits;
  GBool mask;
  Object obj1, obj2;
  int i, j;
  GfxColorSpace *colorSpace = NULL;
  int nComponents;
  int bytes;
  Guchar *lineBuf;

  // get info from the stream
  bits = 0;

  // get stream dict
  dict = istr->getDict();

  // get size
  dict->lookup(const_cast<char *>("Width"), &obj1);
  if (obj1.isNull()) {
    obj1.free();
    dict->lookup(const_cast<char *>("W"), &obj1);
  }
  if (!obj1.isInt())
    goto err2;
  width = obj1.getInt();
  obj1.free();
  dict->lookup(const_cast<char *>("Height"), &obj1);
  if (obj1.isNull()) {
    obj1.free();
    dict->lookup(const_cast<char *>("H"), &obj1);
  }
  if (!obj1.isInt())
    goto err2;
  height = obj1.getInt();
  obj1.free();

  // image or mask?
  dict->lookup(const_cast<char *>("ImageMask"), &obj1);
  if (obj1.isNull()) {
    obj1.free();
    dict->lookup(const_cast<char *>("IM"), &obj1);
  }
  mask = gFalse;
  if (obj1.isBool())
    mask = obj1.getBool();
  else if (!obj1.isNull())
    goto err2;
  obj1.free();

  // bit depth
  if (bits == 0) {
    dict->lookup(const_cast<char *>("BitsPerComponent"), &obj1);
    if (obj1.isNull()) {
      obj1.free();
      dict->lookup(const_cast<char *>("BPC"), &obj1);
    }
    if (obj1.isInt()) {
      bits = obj1.getInt();
    } else if (mask) {
      bits = 1;
    } else {
      goto err2;
    }
    obj1.free();
  }

  // display a mask
  if (mask) {
    nComponents = 1;
  } else {
    // get color space
    dict->lookup(const_cast<char *>("ColorSpace"), &obj1);
    if (obj1.isNull()) {
      obj1.free();
      dict->lookup(const_cast<char *>("CS"), &obj1);
    }
    if (obj1.isName() && orgCSResource != 0) {
      orgCSResource->lookup(obj1.getName(), &obj2);
      if (!obj2.isNull()) {
	obj1.free();
	obj1 = obj2;
      } else {
	obj2.free();
      }
    }
    if (!obj1.isNull()) {
#ifdef OLD_CS_PARSE
      colorSpace = GfxColorSpace::parse(&obj1);
#else
      colorSpace = GfxColorSpace::parse(&obj1, NULL);
#endif
    }
    obj1.free();
    if (!colorSpace) {
      goto err1;
    }
    nComponents = colorSpace->getNComps();
    delete colorSpace;
  }
  istr->reset();
  /* number of bytes per line */
  bytes = (bits*nComponents*width+7)/8;

  /* then, out image body */
  lineBuf = new Guchar[bytes];
  for (i = 0;i < height;i++) {
    for (j = 0;j < bytes;j++) {
	lineBuf[j] = istr->getChar();
    }
    str->write(lineBuf,bytes);
  }
  str->putchar('\n');
  delete[] lineBuf;

  return;

 err2:
  obj1.free();
 err1:
  p2pError(getPos(), const_cast<char *>("Bad image parameters"));
}

//========================================================================
//
// P2PCharCodeToUnicode.h
// Mapping from character codes to Unicode.
// BBR Inc.
//
// Based Poppler CharCodeToUnicode.h
// Copyright 2001-2003 Glyph & Cog, LLC
//
//========================================================================

#ifndef P2PCHARCODETOUNICODE_H
#define P2PCHARCODETOUNICODE_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include "CharTypes.h"

struct P2PCharCodeToUnicodeString;

//------------------------------------------------------------------------

class P2PCharCodeToUnicode {
public:

  // Parse a ToUnicode CMap for an 8- or 16-bit font from File 
  static P2PCharCodeToUnicode *parseCMapFromFile(GooString *fileName,
    int nBits);

  ~P2PCharCodeToUnicode();

  // Map a CharCode to Unicode.
  int mapToUnicode(CharCode c, Unicode *u, int size);

  // Return the mapping's length, i.e., one more than the max char
  // code supported by the mapping.
  CharCode getLength() { return mapLen; }

private:

  void parseCMap1(int (*getCharFunc)(void *), void *data, int nBits);
  void addMapping(CharCode code, char *uStr, int n, int offset);
  P2PCharCodeToUnicode();
  Unicode *map;
  CharCode mapLen;
  P2PCharCodeToUnicodeString *sMap;
  int sMapLen, sMapSize;
};

#endif

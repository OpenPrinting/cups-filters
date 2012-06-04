//========================================================================
//
// P2PCharCodeToUnicode.cc
//
// BBR Inc.
//
// Based Poppler CharCodeToUnicode.h
// Copyright 2001-2003 Glyph & Cog, LLC
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdio.h>
#include <string.h>
#include "goo/gmem.h"
#include "goo/GooString.h"
#include "P2PError.h"
#include "GlobalParams.h"
#include "PSTokenizer.h"
#include "P2PCharCodeToUnicode.h"

//------------------------------------------------------------------------

#define maxUnicodeString 8

struct P2PCharCodeToUnicodeString {
  CharCode c;
  Unicode u[maxUnicodeString];
  int len;
};

//------------------------------------------------------------------------

static int getCharFromFile(void *data) {
  return fgetc((FILE *)data);
}

//------------------------------------------------------------------------

P2PCharCodeToUnicode *P2PCharCodeToUnicode::parseCMapFromFile(
    GooString *fileName, int nBits) {
  P2PCharCodeToUnicode *ctu;
  FILE *f;

  ctu = new P2PCharCodeToUnicode();
  if ((f = globalParams->findToUnicodeFile(fileName))) {
    ctu->parseCMap1(&getCharFromFile, f, nBits);
    fclose(f);
  } else {
    p2pError(-1, const_cast<char *>("Couldn't find ToUnicode CMap file for '%s'"),
	  fileName->getCString());
  }
  return ctu;
}

void P2PCharCodeToUnicode::parseCMap1(int (*getCharFunc)(void *), void *data,
				   int nBits) {
  PSTokenizer *pst;
  char tok1[256], tok2[256], tok3[256];
  int nDigits, n1, n2, n3;
  CharCode i;
  CharCode code1, code2;
  GooString *name;
  FILE *f;

  nDigits = nBits / 4;
  pst = new PSTokenizer(getCharFunc, data);
  pst->getToken(tok1, sizeof(tok1), &n1);
  while (pst->getToken(tok2, sizeof(tok2), &n2)) {
    if (!strcmp(tok2, "usecmap")) {
      if (tok1[0] == '/') {
	name = new GooString(tok1 + 1);
	if ((f = globalParams->findToUnicodeFile(name))) {
	  parseCMap1(&getCharFromFile, f, nBits);
	  fclose(f);
	} else {
	  p2pError(-1, const_cast<char *>("Couldn't find ToUnicode CMap file for '%s'"),
		name->getCString());
	}
	delete name;
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else if (!strcmp(tok2, "beginbfchar")) {
      while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	if (!strcmp(tok1, "endbfchar")) {
	  break;
	}
	if (!pst->getToken(tok2, sizeof(tok2), &n2) ||
	    !strcmp(tok2, "endbfchar")) {
	  p2pError(-1, const_cast<char *>("Illegal entry in bfchar block in ToUnicode CMap"));
	  break;
	}
	if (!(n1 == 2 + nDigits && tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	  
	  // check there was no line jump inside the token and so the length is 
	  // longer than it should be
	  int countAux = 0;
	  for (int k = 0; k < n1; k++)
	    if (tok1[k] != '\n' && tok1[k] != '\r') countAux++;
	
	  if (!(countAux == 2 + nDigits && tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	    p2pError(-1, const_cast<char *>("Illegal entry in bfchar block in ToUnicode CMap"));
	    continue;
	  }
	}
	tok1[n1 - 1] = tok2[n2 - 1] = '\0';
	if (sscanf(tok1 + 1, "%x", &code1) != 1) {
	  p2pError(-1, const_cast<char *>("Illegal entry in bfchar block in ToUnicode CMap"));
	  continue;
	}
	addMapping(code1, tok2 + 1, n2 - 2, 0);
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else if (!strcmp(tok2, "beginbfrange")) {
      while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	if (!strcmp(tok1, "endbfrange")) {
	  break;
	}
	if (!pst->getToken(tok2, sizeof(tok2), &n2) ||
	    !strcmp(tok2, "endbfrange") ||
	    !pst->getToken(tok3, sizeof(tok3), &n3) ||
	    !strcmp(tok3, "endbfrange")) {
	  p2pError(-1, const_cast<char *>("Illegal entry in bfrange block in ToUnicode CMap"));
	  break;
	}
	if (!(n1 == 2 + nDigits && tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      n2 == 2 + nDigits && tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	  // check there was no line jump inside the token and so the length is 
	  // longer than it should be
	  int countAux = 0;
	  for (int k = 0; k < n1; k++)
	    if (tok1[k] != '\n' && tok1[k] != '\r') countAux++;
	  
	  int countAux2 = 0;
	  for (int k = 0; k < n1; k++)
	    if (tok2[k] != '\n' && tok2[k] != '\r') countAux++;
	  
	  if (!(countAux == 2 + nDigits && tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      countAux2 == 2 + nDigits && tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	    p2pError(-1, const_cast<char *>("Illegal entry in bfrange block in ToUnicode CMap"));
	    continue;
	  }
	}
	tok1[n1 - 1] = tok2[n2 - 1] = '\0';
	if (sscanf(tok1 + 1, "%x", &code1) != 1 ||
	    sscanf(tok2 + 1, "%x", &code2) != 1) {
	  p2pError(-1, const_cast<char *>("Illegal entry in bfrange block in ToUnicode CMap"));
	  continue;
	}
	if (!strcmp(tok3, "[")) {
	  i = 0;
	  while (pst->getToken(tok1, sizeof(tok1), &n1) &&
		 code1 + i <= code2) {
	    if (!strcmp(tok1, "]")) {
	      break;
	    }
	    if (tok1[0] == '<' && tok1[n1 - 1] == '>') {
	      tok1[n1 - 1] = '\0';
	      addMapping(code1 + i, tok1 + 1, n1 - 2, 0);
	    } else {
	      p2pError(-1, const_cast<char *>("Illegal entry in bfrange block in ToUnicode CMap"));
	    }
	    ++i;
	  }
	} else if (tok3[0] == '<' && tok3[n3 - 1] == '>') {
	  tok3[n3 - 1] = '\0';
	  for (i = 0; code1 <= code2; ++code1, ++i) {
	    addMapping(code1, tok3 + 1, n3 - 2, i);
	  }

	} else {
	  p2pError(-1, const_cast<char *>("Illegal entry in bfrange block in ToUnicode CMap"));
	}
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else {
      strcpy(tok1, tok2);
    }
  }
  delete pst;
}

void P2PCharCodeToUnicode::addMapping(CharCode code, char *uStr, int n,
				   int offset) {
  CharCode oldLen, i;
  Unicode u;
  char uHex[5];
  int j;

  if (code >= mapLen) {
    oldLen = mapLen;
    mapLen = (code + 256) & ~255;
    map = (Unicode *)greallocn(map, mapLen, sizeof(Unicode));
    for (i = oldLen; i < mapLen; ++i) {
      map[i] = 0;
    }
  }
  if (n <= 4) {
    if (sscanf(uStr, "%x", &u) != 1) {
      p2pError(-1, const_cast<char *>("Illegal entry in ToUnicode CMap"));
      return;
    }
    map[code] = u + offset;
  } else {
    if (sMapLen >= sMapSize) {
      sMapSize = sMapSize + 16;
      sMap = (P2PCharCodeToUnicodeString *)
	       greallocn(sMap, sMapSize, sizeof(P2PCharCodeToUnicodeString));
    }
    map[code] = 0;
    sMap[sMapLen].c = code;
    sMap[sMapLen].len = n / 4;
    for (j = 0; j < sMap[sMapLen].len && j < maxUnicodeString; ++j) {
      strncpy(uHex, uStr + j*4, 4);
      uHex[4] = '\0';
      if (sscanf(uHex, "%x", &sMap[sMapLen].u[j]) != 1) {
	p2pError(-1, const_cast<char *>("Illegal entry in ToUnicode CMap"));
      }
    }
    sMap[sMapLen].u[sMap[sMapLen].len - 1] += offset;
    ++sMapLen;
  }
}

P2PCharCodeToUnicode::P2PCharCodeToUnicode() {
  CharCode i;

  mapLen = 256;
  map = (Unicode *)gmallocn(mapLen, sizeof(Unicode));
  for (i = 0; i < mapLen; ++i) {
    map[i] = 0;
  }
  sMap = NULL;
  sMapLen = sMapSize = 0;
}

P2PCharCodeToUnicode::~P2PCharCodeToUnicode() {
  gfree(map);
  if (sMap) {
    gfree(sMap);
  }
}

int P2PCharCodeToUnicode::mapToUnicode(CharCode c, Unicode *u, int size) {
  int i, j;

  if (c >= mapLen) {
    return 0;
  }
  if (map[c]) {
    u[0] = map[c];
    return 1;
  }
  for (i = 0; i < sMapLen; ++i) {
    if (sMap[i].c == c) {
      for (j = 0; j < sMap[i].len && j < size; ++j) {
	u[j] = sMap[i].u[j];
      }
      return j;
    }
  }
  return 0;
}

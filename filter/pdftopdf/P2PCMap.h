//========================================================================
//
// P2PCMap.h
//
// Copyright 2001-2003 Glyph & Cog, LLC
// 2007 Modefied by BBR Inc.
//
//========================================================================

#ifndef CMAP_H
#define CMAP_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include "poppler-config.h"
#include "goo/gtypes.h"
#include "CharTypes.h"

#if MULTITHREADED
#include <goo/GooMutex.h>
#endif

class GooString;
struct CMapVectorEntry;
class P2PCMapCache;

//------------------------------------------------------------------------

class P2PCMap {
public:

  // Create the CMap specified by <collection> and <cMapName>.  Sets
  // the initial reference count to 1.  Returns NULL on failure.
  static P2PCMap *parse(P2PCMapCache *cache, GooString *collectionA,
		     GooString *cMapNameA);

  ~P2PCMap();

  void incRefCnt();
  void decRefCnt();

  // Return collection name (<registry>-<ordering>).
  GooString *getCollection() { return collection; }

  // Return true if this CMap matches the specified <collectionA>, and
  // <cMapNameA>.
  GBool match(GooString *collectionA, GooString *cMapNameA);

  // Return the CID corresponding to the character code starting at
  // <s>, which contains <len> bytes.  Sets *<nUsed> to the number of
  // bytes used by the char code.
  CID getCID(char *s, int len, int *nUsed);

  // Return the writing mode (0=horizontal, 1=vertical).
  int getWMode() { return wMode; }

  void setReverseMap(Guint *rmap, Guint rmapSize, Guint ncand);

private:

  P2PCMap(GooString *collectionA, GooString *cMapNameA);
  P2PCMap(GooString *collectionA, GooString *cMapNameA, int wModeA);
  void useCMap(P2PCMapCache *cache, char *useName);
  void copyVector(CMapVectorEntry *dest, CMapVectorEntry *src);
  void addCodeSpace(CMapVectorEntry *vec, Guint start, Guint end,
		    Guint nBytes);
  void addCIDs(Guint start, Guint end, Guint nBytes, CID firstCID);
  void freeCMapVector(CMapVectorEntry *vec);
  void setReverseMapVector(Guint startCode, CMapVectorEntry *vec,
          Guint *rmap, Guint rmapSize, Guint ncand);

  GooString *collection;
  GooString *cMapName;
  int wMode;			// writing mode (0=horizontal, 1=vertical)
  CMapVectorEntry *vector;	// vector for first byte (NULL for
				//   identity CMap)
  int refCnt;
#if MULTITHREADED
  GooMutex mutex;
#endif
};

//------------------------------------------------------------------------

#define cMapCacheSize 4

class P2PCMapCache {
public:

  P2PCMapCache();
  ~P2PCMapCache();

  // Get the <cMapName> CMap for the specified character collection.
  // Increments its reference count; there will be one reference for
  // the cache plus one for the caller of this function.  Returns NULL
  // on failure.
  P2PCMap *getCMap(GooString *collection, GooString *cMapName);

private:

  P2PCMap *cache[cMapCacheSize];
};

#endif

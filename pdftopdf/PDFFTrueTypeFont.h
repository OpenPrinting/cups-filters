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
 PDFFTrueTypeFont.h
 TrueType Font manager
*/
#ifndef _PDFFTRUTYPEFONT_H_
#define _PDFFTRUTYPEFONT_H_

#include <endian.h>
#include "UGooString.h"
#ifdef PDFTOPDF
#include "P2POutputStream.h"
#endif

class PDFFTrueTypeFont {
public:
  typedef unsigned char BYTE;
  typedef signed char CHAR;
  typedef unsigned short USHORT;
  typedef signed short SHORT;
  typedef unsigned long ULONG;
  typedef signed long LONG;
  typedef unsigned long Fixed;
  typedef unsigned int FUNIT;
  typedef signed short FWORD;
  typedef unsigned short UFWORD;
  typedef unsigned short F2Dot14;
  typedef signed long long LONGDATETIME;
  typedef unsigned long TAG;
  typedef unsigned short GlyphID;

  PDFFTrueTypeFont();
  ~PDFFTrueTypeFont();
  int init(const char *fileNameA, unsigned int faceIndexA);
  UGooString *getFontName();
  int setupCmap();
#ifdef PDFTOPDF
  unsigned long getGID(unsigned long unicode, int wmode, int whole);
  void output(P2POutputStream *str);
  int getLength() { return outputLength; }
#else
  unsigned long getGID(unsigned long unicode, int wmode);
#endif
  unsigned long mapToVertGID(unsigned long code, unsigned long orgGID);
  int setupGSUB(const char *tagName);
private:

  /* table directory entry */
  class TableDirectory {
  public:
    ULONG tag;
    ULONG checkSum;
    ULONG offset;
    ULONG length;
#ifdef PDFTOPDF
    void output(PDFFTrueTypeFont *font, P2POutputStream *str);
    unsigned long calcCheckSum();
#endif
  };

#ifdef PDFTOPDF
  /* hhea table */
  class Hhea {
  public:
    Fixed version;
    FWORD Ascender;
    FWORD Descender;
    FWORD LineGap;
    UFWORD advanceWidthMax;
    FWORD minLeftSideBearing;
    FWORD minRightSideBearing;
    FWORD xMacExtent;
    SHORT caretSlopeRise;
    SHORT caretSlopeRun;
    SHORT caretOffset;
    SHORT metricDataFormat;
    USHORT numberOfHMetrics;
    int read(PDFFTrueTypeFont *font);
    void output(PDFFTrueTypeFont *font, P2POutputStream *str);
    unsigned long checkSum();
  };

  /* long format metrics */
  class LongHorMetric {
  public:
    USHORT advanceWidth;
    SHORT lsb;
    void output(PDFFTrueTypeFont *font, P2POutputStream *str) {
      font->write(str,advanceWidth);
      font->write(str,lsb);
    }
    unsigned long checkSum() {
      return (advanceWidth << 16)+(lsb & 0xffff);
    }
  };

  /* maxp table */
  class Maxp {
  public:
    Fixed version;
    USHORT numGlyphs;
    USHORT maxPoints;
    USHORT maxContours;
    USHORT maxCompositePoints;
    USHORT maxCompositeContours;
    USHORT maxZones;
    USHORT maxTwilightPoints;
    USHORT maxStorage;
    USHORT maxFunctionDefs;
    USHORT maxInstructionDefs;
    USHORT maxStackElements;
    USHORT maxSizeOfInstructions;
    USHORT maxComponentElements;
    USHORT maxComponentDepth;
    int read(PDFFTrueTypeFont *font);
    void output(PDFFTrueTypeFont *font, P2POutputStream *str);
    unsigned long checkSum();
  };

  /* head table */
  class Head {
  public:
    Fixed version;
    Fixed fontRevision;
    ULONG checkSumAdjustment;
    ULONG magicNumber;
    USHORT flags;
    USHORT unitsPerEm;
    LONGDATETIME created;
    LONGDATETIME modified;
    SHORT xMin;
    SHORT yMin;
    SHORT xMax;
    SHORT yMax;
    USHORT macStyle;
    USHORT lowestRecPPEM;
    SHORT fontDirectionHint;
    SHORT indexToLocFormat;
    SHORT glyphDataFormat;
    int read(PDFFTrueTypeFont *font);
    void output(PDFFTrueTypeFont *font, P2POutputStream *str);
    unsigned long checkSum();
  };

  int readOrgTables();
  void freeNewTables();
  void outputWholeFile(P2POutputStream *str);
  void setupNewTableDirectory();
  /* allocate new loca, gfyf, hmtx table */
  void allocNewLGHTable();
  /* reallocate new loca and hmtx table */
  void reallocNewLHTable();
  /* create loca, gfyf, hmtx table */
  void createLGHTable();
  void outputOrgData(P2POutputStream *str,
    unsigned long offset, unsigned long len);
  void outputOffsetTable(P2POutputStream *str);
  void outputTableDirectory(P2POutputStream *str);
  void outputHead(P2POutputStream *str) {
    head.output(this,str);
  }
  void outputHhea(P2POutputStream *str) {
    hhea.output(this,str);
  }
  void outputLoca(P2POutputStream *str);
  void outputMaxp(P2POutputStream *str) {
    maxp.output(this,str);
  }
  void outputCvt(P2POutputStream *str);
  void outputPrep(P2POutputStream *str);
  void outputGlyf(P2POutputStream *str);
  void outputHmtx(P2POutputStream *str);
  void outputFpgm(P2POutputStream *str);
  unsigned long locaCheckSum();
  unsigned long glyfCheckSum();
  unsigned long hmtxCheckSum();
  unsigned long offsetTableCheckSum();
  unsigned long tableDirectoryCheckSum();
  unsigned long allCheckSum();
  unsigned long mapGID(unsigned long orgGID);
  template<class T> void write(P2POutputStream *str, T v) {
    v = toBE(v);
    str->write(&v,sizeof(T));
  }

  int outputLength;
  unsigned long *GIDMap;
  unsigned long GIDMapSize;
  unsigned long maxGID;
  unsigned char *glyf;
  unsigned long glyfSize;
  unsigned long cGlyfIndex;
  LongHorMetric *hmtx;
  unsigned long hmtxSize;
  ULONG *loca;
  unsigned long locaSize;
  unsigned long locaEntrySize;
  Hhea hhea;
  Maxp maxp;
  /* table directory */
  TableDirectory *tDir;
  TableDirectory *orgTDir;
  Head head;
#endif

  /* cmap */
  class Cmap {
  public:
    virtual unsigned long getGID(PDFFTrueTypeFont *font,
      unsigned long unicode) = 0;
    virtual void read(PDFFTrueTypeFont *font) = 0;
    virtual ~Cmap() {};
  };

  /* cmap format 4 */
  class CmapFormat4 : public Cmap {
  public:
    CmapFormat4() {
      endCode = startCode = idRangeOffset = 0;
      idDelta = 0;
    }

    virtual ~CmapFormat4() {
      if (endCode != 0) delete[] endCode;
      if (startCode != 0) delete[] startCode;
      if (idDelta != 0) delete[] idDelta;
      if (idRangeOffset != 0) delete[] idRangeOffset;
    }
    virtual unsigned long getGID(PDFFTrueTypeFont *font,
      unsigned long unicode);
    virtual void read(PDFFTrueTypeFont *font);
  private:
    USHORT length;
    USHORT language;
    USHORT segCountX2;
    USHORT segCount;
    USHORT searchRange;
    USHORT entrySelector;
    USHORT rangeShift;
    USHORT *endCode;
    USHORT *startCode;
    SHORT *idDelta;
    ULONG idRangeOffsetTop;
    USHORT *idRangeOffset;

  };

  /* cmap format 12 */
  class CmapFormat12 : public Cmap {
  public:
    CmapFormat12() {
      startCharCode = endCharCode = startGlyphID = 0;
    }

    virtual ~CmapFormat12() {
      if (startCharCode != 0) delete[] startCharCode;
      if (endCharCode != 0) delete[] endCharCode;
      if (startGlyphID != 0) delete[] startGlyphID;
    }
    virtual unsigned long getGID(PDFFTrueTypeFont *font,
      unsigned long unicode);
    virtual void read(PDFFTrueTypeFont *font);
  private:
    ULONG length;
    ULONG language;
    ULONG nGroups;
    ULONG *startCharCode;
    ULONG *endCharCode;
    ULONG *startGlyphID;
  };


  int setOffsetTable(unsigned int faceIndex);
  unsigned long doMapToVertGID(unsigned long orgGID);
  int getTableDirEntry(TAG tableTag, TableDirectory *ent);
  int getTable(TAG tableTag, ULONG *tableOffset = 0, ULONG *tableLength = 0);
  void setOffset(unsigned long offsetA) { offset = offsetA; }
  unsigned long getOffset() { return offset; }
  void advance(unsigned long adv) { offset += adv; }
  void read(void *p, unsigned long len) {
    unsigned char *d = static_cast<unsigned char *>(p);
    unsigned char *s = top+offset;
    unsigned char *e;

    if (offset+len > fileLength) {
      e = top+fileLength-1; 
    } else {
      e = top+offset+len-1;
    }
#if __BYTE_ORDER == __LITTLE_ENDIAN
    while (s <= e) {
      *d++ = *e--;
    }
#else /* BIG ENDIAN */
    while (s <= e) {
      *d++ = *s++;
    }
#endif
    advance(len);
  }
  void read(unsigned long offsetA, void *p, unsigned long len) {
    setOffset(offsetA);
    read(p,len);
  }
  template<class T> void read(unsigned long offsetA, T *p) {
    read(offsetA, p, sizeof(T));
  }
  template<class T> void read(T *p) {
    read(p, sizeof(T));
  }
  template<class T> void skip() { advance(sizeof(T)); }

  /* convert to Big Endian */
  template<class T> static T toBE(T v) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    T r = 0;
    unsigned int i;

    for (i = 0;i < sizeof(T);i++) {
      r <<= 8;
      r |= (v & 0xff);
      v >>= 8;
    }
    return r;
#else /* BIG ENDIAN */
    return v;
#endif
  }
  unsigned long scanLookupList(USHORT listIndex, unsigned long orgGID);
  unsigned long scanLookupSubTable(ULONG subTable, unsigned long orgGID);
  int checkGIDInCoverage(ULONG coverage, unsigned long orgGID);
  unsigned long charToTag(const char *tagName);

  Cmap *cmap;
  char *fileName;
  int fontFileFD;
  unsigned char *top;
  unsigned long offset;
  unsigned long fileLength;
  ULONG offsetTable;
  ULONG featureTable;
  ULONG lookupList;
  UGooString *fontName;
  USHORT numTables;
  ULONG scriptTag;
};

#endif

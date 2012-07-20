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
 PDFFTrueTypeFont.cc
 TrueType font manager
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include "PDFFTrueTypeFont.h"
#include "P2PError.h"

#define TAG(a1,a2,a3,a4) (((a1) & 0xff) << 24 | \
   ((a2) & 0xff) << 16 | ((a3) & 0xff) << 8| ((a4) & 0xff))

#define TAG_TTC  TAG('t','t','c','f') /* ttcf */
#define TAG_NAME TAG('n','a','m','e') /* name */
#define TAG_CMAP TAG('c','m','a','p') /* cmap */
#define TAG_GSUB TAG('G','S','U','B') /* GSUB */
#define TAG_KANA TAG('k','a','n','a') /* kana */
#define TAG_VERT TAG('v','e','r','t') /* vert */
#define TAG_VRT2 TAG('v','r','t','2') /* vrt2 */
#define TAG_GLYF TAG('g','l','y','f') /* glyf */
#define TAG_LOCA TAG('l','o','c','a') /* loca */
#define TAG_HEAD TAG('h','e','a','d') /* head */
#define TAG_HHEA TAG('h','h','e','a') /* hhea */
#define TAG_HMTX TAG('h','m','t','x') /* hmtx */
#define TAG_MAXP TAG('m','a','x','p') /* maxp */
#define TAG_CVT  TAG('c','v','t',' ') /* cvt */
#define TAG_PREP TAG('p','r','e','p') /* prep */
#define TAG_FPGM TAG('f','p','g','m') /* fpgm */

#define GIDMAP_TABLE_INC 1024
#define OFFSET_TABLE_LEN 12

#ifdef PDFTOPDF
#define GLYF_INC 4096
#define LOCA_INC 1024
#define HMTX_INC 1024

/* generating font specifications */
/* table directory entry index */
#define TDIR_HEAD 0
#define TDIR_HHEA 1
#define TDIR_LOCA 2
#define TDIR_MAXP 3
#define TDIR_CVT  4
#define TDIR_PREP 5
#define TDIR_GLYF 6
#define TDIR_HMTX 7
#define TDIR_FPGM 8

/* number of tables */
#define TDIR_LEN 9

/* (Maximum power of 2 <= numTables) x 16. */
#define SEARCH_RANGE (8*16)
/* Log2(maximum power of 2 <= numTables) */
#define ENTRY_SELECTOR 3

/* table sizes */
#define HEAD_TABLE_LEN (54 + 2) /* 2 for padding */
#define HHEA_TABLE_LEN 36
#define MAXP_TABLE_LEN 32

/* table directory entry size */
#define TABLE_DIR_ENTRY_LEN 16

/* length of longHorMetrics entry */
#define LONGHORMETRIC_LEN (sizeof(USHORT)+sizeof(SHORT))

/* composite glyph script flags */
#define CG_ARG_1_AND_2_ARE_WORDS (1 << 0)
#define CG_ARGS_ARE_XY_VALUES (1 << 1)
#define CG_ROUND_XY_TO_GRID (1 << 2)
#define CG_WE_HAVE_A_SCALE (1 << 3)
#define CG_RESERVED (1 << 4)
#define CG_MORE_COMPONENTS (1 << 5)
#define CG_WE_HAVE_AN_X_AND_Y_SCALE (1 << 6)
#define CG_WE_HAVE_A_TWO_BY_TWO (1 << 7)
#define CG_WE_HAVE_INSTRUCTIONS (1 << 8)
#define CG_USE_MY_METRICS (1 << 9)
#define CG_OVERLAP_COMPOUND (1 << 10)
#define CG_SCALED_COMPONENTS_OFFSET (1 << 11)
#define CG_UNSCALED_COMPONENT_OFFSET (1 << 12)
#endif

#ifdef PDFTOPDF
unsigned long PDFFTrueTypeFont::mapGID(unsigned long orgGID)
{
  unsigned long i;

  if (orgGID == 0) return 0;
  if (GIDMap == 0) {
    GIDMapSize = GIDMAP_TABLE_INC;
    GIDMap = new unsigned long [GIDMapSize];
    GIDMap[0] = 0; /* always map 0 to 0 */
  }
  /* check if gid is already registered */
  for (i = 1;i < maxGID;i++) {
    if (GIDMap[i] == orgGID) return i;
  }
  /* not found */
  /* then regsiter it */
  if (++maxGID >= GIDMapSize) {
    /* realloc GIDMap */
    unsigned long *p;
    unsigned int newSize;

    newSize = GIDMapSize + GIDMAP_TABLE_INC;
    p = new unsigned long [newSize];
    memcpy(p,GIDMap,GIDMapSize*sizeof(unsigned long));
    delete[] GIDMap;
    GIDMap = p;
    GIDMapSize = newSize;
  }
  GIDMap[maxGID] = orgGID;
//fprintf(stderr,"DEBUG:map GID %d to %d\n",orgGID,maxGID);
  return maxGID;
}

void PDFFTrueTypeFont::allocNewLGHTable()
{
  /* Long version loca */
  loca = new ULONG [maxGID+2];
  locaSize = maxGID+2;

  /* allocate glyf */
  glyfSize = GLYF_INC;
  cGlyfIndex = 0;
  glyf = new unsigned char [glyfSize];

  /* allocate hmtx */
  /* all long metric */
  hmtx = new LongHorMetric [maxGID+1];
  hmtxSize = maxGID+1;
}

void PDFFTrueTypeFont::reallocNewLHTable()
{
  if (locaSize < maxGID+2) {
    unsigned long newSize;
    ULONG *np;

    newSize = locaSize+LOCA_INC;
    np = new ULONG [newSize];
    memcpy(np,loca,locaSize*sizeof(ULONG));
    delete[] loca;
    loca = np;
    locaSize = newSize;
  }
  if (hmtxSize < maxGID+1) {
    unsigned long newSize;
    LongHorMetric *np;

    newSize = hmtxSize+HMTX_INC;
    np = new LongHorMetric [newSize];
    memcpy(np,hmtx,hmtxSize*sizeof(LongHorMetric));
    delete[] hmtx;
    hmtx = np;
    hmtxSize = newSize;
  }
}

int PDFFTrueTypeFont::readOrgTables()
{
  if (getTableDirEntry(TAG_HEAD,&(orgTDir[TDIR_HEAD])) < 0) {
    p2pError(-1,const_cast<char *>("head table not found in font file %s"),
       fileName);
    return -1;
  }
  setOffset(orgTDir[TDIR_HEAD].offset);
  if (head.read(this) < 0) {
    return -1;
  }

  if (getTableDirEntry(TAG_HHEA,&(orgTDir[TDIR_HHEA])) < 0) {
    p2pError(-1,const_cast<char *>("hhea table not found in font file %s"),
       fileName);
    return -1;
  }
  setOffset(orgTDir[TDIR_HHEA].offset);
  if (hhea.read(this) < 0) {
    return -1;
  }

  if (getTableDirEntry(TAG_MAXP,&(orgTDir[TDIR_MAXP])) < 0) {
    p2pError(-1,const_cast<char *>("maxp table not found in font file %s"),
      fileName);
    return -1;
  }
  setOffset(orgTDir[TDIR_MAXP].offset);
  if (maxp.read(this) < 0) {
    return -1;
  }

  if (getTableDirEntry(TAG_LOCA,&(orgTDir[TDIR_LOCA])) < 0) {
    p2pError(-1,const_cast<char *>("loca table not found in font file %s"),
      fileName);
    return -1;
  }

  if (getTableDirEntry(TAG_GLYF,&(orgTDir[TDIR_GLYF])) < 0) {
    p2pError(-1,const_cast<char *>("glyf table not found in font file %s"),
      fileName);
    return -1;
  }

  if (getTableDirEntry(TAG_HMTX,&(orgTDir[TDIR_HMTX])) < 0) {
    p2pError(-1,const_cast<char *>("hmtx table not found in font file %s"),
      fileName);
    return -1;
  }

  if (getTableDirEntry(TAG_CVT,&(orgTDir[TDIR_CVT])) < 0) {
    /* not found */
    orgTDir[TDIR_CVT].tag = 0; /* null */
  }

  if (getTableDirEntry(TAG_PREP,&(orgTDir[TDIR_PREP])) < 0) {
    /* not found */
    orgTDir[TDIR_PREP].tag = 0; /* null */
  }

  if (getTableDirEntry(TAG_FPGM,&(orgTDir[TDIR_FPGM])) < 0) {
    /* not found */
    orgTDir[TDIR_FPGM].tag = 0; /* null */
  }
  return 0;
}

void PDFFTrueTypeFont::outputWholeFile(P2POutputStream *str)
{
  int n = fileLength;
  int r;
  unsigned char *p = top;

  while (n > 0) {
    r = str->write(p,n);
    if (r <= 0) break;
    n -= r;
    p += r;
  }
  outputLength = fileLength;
}

/* creating loca ,glyf and hmtx */
void PDFFTrueTypeFont::createLGHTable()
{
  unsigned long i;
  USHORT defWidth;

  /* allocate new tables */
  allocNewLGHTable();

  locaEntrySize = head.indexToLocFormat == 0 ? 2 : 4;

  /* read the last longHorMetrics for default width */
  read(orgTDir[TDIR_HHEA].offset+(hhea.numberOfHMetrics-1)*(sizeof(USHORT)
    +sizeof(SHORT)),&defWidth);

  /* NOTE: maxGID may be changed in this loop. */
  for (i = 0;i <= maxGID;i++) {
    ULONG start, end;
    unsigned long len;
    SHORT numberOfContours;
    unsigned long longHorMetricsLen
      = hhea.numberOfHMetrics*(sizeof(USHORT)+sizeof(SHORT));

    /* glyf */
    setOffset(orgTDir[TDIR_LOCA].offset+GIDMap[i]*locaEntrySize);
    read(&start,locaEntrySize);
    read(&end,locaEntrySize);
    len = end-start;
    /* convert start to offset from the top of file */
    start += orgTDir[TDIR_GLYF].offset;

    /* copy glyf entry */
    if (cGlyfIndex+len >= glyfSize) {
      /* realloc glyf */
      unsigned char *p;
      unsigned long newSize
        = (cGlyfIndex+len+GLYF_INC-1)/GLYF_INC;
      newSize *= GLYF_INC;

      p = new unsigned char [newSize];
      memcpy(p,glyf,cGlyfIndex);
      delete[] glyf;
      glyf = p;
      glyfSize = newSize;
    }
    read(start,&numberOfContours);
    if (numberOfContours < 0) {
      /* composite glyph */
//fprintf(stderr,"DEBUG: GID=%d is composite glyph\n",GIDMap[i]);
      SHORT flags;
      /* closure for copy */
      class _CopyGlyf {
      public:
	unsigned char *dst;
	unsigned char *src;

	_CopyGlyf(unsigned char *dstA, unsigned char *srcA) {
	  dst = dstA;
	  src = srcA;
	}
	void copy(unsigned long n) {
	  memcpy(dst,src,n);
	  dst += n;
	  src += n;
	}
	void copy(unsigned char *p, unsigned long n) {
	  memcpy(dst,p,n);
	  dst += n;
	  src += n;
	}
      } copyGlyf(glyf+cGlyfIndex,top+start);

      int haveInstructions = 0;

      /* skip xMin,yMin,xMax,yMax */
      advance(sizeof(SHORT)*4);

      /* copy header */
      copyGlyf.copy(sizeof(SHORT)*5);

      do {
	unsigned long oldMax;
	SHORT mappedGID;
	SHORT glyphIndex;

	read(&flags);
	haveInstructions |= (flags & CG_WE_HAVE_INSTRUCTIONS) != 0;
	/* copy flags */
	copyGlyf.copy(sizeof(flags));

	read(&glyphIndex);
//fprintf(stderr,"DEBUG: composite ref GID=%d, maxGID=%d\n",glyphIndex,maxGID);
	oldMax = maxGID;
	mappedGID = mapGID(glyphIndex);
	mappedGID = toBE(mappedGID); /* convert Big endian */
	/* put mapped GID */
	copyGlyf.copy(reinterpret_cast<unsigned char *>(&mappedGID),
	  sizeof(glyphIndex));

	if (maxGID != oldMax) {
	  /* new GID has been added */
//fprintf(stderr,"DEBUG: composite GID added\n");
	  reallocNewLHTable();
	}

	if (flags & CG_ARG_1_AND_2_ARE_WORDS) {
	  /* copy argument1, argument2 */
	  advance(sizeof(SHORT)*2);
	  copyGlyf.copy(sizeof(SHORT)*2);
	} else {
	  /* copy arg1and2 */
	  advance(sizeof(USHORT));
	  copyGlyf.copy(sizeof(USHORT));
	}
	if (flags & CG_WE_HAVE_A_SCALE) {
	  /* copy scale */
	  advance(sizeof(F2Dot14));
	  copyGlyf.copy(sizeof(F2Dot14));
	} else if (flags & CG_WE_HAVE_AN_X_AND_Y_SCALE) {
	  /* copy xscale, yscale */
	  advance(sizeof(F2Dot14)*2);
	  copyGlyf.copy(sizeof(F2Dot14)*2);
	} else if (flags & CG_WE_HAVE_A_TWO_BY_TWO) {
	  /* copy xscale, scale01, scale10, yscale */
	  advance(sizeof(F2Dot14)*4);
	  copyGlyf.copy(sizeof(F2Dot14)*4);
	}
      } while (flags & CG_MORE_COMPONENTS);
      if (haveInstructions) {
	USHORT numInstr;

	read(&numInstr);
	/* copy instructions */
	advance(sizeof(USHORT)+numInstr);
	copyGlyf.copy(sizeof(USHORT)+numInstr);
      }
    } else {
      /* single glyph */
      memcpy(glyf+cGlyfIndex,top+start,len);
    }
    loca[i] = cGlyfIndex;
    cGlyfIndex += len;

    /* hmtx */
    if (GIDMap[i] < hhea.numberOfHMetrics) {
      /* long version */
      setOffset(orgTDir[TDIR_HMTX].offset+GIDMap[i]*(sizeof(USHORT)
        +sizeof(SHORT)));
      read(&(hmtx[i].advanceWidth));
      read(&(hmtx[i].lsb));
    } else {
      /* lsb only version */
      read(orgTDir[TDIR_HMTX].offset+longHorMetricsLen
        +(GIDMap[i]-hhea.numberOfHMetrics)*sizeof(SHORT),&(hmtx[i].lsb));
      hmtx[i].advanceWidth = defWidth;
    }
  }
  loca[i] = cGlyfIndex; /* last entry */
}

unsigned long PDFFTrueTypeFont::locaCheckSum()
{
  unsigned long sum = 0;
  unsigned long i;

  for (i = 0;i < maxGID+2;i++) {
    sum += loca[i];
  }
  return sum;
}

unsigned long PDFFTrueTypeFont::glyfCheckSum()
{
  unsigned long sum = 0;
  unsigned long n = (cGlyfIndex+3)/4;
  unsigned long i;
  ULONG *p = reinterpret_cast<ULONG *>(glyf);

  for (i = 0;i < n;i++) {
    sum += toBE(p[i]);
  }
  return sum;
}

unsigned long PDFFTrueTypeFont::hmtxCheckSum()
{
  unsigned long sum = 0;
  unsigned long i;

  for (i = 0;i < maxGID+1;i++) {
    sum += hmtx[i].checkSum();
  }
  return sum;
}

void PDFFTrueTypeFont::setupNewTableDirectory()
{
  unsigned long toffset = OFFSET_TABLE_LEN+TABLE_DIR_ENTRY_LEN*TDIR_LEN;

  tDir[TDIR_HEAD].tag = TAG_HEAD;
  tDir[TDIR_HEAD].checkSum = head.checkSum();
  tDir[TDIR_HEAD].offset = toffset;
  tDir[TDIR_HEAD].length = HEAD_TABLE_LEN;
  toffset += tDir[TDIR_HEAD].length;

  tDir[TDIR_HHEA].tag = TAG_HHEA;
  tDir[TDIR_HHEA].checkSum = hhea.checkSum();
  tDir[TDIR_HHEA].offset = toffset;
  tDir[TDIR_HHEA].length = HHEA_TABLE_LEN;
  toffset += tDir[TDIR_HHEA].length;

  tDir[TDIR_LOCA].tag = TAG_LOCA;
  tDir[TDIR_LOCA].checkSum = locaCheckSum();
  tDir[TDIR_LOCA].offset = toffset;
  tDir[TDIR_LOCA].length = (maxGID+2)*sizeof(ULONG);
  toffset += tDir[TDIR_LOCA].length;

  tDir[TDIR_MAXP].tag = TAG_MAXP;
  tDir[TDIR_MAXP].checkSum = maxp.checkSum();
  tDir[TDIR_MAXP].offset = toffset;
  tDir[TDIR_MAXP].length = MAXP_TABLE_LEN;
  toffset += tDir[TDIR_MAXP].length;

  /* copy cvt table, so checkSum and length are same as original */
  tDir[TDIR_CVT].tag = TAG_CVT;
  tDir[TDIR_CVT].offset = toffset;
  if (orgTDir[TDIR_CVT].tag == 0) {
    /* no cvt table in original font, output empty */
    tDir[TDIR_CVT].checkSum = TAG_CVT + toffset;
    tDir[TDIR_CVT].length = 0;
  } else {
    tDir[TDIR_CVT].checkSum = orgTDir[TDIR_CVT].checkSum;
    tDir[TDIR_CVT].length = orgTDir[TDIR_CVT].length;
  }
  toffset += tDir[TDIR_CVT].length;

  /* copy prep table, so checkSum and length are same as original */
  tDir[TDIR_PREP].tag = TAG_PREP;
  tDir[TDIR_PREP].offset = toffset;
  if (orgTDir[TDIR_PREP].tag == 0) {
    /* no prep table in original font, output empty */
    tDir[TDIR_PREP].checkSum = TAG_PREP + toffset;
    tDir[TDIR_PREP].length = 0;
  } else {
    tDir[TDIR_PREP].length = orgTDir[TDIR_PREP].length;
    tDir[TDIR_PREP].checkSum = orgTDir[TDIR_PREP].checkSum;
  }
  toffset += tDir[TDIR_PREP].length;

  tDir[TDIR_GLYF].tag = TAG_GLYF;
  tDir[TDIR_GLYF].checkSum = glyfCheckSum();
  tDir[TDIR_GLYF].offset = toffset;
  tDir[TDIR_GLYF].length = cGlyfIndex;
  toffset += tDir[TDIR_GLYF].length;

  tDir[TDIR_HMTX].tag = TAG_HMTX;
  tDir[TDIR_HMTX].checkSum = hmtxCheckSum();
  tDir[TDIR_HMTX].offset = toffset;
  tDir[TDIR_HMTX].length = (maxGID+1)*LONGHORMETRIC_LEN;
  toffset += tDir[TDIR_HMTX].length;

  /* copy fpgm table, so checkSum and length are same as original */
  tDir[TDIR_FPGM].tag = TAG_FPGM;
  tDir[TDIR_FPGM].offset = toffset;
  if (orgTDir[TDIR_FPGM].tag == 0) {
    /* no fpgm table in original font, output empty */
    tDir[TDIR_FPGM].checkSum = TAG_FPGM + toffset;
    tDir[TDIR_FPGM].length = 0;
  } else {
    tDir[TDIR_FPGM].length = orgTDir[TDIR_FPGM].length;
    tDir[TDIR_FPGM].checkSum = orgTDir[TDIR_FPGM].checkSum;
  }
  toffset += tDir[TDIR_FPGM].length;
}

void PDFFTrueTypeFont::outputOffsetTable(P2POutputStream *str)
{
  write(str,static_cast<Fixed>(0x0010000)); /* version 1.0 */
  write(str,static_cast<USHORT>(TDIR_LEN)); /* numTable */
  write(str,static_cast<USHORT>(SEARCH_RANGE)); /* searchRange */
  write(str,static_cast<USHORT>(ENTRY_SELECTOR)); /* entrySelector */
  write(str,static_cast<USHORT>(TDIR_LEN*16-SEARCH_RANGE)); /* rangeShift */
}

unsigned long PDFFTrueTypeFont::offsetTableCheckSum()
{
  unsigned long sum = 0;

  sum += 0x0010000; /* version 1.0 */
  sum += TDIR_LEN << 16; /* numTable */
  sum += SEARCH_RANGE & 0xffff; /* searchRange */
  sum += ENTRY_SELECTOR << 16; /* entrySelector */
  sum += (TDIR_LEN*16-SEARCH_RANGE) & 0xffff; /* rangeShift */
  return sum;
}

unsigned long PDFFTrueTypeFont::allCheckSum()
{
  unsigned long sum = 0;
  int i;

  sum += offsetTableCheckSum();
  sum += tableDirectoryCheckSum();
  for (i = 0;i < TDIR_LEN;i++) {
    sum += tDir[i].checkSum;
  }
  return sum;
}

unsigned long PDFFTrueTypeFont::tableDirectoryCheckSum()
{
  int i;
  unsigned long sum = 0;

  for (i = 0;i < TDIR_LEN;i++) {
    sum += tDir[i].calcCheckSum();
  }
  return sum;
}

void PDFFTrueTypeFont::outputOrgData(P2POutputStream *str,
  unsigned long offset, unsigned long len)
{
  str->write(top+offset,len);
}

void PDFFTrueTypeFont::outputCvt(P2POutputStream *str)
{
  /* copy original data */
  outputOrgData(str,orgTDir[TDIR_CVT].offset,tDir[TDIR_CVT].length);
}

void PDFFTrueTypeFont::outputPrep(P2POutputStream *str)
{
  /* copy original data */
  outputOrgData(str,orgTDir[TDIR_PREP].offset,tDir[TDIR_PREP].length);
}

void PDFFTrueTypeFont::outputFpgm(P2POutputStream *str)
{
  /* copy original data */
  outputOrgData(str,orgTDir[TDIR_FPGM].offset,tDir[TDIR_FPGM].length);
}

void PDFFTrueTypeFont::outputLoca(P2POutputStream *str)
{
  unsigned long i;

  for (i = 0;i < maxGID+2;i++) {
    write(str,loca[i]);
  }
}

void PDFFTrueTypeFont::outputGlyf(P2POutputStream *str)
{
  str->write(glyf,cGlyfIndex);
}

void PDFFTrueTypeFont::outputHmtx(P2POutputStream *str)
{
  unsigned long i;

  for (i = 0;i < maxGID+1;i++) {
    hmtx[i].output(this,str);
  }
}

void PDFFTrueTypeFont::outputTableDirectory(P2POutputStream *str)
{
  unsigned int i;

  for (i = 0;i < TDIR_LEN;i++) {
    tDir[i].output(this,str);
  }
}

void PDFFTrueTypeFont::output(P2POutputStream *str)
{
  if (GIDMap == 0) {
    /* output whole file */
    outputWholeFile(str);
    return;
  }

  /* read or setup original tables */
  if (readOrgTables() < 0) {
    /* error */
    return;
  }

  /* creating loca ,glyf and hmtx */
  createLGHTable();

  /* change rest table values */
  head.checkSumAdjustment = 0;
  hhea.numberOfHMetrics = maxGID+1; /* all metrics are long format */
  maxp.numGlyphs = maxGID+1;
  maxp.maxContours = maxGID+1;
  maxp.maxCompositeContours = maxGID+1;

  setupNewTableDirectory();

  /* calc Adjustment */
  head.checkSumAdjustment = 0xB1B0AFBA-allCheckSum();

  /* output font */
  outputOffsetTable(str);
  outputTableDirectory(str);
  outputHead(str);
  outputHhea(str);
  outputLoca(str);
  outputMaxp(str);
  outputCvt(str);
  outputPrep(str);
  outputGlyf(str);
  outputHmtx(str);
  outputFpgm(str);

  /* setup table directory */
  freeNewTables();
}

void PDFFTrueTypeFont::freeNewTables()
{
  if (glyf != 0) {
    delete[] glyf;
    glyf = 0;
  }
  if (hmtx != 0) {
    delete[] hmtx;
    hmtx = 0;
    hmtxSize = 0;
  }
  if (loca != 0) {
    delete[] loca;
    loca = 0;
    locaSize = 0;
  }
}

int PDFFTrueTypeFont::Head::read(PDFFTrueTypeFont *font)
{
  font->read(&version);
  if ((version & 0x10000) != 0x10000) {
    p2pError(-1,const_cast<char *>("Not supported head table version in file %s"),
       font->fileName);
    return -1;
  }
  font->read(&fontRevision);
  font->read(&checkSumAdjustment);
  font->read(&magicNumber);
  font->read(&flags);
  font->read(&unitsPerEm);
  font->read(&created);
  font->read(&modified);
  font->read(&xMin);
  font->read(&yMin);
  font->read(&xMax);
  font->read(&yMax);
  font->read(&macStyle);
  font->read(&lowestRecPPEM);
  font->read(&fontDirectionHint);
  font->read(&indexToLocFormat);
  font->read(&glyphDataFormat);
  return 0;
}

void PDFFTrueTypeFont::Head::output(PDFFTrueTypeFont *font, P2POutputStream *str)
{
  font->write(str,version);
  font->write(str,fontRevision);
  font->write(str,checkSumAdjustment);
  font->write(str,magicNumber);
  font->write(str,flags);
  font->write(str,unitsPerEm);
  font->write(str,created);
  font->write(str,modified);
  font->write(str,xMin);
  font->write(str,yMin);
  font->write(str,xMax);
  font->write(str,yMax);
  font->write(str,macStyle);
  font->write(str,lowestRecPPEM);
  font->write(str,fontDirectionHint);
  font->write(str,indexToLocFormat);
  font->write(str,glyphDataFormat);
  font->write(str,static_cast<USHORT>(0)); /* padding */
}

unsigned long PDFFTrueTypeFont::Head::checkSum()
{
  unsigned long sum = 0;

  sum += version;
  sum += fontRevision;
  sum += magicNumber;
  sum += flags << 16;
  sum += unitsPerEm & 0xffff;
  sum += (created >> 32) & 0xffffffff;
  sum += created & 0xffffffff;
  sum += (modified >> 32) & 0xffffffff;
  sum += modified & 0xffffffff;
  sum += xMin << 16;
  sum += yMin & 0xffff;
  sum += xMax << 16;
  sum += yMax & 0xffff;
  sum += macStyle << 16;
  sum += lowestRecPPEM & 0xffff;
  sum += fontDirectionHint << 16;
  sum += indexToLocFormat & 0xffff;
  sum += glyphDataFormat << 16;

  return sum;
}

int PDFFTrueTypeFont::Hhea::read(PDFFTrueTypeFont *font)
{
  font->read(&version);
  if ((version & 0x10000) != 0x10000) {
    p2pError(-1,const_cast<char *>("Not supported hhea table version in file %s"),
      font->fileName);
    return -1;
  }
  font->read(&Ascender);
  font->read(&Descender);
  font->read(&LineGap);
  font->read(&advanceWidthMax);
  font->read(&minLeftSideBearing);
  font->read(&minRightSideBearing);
  font->read(&xMacExtent);
  font->read(&caretSlopeRise);
  font->read(&caretSlopeRun);
  font->read(&caretOffset);
  /* skip reserved */
  font->skip<SHORT>();
  font->skip<SHORT>();
  font->skip<SHORT>();
  font->skip<SHORT>();
  font->read(&metricDataFormat);
  font->read(&numberOfHMetrics);
  return 0;
}

void PDFFTrueTypeFont::Hhea::output(PDFFTrueTypeFont *font, P2POutputStream *str)
{
  font->write(str,version);
  font->write(str,Ascender);
  font->write(str,Descender);
  font->write(str,LineGap);
  font->write(str,advanceWidthMax);
  font->write(str,minLeftSideBearing);
  font->write(str,minRightSideBearing);
  font->write(str,xMacExtent);
  font->write(str,caretSlopeRise);
  font->write(str,caretSlopeRun);
  font->write(str,caretOffset);
  /* output reserved */
  font->write(str,static_cast<SHORT>(0));
  font->write(str,static_cast<SHORT>(0));
  font->write(str,static_cast<SHORT>(0));
  font->write(str,static_cast<SHORT>(0));
  font->write(str,metricDataFormat);
  font->write(str,numberOfHMetrics);
}

unsigned long PDFFTrueTypeFont::Hhea::checkSum()
{
  unsigned long sum = 0;

  sum += version;
  sum += Ascender << 16;
  sum += Descender & 0xffff;
  sum += LineGap << 16;
  sum += advanceWidthMax & 0xffff;
  sum += minLeftSideBearing << 16;
  sum += minRightSideBearing & 0xffff;
  sum += xMacExtent << 16;
  sum += caretSlopeRise & 0xffff;
  sum += caretSlopeRun << 16;
  sum += caretOffset & 0xffff;
  sum += metricDataFormat << 16;
  sum += numberOfHMetrics & 0xffff;
  return sum;
}

int PDFFTrueTypeFont::Maxp::read(PDFFTrueTypeFont *font)
{
  font->read(&version);
  if ((version & 0x10000) != 0x10000) {
    p2pError(-1,const_cast<char *>("Not supported maxp table version in file %s"),
      font->fileName);
    return -1;
  }
  font->read(&numGlyphs);
  font->read(&maxPoints);
  font->read(&maxContours);
  font->read(&maxCompositePoints);
  font->read(&maxCompositeContours);
  font->read(&maxZones);
  font->read(&maxTwilightPoints);
  font->read(&maxStorage);
  font->read(&maxFunctionDefs);
  font->read(&maxInstructionDefs);
  font->read(&maxStackElements);
  font->read(&maxSizeOfInstructions);
  font->read(&maxComponentElements);
  font->read(&maxComponentDepth);
  return 0;
}

void PDFFTrueTypeFont::Maxp::output(PDFFTrueTypeFont *font, P2POutputStream *str)
{
  font->write(str,version);
  font->write(str,numGlyphs);
  font->write(str,maxPoints);
  font->write(str,maxContours);
  font->write(str,maxCompositePoints);
  font->write(str,maxCompositeContours);
  font->write(str,maxZones);
  font->write(str,maxTwilightPoints);
  font->write(str,maxStorage);
  font->write(str,maxFunctionDefs);
  font->write(str,maxInstructionDefs);
  font->write(str,maxStackElements);
  font->write(str,maxSizeOfInstructions);
  font->write(str,maxComponentElements);
  font->write(str,maxComponentDepth);
}

unsigned long PDFFTrueTypeFont::Maxp::checkSum()
{
  unsigned long sum = 0;

  sum += version;
  sum += numGlyphs << 16;
  sum += maxPoints & 0xffff;
  sum += maxContours << 16;
  sum += maxCompositePoints & 0xffff;
  sum += maxCompositeContours << 16;
  sum += maxZones & 0xffff;
  sum += maxTwilightPoints << 16;
  sum += maxStorage & 0xffff;
  sum += maxFunctionDefs << 16;
  sum += maxInstructionDefs & 0xffff;
  sum += maxStackElements << 16;
  sum += maxSizeOfInstructions & 0xffff;
  sum += maxComponentElements << 16;
  sum += maxComponentDepth & 0xffff;
  return sum;
}

void PDFFTrueTypeFont::TableDirectory::output(PDFFTrueTypeFont *font,
  P2POutputStream *str)
{
  font->write(str,tag);
  font->write(str,checkSum);
  font->write(str,offset);
  font->write(str,length);
}

unsigned long PDFFTrueTypeFont::TableDirectory::calcCheckSum()
{
  unsigned long sum = 0;

  sum += tag;
  sum += checkSum;
  sum += offset;
  sum += length;
  return sum;
}
#endif

int PDFFTrueTypeFont::setupCmap()
{
  ULONG cmapTable;
  USHORT numCMTables;
  unsigned int i;
  USHORT format = 0;
  unsigned int format4Offset = 0;

  if (cmap != 0) return 0; /* already setup */
  if (getTable(TAG_CMAP,&cmapTable) < 0) {
    p2pError(-1,const_cast<char *>("cmap table not found in font file %s"),
      fileName);
    return -1;
  }
  setOffset(cmapTable);
  skip<USHORT>(); // skip version
  read(&numCMTables);

  /* find unicode table */
  /* NOTICE: only support, Microsoft Unicode or platformID == 0 */
  for (i = 0;i < numCMTables;i++) {
    USHORT platformID, encodingID;
    ULONG subtable;

    read(&platformID);
    read(&encodingID);
    read(&subtable);
    if ((platformID == 3 /* Microsoft */ && encodingID == 1 /* Unicode */)
        || platformID == 0) {
      unsigned long old = getOffset(); /* save current offset */

      setOffset(subtable+cmapTable);
      read(&format);
      if (format == 4) {
	/* found , but continue searching for format 12 */
	format4Offset = getOffset();
      }
      setOffset(old); /* restore old offset */
    } else if ((platformID == 3 /* Microsoft */ && encodingID == 10 /* UCS-4 */)
        || platformID == 0) {
      unsigned long old = getOffset(); /* save current offset */

      setOffset(subtable+cmapTable);
      read(&format);
      if (format == 12) {
	/* found */
	break;
      }
      if (format == 4) {
	/* found , but continue searching for format 12 */
	format4Offset = getOffset();
      }
      setOffset(old); /* restore old offset */
    }
  }
  if (i >= numCMTables && format4Offset == 0) {
    /* not found */
    p2pError(-1,const_cast<char *>("cmap table: Microsoft, Unicode, format 4 or 12 subtable not found in font file %s"),fileName);
    return -1;
  }
  if (format == 12) {
    cmap = new CmapFormat12();
    cmap->read(this);
  } else {
    /* format 4 */
    setOffset(format4Offset);
    cmap = new CmapFormat4();
    cmap->read(this);
  }

  return 0;
}

#ifdef PDFTOPDF
unsigned long PDFFTrueTypeFont::getGID(unsigned long unicode, int wmode, int whole)
#else
unsigned long PDFFTrueTypeFont::getGID(unsigned long unicode, int wmode)
#endif
{
  USHORT gid = 0;

  if (cmap == 0) return 0;

//fprintf(stderr,"DEBUG:getGID %d\n",code);
  gid = cmap->getGID(this, unicode);

  if (wmode && gid != 0) {
    gid = mapToVertGID(unicode,gid);
  }

#ifdef PDFTOPDF
  if (!whole) gid = mapGID(gid);
#endif
  return gid;
}

unsigned long PDFFTrueTypeFont::CmapFormat4::getGID(PDFFTrueTypeFont *font,
   unsigned long unicode)
{
  USHORT gid = 0;
  int a = -1;
  int b = segCount - 1;
  int m;

  if (unicode > endCode[b]) {
    /* segment not found */
    /* illegal format */
    return 0;
  }
  while (b - a > 1) {
    m = (a+b)/2;
    if (endCode[m] < unicode) {
      a = m;
    } else {
      b = m;
    }
  }

  if (unicode >= startCode[b]) {
    if (idRangeOffset[b] != 0) {
      ULONG off = idRangeOffsetTop + b*2
	  + idRangeOffset[b]
	  + (unicode - startCode[b])*2;
      font->read(off,&gid);
      if (gid != 0) gid += idDelta[b];
    } else {
      gid = unicode + idDelta[b];
    }
  }
  return gid;
}

void PDFFTrueTypeFont::CmapFormat4::read(PDFFTrueTypeFont *font)
{
  unsigned int i;

  /* read subtable */
  font->read(&length);
  font->read(&language);
  font->read(&segCountX2);
  segCount = segCountX2/2;
  font->read(&searchRange);
  font->read(&entrySelector);
  font->read(&rangeShift);
  endCode = new USHORT[segCount];
  for (i = 0;i < segCount;i++) {
    font->read(&(endCode[i]));
  }
  font->skip<USHORT>(); // skip reservedPad 
  startCode = new USHORT[segCount];
  for (i = 0;i < segCount;i++) {
    font->read(&(startCode[i]));
  }
  idDelta = new SHORT[segCount];
  for (i = 0;i < segCount;i++) {
    font->read(&(idDelta[i]));
  }
  idRangeOffsetTop = font->getOffset();
  idRangeOffset = new USHORT[segCount];
  for (i = 0;i < segCount;i++) {
    font->read(&(idRangeOffset[i]));
  }
}

unsigned long PDFFTrueTypeFont::CmapFormat12::getGID(PDFFTrueTypeFont *font,
   unsigned long unicode)
{
  unsigned long gid = 0;
  int a = -1;
  int b = nGroups - 1;
  int m;

  if (unicode > endCharCode[b]) {
    /* segment not found */
    /* illegal format */
    return 0;
  }
  while (b - a > 1) {
    m = (a+b)/2;
    if (endCharCode[m] < unicode) {
      a = m;
    } else {
      b = m;
    }
  }

  if (unicode >= startCharCode[b]) {
    gid = startGlyphID[b] + unicode - startCharCode[b];
  }
  return gid;
}

void PDFFTrueTypeFont::CmapFormat12::read(PDFFTrueTypeFont *font)
{
  unsigned int i;

  /* read subtable */
  font->skip<USHORT>(); // skip reservedPad 
  font->read(&length);
  font->read(&language);
  font->read(&nGroups);

  startCharCode = new ULONG[nGroups];
  endCharCode = new ULONG[nGroups];
  startGlyphID = new ULONG[nGroups];
  for (i = 0;i < nGroups;i++) {
    font->read(&(startCharCode[i]));
    font->read(&(endCharCode[i]));
    font->read(&(startGlyphID[i]));
  }
}

int PDFFTrueTypeFont::setOffsetTable(unsigned int faceIndexA)
{
  char *ext;

  offsetTable = 0;
  /* check if file is TTC */
  if ((ext = strrchr(fileName,'.')) != 0 && strncasecmp(ext,".ttc",4) == 0) {
    /* TTC file */
    /* set offsetTable */
    TAG TTCTag;
    ULONG numFonts;

    setOffset(0);
    read(&TTCTag);
    if (TTCTag != TAG_TTC) {
      p2pError(-1,const_cast<char *>("Illegal TTCTag:0x%08lx of TTC file:%s"),
        TTCTag,fileName);
      return -1;
    }
    skip<ULONG>(); /* skip version */
    read(&numFonts);
    if (numFonts <= faceIndexA) {
      p2pError(-1,const_cast<char *>("Too large faceIndex:%d of TTC file:%s : faces=%ld"),
        faceIndexA,fileName,numFonts);
      return -1;
    }
    advance(sizeof(ULONG)*faceIndexA); /* skip to the element */
    read(&offsetTable);
  }
  return 0;
}

PDFFTrueTypeFont::PDFFTrueTypeFont()
{
#ifdef PDFTOPDF
  outputLength = 0;
  GIDMap = 0;
  GIDMapSize = 0;
  maxGID = 0;
  glyf = 0;
  glyfSize = 0;
  cGlyfIndex = 0;
  hmtx = 0;
  hmtxSize = 0;
  loca = 0;
  locaSize = 0;
  locaEntrySize = 0;
  tDir = new TableDirectory [TDIR_LEN];
  orgTDir = new TableDirectory [TDIR_LEN];
#endif
  cmap = 0;
  top = 0;
  offset = 0;
  fileName = 0;
  fileLength = 0;
  fontFileFD = -1;
  featureTable = 0;
  lookupList = 0;
  fontName = 0;
  numTables = 0;
  scriptTag = 0;
}

UGooString *PDFFTrueTypeFont::getFontName()
{
  ULONG nameTable;
  USHORT count;
  USHORT stringOffset;
  unsigned int i;

  if (fontName != 0) return fontName;
  if (getTable(TAG_NAME,&nameTable) < 0) {
    p2pError(-1,const_cast<char *>("name table not found in font file %s"),
       fileName);
    return 0;
  }
  setOffset(nameTable);
  skip<USHORT>(); /* skip format */
  read(&count);
  read(&stringOffset);
  for (i = 0;i < count;i++) {
    USHORT nameID;
    USHORT length;
    USHORT so;
    USHORT platformID;
    USHORT encodingID;
    USHORT languageID;

    //advance(sizeof(USHORT)*3); /* skip to nameID */
    read(&platformID);
    read(&encodingID);
    read(&languageID);
    read(&nameID);
    read(&length);
    read(&so);
    if (nameID == 6) {
      /* PostScript name */
      if (platformID == 0 /* Unicode */ || platformID == 3 /* Microsoft */) {
	 /* Unicode */
	Unicode *p = new Unicode[length/2];
	unsigned int j;

	setOffset(nameTable+stringOffset+so);
	for (j = 0;j < length/2;j++) {
	  USHORT u;
	  read(&u);
	  p[j] = u;
	}
	fontName = new UGooString(p,length/2);
	return fontName;
      } else {
	unsigned int j;
	Unicode *p = new Unicode[length];
	unsigned char *q = top+nameTable+stringOffset+so;

	for (j = 0;j < length;j++) {
	  p[j] = q[j];
	}
	fontName = new UGooString(p,length);
	return fontName;
      }
    }
  }
  return 0;
}

int PDFFTrueTypeFont::init(const char *fileNameA, unsigned int faceIndexA)
{
  struct stat sbuf;
  Fixed sfntVersion;

  offsetTable = 0;
  fileName = new char [strlen(fileNameA)+1];
  strcpy(fileName,fileNameA);
  if ((fontFileFD = open(fileName,O_RDONLY)) < 0) {
    p2pError(-1,const_cast<char *>("Can't open font file %s"),fileName);
    return -1;
  }
  if (fstat(fontFileFD,&sbuf) < 0) {
    p2pError(-1,const_cast<char *>("Can't get stat of font file %s"),fileName);
    goto error_end;
  }
  fileLength = sbuf.st_size;
  if ((top = reinterpret_cast<unsigned char *>(
      mmap(0,fileLength,PROT_READ,MAP_PRIVATE,fontFileFD,0))) == 0) {
    p2pError(-1,const_cast<char *>("mmap font file %s failed"),fileName);
    goto error_end;
  }
  if (setOffsetTable(faceIndexA) < 0) goto error_end2;
  setOffset(offsetTable);
  read(&sfntVersion);
  if (sfntVersion != 0x00010000) {
    p2pError(-1,const_cast<char *>("Illegal sfnt version in font file %s"),
      fileName);
    goto error_end2;
  }
  read(&numTables);
  return 0;

error_end2:
  munmap((char *)top,fileLength);
error_end:
  close(fontFileFD);
  fontFileFD = -1;
  return -1;
}

PDFFTrueTypeFont::~PDFFTrueTypeFont()
{
#ifdef PDFTOPDF
  if (cmap != 0) delete cmap;
  if (GIDMap != 0) delete[] GIDMap;
  if (tDir != 0) delete[] tDir;
  if (orgTDir != 0) delete[] orgTDir;
  freeNewTables();
#endif

  if (top != 0) {
    munmap((char *)top,fileLength);
  }
  if (fontFileFD >= 0) close(fontFileFD);
  if (fileName != 0) delete[] fileName;
  if (fontName != 0) delete fontName;
}

unsigned long PDFFTrueTypeFont::charToTag(const char *tagName)
{
  int n = strlen(tagName);
  unsigned long tag = 0;
  int i;

  if (n > 4) n = 4;
  for (i = 0;i < n;i++) {
    tag <<= 8;
    tag |= tagName[i] & 0xff;
  }
  for (;i < 4;i++) {
    tag <<= 8;
    tag |= ' ';
  }
  return tag;
}

/*
  setup GSUB table data
  Only supporting vertical text substitution.
*/
int PDFFTrueTypeFont::setupGSUB(const char *tagName)
{
  ULONG gsubTable;
  unsigned int i;
  USHORT scriptList, featureList;
  USHORT scriptCount;
  TAG tag;
  USHORT scriptTable;
  USHORT langSys;
  USHORT featureCount;
  USHORT featureIndex;
  USHORT ftable = 0;
  USHORT llist;
  ULONG oldOffset;
  ULONG ttag;

  if (tagName == 0) {
    featureTable = 0;
    return 0;
  }
  ttag = charToTag(tagName);
  if (featureTable != 0 && ttag == scriptTag) {
    /* already setup GSUB */
    return 0;
  }
  scriptTag = ttag;
  /* read GSUB Header */
  if (getTable(TAG_GSUB,&gsubTable) < 0) {
    return 0; /* GSUB table not found */
  }
  setOffset(gsubTable);
  skip<Fixed>(); // skip version
  read(&scriptList);
  read(&featureList);
  read(&llist);

  lookupList = llist+gsubTable; /* change to offset from top of file */
  /* read script list table */
  setOffset(gsubTable+scriptList);
  read(&scriptCount);
  /* find  script */
  for (i = 0;i < scriptCount;i++) {
    read(&tag);
    read(&scriptTable);
    if (tag == scriptTag) {
      /* found */
      break;
    }
  }
  if (i >= scriptCount) {
    /* not found */
    return 0;
  }

  /* read script table */
  /* use default language system */
  setOffset(gsubTable+scriptList+scriptTable);
  read(&langSys); /* default language system */

  /* read LangSys table */
  if (langSys == 0) {
    /* no ldefault LangSys */
    return 0;
  }

  setOffset(gsubTable+scriptList+scriptTable+langSys);
  skip<USHORT>(); /* skip LookupOrder */
  read(&featureIndex); /* ReqFeatureIndex */
  if (featureIndex != 0xffff) {
    oldOffset = getOffset();
    /* read feature record */
    setOffset(gsubTable+featureList);

    read(&featureCount);
    setOffset(gsubTable+featureList+sizeof(USHORT)+
       featureIndex*(sizeof(TAG)+sizeof(USHORT)));
    read(&tag);
    if (tag == TAG_VRT2) {
      /* vrt2 is preferred, overwrite vert */
      read(&ftable);
      /* convert to offset from file top */
      featureTable = ftable+gsubTable+featureList;
      return 0;
    } else if (tag == TAG_VERT) {
      read(&ftable);
    }
    setOffset(oldOffset); /* restore offset */
  }
  read(&featureCount);
  /* find 'vrt2' or 'vert' feature */
  for (i = 0;i < featureCount;i++) {
    read(&featureIndex);
    oldOffset = getOffset();
    /* read feature record */
    setOffset(gsubTable+featureList+sizeof(USHORT)+
       featureIndex*(sizeof(TAG)+sizeof(USHORT)));
    read(&tag);
    if (tag == TAG_VRT2) {
      /* vrt2 is preferred, overwrite vert */
      read(&ftable);
      break;
    } else if (ftable == 0 && tag == TAG_VERT) {
      read(&ftable);
    }
    setOffset(oldOffset); /* restore offset */
  }
  if (ftable == 0) {
    /* vert nor vrt2 are not found */
    return 0;
  }
  /* convert to offset from file top */
  featureTable = ftable+gsubTable+featureList;
  return 0;
}

int PDFFTrueTypeFont::getTableDirEntry(TAG tableTag, TableDirectory *ent)
{
  unsigned int i;

 /* set to the top of Table Directory entiries */
  setOffset(offsetTable+OFFSET_TABLE_LEN);
  for (i = 0;i < numTables;i++) {
    ULONG tag;

    read(&tag);
    if (tag == tableTag) {
      /* found */
      ent->tag = tag;
      read(&(ent->checkSum));
      read(&(ent->offset));
      read(&(ent->length));
      return 0;
    }
    advance(sizeof(ULONG)*3); /* skip to next entry */
  }
  return -1; /* not found */
}

int PDFFTrueTypeFont::getTable(TAG tableTag, ULONG *tableOffset,
  ULONG *tableLength)
{
  TableDirectory ent;

  if (getTableDirEntry(tableTag,&ent) < 0) {
    /* not found */
    return -1;
  }
  if (tableOffset != 0) {
    *tableOffset = ent.offset;
  }
  if (tableLength != 0) {
    *tableLength = ent.length;
  }
  return 0;
}

unsigned long PDFFTrueTypeFont::doMapToVertGID(unsigned long orgGID)
{
  USHORT lookupCount;
  USHORT lookupListIndex;
  ULONG i;
  unsigned long gid = 0;

  setOffset(featureTable+sizeof(USHORT));
  read(&lookupCount);
  for (i = 0;i < lookupCount;i++) {
    read(&lookupListIndex);
    if ((gid = scanLookupList(lookupListIndex,orgGID)) != 0) {
      break;
    }
  }
  return gid;
}

unsigned long PDFFTrueTypeFont::mapToVertGID(unsigned long code,
  unsigned long orgGID)
{
  unsigned long mapped;
  //unsigned long i;

  if (featureTable == 0) return orgGID;
  if ((mapped = doMapToVertGID(orgGID)) != 0) {
//fprintf(stderr,"DEBUG:GSUB map %d to %d\n",orgGID,mapped);
    return mapped;
  }
  return orgGID;
}

unsigned long PDFFTrueTypeFont::scanLookupList(USHORT listIndex,
  unsigned long orgGID)
{
  USHORT lookupTable;
  USHORT subTableCount;
  USHORT subTable;
  ULONG i;
  unsigned long gid = 0;
  ULONG oldOffset = getOffset();

  if (lookupList == 0) return 0; /* no lookup list */
  setOffset(lookupList+sizeof(USHORT)+listIndex*sizeof(USHORT));
  read(&lookupTable);
  /* read lookup table */
  setOffset(lookupList+lookupTable);
  skip<USHORT>(); /* skip LookupType */
  skip<USHORT>(); /* skip LookupFlag */
  read(&subTableCount);
  for (i = 0;i < subTableCount;i++) {
    read(&subTable);
    if ((gid = scanLookupSubTable(lookupList+lookupTable+subTable,orgGID))
         != 0) break;
  }
  setOffset(oldOffset); /* restore offset */
  return gid;
}

unsigned long PDFFTrueTypeFont::scanLookupSubTable(ULONG subTable,
  unsigned long orgGID)
{
  USHORT format;
  USHORT coverage;
  SHORT delta;
  SHORT glyphCount;
  GlyphID substitute;
  unsigned long gid = 0;
  int coverageIndex;
  ULONG oldOffset = getOffset();

  setOffset(subTable);
  read(&format);
  read(&coverage);
  if ((coverageIndex =
     checkGIDInCoverage(subTable+coverage,orgGID)) >= 0) {
    switch (format) {
    case 1:
      /* format 1 */
      read(&delta);
      gid = orgGID+delta;
      break;
    case 2:
      /* format 2 */
      read(&glyphCount);
      if (glyphCount > coverageIndex) {
	advance(coverageIndex*sizeof(GlyphID));
	read(&substitute);
	gid = substitute;
      }
      break;
    default:
      /* unknown format */
      break;
    }
  }
  setOffset(oldOffset); /* restore offset */
  return gid;
}

int PDFFTrueTypeFont::checkGIDInCoverage(ULONG coverage, unsigned long orgGID)
{
  int index = -1;
  ULONG oldOffset = getOffset();
  USHORT format;
  USHORT count;
  ULONG i;

  setOffset(coverage);
  read(&format);
  switch (format) {
  case 1:
    read(&count);
    for (i = 0;i < count;i++) {
      GlyphID gid;

      read(&gid);
      if (gid == orgGID) {
	/* found */
	index = i;
	break;
      } else if (gid > orgGID) {
	/* not found */
	break;
      }
    }
    break;
  case 2:
    read(&count);
    for (i = 0;i < count;i++) {
      GlyphID startGID, endGID;
      USHORT startIndex;

      read(&startGID);
      read(&endGID);
      read(&startIndex);
      if (startGID <= orgGID && orgGID <= endGID) {
	/* found */
	index = startIndex+orgGID-startGID;
	break;
      } else if (orgGID <= endGID) {
	/* not found */
	break;
      }
    }
    break;
  default:
    break;
  }
  setOffset(oldOffset); /* restore offset */
  return index;
}

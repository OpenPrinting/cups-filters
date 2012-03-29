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
 P2PFont.cc
 pdftopdf font manager
*/

#include <config.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include "goo/gmem.h"
#include "P2PFont.h"
#include "GfxFont.h"
#include "P2PError.h"
#include "P2PXRef.h"
#include "BuiltinFontTables.h"
#include "P2PCMap.h"
#include "GlobalParams.h"
#include "PDFFTrueTypeFont.h"
#include "P2PDoc.h"
#include "CharCodeToUnicode.h"
#include "P2PCharCodeToUnicode.h"

/* Size of CID (bytes) */
#define CID_SIZE 2
#define CIDTOGID_SIZE (1 << (8*CID_SIZE))

int P2PFontFile::nFontFiles = 0;
int P2PFontFile::fontFileListSize = 0;
P2PFontFile **P2PFontFile::fontFileList = 0;
char P2PFontFile::nextTag[7] = "AAAAA@";

#define FONT_FILE_LIST_INC 256


P2PCIDToGID::P2PCIDToGID(P2PFontFile *fontFileA, GBool wmodeA)
{
  fontFile = fontFileA;
  wmode = wmodeA;
}

P2PCIDToGID::~P2PCIDToGID()
{
  /* must not delete fontFile */
}

void P2PCIDToGID::output(P2POutputStream *str, XRef *xref)
{
  P2PObj *lenObj = new P2PObj();
  int cp;
  int len;

  P2PXRef::put(lenObj);
  outputBegin(str);
  str->puts("<< /Length ");
  lenObj->outputRef(str);
  if (P2PDoc::options.fontCompress && str->canDeflate()) {
    str->puts(" /Filter /FlateDecode ");
  }
  str->puts(" >>\n"
        "stream\n");
  cp = str->getPosition(); /* start position */
  if (P2PDoc::options.fontCompress) str->startDeflate();
  fontFile->outputCIDToGID(wmode,str,xref);
  if (P2PDoc::options.fontCompress) str->endDeflate();
  len = str->getPosition() - cp; /* calculate length */
  str->puts("\nendstream\n");
  outputEnd(str);

  /* output length */
  lenObj->outputBegin(str);
  str->printf("%d\n",len);
  lenObj->outputEnd(str);
}

char *P2PFontFile::getNextTag()
{
  int i;

  for (i = 5;i >= 0;i--) {
    int c = nextTag[i];

    nextTag[i] = ++c;
    if (c <= 'Z') break;
    nextTag[i] = 'A';
  }
  return nextTag;
}

P2PFontFile *P2PFontFile::getFontFile(GooString *fileNameA,
  GfxFont *fontA, GfxFontType typeA, int faceIndexA)
{
  int i;

  for (i = 0;i < nFontFiles;i++) {
    if (fontFileList[i] != 0 && strcmp(fileNameA->getCString(),
         fontFileList[i]->getFileName()->getCString()) == 0
	 && faceIndexA == fontFileList[i]->faceIndex) {
      return fontFileList[i];
    }
  }
  if (nFontFiles >= fontFileListSize) {
    int size = fontFileListSize + FONT_FILE_LIST_INC;
    P2PFontFile **list;

    list = new P2PFontFile *[size];
    for (i = 0;i < nFontFiles;i++) {
      list[i] = fontFileList[i];
    }
    for (;i < size;i++) {
      list[i] = 0;
    }
    delete fontFileList;
    fontFileList = list;
    fontFileListSize = size;
  }
  fontFileList[nFontFiles] = new P2PFontFile(fileNameA,fontA,typeA,faceIndexA);
  P2PXRef::put(fontFileList[nFontFiles]);
  return fontFileList[nFontFiles++];
}

P2PFontFile::P2PFontFile(GooString *fileNameA, GfxFont *fontA,
  GfxFontType typeA, int faceIndexA)
{
  fileName = new GooString(fileNameA);
  font = fontA;
  type = typeA;
  faceIndex = faceIndexA;
  CIDToGID = 0;
  CIDToGID_V = 0;
  maxRefCID = 0;
  fontName = 0;
  setSecondPhase(gTrue);
  if (type == fontCIDType2) {
    charRefTable = new unsigned char[CIDTOGID_SIZE/8];
    memset(charRefTable,0,CIDTOGID_SIZE/8);
  } else {
    charRefTable = 0;
  }
  if (type == fontCIDType2 || type == fontTrueType) {
    UGooString *fn;

    tfont.init(fileName->getCString(),faceIndex);
    tfont.setupCmap();
    fn = tfont.getFontName();
    if (fn != 0) {
      if (type == fontCIDType2 && !P2PDoc::options.fontEmbeddingWhole) {
	char *tag;
	Unicode *p;
	int len;
	int tagLen;
	int fnLen;
	int i,j;
	Unicode *u;

	tag = getNextTag();
	tagLen = strlen(tag);
	fnLen = fn->getLength();
	len = tagLen+1+fnLen;
	/* use gmalloc because UGooString uses it */
	p = static_cast<Unicode *>(gmalloc(len*sizeof(Unicode)));
	/* copy tag */
	for (i = 0;i < tagLen;i++) {
	  p[i] = tag[i];
	}
	p[i++] = '+';
	/* copy font name */
	u = fn->unicode();
	for (j = 0;j < fnLen;j++) {
	  p[i++] = u[j];
	}
	fontName = new UGooString(p,len);
      } else {
	fontName = new UGooString(*fn);
      }
    }
  }
}

P2PCIDToGID *P2PFontFile::getCIDToGID(GBool wmode)
{
  P2PCIDToGID **p;

  if (wmode) {
    p = &CIDToGID_V;
  } else {
    p = &CIDToGID;
  }
  if (*p == 0) {
    *p = new P2PCIDToGID(this,wmode);
    P2PXRef::put(*p);
  }
  return *p;
}

P2PFontFile::~P2PFontFile()
{
  if (fileName != 0) {
    delete fileName;
  }
  if (charRefTable != 0) delete[] charRefTable;
  if (fontName != 0) delete fontName;
  /* font is deleted by P2PFontDict, you must not delete it here */
  /* must not delete CIDToGID */
}

GBool P2PFontFile::isIdentity()
{
  /* alway false now */
  return gFalse;
}

void P2PFontFile::outputCIDToGID(GBool wmode, P2POutputStream *str, XRef *xref)
{
#define N_UCS_CANDIDATES 2
  /* space characters */
  static unsigned long spaces[] = {
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,
    0x2008,0x2009,0x200A,0x00A0,0x200B,0x2060,0x3000,0xFEFF,
    0
  };
  static const char *adobe_cns1_cmaps[] = {
    "UniCNS-UTF32-V",
    "UniCNS-UCS2-V",
    "UniCNS-UTF32-H",
    "UniCNS-UCS2-H",
    0
  };
  static const char *adobe_gb1_cmaps[] = {
    "UniGB-UTF32-V",
    "UniGB-UCS2-V",
    "UniGB-UTF32-H",
    "UniGB-UCS2-H",
    0
  };
  static const char *adobe_japan1_cmaps[] = {
    "UniJIS-UTF32-V",
    "UniJIS-UCS2-V",
    "UniJIS-UTF32-H",
    "UniJIS-UCS2-H",
    0
  };
  static const char *adobe_japan2_cmaps[] = {
    "UniHojo-UTF32-V",
    "UniHojo-UCS2-V",
    "UniHojo-UTF32-H",
    "UniHojo-UCS2-H",
    0
  };
  static const char *adobe_korea1_cmaps[] = {
    "UniKS-UTF32-V",
    "UniKS-UCS2-V",
    "UniKS-UTF32-H",
    "UniKS-UCS2-H",
    0
  };
  static struct CMapListEntry {
    const char *collection;
    const char *scriptTag;
    const char *toUnicodeMap;
    const char **CMaps;
  } CMapList[] = {
    {
      "Adobe-CNS1",
      "kana",
      "Adobe-CNS1-UCS2",
      adobe_cns1_cmaps,
    },
    {
      "Adobe-GB1",
      "kana",
      "Adobe-GB1-UCS2",
      adobe_gb1_cmaps,
    },
    {
      "Adobe-Japan1",
      "kana",
      "Adobe-Japan1-UCS2",
      adobe_japan1_cmaps,
    },
    {
      "Adobe-Japan2",
      "kana",
      "Adobe-Japan2-UCS2",
      adobe_japan2_cmaps,
    },
    {
      "Adobe-Korea1",
      "kana",
      "Adobe-Korea1-UCS2",
      adobe_korea1_cmaps,
    },
    {
      0
    }
  };
  unsigned int i;
  Unicode *humap = 0;
  Unicode *vumap = 0;
  Unicode *tumap = 0;

  if (charRefTable == 0 || maxRefCID == 0) {
    str->putchar(0);
    str->putchar(0);
    return;
  }
  tfont.setupGSUB(0); /* reset GSUB */
  if (font != 0) {
    const char **cmapName;
    P2PCMap *cMap;
    CMapListEntry *lp;
    CharCodeToUnicode *octu;

    GfxCIDFont *cidfont = static_cast<GfxCIDFont *>(font);
    for (lp = CMapList;lp->collection != 0;lp++) {
      if (strcmp(lp->collection,cidfont->getCollection()->getCString()) == 0) {
	break;
      }
    }
    tumap = new Unicode[maxRefCID+1];
    if (lp->collection != 0) {
      GooString tname(lp->toUnicodeMap);
      P2PCharCodeToUnicode *ctu;
      if ((ctu = P2PCharCodeToUnicode::parseCMapFromFile(&tname,16)) != 0) {
	CharCode cid;
	for (cid = 0;cid <= maxRefCID ;cid++) {
	  int len;
	  Unicode ucodes[4];

	  len = ctu->mapToUnicode(cid,ucodes,4);
	  if (len == 1) {
	    tumap[cid] = ucodes[0];
	  } else {
	    /* if not single character, ignore it */
	    tumap[cid] = 0;
	  }
	}
	delete ctu;
      }
      vumap = new Unicode[maxRefCID+1];
      memset(vumap,0,sizeof(Unicode)*(maxRefCID+1));
      humap = new Unicode[(maxRefCID+1)*N_UCS_CANDIDATES];
      memset(humap,0,sizeof(Unicode)*(maxRefCID+1)*N_UCS_CANDIDATES);
      for (cmapName = lp->CMaps;*cmapName != 0;cmapName++) {
	GooString cname(*cmapName);

	if ((cMap = P2PCMap::parse(&cmapCache,cidfont->getCollection(),&cname))
	     != 0) {
	  if (cMap->getWMode()) {
	    cMap->setReverseMap(vumap,maxRefCID+1,1);
	  } else {
	    cMap->setReverseMap(humap,maxRefCID+1,N_UCS_CANDIDATES);
	  }
	  cMap->decRefCnt();
	}
      }
      tfont.setupGSUB(lp->scriptTag);
    } else {
      p2pError(-1,const_cast<char *>("Unknown character collection %s\n"),
        cidfont->getCollection()->getCString());
      if ((octu = cidfont->getToUnicode()) != 0) {
	CharCode cid;
	for (cid = 0;cid <= maxRefCID ;cid++) {
#ifdef OLD_MAPTOUNICODE
	  Unicode ucode;

	  humap[cid*N_UCS_CANDIDATES] = ucode;
#else
	  Unicode *ucode = NULL;

	  humap[cid*N_UCS_CANDIDATES] = *ucode;
#endif
	  for (i = 1;i < N_UCS_CANDIDATES;i++) {
	    humap[cid*N_UCS_CANDIDATES+i] = 0;
	  }
	}
	octu->decRefCnt();
      }
    }
  }
  for (i = 0;i <= maxRefCID;i++) {
    Unicode unicode;
    unsigned long gid;
    int j;

    if ((charRefTable[i/8] & (1 << (i & (8-1)))) == 0) {
      /* not referenced CID */
      gid = 0;
    } else {
      /* referenced CID */
      unicode = 0;
      gid = 0;
      if (humap != 0) {
	for (j = 0;j < N_UCS_CANDIDATES
	  && gid == 0 && (unicode = humap[i*N_UCS_CANDIDATES+j]) != 0;j++) {
	  gid = tfont.getGID(unicode,gFalse,
	    P2PDoc::options.fontEmbeddingWhole ? 1 : 0);
	}
      }
      if (gid == 0 && vumap != 0) {
	unicode = vumap[i];
	if (unicode != 0) {
	  gid = tfont.getGID(unicode,gTrue,
	    P2PDoc::options.fontEmbeddingWhole ? 1 : 0);
	  if (gid == 0 && tumap != 0) {
	    if ((unicode = tumap[i]) != 0) {
	      gid = tfont.getGID(unicode,gTrue,
		P2PDoc::options.fontEmbeddingWhole ? 1 : 0);
	    }
	  }
	}
      }
      if (gid == 0 && tumap != 0) {
	if ((unicode = tumap[i]) != 0) {
	    gid = tfont.getGID(unicode,gFalse,
	      P2PDoc::options.fontEmbeddingWhole ? 1 : 0);
	}
      }
      if (gid == 0) {
	/* special handling space characters */
	unsigned long *p;

	if (humap != 0) unicode = humap[i];
	if (unicode != 0) {
	  /* check if code is space character , so map code to 0x0020 */
	  for (p = spaces;*p != 0;p++) {
	    if (*p == unicode) {
	      unicode = 0x20;
	      gid = tfont.getGID(unicode,wmode,
	                P2PDoc::options.fontEmbeddingWhole ? 1 : 0);
	      break;
	    }
	  }
	}
      }
    }
    /* output GID 
       size is 16bits.
       should output big endian format.
   */
    str->putchar((gid >> 8) & 0xff);
    str->putchar(gid & 0xff);
  }
  if (humap != 0) delete[] humap;
  if (vumap != 0) delete[] vumap;
}

void P2PFontFile::output(P2POutputStream *str, XRef *xref)
{
  int fd;
  char buf[BUFSIZ];
  int n;
  P2PObj *lenObj = new P2PObj();
  P2PObj *len1Obj = 0;
  int cp;
  int len,len1 = 0;

//  fprintf(stderr,"INFO:Embedding font file from file %s\n",
//   fileName->getCString());
  outputBegin(str);
  /* output stream dictionary */
  str->puts("<< ");
  switch (type) {
  case fontType1C:
    str->puts("/SubType /Type1C ");
    /* should output Length1, Length2, Length3 */
    break;
  case fontCIDType0C:
    str->puts("/SubType /CIDFontType0C ");
    /* should output Length1, Length2, Length3 */
    break;
  case fontTrueType:
  case fontCIDType2:
    len1Obj = new P2PObj();
    P2PXRef::put(len1Obj);
    str->puts("/Length1 ");
    len1Obj->outputRef(str);
    break;
  case fontType1:
  case fontCIDType0:
    /* should output Length1, Length2, Length3 */
    break;
  default:
    break;
  }
  P2PXRef::put(lenObj);
  str->puts("/Length ");
  lenObj->outputRef(str);
  if (P2PDoc::options.fontCompress && str->canDeflate()) {
    str->puts(" /Filter /FlateDecode ");
  }
  str->puts(" >>\n");

  /* output stream body */
  str->puts("stream\n");
  if (P2PDoc::options.fontCompress) str->startDeflate();
  cp = str->getPosition();
  switch (type) {
  case fontCIDType2:
    tfont.output(str);
    len1 = tfont.getLength();
    break;
  case fontType1:
  case fontCIDType0:
  case fontType1C:
  case fontCIDType0C:
  case fontTrueType:
  default:
    if ((fd = open(fileName->getCString(),O_RDONLY)) < 0) {
      p2pError(-1,const_cast<char *>("Cannot open FontFile:%s\n"),fileName->getCString());
      return;
    }
    while ((n = read(fd,buf,BUFSIZ)) > 0) {
      char *p = buf;
      int r;

      while (n > 0) {
	r = str->write(p,n);
	if (r <= 0) break;
	n -= r;
	p += r;
      }
    }
    close(fd);
    break;
  }

  if (P2PDoc::options.fontCompress) str->endDeflate();
  len = str->getPosition()-cp; /* calculate length */
  str->puts("\nendstream\n");

  outputEnd(str);

  /* output length */
  lenObj->outputBegin(str);
  str->printf("%d\n",len);
  lenObj->outputEnd(str);

  if (len1Obj != 0) {
    /* output length1 */
    len1Obj->outputBegin(str);
    str->printf("%d\n",len1);
    len1Obj->outputEnd(str);
  }
}

P2PFontDescriptor::P2PFontDescriptor(Object *descriptorA,
  GfxFont *fontA, GooString *fileName, GfxFontType typeA,
  int faceIndexA, XRef *xref, int num, int gen) : P2PObject(num,gen)
{
  descriptorA->copy(&descriptor);
  fontFile = P2PFontFile::getFontFile(fileName,fontA,typeA,faceIndexA);
  type = typeA;
}

P2PFontDescriptor::~P2PFontDescriptor()
{
  /* fontFile is pointed by multiple FontDescriptor,
     so, don't delete it here.  it is deleted by P2PXRef::clean() */

  descriptor.free();
}

void P2PFontDescriptor::output(P2POutputStream *str, XRef *xref)
{
  P2PObject *objs[2];
  const char *keys[2];
  int objsIndex = 0;
  P2PObj fnobj;

  /* only indirect referenced */
  outputBegin(str);
  if (fontFile != 0) {
    UGooString *fn;

    switch (type) {
    case fontCIDType0:
    case fontType1:
    default:
      keys[objsIndex] = "FontFile";
      break;
    case fontCIDType2:
    case fontTrueType:
      keys[objsIndex] = "FontFile2";
      break;
    case fontType1C:
    case fontCIDType0C:
      keys[objsIndex] = "FontFile3";
      break;
    }
    objs[objsIndex++] = fontFile;
    fn = fontFile->getFontName();
    if (fn != 0) {
      char *p;

      p = fn->getCString();
      fnobj.getObj()->initName(p);
      keys[objsIndex] = "FontName";
      objs[objsIndex++] = &fnobj;
      delete[] p;
    }
  }
  P2POutput::outputDict(descriptor.getDict(),keys,objs,objsIndex,str,xref);
  str->putchar('\n');
  outputEnd(str);
}

P2PCIDFontDict::P2PCIDFontDict(Object *fontDictA, GfxFont *fontA,
  GfxFontType typeA, GfxFontType embeddedTypeA,
  P2PFontDescriptor *fontDescriptorA, int num, int gen) : P2PObject(num,gen)
{
  type = typeA;
  embeddedType = embeddedTypeA;
  fontDictA->copy(&fontDict);
  fontDescriptor = fontDescriptorA;
  font = fontA;
}

P2PCIDFontDict::~P2PCIDFontDict()
{
  fontDict.free();
  /* must not delete font, it is deleted by P2PFontDict::~P2PFonrDict(). */
}

void P2PCIDFontDict::output(P2POutputStream *str, XRef *xref)
{
  P2PObject *objs[3];
  const char *keys[3];
  int objsIndex = 0;
  P2PObj *subtypeObjp = 0;
  P2PObj fnobj;

  if (embeddedType != type) {
    /* change subtype */

    subtypeObjp = new P2PObj();
    switch (embeddedType) {
    case fontCIDType0:
      subtypeObjp->getObj()->initName(const_cast<char *>("CIDFontType0"));
      break;
    case fontCIDType2:
      subtypeObjp->getObj()->initName(const_cast<char *>("CIDFontType2"));
      break;
    case fontTrueType:
    case fontUnknownType:
    case fontType1:
    case fontType1C:
    case fontType3:
    case fontCIDType0C:
    default:
      p2pError(-1,const_cast<char *>("P2PCIDFontDict: Illegal embedded font type"));
      goto end_output;
      break;
    }
    keys[objsIndex] = "Subtype";
    objs[objsIndex++] = subtypeObjp;
  }
  if (embeddedType == fontCIDType2) {
    /* Add CIDToGIDMap */
    P2PObject *objp;
    P2PFontFile *fontFile;

    fontFile = fontDescriptor->getFontFile();
    if (fontFile != 0) {
      UGooString *fn;

      if (!(fontFile->isIdentity())) {
	keys[objsIndex] = "CIDToGIDMap";
	objp = fontFile->getCIDToGID(font->getWMode());
	objs[objsIndex++] = objp;
      }
      fn = fontFile->getFontName();
      if (fn != 0) {
	char *p = fn->getCString();
	fnobj.getObj()->initName(p);
	keys[objsIndex] = "BaseFont";
	objs[objsIndex++] = &fnobj;
	delete[] p;
      }
    }
  }

  outputBegin(str);
  P2POutput::outputDict(fontDict.getDict(),keys,objs,objsIndex,str,xref);
  str->putchar('\n');
  outputEnd(str);
end_output:
  if (subtypeObjp != 0) {
    delete subtypeObjp;
  }
}

P2PDescendantFontsWrapper::P2PDescendantFontsWrapper(P2PObject *elementA)
{
  element = elementA;
}

P2PDescendantFontsWrapper::~P2PDescendantFontsWrapper()
{
  /* do nothing */
}

void P2PDescendantFontsWrapper::output(P2POutputStream *str, XRef *xref)
{
  str->puts("[ ");
  element->outputRef(str);
  str->puts(" ]");
}

void P2PFontDict::doReadFontDescriptor(Object *dictObj,
  GfxFontType type, const char *name, XRef *xref)
{
  Dict *dict = dictObj->getDict();
  Object obj;
  int num, gen;
  P2PObject *p;
  int faceIndex = 0;

  if (dict->lookupNF(const_cast<char *>("FontDescriptor"),&obj) == 0
       || obj.isNull()) {
    /* no FontDescriptor */
    p2pError(-1,const_cast<char *>("Font:%s has no FontDescriptor entry.\n"),name);
    return;
  }
  if (!obj.isRef()) {
    /* not indirect reference is error */
    p2pError(-1,const_cast<char *>("FontDescriptor entry of Font:%s is not indirect.\n"),name);
    goto end_read;
  }
  num = obj.getRefNum();
  gen = obj.getRefGen();
  if ((p = P2PObject::find(num,gen)) != 0) {
    /* already read */
    fontDescriptor = static_cast<P2PFontDescriptor *>(p);
    embeddedType = fontDescriptor->getType();
  } else {
    P2PFontFile *fp;
    UGooString *ugs;
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
    GooString *fileName;

    SysFontType sftype;
    fileName = globalParams->findSystemFontFile(font,&sftype,
        &faceIndex, NULL);
    if (fileName == 0) {
      p2pError(-1, const_cast<char *>("Couldn't find a font for %s. Not embedded\n"),name);
      goto end_read;
    }
    switch (sftype) {
    case sysFontPFA:
    case sysFontPFB:
      p2pError(-1, const_cast<char *>("Found a Type1 font for font:%s. Embedding Type1 font not supported yet."),name);
      goto end_read;
      break;
    case sysFontTTF:
    case sysFontTTC:
      switch (type) {
      case fontCIDType2:
      case fontTrueType:
        break;
      case fontCIDType0C:
      case fontCIDType0:
        embeddedType = fontCIDType2;
        break;
      case fontType1:
      case fontType1C:
        embeddedType = fontTrueType;
        break;
      default:
        p2pError(-1, const_cast<char *>("Illegal type font\n"));
        goto end_read;
      }
      break;
    default:
      p2pError(-1, const_cast<char *>("found a unknown type font for %s. Not embedded\n"),name);
      goto end_read;
      break;
    }
#else
    GooString *fileName = font->getExtFontFile();

    if (fileName == 0) {
      DisplayFontParam *dfp = 0;
      /* look for substitute font */

      if (font->getName()) {
	dfp = globalParams->getDisplayFont(font);
	/* a caller must not delete dfp */
      }
      if (dfp == 0) {
	p2pError(-1, const_cast<char *>("Couldn't find a font for %s. Not embedded\n"),name);
	goto end_read;
      }
      switch (dfp->kind) {
      case displayFontT1:
	p2pError(-1, const_cast<char *>("Found a Type1 font for font:%s. Embedding Type1 font not supported yet."),name);
	goto end_read;
	break;
      case displayFontTT:
	switch (type) {
	case fontCIDType2:
	case fontTrueType:
          break;
	case fontCIDType0C:
	case fontCIDType0:
          embeddedType = fontCIDType2;
	  break;
	case fontType1:
	case fontType1C:
          embeddedType = fontTrueType;
	  break;
	default:
	  p2pError(-1, const_cast<char *>("Illegal type font\n"));
	  goto end_read;
	}
	fileName = dfp->tt.fileName;
	faceIndex = dfp->tt.faceIndex;
	break;
      default:
	p2pError(-1, const_cast<char *>("found a unknown type font for %s. Not embedded\n"),name);
	goto end_read;
	break;
      }
    }
#endif
    /* reset obj */
    obj.free();

    xref->fetch(num,gen,&obj);
    if (!obj.isDict()) {
	p2pError(-1, const_cast<char *>("Font Descriptor of Font:%s is not Dictionary. Not embedded\n"),name);
	goto end_read;
    }
//fprintf(stderr, "DEBUG: Embedding Font fileName=%s for %s\n",fileName->getCString(),name);
    fontDescriptor = new P2PFontDescriptor(&obj,font,fileName,
      embeddedType, faceIndex, xref,num,gen);
    P2PXRef::put(fontDescriptor);
    fp = fontDescriptor->getFontFile();
    ugs = 0;
    if (fp != 0) {
      ugs = fp->getFontName();
      if (ugs != 0) {
	char *cs = ugs->getCString();

	fprintf(stderr, "INFO: Embedded Font=%s, from fileName=%s for %s\n",
	  cs,fileName->getCString(),name);
	delete[] cs;
      }
    }
    if (ugs == 0) {
      fprintf(stderr, "INFO: Embedded Font from fileName=%s for %s\n",
	fileName->getCString(),name);
    }
  }
end_read:
  obj.free();
}

void P2PFontDict::read8bitFontDescriptor(GfxFontType type, const char *name,
  XRef *xref)
{
  doReadFontDescriptor(&fontDict,type,name,xref);
}

void P2PFontDict::readCIDFontDescriptor(GfxFontType type, const char *name,
  XRef *xref)
{
  Object obj;
  Object descendant;
  Dict *dict = fontDict.getDict();
  int num = -1, gen = -1;

  if (dict->lookup(const_cast<char *>("DescendantFonts"),&obj) == 0
       || obj.isNull()) {
    /* no DescendantFonts */
    p2pError(-1,const_cast<char *>("Font:%s has no DescendantFonts entry.\n"),name);
    return;
  }
  if (!obj.isArray() || obj.arrayGetNF(0,&descendant) == 0) {
    /* illegal DescendantFonts */
    p2pError(-1,const_cast<char *>("Font:%s has illegal DescendantFonts entry.\n"),name);
    goto end_read;
  }
  if (descendant.isRef()) {
    num = descendant.getRefNum();
    gen = descendant.getRefGen();

    cidFontDict = static_cast<P2PCIDFontDict *>(
      P2PObject::find(num,gen));
    if (cidFontDict == 0) {
      /* reset obj */
      descendant.free();
      xref->fetch(num,gen,&descendant);
    }
  }
  if (cidFontDict == 0) {
    if (!descendant.isDict()) {
      p2pError(-1,const_cast<char *>("Font:%s has illegal DescendantFonts entry.\n"),name);
      goto end_read1;
    }
    doReadFontDescriptor(&descendant,type,name,xref);
    cidFontDict = new P2PCIDFontDict(&descendant,font,type,
      embeddedType,fontDescriptor,num,gen);
    if (num > 0) P2PXRef::put(cidFontDict);
  }
end_read1:
  descendant.free();
end_read:
  obj.free();
}

UGooString *P2PFontDict::getEmbeddingFontName()
{
  P2PFontDescriptor *desc = fontDescriptor;

  if (cidFontDict != 0) {
    desc = cidFontDict->getFontDescriptor();
  }
  if (desc != 0) {
    P2PFontFile *file = desc->getFontFile();
    if (file != 0) return file->getFontName();
  }
  return 0;
}

void P2PFontDict::output(P2POutputStream *str, XRef *xref)
{
  int num, gen;
  P2PObject *objs[2];
  const char *keys[2];
  int nobjs = 0;
  UGooString *fn;
  P2PObj fnobj;

  fn = getEmbeddingFontName();
  if (fn != 0) {
    keys[nobjs] = "BaseFont";
    char *p = fn->getCString();
    fnobj.getObj()->initName(p);
    objs[nobjs] = &fnobj;
    nobjs++;
    delete[] p;
  }
  /* output indirect referenced dictionary only */
  outputBegin(str);
  if (cidFontDict != 0) {
    cidFontDict->getNum(&num,&gen);
    if (num < 0) {
      /* not indirect referenced, change it indirect referenced */
      P2PDescendantFontsWrapper *wrap
         = new P2PDescendantFontsWrapper(cidFontDict);
      keys[nobjs] = "DescendantFonts";
      objs[nobjs] = wrap;
      nobjs++;

      P2POutput::outputDict(fontDict.getDict(),keys,objs,nobjs,str,xref);
      delete wrap;
      P2PXRef::put(cidFontDict);
    } else {
      P2POutput::outputDict(fontDict.getDict(),keys,objs,nobjs,str,xref);
    }
  } else {
    if (embeddedType != font->getType()) {
      /* change subtype */
      P2PObj obj;
      keys[nobjs] = "Subtype";
      objs[nobjs] = &obj;
      nobjs++;

      switch (embeddedType) {
      case fontTrueType:
	obj.getObj()->initName(const_cast<char *>("TrueType"));
	P2POutput::outputDict(fontDict.getDict(),keys,objs,nobjs,str,xref);
	break;
      case fontType1:
	obj.getObj()->initName(const_cast<char *>("Type1"));
	P2POutput::outputDict(fontDict.getDict(),keys,objs,nobjs,str,xref);
	break;
      case fontCIDType2:
      case fontCIDType0:
      case fontCIDType0C:
	/* change is not needed */
	P2POutput::outputDict(fontDict.getDict(),keys,objs,nobjs,str,xref);
	break;
      case fontType1C:
      case fontUnknownType:
      case fontType3:
      default:
	p2pError(-1,const_cast<char *>("P2PFontDict: Illegal embedded font type"));
	return;
	break;
      }
    } else {
      P2POutput::outputDict(fontDict.getDict(),keys,objs,nobjs,str,xref);
    }
  }
  str->putchar('\n');
  outputEnd(str);
}

P2PFontDict::P2PFontDict(Object *fontDictA, XRef *xref, int num, int gen)
  : P2PObject(num,gen)
{
  Ref embID;
  Ref id;
  Dict *dict;
  Object name;
  GfxFontType type;

  font = 0;
  fontDescriptor = 0;
  cidFontDict = 0;
  if (fontDictA == 0 || !fontDictA->isDict()) return;
  fontDictA->copy(&fontDict);

  dict = fontDict.getDict();
  dict->lookup(const_cast<char *>("BaseFont"),&name);
  if (!name.isName()) {
    p2pError(-1,const_cast<char *>("FontDictionary has not name type BaseFont entry\n"));
    goto end_setup;
  }
  /* font id and tag are not used */
  if ((font = GfxFont::makeFont(xref,const_cast<char *>(""),
          id,fontDict.getDict())) == 0) {
    p2pError(-1,const_cast<char *>("Can't get font %s. Not embedded\n"),
      name.getName());
    goto end_setup;
  }
  embeddedType = type = font->getType();
  if (!font->getEmbeddedFontID(&embID)) {
    /* not embedded */
    int i;
    const char *namep = name.getName();

//fprintf(stderr,"DEBUG:%s is not embedded\n",name.getName());

    /* check builtin fonts */
    for (i = 0;i < nBuiltinFonts;i++) {
      if (strcmp(namep, builtinFonts[i].name) == 0) {
	/* built in font */
	/* don't embed built in font */
	fprintf(stderr,"INFO:%s is builtin font. not embedded.\n",namep);
	goto end_setup;
      }
    }

    if (!P2PDoc::options.fontEmbeddingPreLoad) {
      /* check pre-loaded fonts */
      for (i = 0;i < P2PDoc::options.numPreFonts;i++) {
	if (strcmp(namep, P2PDoc::options.preFonts[i]) == 0) {
	  /* pre-loaded font */
	  /* don't embed pre-loaded font */
	  fprintf(stderr,"INFO:%s is pre-loaded font. not embedded.\n",namep);
	  goto end_setup;
	}
      }
    }

    switch (type) {
    case fontType3:
      /* Type3 font is always embedded */
      /* do nothing */
      break;
    case fontTrueType:
    case fontUnknownType:
    case fontType1:
    case fontType1C:
#ifdef FONTTYPE_ENUM2
    case fontType1COT:
    case fontTrueTypeOT:
#endif
      /* 8bit font */
      read8bitFontDescriptor(type,name.getName(),xref);
      break;
    case fontCIDType0:
    case fontCIDType0C:
    case fontCIDType2:
#ifdef FONTTYPE_ENUM2
    case fontCIDType0COT:
    case fontCIDType2OT:
#endif
      /* CID font */
      readCIDFontDescriptor(type,name.getName(),xref);
      break;
    }
  } else {
    /* embedded */
  }
end_setup:
  name.free();
}

P2PFontDict::~P2PFontDict()
{
  if (font != 0) {
    font->decRefCnt();
  }
  /* fontDescriptor is pointed by multiple FontDict,
     so, don't delete it here.  it is deleted by P2PXRef::clean() */
  /* cidFontDict is pointed by multiple FontDict,
     so, don't delete it here.  it is deleted by P2PXRef::clean() */

  fontDict.free();
}

void P2PFontDict::showText(GooString *s)
{
  char *p;
  int len;
  int n;
#ifdef OLD_MAPTOUNICODE
  Unicode u[8];
#else
  Unicode *u = NULL;
#endif
  CharCode code;
  int uLen;
  double dx,dy,originX,originY;
  P2PFontFile *fontFile;

  if (font == 0 || fontDescriptor == 0) return;
  if ((fontFile = fontDescriptor->getFontFile()) == 0) return;
  p = s->getCString();
  len = s->getLength();
  while (len > 0) {
#ifdef OLD_MAPTOUNICODE
    n = font->getNextChar(p,len,&code,u,(sizeof(u)/sizeof(Unicode)),&uLen,
         &dx,&dy,&originX,&originY);
#else
    n = font->getNextChar(p,len,&code,&u,&uLen,
         &dx,&dy,&originX,&originY);
#endif
    code &= (CIDTOGID_SIZE-1); /* mask */
    fontFile->refChar(code);
    p += n;
    len -= n;
  }
}

P2PFontResource::P2PFontResource()
{
  keys = 0;
  fontDicts = 0;
  nDicts = 0;
  nExtGState = 0;
  extGStateKeys = 0;
  extGStateFonts = 0;
}

P2PFontResource::~P2PFontResource()
{
  int i;

  if (keys != 0) {
    for (i = 0;i < nDicts;i++) {
      delete keys[i];
    }
    delete[] keys;
  }
  if (fontDicts != 0) {
    for (i = 0;i < nDicts;i++) {
      if (fontDicts[i] != 0) {
	int num, gen;

	fontDicts[i]->getNum(&num,&gen);
	if (num < 0) {
	  /* Not indirect referenced FontDict */
	  /* output it as normal dictionary */
	  /* so, not needed any more */
	  delete fontDicts[i];
	}
	/* indirect referenced FontDict is registered in P2PXRef */
	/* So, should not delete it. */
      }
    }
    delete[] fontDicts;
  }
  if (extGStateKeys != 0) {
    for (i = 0;i < nExtGState;i++) {
      delete extGStateKeys[i];
    }
    delete[] extGStateKeys;
  }
  if (extGStateFonts != 0) {
    /* indirect referenced FontDict is registered in P2PXRef */
    /* So, should not delete it. */
    delete[] extGStateFonts;
  }
}

void P2PFontResource::setup(P2PResources *resources, XRef *xref)
{
  doSetup(resources->getFontResource(),xref);
  doSetupExtGState(resources->getExtGState(),xref);
}

void P2PFontResource::setup(Dict *resources, XRef *xref)
{
  Object obj;

  if (resources == 0) return;
  resources->lookup(const_cast<char *>("Font"),&obj);
  if (obj.isDict()) {
    doSetup(obj.getDict(),xref);
  }
  obj.free();
  resources->lookup(const_cast<char *>("ExtGState"),&obj);
  if (obj.isDict()) {
    doSetupExtGState(obj.getDict(),xref);
  }
  obj.free();
}

void P2PFontResource::doSetup(Dict *fontResource, XRef *xref)
{
  int i;
  P2PFontDict *p = 0;

  if (fontResource == 0) return;
  nDicts = fontResource->getLength();
  keys = new UGooString *[nDicts];
  fontDicts = new P2PFontDict *[nDicts];
  for (i = 0;i < nDicts;i++) {
    Object obj;
#ifdef HAVE_UGOOSTRING_H
    UGooString *key = fontResource->getKey(i);
#else
    UGooString *key = new UGooString(fontResource->getKey(i));
#endif

    fontResource->getValNF(i,&obj);
    if (obj.isRef()) {
      int num = obj.getRefNum();
      int gen = obj.getRefGen();
      if ((p = static_cast<P2PFontDict *>(P2PObject::find(num,gen))) == 0) {
	Object fobj;

	xref->fetch(num,gen,&fobj);
	if (fobj.isDict()) {
	  p = new P2PFontDict(&fobj,xref,num,gen);
	  /* register this in P2PXRef to output later */
	  P2PXRef::put(p);
	}
	fobj.free();
      }
    }
    if (p != 0) {
      keys[i] = new UGooString(*key);
      fontDicts[i] = p;
    } else if (obj.isDict()) {
      keys[i] = new UGooString(*key);
      fontDicts[i] = new P2PFontDict(&obj,xref);
      P2PXRef::put(fontDicts[i]);
    } else {
      keys[i] = 0;
      fontDicts[i] = 0;
    }
    obj.free();
  }
}

void P2PFontResource::doSetupExtGState(Dict *extGState, XRef *xref)
{
  int i;
  P2PFontDict *p = 0;

  if (extGState == 0) return;
  nExtGState = extGState->getLength();
  extGStateKeys = new UGooString *[nExtGState];
  extGStateFonts = new P2PFontDict *[nExtGState];
  for (i = 0;i < nExtGState;i++) {
    Object gstateObj;
#ifdef HAVE_UGOOSTRING_H
    UGooString *key = extGState->getKey(i);
#else
    char *key = extGState->getKey(i);
#endif

    extGStateKeys[i] = 0;
    extGStateFonts[i] = 0;
    extGState->getVal(i,&gstateObj);
    if (gstateObj.isDict()) {
      Object fontArrayObj;
      Dict *gstate = gstateObj.getDict();

      if (gstate->lookup(const_cast<char *>("Font"),&fontArrayObj) != 0) {
	if (fontArrayObj.isArray() && fontArrayObj.arrayGetLength() > 0) {
	  Object fontRefObj;

	  if (fontArrayObj.arrayGetNF(0,&fontRefObj) && fontRefObj.isRef()) {
	    int num = fontRefObj.getRefNum();
	    int gen = fontRefObj.getRefGen();

	    if ((p = static_cast<P2PFontDict *>(P2PObject::find(num,gen)))
	         == 0) {
	      Object fontObj;
	      xref->fetch(num,gen,&fontObj);
	      if (fontObj.isDict()) {
		p = new P2PFontDict(&fontObj,xref,num,gen);
		/* register this in P2PXRef to output later */
		P2PXRef::put(p);
	      }
	      fontObj.free();
	    }
	  }
	  fontRefObj.free();
	}
#ifdef HAVE_UGOOSTRING_H
	extGStateKeys[i] = new UGooString(*key);
#else
	extGStateKeys[i] = new UGooString(key);
#endif
	extGStateFonts[i] = p;
      }
      fontArrayObj.free();
    }
    gstateObj.free();
  }
}

void P2PFontResource::output(P2POutputStream *str, XRef *xref)
{
  int i;

  str->puts("<< ");
  for (i = 0;i < nDicts;i++) {
    P2POutput::outputName(keys[i]->getCString(),str);
    str->putchar(' ');
    fontDicts[i]->outputRef(str);
    str->putchar('\n');
  }
  str->puts(" >>");
}

P2PFontDict *P2PFontResource::lookup(const UGooString &key)
{
  int i;

  for (i = 0;i < nDicts;i++) {
    if (key.cmp(keys[i]) == 0) {
      return fontDicts[i];
    }
  }
  return 0;
}

P2PFontDict *P2PFontResource::lookupExtGState(const UGooString &key)
{
  int i;

  for (i = 0;i < nExtGState;i++) {
    if (extGStateKeys != 0 && key.cmp(extGStateKeys[i]) == 0) {
      return extGStateFonts[i];
    }
  }
  return 0;
}

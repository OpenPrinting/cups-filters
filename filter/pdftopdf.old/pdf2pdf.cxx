/*
Copyright (c) 2007-2009, BBR Inc.  All rights reserved.

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
 pdf2pdf.cc
 pdf to pdf filter, utilty version
*/

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include "P2PError.h"
#include "goo/GooString.h"
#include "goo/gmem.h"
#include "Object.h"
#include "Stream.h"
#include "PDFDoc.h"
#include "P2PDoc.h"
#include "P2POutputStream.h"
#include "P2PError.h"
#include "GlobalParams.h"
#include "parseargs.h"
#include "PDFFTrueTypeFont.h"

namespace {
  int exitCode = 0;
  GBool fitplot = gFalse;
  GBool mirror = gFalse;
  int numberUp = 1;
  char numberUpLayoutStr[5] = "";
  unsigned int numberUpLayout = PDFTOPDF_LAYOUT_LRTB;
  char pageBorderStr[15] = "";
  char pageSetStr[5] = "";
  char pageRangesStr[255] = "";
  unsigned int pageBorder = PDFTOPDF_BORDERNONE;
  int margin = 0;
  int aMargin = -1;
  double pageLeft = 0;
  double pageRight = 612.0;
  double pageBottom = 0;
  double pageTop = 792.0;
  double pageWidth = 612.0;
  double pageLength = 792.0;
  int paperWidth = 0;
  int paperHeight = 0;
  int xposition = 0;
  int yposition = 0;
  GBool position = gFalse;
  char positionStr[15] = "";
  int orientation = 0;
  double scaling = 1.0;
  int aScaling = -1;
  double naturalScaling = 1.0;
  int aNaturalScaling = -1;
  GBool printHelp = gFalse;
  GBool printVersion = gFalse;
  GBool copies = 1;
  GBool landscape = gFalse;
  char paperSize[15] = "";
  int leftMargin = -1;
  int rightMargin = -1;
  int bottomMargin = -1;
  int topMargin = -1;
  char ownerPassword[33] = "\001";
  char userPassword[33] = "\001";
  GBool reverseOrder = gFalse;
  GBool collate = gFalse;
  GBool fontEmbedding = gFalse;
  GBool fontNoCompress = gFalse;
  GBool contentNoCompress = gFalse;
  GooString *ownerPW = NULL;
  GooString *userPW = NULL;
};

struct Paper {
  const char *name;
  double width;
  double height;
} papers[] = {
  {"Letter", 612, 792},
  {"Legal", 612, 1008},
  {"Executive", 522, 757},
  {"A5", 421, 595},
  {"A4", 595, 842},
  {"A3", 842, 1191},
  {"A2", 1191, 1684},
  {"A1", 1684, 2384},
  {"A0", 2384, 3370},
  {"B5", 516, 729},
  {"B4", 729, 1032},
  {"B3", 1032, 1460},
  {"B2", 1460, 2064},
  {"B1", 2064, 2920},
  {"B0", 2920, 4127},
  {"Postcard", 283, 421},
  {NULL}
};


static ArgDesc argDesc[] = {
  {const_cast<char *>("-c"),	argInt,		&copies,	0,
    const_cast<char *>("number of copies")},
  {const_cast<char *>("-copies"),	argInt,		&copies,	0,
    const_cast<char *>("number of copies")},
  {const_cast<char *>("-fitplot"),argFlag,		&fitplot,	0,
    const_cast<char *>("fit original page to new page region")},
  {const_cast<char *>("-number-up"),argInt,		&numberUp,	0,
    const_cast<char *>("number up (1,2,4,6,8,9,16)")},
  {const_cast<char *>("-number-up-layout"),	argString,numberUpLayoutStr,sizeof(numberUpLayoutStr),
    const_cast<char *>("number up layout (lrtb,lrbt,rltb,rlbt,tblr,tbrl,btlr,btrl)")},
  {const_cast<char *>("-reverse"),	argFlag,	&reverseOrder,	0,
    const_cast<char *>("reverse output order")},
  {const_cast<char *>("-mirror"),	argFlag,	&mirror,	0,
    const_cast<char *>("mirror image")},
  {const_cast<char *>("-collate"),	argFlag,	&collate,	0,
    const_cast<char *>("collate")},
  {const_cast<char *>("-scaling"),argInt,	&aScaling,	0,
    const_cast<char *>("scaling, in %")},
  {const_cast<char *>("-natural-scaling"),argInt, &aNaturalScaling,	0,
    const_cast<char *>("natural scaling, in %")},
  {const_cast<char *>("-page-border"),argString, pageBorderStr,	sizeof(pageBorderStr),
    const_cast<char *>("page border (none,single,single-thick,double,double-thick)")},
  {const_cast<char *>("-landscape"),argFlag,	&landscape,	0,
    const_cast<char *>("landscape")},
  {const_cast<char *>("-orientation"),argInt,	&orientation,	0,
    const_cast<char *>("orientation (0,1,2,3)")},
  {const_cast<char *>("-paper"),	argString,	paperSize,	sizeof(paperSize),
    const_cast<char *>("paper size (letter, legal, A0 ... A5, B0 ... B5)")},
  {const_cast<char *>("-paperw"),	argInt,		&paperWidth,	0,
    const_cast<char *>("paper width, in points")},
  {const_cast<char *>("-paperh"),	argInt,		&paperHeight,	0,
    const_cast<char *>("paper height, in points")},
  {const_cast<char *>("-margin"),	argInt,		&aMargin,	0,
    const_cast<char *>("paper margin, in points")},
  {const_cast<char *>("-page-left"),argInt,		&leftMargin,	0,
    const_cast<char *>("left margin, in points")},
  {const_cast<char *>("-page-right"),argInt,	&rightMargin,	0,
    const_cast<char *>("right margin, in points")},
  {const_cast<char *>("-page-bottom"),argInt,	&bottomMargin,	0,
    const_cast<char *>("bottom margin, in points")},
  {const_cast<char *>("-page-top"),	argInt,		&topMargin,	0,
    const_cast<char *>("top margin, in points")},
  {const_cast<char *>("-page-set"), argString,	pageSetStr, sizeof(pageSetStr),
    const_cast<char *>("page set (odd or even)")},
  {const_cast<char *>("-page-ranges"),argString, pageRangesStr,	sizeof(pageRangesStr),
    const_cast<char *>("page ranges (Ex. 2-4,8)")},
  {const_cast<char *>("-font-embedding"),argFlag,	&fontEmbedding,	0,
    const_cast<char *>("embedding fonts")},
  {const_cast<char *>("-no-compressing-font"),argFlag,&fontNoCompress,0,
    const_cast<char *>("not compressing embedding fonts")},
  {const_cast<char *>("-no-compressing-contents"),argFlag,&contentNoCompress,0,
    const_cast<char *>("not compressing page contents")},
  {const_cast<char *>("-position"),argString, positionStr, sizeof(positionStr),
    const_cast<char *>("page position (center,top,left,right,top-left,top-right,bottom,bottom-left,bottom-right)\n")},
  {const_cast<char *>("-opw"),	argString, ownerPassword, sizeof(ownerPassword),
    const_cast<char *>("owner password (for encrypted files)")},
  {const_cast<char *>("-upw"), argString, userPassword,	sizeof(userPassword),
    const_cast<char *>("user password (for encrypted files)")},
  {const_cast<char *>("-v"), argFlag, &printVersion, 0,
    const_cast<char *>("print version info")},
  {const_cast<char *>("-h"),	argFlag,		&printHelp,	0,
    const_cast<char *>("print usage information")},
  {const_cast<char *>("-help"),	argFlag,		&printHelp,	0,
    const_cast<char *>("print usage information")},
  {const_cast<char *>("--help"), argFlag, &printHelp, 0,
    const_cast<char *>("print usage information")},
  {const_cast<char *>("--?"), argFlag, &printHelp, 0,
    const_cast<char *>("print usage information")},
  {NULL}
};

#ifdef ERROR_HAS_A_CATEGORY
void CDECL myErrorFun(void *data, ErrorCategory category,
    int pos, char *msg)
{
  if (pos >= 0) {
    fprintf(stderr, "ERROR (%d): ", pos);
  } else {
    fprintf(stderr, "ERROR: ");
  }
  fprintf(stderr, "%s\n",msg);
  fflush(stderr);
}
#else
void CDECL myErrorFun(int pos, char *msg, va_list args)
{
  if (pos >= 0) {
    fprintf(stderr, "ERROR (%d): ", pos);
  } else {
    fprintf(stderr, "ERROR: ");
  }
  vfprintf(stderr, msg, args);
  fprintf(stderr, "\n");
  fflush(stderr);
}
#endif
Paper *getPaper(char *name)
{
  for (Paper *p = papers;p->name != NULL;p++) {
    if (strcasecmp(name,p->name) == 0) {
      return p;
    }
  }
  return NULL;
}

void parseOpts(int *argc, char **argv)
{
  GBool ok;
  Paper *ppaper;

  ok = parseArgs(argDesc,argc,argv);
  if (!ok || *argc < 1 || *argc > 3 || printVersion || printHelp) {
    fprintf(stderr, "pdf2pdf version %s\n", VERSION);
    if (!printVersion) {
      printUsage(argv[0],
        const_cast<char *>("[<input PDF-file> [<output PDF-file>]]"), argDesc);
    }
    exit(1);
  }

  if (copies <= 0) {
    fprintf(stderr,"number of copies should be plus number\n");
    exit(1);
  }
  P2PDoc::options.copies = copies;

  /* paper size */
  if (paperSize[0] != '\0') {
    if ((ppaper = getPaper(paperSize)) == NULL) {
      fprintf(stderr,"Unknown paper\n");
      exit(1);
    }
    pageWidth = ppaper->width;
    pageLength = ppaper->height;
    position = gTrue;
  }
  if (paperWidth < 0) {
    fprintf(stderr,"paper width should be plus\n");
    exit(1);
  }
  if (paperWidth > 0) {
    pageWidth = paperWidth;
    position = gTrue;
  }
  if (paperHeight < 0) {
    fprintf(stderr,"paper height should be plus\n");
    exit(1);
  }
  if (paperHeight > 0) {
    pageLength = paperHeight;
    position = gTrue;
  }
  /* paper margin */
  if (aMargin >= 0) {
    margin = aMargin;
    position = gTrue;
  }
  pageLeft = margin;
  pageRight = pageWidth - margin; 
  pageBottom = margin;
  pageTop = pageLength - margin;
  if (landscape) {
    orientation = 1;
  }
  if (orientation < 0 || orientation > 3) {
    fprintf(stderr,"orientation value should be one of (0,1,2,3)\n");
    exit(1);
  }

  if (leftMargin >= 0) {
    switch (orientation & 3) {
      case 0 :
          pageLeft = leftMargin;
          break;
      case 1 :
          pageBottom = leftMargin;
          break;
      case 2 :
          pageRight = pageWidth - leftMargin;
          break;
      case 3 :
          pageTop = pageLength - leftMargin;
          break;
    }
    position = gTrue;
  }
  if (rightMargin >= 0) {
    switch (orientation & 3) {
      case 0 :
          pageRight = pageWidth - rightMargin;
          break;
      case 1 :
          pageTop = pageLength - rightMargin;
          break;
      case 2 :
          pageLeft = rightMargin;
          break;
      case 3 :
          pageBottom = rightMargin;
          break;
    }
    position = gTrue;
  }
  if (bottomMargin >= 0) {
    switch (orientation & 3) {
      case 0 :
          pageBottom = bottomMargin;
          break;
      case 1 :
          pageLeft = bottomMargin;
          break;
      case 2 :
          pageTop = pageLength - bottomMargin;
          break;
      case 3 :
          pageRight = pageWidth - bottomMargin;
          break;
    }
    position = gTrue;
  }
  if (topMargin >= 0) {
    switch (orientation & 3) {
      case 0 :
          pageTop = pageLength - topMargin;
          break;
      case 1 :
          pageRight = pageWidth - topMargin;
          break;
      case 2 :
          pageBottom = topMargin;
          break;
      case 3 :
          pageLeft = topMargin;
          break;
    }
    position = gTrue;
  }

  switch (numberUp) {
    case 1 :
    case 2 :
    case 4 :
    case 6 :
    case 8 :
    case 9 :
    case 16 :
	break;
    default :
	fprintf(stderr,
		"Unsupported number-up value %d, should be one of (1,2,4,6,8,16)\n",
		numberUp);
	exit(1);
	break;
  }
  if (numberUpLayoutStr[0] != '\0') {
    if (!strcasecmp(numberUpLayoutStr,"lrtb")) {
      numberUpLayout = PDFTOPDF_LAYOUT_LRTB;
    } else if (!strcasecmp(numberUpLayoutStr,"lrbt")) {
      numberUpLayout = PDFTOPDF_LAYOUT_LRBT;
    } else if (!strcasecmp(numberUpLayoutStr,"rltb")) {
      numberUpLayout = PDFTOPDF_LAYOUT_RLTB;
    } else if (!strcasecmp(numberUpLayoutStr,"rlbt")) {
      numberUpLayout = PDFTOPDF_LAYOUT_RLBT;
    } else if (!strcasecmp(numberUpLayoutStr,"tblr")) {
      numberUpLayout = PDFTOPDF_LAYOUT_TBLR;
    } else if (!strcasecmp(numberUpLayoutStr,"tbrl")) {
      numberUpLayout = PDFTOPDF_LAYOUT_TBRL;
    } else if (!strcasecmp(numberUpLayoutStr,"btlr")) {
      numberUpLayout = PDFTOPDF_LAYOUT_BTLR;
    } else if (!strcasecmp(numberUpLayoutStr,"btrl")) {
      numberUpLayout = PDFTOPDF_LAYOUT_BTRL;
    } else {
      fprintf(stderr, "Unsupported number-up-layout value %s,\n"
       "  should be one of (lrtb,lrbt,rltb,rlbt,tblr,tbrl,btlr,btrl)\n",
       numberUpLayoutStr);
      exit(1);
    }
  }
  if (reverseOrder) {
    P2PDoc::options.reverse = gTrue;
  }
  if (pageBorderStr[0] != '\0') {
    if (!strcasecmp(pageBorderStr,"none")) {
      pageBorder = PDFTOPDF_BORDERNONE;
    } else if (!strcasecmp(pageBorderStr,"single")) {
      pageBorder = PDFTOPDF_BORDERHAIRLINE;
    } else if (!strcasecmp(pageBorderStr,"single-thick")) {
      pageBorder = PDFTOPDF_BORDERTHICK;
    } else if (!strcasecmp(pageBorderStr,"double")) {
      pageBorder = PDFTOPDF_BORDERDOUBLE | PDFTOPDF_BORDERHAIRLINE;
    } else if (!strcasecmp(pageBorderStr,"double-thick")) {
      pageBorder = PDFTOPDF_BORDERDOUBLE | PDFTOPDF_BORDERTHICK;
    } else {
      fprintf(stderr, "Unsupported page-border value %s,\n"
        "  should be one of (none,single,single-thick,double,double-thick)\n",
	pageBorderStr);
      exit(1);
    }
  }
  if (pageSetStr[0] != '\0') {
    if (strcasecmp(pageSetStr,"even") != 0 &&
       strcasecmp(pageSetStr,"odd") != 0) {
      fprintf(stderr, "Page set should be odd or even\n");
      exit(1);
    }
    P2PDoc::options.pageSet = pageSetStr;
  }
  if (pageRangesStr[0] != '\0') {
    P2PDoc::options.pageRanges = pageRangesStr;
  }
  if (positionStr[0] != '\0') {
    if (strcasecmp(positionStr,"center") == 0) {
      xposition = 0;
      yposition = 0;
    } else if (strcasecmp(positionStr,"top") == 0) {
      xposition = 0;
      yposition = 1;
    } else if (strcasecmp(positionStr,"left") == 0) {
      xposition = -1;
      yposition = 0;
    } else if (strcasecmp(positionStr,"right") == 0) {
      xposition = 1;
      yposition = 0;
    } else if (strcasecmp(positionStr,"top-left") == 0) {
      xposition = -1;
      yposition = 1;
    } else if (strcasecmp(positionStr,"top-right") == 0) {
      xposition = 1;
      yposition = 1;
    } else if (strcasecmp(positionStr,"bottom") == 0) {
      xposition = 0;
      yposition = -1;
    } else if (strcasecmp(positionStr,"bottom-left") == 0) {
      xposition = -1;
      yposition = -1;
    } else if (strcasecmp(positionStr,"bottom-right") == 0) {
      xposition = 1;
      yposition = -1;
    } else {
      fprintf(stderr, "Unsupported postion value %s,\n"
        "  should be one of (center,top,left,right,top-left,top-right,bottom,bottom-left,bottom-right).\n",
	positionStr);
      exit(1);
    }
    position = gTrue;
  }

  if (collate) {
    P2PDoc::options.collate = gTrue;
  }

  if (aScaling > 0) {
    scaling = aScaling * 0.01;
    fitplot = gTrue;
  } else if (fitplot) {
    scaling = 1.0;
  }
  if (aNaturalScaling > 0) {
    naturalScaling = aNaturalScaling * 0.01;
  }

  /* embedding fonts into output PDF */
  P2PDoc::options.fontEmbedding = fontEmbedding;
  P2PDoc::options.fontCompress = !fontNoCompress;
  P2PDoc::options.contentsCompress = !contentNoCompress;

  if (P2PDoc::options.copies == 1) {
    /* collate is not needed */
    P2PDoc::options.collate = gFalse;
  }

  if (ownerPassword[0] != '\001') {
    ownerPW = new GooString(ownerPassword);
  }
  if (userPassword[0] != '\001') {
    userPW = new GooString(userPassword);
  }
}

int main(int argc, char *argv[]) {
  PDFDoc *doc;
  P2PDoc *p2pdoc;
  P2POutputStream *str;
  FILE *outfp;

#ifdef ERROR_HAS_A_CATEGORY
  setErrorCallback(::myErrorFun,NULL);
#else
  setErrorFunction(::myErrorFun);
#endif
  parseOpts(&argc, argv);
#ifdef GLOBALPARAMS_HAS_A_ARG
  globalParams = new GlobalParams(0);
#else
  globalParams = new GlobalParams();
#endif

  PDFRectangle box(pageLeft,pageBottom,pageRight,pageTop);
  PDFRectangle mediaBox(0,0,pageWidth,pageLength);

  if (argc >=3 && strcmp(argv[2],"-") != 0) {
    if ((outfp = fopen(argv[2],"wb")) == NULL) {
      p2pError(-1,const_cast<char *>("Can't open output file:%s"),argv[3]);
      exit(1);
    }
  } else {
    outfp = stdout;
  }
  if (argc <= 1 || strcmp(argv[1],"-") == 0) {
    /* stdin */
    Object obj;
    BaseStream *str;
    FILE *fp;
    char buf[BUFSIZ];
    unsigned int n;

    fp = tmpfile();
    if (fp == NULL) {
      p2pError(-1,const_cast<char *>("Can't create temporary file"));
      exit(1);
    }

    /* copy stdin to the tmp file */
    while ((n = fread(buf,1,BUFSIZ,stdin)) > 0) {
      if (fwrite(buf,1,n,fp) != n) {
        p2pError(-1,const_cast<char *>("Can't copy stdin to temporary file"));
        fclose(fp);
	exit(1);
      }
    }
    rewind(fp);

    obj.initNull();
    str = new FileStream(fp,0,gFalse,0,&obj);
    doc = new PDFDoc(str,ownerPW,userPW);
  } else {
    GooString *fileName = new GooString(argv[1]);
    /* input filenmae is specified */
    doc = new PDFDoc(fileName,ownerPW,userPW);
  }
  if (!doc->isOk()) {
    exitCode = 1;
    goto err1;
  }
  if (!doc->okToChange() && !doc->okToAssemble()) {
    p2pError(-1,const_cast<char *>("Neither Changing, nor Assembling is allowed\n"));
    exit(1);
  }
  p2pdoc = new P2PDoc(doc);
  if (mirror) {
    p2pdoc->mirror();
  }

  if (orientation != 0) {
    p2pdoc->rotate(orientation);
  }

  if (naturalScaling != 1.0) {
    p2pdoc->scale(naturalScaling);
    p2pdoc->position(&box,xposition,yposition);
  }

  if (fitplot) {
    p2pdoc->fit(&box,scaling);
    p2pdoc->position(&box,xposition,yposition);
  }
  if (numberUp != 1) {
    p2pdoc->nup(numberUp,&box,pageBorder,numberUpLayout,
      xposition,yposition);
  } else if (position) {
    p2pdoc->position(&box,xposition,yposition);
  }

  if (orientation != 0 || naturalScaling != 1.0 || fitplot
      || numberUp != 1 || position) {
    /* When changing geometry, 
       set all pages's mediaBox to the target page size */
    p2pdoc->setMediaBox(&mediaBox);
  }

  p2pdoc->select();

  if (P2PDoc::options.collate
      && p2pdoc->getNumberOfPages() == 1
      && !P2PDoc::options.even) {
    /* collate is not needed, disable it */
    /* Number of pages is changed by nup and page-ranges,
	so check this here */
    P2PDoc::options.collate = gFalse;
  }


  str = new P2POutputStream(outfp); /* PDF start here */
  p2pdoc->output(str);

  delete str;
  delete p2pdoc;
err1:
  delete doc;

  // Check for memory leaks
  Object::memCheck(stderr);
  gMemReport(stderr);

  return exitCode;
}

/* replace memory allocation methods for memory check */

void * operator new(size_t size)
{
  return gmalloc(size);
}

void operator delete(void *p)
{
  gfree(p);
}

void * operator new[](size_t size)
{
  return gmalloc(size);
}

void operator delete[](void *p)
{
  gfree(p);
}

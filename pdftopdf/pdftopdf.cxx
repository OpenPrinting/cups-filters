/*
Copyright (c) 2006-2011, BBR Inc.  All rights reserved.

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
 pdftopdf.cc
 pdf to pdf filter
*/

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include "goo/GooString.h"
#include "goo/gmem.h"
#include "Object.h"
#include "Stream.h"
#include "PDFDoc.h"
#include "P2PDoc.h"
#include "P2POutputStream.h"
#include <cups/cups.h>
#include <cups/ppd.h>
#include <stdarg.h>
#include "Error.h"
#include "GlobalParams.h"
#include "PDFFTrueTypeFont.h"

namespace {
  int exitCode = 0;
  GBool fitplot = gFalse;
  GBool mirror = gFalse;
  int numberUp = 1;
  unsigned int numberUpLayout = PDFTOPDF_LAYOUT_LRTB;
  unsigned int pageBorder = PDFTOPDF_BORDERNONE;
  double pageLeft = 18.0;
  double pageRight = 594.0;
  double pageBottom = 36.0;
  double pageTop = 756.0;
  double pageWidth = 612.0;
  double pageLength = 792.0;
  GBool emitJCL = gTrue;
  ppd_file_t *ppd = 0;
  int xposition = 0;
  int yposition = 0;
  GBool position = gFalse;
  int orientation = 0;
  double scaling = 1.0;
  double naturalScaling = 1.0;
  int deviceCopies = 1;
  GBool deviceCollate = gFalse;
  GBool deviceReverse = gFalse;
  GBool autoRotate = gTrue;
  GBool forcePageSize = gFalse;
};

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

GBool checkFeature(const char *feature, int num_options, cups_option_t *options)
{
  const char *val;
  ppd_attr_t *attr;

  return ((val = cupsGetOption(feature,num_options,options)) != 0 &&
             (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
               !strcasecmp(val, "yes"))) ||
         ((attr = ppdFindAttr(ppd,feature,0)) != 0 &&
             (!strcasecmp(attr->value, "true")
	       || !strcasecmp(attr->value, "on") ||
               !strcasecmp(attr->value, "yes")));
}

void emitJCLOptions(FILE *fp, int copies)
{
  int section;
  ppd_choice_t **choices;
  int i;
  char buf[1024];
  ppd_attr_t *attr;
  int pdftoopvp = 0;
  int datawritten = 0;

  if (ppd == 0) return;
  if ((attr = ppdFindAttr(ppd,"pdftopdfJCLBegin",NULL)) != NULL) {
    int n = strlen(attr->value);
    pdftoopvp = 1;
    for (i = 0;i < n;i++) {
	if (attr->value[i] == '\r' || attr->value[i] == '\n') {
	    /* skip new line */
	    continue;
	}
	fputc(attr->value[i],fp);
	datawritten = 1;
    }
  }
         
  snprintf(buf,sizeof(buf),"%d",copies);
  if (ppdFindOption(ppd,"Copies") != NULL) {
    ppdMarkOption(ppd,"Copies",buf);
  } else {
    if ((attr = ppdFindAttr(ppd,"pdftopdfJCLCopies",buf)) != NULL) {
      fputs(attr->value,fp);
      datawritten = 1;
    } else if (pdftoopvp) {
      fprintf(fp,"Copies=%d;",copies);
      datawritten = 1;
    }
  }
  for (section = (int)PPD_ORDER_ANY;
      section <= (int)PPD_ORDER_PROLOG;section++) {
    int n;

    n = ppdCollect(ppd,(ppd_section_t)section,&choices);
    for (i = 0;i < n;i++) {
      snprintf(buf,sizeof(buf),"pdftopdfJCL%s",
        ((ppd_option_t *)(choices[i]->option))->keyword);
      if ((attr = ppdFindAttr(ppd,buf,choices[i]->choice)) != NULL) {
	fputs(attr->value,fp);
	datawritten = 1;
      } else if (pdftoopvp) {
	fprintf(fp,"%s=%s;",
	  ((ppd_option_t *)(choices[i]->option))->keyword,
	  choices[i]->choice);
	datawritten = 1;
      }
    }
  }
  if (datawritten) fputc('\n',fp);
}

void parseOpts(int argc, char **argv)
{
  int num_options;
  cups_option_t *options;
  const char *val;
  ppd_attr_t *attr;
  ppd_choice_t *choice;
  ppd_size_t *pagesize;
  int intval;

  if (argc < 6 || argc > 7) {
    error(-1,const_cast<char *>("%s job-id user title copies options [file]"),
      argv[0]);
    exit(1);
  }
  P2PDoc::options.jobId = atoi(argv[1]);
  P2PDoc::options.user = argv[2];
  P2PDoc::options.title = argv[3];
  P2PDoc::options.copies = atoi(argv[4]);

  ppd = ppdOpenFile(getenv("PPD"));
  ppdMarkDefaults(ppd);
  options = NULL;
  num_options = cupsParseOptions(argv[5],0,&options);
  cupsMarkOptions(ppd,num_options,options);
  if (P2PDoc::options.copies == 1
     && (choice = ppdFindMarkedChoice(ppd,"Copies")) != NULL) {
    P2PDoc::options.copies = atoi(choice->choice);
  }
  if (P2PDoc::options.copies == 0) P2PDoc::options.copies = 1;
  if ((val = cupsGetOption("fitplot", num_options, options)) == NULL) {
    if ((val = cupsGetOption("fit-to-page", num_options, options)) == NULL) {
        val = cupsGetOption("ipp-attribute-fidelity", num_options, options);
    }
  }
  if (val && strcasecmp(val, "no") && strcasecmp(val, "off") &&
      strcasecmp(val, "false"))
    fitplot = gTrue;
  if ((pagesize = ppdPageSize(ppd,0)) != 0) {
    pageWidth = pagesize->width;
    pageLength = pagesize->length;
    pageTop = pagesize->top;
    pageBottom = pagesize->bottom;
    pageLeft = pagesize->left;
    pageRight = pagesize->right;
    forcePageSize = fitplot;
  }
  if ((val = cupsGetOption("landscape",num_options,options)) != 0) {
    if (strcasecmp(val, "no") != 0 && strcasecmp(val, "off") != 0 &&
        strcasecmp(val, "false") != 0) {
      if (ppd && ppd->landscape > 0) {
        orientation = 1;
      } else {
        orientation = 3;
      }
    }
  } else if ((val =
     cupsGetOption("orientation-requested",num_options,options)) != 0) {
   /*
    * Map IPP orientation values to 0 to 3:
    *
    *   3 = 0 degrees   = 0
    *   4 = 90 degrees  = 1
    *   5 = -90 degrees = 3
    *   6 = 180 degrees = 2
    */

    orientation = atoi(val) - 3;
    if (orientation >= 2) {
      orientation ^= 1;
    }
  }
  if ((val = cupsGetOption("page-left",num_options,options)) != 0) {
    switch (orientation & 3) {
      case 0 :
          pageLeft = (float)atof(val);
          break;
      case 1 :
          pageBottom = (float)atof(val);
          break;
      case 2 :
          pageRight = pageWidth - (float)atof(val);
          break;
      case 3 :
          pageTop = pageLength - (float)atof(val);
          break;
    }
  }
  if ((val = cupsGetOption("page-right",num_options,options)) != 0) {
    switch (orientation & 3) {
      case 0 :
          pageRight = pageWidth - (float)atof(val);
          break;
      case 1 :
          pageTop = pageLength - (float)atof(val);
          break;
      case 2 :
          pageLeft = (float)atof(val);
          break;
      case 3 :
          pageBottom = (float)atof(val);
          break;
    }
  }
  if ((val = cupsGetOption("page-bottom",num_options,options)) != 0) {
    switch (orientation & 3) {
      case 0 :
          pageBottom = (float)atof(val);
          break;
      case 1 :
          pageLeft = (float)atof(val);
          break;
      case 2 :
          pageTop = pageLength - (float)atof(val);
          break;
      case 3 :
          pageRight = pageWidth - (float)atof(val);
          break;
    }
  }
  if ((val = cupsGetOption("page-top",num_options,options)) != 0) {
    switch (orientation & 3) {
      case 0 :
          pageTop = pageLength - (float)atof(val);
          break;
      case 1 :
          pageRight = pageWidth - (float)atof(val);
          break;
      case 2 :
          pageBottom = (float)atof(val);
          break;
      case 3 :
          pageLeft = (float)atof(val);
          break;
    }
  }
  if (ppdIsMarked(ppd,"Duplex","DuplexNoTumble") ||
      ppdIsMarked(ppd,"Duplex","DuplexTumble") ||
      ppdIsMarked(ppd,"JCLDuplex","DuplexNoTumble") ||
      ppdIsMarked(ppd,"JCLDuplex","DuplexTumble") ||
      ppdIsMarked(ppd,"EFDuplex","DuplexNoTumble") ||
      ppdIsMarked(ppd,"EFDuplex","DuplexTumble") ||
      ppdIsMarked(ppd,"KD03Duplex","DuplexNoTumble") ||
      ppdIsMarked(ppd,"KD03Duplex","DuplexTumble")) {
      P2PDoc::options.duplex = gTrue;
  } else if ((val = cupsGetOption("Duplex",num_options,options)) != 0 &&
      (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
       !strcasecmp(val, "yes"))) {
      /* for compatiblity */
      if (ppdFindOption(ppd,"Duplex") != NULL) {
	ppdMarkOption(ppd,"Duplex","True");
	ppdMarkOption(ppd,"Duplex","On");
	P2PDoc::options.duplex = gTrue;
      }
  } else if ((val = cupsGetOption("sides",num_options,options)) != 0 &&
      (!strcasecmp(val, "two-sided-long-edge") ||
       !strcasecmp(val, "two-sided-short-edge"))) {
      /* for compatiblity */
      if (ppdFindOption(ppd,"Duplex") != NULL) {
	ppdMarkOption(ppd,"Duplex","True");
	ppdMarkOption(ppd,"Duplex","On");
	P2PDoc::options.duplex = gTrue;
      }
  }

  if ((val = cupsGetOption("number-up",num_options,options)) != 0) {
    switch (intval = atoi(val)) {
      case 1 :
      case 2 :
      case 4 :
      case 6 :
      case 8 :
      case 9 :
      case 16 :
          numberUp = intval;
          break;
      default :
          error(-1,
		  const_cast<char *>("Unsupported number-up value %d, using number-up=1!\n"),
                  intval);
          break;
    }
  }
  if ((val = cupsGetOption("number-up-layout",num_options,options)) != 0) {
    if (!strcasecmp(val,"lrtb")) {
      numberUpLayout = PDFTOPDF_LAYOUT_LRTB;
    } else if (!strcasecmp(val,"lrbt")) {
      numberUpLayout = PDFTOPDF_LAYOUT_LRBT;
    } else if (!strcasecmp(val,"rltb")) {
      numberUpLayout = PDFTOPDF_LAYOUT_RLTB;
    } else if (!strcasecmp(val,"rlbt")) {
      numberUpLayout = PDFTOPDF_LAYOUT_RLBT;
    } else if (!strcasecmp(val,"tblr")) {
      numberUpLayout = PDFTOPDF_LAYOUT_TBLR;
    } else if (!strcasecmp(val,"tbrl")) {
      numberUpLayout = PDFTOPDF_LAYOUT_TBRL;
    } else if (!strcasecmp(val,"btlr")) {
      numberUpLayout = PDFTOPDF_LAYOUT_BTLR;
    } else if (!strcasecmp(val,"btrl")) {
      numberUpLayout = PDFTOPDF_LAYOUT_BTRL;
    } else {
      error(-1, const_cast<char *>("Unsupported number-up-layout value %s,"
              " using number-up-layout=lrtb!\n"), val);
    }
  }
  if ((val = cupsGetOption("OutputOrder",num_options,options)) != 0) {
    if (!strcasecmp(val, "Reverse")) {
      P2PDoc::options.reverse = gTrue;
    }
  } else if (ppd) {
   /*
    * Figure out the right default output order from the PPD file...
    */

    if ((choice = ppdFindMarkedChoice(ppd,"OutputOrder")) != 0) {
      P2PDoc::options.reverse = !strcasecmp(choice->choice,"Reverse");
    } else if ((choice = ppdFindMarkedChoice(ppd,"OutputBin")) != 0 &&
        (attr = ppdFindAttr(ppd,"PageStackOrder",choice->choice)) != 0 &&
        attr->value) {
      P2PDoc::options.reverse = !strcasecmp(attr->value,"Reverse");
    } else if ((attr = ppdFindAttr(ppd,"DefaultOutputOrder",0)) != 0 &&
             attr->value) {
      P2PDoc::options.reverse = !strcasecmp(attr->value,"Reverse");
    }
  }
  if ((val = cupsGetOption("page-border",num_options,options)) != 0) {
    if (!strcasecmp(val,"none")) {
      pageBorder = PDFTOPDF_BORDERNONE;
    } else if (!strcasecmp(val,"single")) {
      pageBorder = PDFTOPDF_BORDERHAIRLINE;
    } else if (!strcasecmp(val,"single-thick")) {
      pageBorder = PDFTOPDF_BORDERTHICK;
    } else if (!strcasecmp(val,"double")) {
      pageBorder = PDFTOPDF_BORDERDOUBLE | PDFTOPDF_BORDERHAIRLINE;
    } else if (!strcasecmp(val,"double-thick")) {
      pageBorder = PDFTOPDF_BORDERDOUBLE | PDFTOPDF_BORDERTHICK;
    } else {
      error(-1, const_cast<char *>("Unsupported page-border value %s, using "
                      "page-border=none!\n"), val);
    }
  }
  P2PDoc::options.pageLabel = cupsGetOption("page-label",num_options,options);
  P2PDoc::options.pageSet = cupsGetOption("page-set",num_options,options);
  P2PDoc::options.pageRanges = cupsGetOption("page-ranges",num_options,options);

  if ((choice = ppdFindMarkedChoice(ppd, "MirrorPrint")) != NULL) {
    val = choice->choice;
    choice->marked =0;
    if (val && (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
                    !strcasecmp(val, "yes"))) {
      mirror = gTrue;
    }
  } else if ((val = cupsGetOption("mirror",num_options,options)) != 0 &&
      (!strcasecmp(val,"true") || !strcasecmp(val,"on") ||
       !strcasecmp(val,"yes"))) {
    mirror = gTrue;
  }
  if ((val = cupsGetOption("emit-jcl",num_options,options)) != 0 &&
      (!strcasecmp(val,"false") || !strcasecmp(val,"off") ||
       !strcasecmp(val,"no") || !strcmp(val,"0"))) {
    emitJCL = gFalse;
  }
  if ((val = cupsGetOption("position",num_options,options)) != 0) {
    if (strcasecmp(val,"center") == 0) {
      xposition = 0;
      yposition = 0;
    } else if (strcasecmp(val,"top") == 0) {
      xposition = 0;
      yposition = 1;
    } else if (strcasecmp(val,"left") == 0) {
      xposition = -1;
      yposition = 0;
    } else if (strcasecmp(val,"right") == 0) {
      xposition = 1;
      yposition = 0;
    } else if (strcasecmp(val,"top-left") == 0) {
      xposition = -1;
      yposition = 1;
    } else if (strcasecmp(val,"top-right") == 0) {
      xposition = 1;
      yposition = 1;
    } else if (strcasecmp(val,"bottom") == 0) {
      xposition = 0;
      yposition = -1;
    } else if (strcasecmp(val,"bottom-left") == 0) {
      xposition = -1;
      yposition = -1;
    } else if (strcasecmp(val,"bottom-right") == 0) {
      xposition = 1;
      yposition = -1;
    }
    position = gTrue;
  }

  if ((val = cupsGetOption("multiple-document-handling",num_options,options)) 
      != 0) {
    P2PDoc::options.collate =
      strcasecmp(val,"separate-documents-uncollated-copies") != 0;
  }
  if ((val = cupsGetOption("Collate",num_options,options)) != 0) {
    if (strcasecmp(val,"True") == 0) {
      P2PDoc::options.collate = gTrue;
    }
  } else {
    if ((choice = ppdFindMarkedChoice(ppd,"Collate")) != NULL
      && (!strcasecmp(choice->choice,"true")
	|| !strcasecmp(choice->choice, "on")
	|| !strcasecmp(choice->choice, "yes"))) {
      P2PDoc::options.collate = gTrue;
    }
  }

  if ((val = cupsGetOption("scaling",num_options,options)) != 0) {
    scaling = atoi(val) * 0.01;
    fitplot = gTrue;
  } else if (fitplot) {
    scaling = 1.0;
  }
  if ((val = cupsGetOption("natural-scaling",num_options,options)) != 0) {
    naturalScaling = atoi(val) * 0.01;
  }
  /* adujst to even page when duplex */
  if (checkFeature("cupsEvenDuplex",num_options,options)) {
    P2PDoc::options.even = gTrue;
  }

  /* embedding fonts into output PDF */
  if (checkFeature("pdftopdfFontEmbedding",num_options,options)) {
    P2PDoc::options.fontEmbedding = gTrue;
  }
  /* embedding whole font file into output PDF */
  if (checkFeature("pdftopdfFontEmbeddingWhole",num_options,options)) {
    P2PDoc::options.fontEmbeddingWhole = gTrue;
  }
  /* embedding pre-loaded fonts specified in PPD into output PDF */
  if (checkFeature("pdftopdfFontEmbeddingPreLoad",num_options,options)) {
    P2PDoc::options.fontEmbeddingPreLoad = gTrue;
  }
  /* compressing embedded fonts */
  if (checkFeature("pdftopdfFontCompress",num_options,options)) {
    P2PDoc::options.fontCompress = gTrue;
  }
  /* compressing page contents */
  if (checkFeature("pdftopdfContentsCompress",num_options,options)) {
    P2PDoc::options.contentsCompress = gTrue;
  }
  /* auto rotate */
  if (cupsGetOption("pdftopdfAutoRotate",num_options,options) != 0 ||
         ppdFindAttr(ppd,"pdftopdfAutoRotate",0) != 0) {
      if (!checkFeature("pdftopdfAutoRotate",num_options,options)) {
	/* disable auto rotate */
	autoRotate = gFalse;
      }
  }

  /* pre-loaded fonts */
  if (ppd != 0) {
    P2PDoc::options.numPreFonts = ppd->num_fonts;
    P2PDoc::options.preFonts = ppd->fonts;
  }

  if (P2PDoc::options.copies == 1) {
    /* collate is not needed */
    P2PDoc::options.collate = gFalse;
    ppdMarkOption(ppd,"Collate","False");
  }
  if (!P2PDoc::options.duplex) {
    /* evenDuplex is not needed */
    P2PDoc::options.even = gFalse;
  }

  /* check collate device */
  if (P2PDoc::options.collate && !ppd->manual_copies) {
    if ((choice = ppdFindMarkedChoice(ppd,"Collate")) != NULL &&
       !strcasecmp(choice->choice,"true")) {
      ppd_option_t *opt;

      if ((opt = ppdFindOption(ppd,"Collate")) != NULL &&
        !opt->conflicted) {
	deviceCollate = gTrue;
      } else {
	ppdMarkOption(ppd,"Collate","False");
      }
    }
  }
  /* check OutputOrder device */
  if (P2PDoc::options.reverse) {
    if (ppdFindOption(ppd,"OutputOrder") != NULL) {
      deviceReverse = gTrue;
    }
  }
  if (ppd != NULL &&
       !ppd->manual_copies && P2PDoc::options.collate && !deviceCollate) {
    /* Copying by device , software collate is impossible */
    /* Enable software copying */
    ppd->manual_copies = 1;
  }
  if (P2PDoc::options.copies > 1 && (ppd == NULL || ppd->manual_copies)
      && P2PDoc::options.duplex) {
    /* Enable software collate , or same pages are printed in both sides */
      P2PDoc::options.collate = gTrue;
      if (deviceCollate) {
	deviceCollate = gFalse;
	ppdMarkOption(ppd,"Collate","False");
      }
  }
  if (P2PDoc::options.duplex && P2PDoc::options.collate && !deviceCollate) {
    /* Enable evenDuplex or the first page may be printed other side of the
      end of precedings */
    P2PDoc::options.even = gTrue;
  }
  if (P2PDoc::options.duplex && P2PDoc::options.reverse && !deviceReverse) {
    /* Enable evenDuplex or the first page may be empty. */
    P2PDoc::options.even = gTrue;
  }
  /* change feature for software */
  if (deviceCollate) {
    P2PDoc::options.collate = gFalse;
  }
  if (deviceReverse) {
    P2PDoc::options.reverse = gFalse;
  }

  if (ppd != NULL) {
    if (ppd->manual_copies) {
      /* sure disable hardware copying */
      ppdMarkOption(ppd,"Copies","1");
      ppdMarkOption(ppd,"JCLCopies","1");
    } else {
      /* change for hardware copying */
      deviceCopies = P2PDoc::options.copies;
      P2PDoc::options.copies = 1;
    }
  }
}

int main(int argc, char *argv[]) {
  PDFDoc *doc;
  P2PDoc *p2pdoc;
  P2POutputStream *str;

  setErrorFunction(::myErrorFun);
#ifdef GLOBALPARAMS_HAS_A_ARG
  globalParams = new GlobalParams(0);
#else
  globalParams = new GlobalParams();
#endif
  parseOpts(argc, argv);

  PDFRectangle box(pageLeft,pageBottom,pageRight,pageTop);
  PDFRectangle mediaBox(0,0,pageWidth,pageLength);

  if (argc == 6) {
    /* stdin */
    int fd;
    Object obj;
    BaseStream *str;
    FILE *fp;
    char buf[BUFSIZ];
    int n;

    fd = cupsTempFd(buf,sizeof(buf));
    if (fd < 0) {
      error(-1,const_cast<char *>("Can't create temporary file"));
      exit(1);
    }
    /* remove name */
    unlink(buf);

    /* copy stdin to the tmp file */
    while ((n = read(0,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
        error(-1,const_cast<char *>("Can't copy stdin to temporary file"));
        close(fd);
	exit(1);
      }
    }
    if (lseek(fd,0,SEEK_SET) < 0) {
        error(-1,const_cast<char *>("Can't rewind temporary file"));
        close(fd);
	exit(1);
    }

    if ((fp = fdopen(fd,"rb")) == 0) {
        error(-1,const_cast<char *>("Can't fdopen temporary file"));
        close(fd);
	exit(1);
    }

    obj.initNull();
    str = new FileStream(fp,0,gFalse,0,&obj);
    doc = new PDFDoc(str);
  } else {
    GooString *fileName = new GooString(argv[6]);
    /* argc == 7 filenmae is specified */
    doc = new PDFDoc(fileName,NULL,NULL);
  }

  if (!doc->isOk()) {
    exitCode = 1;
    goto err1;
  }
  if (!doc->okToPrintHighRes() && !doc->okToPrint()) {
    error(-1,const_cast<char *>("Printing is not allowed\n"));
    exit(1);
  }
  p2pdoc = new P2PDoc(doc);
  if (mirror) {
    p2pdoc->mirror();
  }

  if (orientation != 0) {
    p2pdoc->rotate(orientation);
    p2pdoc->position(&box,xposition,yposition);
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

  p2pdoc->select();

  if (autoRotate && orientation == 0
     && naturalScaling == 1.0 && !fitplot && numberUp == 1 && !position) {
    /* If no translation is specified, do auto-rotate.
     * This is for compatibility with pdftops filter.
     */
    p2pdoc->autoRotate(&mediaBox);
  }

  /* set all pages's mediaBox to the target page size, but only if a page
   * size is given on the command line or an option which influences the
   * printout size is used */
  if (forcePageSize || orientation != 0 ||
      naturalScaling != 1.0 || fitplot || numberUp != 1 || position) {
    p2pdoc->setMediaBox(&mediaBox);
  }

  if ((P2PDoc::options.collate || deviceCollate)
      && p2pdoc->getNumberOfPages() == 1
      && !P2PDoc::options.even) {
    /* collate is not needed, disable it */
    /* Number of pages is changed by nup and page-ranges,
	so check this here */
    deviceCollate = gFalse;
    P2PDoc::options.collate = gFalse;
    ppdMarkOption(ppd,"Collate","False");
  }

  ppdEmit(ppd,stdout,PPD_ORDER_EXIT);

  if (emitJCL) {
    ppdEmitJCL(ppd,stdout,P2PDoc::options.jobId,P2PDoc::options.user,
      P2PDoc::options.title);
    emitJCLOptions(stdout,deviceCopies);
  }
  str = new P2POutputStream(stdout); /* PDF start here */
  p2pdoc->output(str,deviceCopies,deviceCollate);
#ifndef CUPS_1_1
  if (emitJCL) {
    ppdEmitJCLEnd(ppd,stdout);
  }
#endif

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

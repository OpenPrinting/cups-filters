/*
 * Image file to PDF filter function for cups-filters.
 * developped by BBR Inc. 2006-2007
 *
 * This is based on imagetops.c of CUPS
 *
 * imagetops.c copyright notice is follows
 *
 *   Copyright 1993-2006 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "COPYING" which should have been included with this file.
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include <cupsfilters/filter.h>
#include <cupsfilters/image.h>
#include <cupsfilters/ppdgenerator.h>
#include <cupsfilters/raster.h>
#include <cupsfilters/image-private.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>

#define N_OBJECT_ALLOC 100
#define LINEBUFSIZE 1024

/*
 * Types...
 */

struct pdfObject {
    int offset;
};

typedef struct imagetopdf_doc_s {       /**** Document information ****/
  int		Flip,			/* Flip/mirror pages */
		XPosition,		/* Horizontal position on page */
		YPosition,		/* Vertical position on page */
		Collate,		/* Collate copies? */
		Copies,			/* Number of copies */
		Reverse,		/* Output order */
		EvenDuplex;		/* cupsEvenDuplex */
  int   	Orientation,    	/* 0 = portrait, 1 = landscape, etc. */
        	Duplex,         	/* Duplexed? */
        	LanguageLevel,  	/* Language level of printer */
        	Color;    	 	/* Print in color? */
  float 	PageLeft,       	/* Left margin */
        	PageRight,      	/* Right margin */
        	PageBottom,     	/* Bottom margin */
        	PageTop,        	/* Top margin */
        	PageWidth,      	/* Total page width */
        	PageLength;     	/* Total page length */
  struct pdfObject *objects;
  int		currentObjectNo;
  int		allocatedObjectNum;
  int		currentOffset;
  int		xrefOffset;
  int		*pageObjects;
  int		catalogObj;
  int		pagesObj;
  const char	*title;
  int		xpages,			/* # x pages */
		ypages,			/* # y pages */
		xpage,			/* Current x page */
		ypage,			/* Current y page */
		page;			/* Current page number */
  int		xc0, yc0,		/* Corners of the page in */
		xc1, yc1;		/* image coordinates */
  float		left, top;		/* Left and top of image */
  float		xprint,			/* Printable area */
		yprint,
		xinches,		/* Total size in inches */
		yinches;
  float		xsize,			/* Total size in points */
		ysize,
		xsize2,
		ysize2;
  float		aspect;			/* Aspect ratio */
  cf_image_t	*img;			/* Image to print */
  int		colorspace;		/* Output colorspace */
  cf_ib_t	*row;			/* Current row */
  float		gammaval;		/* Gamma correction value */
  float		brightness;		/* Gamma correction value */
  ppd_file_t	*ppd;			/* PPD file */
  char		linebuf[LINEBUFSIZE];
  FILE		*outputfp;
} imagetopdf_doc_t;

/*
 * Local functions...
 */

static void	emit_jcl_options(imagetopdf_doc_t *doc, FILE *fp, int copies);
#ifdef OUT_AS_HEX
static void	out_hex(imagetopdf_doc_t *doc, cf_ib_t *, int, int);
#else
#ifdef OUT_AS_ASCII85
static void	out_ascii85(imagetopdf_doc_t *doc, cf_ib_t *, int, int);
#else
static void	out_bin(imagetopdf_doc_t *doc, cf_ib_t *, int, int);
#endif
#endif
static void	out_pdf(imagetopdf_doc_t *doc, const char *str);
static void	putc_pdf(imagetopdf_doc_t *doc, char c);
static int	new_obj(imagetopdf_doc_t *doc);
static void	free_all_obj(imagetopdf_doc_t *doc);
static void	out_xref(imagetopdf_doc_t *doc);
static void	out_trailer(imagetopdf_doc_t *doc);
static void	out_string(imagetopdf_doc_t *doc, const char *s);
static int	out_prologue(imagetopdf_doc_t *doc, int nPages);
static int	alloc_page_objects(imagetopdf_doc_t *doc, int noPages);
static void	set_offset(imagetopdf_doc_t *doc, int obj);
static int	out_page_object(imagetopdf_doc_t *doc, int pageObj,
			      int contentsObj, int imgObj);
static int	out_page_contents(imagetopdf_doc_t *doc, int contentsObj);
static int	out_image(imagetopdf_doc_t *doc, int imgObj);

static void emit_jcl_options(imagetopdf_doc_t *doc, FILE *fp, int copies)
{
  int section;
  ppd_choice_t **choices;
  int i;
  char buf[1024];
  ppd_attr_t *attr;
  int pdftopdfjcl = 0;
  int datawritten = 0;

  if (doc->ppd == 0) return;
  if ((attr = ppdFindAttr(doc->ppd,"pdftopdfJCLBegin",NULL)) != NULL) {
    int n = strlen(attr->value);
    pdftopdfjcl = 1;
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
  if (ppdFindOption(doc->ppd,"Copies") != NULL) {
    ppdMarkOption(doc->ppd,"Copies",buf);
  } else {
    if ((attr = ppdFindAttr(doc->ppd,"pdftopdfJCLCopies",buf)) != NULL) {
      fputs(attr->value,fp);
      datawritten = 1;
    } else if (pdftopdfjcl) {
      fprintf(fp,"Copies=%d;",copies);
      datawritten = 1;
    }
  }
  for (section = (int)PPD_ORDER_ANY;
      section <= (int)PPD_ORDER_PROLOG;section++) {
    int n;

    n = ppdCollect(doc->ppd,(ppd_section_t)section,&choices);
    for (i = 0;i < n;i++) {
      snprintf(buf,sizeof(buf),"pdftopdfJCL%s",
        ((ppd_option_t *)(choices[i]->option))->keyword);
      if ((attr = ppdFindAttr(doc->ppd,buf,choices[i]->choice)) != NULL) {
        fputs(attr->value,fp);
	datawritten = 1;
      } else if (pdftopdfjcl) {
        fprintf(fp,"%s=%s;",
          ((ppd_option_t *)(choices[i]->option))->keyword,
          choices[i]->choice);
	datawritten = 1;
      }
    }
  }
  if (datawritten) fputc('\n',fp);
}

static void set_offset(imagetopdf_doc_t *doc, int obj)
{
  doc->objects[obj].offset = doc->currentOffset;
}

static int alloc_page_objects(imagetopdf_doc_t *doc, int nPages)
{
  int i, n;

  if ((doc->pageObjects = malloc(sizeof(int)*nPages)) == NULL)
    return (-1);
  for (i = 0;i < nPages;i++)
  {
    if ((n = new_obj(doc)) >= 0)
      doc->pageObjects[i] = n;
    else
      return (-1);
  }
  return (0);
}

static int new_obj(imagetopdf_doc_t *doc)
{
  if (doc->objects == NULL)
  {
    if ((doc->objects = malloc(sizeof(struct pdfObject)*N_OBJECT_ALLOC))
	  == NULL)
      return(-1);
    doc->allocatedObjectNum = N_OBJECT_ALLOC;
  }
  if (doc->currentObjectNo >= doc->allocatedObjectNum)
  {
    if ((doc->objects = realloc(doc->objects,
	  sizeof(struct pdfObject)*(doc->allocatedObjectNum+N_OBJECT_ALLOC)))
	  == NULL)
      return(-1);
    doc->allocatedObjectNum += N_OBJECT_ALLOC;
  }
  doc->objects[doc->currentObjectNo].offset = doc->currentOffset;
  return doc->currentObjectNo++;
}

static void free_all_obj(imagetopdf_doc_t *doc)
{
  if (doc->objects != NULL)
  {
    free(doc->objects);
    doc->objects = NULL;
  }
}

static void putc_pdf(imagetopdf_doc_t *doc, char c)
{
  fputc(c, doc->outputfp);
  doc->currentOffset++;
}

static void out_pdf(imagetopdf_doc_t *doc, const char *str)
{
  unsigned long len = strlen(str);

  fputs(str, doc->outputfp);
  doc->currentOffset += len;
}

static void out_xref(imagetopdf_doc_t *doc)
{
  char buf[21];
  int i;

  doc->xrefOffset = doc->currentOffset;
  out_pdf(doc, "xref\n");
  snprintf(buf,21,"0 %d\n",doc->currentObjectNo);
  out_pdf(doc, buf);
  out_pdf(doc, "0000000000 65535 f \n");
  for (i = 1;i < doc->currentObjectNo;i++)
  {
    snprintf(buf,21,"%010d 00000 n \n",doc->objects[i].offset);
    out_pdf(doc, buf);
  }
}

static void out_string(imagetopdf_doc_t *doc, const char *s)
{
  char c;

  putc_pdf(doc, '(');
  for (;(c = *s) != '\0';s++) {
    if (c == '\\' || c == '(' || c == ')') {
      putc_pdf(doc, '\\');
    }
    putc_pdf(doc, c);
  }
  putc_pdf(doc, ')');
}

static void out_trailer(imagetopdf_doc_t *doc)
{
  time_t	curtime;
  struct tm	*curtm;
  char		curdate[255];

  curtime = time(NULL);
  curtm = localtime(&curtime);
  strftime(curdate, sizeof(curdate),"D:%Y%m%d%H%M%S%z", curtm);

  out_pdf(doc, "trailer\n");
  snprintf(doc->linebuf, LINEBUFSIZE,"<</Size %d ",doc->currentObjectNo);
  out_pdf(doc, doc->linebuf);
  out_pdf(doc, "/Root 1 0 R\n");
  out_pdf(doc, "/Info << /Title ");
  out_string(doc, doc->title);
  putc_pdf(doc, ' ');
  snprintf(doc->linebuf,LINEBUFSIZE,"/CreationDate (%s) ",curdate);
  out_pdf(doc, doc->linebuf);
  snprintf(doc->linebuf,LINEBUFSIZE,"/ModDate (%s) ",curdate);
  out_pdf(doc, doc->linebuf);
  out_pdf(doc, "/Producer (imagetopdf) ");
  out_pdf(doc, "/Trapped /False >>\n");
  out_pdf(doc, ">>\n");
  out_pdf(doc, "startxref\n");
  snprintf(doc->linebuf,LINEBUFSIZE,"%d\n",doc->xrefOffset);
  out_pdf(doc, doc->linebuf);
  out_pdf(doc, "%%EOF\n");
}

static int out_prologue(imagetopdf_doc_t *doc, int nPages)
{
  int i;

  /* out header */
  if (new_obj(doc) < 0) /* dummy for no 0 object */
    return (-1);
  out_pdf(doc, "%PDF-1.3\n");
  /* out binary for transfer program */
  doc->linebuf[0] = '%';
  doc->linebuf[1] = (char)129;
  doc->linebuf[2] = (char)130;
  doc->linebuf[3] = (char)131;
  doc->linebuf[4] = (char)132;
  doc->linebuf[5] = '\n';
  doc->linebuf[6] = (char)0;
  out_pdf(doc, doc->linebuf);
  out_pdf(doc, "% This file was generated by imagetopdf\n");

  if ((doc->catalogObj = new_obj(doc)) < 0)
    return (-1);
  if ((doc->pagesObj = new_obj(doc)) < 0)
    return (-1);
  if (alloc_page_objects(doc, nPages) < 0)
    return (-1);

  /* out catalog */
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj <</Type/Catalog /Pages %d 0 R ",doc->catalogObj,doc->pagesObj);
  out_pdf(doc, doc->linebuf);
  out_pdf(doc, ">> endobj\n");

  /* out Pages */
  set_offset(doc, doc->pagesObj);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj <</Type/Pages /Kids [ ",doc->pagesObj);
  out_pdf(doc, doc->linebuf);
  if (doc->Reverse) {
    for (i = nPages-1;i >= 0;i--)
    {
      snprintf(doc->linebuf,LINEBUFSIZE,"%d 0 R ",doc->pageObjects[i]);
      out_pdf(doc, doc->linebuf);
    }
  } else {
    for (i = 0;i < nPages;i++)
    {
      snprintf(doc->linebuf,LINEBUFSIZE,"%d 0 R ",doc->pageObjects[i]);
      out_pdf(doc, doc->linebuf);
    }
  }
  out_pdf(doc, "] ");
  snprintf(doc->linebuf,LINEBUFSIZE,"/Count %d >> endobj\n",nPages);
  out_pdf(doc, doc->linebuf);
  return (0);
}

static int out_page_object(imagetopdf_doc_t *doc, int pageObj, int contentsObj,
			 int imgObj)
{
  int trfuncObj;
  int lengthObj;
  int startOffset;
  int length;
  int outTrfunc = (doc->gammaval != 1.0 || doc->brightness != 1.0);

  /* out Page Object */
  set_offset(doc, pageObj);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj <</Type/Page /Parent %d 0 R ",
    pageObj,doc->pagesObj);
  out_pdf(doc, doc->linebuf);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "/MediaBox [ 0 0 %f %f ] ",doc->PageWidth,doc->PageLength);
  out_pdf(doc, doc->linebuf);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "/TrimBox [ 0 0 %f %f ] ",doc->PageWidth,doc->PageLength);
  out_pdf(doc, doc->linebuf);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "/CropBox [ 0 0 %f %f ] ",doc->PageWidth,doc->PageLength);
  out_pdf(doc, doc->linebuf);
  if (contentsObj >= 0) {
    snprintf(doc->linebuf,LINEBUFSIZE,
      "/Contents %d 0 R ",contentsObj);
    out_pdf(doc, doc->linebuf);
    snprintf(doc->linebuf,LINEBUFSIZE,
      "/Resources <</ProcSet [/PDF] "
      "/XObject << /Im %d 0 R >>\n",imgObj);
    out_pdf(doc, doc->linebuf);
  } else {
    /* empty page */
    snprintf(doc->linebuf,LINEBUFSIZE,
      "/Resources <</ProcSet [/PDF] \n");
    out_pdf(doc, doc->linebuf);
  }
  if (outTrfunc) {
    if ((trfuncObj = new_obj(doc)) < 0)
      return (-1);
    if ((lengthObj = new_obj(doc)) < 0)
      return (-1);
    snprintf(doc->linebuf,LINEBUFSIZE,
      "/ExtGState << /GS1 << /TR %d 0 R >> >>\n",trfuncObj);
    out_pdf(doc, doc->linebuf);
  }
  out_pdf(doc, "     >>\n>>\nendobj\n");

  if (outTrfunc) {
    /* out translate function */
    set_offset(doc, trfuncObj);
    snprintf(doc->linebuf,LINEBUFSIZE,
      "%d 0 obj <</FunctionType 4 /Domain [0 1.0]"
      " /Range [0 1.0] /Length %d 0 R >>\n",
      trfuncObj,lengthObj);
    out_pdf(doc, doc->linebuf);
    out_pdf(doc, "stream\n");
    startOffset = doc->currentOffset;
    snprintf(doc->linebuf,LINEBUFSIZE,
     "{ neg 1 add dup 0 lt { pop 1 } { %.3f exp neg 1 add } "
     "ifelse %.3f mul }\n", doc->gammaval, doc->brightness);
    out_pdf(doc, doc->linebuf);
    length = doc->currentOffset - startOffset;
    snprintf(doc->linebuf,LINEBUFSIZE,
     "endstream\nendobj\n");
    out_pdf(doc, doc->linebuf);

    /* out length object */
    set_offset(doc, lengthObj);
    snprintf(doc->linebuf,LINEBUFSIZE,
      "%d 0 obj %d endobj\n",lengthObj,length);
    out_pdf(doc, doc->linebuf);
  }
  return (0);
}

static int out_page_contents(imagetopdf_doc_t *doc, int contentsObj)
{
  int startOffset;
  int lengthObj;
  int length;

  set_offset(doc, contentsObj);
  if ((lengthObj = new_obj(doc)) < 0)
    return (-1);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj <</Length %d 0 R >> stream\n",contentsObj,lengthObj);
  out_pdf(doc, doc->linebuf);
  startOffset = doc->currentOffset;

  if (doc->gammaval != 1.0 || doc->brightness != 1.0)
    out_pdf(doc, "/GS1 gs\n");
  if (doc->Flip)
  {
    snprintf(doc->linebuf,LINEBUFSIZE,
      "-1 0 0 1 %.0f 0 cm\n",doc->PageWidth);
    out_pdf(doc, doc->linebuf);
  }

  switch (doc->Orientation)
  {
    case 1:
	snprintf(doc->linebuf,LINEBUFSIZE,
	  "0 1 -1 0 %.0f 0 cm\n",doc->PageWidth);
	out_pdf(doc, doc->linebuf);
	break;
    case 2:
	snprintf(doc->linebuf,LINEBUFSIZE,
	  "-1 0 0 -1 %.0f %.0f cm\n",doc->PageWidth, doc->PageLength);
	out_pdf(doc, doc->linebuf);
	break;
    case 3:
	snprintf(doc->linebuf,LINEBUFSIZE,
	  "0 -1 1 0 0 %.0f cm\n",doc->PageLength);
	out_pdf(doc, doc->linebuf);
	break;
  }

  doc->xc0 = cfImageGetWidth(doc->img) * doc->xpage / doc->xpages;
  doc->xc1 = cfImageGetWidth(doc->img) * (doc->xpage + 1) / doc->xpages - 1;
  doc->yc0 = cfImageGetHeight(doc->img) * doc->ypage / doc->ypages;
  doc->yc1 = cfImageGetHeight(doc->img) * (doc->ypage + 1) / doc->ypages - 1;

  snprintf(doc->linebuf,LINEBUFSIZE,
    "1 0 0 1 %.1f %.1f cm\n",doc->left,doc->top);
  out_pdf(doc, doc->linebuf);

  snprintf(doc->linebuf,LINEBUFSIZE,
    "%.3f 0 0 %.3f 0 0 cm\n",
     doc->xprint * 72.0, doc->yprint * 72.0);
  out_pdf(doc, doc->linebuf);
  out_pdf(doc, "/Im Do\n");
  length = doc->currentOffset - startOffset - 1;
  out_pdf(doc, "endstream\nendobj\n");

  /* out length object */
  set_offset(doc, lengthObj);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj %d endobj\n",lengthObj,length);
  out_pdf(doc, doc->linebuf);
  return (0);
}

static int out_image(imagetopdf_doc_t *doc, int imgObj)
{
  int		y;			/* Current Y coordinate in image */
#ifdef OUT_AS_ASCII85
  int		out_offset;		/* Offset into output buffer */
#endif
  int		out_length;		/* Length of output buffer */
  int startOffset;
  int lengthObj;
  int length;

  set_offset(doc, imgObj);
  if ((lengthObj = new_obj(doc)) < 0)
    return (-1);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj << /Length %d 0 R /Type /XObject "
    "/Subtype /Image /Name /Im"
#ifdef OUT_AS_HEX
    "/Filter /ASCIIHexDecode "
#else
#ifdef OUT_AS_ASCII85
    "/Filter /ASCII85Decode "
#endif
#endif
    ,imgObj,lengthObj);
  out_pdf(doc, doc->linebuf);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "/Width %d /Height %d /BitsPerComponent 8 ",
    doc->xc1 - doc->xc0 + 1, doc->yc1 - doc->yc0 + 1);
  out_pdf(doc, doc->linebuf);

  switch (doc->colorspace)
  {
    case CF_IMAGE_WHITE :
      out_pdf(doc, "/ColorSpace /DeviceGray ");
      out_pdf(doc, "/Decode[0 1] ");
      break;
    case CF_IMAGE_RGB :
      out_pdf(doc, "/ColorSpace /DeviceRGB ");
      out_pdf(doc, "/Decode[0 1 0 1 0 1] ");
      break;
    case CF_IMAGE_CMYK :
      out_pdf(doc, "/ColorSpace /DeviceCMYK ");
      out_pdf(doc, "/Decode[0 1 0 1 0 1 0 1] ");
      break;
  }
  if (((doc->xc1 - doc->xc0 + 1) / doc->xprint) < 100.0)
    out_pdf(doc, "/Interpolate true ");

  out_pdf(doc, ">>\n");
  out_pdf(doc, "stream\n");
  startOffset = doc->currentOffset;

#ifdef OUT_AS_ASCII85
  /* out ascii85 needs multiple of 4bytes */
  for (y = doc->yc0, out_offset = 0; y <= doc->yc1; y ++)
  {
    cfImageGetRow(doc->img, doc->xc0, y, doc->xc1 - doc->xc0 + 1,
		    doc->row + out_offset);

    out_length = (doc->xc1 - doc->xc0 + 1) * abs(doc->colorspace) + out_offset;
    out_offset = out_length & 3;

    out_ascii85(doc, doc->row, out_length, y == doc->yc1);

    if (out_offset > 0)
      memcpy(doc->row, doc->row + out_length - out_offset, out_offset);
  }
#else
  for (y = doc->yc0; y <= doc->yc1; y ++)
  {
    cfImageGetRow(doc->img, doc->xc0, y, doc->xc1 - doc->xc0 + 1, doc->row);

    out_length = (doc->xc1 - doc->xc0 + 1) * abs(doc->colorspace);

#ifdef OUT_AS_HEX
    out_hex(doc, doc->row, out_length, y == doc->yc1);
#else
    out_bin(doc, doc->row, out_length, y == doc->yc1);
#endif
  }
#endif
  length = doc->currentOffset - startOffset;
  out_pdf(doc, "\nendstream\nendobj\n");

  /* out length object */
  set_offset(doc, lengthObj);
  snprintf(doc->linebuf,LINEBUFSIZE,
    "%d 0 obj %d endobj\n",lengthObj,length);
  out_pdf(doc, doc->linebuf);
  return (0);
}

/*
 * Copied ppd_decode() from CUPS which is not exported to the API
 */

static int				/* O - Length of decoded string */
ppd_decode(char *string)		/* I - String to decode */
{
  char	*inptr,				/* Input pointer */
	*outptr;			/* Output pointer */


  inptr  = string;
  outptr = string;

  while (*inptr != '\0')
    if (*inptr == '<' && isxdigit(inptr[1] & 255))
    {
     /*
      * Convert hex to 8-bit values...
      */

      inptr ++;
      while (isxdigit(*inptr & 255))
      {
	if (isalpha(*inptr))
	  *outptr = (tolower(*inptr) - 'a' + 10) << 4;
	else
	  *outptr = (*inptr - '0') << 4;

	inptr ++;

        if (!isxdigit(*inptr & 255))
	  break;

	if (isalpha(*inptr))
	  *outptr |= tolower(*inptr) - 'a' + 10;
	else
	  *outptr |= *inptr - '0';

	inptr ++;
	outptr ++;
      }

      while (*inptr != '>' && *inptr != '\0')
	inptr ++;
      while (*inptr == '>')
	inptr ++;
    }
    else
      *outptr++ = *inptr++;

  *outptr = '\0';

  return ((int)(outptr - string));
}

/*
 * 'cfFilterImageToPDF()' - Filter function to convert many common image file
 *                  formats into PDF
 */

int                             /* O - Error status */
cfFilterImageToPDF(int inputfd,         /* I - File descriptor input stream */
	   int outputfd,        /* I - File descriptor output stream */
	   int inputseekable,   /* I - Is input stream seekable? (unused) */
	   cf_filter_data_t *data, /* I - Job and printer data */
	   void *parameters)    /* I - Filter-specific parameters (unused) */
{
  imagetopdf_doc_t	doc;		/* Document information */
  cups_page_header2_t h;                /* CUPS Raster page header, to */
                                        /* accommodate results of command */
                                        /* line parsing for PPD-less queue */
  ppd_choice_t	*choice;		/* PPD option choice */
  int		num_options = 0;		/* Number of print options */
  cups_option_t	*options = NULL;		/* Print options */
  const char	*val;			/* Option value */
  float		zoom;			/* Zoom facter */
  int		xppi, yppi;		/* Pixels-per-inch */
  int		hue, sat;		/* Hue and saturation adjustment */
  int		emit_jcl;
  int           pdf_printer = 0;
  char		tempfile[1024];		/* Name of file to print */
  FILE          *fp;			/* Input file */
  int           fd;			/* File descriptor for temp file */
  char          buf[BUFSIZ];
  int           bytes;
  int		deviceCopies = 1;
  int		deviceCollate = 0;
  int		deviceReverse = 0;
  ppd_attr_t	*attr;
  int		pl, pr;
  int		fillprint = 0;		/* print-scaling = fill */
  int		cropfit = 0;		/* -o crop-to-fit = true */
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;
  ipp_t *printer_attrs = data->printer_attrs;
  ipp_t *job_attrs = data->job_attrs;
  ipp_attribute_t *ipp;
  int 			min_length = __INT32_MAX__,       /*  ppd->custom_min[1]	*/
      			min_width = __INT32_MAX__,        /*  ppd->custom_min[0]	*/
      			max_length = 0, 		  /*  ppd->custom_max[1]	*/
      			max_width=0;			  /*  ppd->custom_max[0]	*/
  float 		customLeft = 0.0,		/*  ppd->custom_margin[0]  */
        		customBottom = 0.0,	        /*  ppd->custom_margin[1]  */
			customRight = 0.0,	        /*  ppd->custom_margin[2]  */
			customTop = 0.0;	        /*  ppd->custom_margin[3]  */
  char 			defSize[41];


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Initialize data structure
  */

  doc.Flip = 0;
  doc.XPosition = 0;
  doc.YPosition = 0;
  doc.Collate = 0;
  doc.Copies = 1;
  doc.Reverse = 0;
  doc.EvenDuplex = 0;
  doc.objects = NULL;
  doc.currentObjectNo = 0;
  doc.allocatedObjectNum = 0;
  doc.currentOffset = 0;
  doc.pageObjects = NULL;
  doc.gammaval = 1.0;
  doc.brightness = 1.0;

 /*
  * Open the input data stream specified by the inputfd ...
  */

  if ((fp = fdopen(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPDF: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Copy input into temporary file if needed ...
  */

  if (!inputseekable) {
    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPDF: Unable to copy input: %s",
		   strerror(errno));
      fclose(fp);
      return (1);
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Copying input to temp file \"%s\"",
		 tempfile);

    while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0)
      bytes = write(fd, buf, bytes);

    fclose(fp);
    close(fd);

   /*
    * Open the temporary file to read it instead of the original input ...
    */

    if ((fp = fopen(tempfile, "r")) == NULL)
    {
      if (!iscanceled || !iscanceled(icd))
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterImageToPDF: Unable to open temporary file.");
      }

      unlink(tempfile);
      return (1);
    }
  }

 /*
  * Open the output data stream specified by the outputfd...
  */

  if ((doc.outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPDF: Unable to open output data stream.");
    }

    fclose(fp);
    if (!inputseekable)
      unlink(tempfile);

    return (1);
  }

 /*
  * Process options and write the prolog...
  */

  zoom = 1.0;
  xppi = 0;
  yppi = 0;
  hue  = 0;
  sat  = 100;

  doc.title = data->job_title;
  doc.Copies = data->copies;

 /*
  * Option list...
  * Also add job-attrs in options list itself. 
  */

  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

 /* 
  * Compute custom margins and min_width and min_length of the page... 
  */

  if (printer_attrs != NULL) {
    int left, right, top, bottom;
    cfGenerateSizes(printer_attrs, &ipp, &min_length, &min_width,
		    &max_length, &max_width, &bottom, &left, &right, &top,
		    defSize);
    customLeft = left*72.0/2540.0;
    customRight = right*72.0/2540.0;
    customTop = top*72.0/2540.0;
    customBottom = bottom*72.0/2540.0;
  }


 /*
  * Process job options...
  */

  doc.ppd = data->ppd;
  cfFilterSetCommonOptions(doc.ppd, num_options, options, 0,
			 &doc.Orientation, &doc.Duplex,
			 &doc.LanguageLevel, &doc.Color,
			 &doc.PageLeft, &doc.PageRight,
			 &doc.PageTop, &doc.PageBottom,
			 &doc.PageWidth, &doc.PageLength,
			 log, ld);

  /* The cfFilterSetCommonOptions() does not set doc.Color
     according to option settings (user's demand for color/gray),
     so we parse the options and set the mode here */
  cfRasterParseIPPOptions(&h, data, 0, 1);
  if (doc.Color)
    doc.Color = h.cupsNumColors <= 1 ? 0 : 1;
  if (!doc.ppd) {
    /* Without PPD use also the other findings of cfRasterParseIPPOptions() */
    doc.Orientation = h.Orientation;
    doc.Duplex = h.Duplex;
    doc.PageWidth = h.cupsPageSize[0] != 0.0 ? h.cupsPageSize[0] :
      (float)h.PageSize[0];
    doc.PageLength = h.cupsPageSize[1] != 0.0 ? h.cupsPageSize[1] :
      (float)h.PageSize[1];
    doc.PageLeft = h.cupsImagingBBox[0] != 0.0 ? h.cupsImagingBBox[0] :
      (float)h.ImagingBoundingBox[0];
    doc.PageBottom = h.cupsImagingBBox[1] != 0.0 ? h.cupsImagingBBox[1] :
      (float)h.ImagingBoundingBox[1];
    doc.PageRight = h.cupsImagingBBox[2] != 0.0 ? h.cupsImagingBBox[2] :
      (float)h.ImagingBoundingBox[2];
    doc.PageTop = h.cupsImagingBBox[3] != 0.0 ? h.cupsImagingBBox[3] :
      (float)h.ImagingBoundingBox[3];
    doc.Flip = h.MirrorPrint ? 1 : 0;
    doc.Collate = h.Collate ? 1 : 0;
    doc.Copies = h.NumCopies;
  }

  if (doc.Copies == 1
      && (choice = ppdFindMarkedChoice(doc.ppd,"Copies")) != NULL) {
    doc.Copies = atoi(choice->choice);
  }
  if (doc.Copies == 0) doc.Copies = 1;
  if ((val = cupsGetOption("Duplex",num_options,options)) != 0 &&
      (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
       !strcasecmp(val, "yes"))) {
      /* for compatiblity */
      if (ppdFindOption(doc.ppd,"Duplex") != NULL) {
        ppdMarkOption(doc.ppd,"Duplex","True");
        ppdMarkOption(doc.ppd,"Duplex","On");
        doc.Duplex = 1;
      }
  } else if ((val = cupsGetOption("sides",num_options,options)) != 0 &&
      (!strcasecmp(val, "two-sided-long-edge") ||
       !strcasecmp(val, "two-sided-short-edge"))) {
      /* for compatiblity */
      if (ppdFindOption(doc.ppd,"Duplex") != NULL) {
        ppdMarkOption(doc.ppd,"Duplex","True");
        ppdMarkOption(doc.ppd,"Duplex","On");
        doc.Duplex = 1;
      }
  }

  if ((val = cupsGetOption("OutputOrder",num_options,options)) != 0	||
	(val = cupsGetOption("outputorder", num_options, options))!=NULL) {
    if (!strcasecmp(val, "Reverse")) {
      doc.Reverse = 1;
    }
  } else if (doc.ppd) {
   /*
    * Figure out the right default output order from the PPD file...
    */

    if ((choice = ppdFindMarkedChoice(doc.ppd,"OutputOrder")) != 0) {
      doc.Reverse = !strcasecmp(choice->choice,"Reverse");
    } else if ((choice = ppdFindMarkedChoice(doc.ppd,"OutputBin")) != 0 &&
        (attr = ppdFindAttr(doc.ppd,"PageStackOrder",choice->choice)) != 0 &&
        attr->value) {
      doc.Reverse = !strcasecmp(attr->value,"Reverse");
    } else if ((attr = ppdFindAttr(doc.ppd,"DefaultOutputOrder",0)) != 0 &&
             attr->value) {
      doc.Reverse = !strcasecmp(attr->value,"Reverse");
    }
  }
    else if(printer_attrs){
	/*	If PPD file is NULL, we use printer attrs to determine if we need to print in reverse order */
	char *defaultoutbin = strdup("");
	const char* outbin;
	if ((ipp = ippFindAttribute(printer_attrs, "output-bin-default", IPP_TAG_ZERO))
      	!= NULL)
    		defaultoutbin = strdup(ippGetString(ipp, 0, NULL));
	/* Find out on which position of the list of output bins the default one is,
	if there is no default bin, take the first of this list */
  	int i = 0;
	if ((ipp = ippFindAttribute(printer_attrs, "output-bin-supported",
				IPP_TAG_ZERO)) != NULL) {
	    int count = ippGetCount(ipp);
		for (i = 0; i < count; i ++) {
		    outbin = ippGetString(ipp, i, NULL);
			if (outbin == NULL)
			    continue;
			if (defaultoutbin == NULL) {
			    defaultoutbin = strdup(outbin);
			    break;
			} else if (strcasecmp(outbin, defaultoutbin) == 0)
			break;
		}
	}
	void *outbin_properties_octet;
	int octet_str_len;
	char outbin_properties[1024];
	int outputorderinfofound = 0;
	int faceupdown = 0;
	int firsttolast = 0;
	if ((ipp = ippFindAttribute(printer_attrs, "printer-output-tray",
		IPP_TAG_STRING)) != NULL &&
			i < ippGetCount(ipp)) {
	    outbin_properties_octet = ippGetOctetString(ipp, i, &octet_str_len);
	    memset(outbin_properties, 0, sizeof(outbin_properties));
	    memcpy(outbin_properties, outbin_properties_octet,
		((size_t)octet_str_len < sizeof(outbin_properties) - 1 ?
		(size_t)octet_str_len : sizeof(outbin_properties) - 1));
		if (strcasestr(outbin_properties, "pagedelivery=faceUp")) {
		    outputorderinfofound = 1;
		    faceupdown = -1;
		}
		if (strcasestr(outbin_properties, "stackingorder=lastToFirst"))
		firsttolast = -1;
	}
	if (outputorderinfofound == 0 && defaultoutbin &&
	strcasestr(defaultoutbin, "face-up"))
	    faceupdown = -1;
	if (defaultoutbin)
	    free (defaultoutbin);
	if (firsttolast * faceupdown < 0)
	    doc.Reverse = 1;;
    }

  /* adjust to even page when duplex */
  if (((val = cupsGetOption("cupsEvenDuplex",num_options,options)) != 0 &&
             (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
               !strcasecmp(val, "yes"))) ||
         ((attr = ppdFindAttr(doc.ppd,"cupsEvenDuplex",0)) != 0 &&
             (!strcasecmp(attr->value, "true")
               || !strcasecmp(attr->value, "on") ||
               !strcasecmp(attr->value, "yes")))) {
    doc.EvenDuplex = 1;
  }

  if ((val = cupsGetOption("multiple-document-handling",
			   num_options, options)) != NULL)
  {
   /*
    * This IPP attribute is unnecessarily complicated...
    *
    *   single-document, separate-documents-collated-copies, and
    *   single-document-new-sheet all require collated copies.
    *
    *   separate-documents-uncollated-copies allows for uncollated copies.
    */

    doc.Collate = strcasecmp(val, "separate-documents-uncollated-copies") != 0;
  }

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL  ||
	(val = cupsGetOption("collate", num_options, options))) {
    if (strcasecmp(val, "True") == 0) {
      doc.Collate = 1;
    }
  } else {
    if ((choice = ppdFindMarkedChoice(doc.ppd,"Collate")) != NULL
      && (!strcasecmp(choice->choice,"true")
        || !strcasecmp(choice->choice, "on")
	|| !strcasecmp(choice->choice, "yes"))) {
      doc.Collate = 1;
    }
    else if((ipp = ippFindAttribute(printer_attrs, "multiple-document-handling-default", 
		IPP_TAG_ZERO))!=NULL){
	ippAttributeString(ipp, buf, sizeof(buf));
	val = buf;
	doc.Collate = strcasecmp(val, "separate-documents-uncollated-copies") != 0;
    }
    /*	Still if we are having no clue about collate, pick any random value(1st in this case)
	from supported options	*/
    else if((ipp = ippFindAttribute(printer_attrs, "multiple-document-handling-supported", 
			IPP_TAG_ZERO))!=NULL){
	ippAttributeString(ipp, buf, sizeof(buf));
	int i=0;
	for(i=0; buf[i]!='\0';i++){
	    if(buf[i]==' ' || buf[i]==','){
	    	buf[i] = '\0';
		break;
	    }
	}
	val = buf;
	doc.Collate = strcasecmp(val, "separate-documents-uncollated-copies") != 0;
    }
  }

  if ((val = cupsGetOption("gamma", num_options, options)) != NULL)
      doc.gammaval = atoi(val) * 0.001f;

  if ((val = cupsGetOption("brightness", num_options, options)) != NULL)
      doc.brightness = atoi(val) * 0.01f;

  if ((val = cupsGetOption("ppi", num_options, options)) != NULL)
  {
    sscanf(val, "%d", &xppi);
    yppi = xppi;
    zoom = 0.0;
  }

  if ((val = cupsGetOption("position", num_options, options)) != NULL)
  {
    if (strcasecmp(val, "center") == 0)
    {
      doc.XPosition = 0;
      doc.YPosition = 0;
    }
    else if (strcasecmp(val, "top") == 0)
    {
      doc.XPosition = 0;
      doc.YPosition = 1;
    }
    else if (strcasecmp(val, "left") == 0)
    {
      doc.XPosition = -1;
      doc.YPosition = 0;
    }
    else if (strcasecmp(val, "right") == 0)
    {
      doc.XPosition = 1;
      doc.YPosition = 0;
    }
    else if (strcasecmp(val, "top-left") == 0)
    {
      doc.XPosition = -1;
      doc.YPosition = 1;
    }
    else if (strcasecmp(val, "top-right") == 0)
    {
      doc.XPosition = 1;
      doc.YPosition = 1;
    }
    else if (strcasecmp(val, "bottom") == 0)
    {
      doc.XPosition = 0;
      doc.YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-left") == 0)
    {
      doc.XPosition = -1;
      doc.YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-right") == 0)
    {
      doc.XPosition = 1;
      doc.YPosition = -1;
    }
  }

  if ((val = cupsGetOption("saturation", num_options, options)) != NULL)
    sat = atoi(val);

  if ((val = cupsGetOption("hue", num_options, options)) != NULL)
    hue = atoi(val);

  if ((val = cupsGetOption("mirror", num_options, options)) != NULL &&
      strcasecmp(val, "True") == 0)
    doc.Flip = 1;

  if ((val = cupsGetOption("emit-jcl", num_options, options)) != NULL &&
      (!strcasecmp(val, "false") || !strcasecmp(val, "off") ||
       !strcasecmp(val, "no") || !strcmp(val, "0")))
    emit_jcl = 0;
  else
    emit_jcl = 1;



 /*
  * Open the input image to print...
  */

  doc.colorspace = doc.Color ? CF_IMAGE_RGB_CMYK : CF_IMAGE_WHITE;

  doc.img = cfImageOpenFP(fp, doc.colorspace, CF_IMAGE_WHITE, sat, hue,
			    NULL);
  if (doc.img != NULL) {

  int margin_defined = 0;
  int fidelity = 0;
  int document_large = 0;

  if (doc.ppd && (doc.ppd->custom_margins[0] || doc.ppd->custom_margins[1] ||
		  doc.ppd->custom_margins[2] || doc.ppd->custom_margins[3]))
                                            // In case of custom margins
    margin_defined = 1;
  else if(customBottom!=0 || customLeft!=0 || customRight!=0 || customTop!=0)
	margin_defined = 1;
  if (doc.PageLength != doc.PageTop - doc.PageBottom ||
      doc.PageWidth != doc.PageRight - doc.PageLeft)
    margin_defined = 1;

  if ((val = cupsGetOption("ipp-attribute-fidelity",num_options,options)) !=
      NULL) {
    if(!strcasecmp(val, "true") || !strcasecmp(val, "yes") ||
        !strcasecmp(val, "on")) {
      fidelity = 1;
    }
  }

  float w = (float)cfImageGetWidth(doc.img);
  float h = (float)cfImageGetHeight(doc.img);
  float pw = doc.PageRight-doc.PageLeft;
  float ph = doc.PageTop-doc.PageBottom;
  int tempOrientation = doc.Orientation;
  if((val = cupsGetOption("orientation-requested",num_options,options))!=NULL) {
    tempOrientation = atoi(val);
  }
  else if((val = cupsGetOption("landscape",num_options,options))!=NULL) {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes")) {
      tempOrientation = 4;
    }
  }
  if(tempOrientation==0) {
    if(((pw > ph) && (w < h)) || ((pw < ph) && (w > h)))
      tempOrientation = 4;
  }
  if(tempOrientation==4||tempOrientation==5) {
    int tmp = pw;
    pw = ph;
    ph = tmp;
  }
  if (w * 72.0 / doc.img->xppi > pw || h * 72.0 / doc.img->yppi > ph)
    document_large = 1;

  if((val = cupsGetOption("print-scaling",num_options,options)) != NULL) {
    if(!strcasecmp(val,"auto")) {
      if(fidelity||document_large) {
        if(margin_defined)
          zoom = 1.0;       // fit method
        else
          fillprint = 1;    // fill method
      }
      else
        cropfit = 1;        // none method
    }
    else if(!strcasecmp(val,"auto-fit")) {
      if(fidelity||document_large)
        zoom = 1.0;         // fit method
      else
        cropfit = 1;        // none method
    }
    else if(!strcasecmp(val,"fill"))
      fillprint = 1;        // fill method
    else if(!strcasecmp(val,"fit"))
      zoom = 1.0;           // fitplot = 1 or fit method
    else
      cropfit=1;            // none or crop-to-fit
  }
  else{       // print-scaling is not defined, look for alternate options.

  if ((val = cupsGetOption("scaling", num_options, options)) != NULL)
    zoom = atoi(val) * 0.01;
  else if (((val =
	     cupsGetOption("fit-to-page", num_options, options)) != NULL) ||
	   ((val = cupsGetOption("fitplot", num_options, options)) != NULL))
  {
    if (!strcasecmp(val, "yes") || !strcasecmp(val, "on") ||
	      !strcasecmp(val, "true"))
      zoom = 1.0;
    else
      zoom = 0.0;
  }
  else if ((val = cupsGetOption("natural-scaling", num_options, options)) !=
	   NULL)
    zoom = 0.0;

  if((val = cupsGetOption("fill",num_options,options))!=0) {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes")) {
      fillprint = 1;
    }
  }

  if((val = cupsGetOption("crop-to-fit",num_options,options))!= NULL){
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes")) {
      cropfit=1;
    }
  } }
  }
  if(fillprint||cropfit)
  {
    /* For cropfit do the math without the unprintable margins to get correct
       centering */
    if (cropfit)
    {
      doc.PageBottom = 0.0;
      doc.PageTop = doc.PageLength;
      doc.PageLeft = 0.0;
      doc.PageRight = doc.PageWidth;
    }
    float w = (float)cfImageGetWidth(doc.img);
    float h = (float)cfImageGetHeight(doc.img);
    float pw = doc.PageRight-doc.PageLeft;
    float ph = doc.PageTop-doc.PageBottom;
    int tempOrientation = doc.Orientation;
    const char *val;
    int flag = 3;
    if((val = cupsGetOption("orientation-requested",num_options,options))!=NULL)
    {
      tempOrientation = atoi(val);
    }
    else if((val = cupsGetOption("landscape",num_options,options))!=NULL)
    {
      if(!strcasecmp(val,"true")||!strcasecmp(val,"yes"))
      {
        tempOrientation = 4;
      }
    }
    if(tempOrientation>0)
    {
      if(tempOrientation==4||tempOrientation==5)
      {
        float temp = pw;
        pw = ph;
        ph = temp;
        flag = 4;
      }
    }
    if(tempOrientation==0)
    {
      if(((pw > ph) && (w < h)) || ((pw < ph) && (w > h)))
      {
        int temp = pw;
        pw = ph;
        ph = temp;
        flag = 4;
      }
    }
    if(fillprint){
      float final_w,final_h;
      if(w*ph/pw <=h){
        final_w =w;
        final_h =w*ph/pw;
      }
      else{
        final_w = h*pw/ph;
        final_h = h;
      }
      float posw=(w-final_w)/2,posh=(h-final_h)/2;
      posw = (1+doc.XPosition)*posw;
      posh = (1-doc.YPosition)*posh;
      cf_image_t *img2 = cfImageCrop(doc.img,posw,posh,final_w,final_h);
      cfImageClose(doc.img);
      doc.img = img2;
    }
    else {
      float final_w=w,final_h=h;
      if (w > pw * doc.img->xppi / 72.0)
	final_w = pw * doc.img->xppi / 72.0;
      if (h > ph * doc.img->yppi / 72.0)
	final_h = ph * doc.img->yppi / 72.0;
      float posw=(w-final_w)/2,posh=(h-final_h)/2;
      posw = (1+doc.XPosition)*posw;
      posh = (1-doc.YPosition)*posh;
      cf_image_t *img2 = cfImageCrop(doc.img,posw,posh,final_w,final_h);
      cfImageClose(doc.img);
      doc.img = img2;
      if(flag==4)
      {
	doc.PageBottom += (doc.PageLength - final_w * 72.0 / doc.img->xppi) / 2;
	doc.PageTop = doc.PageBottom + final_w * 72.0 / doc.img->xppi;
	doc.PageLeft += (doc.PageWidth - final_h * 72.0 / doc.img->yppi) / 2;
	doc.PageRight = doc.PageLeft + final_h * 72.0 / doc.img->yppi;
      }
      else
      {
	doc.PageBottom += (doc.PageLength - final_h * 72.0 / doc.img->yppi) / 2;
	doc.PageTop = doc.PageBottom + final_h * 72.0 / doc.img->yppi;
	doc.PageLeft += (doc.PageWidth - final_w * 72.0 / doc.img->xppi) / 2;
	doc.PageRight = doc.PageLeft + final_w * 72.0 / doc.img->xppi;
      }
      if(doc.PageBottom<0) doc.PageBottom = 0;
      if(doc.PageLeft<0) doc.PageLeft = 0;
    }
  }

  if (!inputseekable)
    unlink(tempfile);

  if (doc.img == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterImageToPDF: Unable to open image file for printing!");
    fclose(doc.outputfp);
    close(outputfd);
    return (1);
  }

  doc.colorspace = cfImageGetColorSpace(doc.img);

 /*
  * Scale as necessary...
  */

  if (zoom == 0.0 && xppi == 0)
  {
    xppi = cfImageGetXPPI(doc.img);
    yppi = cfImageGetYPPI(doc.img);
  }

  if (yppi == 0)
    yppi = xppi;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToPDF: Before scaling: xppi=%d, yppi=%d, zoom=%.2f",
	       xppi, yppi, zoom);

  if (xppi > 0)
  {
   /*
    * Scale the image as neccesary to match the desired pixels-per-inch.
    */

    if (doc.Orientation & 1)
    {
      doc.xprint = (doc.PageTop - doc.PageBottom) / 72.0;
      doc.yprint = (doc.PageRight - doc.PageLeft) / 72.0;
    }
    else
    {
      doc.xprint = (doc.PageRight - doc.PageLeft) / 72.0;
      doc.yprint = (doc.PageTop - doc.PageBottom) / 72.0;
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Before scaling: xprint=%.1f, yprint=%.1f",
		 doc.xprint, doc.yprint);

    doc.xinches = (float)cfImageGetWidth(doc.img) / (float)xppi;
    doc.yinches = (float)cfImageGetHeight(doc.img) / (float)yppi;

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Image size is %.1f x %.1f inches...",
		 doc.xinches, doc.yinches);

    if ((val = cupsGetOption("natural-scaling", num_options, options)) != NULL)
    {
      doc.xinches = doc.xinches * atoi(val) / 100;
      doc.yinches = doc.yinches * atoi(val) / 100;
    }

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Rotate the image if it will fit landscape but not portrait...
      */

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPDF: Auto orientation...");

      if ((doc.xinches > doc.xprint || doc.yinches > doc.yprint) &&
          doc.xinches <= doc.yprint && doc.yinches <= doc.xprint)
      {
       /*
	* Rotate the image as needed...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToPDF: Using landscape orientation...");

	doc.Orientation = (doc.Orientation + 1) & 3;
	doc.xsize       = doc.yprint;
	doc.yprint      = doc.xprint;
	doc.xprint      = doc.xsize;
      }
    }
  }
  else
  {
   /*
    * Scale percentage of page size...
    */

    doc.xprint = (doc.PageRight - doc.PageLeft) / 72.0;
    doc.yprint = (doc.PageTop - doc.PageBottom) / 72.0;
    doc.aspect = (float)cfImageGetYPPI(doc.img) /
      (float)cfImageGetXPPI(doc.img);

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Before scaling: xprint=%.1f, yprint=%.1f",
		 doc.xprint, doc.yprint);

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: cfImageGetXPPI(img) = %d, "
		 "cfImageGetYPPI(img) = %d, aspect = %f",
		 cfImageGetXPPI(doc.img), cfImageGetYPPI(doc.img),
		 doc.aspect);

    doc.xsize = doc.xprint * zoom;
    doc.ysize = doc.xsize * cfImageGetHeight(doc.img) /
      cfImageGetWidth(doc.img) / doc.aspect;

    if (doc.ysize > (doc.yprint * zoom))
    {
      doc.ysize = doc.yprint * zoom;
      doc.xsize = doc.ysize * cfImageGetWidth(doc.img) *
	doc.aspect / cfImageGetHeight(doc.img);
    }

    doc.xsize2 = doc.yprint * zoom;
    doc.ysize2 = doc.xsize2 * cfImageGetHeight(doc.img) /
      cfImageGetWidth(doc.img) / doc.aspect;

    if (doc.ysize2 > (doc.xprint * zoom))
    {
      doc.ysize2 = doc.xprint * zoom;
      doc.xsize2 = doc.ysize2 * cfImageGetWidth(doc.img) *
	doc.aspect / cfImageGetHeight(doc.img);
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Portrait size is %.2f x %.2f inches",
		 doc.xsize, doc.ysize);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Landscape size is %.2f x %.2f inches",
		 doc.xsize2, doc.ysize2);

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Choose the rotation with the largest area, but prefer
      * portrait if they are equal...
      */

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPDF: Auto orientation...");

      if ((doc.xsize * doc.ysize) < (doc.xsize2 * doc.xsize2))
      {
       /*
	* Do landscape orientation...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToPDF: Using landscape orientation...");

	doc.Orientation = 1;
	doc.xinches     = doc.xsize2;
	doc.yinches     = doc.ysize2;
	doc.xprint      = (doc.PageTop - doc.PageBottom) / 72.0;
	doc.yprint      = (doc.PageRight - doc.PageLeft) / 72.0;
      }
      else
      {
       /*
	* Do portrait orientation...
	*/

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterImageToPDF: Using portrait orientation...");

	doc.Orientation = 0;
	doc.xinches     = doc.xsize;
	doc.yinches     = doc.ysize;
      }
    }
    else if (doc.Orientation & 1)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPDF: Using landscape orientation...");

      doc.xinches     = doc.xsize2;
      doc.yinches     = doc.ysize2;
      doc.xprint      = (doc.PageTop - doc.PageBottom) / 72.0;
      doc.yprint      = (doc.PageRight - doc.PageLeft) / 72.0;
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterImageToPDF: Using portrait orientation...");

      doc.xinches     = doc.xsize;
      doc.yinches     = doc.ysize;
      doc.xprint      = (doc.PageRight - doc.PageLeft) / 72.0;
      doc.yprint      = (doc.PageTop - doc.PageBottom) / 72.0;
    }
  }

 /*
  * Compute the number of pages to print and the size of the image on each
  * page...
  */

  if (zoom == 1.0) {
    /* If fitplot is specified, make xpages, ypages 1 forcedly.
       Because calculation error may be caused and
          result of ceil function may be larger than 1.
    */
    doc.xpages = doc.ypages = 1;
  } else {
    doc.xpages = ceil(doc.xinches / doc.xprint);
    doc.ypages = ceil(doc.yinches / doc.yprint);
  }

  doc.xprint = doc.xinches / doc.xpages;
  doc.yprint = doc.yinches / doc.ypages;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToPDF: xpages = %dx%.2fin, ypages = %dx%.2fin",
	       doc.xpages, doc.xprint, doc.ypages, doc.yprint);

 /*
  * Update the page size for custom sizes...
  */

  /* If size if specified by user, use it, else default size from
     printer_attrs*/
  if ((ipp = ippFindAttribute(job_attrs, "media-size", IPP_TAG_ZERO)) != NULL ||
      (val = cupsGetOption("MediaSize", num_options, options)) != NULL ||
      (ipp = ippFindAttribute(job_attrs, "page-size", IPP_TAG_ZERO)) != NULL ||
      (val = cupsGetOption("PageSize", num_options, options)) != NULL ) {
    if (val == NULL) {
      ippAttributeString(ipp, buf, sizeof(buf));
      strcpy(defSize, buf);
    }
    else
	snprintf(defSize, sizeof(defSize), "%s", val);
  }



  if (((choice = ppdFindMarkedChoice(doc.ppd, "PageSize")) != NULL &&
      strcasecmp(choice->choice, "Custom") == 0) ||
	strncasecmp(defSize, "custom", 6)==0)
  {
    float	width,		/* New width in points */
		length;		/* New length in points */
    char	s[255];		/* New custom page size... */


   /*
    * Use the correct width and length for the current orientation...
    */

    if (doc.Orientation & 1)
    {
      width  = doc.yprint * 72.0;
      length = doc.xprint * 72.0;
    }
    else
    {
      width  = doc.xprint * 72.0;
      length = doc.yprint * 72.0;
    }

   /*
    * Add margins to page size...
    */
    if(doc.ppd!=NULL){
    	width  += doc.ppd->custom_margins[0] + doc.ppd->custom_margins[2];
    	length += doc.ppd->custom_margins[1] + doc.ppd->custom_margins[3];
    }
    else{
	width += customLeft + customRight;
	length += customTop + customBottom;
    }
   /*
    * Enforce minimums...
    */

    if(doc.ppd!=NULL)
    {
	if(width<doc.ppd->custom_min[0])
	  width = doc.ppd->custom_min[0];
	if(length<doc.ppd->custom_min[1])
	  length = doc.ppd->custom_min[1];
    }
    else{
	if(width<min_width)
	  width = min_width;
	if(length<min_length)
	  length = min_length;
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterImageToPDF: Updated custom page size to %.2f x %.2f "
		 "inches...",
		 width / 72.0, length / 72.0);

   /*
    * Set the new custom size...
    */

    sprintf(s, "Custom.%.0fx%.0f", width, length);
    ppdMarkOption(doc.ppd, "PageSize", s);

   /*
    * Update page variables...
    */

    doc.PageWidth  = width;
    doc.PageLength = length;
    if (doc.ppd != NULL)
      doc.PageLeft   = doc.ppd->custom_margins[0];
    else
      doc.PageLeft = customLeft;
    if (doc.ppd != NULL)
      doc.PageRight  = width - doc.ppd->custom_margins[2];
    else
      doc.PageRight = width - customRight;
    if (doc.ppd != NULL)
      doc.PageBottom = doc.ppd->custom_margins[1];
    else
      doc.PageBottom = customBottom;
    if (doc.ppd != NULL)
      doc.PageTop    = length - doc.ppd->custom_margins[3];
    else
      doc.PageTop = length - customTop;
  }

  if (doc.Copies == 1) {
    /* collate is not needed */
    doc.Collate = 0;
    ppdMarkOption(doc.ppd,"Collate","False");
  }
  if (!doc.Duplex) {
    /* evenDuplex is not needed */
    doc.EvenDuplex = 0;
  }

  /* check collate device */
  if (doc.Collate) {
    if ((choice = ppdFindMarkedChoice(doc.ppd,"Collate")) != NULL &&
       !strcasecmp(choice->choice,"true")) {
      ppd_option_t *opt;

      if ((opt = ppdFindOption(doc.ppd,"Collate")) != NULL &&
        !opt->conflicted) {
        deviceCollate = 1;
      } else {
        ppdMarkOption(doc.ppd,"Collate","False");
      }
    }
  }
  /* check OutputOrder device */
  if (doc.Reverse) {
    if (ppdFindOption(doc.ppd,"OutputOrder") != NULL) {
      deviceReverse = 1;
    }
  }
  if (doc.ppd != NULL &&
       !doc.ppd->manual_copies && doc.Collate && !deviceCollate) {
    /* Copying by device , software collate is impossible */
    /* Enable software copying */
    doc.ppd->manual_copies = 1;
  }
  if (doc.Copies > 1 && (doc.ppd == NULL || doc.ppd->manual_copies)
      && doc.Duplex) {
    /* Enable software collate , or same pages are printed in both sides */
      doc.Collate = 1;
      if (deviceCollate) {
        deviceCollate = 0;
        ppdMarkOption(doc.ppd,"Collate","False");
      }
  }
  if (doc.Duplex && doc.Collate && !deviceCollate) {
    /* Enable evenDuplex or the first page may be printed other side of the
      end of precedings */
    doc.EvenDuplex = 1;
  }
  if (doc.Duplex && doc.Reverse && !deviceReverse) {
    /* Enable evenDuplex or the first page may be empty. */
    doc.EvenDuplex = 1;
  }
  /* change feature for software */
  if (deviceCollate) {
    doc.Collate = 0;
  }
  if (deviceReverse) {
    doc.Reverse = 0;
  }
  if (doc.ppd != NULL) {
    if (doc.ppd->manual_copies) {
      /* sure disable hardware copying */
      ppdMarkOption(doc.ppd,"Copies","1");
      ppdMarkOption(doc.ppd,"JCLCopies","1");
    } else {
      /* change for hardware copying */
      deviceCopies = doc.Copies;
      doc.Copies = 1;
    }
  }

 /*
  * See if we need to collate, and if so how we need to do it...
  */

  if (doc.xpages == 1 && doc.ypages == 1
      && (doc.Collate || deviceCollate) && !doc.EvenDuplex) {
    /* collate is not needed, disable it */
    deviceCollate = 0;
    doc.Collate = 0;
    ppdMarkOption(doc.ppd,"Collate","False");
  }

  if (((doc.xpages*doc.ypages) % 2) == 0) {
    /* even pages, disable EvenDuplex */
    doc.EvenDuplex = 0;
  }

 /*
  * Write any "exit server" options that have been selected...
  */

  ppdEmit(doc.ppd, doc.outputfp, PPD_ORDER_EXIT);

 /*
  * Write any JCL commands that are needed to print PostScript code...
  */

  if (doc.ppd && emit_jcl) {
    /* pdftopdf only adds JCL to the job if the printer is a native PDF
       printer and the PPD is for this mode, having the "*JCLToPDFInterpreter:"
       keyword. We need to read this keyword manually from the PPD and replace
       the content of ppd->jcl_ps by the value of this keyword, so that
       ppdEmitJCL() actalually adds JCL based on the presence on
       "*JCLToPDFInterpreter:". */
    ppd_attr_t *attr;
    char buf[1024];
    int devicecopies_done = 0;
    char *old_jcl_ps = doc.ppd->jcl_ps;
    /* If there is a "Copies" option in the PPD file, assure that hardware
       copies are implemented as described by this option */
    if (ppdFindOption(doc.ppd,"Copies") != NULL && deviceCopies > 1)
    {
      snprintf(buf,sizeof(buf),"%d",deviceCopies);
      ppdMarkOption(doc.ppd,"Copies",buf);
      devicecopies_done = 1;
    }
    if ((attr = ppdFindAttr(doc.ppd,"JCLToPDFInterpreter",NULL)) != NULL)
    {
      if (deviceCopies > 1 && devicecopies_done == 0 && /* HW copies */
	  strncmp(doc.ppd->jcl_begin, "\033%-12345X@", 10) == 0) /* PJL */
      {
	/* Add a PJL command to implement the hardware copies */
        const size_t size = strlen(attr->value) + 1 + 30;
        doc.ppd->jcl_ps = (char *)malloc(size * sizeof(char));
        if (deviceCollate)
	{
          snprintf(doc.ppd->jcl_ps, size, "@PJL SET QTY=%d\n%s",
                   deviceCopies, attr->value);
        }
	else
	{
          snprintf(doc.ppd->jcl_ps, size, "@PJL SET COPIES=%d\n%s",
                   deviceCopies, attr->value);
        }
      }
      else
	doc.ppd->jcl_ps = strdup(attr->value);
      ppd_decode(doc.ppd->jcl_ps);
      pdf_printer = 1;
    }
    else
    {
      doc.ppd->jcl_ps = NULL;
      pdf_printer = 0;
    }
    ppdEmitJCL(doc.ppd, doc.outputfp, data->job_id, data->job_user,
	       data->job_title);
    emit_jcl_options(&doc, doc.outputfp, deviceCopies);
    free(doc.ppd->jcl_ps);
    doc.ppd->jcl_ps = old_jcl_ps; /* cups uses pool allocator, not free() */
  }

 /*
  * Start sending the document with any commands needed...
  */

  if (out_prologue(&doc, doc.Copies * doc.xpages * doc.ypages +
		  (doc.EvenDuplex ? 1 : 0)) < 0)
    goto out_of_memory;

 /*
  * Output the pages...
  */

  doc.row = malloc(cfImageGetWidth(doc.img) * abs(doc.colorspace) + 3);

  if (log) {
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPDF: XPosition=%d, YPosition=%d, Orientation=%d",
	doc.XPosition, doc.YPosition, doc.Orientation);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPDF: xprint=%.2f, yprint=%.2f", doc.xprint, doc.yprint);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPDF: PageLeft=%.0f, PageRight=%.0f, PageWidth=%.0f",
	doc.PageLeft, doc.PageRight, doc.PageWidth);
    log(ld, CF_LOGLEVEL_DEBUG,
	"cfFilterImageToPDF: PageBottom=%.0f, PageTop=%.0f, PageLength=%.0f",
	doc.PageBottom, doc.PageTop, doc.PageLength);
  }

  if (doc.Flip) {
    pr = doc.PageWidth - doc.PageLeft;
    pl = doc.PageWidth - doc.PageRight;
  } else {
    pr = doc.PageRight;
    pl = doc.PageLeft;
  }

  switch (doc.Orientation)
  {
    default :
	switch (doc.XPosition)
	{
	  case -1 :
	      doc.left = pl;
	      break;
	  default :
              doc.left = (pr + pl - doc.xprint * 72) / 2;
	      break;
	  case 1 :
	      doc.left = pr - doc.xprint * 72;
	      break;
	}

	switch (doc.YPosition)
	{
	  case -1 :
	      doc.top = doc.PageBottom;
	      break;
	  default :
	      doc.top = (doc.PageTop + doc.PageBottom - doc.yprint * 72) / 2;
	      break;
	  case 1 :
	      doc.top = doc.PageTop - doc.yprint * 72;;
	      break;
	}
	break;

    case 1 :
	switch (doc.XPosition)
	{
	  case -1 :
              doc.left = doc.PageBottom;
	      break;
	  default :
              doc.left = (doc.PageTop + doc.PageBottom - doc.xprint * 72) / 2;
	      break;
	  case 1 :
              doc.left = doc.PageTop - doc.xprint * 72;
	      break;
	}

	switch (doc.YPosition)
	{
	  case -1 :
	      doc.top = pl;
	      break;
	  default :
	      doc.top = (pr + pl - doc.yprint * 72) / 2;
	      break;
	  case 1 :
	      doc.top = pr - doc.yprint * 72;;
	      break;
	}
	break;

    case 2 :
	switch (doc.XPosition)
	{
	  case -1 :
              doc.left = pr - doc.xprint * 72;
	      break;
	  default :
              doc.left = (pr + pl - doc.xprint * 72) / 2;
	      break;
	  case 1 :
              doc.left = pl;
	      break;
	}

	switch (doc.YPosition)
	{
	  case -1 :
	      doc.top = doc.PageTop - doc.yprint * 72;
	      break;
	  default :
	      doc.top = (doc.PageTop + doc.PageBottom - doc.yprint * 72) / 2;
	      break;
	  case 1 :
	      doc.top = doc.PageBottom;
	      break;
	}
	break;

    case 3 :
	switch (doc.XPosition)
	{
	  case -1 :
              doc.left = doc.PageTop - doc.xprint * 72;
	      break;
	  default :
              doc.left = (doc.PageTop + doc.PageBottom - doc.xprint * 72) / 2;
	      break;
	  case 1 :
              doc.left = doc.PageBottom;
	      break;
	}

	switch (doc.YPosition)
	{
	  case -1 :
	      doc.top = pr - doc.yprint * 72;;
	      break;
	  default :
	      doc.top = (pr + pl - doc.yprint * 72) / 2;
	      break;
	  case 1 :
	      doc.top = pl;
	      break;
	}
	break;
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterImageToPDF: left=%.2f, top=%.2f", doc.left, doc.top);

  if (doc.Collate)
  {
    int *contentsObjs;
    int *imgObjs;

    if ((contentsObjs = malloc(sizeof(int)*doc.xpages*doc.ypages)) == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPDF: Can't allocate contentsObjs");
      goto out_of_memory;
    }
    if ((imgObjs = malloc(sizeof(int)*doc.xpages*doc.ypages)) == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterImageToPDF: Can't allocate imgObjs");
      goto out_of_memory;
    }
    for (doc.xpage = 0; doc.xpage < doc.xpages; doc.xpage ++)
      for (doc.ypage = 0; doc.ypage < doc.ypages; doc.ypage ++)
      {
	int imgObj;
	int contentsObj;

	if (iscanceled && iscanceled(icd))
	{
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterImageToPDF: Job canceled");
	  goto canceled;
	}

	if ((contentsObj = contentsObjs[doc.ypages*doc.xpage+doc.ypage] =
	     new_obj(&doc)) < 0)
	  goto out_of_memory;
	if ((imgObj = imgObjs[doc.ypages*doc.xpage+doc.ypage] =
	     new_obj(&doc)) < 0)
	  goto out_of_memory;

	/* out contents object */
	if (out_page_contents(&doc, contentsObj) < 0)
	  goto out_of_memory;

	/* out image object */
	if (out_image(&doc, imgObj) < 0)
	  goto out_of_memory;
      }
    for (doc.page = 0; doc.Copies > 0 ; doc.Copies --) {
      for (doc.xpage = 0; doc.xpage < doc.xpages; doc.xpage ++)
	for (doc.ypage = 0; doc.ypage < doc.ypages; doc.ypage ++, doc.page ++)
	{
	  if (iscanceled && iscanceled(icd))
	  {
	    if (log) log(ld, CF_LOGLEVEL_DEBUG,
			 "cfFilterImageToPDF: Job canceled");
	    goto canceled;
	  }

	  /* out Page Object */
	  if (out_page_object(&doc, doc.pageObjects[doc.page],
			    contentsObjs[doc.ypages * doc.xpage + doc.ypage],
			    imgObjs[doc.ypages * doc.xpage + doc.ypage]) < 0)
	    goto out_of_memory;
	  if (pdf_printer && log)
	    log(ld, CF_LOGLEVEL_CONTROL,
		"PAGE: %d %d\n", doc.page+1, 1);
	}
      if (doc.EvenDuplex) {
	/* out empty page */
	if (out_page_object(&doc, doc.pageObjects[doc.page], -1, -1) < 0)
	  goto out_of_memory;
	if (pdf_printer && log)
	  log(ld, CF_LOGLEVEL_CONTROL,
	      "PAGE: %d %d\n", doc.page+1, 1);
      }
    }
    free(contentsObjs);
    free(imgObjs);
  }
  else
  {
    for (doc.page = 0, doc.xpage = 0; doc.xpage < doc.xpages; doc.xpage ++)
      for (doc.ypage = 0; doc.ypage < doc.ypages; doc.ypage ++)
      {
	int imgObj;
	int contentsObj;
	int p;

	if (iscanceled && iscanceled(icd))
	{
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterImageToPDF: Job canceled");
	  goto canceled;
	}

	if ((imgObj = new_obj(&doc)) < 0)
	  goto out_of_memory;
	if ((contentsObj = new_obj(&doc)) < 0)
	  goto out_of_memory;

	/* out contents object */
	if (out_page_contents(&doc, contentsObj) < 0)
	  goto out_of_memory;

	/* out image object */
	if (out_image(&doc, imgObj) < 0)
	  goto out_of_memory;

	for (p = 0;p < doc.Copies;p++, doc.page++)
	{
	  if (iscanceled && iscanceled(icd))
	  {
	    if (log) log(ld, CF_LOGLEVEL_DEBUG,
			 "cfFilterImageToPDF: Job canceled");
	    goto canceled;
	  }

	  /* out Page Object */
	  if (out_page_object(&doc, doc.pageObjects[doc.page], contentsObj,
			    imgObj) < 0)
	    goto out_of_memory;
	  if (pdf_printer && log)
	    log(ld, CF_LOGLEVEL_CONTROL,
		"PAGE: %d %d\n", doc.page+1, 1);
	}
      }
    if (doc.EvenDuplex) {
      /* out empty pages */
      int p;

      for (p = 0;p < doc.Copies;p++, doc.page++)
      {
	if (iscanceled && iscanceled(icd))
	{
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterImageToPDF: Job canceled");
	  goto canceled;
	}

	if (out_page_object(&doc, doc.pageObjects[doc.page], -1, -1) < 0)
	  goto out_of_memory;
	if (pdf_printer && log)
	  log(ld, CF_LOGLEVEL_CONTROL,
	      "PAGE: %d %d\n", doc.page+1, 1);
      }
    }
  }

 canceled:
  out_xref(&doc);
  out_trailer(&doc);
  free_all_obj(&doc);
 /*
  * Close files...
  */

  if (emit_jcl)
  {
    if (doc.ppd && doc.ppd->jcl_end)
      ppdEmitJCLEnd(doc.ppd, doc.outputfp);
  }

  cfImageClose(doc.img);
  fclose(doc.outputfp);
  close(outputfd);
  return (0);

 out_of_memory:

  if (log) log(ld, CF_LOGLEVEL_ERROR,
	       "cfFilterImageToPDF: Cannot allocate any more memory.");
  free_all_obj(&doc);
  cfImageClose(doc.img);
  fclose(doc.outputfp);
  close(outputfd);
  return (2);
}

#ifdef OUT_AS_HEX
/*
 * 'out_hex()' - Print binary data as a series of hexadecimal numbers.
 */

static void
out_hex(imagetopdf_doc_t *doc,
	cf_ib_t *data,		/* I - Data to print */
	int       length,		/* I - Number of bytes to print */
	int       last_line)		/* I - Last line of raster data? */
{
  static int	col = 0;		/* Current column */
  static char	*hex = "0123456789ABCDEF";
					/* Hex digits */


  while (length > 0)
  {
   /*
    * Put the hex chars out to the file; note that we don't use printf()
    * for speed reasons...
    */

    putc_pdf(doc, hex[*data >> 4]);
    putc_pdf(doc, hex[*data & 15]);

    data ++;
    length --;

    col += 2;
    if (col > 78)
    {
      putc_pdf(doc, '\n');
      col = 0;
    }
  }

  if (last_line && col)
  {
    putc_pdf(doc, '\n');
    col = 0;
  }
}
#else

#ifdef OUT_AS_ASCII85
/*
 * 'out_ascii85()' - Print binary data as a series of base-85 numbers.
 */

static void
out_ascii85(imagetopdf_doc_t *doc,
	    cf_ib_t *data,		/* I - Data to print */
	    int       length,		/* I - Number of bytes to print */
	    int       last_line)	/* I - Last line of raster data? */
{
  unsigned	b;			/* Binary data word */
  unsigned char	c[6];			/* ASCII85 encoded chars */
  static int	col = 0;		/* Current column */


  c[5] = '\0'; /* end mark */
  while (length > 3)
  {
    b = (((((data[0] << 8) | data[1]) << 8) | data[2]) << 8) | data[3];

    if (b == 0)
    {
      putc_pdf(doc, 'z');
      col ++;
    }
    else
    {
      c[4] = (b % 85) + '!';
      b /= 85;
      c[3] = (b % 85) + '!';
      b /= 85;
      c[2] = (b % 85) + '!';
      b /= 85;
      c[1] = (b % 85) + '!';
      b /= 85;
      c[0] = b + '!';

      out_pdf(doc, c);
      col += 5;
    }

    data += 4;
    length -= 4;

    if (col >= 75)
    {
      putc_pdf(doc, '\n');
      col = 0;
    }
  }

  if (last_line)
  {
    if (length > 0)
    {
      memset(data + length, 0, 4 - length);
      b = (((((data[0] << 8) | data[1]) << 8) | data[2]) << 8) | data[3];

      c[4] = (b % 85) + '!';
      b /= 85;
      c[3] = (b % 85) + '!';
      b /= 85;
      c[2] = (b % 85) + '!';
      b /= 85;
      c[1] = (b % 85) + '!';
      b /= 85;
      c[0] = b + '!';

      c[length+1] = '\0';
      out_pdf(doc, c);
    }

    out_pdf(doc, "~>");
    col = 0;
  }
}
#else
/*
 * 'out_bin()' - Print binary data as binary.
 */

static void
out_bin(imagetopdf_doc_t *doc,
	cf_ib_t *data,		/* I - Data to print */
	int       length,		/* I - Number of bytes to print */
	int       last_line)		/* I - Last line of raster data? */
{
  while (length > 0)
  {
    putc_pdf(doc, *data);
    data ++;
    length --;
  }

  if (last_line)
  {
    putc_pdf(doc, '\n');
  }
}
#endif
#endif

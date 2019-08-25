/*
 * Image file to PDF filter for the Common UNIX Printing System (CUPS).
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
#include "common.h"
#include <cupsfilters/image.h>
#include <cupsfilters/raster.h>
#include <math.h>
#include <ctype.h>

#if CUPS_VERSION_MAJOR < 1 \
  || (CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR < 2)
#ifndef CUPS_1_1
#error Installed libs and specified source Version mismatch \
	Libs >= 1.2 && Source < 1.2
#define CUPS_1_1
#endif
#else
#ifdef CUPS_1_1
#error Installed libs and specified source Version mismatch \
	Libs < 1.2 && Source >= 1.2
#undef CUPS_1_1
#endif
#endif

#define USE_CONVERT_CMD
//#define OUT_AS_HEX
//#define OUT_AS_ASCII85

/*
 * Globals...
 */

int	Flip = 0,		/* Flip/mirror pages */
	XPosition = 0,		/* Horizontal position on page */
	YPosition = 0,		/* Vertical position on page */
	Collate = 0,		/* Collate copies? */
	Copies = 1,		/* Number of copies */
	Reverse = 0,		/* Output order */
	EvenDuplex = 0;		/* cupsEvenDuplex */

#ifdef CUPS_1_1
#define cups_ib_t ib_t
#define cups_image_t image_t
#define CUPS_IMAGE_CMYK IMAGE_CMYK
#define CUPS_IMAGE_WHITE IMAGE_WHITE
#define CUPS_IMAGE_RGB IMAGE_RGB
#define CUPS_IMAGE_RGB_CMYK IMAGE_RGB_CMYK
#define cupsImageOpen ImageOpen
#define cupsImageClose ImageClose
#define cupsImageGetColorSpace(img) (img->colorspace)
#define cupsImageGetXPPI(img) (img->xppi)
#define cupsImageGetYPPI(img) (img->yppi)
#define cupsImageGetWidth(img) (img->xsize)
#define cupsImageGetHeight(img) (img->ysize)
#define cupsImageGetRow ImageGetRow
#endif

/*
 * Local functions...
 */

#ifdef OUT_AS_HEX
static void	out_hex(cups_ib_t *, int, int);
#else
#ifdef OUT_AS_ASCII85
static void	out_ascii85(cups_ib_t *, int, int);
#else
static void	out_bin(cups_ib_t *, int, int);
#endif
#endif
static void	outPdf(const char *str);
static void	putcPdf(char c);
static int	newObj(void);
static void	freeAllObj(void);
static void	outXref(void);
static void	outTrailer(void);
static void	outString(const char *s);
static void	outPrologue(int nPages);
static void	allocPageObjects(int noPages);
static void	setOffset(int obj);
static void	outPageObject(int pageObj, int contentsObj, int imgObj);
static void	outPageContents(int contentsObj);
static void	outImage(int imgObj);

struct pdfObject {
    int offset;
};

static struct pdfObject *objects = NULL;
static int currentObjectNo = 0;
static int allocatedObjectNum = 0;
static int currentOffset = 0;
static int xrefOffset;
static int *pageObjects = NULL;
static int catalogObj;
static int pagesObj;
static const char *title;
static int	xpages,			/* # x pages */
		ypages,			/* # y pages */
		xpage,			/* Current x page */
		ypage,			/* Current y page */
		page;			/* Current page number */
static int	xc0, yc0,			/* Corners of the page in image coords */
		xc1, yc1;
static float	left, top;		/* Left and top of image */
static float	xprint,			/* Printable area */
		yprint,
		xinches,		/* Total size in inches */
		yinches;
static float	xsize,			/* Total size in points */
		ysize,
		xsize2,
		ysize2;
static float	aspect;			/* Aspect ratio */
static cups_image_t	*img;			/* Image to print */
static int	colorspace;		/* Output colorspace */
static cups_ib_t	*row;		/* Current row */
static float	gammaval = 1.0;		/* Gamma correction value */
static float	brightness = 1.0;	/* Gamma correction value */
static ppd_file_t	*ppd;			/* PPD file */

#define N_OBJECT_ALLOC 100
#define LINEBUFSIZE 1024

static char linebuf[LINEBUFSIZE];

void emitJCLOptions(FILE *fp, int copies)
{
  int section;
  ppd_choice_t **choices;
  int i;
  char buf[1024];
  ppd_attr_t *attr;
  int pdftopdfjcl = 0;
  int datawritten = 0;

  if (ppd == 0) return;
  if ((attr = ppdFindAttr(ppd,"pdftopdfJCLBegin",NULL)) != NULL) {
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
  if (ppdFindOption(ppd,"Copies") != NULL) {
    ppdMarkOption(ppd,"Copies",buf);
  } else {
    if ((attr = ppdFindAttr(ppd,"pdftopdfJCLCopies",buf)) != NULL) {
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

    n = ppdCollect(ppd,(ppd_section_t)section,&choices);
    for (i = 0;i < n;i++) {
      snprintf(buf,sizeof(buf),"pdftopdfJCL%s",
        ((ppd_option_t *)(choices[i]->option))->keyword);
      if ((attr = ppdFindAttr(ppd,buf,choices[i]->choice)) != NULL) {
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


static void setOffset(int obj)
{
  objects[obj].offset = currentOffset;
}

static void allocPageObjects(int nPages)
{
  int i;

  if ((pageObjects = malloc(sizeof(int)*nPages)) == NULL)
  {
    fprintf(stderr,"ERROR: Can't allocate pageObjects\n");
    exit(2);
  }
  for (i = 0;i < nPages;i++)
  {
    pageObjects[i] = newObj();
  }
}

static int newObj(void)
{
  if (objects == NULL)
  {
    if ((objects = malloc(sizeof(struct pdfObject)*N_OBJECT_ALLOC))
	  == NULL)
    {
      fprintf(stderr,"ERROR: Can't allocate objects\n");
      exit(2);
    }
    allocatedObjectNum = N_OBJECT_ALLOC;
  }
  if (currentObjectNo >= allocatedObjectNum)
  {
    if ((objects = realloc(objects,
	  sizeof(struct pdfObject)*(allocatedObjectNum+N_OBJECT_ALLOC)))
	  == NULL)
    {
      fprintf(stderr,"ERROR: Can't allocate objects\n");
      exit(2);
    }
    allocatedObjectNum += N_OBJECT_ALLOC;
  }
  objects[currentObjectNo].offset = currentOffset;
  return currentObjectNo++;
}

static void freeAllObj(void)
{
  if (objects != NULL)
  {
    free(objects);
    objects = NULL;
  }
}

static void putcPdf(char c)
{
  fputc(c,stdout);
  currentOffset++;
}

static void outPdf(const char *str)
{
  unsigned long len = strlen(str);

  fputs(str,stdout);
  currentOffset += len;
}

static void outXref(void)
{
  char buf[21];
  int i;

  xrefOffset = currentOffset;
  outPdf("xref\n");
  snprintf(buf,21,"0 %d\n",currentObjectNo);
  outPdf(buf);
  outPdf("0000000000 65535 f \n");
  for (i = 1;i < currentObjectNo;i++)
  {
    snprintf(buf,21,"%010d 00000 n \n",objects[i].offset);
    outPdf(buf);
  }
}

static void outString(const char *s)
{
  char c;

  putcPdf('(');
  for (;(c = *s) != '\0';s++) {
    if (c == '\\' || c == '(' || c == ')') {
      putcPdf('\\');
    }
    putcPdf(c);
  }
  putcPdf(')');
}

static void outTrailer(void)
{
  time_t	curtime;
  struct tm	*curtm;
  char		curdate[255];

  curtime = time(NULL);
  curtm = localtime(&curtime);
  strftime(curdate, sizeof(curdate),"D:%Y%m%d%H%M%S%z", curtm);

  outPdf("trailer\n");
  snprintf(linebuf, LINEBUFSIZE,"<</Size %d ",currentObjectNo);
  outPdf(linebuf);
  outPdf("/Root 1 0 R\n");
  outPdf("/Info << /Title ");
  outString(title);
  putcPdf(' ');
  snprintf(linebuf,LINEBUFSIZE,"/CreationDate (%s) ",curdate);
  outPdf(linebuf);
  snprintf(linebuf,LINEBUFSIZE,"/ModDate (%s) ",curdate);
  outPdf(linebuf);
  outPdf("/Producer (imagetopdf) ");
  outPdf("/Trapped /False >>\n");
  outPdf(">>\n");
  outPdf("startxref\n");
  snprintf(linebuf,LINEBUFSIZE,"%d\n",xrefOffset);
  outPdf(linebuf);
  outPdf("%%EOF\n");
}

static void outPrologue(int nPages)
{
  int i;

  /* out header */
  newObj(); /* dummy for no 0 object */
  outPdf("%PDF-1.3\n");
  /* out binary for transfer program */
  linebuf[0] = '%';
  linebuf[1] = (char)129;
  linebuf[2] = (char)130;
  linebuf[3] = (char)131;
  linebuf[4] = (char)132;
  linebuf[5] = '\n';
  linebuf[6] = (char)0;
  outPdf(linebuf);
  outPdf("% This file was generated by imagetopdf\n");

  catalogObj = newObj();
  pagesObj = newObj();
  allocPageObjects(nPages);

  /* out catalog */
  snprintf(linebuf,LINEBUFSIZE,
    "%d 0 obj <</Type/Catalog /Pages %d 0 R ",catalogObj,pagesObj);
  outPdf(linebuf);
  outPdf(">> endobj\n");

  /* out Pages */
  setOffset(pagesObj);
  snprintf(linebuf,LINEBUFSIZE,
    "%d 0 obj <</Type/Pages /Kids [ ",pagesObj);
  outPdf(linebuf);
  if (Reverse) {
    for (i = nPages-1;i >= 0;i--)
    {
      snprintf(linebuf,LINEBUFSIZE,"%d 0 R ",pageObjects[i]);
      outPdf(linebuf);
    }
  } else {
    for (i = 0;i < nPages;i++)
    {
      snprintf(linebuf,LINEBUFSIZE,"%d 0 R ",pageObjects[i]);
      outPdf(linebuf);
    }
  }
  outPdf("] ");
  snprintf(linebuf,LINEBUFSIZE,"/Count %d >> endobj\n",nPages);
  outPdf(linebuf);
}

static void outPageObject(int pageObj, int contentsObj, int imgObj)
{
  int trfuncObj;
  int lengthObj;
  int startOffset;
  int length;
  int outTrfunc = (gammaval != 1.0 || brightness != 1.0);

  /* out Page Object */
  setOffset(pageObj);
  snprintf(linebuf,LINEBUFSIZE,
    "%d 0 obj <</Type/Page /Parent %d 0 R ",
    pageObj,pagesObj);
  outPdf(linebuf);
  snprintf(linebuf,LINEBUFSIZE,
    "/MediaBox [ 0 0 %f %f ] ",PageWidth,PageLength);
  outPdf(linebuf);
  snprintf(linebuf,LINEBUFSIZE,
    "/TrimBox [ 0 0 %f %f ] ",PageWidth,PageLength);
  outPdf(linebuf);
  snprintf(linebuf,LINEBUFSIZE,
    "/CropBox [ 0 0 %f %f ] ",PageWidth,PageLength);
  outPdf(linebuf);
  if (contentsObj >= 0) {
    snprintf(linebuf,LINEBUFSIZE,
      "/Contents %d 0 R ",contentsObj);
    outPdf(linebuf);
    snprintf(linebuf,LINEBUFSIZE,
      "/Resources <</ProcSet [/PDF] "
      "/XObject << /Im %d 0 R >>\n",imgObj);
    outPdf(linebuf);
  } else {
    /* empty page */
    snprintf(linebuf,LINEBUFSIZE,
      "/Resources <</ProcSet [/PDF] \n");
    outPdf(linebuf);
  }
  if (outTrfunc) {
    trfuncObj = newObj();
    lengthObj = newObj();
    snprintf(linebuf,LINEBUFSIZE,
      "/ExtGState << /GS1 << /TR %d 0 R >> >>\n",trfuncObj);
    outPdf(linebuf);
  }
  outPdf("     >>\n>>\nendobj\n");

  if (outTrfunc) {
    /* out translate function */
    setOffset(trfuncObj);
    snprintf(linebuf,LINEBUFSIZE,
      "%d 0 obj <</FunctionType 4 /Domain [0 1.0]"
      " /Range [0 1.0] /Length %d 0 R >>\n",
      trfuncObj,lengthObj);
    outPdf(linebuf);
    outPdf("stream\n");
    startOffset = currentOffset;
    snprintf(linebuf,LINEBUFSIZE,
     "{ neg 1 add dup 0 lt { pop 1 } { %.3f exp neg 1 add } "
     "ifelse %.3f mul }\n", gammaval, brightness);
    outPdf(linebuf);
    length = currentOffset - startOffset;
    snprintf(linebuf,LINEBUFSIZE,
     "endstream\nendobj\n");
    outPdf(linebuf);

    /* out length object */
    setOffset(lengthObj);
    snprintf(linebuf,LINEBUFSIZE,
      "%d 0 obj %d endobj\n",lengthObj,length);
    outPdf(linebuf);
  }
}

static void outPageContents(int contentsObj)
{
  int startOffset;
  int lengthObj;
  int length;

  setOffset(contentsObj);
  lengthObj = newObj();
  snprintf(linebuf,LINEBUFSIZE,
    "%d 0 obj <</Length %d 0 R >> stream\n",contentsObj,lengthObj);
  outPdf(linebuf);
  startOffset = currentOffset;

  if (gammaval != 1.0 || brightness != 1.0)
      outPdf("/GS1 gs\n");
  if (Flip)
  {
    snprintf(linebuf,LINEBUFSIZE,
      "-1 0 0 1 %.0f 0 cm\n",PageWidth);
    outPdf(linebuf);
  }

  switch (Orientation)
  {
    case 1:
	snprintf(linebuf,LINEBUFSIZE,
	  "0 1 -1 0 %.0f 0 cm\n",PageWidth);
	outPdf(linebuf);
	break;
    case 2:
	snprintf(linebuf,LINEBUFSIZE,
	  "-1 0 0 -1 %.0f %.0f cm\n",PageWidth, PageLength);
	outPdf(linebuf);
	break;
    case 3:
	snprintf(linebuf,LINEBUFSIZE,
	  "0 -1 1 0 0 %.0f cm\n",PageLength);
	outPdf(linebuf);
	break;
  }

  xc0 = cupsImageGetWidth(img) * xpage / xpages;
  xc1 = cupsImageGetWidth(img) * (xpage + 1) / xpages - 1;
  yc0 = cupsImageGetHeight(img) * ypage / ypages;
  yc1 = cupsImageGetHeight(img) * (ypage + 1) / ypages - 1;

  snprintf(linebuf,LINEBUFSIZE,
    "1 0 0 1 %.1f %.1f cm\n",left,top);
  outPdf(linebuf);

  snprintf(linebuf,LINEBUFSIZE,
    "%.3f 0 0 %.3f 0 0 cm\n",
     xprint * 72.0, yprint * 72.0);
  outPdf(linebuf);
  outPdf("/Im Do\n");
  length = currentOffset - startOffset - 1;
  outPdf("endstream\nendobj\n");

  /* out length object */
  setOffset(lengthObj);
  snprintf(linebuf,LINEBUFSIZE,
    "%d 0 obj %d endobj\n",lengthObj,length);
  outPdf(linebuf);
}

static void outImage(int imgObj)
{
  int		y;			/* Current Y coordinate in image */
#ifdef OUT_AS_ASCII85
  int		out_offset;		/* Offset into output buffer */
#endif
  int		out_length;		/* Length of output buffer */
  int startOffset;
  int lengthObj;
  int length;

  setOffset(imgObj);
  lengthObj = newObj();
  snprintf(linebuf,LINEBUFSIZE,
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
  outPdf(linebuf);
  snprintf(linebuf,LINEBUFSIZE,
    "/Width %d /Height %d /BitsPerComponent 8 ",
    xc1 - xc0 + 1, yc1 - yc0 + 1);
  outPdf(linebuf);

  switch (colorspace)
  {
    case CUPS_IMAGE_WHITE :
	outPdf("/ColorSpace /DeviceGray ");
	outPdf("/Decode[0 1] ");
	break;
    case CUPS_IMAGE_RGB :
	outPdf("/ColorSpace /DeviceRGB ");
	outPdf("/Decode[0 1 0 1 0 1] ");
	break;
    case CUPS_IMAGE_CMYK :
	outPdf("/ColorSpace /DeviceCMYK ");
	outPdf("/Decode[0 1 0 1 0 1 0 1] ");
	break;
  }
  if (((xc1 - xc0 + 1) / xprint) < 100.0)
      outPdf("/Interpolate true ");

  outPdf(">>\n");
  outPdf("stream\n");
  startOffset = currentOffset;

#ifdef OUT_AS_ASCII85
  /* out ascii85 needs multiple of 4bytes */
  for (y = yc0, out_offset = 0; y <= yc1; y ++)
  {
    cupsImageGetRow(img, xc0, y, xc1 - xc0 + 1, row + out_offset);

    out_length = (xc1 - xc0 + 1) * abs(colorspace) + out_offset;
    out_offset = out_length & 3;

    out_ascii85(row, out_length, y == yc1);

    if (out_offset > 0)
      memcpy(row, row + out_length - out_offset, out_offset);
  }
#else
  for (y = yc0; y <= yc1; y ++)
  {
    cupsImageGetRow(img, xc0, y, xc1 - xc0 + 1, row);

    out_length = (xc1 - xc0 + 1) * abs(colorspace);

#ifdef OUT_AS_HEX
    out_hex(row, out_length, y == yc1);
#else
    out_bin(row, out_length, y == yc1);
#endif
  }
#endif
  length = currentOffset - startOffset;
  outPdf("\nendstream\nendobj\n");

  /* out length object */
  setOffset(lengthObj);
  snprintf(linebuf,LINEBUFSIZE,
    "%d 0 obj %d endobj\n",lengthObj,length);
  outPdf(linebuf);
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
 * 'main()' - Main entry...
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  cups_page_header2_t h;                /* CUPS Raster page header, to */
                                        /* accommodate results of command */
                                        /* line parsing for PPD-less queue */
  ppd_choice_t	*choice;		/* PPD option choice */
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  const char	*val;			/* Option value */
  float		zoom;			/* Zoom facter */
  int		xppi, yppi;		/* Pixels-per-inch */
  int		hue, sat;		/* Hue and saturation adjustment */
  int		emit_jcl;
  int           pdf_printer = 0;
  char		filename[1024];		/* Name of file to print */
  int deviceCopies = 1;
  int deviceCollate = 0;
  int deviceReverse = 0;
  ppd_attr_t *attr;
  int pl,pr;
  int fillprint = 0;  /* print-scaling = fill */
  int cropfit = 0;  /* -o crop-to-fit = true */
 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Check command-line...
  */

  if (argc < 6 || argc > 7)
  {
    fputs("ERROR: imagetopdf job-id user title copies options [file]\n", stderr);
    return (1);
  }

  title = argv[3];
  fprintf(stderr, "INFO: %s %s %s %s %s %s %s\n", argv[0], argv[1], argv[2],
          argv[3], argv[4], argv[5], argv[6] ? argv[6] : "(null)");

 /*
  * Copy stdin as needed...
  */

  if (argc == 6)
  {
    int		fd;		/* File to write to */
    char	buffer[8192];	/* Buffer to read into */
    int		bytes;		/* # of bytes to read */


    if ((fd = cupsTempFd(filename, sizeof(filename))) < 0)
    {
      perror("ERROR: Unable to copy image file");
      return (1);
    }

    fprintf(stderr, "DEBUG: imagetopdf - copying to temp print file \"%s\"\n",
            filename);

    while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
      if (write(fd, buffer, bytes) == -1)
	fprintf(stderr,
		"DEBUG: imagetopdf - write error on temp print file \"%s\"\n",
		filename);

    close(fd);
  }
  else
  {
    strncpy(filename, argv[6], sizeof(filename));
  }

 /*
  * Process command-line options and write the prolog...
  */

  zoom = 1.0;
  xppi = 0;
  yppi = 0;
  hue  = 0;
  sat  = 100;

  Copies = atoi(argv[4]);

  options     = NULL;
  num_options = cupsParseOptions(argv[5], 0, &options);

  ppd = SetCommonOptions(num_options, options, 0);
  if (!ppd) {
    cupsRasterParseIPPOptions(&h, num_options, options, 0, 1);
    Orientation = h.Orientation;
    Duplex = h.Duplex;
    ColorDevice = h.cupsNumColors <= 1 ? 0 : 1;
    PageWidth = h.cupsPageSize[0] != 0.0 ? h.cupsPageSize[0] :
      (float)h.PageSize[0];
    PageLength = h.cupsPageSize[1] != 0.0 ? h.cupsPageSize[1] :
      (float)h.PageSize[1];
    PageLeft = h.cupsImagingBBox[0] != 0.0 ? h.cupsImagingBBox[0] :
      (float)h.ImagingBoundingBox[0];
    PageBottom = h.cupsImagingBBox[1] != 0.0 ? h.cupsImagingBBox[1] :
      (float)h.ImagingBoundingBox[1];
    PageRight = h.cupsImagingBBox[2] != 0.0 ? h.cupsImagingBBox[2] :
      (float)h.ImagingBoundingBox[2];
    PageTop = h.cupsImagingBBox[3] != 0.0 ? h.cupsImagingBBox[3] :
      (float)h.ImagingBoundingBox[3];
    Flip = h.MirrorPrint ? 1 : 0;
    Collate = h.Collate ? 1 : 0;
    Copies = h.NumCopies;
  }

  if (Copies == 1
      && (choice = ppdFindMarkedChoice(ppd,"Copies")) != NULL) {
    Copies = atoi(choice->choice);
  }
  if (Copies == 0) Copies = 1;
  if ((val = cupsGetOption("Duplex",num_options,options)) != 0 &&
      (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
       !strcasecmp(val, "yes"))) {
      /* for compatiblity */
      if (ppdFindOption(ppd,"Duplex") != NULL) {
        ppdMarkOption(ppd,"Duplex","True");
        ppdMarkOption(ppd,"Duplex","On");
        Duplex = 1;
      }
  } else if ((val = cupsGetOption("sides",num_options,options)) != 0 &&
      (!strcasecmp(val, "two-sided-long-edge") ||
       !strcasecmp(val, "two-sided-short-edge"))) {
      /* for compatiblity */
      if (ppdFindOption(ppd,"Duplex") != NULL) {
        ppdMarkOption(ppd,"Duplex","True");
        ppdMarkOption(ppd,"Duplex","On");
        Duplex = 1;
      }
  }

  if ((val = cupsGetOption("OutputOrder",num_options,options)) != 0) {
    if (!strcasecmp(val, "Reverse")) {
      Reverse = 1;
    }
  } else if (ppd) {
   /*
    * Figure out the right default output order from the PPD file...
    */

    if ((choice = ppdFindMarkedChoice(ppd,"OutputOrder")) != 0) {
      Reverse = !strcasecmp(choice->choice,"Reverse");
    } else if ((choice = ppdFindMarkedChoice(ppd,"OutputBin")) != 0 &&
        (attr = ppdFindAttr(ppd,"PageStackOrder",choice->choice)) != 0 &&
        attr->value) {
      Reverse = !strcasecmp(attr->value,"Reverse");
    } else if ((attr = ppdFindAttr(ppd,"DefaultOutputOrder",0)) != 0 &&
             attr->value) {
      Reverse = !strcasecmp(attr->value,"Reverse");
    }
  }

  /* adjust to even page when duplex */
  if (((val = cupsGetOption("cupsEvenDuplex",num_options,options)) != 0 &&
             (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
               !strcasecmp(val, "yes"))) ||
         ((attr = ppdFindAttr(ppd,"cupsEvenDuplex",0)) != 0 &&
             (!strcasecmp(attr->value, "true")
               || !strcasecmp(attr->value, "on") ||
               !strcasecmp(attr->value, "yes")))) {
    EvenDuplex = 1;
  }

  if ((val = cupsGetOption("multiple-document-handling", num_options, options)) != NULL)
  {
   /*
    * This IPP attribute is unnecessarily complicated...
    *
    *   single-document, separate-documents-collated-copies, and
    *   single-document-new-sheet all require collated copies.
    *
    *   separate-documents-uncollated-copies allows for uncollated copies.
    */

    Collate = strcasecmp(val, "separate-documents-uncollated-copies") != 0;
  }

  if ((val = cupsGetOption("Collate", num_options, options)) != NULL) {
    if (strcasecmp(val, "True") == 0) {
      Collate = 1;
    }
  } else {
    if ((choice = ppdFindMarkedChoice(ppd,"Collate")) != NULL
      && (!strcasecmp(choice->choice,"true")
        || !strcasecmp(choice->choice, "on")
	|| !strcasecmp(choice->choice, "yes"))) {
      Collate = 1;
    }
  }

  if ((val = cupsGetOption("gamma", num_options, options)) != NULL)
      gammaval = atoi(val) * 0.001f;

  if ((val = cupsGetOption("brightness", num_options, options)) != NULL)
      brightness = atoi(val) * 0.01f;

  if ((val = cupsGetOption("ppi", num_options, options)) != NULL)
  {
    if (sscanf(val, "%dx%d", &xppi, &yppi) < 2)
      yppi = xppi;
    zoom = 0.0;
  }

  if ((val = cupsGetOption("position", num_options, options)) != NULL)
  {
    if (strcasecmp(val, "center") == 0)
    {
      XPosition = 0;
      YPosition = 0;
    }
    else if (strcasecmp(val, "top") == 0)
    {
      XPosition = 0;
      YPosition = 1;
    }
    else if (strcasecmp(val, "left") == 0)
    {
      XPosition = -1;
      YPosition = 0;
    }
    else if (strcasecmp(val, "right") == 0)
    {
      XPosition = 1;
      YPosition = 0;
    }
    else if (strcasecmp(val, "top-left") == 0)
    {
      XPosition = -1;
      YPosition = 1;
    }
    else if (strcasecmp(val, "top-right") == 0)
    {
      XPosition = 1;
      YPosition = 1;
    }
    else if (strcasecmp(val, "bottom") == 0)
    {
      XPosition = 0;
      YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-left") == 0)
    {
      XPosition = -1;
      YPosition = -1;
    }
    else if (strcasecmp(val, "bottom-right") == 0)
    {
      XPosition = 1;
      YPosition = -1;
    }
  }

  if ((val = cupsGetOption("saturation", num_options, options)) != NULL)
    sat = atoi(val);

  if ((val = cupsGetOption("hue", num_options, options)) != NULL)
    hue = atoi(val);

  if ((val = cupsGetOption("mirror", num_options, options)) != NULL &&
      strcasecmp(val, "True") == 0)
    Flip = 1;

  if ((val = cupsGetOption("emit-jcl", num_options, options)) != NULL &&
      (!strcasecmp(val, "false") || !strcasecmp(val, "off") ||
       !strcasecmp(val, "no") || !strcmp(val, "0")))
    emit_jcl = 0;
  else
    emit_jcl = 1;



 /*
  * Open the input image to print...
  */

  colorspace = ColorDevice ? CUPS_IMAGE_RGB_CMYK : CUPS_IMAGE_WHITE;

  img = cupsImageOpen(filename, colorspace, CUPS_IMAGE_WHITE, sat, hue, NULL);
  if(img!=NULL){

  int margin_defined = 0;
  int fidelity = 0;
  int document_large = 0;

  if(ppd && (ppd->custom_margins[0]||ppd->custom_margins[1]||
      ppd->custom_margins[2]||ppd->custom_margins[3]))   // In case of custom margins
    margin_defined = 1;
  if(PageLength!=PageTop-PageBottom||PageWidth!=PageRight-PageLeft)
    margin_defined = 1;

  if((val = cupsGetOption("ipp-attribute-fidelity",num_options,options)) != NULL) {
    if(!strcasecmp(val,"true")||!strcasecmp(val,"yes")||
        !strcasecmp(val,"on")) {
      fidelity = 1;
    }
  }

  float w = (float)cupsImageGetWidth(img);
  float h = (float)cupsImageGetHeight(img);
  float pw = PageRight-PageLeft;
  float ph = PageTop-PageBottom;
  int tempOrientation = Orientation;
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
  if(w>pw||h>ph) {
    document_large = 1;
  }

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
  else if ((val = cupsGetOption("natural-scaling", num_options, options)) != NULL)
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
    float w = (float)cupsImageGetWidth(img);
    float h = (float)cupsImageGetHeight(img);
    float pw = PageRight-PageLeft;
    float ph = PageTop-PageBottom;
    int tempOrientation = Orientation;
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
      posw = (1+XPosition)*posw;
      posh = (1-YPosition)*posh;
      cups_image_t *img2 = cupsImageCrop(img,posw,posh,final_w,final_h);
      cupsImageClose(img);
      img = img2;
    }
    else {
      float final_w=w,final_h=h;
      if(final_w>pw)
      {
        final_w = pw;
      }
      if(final_h>ph)
      {
        final_h = ph;
      }
      if((fabs(final_w-w)>0.5*w)||(fabs(final_h-h)>0.5*h))
      {
        fprintf(stderr,"[DEBUG]: Ignoring crop-to-fit option!\n");
        cropfit=0;
      }
      else{
        float posw=(w-final_w)/2,posh=(h-final_h)/2;
        posw = (1+XPosition)*posw;
        posh = (1-YPosition)*posh;
        cups_image_t *img2 = cupsImageCrop(img,posw,posh,final_w,final_h);
        cupsImageClose(img);
        img = img2;
        if(flag==4)
        {
          PageBottom+=(PageTop-PageBottom-final_w)/2;
          PageTop = PageBottom+final_w;
          PageLeft +=(PageRight-PageLeft-final_h)/2;
          PageRight = PageLeft+final_h;
        }
        else{
          PageBottom+=(PageTop-PageBottom-final_h)/2;
          PageTop = PageBottom+final_h;
          PageLeft +=(PageRight-PageLeft-final_w)/2;
          PageRight = PageLeft+final_w;
        }
        if(PageBottom<0) PageBottom = 0;
        if(PageLeft<0) PageLeft = 0;
     }
    }
  }

#if defined(USE_CONVERT_CMD) && defined(CONVERT_CMD)
  if (img == NULL) {
    char filename2[1024];
    int fd2;

    if ((fd2 = cupsTempFd(filename2, sizeof(filename2))) < 0)
    {
      perror("ERROR: Unable to copy image file");
      return (1);
    }
    close(fd2);
    snprintf(linebuf,LINEBUFSIZE,
      CONVERT_CMD
      " %s png:%s",filename, filename2);
    if (system(linebuf) != 0) {
      unlink(filename2);
      perror("ERROR: Unable to copy image file");
      return (1);
    }
    img = cupsImageOpen(filename2, colorspace,
            CUPS_IMAGE_WHITE, sat, hue, NULL);
    unlink(filename2);
  }
#endif
  if (argc == 6)
    unlink(filename);

  if (img == NULL)
  {
    fputs("ERROR: Unable to open image file for printing!\n", stderr);
    ppdClose(ppd);
    return (1);
  }

  colorspace = cupsImageGetColorSpace(img);

 /*
  * Scale as necessary...
  */

  if (zoom == 0.0 && xppi == 0)
  {
    xppi = cupsImageGetXPPI(img);
    yppi = cupsImageGetYPPI(img);
  }

  if (yppi == 0)
    yppi = xppi;

  fprintf(stderr, "DEBUG: Before scaling: xppi=%d, yppi=%d, zoom=%.2f\n",
          xppi, yppi, zoom);

  if (xppi > 0)
  {
   /*
    * Scale the image as neccesary to match the desired pixels-per-inch.
    */

    if (Orientation & 1)
    {
      xprint = (PageTop - PageBottom) / 72.0;
      yprint = (PageRight - PageLeft) / 72.0;
    }
    else
    {
      xprint = (PageRight - PageLeft) / 72.0;
      yprint = (PageTop - PageBottom) / 72.0;
    }

    fprintf(stderr, "DEBUG: Before scaling: xprint=%.1f, yprint=%.1f\n",
            xprint, yprint);

    xinches = (float)cupsImageGetWidth(img) / (float)xppi;
    yinches = (float)cupsImageGetHeight(img) / (float)yppi;

    fprintf(stderr, "DEBUG: Image size is %.1f x %.1f inches...\n",
            xinches, yinches);

    if ((val = cupsGetOption("natural-scaling", num_options, options)) != NULL)
    {
      xinches = xinches * atoi(val) / 100;
      yinches = yinches * atoi(val) / 100;
    }

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Rotate the image if it will fit landscape but not portrait...
      */

      fputs("DEBUG: Auto orientation...\n", stderr);

      if ((xinches > xprint || yinches > yprint) &&
          xinches <= yprint && yinches <= xprint)
      {
       /*
	* Rotate the image as needed...
	*/

        fputs("DEBUG: Using landscape orientation...\n", stderr);

	Orientation = (Orientation + 1) & 3;
	xsize       = yprint;
	yprint      = xprint;
	xprint      = xsize;
      }
    }
  }
  else
  {
   /*
    * Scale percentage of page size...
    */

    xprint = (PageRight - PageLeft) / 72.0;
    yprint = (PageTop - PageBottom) / 72.0;
    aspect = (float)cupsImageGetYPPI(img) / (float)cupsImageGetXPPI(img);

    fprintf(stderr, "DEBUG: Before scaling: xprint=%.1f, yprint=%.1f\n",
            xprint, yprint);

    fprintf(stderr, "DEBUG: cupsImageGetXPPI(img) = %d, cupsImageGetYPPI(img) = %d, aspect = %f\n",
            cupsImageGetXPPI(img), cupsImageGetYPPI(img), aspect);

    xsize = xprint * zoom;
    ysize = xsize * cupsImageGetHeight(img) / cupsImageGetWidth(img) / aspect;

    if (ysize > (yprint * zoom))
    {
      ysize = yprint * zoom;
      xsize = ysize * cupsImageGetWidth(img) * aspect / cupsImageGetHeight(img);
    }

    xsize2 = yprint * zoom;
    ysize2 = xsize2 * cupsImageGetHeight(img) / cupsImageGetWidth(img) / aspect;

    if (ysize2 > (xprint * zoom))
    {
      ysize2 = xprint * zoom;
      xsize2 = ysize2 * cupsImageGetWidth(img) * aspect / cupsImageGetHeight(img);
    }

    fprintf(stderr, "DEBUG: Portrait size is %.2f x %.2f inches\n", xsize, ysize);
    fprintf(stderr, "DEBUG: Landscape size is %.2f x %.2f inches\n", xsize2, ysize2);

    if (cupsGetOption("orientation-requested", num_options, options) == NULL &&
        cupsGetOption("landscape", num_options, options) == NULL)
    {
     /*
      * Choose the rotation with the largest area, but prefer
      * portrait if they are equal...
      */

      fputs("DEBUG: Auto orientation...\n", stderr);

      if ((xsize * ysize) < (xsize2 * xsize2))
      {
       /*
	* Do landscape orientation...
	*/

        fputs("DEBUG: Using landscape orientation...\n", stderr);

	Orientation = 1;
	xinches     = xsize2;
	yinches     = ysize2;
	xprint      = (PageTop - PageBottom) / 72.0;
	yprint      = (PageRight - PageLeft) / 72.0;
      }
      else
      {
       /*
	* Do portrait orientation...
	*/

        fputs("DEBUG: Using portrait orientation...\n", stderr);

	Orientation = 0;
	xinches     = xsize;
	yinches     = ysize;
      }
    }
    else if (Orientation & 1)
    {
      fputs("DEBUG: Using landscape orientation...\n", stderr);

      xinches     = xsize2;
      yinches     = ysize2;
      xprint      = (PageTop - PageBottom) / 72.0;
      yprint      = (PageRight - PageLeft) / 72.0;
    }
    else
    {
      fputs("DEBUG: Using portrait orientation...\n", stderr);

      xinches     = xsize;
      yinches     = ysize;
      xprint      = (PageRight - PageLeft) / 72.0;
      yprint      = (PageTop - PageBottom) / 72.0;
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
    xpages = ypages = 1;
  } else {
    xpages = ceil(xinches / xprint);
    ypages = ceil(yinches / yprint);
  }

  xprint = xinches / xpages;
  yprint = yinches / ypages;

  fprintf(stderr, "DEBUG: xpages = %dx%.2fin, ypages = %dx%.2fin\n",
          xpages, xprint, ypages, yprint);

 /*
  * Update the page size for custom sizes...
  */

  if ((choice = ppdFindMarkedChoice(ppd, "PageSize")) != NULL &&
      strcasecmp(choice->choice, "Custom") == 0)
  {
    float	width,		/* New width in points */
		length;		/* New length in points */
    char	s[255];		/* New custom page size... */


   /*
    * Use the correct width and length for the current orientation...
    */

    if (Orientation & 1)
    {
      width  = yprint * 72.0;
      length = xprint * 72.0;
    }
    else
    {
      width  = xprint * 72.0;
      length = yprint * 72.0;
    }

   /*
    * Add margins to page size...
    */

    width  += ppd->custom_margins[0] + ppd->custom_margins[2];
    length += ppd->custom_margins[1] + ppd->custom_margins[3];

   /*
    * Enforce minimums...
    */

    if (width < ppd->custom_min[0])
      width = ppd->custom_min[0];

    if (length < ppd->custom_min[1])
      length = ppd->custom_min[1];

    fprintf(stderr, "DEBUG: Updated custom page size to %.2f x %.2f inches...\n",
            width / 72.0, length / 72.0);

   /*
    * Set the new custom size...
    */

    sprintf(s, "Custom.%.0fx%.0f", width, length);
    ppdMarkOption(ppd, "PageSize", s);

   /*
    * Update page variables...
    */

    PageWidth  = width;
    PageLength = length;
    PageLeft   = ppd->custom_margins[0];
    PageRight  = width - ppd->custom_margins[2];
    PageBottom = ppd->custom_margins[1];
    PageTop    = length - ppd->custom_margins[3];
  }

  if (Copies == 1) {
    /* collate is not needed */
    Collate = 0;
    ppdMarkOption(ppd,"Collate","False");
  }
  if (!Duplex) {
    /* evenDuplex is not needed */
    EvenDuplex = 0;
  }

  /* check collate device */
  if (Collate) {
    if ((choice = ppdFindMarkedChoice(ppd,"Collate")) != NULL &&
       !strcasecmp(choice->choice,"true")) {
      ppd_option_t *opt;

      if ((opt = ppdFindOption(ppd,"Collate")) != NULL &&
        !opt->conflicted) {
        deviceCollate = 1;
      } else {
        ppdMarkOption(ppd,"Collate","False");
      }
    }
  }
  /* check OutputOrder device */
  if (Reverse) {
    if (ppdFindOption(ppd,"OutputOrder") != NULL) {
      deviceReverse = 1;
    }
  }
  if (ppd != NULL &&
       !ppd->manual_copies && Collate && !deviceCollate) {
    /* Copying by device , software collate is impossible */
    /* Enable software copying */
    ppd->manual_copies = 1;
  }
  if (Copies > 1 && (ppd == NULL || ppd->manual_copies)
      && Duplex) {
    /* Enable software collate , or same pages are printed in both sides */
      Collate = 1;
      if (deviceCollate) {
        deviceCollate = 0;
        ppdMarkOption(ppd,"Collate","False");
      }
  }
  if (Duplex && Collate && !deviceCollate) {
    /* Enable evenDuplex or the first page may be printed other side of the
      end of precedings */
    EvenDuplex = 1;
  }
  if (Duplex && Reverse && !deviceReverse) {
    /* Enable evenDuplex or the first page may be empty. */
    EvenDuplex = 1;
  }
  /* change feature for software */
  if (deviceCollate) {
    Collate = 0;
  }
  if (deviceReverse) {
    Reverse = 0;
  }
  if (ppd != NULL) {
    if (ppd->manual_copies) {
      /* sure disable hardware copying */
      ppdMarkOption(ppd,"Copies","1");
      ppdMarkOption(ppd,"JCLCopies","1");
    } else {
      /* change for hardware copying */
      deviceCopies = Copies;
      Copies = 1;
    }
  }

 /*
  * See if we need to collate, and if so how we need to do it...
  */

  if (xpages == 1 && ypages == 1
      && (Collate || deviceCollate) && !EvenDuplex) {
    /* collate is not needed, disable it */
    deviceCollate = 0;
    Collate = 0;
    ppdMarkOption(ppd,"Collate","False");
  }

  if (((xpages*ypages) % 2) == 0) {
    /* even pages, disable EvenDuplex */
    EvenDuplex = 0;
  }

 /*
  * Write any "exit server" options that have been selected...
  */

  ppdEmit(ppd, stdout, PPD_ORDER_EXIT);

 /*
  * Write any JCL commands that are needed to print PostScript code...
  */

  if (ppd && emit_jcl) {
    /* pdftopdf only adds JCL to the job if the printer is a native PDF
       printer and the PPD is for this mode, having the "*JCLToPDFInterpreter:"
       keyword. We need to read this keyword manually from the PPD and replace
       the content of ppd->jcl_ps by the value of this keyword, so that
       ppdEmitJCL() actalually adds JCL based on the presence on
       "*JCLToPDFInterpreter:". */
    ppd_attr_t *attr;
    char buf[1024];
    int devicecopies_done = 0;
    char *old_jcl_ps = ppd->jcl_ps;
    /* If there is a "Copies" option in the PPD file, assure that hardware
       copies are implemented as described by this option */
    if (ppdFindOption(ppd,"Copies") != NULL && deviceCopies > 1)
    {
      snprintf(buf,sizeof(buf),"%d",deviceCopies);
      ppdMarkOption(ppd,"Copies",buf);
      devicecopies_done = 1;
    }
    if ((attr = ppdFindAttr(ppd,"JCLToPDFInterpreter",NULL)) != NULL)
    {
      if (deviceCopies > 1 && devicecopies_done == 0 && /* HW copies */
	  strncmp(ppd->jcl_begin, "\033%-12345X@", 10) == 0) /* PJL */
      {
	/* Add a PJL command to implement the hardware copies */
        const size_t size = strlen(attr->value) + 1 + 30;
        ppd->jcl_ps = (char *)malloc(size * sizeof(char));
        if (deviceCollate)
	{
          snprintf(ppd->jcl_ps, size, "@PJL SET QTY=%d\n%s",
                   deviceCopies, attr->value);
        }
	else
	{
          snprintf(ppd->jcl_ps, size, "@PJL SET COPIES=%d\n%s",
                   deviceCopies, attr->value);
        }
      }
      else
	ppd->jcl_ps = strdup(attr->value);
      ppd_decode(ppd->jcl_ps);
      pdf_printer = 1;
    }
    else
    {
      ppd->jcl_ps = NULL;
      pdf_printer = 0;
    }
    ppdEmitJCL(ppd, stdout, atoi(argv[1]), argv[2], argv[3]);
    emitJCLOptions(stdout,deviceCopies);
    free(ppd->jcl_ps);
    ppd->jcl_ps = old_jcl_ps; /* cups uses pool allocator, not free() */
  }

 /*
  * Start sending the document with any commands needed...
  */

  outPrologue(Copies*xpages*ypages+(EvenDuplex ? 1 : 0));

 /*
  * Output the pages...
  */

  row = malloc(cupsImageGetWidth(img) * abs(colorspace) + 3);

  fprintf(stderr, "DEBUG: XPosition=%d, YPosition=%d, Orientation=%d\n",
          XPosition, YPosition, Orientation);
  fprintf(stderr, "DEBUG: xprint=%.0f, yprint=%.0f\n", xprint, yprint);
  fprintf(stderr, "DEBUG: PageLeft=%.0f, PageRight=%.0f, PageWidth=%.0f\n",
          PageLeft, PageRight, PageWidth);
  fprintf(stderr, "DEBUG: PageBottom=%.0f, PageTop=%.0f, PageLength=%.0f\n",
          PageBottom, PageTop, PageLength);

  if (Flip) {
    pr = PageWidth - PageLeft;
    pl = PageWidth - PageRight;
  } else {
    pr = PageRight;
    pl = PageLeft;
  }

  switch (Orientation)
  {
    default :
	switch (XPosition)
	{
	  case -1 :
	      left = pl;
	      break;
	  default :
              left = (pr + pl - xprint * 72) / 2;
	      break;
	  case 1 :
	      left = pr - xprint * 72;
	      break;
	}

	switch (YPosition)
	{
	  case -1 :
	      top = PageBottom;
	      break;
	  default :
	      top = (PageTop + PageBottom - yprint * 72) / 2;
	      break;
	  case 1 :
	      top = PageTop - yprint * 72;;
	      break;
	}
	break;

    case 1 :
	switch (XPosition)
	{
	  case -1 :
              left = PageBottom;
	      break;
	  default :
              left = (PageTop + PageBottom - xprint * 72) / 2;
	      break;
	  case 1 :
              left = PageTop - xprint * 72;
	      break;
	}

	switch (YPosition)
	{
	  case -1 :
	      top = pl;
	      break;
	  default :
	      top = (pr + pl - yprint * 72) / 2;
	      break;
	  case 1 :
	      top = pr - yprint * 72;;
	      break;
	}
	break;

    case 2 :
	switch (XPosition)
	{
	  case -1 :
              left = pr - xprint * 72;
	      break;
	  default :
              left = (pr + pl - xprint * 72) / 2;
	      break;
	  case 1 :
              left = pl;
	      break;
	}

	switch (YPosition)
	{
	  case -1 :
	      top = PageTop - yprint * 72;
	      break;
	  default :
	      top = (PageTop + PageBottom - yprint * 72) / 2;
	      break;
	  case 1 :
	      top = PageBottom;
	      break;
	}
	break;

    case 3 :
	switch (XPosition)
	{
	  case -1 :
              left = PageTop - xprint * 72;
	      break;
	  default :
              left = (PageTop + PageBottom - xprint * 72) / 2;
	      break;
	  case 1 :
              left = PageBottom;
	      break;
	}

	switch (YPosition)
	{
	  case -1 :
	      top = pr - yprint * 72;;
	      break;
	  default :
	      top = (pr + pl - yprint * 72) / 2;
	      break;
	  case 1 :
	      top = pl;
	      break;
	}
	break;
  }

  fprintf(stderr, "DEBUG: left=%.2f, top=%.2f\n", left, top);

  if (Collate)
  {
    int *contentsObjs;
    int *imgObjs;

    if ((contentsObjs = malloc(sizeof(int)*xpages*ypages)) == NULL)
    {
      fprintf(stderr,"ERROR: Can't allocate contentsObjs\n");
      exit(2);
    }
    if ((imgObjs = malloc(sizeof(int)*xpages*ypages)) == NULL)
    {
      fprintf(stderr,"ERROR: Can't allocate imgObjs\n");
      exit(2);
    }
    for (xpage = 0; xpage < xpages; xpage ++)
      for (ypage = 0; ypage < ypages; ypage ++)
      {
	int imgObj;
	int contentsObj;

	contentsObj = contentsObjs[ypages*xpage+ypage] = newObj();
	imgObj = imgObjs[ypages*xpage+ypage] = newObj();

	/* out contents object */
	outPageContents(contentsObj);

	/* out image object */
	outImage(imgObj);
      }
    for (page = 0; Copies > 0 ; Copies --) {
      for (xpage = 0; xpage < xpages; xpage ++)
	for (ypage = 0; ypage < ypages; ypage ++, page ++)
	{
	  /* out Page Object */
	  outPageObject(pageObjects[page],
	    contentsObjs[ypages*xpage+ypage],
	    imgObjs[ypages*xpage+ypage]);
	  if (pdf_printer)
	    fprintf(stderr, "PAGE: %d %d\n", page+1, 1);
	}
      if (EvenDuplex) {
	/* out empty page */
	outPageObject(pageObjects[page],-1,-1);
	if (pdf_printer)
	  fprintf(stderr, "PAGE: %d %d\n", page+1, 1);
      }
    }
    free(contentsObjs);
    free(imgObjs);
  }
  else {
    for (page = 0, xpage = 0; xpage < xpages; xpage ++)
      for (ypage = 0; ypage < ypages; ypage ++)
      {
	int imgObj;
	int contentsObj;
	int p;

	imgObj = newObj();
	contentsObj = newObj();

	/* out contents object */
	outPageContents(contentsObj);

	/* out image object */
	outImage(imgObj);

	for (p = 0;p < Copies;p++, page++)
	{
	  /* out Page Object */
	  outPageObject(pageObjects[page],contentsObj,imgObj);
	  if (pdf_printer)
	    fprintf(stderr, "PAGE: %d %d\n", page+1, 1);
	}
      }
    if (EvenDuplex) {
      /* out empty pages */
      int p;

      for (p = 0;p < Copies;p++, page++)
      {
	outPageObject(pageObjects[page],-1,-1);
	if (pdf_printer)
	  fprintf(stderr, "PAGE: %d %d\n", page+1, 1);
      }
    }
  }

  outXref();
  outTrailer();
  freeAllObj();
 /*
  * Close files...
  */

#ifndef CUPS_1_1
  if (emit_jcl)
  {
    if (ppd && ppd->jcl_end)
      ppdEmitJCLEnd(ppd, stdout);
  }
#endif

  cupsImageClose(img);
  ppdClose(ppd);

  return (0);
}

#ifdef OUT_AS_HEX
/*
 * 'out_hex()' - Print binary data as a series of hexadecimal numbers.
 */

static void
out_hex(cups_ib_t *data,			/* I - Data to print */
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

    putcPdf(hex[*data >> 4]);
    putcPdf(hex[*data & 15]);

    data ++;
    length --;

    col += 2;
    if (col > 78)
    {
      putcPdf('\n');
      col = 0;
    }
  }

  if (last_line && col)
  {
    putcPdf('\n');
    col = 0;
  }
}
#else

#ifdef OUT_AS_ASCII85
/*
 * 'out_ascii85()' - Print binary data as a series of base-85 numbers.
 */

static void
out_ascii85(cups_ib_t *data,		/* I - Data to print */
	   int       length,		/* I - Number of bytes to print */
	   int       last_line)		/* I - Last line of raster data? */
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
      putcPdf('z');
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

      outPdf(c);
      col += 5;
    }

    data += 4;
    length -= 4;

    if (col >= 75)
    {
      putcPdf('\n');
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
      outPdf(c);
    }

    outPdf("~>");
    col = 0;
  }
}
#else
/*
 * 'out_bin()' - Print binary data as binary.
 */

static void
out_bin(cups_ib_t *data,		/* I - Data to print */
	   int       length,		/* I - Number of bytes to print */
	   int       last_line)		/* I - Last line of raster data? */
{
  while (length > 0)
  {
    putcPdf(*data);
    data ++;
    length --;
  }

  if (last_line)
  {
    putcPdf('\n');
  }
}
#endif
#endif

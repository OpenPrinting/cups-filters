/*
 *   Text to PDF filter for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 2008,2012 by Tobias Hoffmann.
 *   Copyright 2007 by Apple Inc.
 *   Copyright 1993-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   main()          - Main entry for text to PDF filter.
 *   WriteEpilogue() - Write the PDF file epilogue.
 *   WritePage()     - Write a page of text.
 *   WriteProlog()   - Write the PDF file prolog with options.
 *   write_line()    - Write a row of text.
 *   write_string()  - Write a string of text.
 */

/*
 * Include necessary headers...
 */

#include "textcommon.h"
#include "pdfutils.h"
#include "fontembed/embed.h"
#include <assert.h>
#include "fontembed/sfnt.h"
#include <fontconfig/fontconfig.h>

/*
 * Globals...
 */

#ifdef CUPS_1_4 /* CUPS 1.4.x or newer: only UTF8 is supported */
int     UTF8 = 1;               /* Use UTF-8 encoding? */
#endif /* CUPS_1_4 */

EMB_PARAMS *font_load(const char *font);

EMB_PARAMS *font_load(const char *font)
{
  OTF_FILE *otf;

  FcPattern *pattern;
  FcFontSet *candidates;
  FcChar8   *fontname = NULL;
  FcResult   result;
  int i;

  if ( (font[0]=='/')||(font[0]=='.') ) {
    candidates = NULL;
    fontname=(FcChar8 *)strdup(font);
  } else {
    FcInit ();
    pattern = FcNameParse ((const FcChar8 *)font);
    FcPatternAddInteger (pattern, FC_SPACING, FC_MONO); // guide fc, in case substitution becomes necessary
    FcConfigSubstitute (0, pattern, FcMatchPattern);
    FcDefaultSubstitute (pattern);

    /* Receive a sorted list of fonts matching our pattern */
    candidates = FcFontSort (0, pattern, FcFalse, 0, &result);
    FcPatternDestroy (pattern);

    /* In the list of fonts returned by FcFontSort()
       find the first one that is both in TrueType format and monospaced */
    for (i = 0; i < candidates->nfont; i++) {
      FcChar8 *fontformat=NULL; // TODO? or just try?
      int spacing=0; // sane default, as FC_MONO == 100
      FcPatternGetString  (candidates->fonts[i], FC_FONTFORMAT, 0, &fontformat);
      FcPatternGetInteger (candidates->fonts[i], FC_SPACING,    0, &spacing);

      if ( (fontformat)&&(spacing == FC_MONO) ) {
        if (strcmp((const char *)fontformat, "TrueType") == 0) {
          fontname = FcPatternFormat (candidates->fonts[i], (const FcChar8 *)"%{file|cescape}/%{index}");
          break;
        } else if (strcmp((const char *)fontformat, "CFF") == 0) {
          fontname = FcPatternFormat (candidates->fonts[i], (const FcChar8 *)"%{file|cescape}"); // TTC only possible with non-cff glyphs!
          break;
        }
      }
    }
    FcFontSetDestroy (candidates);
  }

  if (!fontname) {
    // TODO: try /usr/share/fonts/*/*/%s.ttf
    fprintf(stderr,"No viable font found\n");
    return NULL;
  }

  otf = otf_load((const char *)fontname);
  free(fontname);
  if (!otf) {
    return NULL;
  }

  FONTFILE *ff=fontfile_open_sfnt(otf);
  assert(ff);
  EMB_PARAMS *emb=emb_new(ff,
                          EMB_DEST_PDF16,
                          EMB_C_FORCE_MULTIBYTE|
                          EMB_C_TAKE_FONTFILE);
  assert(emb);
  assert(emb->plan&EMB_A_MULTIBYTE);
  return emb;
}

EMB_PARAMS *font_std(const char *name)
{
  FONTFILE *ff=fontfile_open_std(name);
  assert(ff);
  EMB_PARAMS *emb=emb_new(ff,
                          EMB_DEST_PDF16,
                          EMB_C_TAKE_FONTFILE);
  assert(emb);
  return emb;
}

/*
 * Globals...
 */

int		NumFonts;	/* Number of fonts to use */
EMB_PARAMS	*Fonts[256][4];	/* Fonts to use */
unsigned short	Chars[256];	/* Input char to unicode */
unsigned char	Codes[65536];	/* Unicode glyph mapping to font */
int		Widths[256];	/* Widths of each font */
int		Directions[256];/* Text directions for each font */
pdfOut *pdf;
int    FontResource;   /* Object number of font resource dictionary */
float  FontScaleX,FontScaleY;  /* The font matrix */
lchar_t *Title,*Date;   /* The title and date strings */

/*
 * Local functions...
 */

static void	write_line(int row, lchar_t *line);
static void	write_string(int col, int row, int len, lchar_t *s);
static lchar_t *make_wide(const char *buf);
static void     write_font_str(float x,float y,int fontid, lchar_t *str, int len);
static void     write_pretty_header();


/*
 * 'main()' - Main entry for text to PDF filter.
 */

int			/* O - Exit status */
main(int  argc,		/* I - Number of command-line arguments */
     char *argv[])	/* I - Command-line arguments */
{
  return (TextMain("texttopdf", argc, argv));
}


/*
 * 'WriteEpilogue()' - Write the PDF file epilogue.
 */

void
WriteEpilogue(void)
{
  static char	*names[] =	/* Font names */
		{ "FN","FB","FI","FBI" };
  int i,j;

  // embed fonts
  for (i = PrettyPrint ? 3 : 1; i >= 0; i --) {
    for (j = 0; j < NumFonts; j ++) 
    {
      EMB_PARAMS *emb=Fonts[j][i];
      if (emb->font->fobj) { // already embedded
        continue;
      }
      if ( (!emb->subset)||(bits_used(emb->subset,emb->font->sfnt->numGlyphs)) ) {
        emb->font->fobj=pdfOut_write_font(pdf,emb);
        assert(emb->font->fobj);
      }
    }
  }

  /*
   * Create the global fontdict
   */

  // now fix FontResource
  pdf->xref[FontResource-1]=pdf->filepos;
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<<\n",
                    FontResource);

  for (i = PrettyPrint ? 3 : 1; i >= 0; i --) {
    for (j = 0; j < NumFonts; j ++) {
      EMB_PARAMS *emb=Fonts[j][i];
      if (emb->font->fobj) { // used
        pdfOut_printf(pdf,"  /%s%02x %d 0 R\n",names[i],j,emb->font->fobj);
      }
    }
  }

  pdfOut_printf(pdf,">>\n"
                    "endobj\n");

  pdfOut_finish_pdf(pdf);

  pdfOut_free(pdf);
}

/*
 * {{{ 'WritePage()' - Write a page of text.
 */

void
WritePage(void)
{
  int	line;			/* Current line */

  int content=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Length %d 0 R\n"
                    ">>\n"
                    "stream\n"
                    "q\n"
                    ,content,content+1);
  long size=-(pdf->filepos-2);

  NumPages ++;
  if (PrettyPrint)
    write_pretty_header(pdf);

  for (line = 0; line < SizeLines; line ++)
    write_line(line, Page[line]);

  size+=pdf->filepos+2;
  pdfOut_printf(pdf,"Q\n"
                    "endstream\n"
                    "endobj\n");
  
  int len_obj=pdfOut_add_xref(pdf);
  assert(len_obj==content+1);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "%ld\n"
                    "endobj\n",
                    len_obj,size);

  int obj=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Type/Page\n"
                    "  /Parent 1 0 R\n"
                    "  /MediaBox [0 0 %.0f %.0f]\n"
                    "  /Contents %d 0 R\n"
                    "  /Resources << /Font %d 0 R >>\n"
                    ">>\n"
                    "endobj\n",
                    obj,PageWidth,PageLength,content,FontResource);
  pdfOut_add_page(pdf,obj);

  memset(Page[0], 0, sizeof(lchar_t) * SizeColumns * SizeLines);
}
// }}}

/* 
 * {{{'WriteProlog()' - Write the PDF file prolog with options.
 */

void
WriteProlog(const char *title,		/* I - Title of job */
	    const char *user,		/* I - Username */
            const char *classification,	/* I - Classification */
	    const char *label,		/* I - Page label */
            ppd_file_t *ppd)		/* I - PPD file info */
{
  int		i, j, k;	/* Looping vars */
  char		*charset;	/* Character set string */
  char		filename[1024];	/* Glyph filenames */
  FILE		*fp;		/* Glyph files */
  const char	*datadir;	/* CUPS_DATADIR environment variable */
  char		line[1024],	/* Line from file */
		*lineptr,	/* Pointer into line */
		*valptr;	/* Pointer to value in line */
#ifndef CUPS_1_4 /* CUPS 1.4.x or newer: support for non-utf8 removed */
  int		ch, unicode;	/* Character values */
#endif
  int		start, end;	/* Start and end values for range */
  time_t	curtime;	/* Current time */
  struct tm	*curtm;		/* Current date */
  char		curdate[255];	/* Current date (text format) */
  int		num_fonts=0;	/* Number of unique fonts */
  EMB_PARAMS	*fonts[1024];	/* Unique fonts */
  char		*fontnames[1024];	/* Unique fonts */
#if 0
  static char	*names[] =	/* Font names */
		{
                  "FN","FB","FI"
                /*
		  "cupsNormal",
		  "cupsBold",
		  "cupsItalic"
                */
		};
#endif


 /*
  * Get the data directory...
  */

  if ((datadir = getenv("CUPS_DATADIR")) == NULL)
    datadir = CUPS_DATADIR;

 /*
  * Adjust margins as necessary...
  */

  if (classification || label)
  {
   /*
    * Leave room for labels...
    */

    PageBottom += 36;
    PageTop    -= 36;
  }

  if (PageColumns > 1)
  {
    ColumnGutter = CharsPerInch / 2;
    ColumnWidth  = (SizeColumns - ColumnGutter * (PageColumns - 1)) /
                   PageColumns;
  }
  else
    ColumnWidth = SizeColumns;

 /*
  * {{{ Output the PDF header...
  */

  assert(!pdf);
  pdf=pdfOut_new();
  assert(pdf);

  pdfOut_begin_pdf(pdf);
  pdfOut_printf(pdf,"%%cupsRotation: %d\n", (Orientation & 3) * 90); // TODO?

  pdfOut_add_kv(pdf,"Creator","texttopdf/" PACKAGE_VERSION);

  curtime = time(NULL);
  curtm   = localtime(&curtime);
  strftime(curdate, sizeof(curdate), "%c", curtm);

  pdfOut_add_kv(pdf,"CreationDate",pdfOut_to_pdfdate(curtm));
  pdfOut_add_kv(pdf,"Title",title);
  pdfOut_add_kv(pdf,"Author",user); // was(PostScript): /For
  // }}}

 /*
  * {{{ Initialize globals...
  */

  NumFonts = 0;
  memset(Fonts, 0, sizeof(Fonts));
  memset(Chars, 0, sizeof(Chars));
  memset(Codes, 0, sizeof(Codes));
  // }}}

 /*
  * Get the output character set...
  */

  charset = getenv("CHARSET");
  if (charset != NULL && strcmp(charset, "us-ascii") != 0) // {{{
  {
    snprintf(filename, sizeof(filename), "%s/charsets/pdf.%s", datadir, charset);

    if ((fp = fopen(filename, "r")) == NULL)
    {
     /*
      * Can't open charset file!
      */

      fprintf(stderr, "ERROR: Unable to open %s: %s\n", filename,
              strerror(errno));
      exit(1);
    }

   /*
    * Opened charset file; now see if this is really a charset file...
    */

    if (fgets(line, sizeof(line), fp) == NULL)
    {
     /*
      * Bad/empty charset file!
      */

      fclose(fp);
      fprintf(stderr, "ERROR: Bad charset file %s\n", filename);
      exit(1);
    }

    if (strncmp(line, "charset", 7) != 0)
    {
     /*
      * Bad format/not a charset file!
      */

      fclose(fp);
      fprintf(stderr, "ERROR: Bad charset file %s\n", filename);
      exit(1);
    }

   /*
    * See if this is an 8-bit or UTF-8 character set file...
    */

    line[strlen(line) - 1] = '\0'; /* Drop \n */
    for (lineptr = line + 7; isspace(*lineptr & 255); lineptr ++); /* Skip whitespace */

#ifndef CUPS_1_4 /* CUPS 1.4.x or newer: support for non-utf8 removed */
    if (strcmp(lineptr, "8bit") == 0) // {{{
    {
     /*
      * 8-bit text...
      */

      UTF8     = 0;
      NumFonts = 0;

     /*
      * Read the font description(s)...
      */

      while (fgets(line, sizeof(line), fp) != NULL)
      {
       /*
        * Skip comment and blank lines...
	*/

        if (line[0] == '#' || line[0] == '\n')
	  continue;

       /*
	* Read the font descriptions that should look like:
	*
	*   first last direction width normal [bold italic bold-italic]
	*/

	lineptr = line;

        start = strtol(lineptr, &lineptr, 16);
	end   = strtol(lineptr, &lineptr, 16);

	while (isspace(*lineptr & 255))
	  lineptr ++;

        if (!*lineptr)
	  break;	/* Must be a font mapping */

	valptr = lineptr;

	while (!isspace(*lineptr & 255) && *lineptr)
	  lineptr ++;

	if (!*lineptr)
	{
	 /*
	  * Can't have a font without all required values...
	  */

	  fprintf(stderr, "ERROR: Bad font description line: %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "ltor") == 0)
	  Directions[NumFonts] = 1;
	else if (strcmp(valptr, "rtol") == 0)
	  Directions[NumFonts] = -1;
	else
	{
	  fprintf(stderr, "ERROR: Bad text direction %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

       /*
	* Got the direction, now get the width...
	*/

	while (isspace(*lineptr & 255))
	  lineptr ++;

	valptr = lineptr;

	while (!isspace(*lineptr & 255) && *lineptr)
	  lineptr ++;

	if (!*lineptr)
	{
	 /*
	  * Can't have a font without all required values...
	  */

	  fprintf(stderr, "ERROR: Bad font description line: %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "single") == 0)
          Widths[NumFonts] = 1;
	else if (strcmp(valptr, "double") == 0)
          Widths[NumFonts] = 2;
	else 
	{
	  fprintf(stderr, "ERROR: Bad text width %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

       /*
	* Get the fonts...
	*/

	for (i = 0; *lineptr && i < 4; i ++)
	{
	  while (isspace(*lineptr & 255))
	    lineptr ++;

	  valptr = lineptr;

	  while (!isspace(*lineptr & 255) && *lineptr)
	    lineptr ++;

          if (*lineptr)
	    *lineptr++ = '\0';

          if (lineptr > valptr) {
            // search for duplicates
            for (k = 0; k < num_fonts; k ++)
              if (strcmp(valptr, fontnames[k]) == 0) {
	        Fonts[NumFonts][i] = fonts[k];
                break;
              }

            if (k==num_fonts) {  // not found
	      fonts[num_fonts] = Fonts[NumFonts][i] = font_load(valptr);
              if (!fonts[num_fonts]) { // font missing/corrupt, replace by first
                fprintf(stderr,"WARNING: Ignored bad font \"%s\"\n",valptr);
                break;
              }
              fontnames[num_fonts++] = strdup(valptr);
            }
          }
	}

        /* ignore complete range, when the first font is not available */
        if (i==0) {
          continue;
        }

       /*
	* Fill in remaining fonts as needed...
	*/

	for (j = i; j < 4; j ++)
	  Fonts[NumFonts][j] = Fonts[NumFonts][0];

       /*
        * Define the character mappings...
	*/

	for (i = start; i <= end; i ++)
	  Codes[i] = NumFonts;

        NumFonts ++;
      }

     /*
      * Read encoding lines...
      */

      do
      {
       /*
        * Skip comment and blank lines...
	*/

        if (line[0] == '#' || line[0] == '\n')
	  continue;

       /*
        * Grab the character and unicode glyph number.
	*/

	if (sscanf(line, "%x%x", &ch, &unicode) == 2 && ch < 256)
          Chars[ch] = unicode;
      }
      while (fgets(line, sizeof(line), fp) != NULL);

      fclose(fp);
    } else // }}}
#endif
    if (strcmp(lineptr, "utf8") == 0) { // {{{
     /*
      * UTF-8 (Unicode) text...
      */

      UTF8 = 1;

     /*
      * Read the font descriptions...
      */

      NumFonts = 0;

      while (fgets(line, sizeof(line), fp) != NULL)
      {
       /*
        * Skip comment and blank lines...
	*/

        if (line[0] == '#' || line[0] == '\n')
	  continue;

       /*
	* Read the font descriptions that should look like:
	*
	*   start end direction width normal [bold italic bold-italic]
	*/

	lineptr = line;

        start = strtol(lineptr, &lineptr, 16);
	end   = strtol(lineptr, &lineptr, 16);

	while (isspace(*lineptr & 255))
	  lineptr ++;

	valptr = lineptr;

	while (!isspace(*lineptr & 255) && *lineptr)
	  lineptr ++;

	if (!*lineptr)
	{
	 /*
	  * Can't have a font without all required values...
	  */

	  fprintf(stderr, "ERROR: Bad font description line: %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "ltor") == 0)
	  Directions[NumFonts] = 1;
	else if (strcmp(valptr, "rtol") == 0)
	  Directions[NumFonts] = -1;
	else
	{
	  fprintf(stderr, "ERROR: Bad text direction %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

       /*
	* Got the direction, now get the width...
	*/

	while (isspace(*lineptr & 255))
	  lineptr ++;

	valptr = lineptr;

	while (!isspace(*lineptr & 255) && *lineptr)
	  lineptr ++;

	if (!*lineptr)
	{
	 /*
	  * Can't have a font without all required values...
	  */

	  fprintf(stderr, "ERROR: Bad font description line: %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "single") == 0)
          Widths[NumFonts] = 1;
	else if (strcmp(valptr, "double") == 0)
          Widths[NumFonts] = 2;
	else 
	{
	  fprintf(stderr, "ERROR: Bad text width %s\n", valptr);
	  fclose(fp);
	  exit(1);
	}

       /*
	* Get the fonts...
	*/

	for (i = 0; *lineptr && i < 4; i ++)
	{
	  while (isspace(*lineptr & 255))
	    lineptr ++;

	  valptr = lineptr;

	  while (!isspace(*lineptr & 255) && *lineptr)
	    lineptr ++;

          if (*lineptr)
	    *lineptr++ = '\0';

          if (lineptr > valptr) {
            // search for duplicates
            for (k = 0; k < num_fonts; k ++)
              if (strcmp(valptr, fontnames[k]) == 0) {
	        Fonts[NumFonts][i] = fonts[k];
                break;
              }

            if (k==num_fonts) {  // not found
	      fonts[num_fonts] = Fonts[NumFonts][i] = font_load(valptr);
              if (!fonts[num_fonts]) { // font missing/corrupt, replace by first
                fprintf(stderr,"WARNING: Ignored bad font \"%s\"\n",valptr);
                break;
              }
              fontnames[num_fonts++] = strdup(valptr);
            }
          }
	}

        /* ignore complete range, when the first font is not available */
        if (i==0) {
          continue;
        }

       /*
	* Fill in remaining fonts as needed...
	*/

	for (j = i; j < 4; j ++)
	  Fonts[NumFonts][j] = Fonts[NumFonts][0];

       /*
        * Define the character mappings...
	*/

	for (i = start; i <= end; i ++)
	{
          Codes[i] = NumFonts;
	}

       /*
        * Move to the next font, stopping if needed...
	*/

        NumFonts ++;
	if (NumFonts >= 256)
	  break;
      }

      fclose(fp);
    } // }}}
    else // {{{
    {
      fprintf(stderr, "ERROR: Bad charset type %s\n", lineptr);
      fclose(fp);
      exit(1);
    } // }}}
  } // }}}
  else // {{{ Standard ASCII
  {
   /*
    * Standard ASCII output just uses Courier, Courier-Bold, and
    * possibly Courier-Oblique.
    */

    NumFonts = 1;

    Fonts[0][ATTR_NORMAL]     = font_std("Courier");
    Fonts[0][ATTR_BOLD]       = font_std("Courier-Bold");
    Fonts[0][ATTR_ITALIC]     = font_std("Courier-Oblique");
    Fonts[0][ATTR_BOLDITALIC] = font_std("Courier-BoldOblique");

    Widths[0]     = 1;
    Directions[0] = 1;

   /*
    * Define US-ASCII characters...
    */

    for (i = 32; i < 127; i ++)
    {
      Chars[i] = i;
      Codes[i] = NumFonts-1;
    }
  }
  // }}}

  if (NumFonts==0) {
    fprintf(stderr, "ERROR: No usable font available\n");
    exit(1);
  }

  FontScaleX=120.0 / CharsPerInch;
  FontScaleY=68.0 / LinesPerInch;

  // allocate now, for pages to use. will be fixed in epilogue
  FontResource=pdfOut_add_xref(pdf);

  if (PrettyPrint)
  {
    Date=make_wide(curdate);
    Title=make_wide(title);
  }
}
// }}}

/*
 * {{{ 'write_line()' - Write a row of text.
 */

static void
write_line(int     row,		/* I - Row number (0 to N) */
           lchar_t *line)	/* I - Line to print */
{
  int		i;		/* Looping var */
  int		col,xcol,xwid;		/* Current column */
  int		attr;		/* Current attribute */
  int		font,		/* Font to use */
		lastfont,	/* Last font */
		mono;		/* Monospaced? */
  lchar_t	*start;		/* First character in sequence */


  xcol=0;
  for (col = 0, start = line; col < SizeColumns;)
  {
    while (col < SizeColumns && (line->ch == ' ' || line->ch == 0))
    {
      col ++;
      xcol ++;
      line ++;
    }

    if (col >= SizeColumns)
      break;

    if (NumFonts == 1)
    {
     /*
      * All characters in a single font - assume monospaced and single width...
      */

      attr  = line->attr;
      start = line;

      while (col < SizeColumns && line->ch != 0 && attr == line->attr)
      {
	col ++;
	line ++;
      }

      write_string(col - (line - start), row, line - start, start);
    }
    else
    {
     /*
      * Multiple fonts; break up based on the font...
      */

      attr     = line->attr;
      start    = line;
      xwid     = 0;
      if (UTF8) {
        lastfont = Codes[line->ch];
      } else {
        lastfont = Codes[Chars[line->ch]];
      }
//      mono     = strncmp(Fonts[lastfont][0], "Courier", 7) == 0;
mono=1; // TODO

      col ++;
      xwid += Widths[lastfont];
      line ++;

      if (mono)
      {
	while (col < SizeColumns && line->ch != 0 && attr == line->attr)
	{
          if (UTF8) {
            font = Codes[line->ch];
          } else {
            font = Codes[Chars[line->ch]];
          }
          if (/*strncmp(Fonts[font][0], "Courier", 7) != 0 ||*/ // TODO
	      font != lastfont)
	    break;

	  col ++;
          xwid += Widths[lastfont];
	  line ++;
	}
      }

      if (Directions[lastfont] > 0) {
        write_string(xcol, row, line - start, start);
        xcol += xwid;
      }
      else
      {
       /*
        * Do right-to-left text... ; assume no font change without direction change
	*/

	while (col < SizeColumns && line->ch != 0 && attr == line->attr)
	{
          if (UTF8) {
            font = Codes[line->ch];
          } else {
            font = Codes[Chars[line->ch]];
          }
          if (Directions[font] > 0 &&
	      !ispunct(line->ch & 255) && !isspace(line->ch & 255))
	    break;

	  col ++;
          xwid += Widths[lastfont];
	  line ++;
	}

        for (i = 1; start < line; i ++, start ++)
	  if (!isspace(start->ch & 255)) {
            xwid-=Widths[lastfont];
	    write_string(xcol + xwid, row, 1, start);
          } else {
            xwid--;
          }
      }
    }
  }
}
// }}}

static lchar_t *make_wide(const char *buf)  // {{{ - convert to lchar_t
{
  const unsigned char	*utf8;	/* UTF8 text */
  lchar_t *ret,*out;
  
  // this is enough, utf8 chars will only require less space
  out=ret=malloc((strlen(buf)+1)*sizeof(lchar_t)); 

  utf8 = (const unsigned char *)buf;
  while (*utf8)
  {
    out->attr=0;

    if (*utf8 < 0xc0 || !UTF8)
      out->ch = *utf8 ++;
    else if ((*utf8 & 0xe0) == 0xc0)
    {
     /*
      * Two byte character...
      */

      out->ch = ((utf8[0] & 0x1f) << 6) | (utf8[1] & 0x3f);
      utf8 += 2;
    }
    else
    {
     /*
      * Three byte character...
      */

      out->ch = ((((utf8[0] & 0x1f) << 6) | (utf8[1] & 0x3f)) << 6) |
                (utf8[2] & 0x3f);
      utf8 += 3;
    }

    out++;
  }
  out->ch=out->attr=0;
  return ret;
}
// }}}

/*
 * {{{ 'write_string()' - Write a string of text.
 */

static void
write_string(int     col,	/* I - Start column */
             int     row,	/* I - Row */
             int     len,	/* I - Number of characters */
             lchar_t *s)	/* I - String to print */
{
  float		x, y;		/* Position of text */
  unsigned	attr;		/* Character attributes */


 /*
  * Position the text and set the font...
  */

  if (Duplex && (NumPages & 1) == 0)
  {
    x = PageWidth - PageRight;
    y = PageTop;
  }
  else
  {
    x = PageLeft;
    y = PageTop;
  }

  x += (float)col * 72.0f / (float)CharsPerInch;
  y -= (float)(row + 0.843) * 72.0f / (float)LinesPerInch;

  attr = s->attr;

  if (attr & ATTR_RAISED)
    y += 36.0 / (float)LinesPerInch;
  else if (attr & ATTR_LOWERED)
    y -= 36.0 / (float)LinesPerInch;

  if (attr & ATTR_UNDERLINE)
    pdfOut_printf(pdf,"q 0.5 w 0 g %.3f %.3f m %.3f %.3f l S Q ",
                      x, y - 6.8 / LinesPerInch,
                      x + (float)len * 72.0 / (float)CharsPerInch,
                      y - 6.8 / LinesPerInch);

  if (PrettyPrint)
  {
    if (ColorDevice) {
      if (attr & ATTR_RED)
        pdfOut_printf(pdf,"0.5 0 0 rg\n");
      else if (attr & ATTR_GREEN)
        pdfOut_printf(pdf,"0 0.5 0 rg\n");
      else if (attr & ATTR_BLUE)
        pdfOut_printf(pdf,"0 0 0.5 rg\n");
      else
        pdfOut_printf(pdf,"0 g\n");
    } else {
      if ( (attr & ATTR_RED)||(attr & ATTR_GREEN)||(attr & ATTR_BLUE) )
        pdfOut_printf(pdf,"0.2 g\n");
      else
        pdfOut_printf(pdf,"0 g\n");
    }
  }
  else
    pdfOut_printf(pdf,"0 g\n");
  
  write_font_str(x,y,attr & ATTR_FONT,s,len);
}
// }}}

// {{{ show >len characters from >str, using the right font(s) at >x,>y
static void write_font_str(float x,float y,int fontid, lchar_t *str, int len)
{
  unsigned short		ch;		/* Current character */
  static char	*names[] =	/* Font names */
		{ "FN","FB","FI","FBI" };

  if (len==-1) {
    for (len=0;str[len].ch;len++);
  }
  pdfOut_printf(pdf,"BT\n");

  if (x == (int)x)
    pdfOut_printf(pdf,"  %.0f ", x);
  else
    pdfOut_printf(pdf,"  %.3f ", x);

  if (y == (int)y)
    pdfOut_printf(pdf,"%.0f Td\n", y);
  else
    pdfOut_printf(pdf,"%.3f Td\n", y);

  int lastfont,font;

  // split on font boundary
  while (len > 0) 
  {
   /*
    * Write a hex string...
    */
    if (UTF8) {
      lastfont=Codes[str->ch];
    } else {
      lastfont=Codes[Chars[str->ch]];
    }
    EMB_PARAMS *emb=Fonts[lastfont][fontid];
    OTF_FILE *otf=emb->font->sfnt;

    if (otf) { // TODO?
      pdfOut_printf(pdf,"  %.3f Tz\n",
                        FontScaleX*600.0/(otf_get_width(otf,4)*1000.0/otf->unitsPerEm)*100.0/FontScaleY); // TODO? 
      // gid==4 is usually '!', the char after space. We just need "the" width for the monospaced font. gid==0 is bad, and space might also be bad.
    } else {
      pdfOut_printf(pdf,"  %.3f Tz\n",
                        FontScaleX*100.0/FontScaleY); // TODO?
    }

    pdfOut_printf(pdf,"  /%s%02x %.3f Tf <",
                      names[fontid],lastfont,FontScaleY);

    while (len > 0)
    {
      if (UTF8) {
        ch=str->ch;
      } else {
        ch=Chars[str->ch];
      }

      font = Codes[ch];
      if (lastfont != font) { // only possible, when not used via write_string (e.g. utf-8filename.txt in prettyprint)
        break;
      }
      if (otf) { // TODO 
        const unsigned short gid=emb_get(emb,ch);
        pdfOut_printf(pdf,"%04x", gid);
      } else { // std 14 font with 7-bit us-ascii uses single byte encoding, TODO
        pdfOut_printf(pdf,"%02x",ch);
      }

      len --;
      str ++;
    }

    pdfOut_printf(pdf,"> Tj\n");
  }
  pdfOut_printf(pdf,"ET\n");
}
// }}}

static float stringwidth_x(lchar_t *str)
{
  int len;

  for (len=0;str[len].ch;len++);

  return  (float)len * 72.0 / (float)CharsPerInch;
}

static void write_pretty_header() // {{{
{
  float x,y;
  pdfOut_printf(pdf,"q\n"
                    "0.9 g\n");

  if (Duplex && (NumPages & 1) == 0) {
    x = PageWidth - PageRight;
    y = PageTop + 72.0f / LinesPerInch;
  } else {
    x = PageLeft;
    y = PageTop + 72.0f / LinesPerInch;
  }

  pdfOut_printf(pdf,"1 0 0 1 %.3f %.3f cm\n",x,y); // translate
  pdfOut_printf(pdf,"0 0 %.3f %.3f re f\n",
                    PageRight - PageLeft, 144.0f / LinesPerInch);
  pdfOut_printf(pdf,"0 g 0 G\n");

  if (Duplex && (NumPages & 1) == 0) {
      x = PageRight - PageLeft - 36.0f / LinesPerInch - stringwidth_x(Title);
      y = (0.5f + 0.157f) * 72.0f / LinesPerInch;
  } else {
      x = 36.0f / LinesPerInch;
      y = (0.5f + 0.157f) * 72.0f / LinesPerInch;
  }
  write_font_str(x,y,ATTR_BOLD,Title,-1);

  x = (-stringwidth_x(Date) + PageRight - PageLeft) * 0.5;
  write_font_str(x,y,ATTR_BOLD,Date,-1);

  // convert pagenumber to string
  char tmp[20];
  tmp[19]=0;
  snprintf(tmp,19,"%d",NumPages);
  lchar_t *pagestr=make_wide(tmp);

  if (Duplex && (NumPages & 1) == 0) {
      x = 36.0f / LinesPerInch;
  } else {
      x = PageRight - PageLeft - 36.0f / LinesPerInch - stringwidth_x(pagestr);
  }
  write_font_str(x,y,ATTR_BOLD,pagestr,-1);
  free(pagestr);

  pdfOut_printf(pdf,"Q\n");
}
// }}}


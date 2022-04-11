/*
 *   Text to PDF filter function for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 2008,2012 by Tobias Hoffmann.
 *   Copyright 2007 by Apple Inc.
 *   Copyright 1993-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

/*
 * Include necessary headers...
 */

#include <assert.h>
#include <config.h>
#include "cupsfilters/pdfutils.h"
#include "cupsfilters/raster.h"
#include <ctype.h>
#include <errno.h>
#include "filter.h"
#include "fontconfig/fontconfig.h"
#include "fontembed/embed.h"
#include "fontembed/sfnt.h"
#include "fontembed/fontfile.h"

/*
 * Constants...
 */

#define ATTR_NORMAL	0x00
#define ATTR_BOLD	0x01
#define ATTR_ITALIC	0x02
#define ATTR_BOLDITALIC	0x03
#define ATTR_FONT	0x03

#define ATTR_UNDERLINE	0x04
#define ATTR_RAISED	0x08
#define ATTR_LOWERED	0x10
#define ATTR_RED	0x20
#define ATTR_GREEN	0x40
#define ATTR_BLUE	0x80

#define PRETTY_OFF	0
#define PRETTY_PLAIN	1
#define PRETTY_CODE	2
#define PRETTY_SHELL	3
#define PRETTY_PERL	4
#define PRETTY_HTML	5

/*
 * Globals...
 */

static char *code_keywords[] =	/* List of known C/C++ keywords... */
	{
	  "and",
	  "and_eq",
	  "asm",
	  "auto",
	  "bitand",
	  "bitor",
	  "bool",
	  "break",
	  "case",
	  "catch",
	  "char",
	  "class",
	  "compl",
	  "const",
	  "const_cast",
	  "continue",
	  "default",
	  "delete",
	  "do",
	  "double",
	  "dynamic_cast",
	  "else",
	  "enum",
	  "explicit",
	  "extern",
	  "false",
	  "float",
	  "for",
	  "friend",
	  "goto",
	  "if",
	  "inline",
	  "int",
	  "long",
	  "mutable",
	  "namespace",
	  "new",
	  "not",
	  "not_eq",
	  "operator",
	  "or",
	  "or_eq",
	  "private",
	  "protected",
	  "public",
	  "register",
	  "reinterpret_cast",
	  "return",
	  "short",
	  "signed",
	  "sizeof",
	  "static",
	  "static_cast",
	  "struct",
	  "switch",
	  "template",
	  "this",
	  "throw",
	  "true",
	  "try",
	  "typedef",
	  "typename",
	  "union",
	  "unsigned",
	  "virtual",
	  "void",
	  "volatile",
	  "while",
	  "xor",
	  "xor_eq"
	},
	*sh_keywords[] =	/* List of known Boure/Korn/zsh/bash keywords... */
	{
	  "alias",
	  "bg",
	  "break",
	  "case",
	  "cd",
	  "command",
	  "continue",
	  "do",
	  "done",
	  "echo",
	  "elif",
	  "else",
	  "esac",
	  "eval",
	  "exec",
	  "exit",
	  "export",
	  "fc",
	  "fg",
	  "fi",
	  "for",
	  "function",
	  "getopts",
	  "if",
	  "in",
	  "jobs",
	  "kill",
	  "let",
	  "limit",
	  "newgrp",
	  "print",
	  "pwd",
	  "read",
	  "readonly",
	  "return",
	  "select",
	  "set",
	  "shift",
	  "test",
	  "then",
	  "time",
	  "times",
	  "trap",
	  "typeset",
	  "ulimit",
	  "umask",
	  "unalias",
	  "unlimit",
	  "unset",
	  "until",
	  "wait",
	  "whence"
	  "while",
	},
	*csh_keywords[] =	/* List of known csh/tcsh keywords... */
	{
	  "alias",
	  "aliases",
	  "bg",
	  "bindkey",
	  "break",
	  "breaksw",
	  "builtins",
	  "case",
	  "cd",
	  "chdir",
	  "complete",
	  "continue",
	  "default",
	  "dirs",
	  "echo",
	  "echotc",
	  "else",
	  "end",
	  "endif",
	  "eval",
	  "exec",
	  "exit",
	  "fg",
	  "foreach",
	  "glob",
	  "goto",
	  "history",
	  "if",
	  "jobs",
	  "kill",
	  "limit",
	  "login",
	  "logout",
	  "ls",
	  "nice",
	  "nohup",
	  "notify",
	  "onintr",
	  "popd",
	  "pushd",
	  "pwd",
	  "rehash",
	  "repeat",
	  "set",
	  "setenv",
	  "settc",
	  "shift",
	  "source",
	  "stop",
	  "suspend",
	  "switch",
	  "telltc",
	  "then",
	  "time",
	  "umask",
	  "unalias",
	  "unbindkey",
	  "unhash",
	  "unlimit",
	  "unset",
	  "unsetenv",
	  "wait",
	  "where",
	  "which",
	  "while"
	},
	*perl_keywords[] =	/* List of known perl keywords... */
	{
	  "abs",
	  "accept",
	  "alarm",
	  "and",
	  "atan2",
	  "bind",
	  "binmode",
	  "bless",
	  "caller",
	  "chdir",
	  "chmod",
	  "chomp",
	  "chop",
	  "chown",
	  "chr",
	  "chroot",
	  "closdir",
	  "close",
	  "connect",
	  "continue",
	  "cos",
	  "crypt",
	  "dbmclose",
	  "dbmopen",
	  "defined",
	  "delete",
	  "die",
	  "do",
	  "dump",
	  "each",
	  "else",
	  "elsif",
	  "endgrent",
	  "endhostent",
	  "endnetent",
	  "endprotoent",
	  "endpwent",
	  "endservent",
	  "eof",
	  "eval",
	  "exec",
	  "exists",
	  "exit",
	  "exp",
	  "fcntl",
	  "fileno",
	  "flock",
	  "for",
	  "foreach",
	  "fork",
	  "format",
	  "formline",
	  "getc",
	  "getgrent",
	  "getgrgid",
	  "getgrnam",
	  "gethostbyaddr",
	  "gethostbyname",
	  "gethostent",
	  "getlogin",
	  "getnetbyaddr",
	  "getnetbyname",
	  "getnetent",
	  "getpeername",
	  "getpgrp",
	  "getppid",
	  "getpriority",
	  "getprotobyname",
	  "getprotobynumber",
	  "getprotoent",
	  "getpwent",
	  "getpwnam",
	  "getpwuid",
	  "getservbyname",
	  "getservbyport",
	  "getservent",
	  "getsockname",
	  "getsockopt",
	  "glob",
	  "gmtime",
	  "goto",
	  "grep",
	  "hex",
	  "if",
	  "import",
	  "index",
	  "int",
	  "ioctl",
	  "join",
	  "keys",
	  "kill",
	  "last",
	  "lc",
	  "lcfirst",
	  "length",
	  "link",
	  "listen",
	  "local",
	  "localtime",
	  "log",
	  "lstat",
	  "map",
	  "mkdir",
	  "msgctl",
	  "msgget",
	  "msgrcv",
	  "msgsend",
	  "my",
	  "next",
	  "no",
	  "not",
	  "oct",
	  "open",
	  "opendir",
	  "or",
	  "ord",
	  "pack",
	  "package",
	  "pipe",
	  "pop",
	  "pos",
	  "print",
	  "printf",
	  "push",
	  "quotemeta",
	  "rand",
	  "read",
	  "readdir",
	  "readlink",
	  "recv",
	  "redo",
	  "ref",
	  "rename",
	  "require",
	  "reset",
	  "return",
	  "reverse",
	  "rewinddir",
	  "rindex",
	  "rmdir",
	  "scalar",
	  "seek",
	  "seekdir",
	  "select",
	  "semctl",
	  "semget",
	  "semop",
	  "send",
	  "setgrent",
	  "sethostent",
	  "setnetent",
	  "setpgrp",
	  "setpriority",
	  "setprotoent",
	  "setpwent",
	  "setservent",
	  "setsockopt",
	  "shift",
	  "shmctl",
	  "shmget",
	  "shmread",
	  "shmwrite",
	  "shutdown",
	  "sin",
	  "sleep",
	  "socket",
	  "socketpair",
	  "sort",
	  "splice",
	  "split",
	  "sprintf",
	  "sqrt",
	  "srand",
	  "stat",
	  "study",
	  "sub",
	  "substr",
	  "symlink",
	  "syscall",
	  "sysread",
	  "sysseek",
	  "system",
	  "syswrite",
	  "tell",
	  "telldir",
	  "tie",
	  "tied",
	  "time",
	  "times"
	  "times",
	  "truncate",
	  "uc",
	  "ucfirst",
	  "umask",
	  "undef",
	  "unless",
	  "unlink",
	  "unpack",
	  "unshift",
	  "untie",
	  "until",
	  "use",
	  "utime",
	  "values",
	  "vec",
	  "wait",
	  "waitpid",
	  "wantarray",
	  "warn",
	  "while",
	  "write"
	};

/*
 * Types...
 */

typedef struct			/**** Character/attribute structure... ****/
{
  unsigned short ch,		/* Character */
		attr;		/* Any attributes */
} lchar_t;

typedef struct texttopdf_doc_s
{
  int		NumFonts;	/* Number of fonts to use */
  EMB_PARAMS	*Fonts[256][4];	/* Fonts to use */
  unsigned short Chars[256];	/* Input char to unicode */
  unsigned char	Codes[65536];	/* Unicode glyph mapping to font */
  int		Widths[256];	/* Widths of each font */
  int		Directions[256];/* Text directions for each font */
  cf_pdf_out_t	*pdf;
  int		FontResource;   /* Object number of font resource dictionary */
  float		FontScaleX, FontScaleY; /* The font matrix */
  lchar_t	*Title, *Date;	/* The title and date strings */

  cups_page_header2_t h;        /* CUPS Raster page header, to */
                                /* accommodate results of command */
                                /* line parsing for PPD-less queue */
  cf_filter_texttopdf_parameter_t env_vars;
  int		NumKeywords;
  float		PageLeft,	/* Left margin */
		PageRight,	/* Right margin */
		PageBottom,	/* Bottom margin */
		PageTop,	/* Top margin */
		PageWidth,	/* Total page width */
		PageLength;
  int		NumPages;
  int		WrapLines,	/* Wrap text in lines */
		SizeLines,	/* Number of lines on a page */
		SizeColumns,	/* Number of columns on a line */
		PageColumns,	/* Number of columns on a page */
		ColumnGutter,	/* Number of characters between text columns */
		ColumnWidth,	/* Width of each column */
		PrettyPrint,	/* Do pretty code formatting? */
		Copies;		/* Number of copies to produce */
  float		CharsPerInch,	/* Number of character columns per inch */
		LinesPerInch;	/* Number of lines per inch */
  int		UTF8;
  char		**Keywords;	/* List of known keywords... */

  int		Orientation,	/* 0 = portrait, 1 = landscape, etc. */
		Duplex,		/* Duplexed? */
		LanguageLevel,	/* Language level of printer */
		ColorDevice;
  lchar_t	**Page;
} texttopdf_doc_t;


/*
 * Local functions...
 */

static EMB_PARAMS *font_load(const char *font, int fontwidth, cf_logfunc_t log,
			     void *ld);
static EMB_PARAMS *font_std(const char *name);
static int	compare_keywords(const void *k1, const void *k2);
static int	get_utf8(FILE *fp);
static void	write_line(int row, lchar_t *line, texttopdf_doc_t *doc);
static void	write_string(int col, int row, int len, lchar_t *s,
			     texttopdf_doc_t *doc);
static lchar_t  *make_wide(const char *buf, texttopdf_doc_t *doc);
static void     write_font_str(float x,float y,int fontid, lchar_t *str,
			       int len, texttopdf_doc_t *doc);
static void     write_pretty_header();
static int      write_prolog(const char *title, const char *user,
			    const char *classification, const char *label, 
			    ppd_file_t *ppd, texttopdf_doc_t *doc,
			    cf_logfunc_t log, void *ld);
static void     write_page(texttopdf_doc_t *doc);
static void     write_epilogue(texttopdf_doc_t *doc);


/*
 * 'cfFilterTextToPDF()' - Main entry for text to PDF filter.
 */

int				/* O - Exit status */
cfFilterTextToPDF(int inputfd,  	/* I - File descriptor input stream */
	  int outputfd, 	/* I - File descriptor output stream */
	  int inputseekable,	/* I - Is input stream seekable? (unused) */
	  cf_filter_data_t *data,	/* I - Job and printer data */
	  void *parameters)	/* I - Filter-specific parameters (unused) */
{
  texttopdf_doc_t doc;
  int		i,		/* Looping var */
		empty,		/* Is the input empty? */
		ch,		/* Current char from file */
		lastch,		/* Previous char from file */
		attr,		/* Current attribute */
		line,		/* Current line */
		column,		/* Current column */
		page_column;	/* Current page column */

  const char	*val;		/* Option value */
  char		keyword[64],	/* Keyword string */
		*keyptr;	/* Pointer into string */
  int		keycol;		/* Column where keyword starts */
  enum	{NLstyl=-1, NoCmnt, SNTXstyl}
  		cmntState;	/* Inside a comment */
  enum	{StrBeg=-1, NoStr, StrEnd}	
  		strState;	/* Inside a dbl-quoted string */

  cf_logfunc_t log = data->logfunc;
  void		*ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void		*icd = data->iscanceleddata;
  FILE		*fp;		/* Print file */
  int		stdoutbackupfd;	/* The "real" stdout is backupped here while */
				/* stdout is redirected */
  int		ret = 0;	/* Return value */


 /*
  * Make sure status messages are not buffered...
  */
  #ifdef CUPS_1_4 /* CUPS 1.4.x or newer: only UTF8 is supported */
  doc.UTF8 = 1;               /* Use UTF-8 encoding? */
  #endif /* CUPS_1_4 */

  doc.WrapLines = 1;		/* Wrap text in lines */
  doc.SizeLines = 60;		/* Number of lines on a page */
  doc.SizeColumns = 80;		/* Number of columns on a line */
  doc.PageColumns = 1;		/* Number of columns on a page */
  doc.ColumnGutter = 0;		/* Number of characters between text columns */
  doc.ColumnWidth = 80;		/* Width of each column */
  doc.PrettyPrint = 0;		/* Do pretty code formatting */
  doc.Copies = 1;		/* Number of copies */
  doc.Page = NULL;		/* Page characters */
  doc.NumPages = 0;		/* Number of pages in document */
  doc.CharsPerInch = 10;	/* Number of character columns per inch */
  doc.LinesPerInch = 6;		/* Number of lines per inch */
  doc.NumKeywords = 0;		/* Number of known keywords */
  doc.Keywords = NULL;		/* List of known keywords */
  doc.Orientation = 0;		/* 0 = portrait, 1 = landscape, etc. */
  doc.Duplex = 0;		/* Duplexed? */
  doc.LanguageLevel = 1;	/* Language level of printer */
  doc.ColorDevice = 1;		/* Do color text? */
  doc.PageLeft = 18.0f;		/* Left margin */
  doc.PageRight = 594.0f;	/* Right margin */
  doc.PageBottom = 36.0f;	/* Bottom margin */
  doc.PageTop = 756.0f;		/* Top margin */
  doc.PageWidth = 612.0f;	/* Total page width */
  doc.PageLength = 792.0f;	/* Total page length */
  doc.pdf = NULL;

  if (parameters) {
    doc.env_vars = *((cf_filter_texttopdf_parameter_t *)parameters);
  } else {
    doc.env_vars.data_dir = CUPS_DATADIR;
    doc.env_vars.char_set = NULL;
    doc.env_vars.content_type = NULL;
    doc.env_vars.classification = NULL;
  }

  setbuf(stderr, NULL);

 /*
  * Open the input data stream specified by the inputfd...
  */

  if ((fp = fdopen(inputfd, "rb")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "textopdf: Unable to open input data stream.");
    }
    return (1);
  }

 /*
  * Redirect stdout to the outputfd (the PDF output strem of this filter
  * function)
  */

  if (outputfd != 1)
  {
    stdoutbackupfd = dup(1);
    dup2(outputfd, 1);
    close(outputfd);
  }

 /*
  * Process command-line options and write the prolog...
  */

  if (data->ppd)
  {
    ppdMarkOptions(data->ppd, data->num_options, data->options);
  }

  if ((val = cupsGetOption("prettyprint",
			   data->num_options, data->options)) != NULL &&
      strcasecmp(val, "no") && strcasecmp(val, "off") &&
      strcasecmp(val, "false"))
  {
    doc.PageLeft     = 72.0f;
    doc.PageRight    = doc.PageWidth - 36.0f;
    doc.PageBottom   = doc.PageBottom > 36.0f ? doc.PageBottom : 36.0f;
    doc.PageTop      = doc.PageLength - 36.0f;
    doc.CharsPerInch = 12;
    doc.LinesPerInch = 8;

    if ((val = doc.env_vars.content_type) == NULL)
    {
      doc.PrettyPrint = PRETTY_PLAIN;
      doc.NumKeywords = 0;
      doc.Keywords    = NULL;
    }
    else if (strcasecmp(val, "application/x-cshell") == 0)
    {
      doc.PrettyPrint = PRETTY_SHELL;
      doc.NumKeywords = sizeof(csh_keywords) / sizeof(csh_keywords[0]);
      doc.Keywords    = csh_keywords;
    }
    else if (strcasecmp(val, "application/x-csource") == 0)
    {
      doc.PrettyPrint = PRETTY_CODE;
      doc.NumKeywords = sizeof(code_keywords) / sizeof(code_keywords[0]);
      doc.Keywords    = code_keywords;
    }
    else if (strcasecmp(val, "application/x-perl") == 0)
    {
      doc.PrettyPrint = PRETTY_PERL;
      doc.NumKeywords = sizeof(perl_keywords) / sizeof(perl_keywords[0]);
      doc.Keywords    = perl_keywords;
    }
    else if (strcasecmp(val, "application/x-shell") == 0)
    {
      doc.PrettyPrint = PRETTY_SHELL;
      doc.NumKeywords = sizeof(sh_keywords) / sizeof(sh_keywords[0]);
      doc.Keywords    = sh_keywords;
    }
    else
    {
      doc.PrettyPrint = PRETTY_PLAIN;
      doc.NumKeywords = 0;
      doc.Keywords    = NULL;
    }
  }

  cfFilterSetCommonOptions(data->ppd, data->num_options, data->options, 1,
			 &(doc.Orientation), &(doc.Duplex),
			 &(doc.LanguageLevel), &(doc.ColorDevice),
			 &(doc.PageLeft), &(doc.PageRight), &(doc.PageTop),
			 &(doc.PageBottom), &(doc.PageWidth), &(doc.PageLength),
			 log, ld);

  if (!data->ppd) {
    cfRasterParseIPPOptions(&(doc.h), data, 0, 1);
    doc.Orientation = doc.h.Orientation;
    doc.Duplex = doc.h.Duplex;
    doc.ColorDevice = doc.h.cupsNumColors <= 1 ? 0 : 1;
    doc.PageWidth = doc.h.cupsPageSize[0] != 0.0 ? doc.h.cupsPageSize[0] :
      (float)doc.h.PageSize[0];
    doc.PageLength = doc.h.cupsPageSize[1] != 0.0 ? doc.h.cupsPageSize[1] :
      (float)doc.h.PageSize[1];
    doc.PageLeft = doc.h.cupsImagingBBox[0] != 0.0 ? doc.h.cupsImagingBBox[0] :
      (float)doc.h.ImagingBoundingBox[0];
    doc.PageBottom = doc.h.cupsImagingBBox[1] != 0.0 ?
      doc.h.cupsImagingBBox[1] : (float)doc.h.ImagingBoundingBox[1];
    doc.PageRight = doc.h.cupsImagingBBox[2] != 0.0 ? doc.h.cupsImagingBBox[2] :
      (float)doc.h.ImagingBoundingBox[2];
    doc.PageTop = doc.h.cupsImagingBBox[3] != 0.0 ? doc.h.cupsImagingBBox[3] :
      (float)doc.h.ImagingBoundingBox[3];
    doc.Copies = doc.h.NumCopies;
  }

  if ((val = cupsGetOption("wrap", data->num_options, data->options)) == NULL)
    doc.WrapLines = 1;
  else
    doc.WrapLines = !strcasecmp(val, "true") || !strcasecmp(val, "on") ||
                !strcasecmp(val, "yes");

  if ((val = cupsGetOption("columns", data->num_options,
			   data->options)) != NULL)
  {
    doc.PageColumns = atoi(val);

    if (doc.PageColumns < 1)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Bad columns value %d", doc.PageColumns);
      ret = 1;
      goto out;
    }
  }

  if ((val = cupsGetOption("cpi", data->num_options, data->options)) != NULL)
  {
    doc.CharsPerInch = atof(val);

    if (doc.CharsPerInch <= 0.0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Bad cpi value %f", doc.CharsPerInch);
      ret = 1;
      goto out;
    }
  }

  if ((val = cupsGetOption("lpi", data->num_options, data->options)) != NULL)
  {
    doc.LinesPerInch = atof(val);

    if (doc.LinesPerInch <= 0.0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Bad lpi value %f", doc.LinesPerInch);
      ret = 1;
      goto out;
    }
  }

  if (doc.PrettyPrint)
    doc.PageTop -= 216.0f / doc.LinesPerInch;

 /*
  * Allocate memory for the page...
  */

  doc.SizeColumns = (doc.PageRight - doc.PageLeft) / 72.0 * doc.CharsPerInch;
  doc.SizeLines   = (doc.PageTop - doc.PageBottom) / 72.0 * doc.LinesPerInch;

 /*
  * Enforce minimum size...
  */
  if (doc.SizeColumns < 1)
    doc.SizeColumns = 1;
  if (doc.SizeLines < 1)
    doc.SizeLines = 1;

  if (doc.SizeLines >= INT_MAX / doc.SizeColumns / sizeof(lchar_t))
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR, "cfFilterTextToPDF: Bad page size");
    ret = 1;
    goto out;
  }

  doc.Page    = calloc(sizeof(lchar_t *), doc.SizeLines);
  if (!doc.Page)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterTextToPDF: cannot allocate memory for page");
    ret = 1;
    goto out;
  }

  doc.Page[0] = calloc(sizeof(lchar_t), doc.SizeColumns * doc.SizeLines);
  if (!doc.Page[0])
  {
    free(doc.Page);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterTextToPDF: cannot allocate memory for page");
    ret = 1;
    goto out;
  }

  for (i = 1; i < doc.SizeLines; i ++)
    doc.Page[i] = doc.Page[0] + i * doc.SizeColumns;

  doc.Copies = data->copies;

 /*
  * Read text from the specified source and print it...
  */

  empty        = 1;
  lastch       = 0;
  column       = 0;
  line         = 0;
  page_column  = 0;
  attr         = 0;
  keyptr       = keyword;
  keycol       = 0;
  cmntState     = NoCmnt;
  strState      = NoStr;

  while ((ch = get_utf8(fp)) >= 0)
  {
    if (empty)
    {
      /* Found the first valid character, write file header */
      empty = 0;
      ret = write_prolog(data->job_title, data->job_user,
			doc.env_vars.classification,
			cupsGetOption("page-label", data->num_options,
				      data->options),
			data->ppd, &doc, log, ld);
      if (ret)
	goto out;
    }

   /*
    * Control codes:
    *
    *   BS	Backspace (0x08)
    *   HT	Horizontal tab; next 8th column (0x09)
    *   LF	Line feed; forward full line (0x0a)
    *   VT	Vertical tab; reverse full line (0x0b)
    *   FF	Form feed (0x0c)
    *   CR	Carriage return (0x0d)
    *   ESC 7	Reverse full line (0x1b 0x37)
    *   ESC 8	Reverse half line (0x1b 0x38)
    *   ESC 9	Forward half line (0x1b 0x39)
    */

    switch (ch)
    {
      case 0x08 :		/* BS - backspace for boldface & underline */
          if (column > 0)
            column --;

          keyptr = keyword;
	  keycol = column;
          break;

      case 0x09 :		/* HT - tab to next 8th column */
          if (doc.PrettyPrint && keyptr > keyword)
	  {
	    *keyptr = '\0';
	    keyptr  = keyword;

	    if (bsearch(&keyptr, doc.Keywords, doc.NumKeywords, sizeof(char *),
	                compare_keywords))
            {
	     /*
	      * Put keywords in boldface...
	      */

	      i = page_column * (doc.ColumnWidth + doc.ColumnGutter);

	      while (keycol < column)
	      {
	        doc.Page[line][keycol + i].attr |= ATTR_BOLD;
		keycol ++;
	      }
	    }
	  }

          column = (column + 8) & ~7;

          if (column >= doc.ColumnWidth && doc.WrapLines)
          {			/* Wrap text to margins */
            line ++;
            column = 0;

            if (line >= doc.SizeLines)
            {
              page_column ++;
              line = 0;

              if (page_column >= doc.PageColumns)
              {
                write_page(&doc);
		page_column = 0;
              }
            }
          }

	  keycol = column;

          attr &= ~ATTR_BOLD;
          break;

      case 0x0d :		/* CR */
#ifndef __APPLE__
         /*
	  * All but MacOS/Darwin treat CR as was intended by ANSI
	  * folks, namely to move to column 0/1.  Some programs still
	  * use this to do boldfacing and underlining...
	  */

          column = 0;
          break;
#else
         /*
	  * MacOS/Darwin still need to treat CR as a line ending.
	  */

          {
	    int nextch;
            if ((nextch = getc(fp)) != 0x0a)
	      ungetc(nextch, fp);
	    else
	      ch = nextch;
	  }
#endif /* !__APPLE__ */

      case 0x0a :		/* LF - output current line */
          if (doc.PrettyPrint && keyptr > keyword)
	  {
	    *keyptr = '\0';
	    keyptr  = keyword;

	    if (bsearch(&keyptr, doc.Keywords, doc.NumKeywords, sizeof(char *),
	                compare_keywords))
            {
	     /*
	      * Put keywords in boldface...
	      */

	      i = page_column * (doc.ColumnWidth + doc.ColumnGutter);

	      while (keycol < column)
	      {
	        doc.Page[line][keycol + i].attr |= ATTR_BOLD;
		keycol ++;
	      }
	    }
	  }

          line ++;
          column = 0;
	  keycol = 0;

	  if (cmntState == NLstyl)
	  	cmntState = NoCmnt;

          if (!cmntState && !strState)
	    attr &= ~(ATTR_ITALIC | ATTR_BOLD | ATTR_RED | ATTR_GREEN |
		      ATTR_BLUE);

          if (line >= doc.SizeLines)
          {
            page_column ++;
            line = 0;

            if (page_column >= doc.PageColumns)
            {
              write_page(&doc);
	      page_column = 0;
            }
          }
          break;

      case 0x0b :		/* VT - move up 1 line */
          if (line > 0)
	    line --;

          keyptr = keyword;
	  keycol = column;

	  if (cmntState == NLstyl)
	  	cmntState = NoCmnt;

          if (!cmntState && !strState)
	    attr &= ~(ATTR_ITALIC | ATTR_BOLD | ATTR_RED | ATTR_GREEN |
		      ATTR_BLUE);
          break;

      case 0x0c :		/* FF - eject current page... */
          if (doc.PrettyPrint && keyptr > keyword)
	  {
	    *keyptr = '\0';
	    keyptr  = keyword;

	    if (bsearch(&keyptr, doc.Keywords, doc.NumKeywords, sizeof(char *),
	                compare_keywords))
            {
	     /*
	      * Put keywords in boldface...
	      */

	      i = page_column * (doc.ColumnWidth + doc.ColumnGutter);

	      while (keycol < column)
	      {
	        doc.Page[line][keycol + i].attr |= ATTR_BOLD;
		keycol ++;
	      }
	    }
	  }

          page_column ++;
	  column = 0;
	  keycol = 0;
          line   = 0;

	  if (cmntState == NLstyl)
	  	cmntState = NoCmnt;

          if (!cmntState && !strState)
	    attr &= ~(ATTR_ITALIC | ATTR_BOLD | ATTR_RED | ATTR_GREEN |
		      ATTR_BLUE);

          if (page_column >= doc.PageColumns)
          {
            write_page(&doc);
            page_column = 0;
          }
          break;

      case 0x1b :		/* Escape sequence */
          ch = get_utf8(fp);
	  if (ch == '7')
	  {
	   /*
	    * ESC 7	Reverse full line (0x1b 0x37)
	    */

            if (line > 0)
	      line --;
	  }
	  else if (ch == '8')
	  {
           /*
	    *   ESC 8	Reverse half line (0x1b 0x38)
	    */

            if ((attr & ATTR_RAISED) && line > 0)
	    {
	      attr &= ~ATTR_RAISED;
              line --;
	    }
	    else if (attr & ATTR_LOWERED)
	      attr &= ~ATTR_LOWERED;
	    else
	      attr |= ATTR_RAISED;
	  }
	  else if (ch == '9')
	  {
           /*
	    *   ESC 9	Forward half line (0x1b 0x39)
	    */

            if ((attr & ATTR_LOWERED) && line < (doc.SizeLines - 1))
	    {
	      attr &= ~ATTR_LOWERED;
              line ++;
	    }
	    else if (attr & ATTR_RAISED)
	      attr &= ~ATTR_RAISED;
	    else
	      attr |= ATTR_LOWERED;
	  }
	  break;

      default :			/* All others... */
          if (ch < ' ')
            break;		/* Ignore other control chars */

          if (doc.PrettyPrint > PRETTY_PLAIN)
	  {
	   /*
	    * Do highlighting of C/C++ keywords, preprocessor commands,
	    * and comments...
	    */

	    if (ch == ' ' && (attr & ATTR_BOLD))
	    {
	     /*
	      * Stop bolding preprocessor command...
	      */

	      attr &= ~ATTR_BOLD;
	    }
	    else if (!(isalnum(ch & 255) || ch == '_') && keyptr > keyword)
	    {
	     /*
	      * Look for a keyword...
	      */

	      *keyptr = '\0';
	      keyptr  = keyword;

	      if (bsearch(&keyptr, doc.Keywords, doc.NumKeywords,
			  sizeof(char *), compare_keywords))
              {
	       /*
	        * Put keywords in boldface...
		*/

	        i = page_column * (doc.ColumnWidth + doc.ColumnGutter);

		while (keycol < column)
		{
	          doc.Page[line][keycol + i].attr |= ATTR_BOLD;
		  keycol ++;
		}
	      }
	    }

	   /*
	    * Look for Syntax-transition Starts...
	    */
	    if (!cmntState && !strState)
	    {
	      if ((isalnum(ch & 255) || ch == '_'))
	      {
	       /*
	        * Add characters to the current keyword (if they'll fit).
	        */

	        if (keyptr == keyword)
	          keycol = column;

	        if (keyptr < (keyword + sizeof(keyword) - 1))
	          *keyptr++ = ch;
	      }
	      else if (ch == '\"' && lastch != '\\')
	      {
	       /*
	        * Start a dbl-quote string constant...
	        */

	        strState = StrBeg;
		attr    = ATTR_BLUE;
	      }
	      else if (ch == '*' && lastch == '/' &&
	               doc.PrettyPrint != PRETTY_SHELL)
	      {
	       /*
	        * Start a C-style comment...
	        */

	        cmntState = SNTXstyl;
	        attr     = ATTR_ITALIC | ATTR_GREEN;
	      }
	      else if (ch == '/' && lastch == '/' &&
	               doc.PrettyPrint == PRETTY_CODE)
	      {
	       /*
	        * Start a C++-style comment...
	        */

	        cmntState = NLstyl;
	        attr = ATTR_ITALIC | ATTR_GREEN;
	      }
	      else if (ch == '#' && doc.PrettyPrint != PRETTY_CODE)
	      {
	       /*
	        * Start a shell-style comment...
	        */

	        cmntState = NLstyl;
	        attr = ATTR_ITALIC | ATTR_GREEN;
	      }
	      else if (ch == '#' && column == 0 &&
	               doc.PrettyPrint == PRETTY_CODE)
	      {
	       /*
	        * Start a preprocessor command...
	        */

	        attr = ATTR_BOLD | ATTR_RED;
	      }
	    }
          }

          if (column >= doc.ColumnWidth && doc.WrapLines)
          {			/* Wrap text to margins */
            column = 0;
	    line ++;

            if (line >= doc.SizeLines)
            {
              page_column ++;
              line = 0;

              if (page_column >= doc.PageColumns)
              {
        	write_page(&doc);
        	page_column = 0;
              }
            }
          }

         /*
	  * Add text to the current column & line...
	  */

          if (column < doc.ColumnWidth)
	  {
	    i = column + page_column * (doc.ColumnWidth + doc.ColumnGutter);

            if (doc.PrettyPrint)
              doc.Page[line][i].attr = attr;

	    if (ch == ' ' && doc.Page[line][i].ch)
	      ch = doc.Page[line][i].ch;
            else if (ch == doc.Page[line][i].ch)
              doc.Page[line][i].attr |= ATTR_BOLD;
            else if (doc.Page[line][i].ch == '_')
              doc.Page[line][i].attr |= ATTR_UNDERLINE;
            else if (ch == '_')
	    {
              doc.Page[line][i].attr |= ATTR_UNDERLINE;

              if (doc.Page[line][i].ch)
	        ch = doc.Page[line][i].ch;
	    }
	    else
              doc.Page[line][i].attr = attr;

            doc.Page[line][i].ch = ch;
	  }

          if (doc.PrettyPrint)
	  {
	    if ((ch == '{' || ch == '}') && !cmntState && !strState &&
	        column < doc.ColumnWidth)
	    {
	     /*
	      * Highlight curley braces...
	      */

	      doc.Page[line][column].attr |= ATTR_BOLD;
	    }
	    else if ((ch == '/' || ch == '*') && lastch == '/' &&
	             column < doc.ColumnWidth &&
		     doc.PrettyPrint != PRETTY_SHELL)
	    {
	     /*
	      * Highlight first comment character...
	      */

	      doc.Page[line][column - 1].attr = attr;
	    }
	    else if (ch == '\"' && lastch != '\\' && !cmntState &&
		     strState == StrEnd)
	    {
	     /*
	      * End a dbl-quote string constant...
	      */

	      strState = NoStr;
	      attr    &= ~ATTR_BLUE;
            }
	    else if (ch == '/' && lastch == '*' && cmntState)
	    {
	     /*
	      * End a C-style comment...
	      */

	      cmntState = NoCmnt;
	      attr     &= ~(ATTR_ITALIC | ATTR_GREEN);
	    }

            if (strState == StrBeg)
	      strState = StrEnd;
	  }

          column ++;
          break;
    }

   /*
    * Save this character for the next cycle.
    */

    lastch = ch;
  }
  
  /* Do not write anything if the input file is empty */
  if (empty)
  {
    if(log) log(ld, CF_LOGLEVEL_DEBUG,
		"Input is empty, outputting empty file");
    goto out;
  }

 /*
  * Write any remaining page data...
  */

  if (line > 0 || page_column > 0 || column > 0)
    write_page(&doc);

 /*
  * Write the epilog and return...
  */

  write_epilogue(&doc);

 out:

 /*
  * Close input data stream
  */

  if (fp != stdin)
    fclose(fp);

 /*
  * Flush and close output data stream
  */

  fflush(stdout);
  close(1);

 /*
  * Re-activate stdout output
  */

  if (outputfd != 1)
  {
    dup2(stdoutbackupfd, 1);
    close(stdoutbackupfd);
  }

 /*
  * Clean up
  */

  if (doc.Page)
  {
    free(doc.Page[0]);
    free(doc.Page);
  }

  return (ret);
}


static EMB_PARAMS *font_load(const char *font, int fontwidth, cf_logfunc_t log,
		      void *ld)
{
  OTF_FILE *otf;

  FcPattern *pattern;
  FcFontSet *candidates;
  FcChar8   *fontname = NULL;
  FcResult   result;
  int i;

  if ((font[0] == '/') || (font[0] == '.'))
  {
    candidates = NULL;
    fontname = (FcChar8 *)strdup(font);
  }
  else
  {
    FcInit();
    pattern = FcNameParse ((const FcChar8 *)font);
    FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
                      // guide fc, in case substitution becomes necessary
    FcConfigSubstitute (0, pattern, FcMatchPattern);
    FcDefaultSubstitute (pattern);

    /* Receive a sorted list of fonts matching our pattern */
    candidates = FcFontSort (0, pattern, FcFalse, 0, &result);
    FcPatternDestroy (pattern);

    if (candidates)
    {
      /* In the list of fonts returned by FcFontSort()
	 find the first one that is both in TrueType format and monospaced */
      for (i = 0; i < candidates->nfont; i ++)
      {
	FcChar8 *fontformat = NULL; // TODO? or just try?
	int spacing = 0; // sane default, as FC_MONO == 100
	FcPatternGetString(candidates->fonts[i], FC_FONTFORMAT, 0, &fontformat);
	FcPatternGetInteger(candidates->fonts[i], FC_SPACING, 0, &spacing);

	if ((fontformat) && ((spacing == FC_MONO) || (fontwidth == 2)))
	{
	  // check for monospace or double width fonts
	  if (strcmp((const char *)fontformat, "TrueType") == 0)
	  {
	    fontname =
	      FcPatternFormat(candidates->fonts[i],
			      (const FcChar8 *)"%{file|cescape}/%{index}");
	    break;
	  }
	  else if (strcmp((const char *)fontformat, "CFF") == 0)
	  {
	    fontname =
	      FcPatternFormat (candidates->fonts[i],
			       (const FcChar8 *)"%{file|cescape}");
	                          // TTC only possible with non-cff glyphs!
	    break;
	  }
	}
      }
      FcFontSetDestroy (candidates);
    }
  }

  if (!fontname)
  {
    // TODO: try /usr/share/fonts/*/*/%s.ttf
    if(log) log(ld, CF_LOGLEVEL_ERROR,"cfFilterTextToPDF: No viable font found.");
    return NULL;
  }

  otf = otf_load((const char *)fontname);
  free(fontname);
  if (!otf)
  {
    return NULL;
  }

  FONTFILE *ff = fontfile_open_sfnt(otf);
  assert(ff);
  EMB_PARAMS *emb = emb_new(ff,
			    EMB_DEST_PDF16,
			    EMB_C_FORCE_MULTIBYTE|
			    EMB_C_TAKE_FONTFILE);
  assert(emb);
  assert(emb->plan&EMB_A_MULTIBYTE);
  return emb;
}

static EMB_PARAMS *font_std(const char *name)
{
  FONTFILE *ff = fontfile_open_std(name);
  assert(ff);
  EMB_PARAMS *emb = emb_new(ff,
			    EMB_DEST_PDF16,
			    EMB_C_TAKE_FONTFILE);
  assert(emb);
  return emb;
}


/*
 * 'compare_keywords()' - Compare two C/C++ keywords.
 */

static int				/* O - Result of strcmp */
compare_keywords(const void *k1,	/* I - First keyword */
                 const void *k2)	/* I - Second keyword */
{
  return (strcmp(*((const char **)k1), *((const char **)k2)));
}


/*
 * 'get_utf8()' - Get a UTF-8 encoded wide character...
 */

static int		/* O - Character or -1 on error */
get_utf8(FILE *fp)	/* I - File to read from */
{
  int	ch;		/* Current character value */
  int	next;		/* Next character from file */


 /*
  * Read the first character and process things accordingly...
  *
  * UTF-8 maps 16-bit characters to:
  *
  *        0 to 127 = 0xxxxxxx
  *     128 to 2047 = 110xxxxx 10yyyyyy (xxxxxyyyyyy)
  *   2048 to 65535 = 1110xxxx 10yyyyyy 10zzzzzz (xxxxyyyyyyzzzzzz)
  *
  * We also accept:
  *
  *      128 to 191 = 10xxxxxx
  *
  * since this range of values is otherwise undefined unless you are
  * in the middle of a multi-byte character...
  *
  * This code currently does not support anything beyond 16-bit
  * characters, in part because PostScript doesn't support more than
  * 16-bit characters...
  */

  if ((ch = getc(fp)) == EOF)
    return (EOF);

  if (ch < 0xc0)			/* One byte character? */
    return (ch);
  else if ((ch & 0xe0) == 0xc0)
  {
   /*
    * Two byte character...
    */

    if ((next = getc(fp)) == EOF)
      return (EOF);
    else
      return (((ch & 0x1f) << 6) | (next & 0x3f));
  }
  else if ((ch & 0xf0) == 0xe0)
  {
   /*
    * Three byte character...
    */

    if ((next = getc(fp)) == EOF)
      return (EOF);

    ch = ((ch & 0x0f) << 6) | (next & 0x3f);

    if ((next = getc(fp)) == EOF)
      return (EOF);
    else
      return ((ch << 6) | (next & 0x3f));
  }
  else
  {
   /*
    * More than three bytes...  We don't support that...
    */

    return (EOF);
  }
}


/*
 * 'write_epilogue()' - Write the PDF file epilogue.
 */

static void
write_epilogue(texttopdf_doc_t *doc)
{
  static char	*names[] =	/* Font names */
		{ "FN","FB","FI","FBI" };
  int i,j;

  // embed fonts
  for (i = doc->PrettyPrint ? 3 : 1; i >= 0; i --)
  {
    for (j = 0; j < doc->NumFonts; j ++) 
    {
      EMB_PARAMS *emb=doc->Fonts[j][i];
      if (emb->font->fobj) // already embedded
      {
        continue;
      }
      if ((!emb->subset) ||
	  (bits_used(emb->subset, emb->font->sfnt->numGlyphs)))
      {
        emb->font->fobj=cfPDFOutWriteFont(doc->pdf,emb);
        assert(emb->font->fobj);
      }
    }
  }

  /*
   * Create the global fontdict
   */

  // now fix FontResource
  doc->pdf->xref[doc->FontResource-1]=doc->pdf->filepos;
  cfPDFOutPrintF(doc->pdf,"%d 0 obj\n"
                    "<<\n",
                    doc->FontResource);

  for (i = doc->PrettyPrint ? 3 : 1; i >= 0; i --)
  {
    for (j = 0; j < doc->NumFonts; j ++)
    {
      EMB_PARAMS *emb=doc->Fonts[j][i];
      if (emb->font->fobj) // used
      {
        cfPDFOutPrintF(doc->pdf,"  /%s%02x %d 0 R\n",names[i],j,emb->font->fobj);
      }
    }
  }

  cfPDFOutPrintF(doc->pdf,">>\n"
                    "endobj\n");

  cfPDFOutFinishPDF(doc->pdf);

  cfPDFOutFree(doc->pdf);
}

/*
 * {{{ 'write_page()' - Write a page of text.
 */

static void
write_page(texttopdf_doc_t *doc)
{
  int	line;			/* Current line */

  int content=cfPDFOutAddXRef(doc->pdf);
  cfPDFOutPrintF(doc->pdf,"%d 0 obj\n"
		"<</Length %d 0 R\n"
		">>\n"
		"stream\n"
		"q\n",
		content, content+1);
  long size=-((doc->pdf->filepos)-2);

  (doc->NumPages)++;
  if (doc->PrettyPrint)
    write_pretty_header(doc->pdf);

  for (line = 0; line < doc->SizeLines; line ++)
    write_line(line, doc->Page[line], doc);

  size+= ((doc->pdf->filepos)+2);
  cfPDFOutPrintF(doc->pdf,"Q\n"
		"endstream\n"
		"endobj\n");
  
  int len_obj=cfPDFOutAddXRef(doc->pdf);
  assert(len_obj==content+1);
  cfPDFOutPrintF(doc->pdf,"%d 0 obj\n"
		"%ld\n"
		"endobj\n",
		len_obj,size);

  int obj=cfPDFOutAddXRef(doc->pdf);
  cfPDFOutPrintF(doc->pdf,"%d 0 obj\n"
		"<</Type/Page\n"
		"  /Parent 1 0 R\n"
		"  /MediaBox [0 0 %.0f %.0f]\n"
		"  /Contents %d 0 R\n"
		"  /Resources << /Font %d 0 R >>\n"
		">>\n"
		"endobj\n",
		obj,doc->PageWidth, doc->PageLength, content,
		doc->FontResource);
  cfPDFOutAddPage(doc->pdf,obj);

  memset(doc->Page[0], 0,
	 sizeof(lchar_t) * (doc->SizeColumns) * (doc->SizeLines));
}
// }}}

/* 
 * {{{'write_prolog()' - Write the PDF file prolog with options.
 */

static int
write_prolog(const char *title,		/* I - Title of job */
	    const char *user,		/* I - Username */
            const char *classification,	/* I - Classification */
	    const char *label,		/* I - Page label */
            ppd_file_t *ppd, 		/* I - PPD file info */
            texttopdf_doc_t *doc,
            cf_logfunc_t log,
            void *ld)
{
  int		i, j, k;	/* Looping vars */
  const char	*charset;	/* Character set string */
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
  int		num_fonts = 0;	/* Number of unique fonts */
  EMB_PARAMS	*fonts[1024];	/* Unique fonts */
  char		*fontnames[1024]; /* Unique fonts */
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

  datadir = doc->env_vars.data_dir;
 /*
  * Adjust margins as necessary...
  */

  if (classification || label)
  {
   /*
    * Leave room for labels...
    */

    doc->PageBottom += 36;
    doc->PageTop    -= 36;
  }

  if (doc->PageColumns > 1)
  {
    doc->ColumnGutter = doc->CharsPerInch / 2;
    doc->ColumnWidth  = (doc->SizeColumns - doc->ColumnGutter *
			 (doc->PageColumns - 1)) /
                        doc->PageColumns;
  }
  else
    doc->ColumnWidth = doc->SizeColumns;

 /*
  * {{{ Output the PDF header...
  */

  assert(!(doc->pdf));
  doc->pdf = cfPDFOutNew();
  assert(doc->pdf);

  cfPDFOutBeginPDF(doc->pdf);
  cfPDFOutPrintF(doc->pdf,"%%cupsRotation: %d\n",
		(doc->Orientation & 3) * 90); // TODO?

  cfPDFOutAddKeyValue(doc->pdf,"Creator","texttopdf/" PACKAGE_VERSION);

  curtime = time(NULL);
  curtm   = localtime(&curtime);
  strftime(curdate, sizeof(curdate), "%c", curtm);

  cfPDFOutAddKeyValue(doc->pdf,"CreationDate",cfPDFOutToPDFDate(curtm));
  cfPDFOutAddKeyValue(doc->pdf,"Title",title);
  cfPDFOutAddKeyValue(doc->pdf,"Author",user); // was(PostScript): /For
  // }}}

 /*
  * {{{ Initialize globals...
  */

  doc->NumFonts = 0;
  memset(doc->Fonts, 0, sizeof(doc->Fonts));
  memset(doc->Chars, 0, sizeof(doc->Chars));
  memset(doc->Codes, 0, sizeof(doc->Codes));
  // }}}

 /*
  * Get the output character set...
  */

  charset = doc->env_vars.char_set;
  if (charset != NULL && strcmp(charset, "us-ascii") != 0) // {{{
  {
    snprintf(filename, sizeof(filename), "%s/charsets/pdf.%s", datadir,
	     charset);

    if ((fp = fopen(filename, "r")) == NULL)
    {
     /*
      * Can't open charset file!
      */

      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Unable to open %s: %s",
		   filename, strerror(errno));
      return (1);
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Bad charset file %s", filename);
      return (1);
    }

    if (strncmp(line, "charset", 7) != 0)
    {
     /*
      * Bad format/not a charset file!
      */

      fclose(fp);
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Bad charset file %s", filename);
      return (1);
    }

   /*
    * See if this is an 8-bit or UTF-8 character set file...
    */

    line[strlen(line) - 1] = '\0'; /* Drop \n */
    for (lineptr = line + 7; isspace(*lineptr & 255); lineptr ++);
                                                   /* Skip whitespace */

#ifndef CUPS_1_4 /* CUPS 1.4.x or newer: support for non-utf8 removed */
    if (strcmp(lineptr, "8bit") == 0) // {{{
    {
     /*
      * 8-bit text...
      */

      doc->UTF8     = 0;
      doc->NumFonts = 0;

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

	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad font description line: %s", valptr);
	  return (1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "ltor") == 0)
	  doc->Directions[doc->NumFonts] = 1;
	else if (strcmp(valptr, "rtol") == 0)
	  doc->Directions[doc->NumFonts] = -1;
	else
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad text direction %s", valptr);
	  return (1);
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

	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad font description line: %s", valptr);
	  return (1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "single") == 0)
          doc->Widths[doc->NumFonts] = 1;
	else if (strcmp(valptr, "double") == 0)
          doc->Widths[doc->NumFonts] = 2;
	else 
	{
	  if(log) log(ld, CF_LOGLEVEL_ERROR,
		      "cfFilterTextToPDF: Bad text width %s", valptr);
	  return (1);
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

          if (lineptr > valptr)
	  {
            // search for duplicates
            for (k = 0; k < num_fonts; k ++)
              if (strcmp(valptr, fontnames[k]) == 0)
	      {
	        doc->Fonts[doc->NumFonts][i] = fonts[k];
                break;
              }

            if (k==num_fonts) // not found
	    {
	      fonts[num_fonts] = doc->Fonts[doc->NumFonts][i] =
		font_load(valptr, doc->Widths[doc->NumFonts], log, ld);
              if (!fonts[num_fonts]) // font missing/corrupt, replace by first
	      {
                if(log) log(ld, CF_LOGLEVEL_WARN,
			    "cfFilterTextToPDF: Ignored bad font \"%s\"",valptr);
                break;
              }
              fontnames[num_fonts++] = strdup(valptr);
            }
          }
	}

        /* ignore complete range, when the first font is not available */
        if (i==0)
	{
          continue;
        }

       /*
	* Fill in remaining fonts as needed...
	*/

	for (j = i; j < 4; j ++)
	  doc->Fonts[doc->NumFonts][j] = doc->Fonts[doc->NumFonts][0];

       /*
        * Define the character mappings...
	*/

	for (i = start; i <= end; i ++)
	  doc->Codes[i] = doc->NumFonts;

        (doc->NumFonts)++;
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
          doc->Chars[ch] = unicode;
      }
      while (fgets(line, sizeof(line), fp) != NULL);

      fclose(fp);
    } else // }}}
#endif
    if (strcmp(lineptr, "utf8") == 0) // {{{
    {
     /*
      * UTF-8 (Unicode) text...
      */

      doc->UTF8 = 1;

     /*
      * Read the font descriptions...
      */

      doc->NumFonts = 0;

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
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad font description line: %s", valptr);
	  return (1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "ltor") == 0)
	  doc->Directions[doc->NumFonts] = 1;
	else if (strcmp(valptr, "rtol") == 0)
	  doc->Directions[doc->NumFonts] = -1;
	else
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad text direction %s", valptr);
	  return (1);
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
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad font description line: %s", valptr);
	  return (1);
	}

	*lineptr++ = '\0';

	if (strcmp(valptr, "single") == 0)
          doc->Widths[doc->NumFonts] = 1;
	else if (strcmp(valptr, "double") == 0)
          doc->Widths[doc->NumFonts] = 2;
	else 
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterTextToPDF: Bad text width %s", valptr);
	  return (1);
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

          if (lineptr > valptr)
	  {
            // search for duplicates
            for (k = 0; k < num_fonts; k ++)
              if (strcmp(valptr, fontnames[k]) == 0)
	      {
	        doc->Fonts[doc->NumFonts][i] = fonts[k];
                break;
              }

            if (k==num_fonts) // not found
	    {
	      fonts[num_fonts] = doc->Fonts[doc->NumFonts][i] =
		font_load(valptr, doc->Widths[doc->NumFonts], log, ld);
              if (!fonts[num_fonts]) // font missing/corrupt, replace by first
	      {
                if(log) log(ld, CF_LOGLEVEL_WARN,
			    "cfFilterTextToPDF: Ignored bad font \"%s\"",valptr);
                break;
              }
              fontnames[num_fonts++] = strdup(valptr);
            }
          }
	}

        /* ignore complete range, when the first font is not available */
        if (i == 0)
	{
          continue;
        }

       /*
	* Fill in remaining fonts as needed...
	*/

	for (j = i; j < 4; j ++)
	  doc->Fonts[doc->NumFonts][j] = doc->Fonts[doc->NumFonts][0];

       /*
        * Define the character mappings...
	*/

	for (i = start; i <= end; i ++)
	{
          doc->Codes[i] = doc->NumFonts;
	}

       /*
        * Move to the next font, stopping if needed...
	*/

        doc->NumFonts ++;
	if (doc->NumFonts >= 256)
	  break;
      }

      fclose(fp);
    } // }}}
    else // {{{
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterTextToPDF: Bad charset type %s", lineptr);
      return (1);
    } // }}}
  } // }}}
  else // {{{ Standard ASCII
  {
   /*
    * Standard ASCII output just uses Courier, Courier-Bold, and
    * possibly Courier-Oblique.
    */

    doc->NumFonts = 1;

    doc->Fonts[0][ATTR_NORMAL]     = font_std("Courier");
    doc->Fonts[0][ATTR_BOLD]       = font_std("Courier-Bold");
    doc->Fonts[0][ATTR_ITALIC]     = font_std("Courier-Oblique");
    doc->Fonts[0][ATTR_BOLDITALIC] = font_std("Courier-BoldOblique");

    doc->Widths[0]     = 1;
    doc->Directions[0] = 1;

   /*
    * Define US-ASCII characters...
    */

    for (i = 32; i < 127; i ++)
    {
      doc->Chars[i] = i;
      doc->Codes[i] = doc->NumFonts-1;
    }
  }
  // }}}

  if (doc->NumFonts == 0) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterTextToPDF:No usable font available");
    return (1);
  }

  doc->FontScaleX=120.0 / (doc->CharsPerInch);
  doc->FontScaleY=68.0 / (doc->LinesPerInch);

  // allocate now, for pages to use. will be fixed in epilogue
  doc->FontResource=cfPDFOutAddXRef(doc->pdf);

  if (doc->PrettyPrint)
  {
    doc->Date=make_wide(curdate, doc);
    doc->Title=make_wide(title, doc);
  }

  return (0);
}
// }}}

/*
 * {{{ 'write_line()' - Write a row of text.
 */

static void
write_line(int     row,		/* I - Row number (0 to N) */
           lchar_t *line,
           texttopdf_doc_t *doc)	/* I - Line to print */
{
  int		i;		/* Looping var */
  int		col,xcol,xwid;		/* Current column */
  int		attr;		/* Current attribute */
  int		font,		/* Font to use */
		lastfont,	/* Last font */
		mono;		/* Monospaced? */
  lchar_t	*start;		/* First character in sequence */


  xcol=0;
  for (col = 0, start = line; col < doc->SizeColumns;)
  {
    while (col < doc->SizeColumns && (line->ch == ' ' || line->ch == 0))
    {
      col ++;
      xcol ++;
      line ++;
    }

    if (col >= doc->SizeColumns)
      break;

    if (doc->NumFonts == 1)
    {
     /*
      * All characters in a single font - assume monospaced and single width...
      */

      attr  = line->attr;
      start = line;

      while (col < doc->SizeColumns && line->ch != 0 && attr == line->attr)
      {
	col ++;
	line ++;
      }

      write_string(col - (line - start), row, line - start, start, doc);
    }
    else
    {
     /*
      * Multiple fonts; break up based on the font...
      */

      attr     = line->attr;
      start    = line;
      xwid     = 0;
      if (doc->UTF8) {
        lastfont = doc->Codes[line->ch];
      } else {
        lastfont = doc->Codes[doc->Chars[line->ch]];
      }
      //mono = strncmp(Fonts[lastfont][0], "Courier", 7) == 0;
      mono = 1; // TODO

      col ++;
      xwid += (doc->Widths[lastfont]);
      line ++;

      if (mono)
      {
	while (col < doc->SizeColumns && line->ch != 0 && attr == line->attr)
	{
          if (doc->UTF8)
	  {
            font = doc->Codes[line->ch];
          }
	  else
	  {
            font = doc->Codes[doc->Chars[line->ch]];
          }
          if (/*strncmp(Fonts[font][0], "Courier", 7) != 0 ||*/ // TODO
	      font != lastfont)
	    break;

	  col ++;
          xwid += (doc->Widths[lastfont]);
	  line ++;
	}
      }

      if (doc->Directions[lastfont] > 0)
      {
        write_string(xcol, row, line - start, start,doc);
        xcol += xwid;
      }
      else
      {
       /*
        * Do right-to-left text... ; assume no font change without direction
        * change
	*/

	while (col < doc->SizeColumns && line->ch != 0 && attr == line->attr)
	{
          if (doc->UTF8)
	  {
            font = doc->Codes[line->ch];
          }
	  else
	  {
            font = doc->Codes[doc->Chars[line->ch]];
          }
          if (doc->Directions[font] > 0 &&
	      !ispunct(line->ch & 255) && !isspace(line->ch & 255))
	    break;

	  col ++;
          xwid += doc->Widths[lastfont];
	  line ++;
	}

        for (i = 1; start < line; i ++, start ++)
	  if (!isspace(start->ch & 255))
	  {
            xwid-=(doc->Widths[lastfont]);
	    write_string(xcol + xwid, row, 1, start, doc);
          }
	  else
	  {
            xwid--;
          }
      }
    }
  }
}
// }}}

static lchar_t *make_wide(const char *buf, texttopdf_doc_t *doc)
                                               // {{{ - convert to lchar_t
{
  const unsigned char	*utf8;	/* UTF8 text */
  lchar_t *ret,*out;
  
  // this is enough, utf8 chars will only require less space
  out=ret=malloc((strlen(buf)+1)*sizeof(lchar_t)); 

  utf8 = (const unsigned char *)buf;
  while (*utf8)
  {
    out->attr=0;

    if (*utf8 < 0xc0 || !(doc->UTF8))
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
             lchar_t *s,	/* I - String to print */
             texttopdf_doc_t *doc)
{
  float		x, y;		/* Position of text */
  unsigned	attr;		/* Character attributes */


 /*
  * Position the text and set the font...
  */

  if (doc->Duplex && (doc->NumPages & 1) == 0)
  {
    x = doc->PageWidth - doc->PageRight;
    y = doc->PageTop;
  }
  else
  {
    x = doc->PageLeft;
    y = doc->PageTop;
  }

  x += (float)col * 72.0f / (float)(doc->CharsPerInch);
  y -= (float)(row + 0.843) * 72.0f / (float)(doc->LinesPerInch);

  attr = s->attr;

  if (attr & ATTR_RAISED)
    y += 36.0 / (float)(doc->LinesPerInch);
  else if (attr & ATTR_LOWERED)
    y -= 36.0 / (float)(doc->LinesPerInch);

  if (attr & ATTR_UNDERLINE)
    cfPDFOutPrintF(doc->pdf,"q 0.5 w 0 g %.3f %.3f m %.3f %.3f l S Q ",
                      x, y - 6.8 / (doc->LinesPerInch),
                      x + (float)len * 72.0 / (float)(doc->CharsPerInch),
                      y - 6.8 / (doc->LinesPerInch));

  if (doc->PrettyPrint)
  {
    if (doc->ColorDevice) {
      if (attr & ATTR_RED)
        cfPDFOutPrintF(doc->pdf,"0.5 0 0 rg\n");
      else if (attr & ATTR_GREEN)
        cfPDFOutPrintF(doc->pdf,"0 0.5 0 rg\n");
      else if (attr & ATTR_BLUE)
        cfPDFOutPrintF(doc->pdf,"0 0 0.5 rg\n");
      else
        cfPDFOutPrintF(doc->pdf,"0 g\n");
    } else {
      if ( (attr & ATTR_RED)||(attr & ATTR_GREEN)||(attr & ATTR_BLUE) )
        cfPDFOutPrintF(doc->pdf,"0.2 g\n");
      else
        cfPDFOutPrintF(doc->pdf,"0 g\n");
    }
  }
  else
    cfPDFOutPrintF(doc->pdf,"0 g\n");
  
  write_font_str(x,y,attr & ATTR_FONT,s,len, doc);
}
// }}}

// {{{ show >len characters from >str, using the right font(s) at >x,>y
static void write_font_str(float x,float y,int fontid, lchar_t *str,
			   int len, texttopdf_doc_t *doc)
{
  unsigned short	ch;		/* Current character */
  static char		*names[] =	/* Font names */
				{ "FN","FB","FI","FBI" };

  if (len==-1) {
    for (len=0;str[len].ch;len++);
  }
  cfPDFOutPrintF(doc->pdf,"BT\n");

  if (x == (int)x)
    cfPDFOutPrintF(doc->pdf,"  %.0f ", x);
  else
    cfPDFOutPrintF(doc->pdf,"  %.3f ", x);

  if (y == (int)y)
    cfPDFOutPrintF(doc->pdf,"%.0f Td\n", y);
  else
    cfPDFOutPrintF(doc->pdf,"%.3f Td\n", y);

  int lastfont,font;

  // split on font boundary
  while (len > 0) 
  {
   /*
    * Write a hex string...
    */
    if (doc->UTF8)
    {
      lastfont=doc->Codes[str->ch];
    }
    else
    {
      lastfont=doc->Codes[doc->Chars[str->ch]];
    }
    EMB_PARAMS *emb=doc->Fonts[lastfont][fontid];
    OTF_FILE *otf=emb->font->sfnt;

    if (otf) // TODO?
    {
      cfPDFOutPrintF(doc->pdf,"  %.3f Tz\n",
		    doc->FontScaleX * 600.0 /
		    (otf_get_width(otf, 4) * 1000.0 /
		     otf->unitsPerEm) * 100.0 / (doc->FontScaleY)); // TODO? 
      // gid==4 is usually '!', the char after space. We just need "the"
      // width for the monospaced font. gid==0 is bad, and space might also
      // be bad.
    }
    else
    {
      cfPDFOutPrintF(doc->pdf,"  %.3f Tz\n",
		    doc->FontScaleX*100.0/(doc->FontScaleY)); // TODO?
    }

    cfPDFOutPrintF(doc->pdf,"  /%s%02x %.3f Tf <",
		  names[fontid],lastfont,doc->FontScaleY);

    while (len > 0)
    {
      if (doc->UTF8)
      {
        ch = str->ch;
      }
      else
      {
        ch = doc->Chars[str->ch];
      }

      font = doc->Codes[ch];
      if (lastfont != font) // only possible, when not used via
	                    // write_string (e.g. utf-8filename.txt in
	                    // prettyprint)
      {
        break;
      }
      if (otf) // TODO
      {
        const unsigned short gid=emb_get(emb,ch);
        cfPDFOutPrintF(doc->pdf, "%04x", gid);
      }
      else // std 14 font with 7-bit us-ascii uses single byte encoding, TODO
      {
        cfPDFOutPrintF(doc->pdf, "%02x", ch);
      }

      len --;
      str ++;
    }

    cfPDFOutPrintF(doc->pdf,"> Tj\n");
  }
  cfPDFOutPrintF(doc->pdf,"ET\n");
}
// }}}

static float string_width_x(lchar_t *str, texttopdf_doc_t * doc)
{
  int len;

  for (len=0;str[len].ch;len++);

  return  (float)len * 72.0 / (float)(doc->CharsPerInch);
}

static void write_pretty_header(texttopdf_doc_t *doc) // {{{
{
  float x,y;
  cfPDFOutPrintF(doc->pdf,"q\n"
		"0.9 g\n");

  if (doc->Duplex && (doc->NumPages & 1) == 0)
  {
    x = doc->PageWidth - doc->PageRight;
    y = doc->PageTop + 72.0f / (doc->LinesPerInch);
  }
  else
  {
    x = doc->PageLeft;
    y = doc->PageTop + 72.0f / (doc->LinesPerInch);
  }

  cfPDFOutPrintF(doc->pdf,"1 0 0 1 %.3f %.3f cm\n",x,y); // translate
  cfPDFOutPrintF(doc->pdf,"0 0 %.3f %.3f re f\n",
		doc->PageRight - doc->PageLeft, 144.0f / (doc->LinesPerInch));
  cfPDFOutPrintF(doc->pdf,"0 g 0 G\n");

  if (doc->Duplex && (doc->NumPages & 1) == 0)
  {
    x = doc->PageRight - doc->PageLeft - 36.0f /
        doc->LinesPerInch - string_width_x(doc->Title, doc);
    y = (0.5f + 0.157f) * 72.0f / doc->LinesPerInch;
  }
  else
  {
    x = 36.0f / doc->LinesPerInch;
    y = (0.5f + 0.157f) * 72.0f / doc->LinesPerInch;
  }
  write_font_str(x, y, ATTR_BOLD, doc->Title, -1, doc);

  x = (-string_width_x(doc->Date, doc) + doc->PageRight - doc->PageLeft) * 0.5;
  write_font_str(x, y, ATTR_BOLD, doc->Date, -1, doc);

  // convert pagenumber to string
  char tmp[20];
  tmp[19]=0;
  snprintf(tmp,19,"%d",doc->NumPages);
  lchar_t *pagestr=make_wide(tmp, doc);

  if (doc->Duplex && (doc->NumPages & 1) == 0)
  {
    x = 36.0f / doc->LinesPerInch;
  }
  else
  {
    x = doc->PageRight - doc->PageLeft -
        36.0f / doc->LinesPerInch - string_width_x(pagestr, doc);
  }
  write_font_str(x, y, ATTR_BOLD, pagestr, -1, doc);
  free(pagestr);

  cfPDFOutPrintF(doc->pdf, "Q\n");
}
// }}}

/*
 *   Common text filter routines for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   TextMain()         - Standard main entry for text filters.
 *   compare_keywords() - Compare two C/C++ keywords.
 *   getutf8()          - Get a UTF-8 encoded wide character...
 *   SetCommonOptions() - Set common filter options for media size,
 *                        etc.
 *   UpdatePageVars()   - Update the page variables for the orientation.
 */

/*
 * Include necessary headers...
 */

#include "textcommon.h"
#include <limits.h>


/*
 * Globals...
 */

int	WrapLines = 1,		/* Wrap text in lines */
	SizeLines = 60,		/* Number of lines on a page */
	SizeColumns = 80,	/* Number of columns on a line */
	PageColumns = 1,	/* Number of columns on a page */
	ColumnGutter = 0,	/* Number of characters between text columns */
	ColumnWidth = 80,	/* Width of each column */
	PrettyPrint = 0,	/* Do pretty code formatting */
	Copies = 1;		/* Number of copies */
lchar_t	**Page = NULL;		/* Page characters */
int	NumPages = 0;		/* Number of pages in document */
float	CharsPerInch = 10;	/* Number of character columns per inch */
float	LinesPerInch = 6;	/* Number of lines per inch */
int	NumKeywords = 0;	/* Number of known keywords */
char	**Keywords = NULL;	/* List of known keywords */
int	Orientation = 0,	/* 0 = portrait, 1 = landscape, etc. */
	Duplex = 0,		/* Duplexed? */
	LanguageLevel = 1,	/* Language level of printer */
	ColorDevice = 1;	/* Do color text? */
float	PageLeft = 18.0f,	/* Left margin */
	PageRight = 594.0f,	/* Right margin */
	PageBottom = 36.0f,	/* Bottom margin */
	PageTop = 756.0f,	/* Top margin */
	PageWidth = 612.0f,	/* Total page width */
	PageLength = 792.0f;	/* Total page length */


/*
 * Local globals...
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
 * Local functions...
 */

static int	compare_keywords(const void *, const void *);
static int	getutf8(FILE *fp);
static ppd_file_t *SetCommonOptions(int num_options, cups_option_t *options,
		                    int change_size);
static void	UpdatePageVars(void);


/*
 * 'TextMain()' - Standard main entry for text filters.
 */

int				/* O - Exit status */
TextMain(const char *name,	/* I - Name of filter */
         int        argc,	/* I - Number of command-line arguments */
         char       *argv[])	/* I - Command-line arguments */
{
  FILE		*fp;		/* Print file */
  ppd_file_t	*ppd;		/* PPD file */
  cups_page_header2_t h;        /* CUPS Raster page header, to */
                                /* accommodate results of command */
                                /* line parsing for PPD-less queue */
  int		i,		/* Looping var */
		empty,		/* Is the input empty? */
		ch,		/* Current char from file */
		lastch,		/* Previous char from file */
		attr,		/* Current attribute */
		line,		/* Current line */
  		column,		/* Current column */
  		page_column;	/* Current page column */
  int		num_options;	/* Number of print options */
  cups_option_t	*options;	/* Print options */
  const char	*val;		/* Option value */
  char		keyword[64],	/* Keyword string */
		*keyptr;	/* Pointer into string */
  int		keycol;		/* Column where keyword starts */
  enum	{NLstyl=-1, NoCmnt, SNTXstyl}
  		cmntState;	/* Inside a comment */
  enum	{StrBeg=-1, NoStr, StrEnd}	
  		strState;	/* Inside a dbl-quoted string */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Check command-line...
  */

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n",
            name);
    return (1);
  }

 /*
  * If we have 7 arguments, print the file named on the command-line.
  * Otherwise, send stdin instead...
  */

  if (argc == 6)
    fp = stdin;
  else
  {
   /*
    * Try to open the print file...
    */

    if ((fp = fopen(argv[6], "rb")) == NULL)
    {
      perror("DEBUG: unable to open print file - ");
      return (1);
    }
  }

 /*
  * Process command-line options and write the prolog...
  */

  options     = NULL;
  num_options = cupsParseOptions(argv[5], 0, &options);

  if ((val = cupsGetOption("prettyprint", num_options, options)) != NULL &&
      strcasecmp(val, "no") && strcasecmp(val, "off") &&
      strcasecmp(val, "false"))
  {
    PageLeft     = 72.0f;
    PageRight    = PageWidth - 36.0f;
    PageBottom   = PageBottom > 36.0f ? PageBottom : 36.0f;
    PageTop      = PageLength - 36.0f;
    CharsPerInch = 12;
    LinesPerInch = 8;

    if ((val = getenv("CONTENT_TYPE")) == NULL)
    {
      PrettyPrint = PRETTY_PLAIN;
      NumKeywords = 0;
      Keywords    = NULL;
    }
    else if (strcasecmp(val, "application/x-cshell") == 0)
    {
      PrettyPrint = PRETTY_SHELL;
      NumKeywords = sizeof(csh_keywords) / sizeof(csh_keywords[0]);
      Keywords    = csh_keywords;
    }
    else if (strcasecmp(val, "application/x-csource") == 0)
    {
      PrettyPrint = PRETTY_CODE;
      NumKeywords = sizeof(code_keywords) / sizeof(code_keywords[0]);
      Keywords    = code_keywords;
    }
    else if (strcasecmp(val, "application/x-perl") == 0)
    {
      PrettyPrint = PRETTY_PERL;
      NumKeywords = sizeof(perl_keywords) / sizeof(perl_keywords[0]);
      Keywords    = perl_keywords;
    }
    else if (strcasecmp(val, "application/x-shell") == 0)
    {
      PrettyPrint = PRETTY_SHELL;
      NumKeywords = sizeof(sh_keywords) / sizeof(sh_keywords[0]);
      Keywords    = sh_keywords;
    }
    else
    {
      PrettyPrint = PRETTY_PLAIN;
      NumKeywords = 0;
      Keywords    = NULL;
    }
  }

  ppd = SetCommonOptions(num_options, options, 1);

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
    Copies = h.NumCopies;
  }

  if ((val = cupsGetOption("wrap", num_options, options)) == NULL)
    WrapLines = 1;
  else
    WrapLines = !strcasecmp(val, "true") || !strcasecmp(val, "on") ||
                !strcasecmp(val, "yes");

  if ((val = cupsGetOption("columns", num_options, options)) != NULL)
  {
    PageColumns = atoi(val);

    if (PageColumns < 1)
    {
      if (fp != stdin)
        fclose(fp);
      fprintf(stderr, "ERROR: Bad columns value %d.\n", PageColumns);
      return (1);
    }
  }

  if ((val = cupsGetOption("cpi", num_options, options)) != NULL)
  {
    CharsPerInch = atof(val);

    if (CharsPerInch <= 0.0)
    {
      if (fp != stdin)
        fclose(fp);
      fprintf(stderr, "ERROR: Bad cpi value %f.\n", CharsPerInch);
      return (1);
    }
  }

  if ((val = cupsGetOption("lpi", num_options, options)) != NULL)
  {
    LinesPerInch = atof(val);

    if (LinesPerInch <= 0.0)
    {
      if (fp != stdin)
        fclose(fp);
      fprintf(stderr, "ERROR: Bad lpi value %f.", LinesPerInch);
      return (1);
    }
  }

  if (PrettyPrint)
    PageTop -= 216.0f / LinesPerInch;

 /*
  * Allocate memory for the page...
  */

  SizeColumns = (PageRight - PageLeft) / 72.0 * CharsPerInch;
  SizeLines   = (PageTop - PageBottom) / 72.0 * LinesPerInch;

 /*
  * Enforce minimum size...
  */
  if (SizeColumns < 1)
    SizeColumns = 1;
  if (SizeLines < 1)
    SizeLines = 1;

  if (SizeLines >= INT_MAX / SizeColumns / sizeof(lchar_t))
  {
    fprintf(stderr, "ERROR: bad page size\n");
    exit(1);
  }

  Page    = calloc(sizeof(lchar_t *), SizeLines);
  if (!Page)
  {
    fprintf(stderr, "ERROR: cannot allocate memory for page\n");
    exit(1);
  }

  Page[0] = calloc(sizeof(lchar_t), SizeColumns * SizeLines);
  if (!Page[0])
  {
    free(Page);
    fprintf(stderr, "ERROR: cannot allocate memory for page\n");
    exit(1);
  }

  for (i = 1; i < SizeLines; i ++)
    Page[i] = Page[0] + i * SizeColumns;

  Copies = atoi(argv[4]);

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

  while ((ch = getutf8(fp)) >= 0)
  {
    if (empty)
    {
      /* Found the first valid character, write file header */
      empty = 0;
      WriteProlog(argv[3], argv[2], getenv("CLASSIFICATION"),
		  cupsGetOption("page-label", num_options, options), ppd);
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
          if (PrettyPrint && keyptr > keyword)
	  {
	    *keyptr = '\0';
	    keyptr  = keyword;

	    if (bsearch(&keyptr, Keywords, NumKeywords, sizeof(char *),
	                compare_keywords))
            {
	     /*
	      * Put keywords in boldface...
	      */

	      i = page_column * (ColumnWidth + ColumnGutter);

	      while (keycol < column)
	      {
	        Page[line][keycol + i].attr |= ATTR_BOLD;
		keycol ++;
	      }
	    }
	  }

          column = (column + 8) & ~7;

          if (column >= ColumnWidth && WrapLines)
          {			/* Wrap text to margins */
            line ++;
            column = 0;

            if (line >= SizeLines)
            {
              page_column ++;
              line = 0;

              if (page_column >= PageColumns)
              {
                WritePage();
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
          if (PrettyPrint && keyptr > keyword)
	  {
	    *keyptr = '\0';
	    keyptr  = keyword;

	    if (bsearch(&keyptr, Keywords, NumKeywords, sizeof(char *),
	                compare_keywords))
            {
	     /*
	      * Put keywords in boldface...
	      */

	      i = page_column * (ColumnWidth + ColumnGutter);

	      while (keycol < column)
	      {
	        Page[line][keycol + i].attr |= ATTR_BOLD;
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
	    attr &= ~(ATTR_ITALIC | ATTR_BOLD | ATTR_RED | ATTR_GREEN | ATTR_BLUE);

          if (line >= SizeLines)
          {
            page_column ++;
            line = 0;

            if (page_column >= PageColumns)
            {
              WritePage();
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
	    attr &= ~(ATTR_ITALIC | ATTR_BOLD | ATTR_RED | ATTR_GREEN | ATTR_BLUE);
          break;

      case 0x0c :		/* FF - eject current page... */
          if (PrettyPrint && keyptr > keyword)
	  {
	    *keyptr = '\0';
	    keyptr  = keyword;

	    if (bsearch(&keyptr, Keywords, NumKeywords, sizeof(char *),
	                compare_keywords))
            {
	     /*
	      * Put keywords in boldface...
	      */

	      i = page_column * (ColumnWidth + ColumnGutter);

	      while (keycol < column)
	      {
	        Page[line][keycol + i].attr |= ATTR_BOLD;
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
	    attr &= ~(ATTR_ITALIC | ATTR_BOLD | ATTR_RED | ATTR_GREEN | ATTR_BLUE);

          if (page_column >= PageColumns)
          {
            WritePage();
            page_column = 0;
          }
          break;

      case 0x1b :		/* Escape sequence */
          ch = getutf8(fp);
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

            if ((attr & ATTR_LOWERED) && line < (SizeLines - 1))
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

          if (PrettyPrint > PRETTY_PLAIN)
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

	      if (bsearch(&keyptr, Keywords, NumKeywords, sizeof(char *),
	                  compare_keywords))
              {
	       /*
	        * Put keywords in boldface...
		*/

	        i = page_column * (ColumnWidth + ColumnGutter);

		while (keycol < column)
		{
	          Page[line][keycol + i].attr |= ATTR_BOLD;
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
	               PrettyPrint != PRETTY_SHELL)
	      {
	       /*
	        * Start a C-style comment...
	        */

	        cmntState = SNTXstyl;
	        attr     = ATTR_ITALIC | ATTR_GREEN;
	      }
	      else if (ch == '/' && lastch == '/' &&
	               PrettyPrint == PRETTY_CODE)
	      {
	       /*
	        * Start a C++-style comment...
	        */

	        cmntState = NLstyl;
	        attr = ATTR_ITALIC | ATTR_GREEN;
	      }
	      else if (ch == '#' && PrettyPrint != PRETTY_CODE)
	      {
	       /*
	        * Start a shell-style comment...
	        */

	        cmntState = NLstyl;
	        attr = ATTR_ITALIC | ATTR_GREEN;
	      }
	      else if (ch == '#' && column == 0 &&
	               PrettyPrint == PRETTY_CODE)
	      {
	       /*
	        * Start a preprocessor command...
	        */

	        attr = ATTR_BOLD | ATTR_RED;
	      }
	    }
          }

          if (column >= ColumnWidth && WrapLines)
          {			/* Wrap text to margins */
            column = 0;
	    line ++;

            if (line >= SizeLines)
            {
              page_column ++;
              line = 0;

              if (page_column >= PageColumns)
              {
        	WritePage();
        	page_column = 0;
              }
            }
          }

         /*
	  * Add text to the current column & line...
	  */

          if (column < ColumnWidth)
	  {
	    i = column + page_column * (ColumnWidth + ColumnGutter);

            if (PrettyPrint)
              Page[line][i].attr = attr;

	    if (ch == ' ' && Page[line][i].ch)
	      ch = Page[line][i].ch;
            else if (ch == Page[line][i].ch)
              Page[line][i].attr |= ATTR_BOLD;
            else if (Page[line][i].ch == '_')
              Page[line][i].attr |= ATTR_UNDERLINE;
            else if (ch == '_')
	    {
              Page[line][i].attr |= ATTR_UNDERLINE;

              if (Page[line][i].ch)
	        ch = Page[line][i].ch;
	    }
	    else
              Page[line][i].attr = attr;

            Page[line][i].ch = ch;
	  }

          if (PrettyPrint)
	  {
	    if ((ch == '{' || ch == '}') && !cmntState && !strState &&
	        column < ColumnWidth)
	    {
	     /*
	      * Highlight curley braces...
	      */

	      Page[line][column].attr |= ATTR_BOLD;
	    }
	    else if ((ch == '/' || ch == '*') && lastch == '/' &&
	             column < ColumnWidth && PrettyPrint != PRETTY_SHELL)
	    {
	     /*
	      * Highlight first comment character...
	      */

	      Page[line][column - 1].attr = attr;
	    }
	    else if (ch == '\"' && lastch != '\\' && !cmntState && strState == StrEnd)
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
    fprintf(stderr, "DEBUG: Input is empty, outputting empty file.\n");
    if (fp != stdin)
      fclose(fp);
    return 0;
  }

 /*
  * Write any remaining page data...
  */

  if (line > 0 || page_column > 0 || column > 0)
    WritePage();

 /*
  * Write the epilog and return...
  */

  WriteEpilogue();

  if (fp != stdin)
    fclose(fp);

  if (ppd != NULL)
    ppdClose(ppd);

  free(Page[0]);
  free(Page);
  return (0);
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
 * 'getutf8()' - Get a UTF-8 encoded wide character...
 */

static int		/* O - Character or -1 on error */
getutf8(FILE *fp)	/* I - File to read from */
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
 * 'SetCommonOptions()' - Set common filter options for media size, etc.
 */

ppd_file_t *				/* O - PPD file */
SetCommonOptions(
    int           num_options,		/* I - Number of options */
    cups_option_t *options,		/* I - Options */
    int           change_size)		/* I - Change page size? */
{
  ppd_file_t	*ppd;			/* PPD file */
  ppd_size_t	*pagesize;		/* Current page size */
  const char	*val;			/* Option value */


#ifdef LC_TIME
  setlocale(LC_TIME, "");
#endif /* LC_TIME */

  ppd = ppdOpenFile(getenv("PPD"));

  ppdMarkDefaults(ppd);
  ppdMarkOptions(ppd, num_options, options);

  if ((pagesize = ppdPageSize(ppd, NULL)) != NULL)
  {
    int corrected = 0;
    if (pagesize->width > 0)
      PageWidth = pagesize->width;
    else
    {
      fprintf(stderr, "ERROR: Invalid value for page width: %.0f\n",
	      pagesize->width);
      corrected = 1;
    }
    if (pagesize->length > 0)
      PageLength = pagesize->length;
    else
    {
      fprintf(stderr, "ERROR: Invalid value for page length: %.0f\n",
	      pagesize->length);
      corrected = 1;
    }
    if (pagesize->top >= 0 && pagesize->top <= PageLength)
      PageTop = pagesize->top;
    else
    {
      fprintf(stderr, "ERROR: Invalid value for page top margin: %.0f\n",
	      pagesize->top);
      if (PageLength >= PageBottom)
	PageTop = PageLength - PageBottom;
      else
	PageTop = PageLength;
      corrected = 1;
    }
    if (pagesize->bottom >= 0 && pagesize->bottom <= PageLength)
      PageBottom = pagesize->bottom;
    else
    {
      fprintf(stderr, "ERROR: Invalid value for page bottom margin: %.0f\n",
	      pagesize->bottom);
      if (PageLength <= PageBottom)
	PageBottom = 0.0f;
      corrected = 1;
    }
    if (PageBottom == PageTop)
    {
      fprintf(stderr, "ERROR: Invalid values for page margins: Bottom: %.0f; Top: %.0f\n",
	      PageBottom, PageTop);
      PageTop = PageLength - PageBottom;
      if (PageBottom == PageTop)
      {
	PageBottom = 0.0f;
	PageTop = PageLength;
      }
      corrected = 1;
    }
    if (PageBottom > PageTop)
    {
      fprintf(stderr, "ERROR: Invalid values for page margins: Bottom: %.0f; Top: %.0f\n",
	      PageBottom, PageTop);
      float swap = PageBottom;
      PageBottom = PageTop;
      PageTop = swap;
      corrected = 1;
    }

    if (pagesize->left >= 0 && pagesize->left <= PageWidth)
      PageLeft = pagesize->left;
    else
    {
      fprintf(stderr, "ERROR: Invalid value for page left margin: %.0f\n",
	      pagesize->left);
      if (PageWidth <= PageLeft)
	PageLeft = 0.0f;
      corrected = 1;
    }
    if (pagesize->right >= 0 && pagesize->right <= PageWidth)
      PageRight = pagesize->right;
    else
    {
      fprintf(stderr, "ERROR: Invalid value for page right margin: %.0f\n",
	      pagesize->right);
      if (PageWidth >= PageLeft)
	PageRight = PageWidth - PageLeft;
      else
	PageRight = PageWidth;
      corrected = 1;
    }
    if (PageLeft == PageRight)
    {
      fprintf(stderr, "ERROR: Invalid values for page margins: Left: %.0f; Right: %.0f\n",
	      PageLeft, PageRight);
      PageRight = PageWidth - PageLeft;
      if (PageLeft == PageRight)
      {
	PageLeft = 0.0f;
	PageRight = PageWidth;
      }
      corrected = 1;
    }
    if (PageLeft > PageRight)
    {
      fprintf(stderr, "ERROR: Invalid values for page margins: Left: %.0f; Right: %.0f\n",
	      PageLeft, PageRight);
      float swap = PageLeft;
      PageLeft = PageRight;
      PageRight = swap;
      corrected = 1;
    }

    if (corrected)
    {
      fprintf(stderr, "ERROR: PPD Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f\n",
	      pagesize->width, pagesize->length, pagesize->left, pagesize->bottom, pagesize->right, pagesize->top);
      fprintf(stderr, "ERROR: Corrected Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f\n",
	      PageWidth, PageLength, PageLeft, PageBottom, PageRight, PageTop);
    }
    else
      fprintf(stderr, "DEBUG: Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f\n",
	      pagesize->width, pagesize->length, pagesize->left, pagesize->bottom, pagesize->right, pagesize->top);
  }

  if (ppd != NULL)
  {
    ColorDevice   = ppd->color_device;
    LanguageLevel = ppd->language_level;
  }

  if ((val = cupsGetOption("landscape", num_options, options)) != NULL)
  {
    if (strcasecmp(val, "no") != 0 && strcasecmp(val, "off") != 0 &&
        strcasecmp(val, "false") != 0)
    {
      if (ppd && ppd->landscape > 0)
        Orientation = 1;
      else
        Orientation = 3;
    }
  }
  else if ((val = cupsGetOption("orientation-requested", num_options, options)) != NULL)
  {
   /*
    * Map IPP orientation values to 0 to 3:
    *
    *   3 = 0 degrees   = 0
    *   4 = 90 degrees  = 1
    *   5 = -90 degrees = 3
    *   6 = 180 degrees = 2
    */

    Orientation = atoi(val) - 3;
    if (Orientation >= 2)
      Orientation ^= 1;
  }

  if ((val = cupsGetOption("page-left", num_options, options)) != NULL)
  {
    switch (Orientation & 3)
    {
      case 0 :
          PageLeft = (float)atof(val);
	  break;
      case 1 :
          PageBottom = (float)atof(val);
	  break;
      case 2 :
          PageRight = PageWidth - (float)atof(val);
	  break;
      case 3 :
          PageTop = PageLength - (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-right", num_options, options)) != NULL)
  {
    switch (Orientation & 3)
    {
      case 0 :
          PageRight = PageWidth - (float)atof(val);
	  break;
      case 1 :
          PageTop = PageLength - (float)atof(val);
	  break;
      case 2 :
          PageLeft = (float)atof(val);
	  break;
      case 3 :
          PageBottom = (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-bottom", num_options, options)) != NULL)
  {
    switch (Orientation & 3)
    {
      case 0 :
          PageBottom = (float)atof(val);
	  break;
      case 1 :
          PageLeft = (float)atof(val);
	  break;
      case 2 :
          PageTop = PageLength - (float)atof(val);
	  break;
      case 3 :
          PageRight = PageWidth - (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-top", num_options, options)) != NULL)
  {
    switch (Orientation & 3)
    {
      case 0 :
          PageTop = PageLength - (float)atof(val);
	  break;
      case 1 :
          PageRight = PageWidth - (float)atof(val);
	  break;
      case 2 :
          PageBottom = (float)atof(val);
	  break;
      case 3 :
          PageLeft = (float)atof(val);
	  break;
    }
  }

  if (change_size)
    UpdatePageVars();

  if (ppdIsMarked(ppd, "Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "Duplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexTumble"))
    Duplex = 1;

  return (ppd);
}


/*
 * 'UpdatePageVars()' - Update the page variables for the orientation.
 */

void
UpdatePageVars(void)
{
  float		temp;			/* Swapping variable */


  switch (Orientation & 3)
  {
    case 0 : /* Portait */
        break;

    case 1 : /* Landscape */
	temp       = PageLeft;
	PageLeft   = PageBottom;
	PageBottom = temp;

	temp       = PageRight;
	PageRight  = PageTop;
	PageTop    = temp;

	temp       = PageWidth;
	PageWidth  = PageLength;
	PageLength = temp;
	break;

    case 2 : /* Reverse Portrait */
	temp       = PageWidth - PageLeft;
	PageLeft   = PageWidth - PageRight;
	PageRight  = temp;

	temp       = PageLength - PageBottom;
	PageBottom = PageLength - PageTop;
	PageTop    = temp;
        break;

    case 3 : /* Reverse Landscape */
	temp       = PageWidth - PageLeft;
	PageLeft   = PageWidth - PageRight;
	PageRight  = temp;

	temp       = PageLength - PageBottom;
	PageBottom = PageLength - PageTop;
	PageTop    = temp;

	temp       = PageLeft;
	PageLeft   = PageBottom;
	PageBottom = temp;

	temp       = PageRight;
	PageRight  = PageTop;
	PageTop    = temp;

	temp       = PageWidth;
	PageWidth  = PageLength;
	PageLength = temp;
	break;
  }
}

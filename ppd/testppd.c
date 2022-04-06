/*
 * PPD test program for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "ppd.h"
#include "array-private.h"
#include "raster-private.h"
#include <sys/stat.h>
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif /* _WIN32 */
#include <math.h>


/*
 * Local functions...
 */

static int	do_ppd_tests(const char *filename, int num_options, cups_option_t *options);
static int	do_ps_tests(void);
static void	print_changes(cups_page_header2_t *header, cups_page_header2_t *expected);


/*
 * Test data...
 */

static const char *dsc_code =
"[{\n"
"%%BeginFeature: *PageSize Tabloid\n"
"<</PageSize[792 1224]>>setpagedevice\n"
"%%EndFeature\n"
"} stopped cleartomark\n";
static const char *setpagedevice_code =
"<<"
"/MediaClass(Media Class)"
"/MediaColor((Media Color))"
"/MediaType(Media\\\\Type)"
"/OutputType<416263>"
"/AdvanceDistance 1000"
"/AdvanceMedia 1"
"/Collate false"
"/CutMedia 2"
"/Duplex true"
"/HWResolution[100 200]"
"/InsertSheet true"
"/Jog 3"
"/LeadingEdge 1"
"/ManualFeed true"
"/MediaPosition 8#777"
"/MediaWeight 16#fe01"
"/MirrorPrint true"
"/NegativePrint true"
"/NumCopies 1"
"/Orientation 1"
"/OutputFaceUp true"
"/PageSize[612 792.1]"
"/Separations true"
"/TraySwitch true"
"/Tumble true"
"/cupsMediaType 2"
"/cupsColorOrder 1"
"/cupsColorSpace 1"
"/cupsCompression 1"
"/cupsRowCount 1"
"/cupsRowFeed 1"
"/cupsRowStep 1"
"/cupsBorderlessScalingFactor 1.001"
"/cupsInteger0 1"
"/cupsInteger1 2"
"/cupsInteger2 3"
"/cupsInteger3 4"
"/cupsInteger4 5"
"/cupsInteger5 6"
"/cupsInteger6 7"
"/cupsInteger7 8"
"/cupsInteger8 9"
"/cupsInteger9 10"
"/cupsInteger10 11"
"/cupsInteger11 12"
"/cupsInteger12 13"
"/cupsInteger13 14"
"/cupsInteger14 15"
"/cupsInteger15 16"
"/cupsReal0 1.1"
"/cupsReal1 2.1"
"/cupsReal2 3.1"
"/cupsReal3 4.1"
"/cupsReal4 5.1"
"/cupsReal5 6.1"
"/cupsReal6 7.1"
"/cupsReal7 8.1"
"/cupsReal8 9.1"
"/cupsReal9 10.1"
"/cupsReal10 11.1"
"/cupsReal11 12.1"
"/cupsReal12 13.1"
"/cupsReal13 14.1"
"/cupsReal14 15.1"
"/cupsReal15 16.1"
"/cupsString0(1)"
"/cupsString1(2)"
"/cupsString2(3)"
"/cupsString3(4)"
"/cupsString4(5)"
"/cupsString5(6)"
"/cupsString6(7)"
"/cupsString7(8)"
"/cupsString8(9)"
"/cupsString9(10)"
"/cupsString10(11)"
"/cupsString11(12)"
"/cupsString12(13)"
"/cupsString13(14)"
"/cupsString14(15)"
"/cupsString15(16)"
"/cupsMarkerType(Marker Type)"
"/cupsRenderingIntent(Rendering Intent)"
"/cupsPageSizeName(Letter)"
"/cupsPreferredBitsPerColor 17"
">> setpagedevice";

static cups_page_header2_t setpagedevice_header =
{
  "Media Class",			/* MediaClass */
  "(Media Color)",			/* MediaColor */
  "Media\\Type",			/* MediaType */
  "Abc",				/* OutputType */
  1000,					/* AdvanceDistance */
  CUPS_ADVANCE_FILE,			/* AdvanceMedia */
  CUPS_FALSE,				/* Collate */
  CUPS_CUT_JOB,				/* CutMedia */
  CUPS_TRUE,				/* Duplex */
  { 100, 200 },				/* HWResolution */
  { 0, 0, 0, 0 },			/* ImagingBoundingBox */
  CUPS_TRUE,				/* InsertSheet */
  CUPS_JOG_SET,				/* Jog */
  CUPS_EDGE_RIGHT,			/* LeadingEdge */
  { 0, 0 },				/* Margins */
  CUPS_TRUE,				/* ManualFeed */
  0777,					/* MediaPosition */
  0xfe01,				/* MediaWeight */
  CUPS_TRUE,				/* MirrorPrint */
  CUPS_TRUE,				/* NegativePrint */
  1,					/* NumCopies */
  CUPS_ORIENT_90,			/* Orientation */
  CUPS_TRUE,				/* OutputFaceUp */
  { 612, 792 },				/* PageSize */
  CUPS_TRUE,				/* Separations */
  CUPS_TRUE,				/* TraySwitch */
  CUPS_TRUE,				/* Tumble */
  0,					/* cupsWidth */
  0,					/* cupsHeight */
  2,					/* cupsMediaType */
  0,					/* cupsBitsPerColor */
  0,					/* cupsBitsPerPixel */
  0,					/* cupsBytesPerLine */
  CUPS_ORDER_BANDED,			/* cupsColorOrder */
  CUPS_CSPACE_RGB,			/* cupsColorSpace */
  1,					/* cupsCompression */
  1,					/* cupsRowCount */
  1,					/* cupsRowFeed */
  1,					/* cupsRowStep */
  0,					/* cupsNumColors */
  1.001f,				/* cupsBorderlessScalingFactor */
  { 612.0f, 792.1f },			/* cupsPageSize */
  { 0.0f, 0.0f, 0.0f, 0.0f },		/* cupsImagingBBox */
  { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
					/* cupsInteger[16] */
  { 1.1f, 2.1f, 3.1f, 4.1f, 5.1f, 6.1f, 7.1f, 8.1f, 9.1f, 10.1f, 11.1f, 12.1f, 13.1f, 14.1f, 15.1f, 16.1f },			/* cupsReal[16] */
  { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
    "14", "15", "16" },			/* cupsString[16] */
  "Marker Type",			/* cupsMarkerType */
  "Rendering Intent",			/* cupsRenderingIntent */
  "Letter"				/* cupsPageSizeName */
};

static const char	*default_code =
			"[{\n"
			"%%BeginFeature: *InstalledDuplexer False\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *PageRegion Letter\n"
			"PageRegion=Letter\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *InputSlot Tray\n"
			"InputSlot=Tray\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *OutputBin Tray1\n"
			"OutputBin=Tray1\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *MediaType Plain\n"
			"MediaType=Plain\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *IntOption None\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *StringOption None\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n";

static const char	*custom_code =
			"[{\n"
			"%%BeginFeature: *InstalledDuplexer False\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *InputSlot Tray\n"
			"InputSlot=Tray\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *MediaType Plain\n"
			"MediaType=Plain\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *OutputBin Tray1\n"
			"OutputBin=Tray1\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *IntOption None\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *CustomStringOption True\n"
			"(value\\0502\\051)\n"
			"(value 1)\n"
			"StringOption=Custom\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *CustomPageSize True\n"
			"400\n"
			"500\n"
			"0\n"
			"0\n"
			"0\n"
			"PageSize=Custom\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n";

static const char	*default2_code =
			"[{\n"
			"%%BeginFeature: *InstalledDuplexer False\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *InputSlot Tray\n"
			"InputSlot=Tray\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *Quality Normal\n"
			"Quality=Normal\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *IntOption None\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n"
			"[{\n"
			"%%BeginFeature: *StringOption None\n"
			"%%EndFeature\n"
			"} stopped cleartomark\n";


/*
 * '_ppdRasterColorSpaceString()' - Return the colorspace name for a
 *                                   cupsColorSpace value.
 */

const char *
_ppdRasterColorSpaceString(
    cups_cspace_t cspace)		/* I - cupsColorSpace value */
{
  static const char * const cups_color_spaces[] =
  {					/* Color spaces */
    "W",
    "RGB",
    "RGBA",
    "K",
    "CMY",
    "YMC",
    "CMYK",
    "YMCK",
    "KCMY",
    "KCMYcm",
    "GMCK",
    "GMCS",
    "WHITE",
    "GOLD",
    "SILVER",
    "CIEXYZ",
    "CIELab",
    "RGBW",
    "SW",
    "SRGB",
    "ADOBERGB",
    "21",
    "22",
    "23",
    "24",
    "25",
    "26",
    "27",
    "28",
    "29",
    "30",
    "31",
    "ICC1",
    "ICC2",
    "ICC3",
    "ICC4",
    "ICC5",
    "ICC6",
    "ICC7",
    "ICC8",
    "ICC9",
    "ICCA",
    "ICCB",
    "ICCC",
    "ICCD",
    "ICCE",
    "ICCF",
    "47",
    "DEVICE1",
    "DEVICE2",
    "DEVICE3",
    "DEVICE4",
    "DEVICE5",
    "DEVICE6",
    "DEVICE7",
    "DEVICE8",
    "DEVICE9",
    "DEVICEA",
    "DEVICEB",
    "DEVICEC",
    "DEVICED",
    "DEVICEE",
    "DEVICEF"
  };

  if (cspace < CUPS_CSPACE_W || cspace > CUPS_CSPACE_DEVICEF)
    return ("Unknown");
  else
    return (cups_color_spaces[cspace]);
}

/*
 * '_log()' - Simple log function
 */

void
_log(void *data,
     cf_loglevel_t level,
     const char *message,
     ...)
{
  va_list arglist;

  (void)data; /* No extra data needed */

  switch(level)
  {
    default:
      break;
    case CF_LOGLEVEL_ERROR:
      printf("ERROR: ");
      break;
    case CF_LOGLEVEL_FATAL:
      printf("FATAL: ");
      break;
  }
  va_start(arglist, message);
  vprintf(message, arglist);
  fflush(stdout);
  va_end(arglist);
  putchar('\n');
}


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  ppd_file_t	*ppd = NULL;		/* PPD file loaded from disk */
  int		status;			/* Status of tests (0 = success, 1 = fail) */
  int		conflicts;		/* Number of conflicts */
  char		*s;			/* String */
  char		buffer[8192];		/* String buffer */
  const char	*text,			/* Localized text */
		*val;			/* Option value */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  ppd_size_t	minsize,		/* Minimum size */
		maxsize,		/* Maximum size */
		*size;			/* Current size */
  ppd_attr_t	*attr;			/* Current attribute */
  ppd_cache_t	*pc;			/* PPD cache */


  status = 0;

  if (argc == 1)
  {
   /*
    * Setup directories for locale stuff...
    */

    if (access("locale", 0))
    {
      mkdir("locale", 0777);
      mkdir("locale/fr", 0777);
      i = symlink("../../../locale/cups_fr.po", "locale/fr/cups_fr.po");
      mkdir("locale/zh_TW", 0777);
      i = symlink("../../../locale/cups_zh_TW.po", "locale/zh_TW/cups_zh_TW.po");
    }

    putenv("LOCALEDIR=locale");
    putenv("SOFTWARE=CUPS");

   /*
    * Do tests with test.ppd...
    */

    fputs("ppdOpenFile(\"ppd/test.ppd\"): ", stdout);

    if ((ppd = ppdOpenFileWithLocalization("ppd/test.ppd", PPD_LOCALIZATION_ALL)) != NULL)
      puts("PASS");
    else
    {
      ppd_status_t	err;		/* Last error in file */
      int		line;		/* Line number in file */


      status ++;
      err = ppdLastError(&line);

      printf("FAIL (%s on line %d)\n", ppdErrorString(err), line);
    }

    fputs("ppdFindAttr(wildcard): ", stdout);
    if ((attr = ppdFindAttr(ppd, "cupsTest", NULL)) == NULL)
    {
      status ++;
      puts("FAIL (not found)");
    }
    else if (strcmp(attr->name, "cupsTest") || strcmp(attr->spec, "Foo"))
    {
      status ++;
      printf("FAIL (got \"%s %s\")\n", attr->name, attr->spec);
    }
    else
      puts("PASS");

    fputs("ppdFindNextAttr(wildcard): ", stdout);
    if ((attr = ppdFindNextAttr(ppd, "cupsTest", NULL)) == NULL)
    {
      status ++;
      puts("FAIL (not found)");
    }
    else if (strcmp(attr->name, "cupsTest") || strcmp(attr->spec, "Bar"))
    {
      status ++;
      printf("FAIL (got \"%s %s\")\n", attr->name, attr->spec);
    }
    else
      puts("PASS");

    fputs("ppdFindAttr(Foo): ", stdout);
    if ((attr = ppdFindAttr(ppd, "cupsTest", "Foo")) == NULL)
    {
      status ++;
      puts("FAIL (not found)");
    }
    else if (strcmp(attr->name, "cupsTest") || strcmp(attr->spec, "Foo"))
    {
      status ++;
      printf("FAIL (got \"%s %s\")\n", attr->name, attr->spec);
    }
    else
      puts("PASS");

    fputs("ppdFindNextAttr(Foo): ", stdout);
    if ((attr = ppdFindNextAttr(ppd, "cupsTest", "Foo")) != NULL)
    {
      status ++;
      printf("FAIL (got \"%s %s\")\n", attr->name, attr->spec);
    }
    else
      puts("PASS");

    fputs("ppdMarkDefaults: ", stdout);
    ppdMarkDefaults(ppd);

    if ((conflicts = ppdConflicts(ppd)) == 0)
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (%d conflicts)\n", conflicts);
    }

    fputs("ppdEmitString (defaults): ", stdout);
    if ((s = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0)) != NULL &&
	!strcmp(s, default_code))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (%d bytes instead of %d)\n", s ? (int)strlen(s) : 0,
	     (int)strlen(default_code));

      if (s)
	puts(s);
    }

    if (s)
      free(s);

    fputs("ppdEmitString (custom size and string): ", stdout);
    ppdMarkOption(ppd, "PageSize", "Custom.400x500");
    ppdMarkOption(ppd, "StringOption", "{String1=\"value 1\" String2=value(2)}");

    if ((s = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0)) != NULL &&
	!strcmp(s, custom_code))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (%d bytes instead of %d)\n", s ? (int)strlen(s) : 0,
	     (int)strlen(custom_code));

      if (s)
	puts(s);
    }

    if (s)
      free(s);

   /*
    * Test constraints...
    */

    fputs("ppdGetConflicts(InputSlot=Envelope): ", stdout);
    ppdMarkOption(ppd, "PageSize", "Letter");

    num_options = ppdGetConflicts(ppd, "InputSlot", "Envelope", &options);
    if (num_options != 2 ||
        (val = cupsGetOption("PageRegion", num_options, options)) == NULL ||
	_ppd_strcasecmp(val, "Letter") ||
	(val = cupsGetOption("PageSize", num_options, options)) == NULL ||
	_ppd_strcasecmp(val, "Letter"))
    {
      printf("FAIL (%d options:", num_options);
      for (i = 0; i < num_options; i ++)
        printf(" %s=%s", options[i].name, options[i].value);
      puts(")");
      status ++;
    }
    else
      puts("PASS");

    fputs("ppdConflicts(): ", stdout);
    ppdMarkOption(ppd, "InputSlot", "Envelope");

    if ((conflicts = ppdConflicts(ppd)) == 2)
      puts("PASS (2)");
    else
    {
      printf("FAIL (%d)\n", conflicts);
      status ++;
    }

    fputs("ppdResolveConflicts(InputSlot=Envelope): ", stdout);
    num_options = 0;
    options     = NULL;
    if (!ppdResolveConflicts(ppd, "InputSlot", "Envelope", &num_options,
                             &options))
    {
      puts("FAIL (Unable to resolve)");
      status ++;
    }
    else if (num_options != 2 ||
             !cupsGetOption("PageSize", num_options, options))
    {
      printf("FAIL (%d options:", num_options);
      for (i = 0; i < num_options; i ++)
        printf(" %s=%s", options[i].name, options[i].value);
      puts(")");
      status ++;
    }
    else
      puts("PASS (Resolved by changing PageSize)");

    cupsFreeOptions(num_options, options);

    fputs("ppdResolveConflicts(No option/choice): ", stdout);
    num_options = 0;
    options     = NULL;
    if (ppdResolveConflicts(ppd, NULL, NULL, &num_options, &options) &&
        num_options == 1 && !_ppd_strcasecmp(options[0].name, "InputSlot") &&
	!_ppd_strcasecmp(options[0].value, "Tray"))
      puts("PASS (Resolved by changing InputSlot)");
    else if (num_options > 0)
    {
      printf("FAIL (%d options:", num_options);
      for (i = 0; i < num_options; i ++)
        printf(" %s=%s", options[i].name, options[i].value);
      puts(")");
      status ++;
    }
    else
    {
      puts("FAIL (Unable to resolve)");
      status ++;
    }
    cupsFreeOptions(num_options, options);

    fputs("ppdInstallableConflict(): ", stdout);
    if (ppdInstallableConflict(ppd, "Duplex", "DuplexNoTumble") &&
        !ppdInstallableConflict(ppd, "Duplex", "None"))
      puts("PASS");
    else if (!ppdInstallableConflict(ppd, "Duplex", "DuplexNoTumble"))
    {
      puts("FAIL (Duplex=DuplexNoTumble did not conflict)");
      status ++;
    }
    else
    {
      puts("FAIL (Duplex=None conflicted)");
      status ++;
    }

   /*
    * ppdPageSizeLimits
    */

    fputs("ppdPageSizeLimits: ", stdout);
    if (ppdPageSizeLimits(ppd, &minsize, &maxsize))
    {
      if (fabs(minsize.width - 36.0) > 0.001 || fabs(minsize.length - 36.0) > 0.001 ||
          fabs(maxsize.width - 1080.0) > 0.001 || fabs(maxsize.length - 86400.0) > 0.001)
      {
        printf("FAIL (got min=%.3fx%.3f, max=%.3fx%.3f, "
	       "expected min=36x36, max=1080x86400)\n", minsize.width,
	       minsize.length, maxsize.width, maxsize.length);
        status ++;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (returned 0)");
      status ++;
    }

   /*
    * ppdMarkOptions with PWG and IPP size names.
    */

    fputs("ppdMarkOptions(media=iso-a4): ", stdout);
    num_options = cupsAddOption("media", "iso-a4", 0, &options);
    ppdMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);

    size = ppdPageSize(ppd, NULL);
    if (!size || strcmp(size->name, "A4"))
    {
      printf("FAIL (%s)\n", size ? size->name : "unknown");
      status ++;
    }
    else
      puts("PASS");

    fputs("ppdMarkOptions(media=na_letter_8.5x11in): ", stdout);
    num_options = cupsAddOption("media", "na_letter_8.5x11in", 0, &options);
    ppdMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);

    size = ppdPageSize(ppd, NULL);
    if (!size || strcmp(size->name, "Letter"))
    {
      printf("FAIL (%s)\n", size ? size->name : "unknown");
      status ++;
    }
    else
      puts("PASS");

    fputs("ppdMarkOptions(media=oe_letter-fullbleed_8.5x11in): ", stdout);
    num_options = cupsAddOption("media", "oe_letter-fullbleed_8.5x11in", 0,
                                &options);
    ppdMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);

    size = ppdPageSize(ppd, NULL);
    if (!size || strcmp(size->name, "Letter.Fullbleed"))
    {
      printf("FAIL (%s)\n", size ? size->name : "unknown");
      status ++;
    }
    else
      puts("PASS");

    fputs("ppdMarkOptions(media=A4): ", stdout);
    num_options = cupsAddOption("media", "A4", 0, &options);
    ppdMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);

    size = ppdPageSize(ppd, NULL);
    if (!size || strcmp(size->name, "A4"))
    {
      printf("FAIL (%s)\n", size ? size->name : "unknown");
      status ++;
    }
    else
      puts("PASS");

   /*
    * Custom sizes...
    */

    fputs("ppdMarkOptions(media=Custom.8x10in): ", stdout);
    num_options = cupsAddOption("media", "Custom.8x10in", 0, &options);
    ppdMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);

    size = ppdPageSize(ppd, NULL);
    if (!size || strcmp(size->name, "Custom") ||
        fabs(size->width - 576.0) > 0.001 ||
        fabs(size->length - 720.0) > 0.001)
    {
      printf("FAIL (%s - %gx%g)\n", size ? size->name : "unknown",
             size ? size->width : 0.0, size ? size->length : 0.0);
      status ++;
    }
    else
      puts("PASS");

   /*
    * Test localization...
    */

    fputs("ppdLocalizeIPPReason(text): ", stdout);
    if (ppdLocalizeIPPReason(ppd, "foo", NULL, buffer, sizeof(buffer)) &&
        !strcmp(buffer, "Foo Reason"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"Foo Reason\")\n", buffer);
    }

    fputs("ppdLocalizeIPPReason(http): ", stdout);
    if (ppdLocalizeIPPReason(ppd, "foo", "http", buffer, sizeof(buffer)) &&
        !strcmp(buffer, "http://foo/bar.html"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"http://foo/bar.html\")\n", buffer);
    }

    fputs("ppdLocalizeIPPReason(help): ", stdout);
    if (ppdLocalizeIPPReason(ppd, "foo", "help", buffer, sizeof(buffer)) &&
        !strcmp(buffer, "help:anchor='foo'%20bookID=Vendor%20Help"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"help:anchor='foo'%%20bookID=Vendor%%20Help\")\n", buffer);
    }

    fputs("ppdLocalizeIPPReason(file): ", stdout);
    if (ppdLocalizeIPPReason(ppd, "foo", "file", buffer, sizeof(buffer)) &&
        !strcmp(buffer, "/help/foo/bar.html"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"/help/foo/bar.html\")\n", buffer);
    }

    putenv("LANG=fr");
    putenv("LC_ALL=fr");
    putenv("LC_CTYPE=fr");
    putenv("LC_MESSAGES=fr");

    fputs("ppdLocalizeIPPReason(fr text): ", stdout);
    if (ppdLocalizeIPPReason(ppd, "foo", NULL, buffer, sizeof(buffer)) &&
        !strcmp(buffer, "La Long Foo Reason"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"La Long Foo Reason\")\n", buffer);
    }

    putenv("LANG=zh_TW");
    putenv("LC_ALL=zh_TW");
    putenv("LC_CTYPE=zh_TW");
    putenv("LC_MESSAGES=zh_TW");

    fputs("ppdLocalizeIPPReason(zh_TW text): ", stdout);
    if (ppdLocalizeIPPReason(ppd, "foo", NULL, buffer, sizeof(buffer)) &&
        !strcmp(buffer, "Number 1 Foo Reason"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"Number 1 Foo Reason\")\n", buffer);
    }

   /*
    * cupsMarkerName localization...
    */

    putenv("LANG=en");
    putenv("LC_ALL=en");
    putenv("LC_CTYPE=en");
    putenv("LC_MESSAGES=en");

    fputs("ppdLocalizeMarkerName(bogus): ", stdout);

    if ((text = ppdLocalizeMarkerName(ppd, "bogus")) != NULL)
    {
      status ++;
      printf("FAIL (\"%s\" instead of NULL)\n", text);
    }
    else
      puts("PASS");

    fputs("ppdLocalizeMarkerName(cyan): ", stdout);

    if ((text = ppdLocalizeMarkerName(ppd, "cyan")) != NULL &&
        !strcmp(text, "Cyan Toner"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"Cyan Toner\")\n",
             text ? text : "(null)");
    }

    putenv("LANG=fr");
    putenv("LC_ALL=fr");
    putenv("LC_CTYPE=fr");
    putenv("LC_MESSAGES=fr");

    fputs("ppdLocalizeMarkerName(fr cyan): ", stdout);
    if ((text = ppdLocalizeMarkerName(ppd, "cyan")) != NULL &&
        !strcmp(text, "La Toner Cyan"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"La Toner Cyan\")\n",
             text ? text : "(null)");
    }

    putenv("LANG=zh_TW");
    putenv("LC_ALL=zh_TW");
    putenv("LC_CTYPE=zh_TW");
    putenv("LC_MESSAGES=zh_TW");

    fputs("ppdLocalizeMarkerName(zh_TW cyan): ", stdout);
    if ((text = ppdLocalizeMarkerName(ppd, "cyan")) != NULL &&
        !strcmp(text, "Number 1 Cyan Toner"))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (\"%s\" instead of \"Number 1 Cyan Toner\")\n",
             text ? text : "(null)");
    }

    ppdClose(ppd);

   /*
    * Test new constraints...
    */

    fputs("ppdOpenFile(\"ppd/test2.ppd\"): ", stdout);

    if ((ppd = ppdOpenFile("ppd/test2.ppd")) != NULL)
      puts("PASS");
    else
    {
      ppd_status_t	err;		/* Last error in file */
      int		line;		/* Line number in file */


      status ++;
      err = ppdLastError(&line);

      printf("FAIL (%s on line %d)\n", ppdErrorString(err), line);
    }

    fputs("ppdMarkDefaults: ", stdout);
    ppdMarkDefaults(ppd);

    if ((conflicts = ppdConflicts(ppd)) == 0)
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (%d conflicts)\n", conflicts);
    }

    fputs("ppdEmitString (defaults): ", stdout);
    if ((s = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0)) != NULL &&
	!strcmp(s, default2_code))
      puts("PASS");
    else
    {
      status ++;
      printf("FAIL (%d bytes instead of %d)\n", s ? (int)strlen(s) : 0,
	     (int)strlen(default2_code));

      if (s)
	puts(s);
    }

    if (s)
      free(s);

    fputs("ppdConflicts(): ", stdout);
    ppdMarkOption(ppd, "PageSize", "Env10");
    ppdMarkOption(ppd, "InputSlot", "Envelope");
    ppdMarkOption(ppd, "Quality", "Photo");

    if ((conflicts = ppdConflicts(ppd)) == 1)
      puts("PASS (1)");
    else
    {
      printf("FAIL (%d)\n", conflicts);
      status ++;
    }

    fputs("ppdResolveConflicts(Quality=Photo): ", stdout);
    num_options = 0;
    options     = NULL;
    if (ppdResolveConflicts(ppd, "Quality", "Photo", &num_options,
                             &options))
    {
      printf("FAIL (%d options:", num_options);
      for (i = 0; i < num_options; i ++)
        printf(" %s=%s", options[i].name, options[i].value);
      puts(")");
      status ++;
    }
    else
      puts("PASS (Unable to resolve)");
    cupsFreeOptions(num_options, options);

    fputs("ppdResolveConflicts(No option/choice): ", stdout);
    num_options = 0;
    options     = NULL;
    if (ppdResolveConflicts(ppd, NULL, NULL, &num_options, &options) &&
        num_options == 1 && !_ppd_strcasecmp(options->name, "Quality") &&
	!_ppd_strcasecmp(options->value, "Normal"))
      puts("PASS");
    else if (num_options > 0)
    {
      printf("FAIL (%d options:", num_options);
      for (i = 0; i < num_options; i ++)
        printf(" %s=%s", options[i].name, options[i].value);
      puts(")");
      status ++;
    }
    else
    {
      puts("FAIL (Unable to resolve!)");
      status ++;
    }
    cupsFreeOptions(num_options, options);

    fputs("ppdResolveConflicts(loop test): ", stdout);
    ppdMarkOption(ppd, "PageSize", "A4");
    ppdMarkOption(ppd, "InputSlot", "Tray");
    ppdMarkOption(ppd, "Quality", "Photo");
    num_options = 0;
    options     = NULL;
    if (!ppdResolveConflicts(ppd, NULL, NULL, &num_options, &options))
      puts("PASS");
    else if (num_options > 0)
    {
      printf("FAIL (%d options:", num_options);
      for (i = 0; i < num_options; i ++)
        printf(" %s=%s", options[i].name, options[i].value);
      puts(")");
    }
    else
      puts("FAIL (No conflicts!)");

    fputs("ppdInstallableConflict(): ", stdout);
    if (ppdInstallableConflict(ppd, "Duplex", "DuplexNoTumble") &&
        !ppdInstallableConflict(ppd, "Duplex", "None"))
      puts("PASS");
    else if (!ppdInstallableConflict(ppd, "Duplex", "DuplexNoTumble"))
    {
      puts("FAIL (Duplex=DuplexNoTumble did not conflict)");
      status ++;
    }
    else
    {
      puts("FAIL (Duplex=None conflicted)");
      status ++;
    }

   /*
    * ppdPageSizeLimits
    */

    ppdMarkDefaults(ppd);

    fputs("ppdPageSizeLimits(default): ", stdout);
    if (ppdPageSizeLimits(ppd, &minsize, &maxsize))
    {
      if (fabs(minsize.width - 36.0) > 0.001 || fabs(minsize.length - 36.0) > 0.001 ||
          fabs(maxsize.width - 1080.0) > 0.001 || fabs(maxsize.length - 86400.0) > 0.001)
      {
        printf("FAIL (got min=%.0fx%.0f, max=%.0fx%.0f, "
	       "expected min=36x36, max=1080x86400)\n", minsize.width,
	       minsize.length, maxsize.width, maxsize.length);
        status ++;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (returned 0)");
      status ++;
    }

    ppdMarkOption(ppd, "InputSlot", "Manual");

    fputs("ppdPageSizeLimits(InputSlot=Manual): ", stdout);
    if (ppdPageSizeLimits(ppd, &minsize, &maxsize))
    {
      if (fabs(minsize.width - 100.0) > 0.001 || fabs(minsize.length - 100.0) > 0.001 ||
          fabs(maxsize.width - 1000.0) > 0.001 || fabs(maxsize.length - 1000.0) > 0.001)
      {
        printf("FAIL (got min=%.0fx%.0f, max=%.0fx%.0f, "
	       "expected min=100x100, max=1000x1000)\n", minsize.width,
	       minsize.length, maxsize.width, maxsize.length);
        status ++;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (returned 0)");
      status ++;
    }

    ppdMarkOption(ppd, "Quality", "Photo");

    fputs("ppdPageSizeLimits(Quality=Photo): ", stdout);
    if (ppdPageSizeLimits(ppd, &minsize, &maxsize))
    {
      if (fabs(minsize.width - 200.0) > 0.001 || fabs(minsize.length - 200.0) > 0.001 ||
          fabs(maxsize.width - 1000.0) > 0.001 || fabs(maxsize.length - 1000.0) > 0.001)
      {
        printf("FAIL (got min=%.0fx%.0f, max=%.0fx%.0f, "
	       "expected min=200x200, max=1000x1000)\n", minsize.width,
	       minsize.length, maxsize.width, maxsize.length);
        status ++;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (returned 0)");
      status ++;
    }

    ppdMarkOption(ppd, "InputSlot", "Tray");

    fputs("ppdPageSizeLimits(Quality=Photo): ", stdout);
    if (ppdPageSizeLimits(ppd, &minsize, &maxsize))
    {
      if (fabs(minsize.width - 300.0) > 0.001 || fabs(minsize.length - 300.0) > 0.001 ||
          fabs(maxsize.width - 1080.0) > 0.001 || fabs(maxsize.length - 86400.0) > 0.001)
      {
        printf("FAIL (got min=%.0fx%.0f, max=%.0fx%.0f, "
	       "expected min=300x300, max=1080x86400)\n", minsize.width,
	       minsize.length, maxsize.width, maxsize.length);
        status ++;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (returned 0)");
      status ++;
    }

    status += do_ps_tests();
  }
  else if (!strcmp(argv[1], "--raster"))
  {
    for (status = 0, num_options = 0, options = NULL, i = 1; i < argc; i ++)
    {
      if (argv[i][0] == '-')
      {
        if (argv[i][1] == 'o')
        {
          if (argv[i][2])
            num_options = cupsParseOptions(argv[i] + 2, num_options, &options);
          else
          {
            i ++;
            if (i < argc)
              num_options = cupsParseOptions(argv[i], num_options, &options);
            else
            {
              puts("Usage: testppd --raster [-o name=value ...] [filename.ppd ...]");
              return (1);
            }
          }
        }
        else
        {
	  puts("Usage: testppd --raster [-o name=value ...] [filename.ppd ...]");
	  return (1);
        }
      }
      else
	status += do_ppd_tests(argv[i], num_options, options);
    }

    cupsFreeOptions(num_options, options);
  }
  else if (!strcmp(argv[1], "dump") && argc == 3)
  {
    ppdCollectionDumpCache(argv[2], _log, NULL);
  }
  else if (strchr(argv[1], ',') ||
	   (s = strcasestr(argv[1], ".ppd")) == NULL ||
	   s[4] != '\0')
  {
    /* First argument is not FILE.ppd but DIR or DIR,DIR,... ->
       PPD directory list -> See whether we have valid PPD collection */

    cups_array_t *dirlist = _ppdArrayNewStrings(argv[1], ',');
    cups_array_t *ppd_collections = cupsArrayNew(NULL, NULL);
    ppd_collection_t *col;
    cups_option_t *options = NULL;
    int num_options = 0;
    int i;
    char *testname = NULL;

    for (s = (char *)cupsArrayFirst(dirlist), i = 1;
	 s;
	 s = (char *)cupsArrayNext(dirlist), i ++)
    {
      col = (ppd_collection_t *)calloc(1, sizeof(ppd_collection_t));
      col->path = s;
      printf("Searching directory %d, %s ...\n", i, col->path); 
      col->name = (char *)calloc(20, sizeof(char));
      snprintf(col->name, 19, "%d", i);
      cupsArrayAdd(ppd_collections, col);
    }

    for (i = 2; i < argc; i ++)
      if (strlen(argv[i]) > 0 && argv[i][0] != '=')
      {
	if ((s = strchr(argv[i], '=')) == NULL)
	  s = "";
	else
	{
	  *s = '\0';
	  s ++;
	}
	num_options = cupsAddOption(argv[i], s, num_options, &options);
      }

    cups_array_t *ppds =
      ppdCollectionListPPDs(ppd_collections, 0, num_options, options,
			    _log, NULL);

    if (ppds)
    {
      if (cupsGetOption("only-makes", num_options, options))
      {
	printf("Found %d manufacturers.\n\n", cupsArrayCount(ppds));
	for (s = (char *)cupsArrayFirst(ppds);
	     s;
	     s = (char *)cupsArrayNext(ppds))
	  puts(s);
      }
      else
      {
	ppd_info_t *ppd;
	printf("Found %d PPD files.\n\n", cupsArrayCount(ppds));
	puts("mtime,size,model_number,type,filename,name,languages0,products0,"
	     "psversions0,make,make_and_model,device_id,scheme");
	for (ppd = (ppd_info_t *)cupsArrayFirst(ppds);
	     ppd;
	     ppd = (ppd_info_t *)cupsArrayNext(ppds))
	{
	  printf("%d,%ld,%d,%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\","
		 "\"%s\",\"%s\"\n",
		 (int)ppd->record.mtime, (long)ppd->record.size,
		 ppd->record.model_number, ppd->record.type,
		 ppd->record.filename,
		 ppd->record.name, ppd->record.languages[0],
		 ppd->record.products[0],
		 ppd->record.psversions[0], ppd->record.make,
		 ppd->record.make_and_model, ppd->record.device_id,
		 ppd->record.scheme);
	  testname = ppd->record.name;
	}
      }
    }

    if (testname)
    {
      if ((ppd = ppdOpen2(ppdCollectionGetPPD(testname, ppd_collections,
					      _log, NULL))) == NULL)
      {
	ppd_status_t	err;		/* Last error in file */
	int		line;		/* Line number in file */

	status ++;
	err = ppdLastError(&line);

	printf("%s: %s on line %d\n", argv[1], ppdErrorString(err), line);
      }
      else
      {
	printf("PPD %s: %s\n", testname, ppd->nickname);
	ppdClose(ppd);
      }
    }
    else
      printf("%s: No PPD files found.\n", argv[1]);

    cupsArrayDelete(dirlist);
    for (col = (ppd_collection_t *)cupsArrayFirst(ppd_collections);
	 col;
	 col = (ppd_collection_t *)cupsArrayNext(ppd_collections))
    {
      free(col->name);
      free(col);
    }
    cupsArrayDelete(ppd_collections);
    if (ppds)
    {
      for (s = (char *)cupsArrayFirst(ppds);
	   s;
	   s = (char *)cupsArrayNext(ppds))
	free(s);
      cupsArrayDelete(ppds);
    }
    if (num_options)
      cupsFreeOptions(num_options, options);

    return (0);
  }
  else
  {
    const char	*name;			/* PPD name */
    char	*filename;		/* Name of actual file containing PPD */
    struct stat	fileinfo;		/* File information */

    name = argv[1];
    filename = strdup(name);
    if ((s = strchr(filename, ':')) != NULL)
      *s = '\0';

    if (lstat(filename, &fileinfo))
    {
      printf("%s: %s\n", filename, strerror(errno));
      return (1);
    }

    if (S_ISLNK(fileinfo.st_mode))
    {
      char	realfile[1024];		/* Real file path */
      ssize_t	realsize;		/* Size of real file path */


      if ((realsize = readlink(filename, realfile, sizeof(realfile) - 1)) < 0)
        strlcpy(realfile, "Unknown", sizeof(realfile));
      else
        realfile[realsize] = '\0';

      if (stat(realfile, &fileinfo))
	printf("%s: symlink to \"%s\", %s\n", filename, realfile,
	       strerror(errno));
      else
	printf("%s: symlink to \"%s\", %ld bytes\n", filename, realfile,
	       (long)fileinfo.st_size);
    }
    else
      printf("%s: regular file, %ld bytes\n", filename, (long)fileinfo.st_size);

    if (s)
      printf("\nPPD file %s from %s:\n\n", s + 1, filename);
    else
      printf("\nPPD file %s:\n\n", filename);
    free(filename);

    if ((ppd = ppdOpen2(ppdCollectionGetPPD(name, NULL, _log, NULL))) ==
	NULL)
    {
      ppd_status_t	err;		/* Last error in file */
      int		line;		/* Line number in file */


      status ++;
      err = ppdLastError(&line);

      printf("%s: %s on line %d\n", argv[1], ppdErrorString(err), line);
    }
    else
    {
      int		j, k;		/* Looping vars */
      ppd_group_t	*group;		/* Option group */
      ppd_option_t	*option;	/* Option */
      ppd_coption_t	*coption;	/* Custom option */
      ppd_cparam_t	*cparam;	/* Custom parameter */
      ppd_const_t	*c;		/* UIConstraints */
      char		lang[255],	/* LANG environment variable */
			lc_all[255],	/* LC_ALL environment variable */
			lc_ctype[255],	/* LC_CTYPE environment variable */
			lc_messages[255];/* LC_MESSAGES environment variable */


      if (argc > 2)
      {
        snprintf(lang, sizeof(lang), "LANG=%s", argv[2]);
	putenv(lang);
        snprintf(lc_all, sizeof(lc_all), "LC_ALL=%s", argv[2]);
	putenv(lc_all);
        snprintf(lc_ctype, sizeof(lc_ctype), "LC_CTYPE=%s", argv[2]);
	putenv(lc_ctype);
        snprintf(lc_messages, sizeof(lc_messages), "LC_MESSAGES=%s", argv[2]);
	putenv(lc_messages);
      }

      ppdLocalize(ppd);
      ppdMarkDefaults(ppd);

      if (argc > 3)
      {
        text = ppdLocalizeIPPReason(ppd, argv[3], NULL, buffer, sizeof(buffer));
	printf("ppdLocalizeIPPReason(%s)=%s\n", argv[3],
	       text ? text : "(null)");
	return (text == NULL);
      }

      for (i = ppd->num_groups, group = ppd->groups;
	   i > 0;
	   i --, group ++)
      {
	printf("%s (%s):\n", group->name, group->text);

	for (j = group->num_options, option = group->options;
	     j > 0;
	     j --, option ++)
	{
	  printf("    %s (%s):\n", option->keyword, option->text);

	  for (k = 0; k < option->num_choices; k ++)
	    printf("        - %s%s (%s)\n",
	           option->choices[k].marked ? "*" : "",
		   option->choices[k].choice, option->choices[k].text);

          if ((coption = ppdFindCustomOption(ppd, option->keyword)) != NULL)
	  {
	    for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
	         cparam;
		 cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
            {
	      switch (cparam->type)
	      {
	        case PPD_CUSTOM_UNKNOWN :
		    printf("              %s(%s): PPD_CUSTOM_UNKNOWN (error)\n", cparam->name, cparam->text);
	            break;

	        case PPD_CUSTOM_CURVE :
		    printf("              %s(%s): PPD_CUSTOM_CURVE (%g to %g)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_curve,
			   cparam->maximum.custom_curve);
		    break;

	        case PPD_CUSTOM_INT :
		    printf("              %s(%s): PPD_CUSTOM_INT (%d to %d)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_int,
			   cparam->maximum.custom_int);
		    break;

	        case PPD_CUSTOM_INVCURVE :
		    printf("              %s(%s): PPD_CUSTOM_INVCURVE (%g to %g)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_invcurve,
			   cparam->maximum.custom_invcurve);
		    break;

	        case PPD_CUSTOM_PASSCODE :
		    printf("              %s(%s): PPD_CUSTOM_PASSCODE (%d to %d)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_passcode,
			   cparam->maximum.custom_passcode);
		    break;

	        case PPD_CUSTOM_PASSWORD :
		    printf("              %s(%s): PPD_CUSTOM_PASSWORD (%d to %d)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_password,
			   cparam->maximum.custom_password);
		    break;

	        case PPD_CUSTOM_POINTS :
		    printf("              %s(%s): PPD_CUSTOM_POINTS (%g to %g)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_points,
			   cparam->maximum.custom_points);
		    break;

	        case PPD_CUSTOM_REAL :
		    printf("              %s(%s): PPD_CUSTOM_REAL (%g to %g)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_real,
			   cparam->maximum.custom_real);
		    break;

	        case PPD_CUSTOM_STRING :
		    printf("              %s(%s): PPD_CUSTOM_STRING (%d to %d)\n",
		           cparam->name, cparam->text,
			   cparam->minimum.custom_string,
			   cparam->maximum.custom_string);
		    break;
	      }
	    }
	  }
	}
      }

      puts("\nSizes:");
      for (i = ppd->num_sizes, size = ppd->sizes; i > 0; i --, size ++)
        printf("    %s = %gx%g, [%g %g %g %g]\n", size->name, size->width,
	       size->length, size->left, size->bottom, size->right, size->top);

      puts("\nConstraints:");

      for (i = ppd->num_consts, c = ppd->consts; i > 0; i --, c ++)
        printf("    *UIConstraints: *%s %s *%s %s\n", c->option1, c->choice1,
	       c->option2, c->choice2);
      if (ppd->num_consts == 0)
        puts("    NO CONSTRAINTS");

      puts("\nFilters:");

      for (i = 0; i < ppd->num_filters; i ++)
        printf("    %s\n", ppd->filters[i]);

      if (ppd->num_filters == 0)
        puts("    NO FILTERS");

      puts("\nAttributes:");

      for (attr = (ppd_attr_t *)cupsArrayFirst(ppd->sorted_attrs);
           attr;
	   attr = (ppd_attr_t *)cupsArrayNext(ppd->sorted_attrs))
        printf("    *%s %s/%s: \"%s\"\n", attr->name, attr->spec,
	       attr->text, attr->value ? attr->value : "");

      puts("\nPPD Cache:");
      if ((pc = ppdCacheCreateWithPPD(ppd)) == NULL)
        printf("    Unable to create: %s\n", cupsLastErrorString());
      else
      {
        ppdCacheWriteFile(pc, "t.cache", NULL);
        puts("    Wrote t.cache.");
      }
    }

    if (!strncmp(argv[1], "-d", 2))
      unlink(filename);
  }

#ifdef __APPLE__
  if (getenv("MallocStackLogging") && getenv("MallocStackLoggingNoCompact"))
  {
    char	command[1024];		/* malloc_history command */

    snprintf(command, sizeof(command), "malloc_history %d -all_by_size",
	     getpid());
    fflush(stdout);
    system(command);
  }
#endif /* __APPLE__ */

  ppdClose(ppd);

  return (status);
}


/*
 * 'do_ppd_tests()' - Test the default option commands in a PPD file.
 */

static int				/* O - Number of errors */
do_ppd_tests(const char    *filename,	/* I - PPD file */
             int           num_options,	/* I - Number of options */
             cups_option_t *options)	/* I - Options */
{
  ppd_file_t		*ppd;		/* PPD file data */
  cups_page_header2_t	header;		/* Page header */


  printf("\"%s\": ", filename);
  fflush(stdout);

  if ((ppd = ppdOpenFile(filename)) == NULL)
  {
    ppd_status_t	status;		/* Status from PPD loader */
    int			line;		/* Line number containing error */


    status = ppdLastError(&line);

    puts("FAIL (bad PPD file)");
    printf("    %s on line %d\n", ppdErrorString(status), line);

    return (1);
  }

  ppdMarkDefaults(ppd);
  ppdMarkOptions(ppd, num_options, options);

  if (ppdRasterInterpretPPD(&header, ppd, 0, NULL, NULL))
  {
    puts("FAIL (error from function)");
    puts(_ppdRasterErrorString());

    return (1);
  }
  else
  {
    puts("PASS");

    return (0);
  }
}


/*
 * 'do_ps_tests()' - Test standard PostScript commands.
 */

static int
do_ps_tests(void)
{
  cups_page_header2_t	header;		/* Page header */
  int			preferred_bits;	/* Preferred bits */
  int			errors = 0;	/* Number of errors */


 /*
  * Test PS exec code...
  */

  fputs("ppdRasterExecPS(\"setpagedevice\"): ", stdout);
  fflush(stdout);

  memset(&header, 0, sizeof(header));
  header.Collate = CUPS_TRUE;
  preferred_bits = 0;

  if (ppdRasterExecPS(&header, &preferred_bits, setpagedevice_code))
  {
    puts("FAIL (error from function)");
    puts(_ppdRasterErrorString());
    errors ++;
  }
  else if (preferred_bits != 17 ||
           memcmp(&header, &setpagedevice_header, sizeof(header)))
  {
    puts("FAIL (bad header)");

    if (preferred_bits != 17)
      printf("    cupsPreferredBitsPerColor %d, expected 17\n",
             preferred_bits);

    print_changes(&setpagedevice_header, &header);
    errors ++;
  }
  else
    puts("PASS");

  fputs("ppdRasterExecPS(\"roll\"): ", stdout);
  fflush(stdout);

  if (ppdRasterExecPS(&header, &preferred_bits,
                        "792 612 0 0 0\n"
			"pop pop pop\n"
			"<</PageSize[5 -2 roll]/ImagingBBox null>>"
			"setpagedevice\n"))
  {
    puts("FAIL (error from function)");
    puts(_ppdRasterErrorString());
    errors ++;
  }
  else if (header.PageSize[0] != 792 || header.PageSize[1] != 612)
  {
    printf("FAIL (PageSize [%d %d], expected [792 612])\n", header.PageSize[0],
           header.PageSize[1]);
    errors ++;
  }
  else
    puts("PASS");

  fputs("ppdRasterExecPS(\"dup index\"): ", stdout);
  fflush(stdout);

  if (ppdRasterExecPS(&header, &preferred_bits,
                        "true false dup\n"
			"<</Collate 4 index"
			"/Duplex 5 index"
			"/Tumble 6 index>>setpagedevice\n"
			"pop pop pop"))
  {
    puts("FAIL (error from function)");
    puts(_ppdRasterErrorString());
    errors ++;
  }
  else
  {
    if (!header.Collate)
    {
      printf("FAIL (Collate false, expected true)\n");
      errors ++;
    }

    if (header.Duplex)
    {
      printf("FAIL (Duplex true, expected false)\n");
      errors ++;
    }

    if (header.Tumble)
    {
      printf("FAIL (Tumble true, expected false)\n");
      errors ++;
    }

    if(header.Collate && !header.Duplex && !header.Tumble)
      puts("PASS");
  }

  fputs("ppdRasterExecPS(\"%%Begin/EndFeature code\"): ", stdout);
  fflush(stdout);

  if (ppdRasterExecPS(&header, &preferred_bits, dsc_code))
  {
    puts("FAIL (error from function)");
    puts(_ppdRasterErrorString());
    errors ++;
  }
  else if (header.PageSize[0] != 792 || header.PageSize[1] != 1224)
  {
    printf("FAIL (bad PageSize [%d %d], expected [792 1224])\n",
           header.PageSize[0], header.PageSize[1]);
    errors ++;
  }
  else
    puts("PASS");

  return (errors);
}




/*
 * 'print_changes()' - Print differences in the page header.
 */

static void
print_changes(
    cups_page_header2_t *header,	/* I - Actual page header */
    cups_page_header2_t *expected)	/* I - Expected page header */
{
  int	i;				/* Looping var */


  if (strcmp(header->MediaClass, expected->MediaClass))
    printf("    MediaClass (%s), expected (%s)\n", header->MediaClass,
           expected->MediaClass);

  if (strcmp(header->MediaColor, expected->MediaColor))
    printf("    MediaColor (%s), expected (%s)\n", header->MediaColor,
           expected->MediaColor);

  if (strcmp(header->MediaType, expected->MediaType))
    printf("    MediaType (%s), expected (%s)\n", header->MediaType,
           expected->MediaType);

  if (strcmp(header->OutputType, expected->OutputType))
    printf("    OutputType (%s), expected (%s)\n", header->OutputType,
           expected->OutputType);

  if (header->AdvanceDistance != expected->AdvanceDistance)
    printf("    AdvanceDistance %d, expected %d\n", header->AdvanceDistance,
           expected->AdvanceDistance);

  if (header->AdvanceMedia != expected->AdvanceMedia)
    printf("    AdvanceMedia %d, expected %d\n", header->AdvanceMedia,
           expected->AdvanceMedia);

  if (header->Collate != expected->Collate)
    printf("    Collate %d, expected %d\n", header->Collate,
           expected->Collate);

  if (header->CutMedia != expected->CutMedia)
    printf("    CutMedia %d, expected %d\n", header->CutMedia,
           expected->CutMedia);

  if (header->Duplex != expected->Duplex)
    printf("    Duplex %d, expected %d\n", header->Duplex,
           expected->Duplex);

  if (header->HWResolution[0] != expected->HWResolution[0] ||
      header->HWResolution[1] != expected->HWResolution[1])
    printf("    HWResolution [%d %d], expected [%d %d]\n",
           header->HWResolution[0], header->HWResolution[1],
           expected->HWResolution[0], expected->HWResolution[1]);

  if (memcmp(header->ImagingBoundingBox, expected->ImagingBoundingBox,
             sizeof(header->ImagingBoundingBox)))
    printf("    ImagingBoundingBox [%d %d %d %d], expected [%d %d %d %d]\n",
           header->ImagingBoundingBox[0],
           header->ImagingBoundingBox[1],
           header->ImagingBoundingBox[2],
           header->ImagingBoundingBox[3],
           expected->ImagingBoundingBox[0],
           expected->ImagingBoundingBox[1],
           expected->ImagingBoundingBox[2],
           expected->ImagingBoundingBox[3]);

  if (header->InsertSheet != expected->InsertSheet)
    printf("    InsertSheet %d, expected %d\n", header->InsertSheet,
           expected->InsertSheet);

  if (header->Jog != expected->Jog)
    printf("    Jog %d, expected %d\n", header->Jog,
           expected->Jog);

  if (header->LeadingEdge != expected->LeadingEdge)
    printf("    LeadingEdge %d, expected %d\n", header->LeadingEdge,
           expected->LeadingEdge);

  if (header->Margins[0] != expected->Margins[0] ||
      header->Margins[1] != expected->Margins[1])
    printf("    Margins [%d %d], expected [%d %d]\n",
           header->Margins[0], header->Margins[1],
           expected->Margins[0], expected->Margins[1]);

  if (header->ManualFeed != expected->ManualFeed)
    printf("    ManualFeed %d, expected %d\n", header->ManualFeed,
           expected->ManualFeed);

  if (header->MediaPosition != expected->MediaPosition)
    printf("    MediaPosition %d, expected %d\n", header->MediaPosition,
           expected->MediaPosition);

  if (header->MediaWeight != expected->MediaWeight)
    printf("    MediaWeight %d, expected %d\n", header->MediaWeight,
           expected->MediaWeight);

  if (header->MirrorPrint != expected->MirrorPrint)
    printf("    MirrorPrint %d, expected %d\n", header->MirrorPrint,
           expected->MirrorPrint);

  if (header->NegativePrint != expected->NegativePrint)
    printf("    NegativePrint %d, expected %d\n", header->NegativePrint,
           expected->NegativePrint);

  if (header->NumCopies != expected->NumCopies)
    printf("    NumCopies %d, expected %d\n", header->NumCopies,
           expected->NumCopies);

  if (header->Orientation != expected->Orientation)
    printf("    Orientation %d, expected %d\n", header->Orientation,
           expected->Orientation);

  if (header->OutputFaceUp != expected->OutputFaceUp)
    printf("    OutputFaceUp %d, expected %d\n", header->OutputFaceUp,
           expected->OutputFaceUp);

  if (header->PageSize[0] != expected->PageSize[0] ||
      header->PageSize[1] != expected->PageSize[1])
    printf("    PageSize [%d %d], expected [%d %d]\n",
           header->PageSize[0], header->PageSize[1],
           expected->PageSize[0], expected->PageSize[1]);

  if (header->Separations != expected->Separations)
    printf("    Separations %d, expected %d\n", header->Separations,
           expected->Separations);

  if (header->TraySwitch != expected->TraySwitch)
    printf("    TraySwitch %d, expected %d\n", header->TraySwitch,
           expected->TraySwitch);

  if (header->Tumble != expected->Tumble)
    printf("    Tumble %d, expected %d\n", header->Tumble,
           expected->Tumble);

  if (header->cupsWidth != expected->cupsWidth)
    printf("    cupsWidth %d, expected %d\n", header->cupsWidth,
           expected->cupsWidth);

  if (header->cupsHeight != expected->cupsHeight)
    printf("    cupsHeight %d, expected %d\n", header->cupsHeight,
           expected->cupsHeight);

  if (header->cupsMediaType != expected->cupsMediaType)
    printf("    cupsMediaType %d, expected %d\n", header->cupsMediaType,
           expected->cupsMediaType);

  if (header->cupsBitsPerColor != expected->cupsBitsPerColor)
    printf("    cupsBitsPerColor %d, expected %d\n", header->cupsBitsPerColor,
           expected->cupsBitsPerColor);

  if (header->cupsBitsPerPixel != expected->cupsBitsPerPixel)
    printf("    cupsBitsPerPixel %d, expected %d\n", header->cupsBitsPerPixel,
           expected->cupsBitsPerPixel);

  if (header->cupsBytesPerLine != expected->cupsBytesPerLine)
    printf("    cupsBytesPerLine %d, expected %d\n", header->cupsBytesPerLine,
           expected->cupsBytesPerLine);

  if (header->cupsColorOrder != expected->cupsColorOrder)
    printf("    cupsColorOrder %d, expected %d\n", header->cupsColorOrder,
           expected->cupsColorOrder);

  if (header->cupsColorSpace != expected->cupsColorSpace)
    printf("    cupsColorSpace %s, expected %s\n", _ppdRasterColorSpaceString(header->cupsColorSpace),
           _ppdRasterColorSpaceString(expected->cupsColorSpace));

  if (header->cupsCompression != expected->cupsCompression)
    printf("    cupsCompression %d, expected %d\n", header->cupsCompression,
           expected->cupsCompression);

  if (header->cupsRowCount != expected->cupsRowCount)
    printf("    cupsRowCount %d, expected %d\n", header->cupsRowCount,
           expected->cupsRowCount);

  if (header->cupsRowFeed != expected->cupsRowFeed)
    printf("    cupsRowFeed %d, expected %d\n", header->cupsRowFeed,
           expected->cupsRowFeed);

  if (header->cupsRowStep != expected->cupsRowStep)
    printf("    cupsRowStep %d, expected %d\n", header->cupsRowStep,
           expected->cupsRowStep);

  if (header->cupsNumColors != expected->cupsNumColors)
    printf("    cupsNumColors %d, expected %d\n", header->cupsNumColors,
           expected->cupsNumColors);

  if (fabs(header->cupsBorderlessScalingFactor - expected->cupsBorderlessScalingFactor) > 0.001)
    printf("    cupsBorderlessScalingFactor %g, expected %g\n",
           header->cupsBorderlessScalingFactor,
           expected->cupsBorderlessScalingFactor);

  if (fabs(header->cupsPageSize[0] - expected->cupsPageSize[0]) > 0.001 ||
      fabs(header->cupsPageSize[1] - expected->cupsPageSize[1]) > 0.001)
    printf("    cupsPageSize [%g %g], expected [%g %g]\n",
           header->cupsPageSize[0], header->cupsPageSize[1],
           expected->cupsPageSize[0], expected->cupsPageSize[1]);

  if (fabs(header->cupsImagingBBox[0] - expected->cupsImagingBBox[0]) > 0.001 ||
      fabs(header->cupsImagingBBox[1] - expected->cupsImagingBBox[1]) > 0.001 ||
      fabs(header->cupsImagingBBox[2] - expected->cupsImagingBBox[2]) > 0.001 ||
      fabs(header->cupsImagingBBox[3] - expected->cupsImagingBBox[3]) > 0.001)
    printf("    cupsImagingBBox [%g %g %g %g], expected [%g %g %g %g]\n",
           header->cupsImagingBBox[0], header->cupsImagingBBox[1],
           header->cupsImagingBBox[2], header->cupsImagingBBox[3],
           expected->cupsImagingBBox[0], expected->cupsImagingBBox[1],
           expected->cupsImagingBBox[2], expected->cupsImagingBBox[3]);

  for (i = 0; i < 16; i ++)
    if (header->cupsInteger[i] != expected->cupsInteger[i])
      printf("    cupsInteger%d %d, expected %d\n", i, header->cupsInteger[i],
             expected->cupsInteger[i]);

  for (i = 0; i < 16; i ++)
    if (fabs(header->cupsReal[i] - expected->cupsReal[i]) > 0.001)
      printf("    cupsReal%d %g, expected %g\n", i, header->cupsReal[i],
             expected->cupsReal[i]);

  for (i = 0; i < 16; i ++)
    if (strcmp(header->cupsString[i], expected->cupsString[i]))
      printf("    cupsString%d (%s), expected (%s)\n", i,
	     header->cupsString[i], expected->cupsString[i]);

  if (strcmp(header->cupsMarkerType, expected->cupsMarkerType))
    printf("    cupsMarkerType (%s), expected (%s)\n", header->cupsMarkerType,
           expected->cupsMarkerType);

  if (strcmp(header->cupsRenderingIntent, expected->cupsRenderingIntent))
    printf("    cupsRenderingIntent (%s), expected (%s)\n",
           header->cupsRenderingIntent,
           expected->cupsRenderingIntent);

  if (strcmp(header->cupsPageSizeName, expected->cupsPageSizeName))
    printf("    cupsPageSizeName (%s), expected (%s)\n",
           header->cupsPageSizeName,
           expected->cupsPageSizeName);
}

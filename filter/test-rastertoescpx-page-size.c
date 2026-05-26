//
// Regression test for rastertoescpx page size selection.
//
// Licensed under Apache License v2.0. See the file "LICENSE" for more
// information.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main rastertoescpx_filter_main
#include "rastertoescpx.c"
#undef main

static void
init_header(cups_page_header2_t *header, int y_resolution)
{
  memset(header, 0, sizeof(*header));
  header->HWResolution[1] = y_resolution;
}

static void
init_size(ppd_size_t *size, int marked, float length, float top)
{
  memset(size, 0, sizeof(*size));
  size->marked = marked;
  size->length = length;
  size->top = top;
}

static int
expect_printer_top(const char          *label,
                   ppd_file_t          *ppd,
                   cups_page_header2_t *header,
                   int                 expected)
{
  int actual = GetPrinterTop(ppd, header);

  if (actual != expected)
  {
    fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
    return (0);
  }

  return (1);
}

static int
write_test_ppd(char   *path,
               size_t path_size)
{
  static const char *ppd_data =
    "*PPD-Adobe: \"4.3\"\n"
    "*FormatVersion: \"4.3\"\n"
    "*FileVersion: \"1.0\"\n"
    "*LanguageVersion: English\n"
    "*LanguageEncoding: ISOLatin1\n"
    "*PCFileName: \"TEST.PPD\"\n"
    "*Manufacturer: \"Test\"\n"
    "*Product: \"(Test RastertoESCPX)\"\n"
    "*ModelName: \"Test RastertoESCPX\"\n"
    "*ShortNickName: \"Test RastertoESCPX\"\n"
    "*NickName: \"Test RastertoESCPX\"\n"
    "*PSVersion: \"(3010.000) 0\"\n"
    "*LanguageLevel: \"3\"\n"
    "*ColorDevice: False\n"
    "*DefaultColorSpace: Gray\n"
    "*FileSystem: False\n"
    "*Throughput: \"1\"\n"
    "*LandscapeOrientation: Plus90\n"
    "*TTRasterizer: Type42\n"
    "*cupsModelNumber: 0\n"
    "*OpenUI *PageSize/Page Size: PickOne\n"
    "*DefaultPageSize: Letter\n"
    "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
    "*PageSize Legal/US Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
    "*CloseUI: *PageSize\n"
    "*DefaultImageableArea: Letter\n"
    "*ImageableArea Letter/US Letter: \"0 36 612 756\"\n"
    "*ImageableArea Legal/US Legal: \"0 72 612 900\"\n"
    "*DefaultPaperDimension: Letter\n"
    "*PaperDimension Letter/US Letter: \"612 792\"\n"
    "*PaperDimension Legal/US Legal: \"612 1008\"\n";
  int  fd;
  FILE *fp;


  snprintf(path, path_size, "/tmp/testrastertoescpx-ppd-XXXXXX");

  fd = mkstemp(path);
  if (fd < 0)
  {
    perror("mkstemp");
    return (0);
  }

  fp = fdopen(fd, "w");
  if (!fp)
  {
    perror("fdopen");
    close(fd);
    unlink(path);
    return (0);
  }

  if (fputs(ppd_data, fp) == EOF || fclose(fp) == EOF)
  {
    perror("write test ppd");
    unlink(path);
    return (0);
  }

  return (1);
}

static int
expect_marked_page_size(const char *label,
                        const char *option_string,
                        int        y_resolution,
                        int        expected)
{
  char               ppd_path[] = "/tmp/testrastertoescpx-ppd-XXXXXX";
  cups_page_header2_t header;
  cups_option_t      *options = NULL;
  ppd_file_t         *ppd;
  int                num_options;
  int                status;


  if (!write_test_ppd(ppd_path, sizeof(ppd_path)))
    return (0);

  ppd = ppdOpenFile(ppd_path);
  unlink(ppd_path);

  if (!ppd)
  {
    fputs("Unable to open generated test PPD.\n", stderr);
    return (0);
  }

  num_options = cupsParseOptions(option_string, 0, &options);
  ppdMarkDefaults(ppd);
  ppdMarkOptions(ppd, num_options, options);

  init_header(&header, y_resolution);
  status = expect_printer_top(label, ppd, &header, expected);

  cupsFreeOptions(num_options, options);
  ppdClose(ppd);

  return (status);
}

int
main(void)
{
  cups_page_header2_t header;
  ppd_file_t ppd;
  ppd_size_t sizes[2];

  memset(&ppd, 0, sizeof(ppd));
  memset(sizes, 0, sizeof(sizes));

  ppd.sizes = sizes;

  init_header(&header, 72);

  init_size(&sizes[0], 1, 792.0f, 756.0f);
  init_size(&sizes[1], 0, 1008.0f, 936.0f);
  ppd.num_sizes = 2;

  if (!expect_printer_top("marked size", &ppd, &header, 36))
    return (1);

  // A canary second entry must not affect a single-size queue.
  init_header(&header, 144);
  init_size(&sizes[0], 1, 792.0f, 756.0f);
  init_size(&sizes[1], 0, 1600.0f, 1400.0f);
  ppd.num_sizes = 1;

  if (!expect_printer_top("single size", &ppd, &header, 72))
    return (1);

  sizes[0].marked = 0;
  if (!expect_printer_top("no marked size", &ppd, &header, 0))
    return (1);

  if (!expect_printer_top("null ppd", NULL, &header, 0))
    return (1);

  if (!expect_printer_top("null header", &ppd, NULL, 0))
    return (1);

  if (!expect_marked_page_size("default page size", "", 72, 36))
    return (1);

  if (!expect_marked_page_size("selected page size", "PageSize=Legal", 72, 108))
    return (1);

  return (0);
}

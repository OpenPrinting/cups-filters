//
// Regression test for rastertopclx EndJob format string handling.
//
// Licensed under Apache License v2.0. See the file "LICENSE" for more
// information.
//

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main rastertopclx_filter_main
#include "rastertopclx.c"
#undef main

static int
write_test_ppd(char *path, size_t path_size, const char *endjob_value)
{
  static const char *ppd_data =
    "*PPD-Adobe: \"4.3\"\n"
    "*FormatVersion: \"4.3\"\n"
    "*FileVersion: \"1.0\"\n"
    "*LanguageVersion: English\n"
    "*LanguageEncoding: ISOLatin1\n"
    "*PCFileName: \"TEST.PPD\"\n"
    "*Manufacturer: \"Test\"\n"
    "*Product: \"(Test RastertoPCLX)\"\n"
    "*ModelName: \"Test RastertoPCLX\"\n"
    "*ShortNickName: \"Test RastertoPCLX\"\n"
    "*NickName: \"Test RastertoPCLX\"\n"
    "*PSVersion: \"(3010.000) 0\"\n"
    "*LanguageLevel: \"3\"\n"
    "*ColorDevice: False\n"
    "*DefaultColorSpace: Gray\n"
    "*FileSystem: False\n"
    "*Throughput: \"1\"\n"
    "*LandscapeOrientation: Plus90\n"
    "*TTRasterizer: Type42\n"
    "*OpenUI *PageSize/Page Size: PickOne\n"
    "*DefaultPageSize: Letter\n"
    "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
    "*CloseUI: *PageSize\n"
    "*cupsPCL EndJob: \"%s\"\n";
  int fd;
  FILE *fp;

  snprintf(path, path_size, "/tmp/rastertopclx-ppd-XXXXXX");

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

  if (fprintf(fp, ppd_data, endjob_value) < 0 || fclose(fp) == EOF)
  {
    perror("write test ppd");
    unlink(path);
    return (0);
  }

  return (1);
}

static int
capture_shutdown_output(ppd_file_t *ppd, char *output, size_t output_size)
{
  int pipefds[2];
  int saved_stdout;
  ssize_t bytes_read;
  size_t total = 0;

  if (pipe(pipefds) != 0)
  {
    perror("pipe");
    return (-1);
  }

  saved_stdout = dup(STDOUT_FILENO);
  if (saved_stdout < 0)
  {
    perror("dup");
    close(pipefds[0]);
    close(pipefds[1]);
    return (-1);
  }

  fflush(stdout);

  if (dup2(pipefds[1], STDOUT_FILENO) < 0)
  {
    perror("dup2");
    close(saved_stdout);
    close(pipefds[0]);
    close(pipefds[1]);
    return (-1);
  }

  close(pipefds[1]);

  Page = 7;
  Shutdown(ppd, 1, "user", "title", 0, NULL);

  fflush(stdout);

  if (dup2(saved_stdout, STDOUT_FILENO) < 0)
  {
    perror("dup2 restore");
    close(saved_stdout);
    close(pipefds[0]);
    return (-1);
  }

  close(saved_stdout);

  while ((bytes_read = read(pipefds[0], output + total,
                            output_size - 1 - total)) > 0)
  {
    total += (size_t)bytes_read;
    if (total >= output_size - 1)
      break;
  }

  close(pipefds[0]);
  output[total] = '\0';

  return ((int)total);
}

static int
expect_literal_percent_s(void)
{
  char ppd_path[] = "/tmp/rastertopclx-ppd-XXXXXX";
  char output[256];
  ppd_file_t *ppd;
  int output_len;

  if (!write_test_ppd(ppd_path, sizeof(ppd_path), "%s"))
    return (1);

  ppd = ppdOpenFile(ppd_path);
  unlink(ppd_path);

  if (!ppd)
  {
    fputs("Unable to open generated test PPD.\n", stderr);
    return (1);
  }

  output_len = capture_shutdown_output(ppd, output, sizeof(output));
  ppdClose(ppd);

  if (output_len < 0)
    return (1);

  if (output_len < 3 ||
      output[0] != '\033' ||
      output[1] != '%' ||
      output[2] != 's')
  {
    fprintf(stderr, "Unexpected Shutdown output prefix (%d bytes): ", output_len);
    for (int i = 0; i < output_len; i ++)
      fprintf(stderr, "%02x ", (unsigned char)output[i]);
    fputc('\n', stderr);
    return (1);
  }

  if (strstr(output, "%s") == NULL)
  {
    fputs("Expected literal \"%s\" in Shutdown output.\n", stderr);
    return (1);
  }

  return (0);
}

static int
expect_page_count_substitution(void)
{
  char ppd_path[] = "/tmp/rastertopclx-ppd-XXXXXX";
  char output[256];
  ppd_file_t *ppd;
  int output_len;

  if (!write_test_ppd(ppd_path, sizeof(ppd_path), "%02d"))
    return (1);

  ppd = ppdOpenFile(ppd_path);
  unlink(ppd_path);

  if (!ppd)
  {
    fputs("Unable to open generated substitution test PPD.\n", stderr);
    return (1);
  }

  output_len = capture_shutdown_output(ppd, output, sizeof(output));
  ppdClose(ppd);

  if (output_len < 0)
    return (1);

  if (output_len < 3 ||
      output[0] != '\033' ||
      output[1] != '0' ||
      output[2] != '7')
  {
    fprintf(stderr, "Unexpected substituted output prefix (%d bytes): ", output_len);
    for (int i = 0; i < output_len; i ++)
      fprintf(stderr, "%02x ", (unsigned char)output[i]);
    fputc('\n', stderr);
    return (1);
  }

  if (strstr(output, "%02d") != NULL)
  {
    fputs("Expected page count substitution for \"%02d\".\n", stderr);
    return (1);
  }

  return (0);
}

int
main(void)
{
  if (expect_literal_percent_s())
    return (1);

  if (expect_page_count_substitution())
    return (1);

  return (0);
}

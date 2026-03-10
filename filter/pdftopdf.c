//
// Legacy CUPS filter wrapper for ppdFilterPDFToPDF() for cups-filters.
//
// Adds support for the "print-as-image" job option (issue #601):
// rasterizes each PDF page via Ghostscript before processing, as a
// workaround for printers that misrender PDF vector/font content.
//
// Copyright © 2020-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/filter.h>
#include <ppd/ppd-filter.h>
#include <config.h>
#include <signal.h>
#include <sys/wait.h>

static int  JobCanceled = 0;

static void  cancel_job(int sig);
static int   rasterize_pdf_as_image(const char *infile,
                                    char       *outfile,
                                    size_t      outfile_size,
                                    int         dpi);

int
main(int  argc,
     char *argv[])
{
  int            ret;
  int            num_options;
  cups_option_t  *options;
  const char     *val;
  int            print_as_image;
  int            dpi;
  char           raster_file[4096];
  char           *new_argv[8];
  int            i;

#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;
#endif

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: pdftopdf job-id user title copies options [file]\n");
    return (1);
  }

#ifdef HAVE_SIGSET
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif

  num_options    = cupsParseOptions(argv[5], 0, &options);
  print_as_image = 0;
  dpi            = 300;

  if ((val = cupsGetOption("print-as-image", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "true") || !strcasecmp(val, "yes") ||
        !strcasecmp(val, "on")   || !strcmp(val, "1"))
    {
      print_as_image = 1;
      fprintf(stderr, "DEBUG: pdftopdf: \"print-as-image\" enabled.\n");
    }
  }

  if (print_as_image)
  {
    if ((val = cupsGetOption("print-as-image-dpi", num_options, options)) != NULL)
    {
      int requested = atoi(val);
      if (requested >= 72 && requested <= 2400)
        dpi = requested;
      else
        fprintf(stderr, "WARNING: pdftopdf: Invalid print-as-image-dpi \"%s\", using %d dpi.\n", val, dpi);
    }
    fprintf(stderr, "DEBUG: pdftopdf: print-as-image rasterization DPI: %d\n", dpi);
  }

  cupsFreeOptions(num_options, options);

  raster_file[0] = '\0';
  ret            = 0;

  if (print_as_image && argc == 7)
  {
    if (rasterize_pdf_as_image(argv[6], raster_file, sizeof(raster_file), dpi) == 0)
    {
      for (i = 0; i < argc; i ++)
        new_argv[i] = argv[i];
      new_argv[6] = raster_file;
      new_argv[7] = NULL;
      fprintf(stderr, "DEBUG: pdftopdf: Using rasterized PDF: %s\n", raster_file);
      ret = ppdFilterCUPSWrapper(argc, new_argv, ppdFilterPDFToPDF, NULL, &JobCanceled);
    }
    else
    {
      fprintf(stderr, "WARNING: pdftopdf: Rasterization failed, falling back to original PDF.\n");
      ret = ppdFilterCUPSWrapper(argc, argv, ppdFilterPDFToPDF, NULL, &JobCanceled);
    }
    if (raster_file[0] != '\0')
      unlink(raster_file);
  }
  else
  {
    if (print_as_image && argc == 6)
      fprintf(stderr, "WARNING: pdftopdf: \"print-as-image\" requested but input is stdin; skipped.\n");
    ret = ppdFilterCUPSWrapper(argc, argv, ppdFilterPDFToPDF, NULL, &JobCanceled);
  }

  if (ret)
    fprintf(stderr, "ERROR: pdftopdf filter function failed.\n");

  return (ret);
}

static int
rasterize_pdf_as_image(const char *infile, char *outfile, size_t outfile_size, int dpi)
{
  const char  *tmpdir;
  int          fd;
  char         res_arg[32];
  char         outfile_arg[4096];
  int          pid;
  int          wstatus;
  int          devnull_fd;
  static const char *gs_candidates[] = {"gs", "/usr/bin/gs", "/usr/local/bin/gs", NULL};
  const char  *gs_bin = NULL;
  int          i;

  tmpdir = getenv("TMPDIR");
  if (!tmpdir || !tmpdir[0])
    tmpdir = "/tmp";

  snprintf(outfile, outfile_size, "%s/pdftopdf-raster-XXXXXX.pdf", tmpdir);

#ifdef HAVE_MKSTEMPS
  fd = mkstemps(outfile, 4);
#else
  {
    char base_template[4096];
    snprintf(base_template, sizeof(base_template), "%s/pdftopdf-raster-XXXXXX", tmpdir);
    fd = mkstemp(base_template);
    if (fd >= 0)
    {
      close(fd);
      snprintf(outfile, outfile_size - 5, "%s", base_template);
      strncat(outfile, ".pdf", outfile_size - strlen(outfile) - 1);
      unlink(base_template);
      fd = open(outfile, O_CREAT | O_EXCL | O_WRONLY, 0600);
    }
  }
#endif

  if (fd < 0)
  {
    fprintf(stderr, "ERROR: pdftopdf: Cannot create temp file: %s\n", strerror(errno));
    outfile[0] = '\0';
    return (-1);
  }
  close(fd);
  unlink(outfile);

  for (i = 0; gs_candidates[i]; i ++)
  {
    if (access(gs_candidates[i], X_OK) == 0)
    {
      gs_bin = gs_candidates[i];
      break;
    }
  }

  if (!gs_bin)
  {
    fprintf(stderr, "ERROR: pdftopdf: Ghostscript not found.\n");
    outfile[0] = '\0';
    return (-1);
  }

  snprintf(res_arg, sizeof(res_arg), "-r%d", dpi);
  snprintf(outfile_arg, sizeof(outfile_arg), "-sOutputFile=%s", outfile);

  fprintf(stderr, "DEBUG: pdftopdf: rasterize_pdf_as_image: Running: %s -dBATCH -dNOPAUSE -dSAFER -dQUIET -sDEVICE=pdfimage24 %s -dCompressPages=true %s %s\n", gs_bin, res_arg, outfile_arg, infile);

  pid = fork();
  if (pid < 0)
  {
    fprintf(stderr, "ERROR: pdftopdf: fork() failed: %s\n", strerror(errno));
    outfile[0] = '\0';
    return (-1);
  }

  if (pid == 0)
  {
    devnull_fd = open("/dev/null", O_WRONLY);
    if (devnull_fd >= 0)
    {
      dup2(devnull_fd, STDOUT_FILENO);
      close(devnull_fd);
    }
    execlp(gs_bin, gs_bin, "-dBATCH", "-dNOPAUSE", "-dSAFER", "-dQUIET",
           "-sDEVICE=pdfimage24", res_arg, "-dCompressPages=true",
           outfile_arg, infile, (char *)NULL);
    fprintf(stderr, "ERROR: pdftopdf: execlp failed: %s\n", strerror(errno));
    _exit(1);
  }

  while (waitpid(pid, &wstatus, 0) < 0)
  {
    if (errno != EINTR)
    {
      fprintf(stderr, "ERROR: pdftopdf: waitpid() failed: %s\n", strerror(errno));
      outfile[0] = '\0';
      return (-1);
    }
  }

  if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
  {
    fprintf(stderr, "ERROR: pdftopdf: Ghostscript exited with status %d.\n",
            WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
    unlink(outfile);
    outfile[0] = '\0';
    return (-1);
  }

  if (access(outfile, R_OK) != 0)
  {
    fprintf(stderr, "ERROR: pdftopdf: gs did not produce output file.\n");
    outfile[0] = '\0';
    return (-1);
  }

  fprintf(stderr, "DEBUG: pdftopdf: rasterize_pdf_as_image: Rasterized PDF written to %s\n", outfile);
  return (0);
}

static void
cancel_job(int sig)
{
  (void)sig;
  JobCanceled = 1;
}

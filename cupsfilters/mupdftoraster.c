/*

Copyright (c) 2016, Pranjal Bhor
Copyright (c) 2008-2016, Till Kamppeter
Copyright (c) 2011, Tim Waugh
Copyright (c) 2011-2013, Richard Hughes

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

MIT Open Source License  -  http://www.opensource.org/

*/


/* PS/PDF to CUPS Raster filter based on mutool */

#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <cups/raster.h>
#include <cupsfilters/colormanager.h>
#include <cupsfilters/raster.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define PDF_MAX_CHECK_COMMENT_LINES	20

#define CUPS_IPTEMPFILE "/tmp/ip-XXXXXX"
#define CUPS_OPTEMPFILE "/tmp/op-XXXXXX"

#ifdef CUPS_RASTER_SYNCv1
typedef cups_page_header2_t mupdf_page_header;
#else
typedef cups_page_header_t mupdf_page_header;
#endif /* CUPS_RASTER_SYNCv1 */


int
parse_doc_type(FILE *fp, filter_logfunc_t log, void *ld)
{
  char buf[5];
  char *rc;

  /* get the first few bytes of the file */
  rewind(fp);
  rc = fgets(buf,sizeof(buf),fp);
  /* empty input */
  if (rc == NULL)
    return 1;

  /* is PDF */
  if (strncmp(buf,"%PDF",4) == 0)
    return 0;

  if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "mupdftoraster: input file cannot be identified");
  return -1;
}

static void
parse_pdf_header_options(FILE *fp, mupdf_page_header *h)
{
  char buf[4096];
  int i;

  rewind(fp);
  /* skip until PDF start header */
  while (fgets(buf,sizeof(buf),fp) != 0) {
    if (strncmp(buf,"%PDF",4) == 0) {
      break;
    }
  }
  for (i = 0;i < PDF_MAX_CHECK_COMMENT_LINES;i++) {
    if (fgets(buf,sizeof(buf),fp) == 0) break;
    if (strncmp(buf,"%%PDFTOPDFNumCopies",19) == 0) {
      char *p;

      p = strchr(buf+19,':');
      h->NumCopies = atoi(p+1);
    } else if (strncmp(buf,"%%PDFTOPDFCollate",17) == 0) {
      char *p;

      p = strchr(buf+17,':');
      while (*p == ' ' || *p == '\t') p++;
      if (strncasecmp(p,"true",4) == 0) {
        h->Collate = CUPS_TRUE;
      } else {
        h->Collate = CUPS_FALSE;
      }
    }
  }
}

static void
add_pdf_header_options(mupdf_page_header *h,
		       cups_array_t 	 *mupdf_args)
{
  char tmpstr[1024];

  if ((h->HWResolution[0] != 100) || (h->HWResolution[1] != 100)) {
    snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d", h->HWResolution[0], 
	     h->HWResolution[1]); 
    cupsArrayAdd(mupdf_args, strdup(tmpstr));
  } else {
    snprintf(tmpstr, sizeof(tmpstr), "-r100x100");
    cupsArrayAdd(mupdf_args, strdup(tmpstr));
  }

  snprintf(tmpstr, sizeof(tmpstr), "-w%d", h->cupsWidth);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));

  snprintf(tmpstr, sizeof(tmpstr), "-h%d", h->cupsHeight);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));

  switch (h->cupsColorSpace) {
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_CMY:
  case CUPS_CSPACE_SRGB:
  case CUPS_CSPACE_ADOBERGB:
    snprintf(tmpstr, sizeof(tmpstr), "-crgb");
    break;

  case CUPS_CSPACE_CMYK:
    snprintf(tmpstr, sizeof(tmpstr), "-ccmyk");
    break;

  case CUPS_CSPACE_SW:
    snprintf(tmpstr, sizeof(tmpstr), "-cgray");
    break;

  default:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
    snprintf(tmpstr, sizeof(tmpstr), "-cmono");
    break;
  }
  cupsArrayAdd(mupdf_args, strdup(tmpstr));
}

static int
mutool_spawn (const char *filename,
	      cups_array_t *mupdf_args,
	      char **envp,
        filter_logfunc_t log,
        void *ld)
{
  char *argument;
  char **mutoolargv;
  const char* apos;
  int i;
  int numargs;
  int pid;
  int status = 65536;
  int wstatus;

  /* Put mutool command line argument into an array for the "exec()"
     call */
  numargs = cupsArrayCount(mupdf_args);
  mutoolargv = calloc(numargs + 1, sizeof(char *));
  for (argument = (char *)cupsArrayFirst(mupdf_args), i = 0; argument;
       argument = (char *)cupsArrayNext(mupdf_args), i++) {
    mutoolargv[i] = argument;
  }
  mutoolargv[i] = NULL;

  /* Debug output: Full mutool command line and environment variables */
  if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "mupdftoraster: mutool command line:");
  for (i = 0; mutoolargv[i]; i ++) {
    if ((strchr(mutoolargv[i],' ')) || (strchr(mutoolargv[i],'\t')))
      apos = "'";
    else
      apos = "";
    if(log) log(ld, FILTER_LOGLEVEL_INFO, "mupdftoraster: %s%s%s", apos, mutoolargv[i], apos);
  }

  for (i = 0; envp[i]; i ++)
    if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "mupdftoraster: envp[%d]=\"%s\"\n", i, envp[i]);

  if ((pid = fork()) == 0) {
    /* Execute mutool command line ... */
    execvpe(filename, mutoolargv, envp);
    perror(filename);
    goto out;
  }

 retry_wait:
  if (waitpid (pid, &wstatus, 0) == -1) {
    if (errno == EINTR)
      goto retry_wait;
    perror ("mutool");
    goto out;
  }

  /* How did mutool process terminate */
  if (WIFEXITED(wstatus))
    /* Via exit() anywhere or return() in the main() function */
    status = WEXITSTATUS(wstatus);
  else if (WIFSIGNALED(wstatus))
    /* Via signal */
    status = 256 * WTERMSIG(wstatus);
  if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "mupdftoraster: mutool completed, status: %d\n", status);

 out:
  free(mutoolargv);
  return status;
}

int
mupdftoraster (int inputfd,         /* I - File descriptor input stream */
       int outputfd,                 /* I - File descriptor output stream */
       int inputseekable,            /* I - Is input stream seekable? (unused)*/
       filter_data_t *data,          /* I - Job and printer data */
       void *parameters)             /* I - Filter-specific parameters */
{
  char buf[BUFSIZ];
  char *icc_profile = NULL;
  char tmpstr[1024];
  cups_array_t *mupdf_args = NULL;
  cups_option_t *options = NULL;
  FILE *fp = NULL;
  char infilename[1024];
  mupdf_page_header h;
  int fd = -1;
  int cm_disabled;
  int n;
  int num_options;
  int empty = 0;
  int status = 1;
  ppd_file_t *ppd = NULL;
  struct sigaction sa;
  cm_calibration_t cm_calibrate;
  filter_data_t curr_data;
   filter_logfunc_t     log = data->logfunc;
  void          *ld = data->logdata;
  char **envp;

#ifdef HAVE_CUPS_1_7
  ppd_attr_t *attr;
#endif /* HAVE_CUPS_1_7 */

  if(parameters)
    envp = (char**)(parameters);
  else
    envp = NULL;

  memset(&sa, 0, sizeof(sa));
  /* Ignore SIGPIPE and have write return an error instead */
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);

  num_options = data->num_options;

  ppd = data->ppd;
  if (ppd) {
    ppdMarkOptions (ppd, num_options, options);
  }

  fd = cupsTempFd(infilename, 1024);
    if (fd < 0) {
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Can't create temporary file");
      goto out;
    }

    /* copy input file to the tmp file */
    while ((n = read(inputfd, buf, BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
        if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Can't copy input to temporary file");
        close(fd);
        goto out;
      }
    }

  if (!inputfd) {

    if (lseek(fd,0,SEEK_SET) < 0) {
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Can't rewind temporary file");
      close(fd);
      goto out;
    }

    if ((fp = fdopen(fd,"rb")) == 0) {
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Can't open temporary file");
      close(fd);
      goto out;
    }
  } else {
    /* filename is specified */

    if ((fp = fdopen(fd,"rb")) == 0) {
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Can't open temporary file");
      goto out;
    }
  }

  curr_data.printer = data->printer;
  curr_data.job_id = data->job_id;
  curr_data.job_user = data->job_user;
  curr_data.job_title = data->job_title;
  curr_data.copies = data->copies;
  curr_data.job_attrs = data->job_attrs;        /* We use command line options */
  curr_data.printer_attrs = data->printer_attrs;    /* We use the queue's PPD file */
  curr_data.num_options = data->num_options;
  curr_data.options = data->options;       /* Command line options from 5th arg */
  curr_data.ppdfile = data->ppdfile; /* PPD file name in the "PPD"
					  environment variable. */
  curr_data.ppd = data->ppd;
                                       /* Load PPD file */
  curr_data.logfunc = log;  /* Logging scheme of CUPS */
  curr_data.logdata = data->logdata;
  curr_data.iscanceledfunc = data->iscanceledfunc; /* Job-is-canceled
						       function */
  curr_data.iscanceleddata = data->iscanceleddata;


  /* If doc type is not PDF exit */
  empty = parse_doc_type(fp, log, ld);
  if (empty == -1)
    goto out;

  /*  Check status of color management in CUPS */
  cm_calibrate = cmGetCupsColorCalibrateMode(data, options, num_options);

  if (cm_calibrate == CM_CALIBRATION_ENABLED)
    cm_disabled = 1;
  else 
    cm_disabled = cmIsPrinterCmDisabled(data, getenv("PRINTER"));

  if (!cm_disabled)
    cmGetPrinterIccProfile(data, getenv("PRINTER"), &icc_profile, ppd);

/*  Find print-rendering-intent */

    getPrintRenderIntent(data, &h);
    if(log) log(ld, FILTER_LOGLEVEL_DEBUG,
    	"Print rendering intent = %s", h.cupsRenderingIntent);


  /* mutool parameters */
  mupdf_args = cupsArrayNew(NULL, NULL);
  if (!mupdf_args) {
    if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Unable to allocate memory for mutool arguments array");
    goto out;
  }

  if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "mupdftoraster: command: %s\n",CUPS_MUTOOL);
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_MUTOOL);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));
  cupsArrayAdd(mupdf_args, strdup("draw"));
  cupsArrayAdd(mupdf_args, strdup("-L"));
  cupsArrayAdd(mupdf_args, strdup("-o-"));
  cupsArrayAdd(mupdf_args, strdup("-smtf"));

  /* mutool output parameters */
  cupsArrayAdd(mupdf_args, strdup("-Fpwg"));

  /* Note that MuPDF only creates PWG Raster and never CUPS Raster,
     so we always set the PWG Raster flag in the cupsRasterParseIPPOptions()
     calls.
     Make also sure that the width and height of the page in pixels is
     the size of the full page (as PWG Raster and MuPDF require it) and not
     only the printable area (as ppdRasterInterpretPPD() sets, to fulfill
     CUPS Raster standard) */
  if (ppd) {
    ppdRasterInterpretPPD(&h, ppd, num_options, options, 0);
    h.cupsWidth = h.HWResolution[0] * h.PageSize[0] / 72;
    h.cupsHeight = h.HWResolution[1] * h.PageSize[1] / 72;
#ifdef HAVE_CUPS_1_7
    cupsRasterParseIPPOptions(&h, &curr_data, 1, 0);
#endif /* HAVE_CUPS_1_7 */
  } else {
#ifdef HAVE_CUPS_1_7
    cupsRasterParseIPPOptions(&h, &curr_data, 1, 1);
#else
    if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: No PPD file specified.");
    goto out;
#endif /* HAVE_CUPS_1_7 */
  }

  if ((h.HWResolution[0] == 100) && (h.HWResolution[1] == 100)) {
    /* No "Resolution" option */
    if (ppd && (attr = ppdFindAttr(ppd, "DefaultResolution", 0)) != NULL) {
      /* "*DefaultResolution" keyword in the PPD */
      const char *p = attr->value;
      h.HWResolution[0] = atoi(p);
      if ((p = strchr(p, 'x')) != NULL)
	h.HWResolution[1] = atoi(p);
      else
	h.HWResolution[1] = h.HWResolution[0];
      if (h.HWResolution[0] <= 0)
	h.HWResolution[0] = 300;
      if (h.HWResolution[1] <= 0)
	h.HWResolution[1] = h.HWResolution[0];
    } else {
      h.HWResolution[0] = 300;
      h.HWResolution[1] = 300;
    }
    h.cupsWidth = h.HWResolution[0] * h.PageSize[0] / 72;
    h.cupsHeight = h.HWResolution[1] * h.PageSize[1] / 72;
  }

  /* set PDF-specific options */
  parse_pdf_header_options(fp, &h);

  /* fixed other values that pdftopdf handles */
  h.MirrorPrint = CUPS_FALSE;
  h.Orientation = CUPS_ORIENT_0;

  /* get all the data from the header and pass it to mutool */
  add_pdf_header_options (&h, mupdf_args);

  snprintf(tmpstr, sizeof(tmpstr), "%s", infilename);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));

  /* Execute mutool command line ... */
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_MUTOOL);
		
  /* call mutool */
  status = mutool_spawn (tmpstr, mupdf_args, envp, log, ld);
  if (status != 0) status = 1;

  if(empty)
  {
    if(log) log(ld, FILTER_LOGLEVEL_ERROR, "mupdftoraster: Input is empty, outputting empty file.");
     status = 0;
  }
out:
  if (fp)
    fclose(fp);
  if (mupdf_args) 
    cupsArrayDelete(mupdf_args);

  free(icc_profile);
  if (ppd)
    ppdClose(ppd);
  if (fd >= 0)
    unlink(infilename);
  return status;
}

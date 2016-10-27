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


/* PS/PDF to CUPS Raster filter based on Mutool */

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


void
parse_doc_type(FILE *fp)
{
  char buf[5];
  char *rc;

  /* get the first few bytes of the file */
  rewind(fp);
  rc = fgets(buf,sizeof(buf),fp);
  if (rc == NULL)
    goto out;

  /* is PDF */
  if (strncmp(buf,"%PDF",4) == 0)
    return;
 out:
  fprintf(stderr,"DEBUG: input file cannot be identified\n");
  exit(EXIT_FAILURE);
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
		       cups_array_t 		 *mupdf_args)
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
	      FILE *fp,
	      int ipfiledes,
	      int opfiledes)
{
  char *argument;
  char buf[BUFSIZ];
  char **mutoolargv;
  const char* apos;
  int i;
  int n;
  int numargs;
  int pid;
  int status = 65536;
  int wstatus;
  FILE *tempfp;

  /* Put mutool command line argument into an array for the "exec()"
     call */
  numargs = cupsArrayCount(mupdf_args);
  mutoolargv = calloc(numargs + 1, sizeof(char *));
  for (argument = (char *)cupsArrayFirst(mupdf_args), i = 0; argument;
       argument = (char *)cupsArrayNext(mupdf_args), i++) {
    mutoolargv[i] = argument;
  }
  mutoolargv[i] = NULL;

  /* Debug output: Full Mutool command line and environment variables */
  fprintf(stderr, "DEBUG: Mutool command line:");
  for (i = 0; mutoolargv[i]; i ++) {
    if ((strchr(mutoolargv[i],' ')) || (strchr(mutoolargv[i],'\t')))
      apos = "'";
    else
      apos = "";
    fprintf(stderr, " %s%s%s", apos, mutoolargv[i], apos);
  }
  fprintf(stderr, "\n");

  for (i = 0; envp[i]; i ++)
    fprintf(stderr, "DEBUG: envp[%d]=\"%s\"\n", i, envp[i]);

  /* save the contents for the file to a temporary location */
  tempfp = fdopen(ipfiledes, "wb");
  while ((n = fread(buf, 1, BUFSIZ, fp)) > 0)
    fwrite(buf, 1, n, tempfp);

  fclose(tempfp);

  if ((pid = fork()) == 0) {
    /* Execute Mutool command line ... */
    execvpe(filename, mutoolargv, envp);
    perror(filename);
    goto out;
  }

 retry_wait:
  if (waitpid (pid, &wstatus, 0) == -1) {
    if (errno == EINTR)
      goto retry_wait;
    perror ("gs");
    goto out;
  }

  /* How did Mutool process terminate */
  if (WIFEXITED(wstatus))
    /* Via exit() anywhere or return() in the main() function */
    status = WEXITSTATUS(wstatus);
  else if (WIFSIGNALED(wstatus))
    /* Via signal */
    status = 256 * WTERMSIG(wstatus);

  /* write the output to stdout */
  tempfp = fdopen(opfiledes, "rb");
  while ((n = fread(buf, 1, BUFSIZ, tempfp)) > 0)
    fwrite(buf, 1, BUFSIZ, stdout);
  
  fclose(tempfp);

 out:
  free(mutoolargv);
  return status;
}

int
main (int argc, char **argv, char *envp[])
{
  char buf[BUFSIZ];
  char *icc_profile = NULL;
  char tmpstr[1024];
  char ipfilebuf[20],
       opfilebuf[20];
  const char *t = NULL;
  cups_array_t *mupdf_args = NULL;
  cups_option_t *options = NULL;
  FILE *fp = NULL;
  mupdf_page_header h;
  int fd,
      ipfiledes,
      opfiledes;
  int cm_disabled;
  int n;
  int num_options;
  int status = 1;
  ppd_file_t *ppd = NULL;
  struct sigaction sa;
  cm_calibration_t cm_calibrate;
#ifdef HAVE_CUPS_1_7
  ppd_attr_t *attr;
#endif /* HAVE_CUPS_1_7 */

  if (argc < 6 || argc > 7) {
    fprintf(stderr, "ERROR: %s job-id user title copies options [file]\n",
	    argv[0]);
    goto out;
  }

  memset(&sa, 0, sizeof(sa));
  /* Ignore SIGPIPE and have write return an error instead */
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);

  num_options = cupsParseOptions(argv[5], 0, &options);

  t = getenv("PPD");
  if (t && t[0] != '\0')
    if ((ppd = ppdOpenFile(t)) == NULL) {
      fprintf(stderr, "ERROR: Failed to open PPD: %s\n", t);
    }

  if (ppd) {
    ppdMarkDefaults (ppd);
    cupsMarkOptions (ppd, num_options, options);
  }

  if (argc == 6) {
    /* stdin */

    fd = cupsTempFd(buf,BUFSIZ);
    if (fd < 0) {
      fprintf(stderr, "ERROR: Can't create temporary file\n");
      goto out;
    }
    /* remove name */
    unlink(buf);

    /* copy stdin to the tmp file */
    while ((n = read(0,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
        fprintf(stderr, "ERROR: Can't copy stdin to temporary file\n");
        close(fd);
        goto out;
      }
    }
    if (lseek(fd,0,SEEK_SET) < 0) {
      fprintf(stderr, "ERROR: Can't rewind temporary file\n");
      close(fd);
      goto out;
    }

    if ((fp = fdopen(fd,"rb")) == 0) {
      fprintf(stderr, "ERROR: Can't fdopen temporary file\n");
      close(fd);
      goto out;
    }
  } else {
    /* argc == 7 filename is specified */

    if ((fp = fopen(argv[6],"rb")) == 0) {
      fprintf(stderr, "ERROR: Can't open input file %s\n",argv[6]);
      goto out;
    }
  }

  /* If doc type is not PDF exit */
  parse_doc_type(fp);

  /*  Check status of color management in CUPS */
  cm_calibrate = cmGetCupsColorCalibrateMode(options, num_options);

  if (cm_calibrate == CM_CALIBRATION_ENABLED)
    cm_disabled = 1;
  else 
    cm_disabled = cmIsPrinterCmDisabled(getenv("PRINTER"));

  if (!cm_disabled)
    cmGetPrinterIccProfile(getenv("PRINTER"), &icc_profile, ppd);

  /* Create temporary input and output files */
  memset(ipfilebuf,0,sizeof(ipfilebuf));
  memset(opfilebuf,0,sizeof(opfilebuf));
  strncpy(ipfilebuf,CUPS_IPTEMPFILE,14);
  strncpy(opfilebuf,CUPS_OPTEMPFILE,14);
  ipfiledes = mkstemp(ipfilebuf);
  opfiledes = mkstemp(opfilebuf);

  /* Mutool parameters */
  mupdf_args = cupsArrayNew(NULL, NULL);
  if (!mupdf_args) {
    fprintf(stderr, "ERROR: Unable to allocate memory for Mutool arguments array\n");
    goto out;
  }

  fprintf(stderr,"command: %s\n",CUPS_MUTOOL);
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_MUTOOL);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));
  cupsArrayAdd(mupdf_args, strdup("draw"));
  cupsArrayAdd(mupdf_args, strdup("-L"));
  snprintf(tmpstr, sizeof(tmpstr), "-o%s", opfilebuf);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));
  cupsArrayAdd(mupdf_args, strdup("-smtf"));

  /* Mutool output parameters */
  cupsArrayAdd(mupdf_args, strdup("-Fpwg"));

  /* Note that MuPDF only creates PWG Raster and never CUPS Raster,
     so we always set the PWG Raster flag in the cupsRasterParseIPPOptions()
     calls.
     Make also sure that the width and height of the page in pixels is
     the size of the full page (as PWG Raster and MuPDF require it) and not
     only the printable area (as cupsRasterInterpretPPD() sets, to fulfill
     CUPS Raster standard) */
  if (ppd) {
    cupsRasterInterpretPPD(&h, ppd, num_options, options, 0);
    h.cupsWidth = h.HWResolution[0] * h.PageSize[0] / 72;
    h.cupsHeight = h.HWResolution[1] * h.PageSize[1] / 72;
#ifdef HAVE_CUPS_1_7
    cupsRasterParseIPPOptions(&h, num_options, options, 1, 0);
#endif /* HAVE_CUPS_1_7 */
  } else {
#ifdef HAVE_CUPS_1_7
    cupsRasterParseIPPOptions(&h, num_options, options, 1, 1);
#else
    fprintf(stderr, "ERROR: No PPD file specified.\n");
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

  /* get all the data from the header and pass it to Mutool */
  add_pdf_header_options (&h, mupdf_args);

  snprintf(tmpstr, sizeof(tmpstr), "%s", ipfilebuf);
  cupsArrayAdd(mupdf_args, strdup(tmpstr));

  /* Execute Mutool command line ... */
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_MUTOOL);
		
  /* call mutool */
  rewind(fp);
  status = mutool_spawn (tmpstr, mupdf_args, envp, fp, ipfiledes, opfiledes);
  if (status != 0) status = 1;
out:
  if (fp)
    fclose(fp);
  if (mupdf_args) 
    cupsArrayDelete(mupdf_args);

  free(icc_profile);
  if (ppd)
    ppdClose(ppd);
  unlink(ipfilebuf);
  unlink(opfilebuf);
  return status;
}

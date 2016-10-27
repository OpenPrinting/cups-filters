/*

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


/* PS/PDF to CUPS Raster filter based on Ghostscript */

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
#include <errno.h>

#define PDF_MAX_CHECK_COMMENT_LINES	20

typedef enum {
  GS_DOC_TYPE_PDF,
  GS_DOC_TYPE_PS,
  GS_DOC_TYPE_UNKNOWN
} GsDocType;

typedef enum {
  OUTPUT_FORMAT_RASTER,
  OUTPUT_FORMAT_PDF,
  OUTPUT_FORMAT_PXL
} OutFormatType;

#ifdef CUPS_RASTER_SYNCv1
typedef cups_page_header2_t gs_page_header;
#else
typedef cups_page_header_t gs_page_header;
#endif /* CUPS_RASTER_SYNCv1 */

static GsDocType
parse_doc_type(FILE *fp)
{
  char buf[5];
  GsDocType doc_type;
  char *rc;

  /* get the first few bytes of the file */
  doc_type = GS_DOC_TYPE_UNKNOWN;
  rewind(fp);
  rc = fgets(buf,sizeof(buf),fp);
  if (rc == NULL)
    goto out;

  /* is PDF */
  if (strncmp(buf,"%PDF",4) == 0) {
    doc_type = GS_DOC_TYPE_PDF;
    goto out;
  }

  /* is PS */
  if (strncmp(buf,"%!",2) == 0) {
    doc_type = GS_DOC_TYPE_PS;
    goto out;
  }
out:
  return doc_type;
}

static void
parse_pdf_header_options(FILE *fp, gs_page_header *h)
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
add_pdf_header_options(gs_page_header *h, cups_array_t *gs_args,
		       OutFormatType outformat, int pxlcolor)
{
  int i;
  char tmpstr[1024];

  /* Simple boolean, enumerated choice, numerical, and string parameters */
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->MediaClass[0] |= '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-sMediaClass=%s", h->MediaClass);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->MediaColor[0] |= '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-sMediaColor=%s", h->MediaColor);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->MediaType[0] |= '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-sMediaType=%s", h->MediaType);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->OutputType[0] |= '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-sOutputType=%s", h->OutputType);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->AdvanceDistance) {
      snprintf(tmpstr, sizeof(tmpstr), "-dAdvanceDistance=%d",
	       (unsigned)(h->AdvanceDistance));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->AdvanceMedia) {
      snprintf(tmpstr, sizeof(tmpstr), "-dAdvanceMedia=%d",
	       (unsigned)(h->AdvanceMedia));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->Collate) {
      cupsArrayAdd(gs_args, strdup("-dCollate"));
    }
    if (h->CutMedia) {
      snprintf(tmpstr, sizeof(tmpstr), "-dCutMedia=%d",
	       (unsigned)(h->CutMedia));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
  }
  if (outformat == OUTPUT_FORMAT_RASTER ||
      outformat == OUTPUT_FORMAT_PXL) {
    /* PDF output is only for turning PostScript input data into PDF
       not for sending PDF to a PDF printer (this is done by pdftopdf)
       therefore we do not apply duplex/tumble here. */
    if (h->Duplex) {
      cupsArrayAdd(gs_args, strdup("-dDuplex"));
    }
  }
  if ((h->HWResolution[0] != 100) || (h->HWResolution[1] != 100))
    snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d",
	     (unsigned)(h->HWResolution[0]), (unsigned)(h->HWResolution[1]));
  else
    snprintf(tmpstr, sizeof(tmpstr), "-r100x100");
  cupsArrayAdd(gs_args, strdup(tmpstr));
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->InsertSheet) {
      cupsArrayAdd(gs_args, strdup("-dInsertSheet"));
    }
    if (h->Jog) {
      snprintf(tmpstr, sizeof(tmpstr), "-dJog=%d",
	       (unsigned)(h->Jog));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->LeadingEdge) {
      snprintf(tmpstr, sizeof(tmpstr), "-dLeadingEdge=%d",
	       (unsigned)(h->LeadingEdge));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->ManualFeed) {
      cupsArrayAdd(gs_args, strdup("-dManualFeed"));
    }
  }
  if (outformat == OUTPUT_FORMAT_RASTER ||
      outformat == OUTPUT_FORMAT_PXL) {
    if (h->MediaPosition) {
      int mediapos;
      if (outformat == OUTPUT_FORMAT_PXL) {
	/* Convert PWG MediaPosition values to PXL-ones */
	if (h->MediaPosition == 1) /* Main */
	  mediapos = 4;
	else if (h->MediaPosition == 2) /* Alternate */
	  mediapos = 5;
	else if (h->MediaPosition == 3) /* Large Capacity */
	  mediapos = 7;
	else if (h->MediaPosition == 4) /* Manual */
	  mediapos = 2;
	else if (h->MediaPosition == 5) /* Envelope */
	  mediapos = 6;
	else if (h->MediaPosition == 11) /* Top */
	  mediapos = 4;
	else if (h->MediaPosition == 12) /* Middle */
	  mediapos = 5;
	else if (h->MediaPosition == 13) /* Bottom */
	  mediapos = 7;
	else if (h->MediaPosition == 19) /* Bypass */
	  mediapos = 3;
	else if (h->MediaPosition == 20) /* Tray 1 */
	  mediapos = 3;
	else if (h->MediaPosition == 21) /* Tray 2 */
	  mediapos = 4;
	else if (h->MediaPosition == 22) /* Tray 3 */
	  mediapos = 5;
	else if (h->MediaPosition == 23) /* Tray 4 */
	  mediapos = 7;
	else
	  mediapos = 0;
      } else
	mediapos = h->MediaPosition;
      snprintf(tmpstr, sizeof(tmpstr), "-dMediaPosition=%d",
	       (unsigned)(mediapos));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
  }
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->MediaWeight) {
      snprintf(tmpstr, sizeof(tmpstr), "-dMediaWeight=%d",
	       (unsigned)(h->MediaWeight));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->MirrorPrint) {
      cupsArrayAdd(gs_args, strdup("-dMirrorPrint"));
    }
    if (h->NegativePrint) {
      cupsArrayAdd(gs_args, strdup("-dNegativePrint"));
    }
    if (h->NumCopies != 1) {
      snprintf(tmpstr, sizeof(tmpstr), "-dNumCopies=%d",
	       (unsigned)(h->NumCopies));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->Orientation) {
      snprintf(tmpstr, sizeof(tmpstr), "-dOrientation=%d",
	       (unsigned)(h->Orientation));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->OutputFaceUp) {
      cupsArrayAdd(gs_args, strdup("-dOutputFaceUp"));
    }
  }
  if (h->PageSize[0] != 612)
    snprintf(tmpstr, sizeof(tmpstr), "-dDEVICEWIDTHPOINTS=%d",
	     (unsigned)(h->PageSize[0]));
  else
    snprintf(tmpstr, sizeof(tmpstr), "-dDEVICEWIDTHPOINTS=612");
  cupsArrayAdd(gs_args, strdup(tmpstr));
  if (h->PageSize[1] != 792)
    snprintf(tmpstr, sizeof(tmpstr), "-dDEVICEHEIGHTPOINTS=%d",
	     (unsigned)(h->PageSize[1]));
  else
    snprintf(tmpstr, sizeof(tmpstr), "-dDEVICEHEIGHTPOINTS=792");
  cupsArrayAdd(gs_args, strdup(tmpstr));
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->Separations) {
      cupsArrayAdd(gs_args, strdup("-dSeparations"));
    }
    if (h->TraySwitch) {
      cupsArrayAdd(gs_args, strdup("-dTraySwitch"));
    }
  }
  if (outformat == OUTPUT_FORMAT_RASTER ||
      outformat == OUTPUT_FORMAT_PXL) {
    /* PDF output is only for turning PostScript input data into PDF
       not for sending PDF to a PDF printer (this is done by pdftopdf)
       therefore we do not apply duplex/tumble here. */
    if (h->Tumble) {
      cupsArrayAdd(gs_args, strdup("-dTumble"));
    }
  }
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->cupsMediaType) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsMediaType=%d",
	       (unsigned)(h->cupsMediaType));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->cupsBitsPerColor != 1)
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsBitsPerColor=%d",
	       (unsigned)(h->cupsBitsPerColor));
    else
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsBitsPerColor=1");
    cupsArrayAdd(gs_args, strdup(tmpstr));
    if (h->cupsColorOrder != CUPS_ORDER_CHUNKED)
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsColorOrder=%d",
	       (unsigned)(h->cupsColorOrder));
    else
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsColorOrder=%d",
	       CUPS_ORDER_CHUNKED);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  }
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->cupsColorSpace != CUPS_CSPACE_K)
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsColorSpace=%d",
	       (unsigned)(h->cupsColorSpace));
    else
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsColorSpace=%d",
	       CUPS_CSPACE_K);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  }
  
  if (outformat == OUTPUT_FORMAT_PXL) {
    if (h->cupsColorSpace == CUPS_CSPACE_W ||
	h->cupsColorSpace == CUPS_CSPACE_K ||
	h->cupsColorSpace == CUPS_CSPACE_WHITE ||
	h->cupsColorSpace == CUPS_CSPACE_GOLD ||
	h->cupsColorSpace == CUPS_CSPACE_SILVER ||
	h->cupsColorSpace == CUPS_CSPACE_SW ||
	h->cupsColorSpace == CUPS_CSPACE_ICC1 ||
	h->cupsColorSpace == CUPS_CSPACE_DEVICE1)
      /* Monochrome color spaces -> use "pxlmono" device */
      pxlcolor = 0;
    if (pxlcolor == 1)
      cupsArrayAdd(gs_args, strdup("-sDEVICE=pxlcolor"));
    else
      cupsArrayAdd(gs_args, strdup("-sDEVICE=pxlmono"));
  }
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->cupsCompression) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsCompression=%d",
	       (unsigned)(h->cupsCompression));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->cupsRowCount) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsRowCount=%d",
	       (unsigned)(h->cupsRowCount));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->cupsRowFeed) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsRowFeed=%d",
	       (unsigned)(h->cupsRowFeed));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->cupsRowStep) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsRowStep=%d",
	       (unsigned)(h->cupsRowStep));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
  }
#ifdef CUPS_RASTER_SYNCv1
  if (outformat == OUTPUT_FORMAT_RASTER) {
    if (h->cupsBorderlessScalingFactor != 1.0f) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsBorderlessScalingFactor=%.4f",
	       h->cupsBorderlessScalingFactor);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    for (i=0; i <= 15; i ++)
      if (h->cupsInteger[i]) {
	snprintf(tmpstr, sizeof(tmpstr), "-dcupsInteger%d=%d",
		 i, (unsigned)(h->cupsInteger[i]));
	cupsArrayAdd(gs_args, strdup(tmpstr));
      }
    for (i=0; i <= 15; i ++)
      if (h->cupsReal[i]) {
	snprintf(tmpstr, sizeof(tmpstr), "-dcupsReal%d=%.4f",
		 i, h->cupsReal[i]);
	cupsArrayAdd(gs_args, strdup(tmpstr));
      }
    for (i=0; i <= 15; i ++)
      if (h->cupsString[i][0] != '\0') {
	snprintf(tmpstr, sizeof(tmpstr), "-scupsString%d=%s",
		 i, h->cupsString[i]);
	cupsArrayAdd(gs_args, strdup(tmpstr));
      }
    if (h->cupsMarkerType[0] != '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-scupsMarkerType=%s",
	       h->cupsMarkerType);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->cupsRenderingIntent[0] != '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-scupsRenderingIntent=%s",
	       h->cupsRenderingIntent);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    if (h->cupsPageSizeName[0] != '\0') {
      snprintf(tmpstr, sizeof(tmpstr), "-scupsPageSizeName=%s",
	       h->cupsPageSizeName);
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
  }
#endif /* CUPS_RASTER_SYNCv1 */
}

static int
gs_spawn (const char *filename,
          cups_array_t *gs_args,
          char **envp,
          FILE *fp)
{
  char *argument;
  char buf[BUFSIZ];
  char **gsargv;
  const char* apos;
  int fds[2];
  int i;
  int n;
  int numargs;
  int pid;
  int status = 65536;
  int wstatus;

  /* Put Ghostscript command line argument into an array for the "exec()"
     call */
  numargs = cupsArrayCount(gs_args);
  gsargv = calloc(numargs + 1, sizeof(char *));
  for (argument = (char *)cupsArrayFirst(gs_args), i = 0; argument;
       argument = (char *)cupsArrayNext(gs_args), i++) {
    gsargv[i] = argument;
  }
  gsargv[i] = NULL;

  /* Debug output: Full Ghostscript command line and environment variables */
  fprintf(stderr, "DEBUG: Ghostscript command line:");
  for (i = 0; gsargv[i]; i ++) {
    if ((strchr(gsargv[i],' ')) || (strchr(gsargv[i],'\t')))
      apos = "'";
    else
      apos = "";
    fprintf(stderr, " %s%s%s", apos, gsargv[i], apos);
  }
  fprintf(stderr, "\n");

  for (i = 0; envp[i]; i ++)
    fprintf(stderr, "DEBUG: envp[%d]=\"%s\"\n", i, envp[i]);

  /* Create a pipe for feeding the job into Ghostscript */
  if (pipe(fds))
  {
    fds[0] = -1;
    fds[1] = -1;
    fprintf(stderr, "ERROR: Unable to establish pipe for Ghostscript call\n");
    goto out;
  }

  /* Set the "close on exec" flag on each end of the pipe... */
  if (fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);
    fds[0] = -1;
    fds[1] = -1;
    fprintf(stderr, "ERROR: Unable to set \"close on exec\" flag on read end of the pipe for Ghostscript call\n");
    goto out;
  }
  if (fcntl(fds[1], F_SETFD, fcntl(fds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);
    fprintf(stderr, "ERROR: Unable to set \"close on exec\" flag on write end of the pipe for Ghostscript call\n");
    goto out;
  }

  if ((pid = fork()) == 0)
  {
    /* Couple pipe with STDIN of Ghostscript process */
    if (fds[0] != 0) {
      close(0);
      if (fds[0] > 0) {
        if (dup(fds[0]) < 0) {
	  fprintf(stderr, "ERROR: Unable to couple pipe with STDIN of Ghostscript process\n");
	  goto out;
	}
      } else {
        fprintf(stderr, "ERROR: Unable to couple pipe with STDIN of Ghostscript process\n");
        goto out;
      }
    }

    /* Execute Ghostscript command line ... */
    execvpe(filename, gsargv, envp);
    perror(filename);
    goto out;
  }

  /* Feed job data into Ghostscript */
  while ((n = fread(buf, 1, BUFSIZ, fp)) > 0) {
    int count;
retry_write:
    count = write(fds[1], buf, n);
    if (count != n) {
      if (count == -1) {
        if (errno == EINTR)
          goto retry_write;
        fprintf(stderr, "ERROR: write failed: %s\n", strerror(errno));
      }
      fprintf(stderr, "ERROR: Can't feed job data into Ghostscript\n");
      goto out;
    }
  }
  close (fds[1]);

retry_wait:
  if (waitpid (pid, &wstatus, 0) == -1) {
    if (errno == EINTR)
      goto retry_wait;
    perror ("gs");
    goto out;
  }

  /* How did Ghostscript terminate */
  if (WIFEXITED(wstatus))
    /* Via exit() anywhere or return() in the main() function */
    status = WEXITSTATUS(wstatus);
  else if (WIFSIGNALED(wstatus))
    /* Via signal */
    status = 256 * WTERMSIG(wstatus);

out:
  free(gsargv);
  return status;
}

#if 0
static char *
get_ppd_icc_fallback (ppd_file_t *ppd, char **qualifier)
{
  char full_path[1024];
  char *icc_profile = NULL;
  char qualifer_tmp[1024];
  const char *profile_key;
  ppd_attr_t *attr;
  char *datadir;

  /* get profile attr, falling back to CUPS */
  profile_key = "APTiogaProfile";
  attr = ppdFindAttr(ppd, profile_key, NULL);
  if (attr == NULL) {
    profile_key = "cupsICCProfile";
    attr = ppdFindAttr(ppd, profile_key, NULL);
  }

  /* create a string for a quick comparion */
  snprintf(qualifer_tmp, sizeof(qualifer_tmp),
           "%s.%s.%s",
           qualifier[0],
           qualifier[1],
           qualifier[2]);

  /* neither */
  if (attr == NULL) {
    fprintf(stderr, "INFO: no profiles specified in PPD\n");
    goto out;
  }

  if ((datadir = getenv("CUPS_DATADIR")) == NULL)
    datadir = CUPS_DATADIR;

  /* try to find a profile that matches the qualifier exactly */
  for (;attr != NULL; attr = ppdFindNextAttr(ppd, profile_key, NULL)) {
    fprintf(stderr, "INFO: found profile %s in PPD with qualifier '%s'\n",
            attr->value, attr->spec);

    /* invalid entry */
    if (attr->spec == NULL || attr->value == NULL)
      continue;

    /* expand to a full path if not already specified */
    if (attr->value[0] != '/')
      snprintf(full_path, sizeof(full_path),
               "%s/profiles/%s", datadir, attr->value);
    else
      strncpy(full_path, attr->value, sizeof(full_path));

    /* check the file exists */
    if (access(full_path, 0)) {
      fprintf(stderr, "INFO: found profile %s in PPD that does not exist\n",
              full_path);
      continue;
    }

    /* matches the qualifier */
    if (strcmp(qualifer_tmp, attr->spec) == 0) {
      icc_profile = strdup(full_path);
      goto out;
    }
  }

  /* no match */
  if (attr == NULL) {
    fprintf(stderr, "INFO: no profiles in PPD for qualifier '%s'\n",
            qualifer_tmp);
    goto out;
  }

out:
  return icc_profile;
}
#endif

int
main (int argc, char **argv, char *envp[])
{
  char *outformat_env = NULL;
  OutFormatType outformat;
  char buf[BUFSIZ];
  char *icc_profile = NULL;
  /*char **qualifier = NULL;*/
  char *tmp;
  char tmpstr[1024];
  const char *t = NULL;
  cups_array_t *gs_args = NULL;
  cups_option_t *options = NULL;
  FILE *fp = NULL;
  GsDocType doc_type;
  gs_page_header h;
  int fd;
  int cm_disabled;
  int n;
  int num_options;
  int status = 1;
  ppd_file_t *ppd = NULL;
  struct sigaction sa;
  cm_calibration_t cm_calibrate;
  int pxlcolor = 1;
#ifdef HAVE_CUPS_1_7
  int pwgraster = 0;
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

  /* Determine the output format via an environment variable set by a wrapper
     script */
  outformat_env = getenv("OUTFORMAT");
  if (outformat_env == NULL || strcasestr(outformat_env, "raster"))
    outformat = OUTPUT_FORMAT_RASTER;
  else if (strcasestr(outformat_env, "pdf"))
    outformat = OUTPUT_FORMAT_PDF;
  else if (strcasestr(outformat_env, "xl"))
    outformat = OUTPUT_FORMAT_PXL;
  else {
    fprintf(stderr, "ERROR: OUTFORMAT=\"%s\", cannot determine output format\n",
      outformat_env);
    goto out;
  }
  fprintf(stderr, "DEBUG: OUTFORMAT=\"%s\", so output format will be %s\n",
	  outformat_env,
	  (outformat == OUTPUT_FORMAT_RASTER ? "CUPS/PWG Raster" :
	   (outformat == OUTPUT_FORMAT_PDF ? "PDF" :
	    "PCL XL")));
  
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

  /* find out file type */
  doc_type = parse_doc_type(fp);
  if (doc_type == GS_DOC_TYPE_UNKNOWN) {
    fprintf(stderr, "ERROR: Can't detect file type\n");
    goto out;
  }

  /*  Check status of color management in CUPS */
  cm_calibrate = cmGetCupsColorCalibrateMode(options, num_options);

  if (cm_calibrate == CM_CALIBRATION_ENABLED)
    cm_disabled = 1;
  else 
    cm_disabled = cmIsPrinterCmDisabled(getenv("PRINTER"));

  if (!cm_disabled)
    cmGetPrinterIccProfile(getenv("PRINTER"), &icc_profile, ppd);

  /* Ghostscript parameters */
  gs_args = cupsArrayNew(NULL, NULL);
  if (!gs_args) {
    fprintf(stderr, "ERROR: Unable to allocate memory for Ghostscript arguments array\n");
    goto out;
  }

  /* Part of Ghostscript command line which is not dependent on the job and/or
     the driver */
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_GHOSTSCRIPT);
  cupsArrayAdd(gs_args, strdup(tmpstr));
  cupsArrayAdd(gs_args, strdup("-dQUIET"));
  /*cupsArrayAdd(gs_args, strdup("-dDEBUG"));*/
  cupsArrayAdd(gs_args, strdup("-dPARANOIDSAFER"));
  cupsArrayAdd(gs_args, strdup("-dNOPAUSE"));
  cupsArrayAdd(gs_args, strdup("-dBATCH"));
  cupsArrayAdd(gs_args, strdup("-dNOINTERPOLATE"));
  cupsArrayAdd(gs_args, strdup("-dNOMEDIAATTRS"));
  if (cm_disabled)
    cupsArrayAdd(gs_args, strdup("-dUseFastColor"));
  cupsArrayAdd(gs_args, strdup("-sstdout=%stderr"));
  cupsArrayAdd(gs_args, strdup("-sOutputFile=%stdout"));

  /* Ghostscript output device */
  if (outformat == OUTPUT_FORMAT_RASTER)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=cups"));
  else if (outformat == OUTPUT_FORMAT_PDF)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=pdfwrite"));
  /* In case of PCL XL output we determine later whether we will have
     to use the "pxlmono" or "pxlcolor" output device */

  /* Special Ghostscript options for PDF output */
  if (outformat == OUTPUT_FORMAT_PDF) {
    cupsArrayAdd(gs_args, strdup("-dCompatibilityLevel=1.3"));
    cupsArrayAdd(gs_args, strdup("-dAutoRotatePages=/None"));
    cupsArrayAdd(gs_args, strdup("-dAutoFilterColorImages=false"));
    cupsArrayAdd(gs_args, strdup("-dNOPLATFONTS"));
    cupsArrayAdd(gs_args, strdup("-dColorImageFilter=/FlateEncode"));
    cupsArrayAdd(gs_args, strdup("-dPDFSETTINGS=/printer"));
    cupsArrayAdd(gs_args, strdup("-dColorConversionStrategy=/LeaveColorUnchanged"));
  }
  
#ifdef HAVE_CUPS_1_7
  if (outformat == OUTPUT_FORMAT_RASTER)
  {
    t = getenv("FINAL_CONTENT_TYPE");
    if (t && strcasestr(t, "pwg"))
      pwgraster = 1;
  }
#endif /* HAVE_CUPS_1_7 */
    
  if (ppd)
  {
    cupsRasterInterpretPPD(&h,ppd,num_options,options,0);
#ifdef HAVE_CUPS_1_7
    if (outformat == OUTPUT_FORMAT_RASTER)
    {
      if ((attr = ppdFindAttr(ppd,"PWGRaster",0)) != 0 &&
	  (!strcasecmp(attr->value, "true") ||
	   !strcasecmp(attr->value, "on") ||
	   !strcasecmp(attr->value, "yes")))
	pwgraster = 1;
      if (pwgraster == 1)
	cupsRasterParseIPPOptions(&h, num_options, options, pwgraster, 0);
    }
#endif /* HAVE_CUPS_1_7 */
    if (outformat == OUTPUT_FORMAT_PXL)
    {
      if ((attr = ppdFindAttr(ppd,"ColorDevice",0)) != 0 &&
	  (!strcasecmp(attr->value, "false") ||
	   !strcasecmp(attr->value, "off") ||
	   !strcasecmp(attr->value, "no")))
	/* Monochrome PCL XL printer, according to PPD */
	pxlcolor = 0;
    }
  }
  else
  {
#ifdef HAVE_CUPS_1_7
    if (outformat == OUTPUT_FORMAT_RASTER)
    {
      pwgraster = 1;
      t = cupsGetOption("media-class", num_options, options);
      if (t == NULL)
	t = cupsGetOption("MediaClass", num_options, options);
      if (t != NULL)
      {
	if (strcasestr(t, "pwg"))
	  pwgraster = 1;
	else
	  pwgraster = 0; 
      }
    }
    cupsRasterParseIPPOptions(&h, num_options, options, pwgraster, 1);
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
  if (doc_type == GS_DOC_TYPE_PDF) {
    parse_pdf_header_options(fp, &h);
  }

  /* fixed other values that pdftopdf handles */
  h.MirrorPrint = CUPS_FALSE;
  h.Orientation = CUPS_ORIENT_0;

  /* get all the data from the header and pass it to ghostscript */
  add_pdf_header_options (&h, gs_args, outformat, pxlcolor);

  /* CUPS font path */
  if ((t = getenv("CUPS_FONTPATH")) == NULL)
    t = CUPS_FONTPATH;
  snprintf(tmpstr, sizeof(tmpstr), "-I%s", t);
  cupsArrayAdd(gs_args, strdup(tmpstr));

  /* set the device output ICC profile */
  if(icc_profile != NULL && icc_profile[0] != '\0') {
    snprintf(tmpstr, sizeof(tmpstr), "-sOutputICCProfile=%s", icc_profile);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  }

  /* Switch to taking PostScript commands on the Ghostscript command line */
  cupsArrayAdd(gs_args, strdup("-c"));

  /* Set margins if we have a bounding box defined */
  if (h.cupsImagingBBox[3] > 0.0) {
    snprintf(tmpstr, sizeof(tmpstr),
	     "<</.HWMargins[%f %f %f %f] /Margins[0 0]>>setpagedevice",
	     h.cupsImagingBBox[0], h.cupsImagingBBox[1],
	     h.cupsPageSize[0] - h.cupsImagingBBox[2],
	     h.cupsPageSize[1] - h.cupsImagingBBox[3]);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  }

  if ((t = cupsGetOption("profile", num_options, options)) != NULL) {
    snprintf(tmpstr, sizeof(tmpstr), "<</cupsProfile(%s)>>setpagedevice", t);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  }

  /* Do we have a "center-of-pixel" command line option or
     "CenterOfPixel" PPD option set to "true"? In this case let
     Ghostscript use the center-of-pixel rule instead of the
     PostScript-standard any-part-of-pixel rule when filling a
     path. This improves the accuracy of graphics (like bar codes for
     example) on low-resolution printers (like label printers with
     typically 203 dpi). See
     https://bugs.linuxfoundation.org/show_bug.cgi?id=1373 */
  if (((t = cupsGetOption("CenterOfPixel", num_options, options)) == NULL &&
       (t = cupsGetOption("center-of-pixel", num_options, options)) == NULL &&
       ppd && (attr = ppdFindAttr(ppd,"DefaultCenterOfPixel", NULL)) != NULL &&
       (!strcasecmp(attr->value, "true") ||
	!strcasecmp(attr->value, "on") ||
	!strcasecmp(attr->value, "yes"))) ||
      (t && (!strcasecmp(t, "true") || !strcasecmp(t, "on") ||
	     !strcasecmp(t, "yes")))) {
    fprintf(stderr, "DEBUG: Ghostscript using Center-of-Pixel method to fill paths.\n");
    cupsArrayAdd(gs_args, strdup("0 .setfilladjust"));
  } else
    fprintf(stderr, "DEBUG: Ghostscript using Any-Part-of-Pixel method to fill paths.\n");

  /* Mark the end of PostScript commands supplied on the Ghostscript command
     line (with the "-c" option), so that we can supply the input file name */
  cupsArrayAdd(gs_args, strdup("-f"));

  /* Let Ghostscript read from STDIN */
  cupsArrayAdd(gs_args, strdup("-_"));

  /* Execute Ghostscript command line ... */
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_GHOSTSCRIPT);

  /* call Ghostscript */
  rewind(fp);
  status = gs_spawn (tmpstr, gs_args, envp, fp);
  if (status != 0) status = 1;
out:
  if (fp)
    fclose(fp);
  if (gs_args) {
    while ((tmp = cupsArrayFirst(gs_args)) != NULL) {
      cupsArrayRemove(gs_args,tmp);
      free(tmp);
    }
    cupsArrayDelete(gs_args);
  }
  free(icc_profile);
  if (ppd)
    ppdClose(ppd);
  return status;
}

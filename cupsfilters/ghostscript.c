/*
 * Ghostscript filter function for cups-filters.
 *
 * Used for PostScript -> PDF, PDF -> Raster, PDF -> PCL-XL
 *
 * Copyright (c) 2008-2020, Till Kamppeter
 * Copyright (c) 2011, Tim Waugh
 * Copyright (c) 2011-2013, Richard Hughes
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <cups/raster.h>
#include <cupsfilters/colormanager.h>
#include <cupsfilters/raster.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "filter.h"
#include "pdf.h"

#define PDF_MAX_CHECK_COMMENT_LINES	20

typedef enum {
  GS_DOC_TYPE_PDF,
  GS_DOC_TYPE_PS,
  GS_DOC_TYPE_EMPTY,
  GS_DOC_TYPE_UNKNOWN
} GsDocType;

#ifdef CUPS_RASTER_SYNCv1
typedef cups_page_header2_t gs_page_header;
#else
typedef cups_page_header_t gs_page_header;
#endif /* CUPS_RASTER_SYNCv1 */

static GsDocType
parse_doc_type(FILE *fp)
{
  char buf[5];
  int is_empty = 1;
  GsDocType type = GS_DOC_TYPE_UNKNOWN;

  /* get the first few bytes of the file */
  rewind(fp);
  /* skip until PDF/PS start header */
  while (fgets(buf,sizeof(buf),fp) != 0) {
    if (is_empty && buf[0] != '\n') is_empty = 0;
    if (strncmp(buf,"%PDF",4) == 0) type = GS_DOC_TYPE_PDF;
    if (strncmp(buf,"%!",2) == 0) type = GS_DOC_TYPE_PS;
  }
  if (is_empty) type = GS_DOC_TYPE_EMPTY;
  rewind(fp);
  return type;
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
		       filter_out_format_t outformat, int pxlcolor)
{
  int i;
  char tmpstr[1024];

  /* Simple boolean, enumerated choice, numerical, and string parameters */
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
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
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER ||
      outformat == OUTPUT_FORMAT_PXL) {
    /* PDF output is only for turning PostScript input data into PDF
       not for sending PDF to a PDF printer (this is done by pdftopdf)
       therefore we do not apply duplex/tumble here. */
    if (h->Duplex) {
      cupsArrayAdd(gs_args, strdup("-dDuplex"));
    }
  }
  snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d",h->HWResolution[0],
	   h->HWResolution[1]);
  cupsArrayAdd(gs_args, strdup(tmpstr));
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
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
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER ||
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
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
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
  snprintf(tmpstr, sizeof(tmpstr), "-dDEVICEWIDTHPOINTS=%d",h->PageSize[0]);
  cupsArrayAdd(gs_args, strdup(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), "-dDEVICEHEIGHTPOINTS=%d",h->PageSize[1]);
  cupsArrayAdd(gs_args, strdup(tmpstr));
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
    if (h->Separations) {
      cupsArrayAdd(gs_args, strdup("-dSeparations"));
    }
    if (h->TraySwitch) {
      cupsArrayAdd(gs_args, strdup("-dTraySwitch"));
    }
  }
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER ||
      outformat == OUTPUT_FORMAT_PXL) {
    /* PDF output is only for turning PostScript input data into PDF
       not for sending PDF to a PDF printer (this is done by pdftopdf)
       therefore we do not apply duplex/tumble here. */
    if (h->Tumble) {
      cupsArrayAdd(gs_args, strdup("-dTumble"));
    }
  }
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
    if (h->cupsMediaType) {
      snprintf(tmpstr, sizeof(tmpstr), "-dcupsMediaType=%d",
	       (unsigned)(h->cupsMediaType));
      cupsArrayAdd(gs_args, strdup(tmpstr));
    }
    snprintf(tmpstr, sizeof(tmpstr), "-dcupsBitsPerColor=%d",
	     h->cupsBitsPerColor);
    cupsArrayAdd(gs_args, strdup(tmpstr));
    snprintf(tmpstr, sizeof(tmpstr), "-dcupsColorOrder=%d", h->cupsColorOrder);
    cupsArrayAdd(gs_args, strdup(tmpstr));
    snprintf(tmpstr, sizeof(tmpstr), "-dcupsColorSpace=%d", h->cupsColorSpace);
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
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
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
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER) {
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
          FILE *fp,
	  int outputfd,
	  filter_logfunc_t log,
	  void *ld,
	  filter_iscanceledfunc_t iscanceled,
	  void *icd)
{
  char *argument;
  char buf[BUFSIZ];
  char **gsargv;
  const char* apos;
  int fds[2], nullfd;
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

  if (log) {
    /* Debug output: Full Ghostscript command line and environment variables */
    snprintf(buf, sizeof(buf), "ghostscript: Ghostscript command line:");
    for (i = 0; gsargv[i]; i ++) {
      if ((strchr(gsargv[i],' ')) || (strchr(gsargv[i],'\t')))
	apos = "'";
      else
	apos = "";
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
	       " %s%s%s", apos, gsargv[i], apos);
    }
    log(ld, FILTER_LOGLEVEL_DEBUG, "%s", buf);

    for (i = 0; envp[i]; i ++)
      log(ld, FILTER_LOGLEVEL_DEBUG,
	  "ghostscript: envp[%d]=\"%s\"", i, envp[i]);
  }

  /* Create a pipe for feeding the job into Ghostscript */
  if (pipe(fds))
  {
    fds[0] = -1;
    fds[1] = -1;
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: Unable to establish stdin pipe for Ghostscript "
		 "call");
    goto out;
  }

  /* Set the "close on exec" flag on each end of the pipe... */
  if (fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);
    fds[0] = -1;
    fds[1] = -1;
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: Unable to set \"close on exec\" flag on read "
		 "end of the stdin pipe for Ghostscript call");
    goto out;
  }
  if (fcntl(fds[1], F_SETFD, fcntl(fds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: Unable to set \"close on exec\" flag on write "
		 "end of the stdin pipe for Ghostscript call");
    goto out;
  }

  if ((pid = fork()) == 0)
  {
    /* Couple pipe with stdin of Ghostscript process */
    if (fds[0] >= 0) {
      if (fds[0] != 0) {
	if (dup2(fds[0], 0) < 0) {
	  if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		       "ghostscript: Unable to couple pipe with stdin of "
		       "Ghostscript process");
	  exit(1);
	}
	close(fds[0]);
      }
      close(fds[1]);
    } else {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: invalid pipe file descriptor to couple with "
		   "stdin of Ghostscript process");
      exit(1);
    }

    /* Send Ghostscript's stderr to the Nirwana, as it does not contain
       anything useful for us */
    if ((nullfd = open("/dev/null", O_RDWR)) > 2)
    {
      dup2(nullfd, 2);
      close(nullfd);
    }
    else
      close(nullfd);
    fcntl(2, F_SETFL, O_NDELAY);

    /* Couple stdout of Ghostscript process */
    if (outputfd >= 1) {
      if (outputfd != 1) {
	if (dup2(outputfd, 1) < 0) {
	  if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		       "ghostscript: Unable to couple stdout of Ghostscript "
		       "process");
	  exit(1);
	}
	close(outputfd);
      }
    } else {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: Invalid file descriptor to couple with "
		   "stdout of Ghostscript process");
      exit(1);
    }

    /* Execute Ghostscript command line ... */
    execvpe(filename, gsargv, envp);
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: Unable to launch Ghostscript: %s: %s", filename,
		 strerror(errno));
    exit(1);
  }
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "ghostscript: Started Ghostscript (PID %d)", pid);

  close(fds[0]);

  /* Feed job data into Ghostscript */
  while ((!iscanceled || !iscanceled(icd)) &&
	 (n = fread(buf, 1, BUFSIZ, fp)) > 0) {
    int count;
  retry_write:
    count = write(fds[1], buf, n);
    if (count != n) {
      if (count == -1) {
        if (errno == EINTR)
          goto retry_write;
	if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		     "ghostscript: write failed: %s", strerror(errno));
      }
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: Can't feed job data into Ghostscript");
      goto out;
    }
  }
  close (fds[1]);
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "ghostscript: Input data feed completed");

 retry_wait:
  if (waitpid (pid, &wstatus, 0) == -1) {
    if (errno == EINTR)
      goto retry_wait;
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "ghostscript: Ghostscript (PID %d) stopped with an error: %s!",
		 pid, strerror(errno));
    goto out;
  }
  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "ghostscript: Ghostscript (PID %d) exited with no errors.",
	       pid);

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
get_ppd_icc_fallback (ppd_file_t *ppd,
		      char **qualifier,
		      filter_logfunc_t log,
		      void *ld)
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
    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "ghostscript: no profiles specified in PPD");
    goto out;
  }

  if ((datadir = getenv("CUPS_DATADIR")) == NULL)
    datadir = CUPS_DATADIR;

  /* try to find a profile that matches the qualifier exactly */
  for (;attr != NULL; attr = ppdFindNextAttr(ppd, profile_key, NULL)) {
    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "ghostscript: found profile %s in PPD with qualifier '%s'",
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
      if (log) log(ld, FILTER_LOGLEVEL_INFO,
		   "ghostscript: found profile %s in PPD that does not exist",
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
    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "ghostscript: no profiles in PPD for qualifier '%s'",
		 qualifer_tmp);
    goto out;
  }

out:
  return icc_profile;
}
#endif /* 0 */

/*
 * 'ghostscript()' - Filter function to use Ghostscript for print
 *                   data conversions
 */

int                              /* O - Error status */
ghostscript(int inputfd,         /* I - File descriptor input stream */
	    int outputfd,        /* I - File descriptor output stream */
	    int inputseekable,   /* I - Is input stream seekable? */
	    filter_data_t *data, /* I - Job and printer data */
	    void *parameters)    /* I - Filter-specific parameters */
{
  filter_out_format_t outformat;
  char buf[BUFSIZ];
  char *filename;
  char *icc_profile = NULL;
  /*char **qualifier = NULL;*/
  char *tmp;
  char tmpstr[1024],
       tempfile[1024];
  const char *t = NULL;
  char *envp[3];
  int num_env = 0;
  cups_array_t *gs_args = NULL;
  cups_option_t *options = NULL;
  FILE *fp = NULL;
  GsDocType doc_type;
  gs_page_header h;
  int bytes;
  int fd;
  int cm_disabled;
  int i;
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
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  if (parameters) {
    outformat = *(filter_out_format_t *)parameters;
    if (outformat != OUTPUT_FORMAT_PDF &&
	outformat != OUTPUT_FORMAT_CUPS_RASTER &&
	outformat != OUTPUT_FORMAT_PWG_RASTER &&
	outformat != OUTPUT_FORMAT_PXL)
      outformat = OUTPUT_FORMAT_PWG_RASTER;
  } else
    outformat = OUTPUT_FORMAT_PWG_RASTER;

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "ghostscript: Output format: %s",
	       (outformat == OUTPUT_FORMAT_CUPS_RASTER ? "CUPS Raster" :
		(outformat == OUTPUT_FORMAT_PWG_RASTER ? "PWG Raster" :
		 (outformat == OUTPUT_FORMAT_PDF ? "PDF" :
		  "PCL XL"))));
  
  memset(&sa, 0, sizeof(sa));
  /* Ignore SIGPIPE and have write return an error instead */
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);

 /*
  * CUPS option list
  */

  num_options = data->num_options;
  options = data->options;

  ppd = data->ppd;

 /*
  * Environment variables for Ghostscript call ...
  */

  if (data->ppdfile)
  {
    snprintf(tmpstr, sizeof(tmpstr), "PPD=%s", data->ppdfile);
    envp[num_env] = strdup(tmpstr);
    num_env ++;
  }

  if ((t = getenv("RIP_MAX_CACHE")) != NULL)
  {
    snprintf(tmpstr, sizeof(tmpstr), "RIP_MAX_CACHE=%s", t);
    envp[num_env] = strdup(tmpstr);
    num_env ++;
  }

  envp[num_env] = NULL;

 /*
  * Open the input data stream specified by the inputfd ...
  */

  if ((fp = fdopen(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "ghostscript: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Find out file type ...
  */

  if (inputseekable)
    doc_type = parse_doc_type(fp);

 /*
  * Copy input into temporary file if needed ...
  * (If the input is not seekable or if it is PostScript, to be able
  *  to count the pages)
  */

  if (!inputseekable || doc_type == GS_DOC_TYPE_PS) {
    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: Unable to copy PDF file: %s", strerror(errno));
      return (1);
    }

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "ghostscript: Copying input to temp file \"%s\"",
		 tempfile);

    while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0)
      bytes = write(fd, buf, bytes);

    fclose(fp);
    close(fd);

    filename = tempfile;

   /*
    * Open the temporary file to read it instead of the original input ...
    */

    if ((fp = fopen(filename, "r")) == NULL)
    {
      if (!iscanceled || !iscanceled(icd))
      {
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "ghostscript: Unable to open temporary file.");
      }

      goto out;
    }
  } else
    filename = NULL;

  if (!inputseekable)
    doc_type = parse_doc_type(fp);

  if (doc_type == GS_DOC_TYPE_EMPTY) {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "ghostscript: Input is empty, outputting empty file.");
    status = 0;
    if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
	outformat == OUTPUT_FORMAT_PWG_RASTER)
      fprintf(stdout, "RaS2");
    goto out;
  } if (doc_type == GS_DOC_TYPE_UNKNOWN) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: Can't detect file type");
    goto out;
  }

  if (doc_type == GS_DOC_TYPE_PDF) {
    int pages = pdf_pages_fp(fp);

    if (pages == 0) {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "ghostscript: No pages left, outputting empty file.");
      status = 0;
      if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
	  outformat == OUTPUT_FORMAT_PWG_RASTER)
        fprintf(stdout, "RaS2");
      goto out;
    }
    if (pages < 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: Unexpected page count");
      goto out;
    }
  } else {
    char gscommand[65536];
    char output[31] = "";
    int pagecount;
    size_t bytes;
    snprintf(gscommand, 65536,
	     "%s -q -dNOPAUSE -dBATCH -sDEVICE=bbox %s 2>&1 | "
	     "grep -c HiResBoundingBox",
	     CUPS_GHOSTSCRIPT, filename);

    FILE *pd = popen(gscommand, "r");
    if (!pd) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: Failed to execute ghostscript to determine "
		   "number of input pages!");
      goto out;
    }

    bytes = fread(output, 1, 31, pd);
    pclose(pd);

    if (bytes <= 0 || sscanf(output, "%d", &pagecount) < 1)
      pagecount = -1;

    if (pagecount == 0) {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "ghostscript: No pages left, outputting empty file.");
      status = 0;
      if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
	  outformat == OUTPUT_FORMAT_PWG_RASTER)
        fprintf(stdout, "RaS2");
      goto out;
    }
    if (pagecount < 0) {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "ghostscript: Unexpected page count");
      goto out;
    }
  }
  if (filename) {
    /* Remove name of temp file*/
    unlink(filename);
    filename = NULL;
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
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: Unable to allocate memory for Ghostscript "
		 "arguments array");
    goto out;
  }

  /* Part of Ghostscript command line which is not dependent on the job and/or
     the driver */
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_GHOSTSCRIPT);
  cupsArrayAdd(gs_args, strdup(tmpstr));
  cupsArrayAdd(gs_args, strdup("-dQUIET"));
  /*cupsArrayAdd(gs_args, strdup("-dDEBUG"));*/
  cupsArrayAdd(gs_args, strdup("-dSAFER"));
  cupsArrayAdd(gs_args, strdup("-dNOPAUSE"));
  cupsArrayAdd(gs_args, strdup("-dBATCH"));
  cupsArrayAdd(gs_args, strdup("-dNOINTERPOLATE"));
  cupsArrayAdd(gs_args, strdup("-dNOMEDIAATTRS"));
  if (cm_disabled)
    cupsArrayAdd(gs_args, strdup("-dUseFastColor"));
  if (doc_type == GS_DOC_TYPE_PDF)
    cupsArrayAdd(gs_args, strdup("-dShowAcroForm"));
  cupsArrayAdd(gs_args, strdup("-sstdout=%stderr"));
  cupsArrayAdd(gs_args, strdup("-sOutputFile=%stdout"));

  /* Ghostscript output device */
  if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
      outformat == OUTPUT_FORMAT_PWG_RASTER)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=cups"));
  else if (outformat == OUTPUT_FORMAT_PDF)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=pdfwrite"));
  /* In case of PCL XL output we determine later whether we will have
     to use the "pxlmono" or "pxlcolor" output device */

  /* Special Ghostscript options for PDF output */
  if (outformat == OUTPUT_FORMAT_PDF) {
    /* If we output PDF we are running as a PostScript-to-PDF filter
       for incoming PostScript jobs. If the client embeds a command
       for multiple copies in the PostScript job instead of using the
       CUPS argument for the number of copies, we need to run
       Ghostscript with the "-dDoNumCopies" option so that it respects
       the embedded command for the number of copies.

       We always supply this option if the number of copies CUPS got
       told about is 1, as this is the case if a client sets the
       number of copies as embedded PostScript command, and it is also
       not doing the wrong thing if the command is missing when the
       client only wants a single copy, independent how the client
       actually triggers multiple copies. If the CUPS arguments tells
       us that the clients wants more than one copy we do not supply
       "-dDoNumCopies" as the client does the right, modern CUPS way,
       and if the client got a "dirty" PostScript file with an
       embedded multi-copy setting, he does not get unwished copies.
       also a buggy client supplying the number of copies both via
       PostScript and CUPS will not cause an unwished number of copies
       this way.

       See https://github.com/OpenPrinting/cups-filters/issues/255

       This was already correctly implemented in the former pdftops
       shell-script-based filter but overlooked when the filter's
       functionality got folded into this gstoraster.c filter. It was
       not seen for long time as clients sending PostScript jobs with
       embedded number of copies are rare. */
    if (data->copies <= 1)
      cupsArrayAdd(gs_args, strdup("-dDoNumCopies"));

    cupsArrayAdd(gs_args, strdup("-dCompatibilityLevel=1.3"));
    cupsArrayAdd(gs_args, strdup("-dAutoRotatePages=/None"));
    cupsArrayAdd(gs_args, strdup("-dAutoFilterColorImages=false"));
    cupsArrayAdd(gs_args, strdup("-dNOPLATFONTS"));
    cupsArrayAdd(gs_args, strdup("-dColorImageFilter=/FlateEncode"));
    cupsArrayAdd(gs_args, strdup("-dPDFSETTINGS=/default"));
    cupsArrayAdd(gs_args,
		 strdup("-dColorConversionStrategy=/LeaveColorUnchanged"));
  }
  
#ifdef HAVE_CUPS_1_7
  if (outformat == OUTPUT_FORMAT_PWG_RASTER)
    pwgraster = 1;
#endif /* HAVE_CUPS_1_7 */
    
  if (ppd)
  {
    ppdRasterInterpretPPD(&h, ppd, num_options, options, 0);
#ifdef HAVE_CUPS_1_7
    if (outformat == OUTPUT_FORMAT_CUPS_RASTER ||
	outformat == OUTPUT_FORMAT_PWG_RASTER)
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
    if (outformat == OUTPUT_FORMAT_CUPS_RASTER)
    {
      pwgraster = 0;
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
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "ghostscript: No PPD file specified.");
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

  /* Set margins if we have a bounding box defined and output format
     is not PDF, as PDF output we have only in the PostScript-to-PDF
     filtering case which happens for converting PostScript input
     files before pdftopdf so margins will be handled later, whereas
     the other output formats for PDF-to-something filtering after
     pdftopdf, to format the pages for the printer, so margins are
     important. */
  if (h.cupsImagingBBox[3] > 0.0 && outformat != OUTPUT_FORMAT_PDF) {
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
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "ghostscript: Ghostscript using Center-of-Pixel method to "
		 "fill paths.");
    cupsArrayAdd(gs_args, strdup("0 0 .setfilladjust2"));
  } else
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "ghostscript: Ghostscript using Any-Part-of-Pixel method to "
		 "fill paths.");

  /* Mark the end of PostScript commands supplied on the Ghostscript command
     line (with the "-c" option), so that we can supply the input file name */
  cupsArrayAdd(gs_args, strdup("-f"));

  /* Let Ghostscript read from stdin */
  cupsArrayAdd(gs_args, strdup("-_"));

  /* Execute Ghostscript command line ... */
  snprintf(tmpstr, sizeof(tmpstr), "%s", CUPS_GHOSTSCRIPT);

  /* call Ghostscript */
  rewind(fp);
  status = gs_spawn (tmpstr, gs_args, envp, fp, outputfd, log, ld,
		     iscanceled, icd);
  if (status != 0) status = 1;
out:
  for (i = 0; envp[i]; i ++)
    free(envp[i]);
  if (fp)
    fclose(fp);
  if (filename)
    /* Remove name of temp file*/
    unlink(filename);
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
  close(outputfd);
  return status;
}

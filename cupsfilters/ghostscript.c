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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
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

typedef enum gs_doc_e {
  GS_DOC_TYPE_PDF,
  GS_DOC_TYPE_PS,
  GS_DOC_TYPE_EMPTY,
  GS_DOC_TYPE_UNKNOWN
} gs_doc_t;

#ifdef CUPS_RASTER_SYNCv1
typedef cups_page_header2_t gs_page_header;
#else
typedef cups_page_header_t gs_page_header;
#endif /* CUPS_RASTER_SYNCv1 */

static gs_doc_t
parse_doc_type(FILE *fp)
{
  char buf[5];
  int is_empty = 1;
  gs_doc_t type = GS_DOC_TYPE_UNKNOWN;

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
		       cf_filter_out_format_t outformat, int pxlcolor)
{
  int i;
  char tmpstr[1024];

  /* Simple boolean, enumerated choice, numerical, and string parameters */
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
    if (outformat != CF_FILTER_OUT_FORMAT_APPLE_RASTER &&
	(h->MediaClass[0] |= '\0')) {
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
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PXL) {
    /* PDF output is only for turning PostScript input data into PDF
       not for sending PDF to a PDF printer (this is done by pdftopdf)
       therefore we do not apply duplex/tumble here. */
    if (h->Duplex) {
      cupsArrayAdd(gs_args, strdup("-dDuplex"));
    }
  }
  if (outformat != CF_FILTER_OUT_FORMAT_PCLM) {
    /* In PCLM we have our own method to generate the needed
       resolution, to respect the printer's supported resolutions for
       PCLm, so this is only for non-PCLm output formats */
    snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d",
	     h->HWResolution[0], h->HWResolution[1]);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  }
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
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
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PXL) {
    if (h->MediaPosition) {
      int mediapos;
      if (outformat == CF_FILTER_OUT_FORMAT_PXL) {
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
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
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
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
    if (h->Separations) {
      cupsArrayAdd(gs_args, strdup("-dSeparations"));
    }
    if (h->TraySwitch) {
      cupsArrayAdd(gs_args, strdup("-dTraySwitch"));
    }
  }
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PXL) {
    /* PDF output is only for turning PostScript input data into PDF
       not for sending PDF to a PDF printer (this is done by pdftopdf)
       therefore we do not apply duplex/tumble here. */
    if (h->Tumble) {
      cupsArrayAdd(gs_args, strdup("-dTumble"));
    }
  }
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
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
  
  if (outformat == CF_FILTER_OUT_FORMAT_PXL) {
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
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
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
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER) {
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
	  cf_logfunc_t log,
	  void *ld,
	  cf_filter_iscanceledfunc_t iscanceled,
	  void *icd)
{
  char *argument;
  char buf[BUFSIZ];
  char **gsargv;
  const char* apos;
  int infds[2], errfds[2];
  int i;
  int n;
  int numargs;
  int pid, gspid, errpid;
  cups_file_t *logfp;
  cf_loglevel_t log_level;
  char *msg;
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
    snprintf(buf, sizeof(buf),
	     "cfFilterGhostscript: Ghostscript command line:");
    for (i = 0; gsargv[i]; i ++) {
      if ((strchr(gsargv[i],' ')) || (strchr(gsargv[i],'\t')))
	apos = "'";
      else
	apos = "";
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
	       " %s%s%s", apos, gsargv[i], apos);
    }
    log(ld, CF_LOGLEVEL_DEBUG, "%s", buf);

    for (i = 0; envp[i]; i ++)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "cfFilterGhostscript: envp[%d]=\"%s\"", i, envp[i]);
  }

  /* Create a pipe for feeding the job into Ghostscript */
  if (pipe(infds))
  {
    infds[0] = -1;
    infds[1] = -1;
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to establish stdin pipe for Ghostscript call");
    goto out;
  }

  /* Create a pipe for stderr output of Ghostscript */
  if (pipe(errfds))
  {
    errfds[0] = -1;
    errfds[1] = -1;
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to establish stderr pipe for Ghostscript call");
    goto out;
  }

  /* Set the "close on exec" flag on each end of the pipes... */
  if (fcntl(infds[0], F_SETFD, fcntl(infds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(infds[0]);
    close(infds[1]);
    infds[0] = -1;
    infds[1] = -1;
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to set \"close on exec\" flag on read end of the stdin pipe for Ghostscript call");
    goto out;
  }
  if (fcntl(infds[1], F_SETFD, fcntl(infds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(infds[0]);
    close(infds[1]);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to set \"close on exec\" flag on write end of the stdin pipe for Ghostscript call");
    goto out;
  }
  if (fcntl(errfds[0], F_SETFD, fcntl(errfds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(errfds[0]);
    close(errfds[1]);
    errfds[0] = -1;
    errfds[1] = -1;
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to set \"close on exec\" flag on read end of the stderr pipe for Ghostscript call");
    goto out;
  }
  if (fcntl(errfds[1], F_SETFD, fcntl(errfds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(errfds[0]);
    close(errfds[1]);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to set \"close on exec\" flag on write end of the stderr pipe for Ghostscript call");
    goto out;
  }

  if ((gspid = fork()) == 0)
  {
    /* Couple infds pipe with stdin of Ghostscript process */
    if (infds[0] >= 0) {
      if (infds[0] != 0) {
	if (dup2(infds[0], 0) < 0) {
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterGhostscript: Unable to couple pipe with stdin of Ghostscript process");
	  exit(1);
	}
	close(infds[0]);
      }
      close(infds[1]);
    } else {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterGhostscript: invalid pipe file descriptor to couple with stdin of Ghostscript process");
      exit(1);
    }

    /* Couple errfds pipe with stdin of Ghostscript process */
    if (errfds[1] >= 2) {
      if (errfds[1] != 2) {
	if (dup2(errfds[1], 2) < 0) {
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterGhostscript: Unable to couple pipe with stderr of Ghostscript process");
	  exit(1);
	}
	close(errfds[1]);
      }
      close(errfds[0]);
    } else {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterGhostscript: invalid pipe file descriptor to couple with stderr of Ghostscript process");
      exit(1);
    }

    /* Couple stdout of Ghostscript process */
    if (outputfd >= 1) {
      if (outputfd != 1) {
	if (dup2(outputfd, 1) < 0) {
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterGhostscript: Unable to couple stdout of Ghostscript process");
	  exit(1);
	}
	close(outputfd);
      }
    } else {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterGhostscript: Invalid file descriptor to couple with stdout of Ghostscript process");
      exit(1);
    }

    /* Execute Ghostscript command line ... */
    execvpe(filename, gsargv, envp);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to launch Ghostscript: %s: %s",
		 filename, strerror(errno));
    exit(1);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterGhostscript: Started Ghostscript (PID %d)", gspid);

  close(infds[0]);
  close(errfds[1]);

  if ((errpid = fork()) == 0)
  {
    close(infds[1]);
    logfp = cupsFileOpenFd(errfds[0], "r");
    while (cupsFileGets(logfp, buf, sizeof(buf)))
      if (log) {
	if (strncmp(buf, "DEBUG: ", 7) == 0) {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 7;
	} else if (strncmp(buf, "DEBUG2: ", 8) == 0) {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 8;
	} else if (strncmp(buf, "INFO: ", 6) == 0) {
	  log_level = CF_LOGLEVEL_INFO;
	  msg = buf + 6;
	} else if (strncmp(buf, "WARNING: ", 9) == 0) {
	  log_level = CF_LOGLEVEL_WARN;
	  msg = buf + 9;
	} else if (strncmp(buf, "ERROR: ", 7) == 0) {
	  log_level = CF_LOGLEVEL_ERROR;
	  msg = buf + 7;
	} else {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf;
	}
	log(ld, log_level, "cfFilterGhostscript: %s", msg);
      }
    cupsFileClose(logfp);
    /* No need to close the fd errfds[0], as cupsFileClose(fp) does this
       already */
    /* Ignore errors of the logging process */
    exit(0);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterGhostscript: Started logging (PID %d)", errpid);

  close(errfds[0]);

  /* Feed job data into Ghostscript */
  while ((!iscanceled || !iscanceled(icd)) &&
	 (n = fread(buf, 1, BUFSIZ, fp)) > 0) {
    int count;
  retry_write:
    count = write(infds[1], buf, n);
    if (count != n) {
      if (count == -1) {
        if (errno == EINTR)
          goto retry_write;
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: write failed: %s", strerror(errno));
      }
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterGhostscript: Can't feed job data into Ghostscript");
      goto out;
    }
  }
  close (infds[1]);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterGhostscript: Input data feed completed");

  while (gspid > 0 || errpid > 0) {
    if ((pid = wait(&wstatus)) < 0) {
      if (errno == EINTR && iscanceled && iscanceled(icd)) {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterGhostscript: Job canceled, killing Ghostscript ...");
	kill(gspid, SIGTERM);
	gspid = -1;
	kill(errpid, SIGTERM);
	errpid = -1;
	break;
      } else
	continue;
    }

    /* How did the filter terminate */
    if (wstatus) {
      if (WIFEXITED(wstatus)) {
	/* Via exit() anywhere or return() in the main() function */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: %s (PID %d) stopped with status %d",
		     (pid == gspid ? "Ghostscript" : "Logging"), pid,
		     WEXITSTATUS(wstatus));
	status = WEXITSTATUS(wstatus);
      } else {
	/* Via signal */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: %s (PID %d) crashed on signal %d",
		     (pid == gspid ? "Ghostscript" : "Logging"), pid,
		     WTERMSIG(wstatus));
	status = 256 * WTERMSIG(wstatus);
      }
    } else {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterGhostscript: %s (PID %d) exited with no errors.",
		   (pid == gspid ? "Ghostscript" : "Logging"), pid);
      status = 0;
    }
    if (pid == gspid)
      gspid = -1;
    else  if (pid == errpid)
      errpid = -1;
  }

out:
  free(gsargv);
  return status;
}

/*
 * 'cfFilterGhostscript()' - Filter function to use Ghostscript for print
 *                           data conversions
 */

int                                         /* O - Error status */
cfFilterGhostscript(int inputfd,            /* I - File descriptor input
					           stream */
		    int outputfd,           /* I - File descriptor output
					           stream */
		    int inputseekable,      /* I - Is input stream seekable? */
		    cf_filter_data_t *data, /* I - Job and printer data */
		    void *parameters)       /* I - Filter-specific parameters */
{
  cf_filter_out_format_t outformat;
  char buf[BUFSIZ];
  char *filename;
  char *icc_profile = NULL;
  char *tmp;
  char tmpstr[1024],
       tempfile[1024];
  const char *t = NULL;
  char *envp[4];
  int num_env = 0;
  cups_array_t *gs_args = NULL;
  cups_option_t *options = NULL;
  FILE *fp = NULL;
  gs_doc_t doc_type;
  gs_page_header h;
  cups_cspace_t cspace = -1;
  int bytes;
  int fd;
  int cm_disabled;
  int i;
  int num_options;
  int status = 1;
  ppd_file_t *ppd = NULL;
  ipp_t *printer_attrs = data->printer_attrs;
  ipp_t *job_attrs = data->job_attrs;
  struct sigaction sa;
  cf_cm_calibration_t cm_calibrate;
  int pxlcolor = 0; /* 1 if printer is color printer otherwise 0. */
  ppd_attr_t *attr;
  ipp_attribute_t *ipp_attr;
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  /* Note: With the CF_FILTER_OUT_FORMAT_APPLE_RASTER selection and a
     Ghostscript version without "appleraster" output device (9.55.x
     and older) the output is actually CUPS Raster but information
     about available color spaces and depths is taken from the
     urf-supported printer IPP attribute or appropriate PPD file
     attribute. This mode is for further processing with
     rastertopwg. With Ghostscript supporting Apple Raster output
     (9.56.0 and newer), we actually produce Apple Raster and no
     further filter is required. */

  if (parameters) {
    outformat = *(cf_filter_out_format_t *)parameters;
    if (outformat != CF_FILTER_OUT_FORMAT_PDF &&
	outformat != CF_FILTER_OUT_FORMAT_PDF_IMAGE &&
	outformat != CF_FILTER_OUT_FORMAT_PCLM &&
	outformat != CF_FILTER_OUT_FORMAT_CUPS_RASTER &&
	outformat != CF_FILTER_OUT_FORMAT_PWG_RASTER &&
	outformat != CF_FILTER_OUT_FORMAT_APPLE_RASTER &&
	outformat != CF_FILTER_OUT_FORMAT_PXL)
      outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
  } else
    outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterGhostscript: Output format: %s",
	       (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ? "CUPS Raster" :
		(outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ? "PWG Raster" :
		 (outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER ?
		  "Apple Raster" :
		  (outformat == CF_FILTER_OUT_FORMAT_PDF ? "PDF" :
		   (outformat == CF_FILTER_OUT_FORMAT_PDF_IMAGE ?
		    "raster-only PDF" :
		    (outformat == CF_FILTER_OUT_FORMAT_PCLM ? "PCLm" :
		     "PCL XL")))))));
  
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

  if ((t = getenv("LD_LIBRARY_PATH")) != NULL)
  {
    snprintf(tmpstr, sizeof(tmpstr), "LD_LIBRARY_PATH=%s", t);
    envp[num_env] = strdup(tmpstr);
    num_env ++;
  }

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
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterGhostscript: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Streaming mode without pre-checking input format or zero-page jobs
  */

  if ((t = cupsGetOption("filter-streaming-mode", num_options, options)) ==
       NULL ||
      (!strcasecmp(t, "false") || !strcasecmp(t, "off") ||
       !strcasecmp(t, "no")))
  {

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
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: Unable to copy PDF file: %s", strerror(errno));
	fclose(fp);
	return (1);
      }

      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterGhostscript: Copying input to temp file \"%s\"",
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
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterGhostscript: Unable to open temporary file.");
	}

	goto out;
      }
    } else
      filename = NULL;

    if (!inputseekable)
      doc_type = parse_doc_type(fp);

    if (doc_type == GS_DOC_TYPE_EMPTY) {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterGhostscript: Input is empty, outputting empty file.");
      status = 0;
      if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
	  outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
	  outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)
	if (write(outputfd, "RaS2", 4)) {};
      goto out;
    } if (doc_type == GS_DOC_TYPE_UNKNOWN) {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterGhostscript: Can't detect file type");
      goto out;
    }

    if (doc_type == GS_DOC_TYPE_PDF) {
      int pages = cfPDFPagesFP(fp);

      if (pages == 0) {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterGhostscript: No pages left, outputting empty file.");
	status = 0;
	if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
	    outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
	    outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)
	  if (write(outputfd, "RaS2", 4)) {};
	goto out;
      }
      if (pages < 0) {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: Unexpected page count");
	goto out;
      }
    } else {
      char gscommand[65536];
      char output[31] = "";
      int pagecount;
      size_t bytes;
      /* Ghostscript runs too long on files converted from djvu files */
      /* Using -dDEVICEWIDTHPOINTS -dDEVICEHEIGHTPOINTS params solves the
	 problem */
      snprintf(gscommand, 65536, "%s -q -dNOPAUSE -dBATCH -sDEVICE=bbox -dDEVICEWIDTHPOINTS=1 -dDEVICEHEIGHTPOINTS=1 %s 2>&1 | grep -c HiResBoundingBox",
	       CUPS_GHOSTSCRIPT, filename);
    
      FILE *pd = popen(gscommand, "r");
      if (!pd) {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: Failed to execute ghostscript to determine "
		     "number of input pages!");
	goto out;
      }

      bytes = fread(output, 1, 31, pd);
      pclose(pd);

      if (bytes <= 0 || sscanf(output, "%d", &pagecount) < 1)
	pagecount = -1;

      if (pagecount == 0) {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterGhostscript: No pages left, outputting empty file.");
	status = 0;
	if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
	    outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
	    outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)
	  if (write(outputfd, "RaS2", 4)) {};
	goto out;
      }
      if (pagecount < 0) {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterGhostscript: Unexpected page count");
	goto out;
      }
    }

    if (filename) {
      /* Remove name of temp file*/
      unlink(filename);
      filename = NULL;
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterGhostscript: Input format: %s",
		 (doc_type == GS_DOC_TYPE_PDF ? "PDF" :
		  (doc_type == GS_DOC_TYPE_PS ? "PostScript" :
		   (doc_type == GS_DOC_TYPE_EMPTY ? "Empty file" :
		    "Unknown"))));
  }
  else
  {
    doc_type = GS_DOC_TYPE_UNKNOWN;
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterGhostscript: Input format: Not determined");
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterGhostscript: Streaming mode, no checks for input format, zero-page input, instructions from previous filter");
  }

  /* Find print-rendering-intent */
  cfGetPrintRenderIntent(data, &h);
  if(log) log(ld, CF_LOGLEVEL_DEBUG,
	      "Print rendering intent = %s", h.cupsRenderingIntent);

  /*  Check status of color management in CUPS */
  cm_calibrate = cfCmGetCupsColorCalibrateMode(data, options, num_options);

  if (cm_calibrate == CF_CM_CALIBRATION_ENABLED)
    cm_disabled = 1;
  else 
    cm_disabled = cfCmIsPrinterCmDisabled(data);

  if (!cm_disabled)
    cfCmGetPrinterIccProfile(data, &icc_profile, ppd);

  /* Ghostscript parameters */
  gs_args = cupsArrayNew(NULL, NULL);
  if (!gs_args) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterGhostscript: Unable to allocate memory for Ghostscript arguments array");
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
  else
    cupsArrayAdd(gs_args, strdup("-dUsePDFX3Profile"));
  cupsArrayAdd(gs_args, strdup("-sstdout=%stderr"));
  cupsArrayAdd(gs_args, strdup("-sOutputFile=%stdout"));

  /* Ghostscript output device */
  if (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=cups"));
  else if (outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=appleraster"));
  else if (outformat == CF_FILTER_OUT_FORMAT_PDF)
    cupsArrayAdd(gs_args, strdup("-sDEVICE=pdfwrite"));
  /* In case of PCL XL, raster-obly PDF, or PCLm output we determine
     the exact output device later */

  /* Special Ghostscript options for PDF output */
  if (outformat == CF_FILTER_OUT_FORMAT_PDF) {
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

    cupsArrayAdd(gs_args, strdup("-dShowAcroForm"));
    cupsArrayAdd(gs_args, strdup("-dCompatibilityLevel=1.3"));
    cupsArrayAdd(gs_args, strdup("-dAutoRotatePages=/None"));
    cupsArrayAdd(gs_args, strdup("-dAutoFilterColorImages=false"));
    cupsArrayAdd(gs_args, strdup("-dNOPLATFONTS"));
    cupsArrayAdd(gs_args, strdup("-dColorImageFilter=/FlateEncode"));
    cupsArrayAdd(gs_args, strdup("-dPDFSETTINGS=/default"));
    cupsArrayAdd(gs_args,
		 strdup("-dColorConversionStrategy=/LeaveColorUnchanged"));
  }

  cspace = icc_profile ? CUPS_CSPACE_RGB : -1;
  cfRasterPrepareHeader(&h, data, outformat, outformat, 0, &cspace);

  /* Special Ghostscript options for raster-only PDF output */

  /* We use PCLm instead of general raster PDF here if the printer
     supports it, as PCLm can get streamed by the printer */

  /* Note that these output formats require Ghostscript 9.55.0 or later */

  if (outformat == CF_FILTER_OUT_FORMAT_PDF_IMAGE ||
      outformat == CF_FILTER_OUT_FORMAT_PCLM) {
    int res_x, res_y,
        sup_res_x, sup_res_y,
        best_res_x = 0, best_res_y = 0,
        res_diff,
        best_res_diff = INT_MAX,
        n;
    const char *res_str = NULL;
    char c;

    ipp_attr = NULL;
    attr = NULL;
    if (outformat == CF_FILTER_OUT_FORMAT_PCLM || /* PCLm forced */
	/* PCLm supported according to printer IPP attributes */
	(printer_attrs &&
	 (ipp_attr =
	  ippFindAttribute(printer_attrs, "pclm-source-resolution-supported",
			   IPP_TAG_ZERO)) != NULL) ||
	/* PCLm supported according to PPD file */
	(ppd &&
	 (attr =
	  ppdFindAttr(ppd, "cupsPclmSourceResolutionSupported", 0)) != NULL)) {

      outformat = CF_FILTER_OUT_FORMAT_PCLM;

      /* Resolution */

      /* Check whether the job's resolution is supported pn PCLm mode and
         correct if needed */
      res_x = h.HWResolution[0];
      res_y = h.HWResolution[1];
      if (attr)
	res_str = attr->value;
      else if (ipp_attr) {
	ippAttributeString(ipp_attr, tmpstr, sizeof(tmpstr));
	res_str = tmpstr;
      }
      if (res_str)
	while ((n = sscanf(res_str, "%d%c%d",
			   &sup_res_x, &c, &sup_res_y)) > 0) {
	  if (n < 3 || (c != 'x' && c != 'X'))
	    sup_res_y = sup_res_x;
	  if (sup_res_x > 0 && sup_res_y > 0) {
	    if (res_x == sup_res_x && res_y == sup_res_y) {
	      best_res_x = res_x;
	      best_res_y = res_y;
	      break;
	    } else {
	      res_diff = (res_x * res_y) / (sup_res_x * sup_res_y);
	      if (res_diff < 1)
		res_diff = (sup_res_x * sup_res_y) / (res_x * res_y);
	      if (res_diff <= best_res_diff) {
		best_res_x = sup_res_x;
		best_res_y = sup_res_y;
	      }
	    }
	  }
	  res_str = strchr(res_str, ',');
	  if (res_str == NULL)
	    break;
	}
      if (best_res_x > 0 && best_res_y > 0) {
	snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d", best_res_x, best_res_y);
	cupsArrayAdd(gs_args, strdup(tmpstr));
      } else if ((printer_attrs &&
		  (ipp_attr =
		   ippFindAttribute(printer_attrs,
				    "pclm-source-resolution-default",
				    IPP_TAG_ZERO)) != NULL) ||
		 (ppd &&
		  (attr =
		   ppdFindAttr(ppd,
			       "cupsPclmSourceResolutionDefault", 0)) != NULL)){
	if (attr)
	  res_str = attr->value;
	else if (ipp_attr) {
	  ippAttributeString(ipp_attr, tmpstr, sizeof(tmpstr));
	  res_str = tmpstr;
	}
	if (res_str)
	  if ((n = sscanf(res_str, "%d%c%d",
			  &best_res_x, &c, &best_res_y)) > 0) {
	    if (n < 3 || (c != 'x' && c != 'X'))
	      best_res_y = best_res_x;
	    if (best_res_x > 0 && best_res_y > 0) {
	      snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d",
		       best_res_x, best_res_y);
	      cupsArrayAdd(gs_args, strdup(tmpstr));
	    }
	  }
      }
      if (best_res_x <= 0 || best_res_y <= 0) {
	snprintf(tmpstr, sizeof(tmpstr), "-r%dx%d", res_x, res_y);
	cupsArrayAdd(gs_args, strdup(tmpstr));
      }

      /* Ghostscript output device */

      if (h.cupsColorSpace == CUPS_CSPACE_SW)
	cupsArrayAdd(gs_args, strdup("-sDEVICE=pclm8"));
      else
	cupsArrayAdd(gs_args, strdup("-sDEVICE=pclm"));

      /* Strip/Band Height */

      n = 0;
      if ((printer_attrs &&
	   (ipp_attr =
	    ippFindAttribute(printer_attrs,
			     "pclm-strip-height-preferred",
			     IPP_TAG_ZERO)) != NULL) ||
	  (ppd &&
	   (attr =
	    ppdFindAttr(ppd,
			"cupsPclmStripHeightPreferred", 0)) != NULL)) {
	if (attr)
	  n = atoi(attr->value);
	else if (ipp_attr)
	  n = ippGetInteger(ipp_attr, 0);
      }
      if (n <= 0) n = 16;
      snprintf(tmpstr, sizeof(tmpstr), "-dStripHeight=%d", n);
      cupsArrayAdd(gs_args, strdup(tmpstr));

      /* Back side orientation for Duplex not (yet) supported by Ghostscript */

      /* Compression method */

      if ((printer_attrs &&
	   (ipp_attr =
	    ippFindAttribute(printer_attrs,
			     "pclm-compression-method-preferred",
			     IPP_TAG_ZERO)) != NULL) ||
	  (ppd &&
	   (attr =
	    ppdFindAttr(ppd,
			"cupsPclmCompressionMethodPreferred", 0)) != NULL)) {
	if (attr)
	  res_str = attr->value;
	else if (ipp_attr) {
	  ippAttributeString(ipp_attr, tmpstr, sizeof(tmpstr));
	  res_str = tmpstr;
	}
	if (res_str) {
	  if (strcasestr(res_str, "flate"))
	    cupsArrayAdd(gs_args, strdup("-sCompression=Flate"));
	  else if (strcasestr(res_str, "rle"))
	    cupsArrayAdd(gs_args, strdup("-sCompression=RLE"));
	  else if (strcasestr(res_str, "jpeg"))
	    cupsArrayAdd(gs_args, strdup("-sCompression=JPEG"));
	  else
	    cupsArrayAdd(gs_args, strdup("-sCompression=Flate"));
	} else
	  cupsArrayAdd(gs_args, strdup("-sCompression=Flate"));
      } else
	cupsArrayAdd(gs_args, strdup("-sCompression=Flate"));
    } else {
      /* No PCLm supported or requested, use general raster PDF */

      /* Ghostscript output device and color/gray */

      n = 0;
      if (ppd) {
        if ((attr = ppdFindAttr(ppd,"ColorDevice", 0)) != 0 &&
	    (!strcasecmp(attr->value, "true") ||
	     !strcasecmp(attr->value, "on") ||
	     !strcasecmp(attr->value, "yes")))
          /* Color printer, according to PPD */
	  n = 1;
      } else if (printer_attrs) {
	if (((ipp_attr =
	      ippFindAttribute(printer_attrs,
			       "color-supported", IPP_TAG_ZERO)) != NULL &&
	     ippGetBoolean(ipp_attr, 0))) {
	  /* Color printer, according to printer attributes */
	  n = 1;
	}
      }
      if (n == 1 && h.cupsNumColors > 1)
	cupsArrayAdd(gs_args, strdup("-sDEVICE=pdfimage24"));
      else
	cupsArrayAdd(gs_args, strdup("-sDEVICE=pdfimage8"));

      /* Compression method */

      cupsArrayAdd(gs_args, strdup("-sCompression=Flate"));
    }

    /* Common option: Downscaling factor */

    cupsArrayAdd(gs_args, strdup("-dDownScaleFactor=1"));
  }

  if (outformat == CF_FILTER_OUT_FORMAT_PXL)
  {
    if (ppd)
    {
      {
        if ((attr = ppdFindAttr(ppd,"ColorDevice", 0)) != 0 &&
	    (!strcasecmp(attr->value, "true") ||
	     !strcasecmp(attr->value, "on") ||
	     !strcasecmp(attr->value, "yes")))
          /* Color PCL XL printer, according to PPD */
	  pxlcolor = 1;
      }  
    }
    else if (printer_attrs)
    {
      if (((ipp_attr =
	    ippFindAttribute(printer_attrs,
			     "color-supported", IPP_TAG_BOOLEAN)) != NULL &&
	   ippGetBoolean(ipp_attr, 0))) {
        /* Color PCL XL printer, according to printer attributes */
        pxlcolor = 1;
      }
    }

    if (job_attrs)
    {
      if ((ipp_attr =
	   ippFindAttribute(job_attrs, "pwg-raster-document-type",
			    IPP_TAG_ZERO)) != NULL ||
          (ipp_attr =
	   ippFindAttribute(job_attrs, "color-space", IPP_TAG_ZERO)) != NULL ||
          (ipp_attr =
	   ippFindAttribute(job_attrs, "color-model", IPP_TAG_ZERO)) != NULL ||
          (ipp_attr =
	   ippFindAttribute(job_attrs, "print-color-mode",
			    IPP_TAG_ZERO)) != NULL ||
          (ipp_attr =
	   ippFindAttribute(job_attrs, "output-mode", IPP_TAG_ZERO)) != NULL)
      {
        ippAttributeString(ipp_attr, buf, sizeof(buf));
        if (!strncasecmp(buf, "AdobeRgb", 8)     ||
	    !strncasecmp(buf, "adobe-rgb", 9)     ||
	    !strcasecmp(buf, "color")             ||
	    !strncasecmp(buf,"Cmyk", 4)           ||
	    !strncasecmp(buf, "Cmy", 3)           ||
	    !strncasecmp(buf, "Srgb", 4)          ||
	    !strncasecmp(buf, "Rgbw", 4)          ||
	    !strcasecmp(buf, "auto")              ||
	    !strncasecmp(buf, "Rgb", 3))
        {
          pxlcolor = 1;
        }
        else if(!strncasecmp(buf, "Device", 6))
        {
          char* ptr = buf+6;
          if (strtol(ptr, (char **)&ptr, 10) > 1) { /* If printer seems to
						       support more than 1
						       color  */
            pxlcolor = 1;
          }
        }
      }
    }

    if (pxlcolor == 0)   /*  Still printer seems to be mono */
    {
      const char* val;
      if ((val = cupsGetOption("pwg-raster-document-type", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("PwgRasterDocumentType", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("color-space", num_options, options)) != NULL ||
      (val = cupsGetOption("ColorSpace", num_options, options)) != NULL ||
      (val = cupsGetOption("color-model", num_options, options)) != NULL ||
      (val = cupsGetOption("ColorModel", num_options, options)) != NULL ||
      (val = cupsGetOption("print-color-mode", num_options, options)) != NULL ||
      (val = cupsGetOption("output-mode", num_options, options)) != NULL ||
      (val = cupsGetOption("OutputMode", num_options, options)) != NULL)
      {
        if(!strncasecmp(val, "AdobeRgb", 8) ||
	   !strncasecmp(val, "adobe-rgb", 9) ||
	   !strcasecmp(val, "color")         ||
	   !strncasecmp(val, "Cmyk", 4)      ||
	   !strncasecmp(val, "Cmy", 3)       ||
	   !strncasecmp(val, "Srgb", 4)      ||
	   !strncasecmp(val, "Rgbw", 4)      ||
	   !strncasecmp(val, "Rgb", 3)       ||
	   !strcasecmp(val, "auto"))
        {
          pxlcolor = 1;
        }
        else if(!strncasecmp(val, "Device", 6))
        {
          const char *ptr = val + 6;
          if(strtol(ptr, (char **)&ptr, 10)>1)  /* Printer seems to support
						   more then 1 color  */
          {
            pxlcolor = 1;
          }
        }
      }
    } 
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

  /* Set the device output ICC profile */
  if (icc_profile != NULL && icc_profile[0] != '\0') {
    snprintf(tmpstr, sizeof(tmpstr), "-sOutputICCProfile=%s", icc_profile);
    cupsArrayAdd(gs_args, strdup(tmpstr));
  } else if (!cm_disabled &&
	     (outformat == CF_FILTER_OUT_FORMAT_CUPS_RASTER ||
	      outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
	      outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)) {
    /* Set standard output ICC profile sGray/sRGB/AdobeRGB */
    if (h.cupsColorSpace == CUPS_CSPACE_SW)
      cupsArrayAdd(gs_args, strdup("-sOutputICCProfile=sgray.icc"));
    else if (h.cupsColorSpace == CUPS_CSPACE_SRGB)
      cupsArrayAdd(gs_args, strdup("-sOutputICCProfile=srgb.icc"));
    else if (h.cupsColorSpace == CUPS_CSPACE_ADOBERGB)
      cupsArrayAdd(gs_args, strdup("-sOutputICCProfile=a98.icc"));
  } else if (!cm_disabled &&
	     outformat == CF_FILTER_OUT_FORMAT_PCLM) {
    /* Set standard output ICC profile sGray/sRGB */
    if (h.cupsColorSpace == CUPS_CSPACE_SW)
      cupsArrayAdd(gs_args, strdup("-sOutputICCProfile=sgray.icc"));
    else if (h.cupsColorSpace == CUPS_CSPACE_SRGB)
      cupsArrayAdd(gs_args, strdup("-sOutputICCProfile=srgb.icc"));
  }
  else if (!cm_disabled)
  {
    cupsArrayAdd(gs_args, strdup("-sOutputICCProfile=srgb.icc"));
  }

  /* Switch to taking PostScript commands on the Ghostscript command line */
  cupsArrayAdd(gs_args, strdup("-c"));

  /* Set margins if we have a bounding box defined and output format
     is not PDF, as PDF output we have only in the PostScript-to-PDF
     filtering case which happens for converting PostScript input
     files before pdftopdf so margins will be handled later, whereas
     the other output formats for PDF-to-something filtering after
     cfFilterPDFToPDF, to format the pages for the printer, so margins are
     important. */
  if (h.cupsImagingBBox[3] > 0.0 && outformat != CF_FILTER_OUT_FORMAT_PDF) {
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
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterGhostscript: Ghostscript using Center-of-Pixel method to "
		 "fill paths.");
    cupsArrayAdd(gs_args, strdup("0 0 .setfilladjust2"));
  } else
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterGhostscript: Ghostscript using Any-Part-of-Pixel method to "
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
  close(outputfd);
  return status;
}

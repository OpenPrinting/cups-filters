/***
  This file is part of cups-filters.

  This file is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  This file is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with cups-filters; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#if defined(__OpenBSD__)
#include <sys/socket.h>
#endif /* __OpenBSD__ */
#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <cupsfilters/ppdgenerator.h>

static int              debug = 0;
static int		job_canceled = 0;
static void		cancel_job(int sig);

int
list_printers (int mode)
{
  int		ippfind_pid,		/* Process ID for ippfind */
		post_proc_pid = 0,	/* Process ID of post-processing */
		post_proc_pipe[2],	/* Pipe to post-processing */
		wait_children,		/* Number of child processes left */
		wait_pid,		/* Process ID from wait() */
		wait_status,		/* Status from child */
  		exit_status = 0,	/* Exit status */
                bytes,			/* Bytes copied */
                i;
  char		*ippfind_argv[100],	/* Arguments for ippfind */
		buffer[8192];		/* Copy buffer */
  cups_file_t	*fp;			/* Post-processing input file */
  char		*ptr;			/* Pointer into string */
  char          *service_uri = NULL,
                *txt_usb_mfg = NULL,
                *txt_usb_mdl = NULL,
                *txt_product = NULL,
                *txt_ty = NULL,
                *txt_pdl = NULL,
		value[256],		/* Value string */
                make_and_model[512],	/* Manufacturer and model */
                make[512],              /* Manufacturer */
		model[256],		/* Model */
		pdl[256],		/* PDL */
		device_id[2048];	/* 1284 device ID */

  /* 
   * Use CUPS' ippfind utility to discover all printers designed for
   * driverless use (IPP Everywhere or Apple Raster), and only IPP
   * network printers, not CUPS queues, output all data elements needed
   * for our desired output.
   */

  /* ippfind ! --txt printer-type --and \( --txt-pdl image/pwg-raster --or --txt-pdl image/urf \) -x echo -en '{service_uri}\t{txt_usb_MFG}\t{txt_usb_MDL}\t{txt_product}\t{txt_ty}\t{service_name}\t{txt_pdl}\n' \; */

  i = 0;
  ippfind_argv[i++]  = "ippfind";
  ippfind_argv[i++]  = "-T";               /* Bonjour poll timeout */
  ippfind_argv[i++]  = "3";                /* 3 seconds */
  ippfind_argv[i++]  = "!";                /* ! --txt printer-type */
  ippfind_argv[i++]  = "--txt";            /* No remote CUPS queues */
  ippfind_argv[i++]  = "printer-type";     /* (no "printer-type" in TXT
					      record) */
  ippfind_argv[i++]  = "--and";            /* and */
  ippfind_argv[i++]  = "(";
  ippfind_argv[i++]  = "--txt-pdl";        /* PDL list in TXT record contains */
  ippfind_argv[i++]  = "image/pwg-raster"; /* PWG Raster (IPP Everywhere) */
#ifdef CUPS_RASTER_HAVE_APPLERASTER
  ippfind_argv[i++]  = "--or";             /* or */
  ippfind_argv[i++]  = "--txt-pdl";
  ippfind_argv[i++] = "image/urf";        /* Apple Raster */
  ippfind_argv[i++] = ")";
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             /* Output the needed data fields */
  ippfind_argv[i++] = "-en";              /* separated by tab characters */
  if (mode > 0)
    ippfind_argv[i++] = "{service_uri}\t{txt_usb_MFG}\t{txt_usb_MDL}\t{txt_product}\t{txt_ty}\t{txt_pdl}\n";
  else
    ippfind_argv[i++] = "{service_uri}\n";
  ippfind_argv[i++] = ";";
  ippfind_argv[i++] = NULL;
#else
  ippfind_argv[i++] = ")";
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             /* Output the needed data fields */
  ippfind_argv[i++] = "-en";              /* separated by tab characters */
  if (mode > 0)
    ippfind_argv[i++] = "{service_uri}\t{txt_usb_MFG}\t{txt_usb_MDL}\t{txt_product}\t{txt_ty}\t{txt_pdl}\n";
  else
    ippfind_argv[i++] = "{service_uri}\n";
  ippfind_argv[i++] = ";";
  ippfind_argv[i++] = NULL;
#endif

 /*
  * Create a pipe for passing the ippfind output to post-processing
  */
  
  if (pipe(post_proc_pipe))
  {
    perror("ERROR: Unable to create pipe to post-processing");

    exit_status = 1;
    goto error;
  }

  if ((ippfind_pid = fork()) == 0)
  {
   /*
    * Child comes here...
    */

    dup2(post_proc_pipe[1], 1);
    close(post_proc_pipe[0]);
    close(post_proc_pipe[1]);

    execvp(CUPS_IPPFIND, ippfind_argv);
    perror("ERROR: Unable to execute ippfind utility");

    exit(1);
  }
  else if (ippfind_pid < 0)
  {
   /*
    * Unable to fork!
    */

    perror("ERROR: Unable to execute ippfind utility");

    exit_status = 1;
    goto error;
  }

  if (debug)
    fprintf(stderr, "DEBUG: Started %s (PID %d)\n", ippfind_argv[0],
	    ippfind_pid);

  if ((post_proc_pid = fork()) == 0)
  {
    /*
     * Child comes here...
     */

    dup2(post_proc_pipe[0], 0);
    close(post_proc_pipe[0]);
    close(post_proc_pipe[1]);

    fp = cupsFileStdin();

    while ((bytes = cupsFileGetLine(fp, buffer, sizeof(buffer))) > 0)
    {
      if (strncasecmp(buffer, "ipp:", 4) && strncasecmp(buffer, "ipps:", 4)) {
	/* Invalid IPP URI */
	fprintf(stderr, "EROOR: Invalid IPP URI: %s\n", buffer);
	continue;
      }
      if (mode == 0)
	/* Manual call on the command line */
	printf("%s", buffer);
      else {
	/* Call by CUPS, either as PPD generator
	   (/usr/lib/cups/driver/, with "list" command line argument)
	   or as backend in discovery mode (/usr/lib/cups/backend/,
	   env variable "SOFTWARE" starts with "CUPS") */
	ptr = buffer;
	service_uri = ptr;
	ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
	if (!ptr) goto read_error;
	*ptr = '\0';
	ptr ++;
	txt_usb_mfg = ptr;
	ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
	if (!ptr) goto read_error;
	*ptr = '\0';
	ptr ++;
	txt_usb_mdl = ptr;
	ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
	if (!ptr) goto read_error;
	*ptr = '\0';
	ptr ++;
	txt_product = ptr;
	ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
	if (!ptr) goto read_error;
	*ptr = '\0';
	ptr ++;
	txt_ty = ptr;
	ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
	if (!ptr) goto read_error;
	*ptr = '\0';
	ptr ++;
	txt_pdl = ptr;
	ptr = memchr(ptr, '\n', sizeof(buffer) - (ptr - buffer));
	if (!ptr) goto read_error;
	*ptr = '\0';

	make_and_model[0] = '\0';
	make[0] = '\0';
	pdl[0] = '\0';
	device_id[0] = '\0';
	strncpy(model, "Unknown", sizeof(model));
	
	if (txt_usb_mfg[0] != '\0') {
	  strncpy(make, txt_usb_mfg, sizeof(make));
	  ptr = device_id + strlen(device_id);
	  snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
		   "MFG:%s;", txt_usb_mfg);
	}
	if (txt_usb_mdl[0] != '\0') {
	  strncpy(model, txt_usb_mdl, sizeof(model));
	  ptr = device_id + strlen(device_id);
	  snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
		   "MDL:%s;", txt_usb_mdl);
	} else if (txt_product[0] != '\0') {
	  if (txt_product[0] == '(') {
	    /* Strip parenthesis... */
	    if ((ptr = txt_product + strlen(txt_product) - 1) > txt_product &&
		*ptr == ')')
	      *ptr = '\0';
	    strncpy(model, txt_product + 1, sizeof(model));
	  } else
	    strncpy(model, txt_product, sizeof(model));
	} else if (txt_ty[0] != '\0') {
	  strncpy(model, txt_ty, sizeof(model));
	  if ((ptr = strchr(model, ',')) != NULL)
	    *ptr = '\0';
	}
	if (txt_pdl[0] != '\0')
	  strncpy(pdl, txt_pdl, sizeof(pdl));

	if (!device_id[0] && strcasecmp(model, "Unknown")) {
	  if (make[0])
	    snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;",
		     make, model);
	  else if (!strncasecmp(model, "designjet ", 10))
	    snprintf(device_id, sizeof(device_id), "MFG:HP;MDL:%s;",
		     model + 10);
	  else if (!strncasecmp(model, "stylus ", 7))
	    snprintf(device_id, sizeof(device_id), "MFG:EPSON;MDL:%s;",
		     model + 7);
	  else if ((ptr = strchr(model, ' ')) != NULL) {
	    /* Assume the first word is the make...*/
	    memcpy(make, model, (size_t)(ptr - model));
	    make[ptr - model] = '\0';
	    snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;",
		     make, ptr + 1);
	  }
	}

	if (device_id[0] &&
	    !strcasestr(device_id, "CMD:") &&
	    !strcasestr(device_id, "COMMAND SET:") &&
	    (strcasestr(pdl, "application/pdf") ||
	     strcasestr(pdl, "application/postscript") ||
	     strcasestr(pdl, "application/vnd.hp-PCL") ||
	     strcasestr(pdl, "image/"))) {
	  value[0] = '\0';
	  if (strcasestr(pdl, "application/pdf"))
	    strncat(value, ",PDF", sizeof(value));
	  if (strcasestr(pdl, "application/postscript"))
	    strncat(value, ",PS", sizeof(value));
	  if (strcasestr(pdl, "application/vnd.hp-PCL"))
	    strncat(value, ",PCL", sizeof(value));
	  if (strcasestr(pdl, "image/pwg-raster"))
	    strncat(value, ",PWGRaster", sizeof(value));
	  if (strcasestr(pdl, "image/urf"))
	    strncat(value, ",AppleRaster", sizeof(value));
	  for (ptr = strcasestr(pdl, "image/"); ptr;
	       ptr = strcasestr(ptr, "image/")) {
	    char *valptr = value + strlen(value);
	    if (valptr < (value + sizeof(value) - 1))
	      *valptr++ = ',';
	    ptr += 6;
	    while (isalnum(*ptr & 255) || *ptr == '-' || *ptr == '.') {
	      if (isalnum(*ptr & 255) && valptr < (value + sizeof(value) - 1))
		*valptr++ = (char)toupper(*ptr++ & 255);
	      else
		break;
	    }
	    *valptr = '\0';
	  }
	  ptr = device_id + strlen(device_id);
	  snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
		   "CMD:%s;", value + 1);
	}

	if (make[0] &&
	    (strncasecmp(model, make, strlen(make)) ||
	     !isspace(model[strlen(make)])))
	  snprintf(make_and_model, sizeof(make_and_model), "%s %s",
		   make, model);
	else
	  strncpy(make_and_model, model, sizeof(make_and_model));

	if (mode == 1)
	  /* Call with "list" argument (PPD generator in list mode */
	  printf("\"driverless:%s\" en \"%s\" \"%s, driverless, cups-filters " VERSION
		 "\" \"%s\"\n", service_uri, make, make_and_model, device_id);
	else
	  /* Call without arguments and env variable "SOFTWARE" starting
	     with "CUPS" (Backend in discovery mode) */
	  printf("network %s \"%s\" \"%s (driverless)\" \"%s\" \"\"\n", service_uri, make_and_model, make_and_model, device_id);

      read_error:
	continue;
      }
    }

    /*
     * Copy the rest of the file
     */

    while ((bytes = cupsFileRead(fp, buffer, sizeof(buffer))) > 0)
      fwrite(buffer, 1, bytes, stdout);

    exit(0);
  }
  else if (post_proc_pid < 0)
  {
    /*
     * Unable to fork!
     */

    perror("ERROR: Unable to execute post-processing process");

    exit_status = 1;
    goto error;
  }

  if (debug)
    fprintf(stderr, "DEBUG: Started post-processing (PID %d)\n", post_proc_pid);

  close(post_proc_pipe[0]);
  close(post_proc_pipe[1]);

 /*
  * Wait for the child processes to exit...
  */

  wait_children = 2;

  while (wait_children > 0)
  {
   /*
    * Wait until we get a valid process ID or the job is canceled...
    */

    while ((wait_pid = wait(&wait_status)) < 0 && errno == EINTR)
    {
      if (job_canceled)
      {
	kill(ippfind_pid, SIGTERM);
	kill(post_proc_pid, SIGTERM);

	job_canceled = 0;
      }
    }

    if (wait_pid < 0)
      break;

    wait_children --;

   /*
    * Report child status...
    */

    if (wait_status)
    {
      if (WIFEXITED(wait_status))
      {
	exit_status = WEXITSTATUS(wait_status);

	if (debug)
	  fprintf(stderr, "DEBUG: PID %d (%s) stopped with status %d!\n",
		  wait_pid,
		  wait_pid == ippfind_pid ? "ippfind" :
		  (wait_pid == post_proc_pid ? "Post-processing" :
		   "Unknown process"),
		  exit_status);
	/* When run by CUPS do not exit with an error status if there is 
	   simply no driverless printer available or no Avahi present */
	if (mode != 0 && wait_pid == ippfind_pid && exit_status <= 2)
	  exit_status = 0;	  
      }
      else if (WTERMSIG(wait_status) == SIGTERM)
      {
	if (debug)
	  fprintf(stderr,
		  "DEBUG: PID %d (%s) was terminated normally with signal %d!\n",
		  wait_pid,
		  wait_pid == ippfind_pid ? "ippfind" :
		  (wait_pid == post_proc_pid ? "Post-processing" :
		   "Unknown process"),
		  exit_status);
      }
      else
      {
	exit_status = WTERMSIG(wait_status);

	if (debug)
	  fprintf(stderr, "DEBUG: PID %d (%s) crashed on signal %d!\n",
		  wait_pid,
		  wait_pid == ippfind_pid ? "ippfind" :
		  (wait_pid == post_proc_pid ? "Post-processing" :
		   "Unknown process"),
		  exit_status);
      }
    }
    else
    {
      if (debug)
	fprintf(stderr, "DEBUG: PID %d (%s) exited with no errors.\n",
		wait_pid,
		wait_pid == ippfind_pid ? "ippfind" :
		(wait_pid == post_proc_pid ? "Post-processing" :
		 "Unknown process"));
    }
  }

 /*
  * Exit...
  */

  error:

  return (exit_status);

}

int
generate_ppd (const char *uri)
{
  int uri_status, host_port;
  http_t *http = NULL;
  char scheme[10], userpass[1024], host_name[1024], resource[1024];
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;
  char buffer[65536], ppdname[1024];
  int i, fd, bytes;

  /* Request printer properties via IPP to generate a PPD file for the
     printer (mainly IPP Everywhere printers)
     If we work with Systen V interface scripts use this info to set
     option defaults. */
  uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, uri,
			       scheme, sizeof(scheme),
			       userpass, sizeof(userpass),
			       host_name, sizeof(host_name),
			       &(host_port),
			       resource, sizeof(resource));
  if (uri_status != HTTP_URI_OK)
    goto fail;
  if ((http = httpConnect(host_name, host_port)) ==
      NULL) {
    fprintf(stderr, "ERROR: Cannot connect to remote printer %s (%s:%d)\n",
	    uri, host_name, host_port);
    goto fail;
  }
  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
	       NULL, uri);
  response = cupsDoRequest(http, request, resource);

  /* Log all printer attributes for debugging */
  if (debug) {
    attr = ippFirstAttribute(response);
    while (attr) {
      fprintf(stderr, "DEBUG2: Attr: %s\n", ippGetName(attr));
      ippAttributeString(attr, buffer, sizeof(buffer));
      fprintf(stderr, "DEBUG2: Value: %s\n", buffer);
      for (i = 0; i < ippGetCount(attr); i ++)
	fprintf(stderr, "DEBUG2: Keyword: %s\n",
		ippGetString(attr, i, NULL));
      attr = ippNextAttribute(response);
    }
  }

  /* Generate the PPD file */
  if (!ppdCreateFromIPP(ppdname, sizeof(ppdname), response, NULL, NULL, 0, 0)) {
    if (errno != 0)
      fprintf(stderr, "ERROR: Unable to create PPD file: %s\n",
	      strerror(errno));
    else
      fprintf(stderr, "ERROR: Unable to create PPD file: %s\n",
	      ppdgenerator_msg);
    goto fail;
  } else if (debug) {
    fprintf(stderr, "DEBUG: PPD generation successful: %s\n", ppdgenerator_msg);
    fprintf(stderr, "DEBUG: Created temporary PPD file: %s\n", ppdname);
  }

  ippDelete(response);
  httpClose(http);

  /* Output of PPD file to stdout */
  fd = open(ppdname, O_RDONLY);
  while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
    bytes = fwrite(buffer, 1, bytes, stdout);
  close(fd);
  unlink(buffer);

  return 0;
  
 fail:
  fprintf(stderr, "ERROR: Unable to generate PPD file.\n");

  ippDelete(response);
  if (http)
    httpClose(http);

  return 1;
}


int main(int argc, char*argv[]) {
  int i;
  char *val;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Register a signal handler to cleanly cancel a job.
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif /* HAVE_SIGSET */

  /* Read command line options */
  if (argc >= 2) {
    for (i = 1; i < argc; i++)
      if (!strcasecmp(argv[i], "--debug") || !strcasecmp(argv[i], "-d") ||
	  !strncasecmp(argv[i], "-v", 2)) {
	/* Output debug messages on stderr also when not running under CUPS
	   ("list" and "cat" options) */
	debug = 1;
      } else if (!strcasecmp(argv[i], "list")) {
	/* List a driver URI and metadata for each printer suitable for
	   driverless printing */
	debug = 1;
	exit(list_printers(1));
      } else if (!strncasecmp(argv[i], "cat", 3)) {
	/* Generate the PPD file for the given driver URI */
	debug = 1;
	val = argv[i] + 3;
	if (strlen(val) == 0) {
	  i ++;
	  if (i < argc && *argv[i] != '-')
	    val = argv[i];
	  else
	    val = NULL;
	}
	if (val) {
	  /* Generate PPD file */
	  if (!strncasecmp(val, "driverless:", 11))
	    val += 11;
	  exit(generate_ppd(val));
	} else {
	  fprintf(stderr,
		  "Reading command line option \"cat\", no driver URI supplied.\n\n");
	  goto help;
	}
      } else if (!strcasecmp(argv[i], "--version") ||
		 !strcasecmp(argv[i], "--help") || !strcasecmp(argv[i], "-h")) {
	/* Help!! */
	goto help;
      } else {
	/* Unknown option, consider as IPP printer URI */
	exit(generate_ppd(argv[i]));
      }
  }

  /* Call without arguments, list printer URIs for all suitable printers
     when started manually, list printer URIs and metadata like CUPS
     backends do when started as CUPS backend (discovery mode only) */
  if ((val = getenv("SOFTWARE")) != NULL &&
      strncasecmp(val, "CUPS", 4) == 0) {
    /* CUPS backend in discovery mode */
    debug = 1;
    exit(list_printers(2));
  } else
    /* Manual call */
    exit(list_printers(0));

 help:

  fprintf(stderr,
	  "\ndriverless of cups-filters version "VERSION"\n\n"
	  "Usage: driverless [options]\n"
	  "Options:\n"
	  "  -h\n"
	  "  --help\n"
	  "  --version               Show this usage message.\n"
	  "  -d\n"
	  "  -v\n"
	  "  --debug                 Debug/verbose mode.\n"
	  "  list                    List the driver URIs and metadata for all available\n"
	  "                          IPP printers supporting driverless printing (to be\n"
	  "                          used by CUPS).\n"
	  "  cat <driver URI>        Generate the PPD file for the driver URI\n"
	  "                          <driver URI> (to be used by CUPS).\n"
	  "  <printer URI>           Generate the PPD file for the IPP printer URI\n"
	  "                          <printer URI>.\n"
	  "\n"
	  "When called without options, the IPP printer URIs of all available IPP printers\n"
	  "will be listed.\n\n"
	  );

  return 1;
}

/*
 * 'cancel_job()' - Flag the job as canceled.
 */

static void
cancel_job(int sig)			/* I - Signal number (unused) */
{
  (void)sig;

  job_canceled = 1;
}

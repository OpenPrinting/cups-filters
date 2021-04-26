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
#include <fcntl.h>
#include <sys/wait.h>
#include <cups/cups.h>
#include <ppd/ppd.h>
#include <cups/raster.h>
#include <cupsfilters/ipp.h>
#include <cupsfilters/ppdgenerator.h>

#define MAX_OUTPUT_LEN 8192

static int              debug = 0;
static int		job_canceled = 0;
static void		cancel_job(int sig);
static cups_array_t     *uuids = NULL;

static int
compare_service_uri(char *a,	char *b)
{
  return (strcmp(a,b));
}

static int
convert_to_port(char *a)
{
  int port = 0;
  for( int i = 0; i<strlen(a); i++)
    port = port*10 + (a[i] - '0');

  return (port);
}

void
listPrintersInArray(int reg_type_no, int mode, int isFax,
		    char* ippfind_output) {
  int	port,
        is_local;
  char	buffer[8192],		/* Copy buffer */
        *ptr,		        /* Pointer into string */
        *scheme = NULL,
        *service_name = NULL,
        *resource = NULL,
        *domain = NULL,
        *ptr_to_port = NULL,    /* pointer to port */
        *reg_type = NULL,
        *service_hostname = NULL,
        *txt_usb_mfg = NULL,
        *txt_usb_mdl = NULL,
        *txt_product = NULL,
        *txt_ty = NULL,
        *txt_pdl = NULL,
        *txt_uuid = NULL,
        value[256],             /* Value string */
        *service_uri,           /* URI to list for this service */
        service_host_name[1024],/* "Host name" for assembling URI */
        make_and_model[1024],	/* Manufacturer and model */
        make[512],              /* Manufacturer */
      	model[256],		/* Model */
        pdl[256],		/* PDL */
        device_id[2048];	/* 1284 device ID */

  service_uri = (char *)malloc(2048*(sizeof(char)));
  /* Mark all the fields of the output of ippfind */
  ptr = ippfind_output;
  if (reg_type_no < 1) {
    scheme = "ipp";
    reg_type = "_ipp._tcp";
  } else if (reg_type_no > 1) {
    scheme = "ipps";
    reg_type = "_ipps._tcp";
  }

  /* ... second, complete the output line, either URI-only or with
     extra info for CUPS */

  if (mode == -1) {
    /* Standard IPP URI (only manual call) */
    service_hostname = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr)
      goto read_error;
    *ptr = '\0';
    ptr ++;

    resource = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    ptr_to_port = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;
    port = convert_to_port(ptr_to_port);

    /* Do we have a local service so that we have to set the host name to
       "localhost"? */
    is_local = (*ptr == 'L');

    httpAssembleURIf(HTTP_URI_CODING_ALL, service_uri,
		     2047,
		     scheme, NULL,
		     (is_local ? "localhost" : service_hostname),
		     port, "/%s", resource);
    printf("%s\n", service_uri);
  } else {
    /* DNS-SD-service-name-based URI */
    service_name = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr)
      goto read_error;
    *ptr = '\0';
    ptr ++;

    domain = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr)
      goto read_error;
    *ptr = '\0';
    ptr ++;

    snprintf(service_host_name, sizeof(service_host_name) - 1, "%s.%s.%s",
	     service_name, reg_type, domain);
    httpAssembleURIf(HTTP_URI_CODING_ALL, service_uri,
		     2047,
		     scheme, NULL,
		     service_host_name, 0, "/");

    if (mode == 0)
      /* Manual call, only show URI, nothing more */
      printf("%s\n", service_uri);
    else {
      /* Call by CUPS, either as PPD generator
	 (/usr/lib/cups/driver/, with "list" command line argument)
	 or as backend in discovery mode (/usr/lib/cups/backend/,
	 env variable "SOFTWARE" starts with "CUPS") */
      txt_usb_mfg = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';
      ptr ++;
      txt_usb_mdl = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';
      ptr ++;
      txt_product = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';
      ptr ++;
      txt_ty = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';
      ptr ++;
      txt_pdl = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';
      ptr ++;
      txt_uuid = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';
      /* We check only for a fax resource (rfo) here, if there is none,
	 resource will stay blank meaning device does not support fax */
      ptr ++;
      resource = ptr;
      ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
      if (!ptr)
	goto read_error;
      *ptr = '\0';

      make_and_model[0] = '\0';
      make[0] = '\0';
      pdl[0] = '\0';
      device_id[0] = '\0';
      strncpy(model, "Unknown", sizeof(model) - 1);

      if (txt_usb_mfg[0] != '\0') {
	strncpy(make, txt_usb_mfg, sizeof(make) - 1);
	if (strlen(txt_usb_mfg) > 511)
	  make[511] = '\0';
	ptr = device_id + strlen(device_id);
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
		 "MFG:%s;", txt_usb_mfg);
      }
      if (txt_usb_mdl[0] != '\0') {
	strncpy(model, txt_usb_mdl, sizeof(model) - 1);
	if (strlen(txt_usb_mdl) > 255)
	  model[255] = '\0';
	ptr = device_id + strlen(device_id);
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
		 "MDL:%s;", txt_usb_mdl);
      } else if (txt_product[0] != '\0') {
	if (txt_product[0] == '(') {
	  /* Strip parenthesis... */
	  if ((ptr = txt_product + strlen(txt_product) - 1) > txt_product &&
	      *ptr == ')')
	    *ptr = '\0';
	  strncpy(model, txt_product + 1, sizeof(model) - 1);
	  if ((strlen(txt_product) + 1) > 255)
	    model[255] = '\0';
	} else
	  strncpy(model, txt_product, sizeof(model) - 1);
      } else if (txt_ty[0] != '\0') {
	strncpy(model, txt_ty, sizeof(model) - 1);
	if (strlen(txt_ty) > 255)
	  model[255] = '\0';
	if ((ptr = strchr(model, ',')) != NULL)
	  *ptr = '\0';
      }
      if (txt_pdl[0] != '\0') {
	strncpy(pdl, txt_pdl, sizeof(pdl) - 1);
	if (strlen(txt_pdl) > 255)
	  pdl[255] = '\0';
      }

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
	   strcasestr(pdl, "application/PCLm") ||
	   strcasestr(pdl, "image/"))) {
	value[0] = '\0';
	if (strcasestr(pdl, "application/pdf"))
	  strncat(value, ",PDF", sizeof(value));
	if (strcasestr(pdl, "application/PCLm"))
	  strncat(value, ",PCLM", sizeof(value));
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
	strncpy(make_and_model, model, sizeof(make_and_model) - 1);

      if (mode == 1) {
	/* Only output the entry if we had this UUID not already */
	if (!txt_uuid[0] || !cupsArrayFind(uuids, txt_uuid)) {
	  /* Save UUID as if an entry with the same UUID appears again, it
	     is the the same pair of print and fax PPDs */
	  if (txt_uuid[0]) cupsArrayAdd(uuids, strdup(txt_uuid));
	  /* Call with "list" argument  (PPD generator in list mode)   */
	  printf("\"%s%s\" en \"%s\" \"%s, %sdriverless, cups-filters "
		 VERSION "\" \"%s\"\n",
		 ((isFax) ? "driverless-fax:" : "driverless:"),
		 service_uri, make, make_and_model,
		 ((isFax) ? "Fax, " : ""),
		 device_id);
	  if (resource[0]) /* We have also fax on this device */
	    printf("\"%s%s\" en \"%s\" \"%s, Fax, driverless, cups-filters "
		   VERSION "\" \"%s\"\n",
		   "driverless-fax:",
		   service_uri, make, make_and_model,
		   device_id);
	}
      } else {
	/* Call without arguments and env variable "SOFTWARE" starting
	   with "CUPS" (Backend in discovery mode) */
	printf("network %s \"%s\" \"%s (driverless)\" \"%s\" \"\"\n",
	       service_uri, make_and_model, make_and_model,
	       device_id);
      }
    }

  read_error:
    free(service_uri);
    return;
  }

  free(service_uri);
  return;
}

int
list_printers (int mode, int reg_type_no, int isFax)
{
  int		ippfind_pid = 0,	/* Process ID of ippfind for IPP */
                post_proc_pipe[2],	/* Pipe to post-processing for IPP */
		wait_res,		/* Process ID from wait() */
		wait_status,		/* Status from child */
  	        exit_status = 0,	/* Exit status */
                i;
  char		*ippfind_argv[100];	/* Arguments for ippfind */
  cups_array_t  *service_uri_list_ipps, /* Array to store ippfind output for
					   IPPS */
                *service_uri_list_ipp;  /* Array to store ippfind output for
					   IPP */
  cups_file_t	*fp;
  int		bytes;
  char		*ptr,
		buffer[MAX_OUTPUT_LEN],	/* Copy buffer */
		*ippfind_output;

  service_uri_list_ipps =
    cupsArrayNew3((cups_array_func_t)compare_service_uri, NULL, NULL, 0, NULL,
		  (cups_afree_func_t)free);
  service_uri_list_ipp =
    cupsArrayNew3((cups_array_func_t)compare_service_uri, NULL, NULL, 0, NULL,
		  (cups_afree_func_t)free);

 /*
  * Use CUPS' ippfind utility to discover all printers designed for
  * driverless use (IPP Everywhere or Apple Raster), and only IPP
  * network printers, not CUPS queues, output all data elements needed
  * for our desired output.
  */

  /* ippfind -T 0 _ipps._tcp _ipp._tcp ! --txt printer-type --and \( --txt-pdl image/pwg-raster --or --txt-pdl application/PCLm --or --txt-pdl image/urf --or --txt-pdl application/pdf \) -x echo -en '\n{service_scheme}\t{service_name}\t{service_domain}\t{txt_usb_MFG}\t{txt_usb_MDL}\t{txt_product}\t{txt_ty}\t{service_name}\t{txt_pdl}\t{txt_UUID}\t{txt_rfo}\t' \; --local -x echo -en L \;*/

  i = 0;
  ippfind_argv[i++] = "ippfind";
  ippfind_argv[i++] = "-T";               /* DNS-SD poll timeout */
  ippfind_argv[i++] = "0";                /* Minimum time required */
  if (reg_type_no >= 1)
    ippfind_argv[i++] = "_ipps._tcp";     /* list IPPS entries */
  if (reg_type_no <= 1)
    ippfind_argv[i++] = "_ipp._tcp";     /* list IPPS entries */
  ippfind_argv[i++] = "!";                /* ! --txt printer-type */
  ippfind_argv[i++] = "--txt";            /* No remote CUPS queues */
  ippfind_argv[i++] = "printer-type";     /* (no "printer-type" in TXT
					      record) */
  if (isFax) {
    ippfind_argv[i++] = "--and";
    ippfind_argv[i++] = "--txt";
    ippfind_argv[i++] = "rfo";
  }
  ippfind_argv[i++] = "--and";            /* and */
  ippfind_argv[i++] = "(";
  ippfind_argv[i++] = "--txt-pdl";        /* PDL list in TXT record contains */
  ippfind_argv[i++] = "image/pwg-raster"; /* PWG Raster (IPP Everywhere) */
#ifdef QPDF_HAVE_PCLM
  ippfind_argv[i++] = "--or";             /* or */
  ippfind_argv[i++] = "--txt-pdl";
  ippfind_argv[i++] = "application/PCLm"; /* PCLm */
#endif
#ifdef CUPS_RASTER_HAVE_APPLERASTER
  ippfind_argv[i++] = "--or";             /* or */
  ippfind_argv[i++] = "--txt-pdl";
  ippfind_argv[i++] = "image/urf";        /* Apple Raster */
#endif
  ippfind_argv[i++] = "--or";             /* or */
  ippfind_argv[i++] = "--txt-pdl";
  ippfind_argv[i++] = "application/pdf";  /* PDF */
  ippfind_argv[i++] = ")";
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             /* Output the needed data fields */
  ippfind_argv[i++] = "-en";              /* separated by tab characters */
  if (mode < 0) {
    if (isFax)
      ippfind_argv[i++] =
	"\n{service_scheme}\t{service_hostname}\t{txt_rfo}\t{service_port}\t";
    else
      ippfind_argv[i++] =
	"\n{service_scheme}\t{service_hostname}\t{txt_rp}\t{service_port}\t";
  } else if (mode > 0)
    ippfind_argv[i++] =
      "{service_scheme}\t{service_name}\t{service_domain}\t{txt_usb_MFG}\t"
      "{txt_usb_MDL}\t{txt_product}\t{txt_ty}\t{txt_pdl}\t{txt_UUID}\t"
      "{txt_rfo}\t\n";
  else
    ippfind_argv[i++] =
      "{service_scheme}\t{service_name}\t{service_domain}\t\n";
  ippfind_argv[i++] = ";";
  if (mode < 0) {
    ippfind_argv[i++] = "--local";        /* Rest only if local service */
    ippfind_argv[i++] = "-x";
    ippfind_argv[i++] = "echo";           /* Output an 'L' at the end of the */
    ippfind_argv[i++] = "-en";            /* line */
    ippfind_argv[i++] = "L";
    ippfind_argv[i++] = ";";
  }
  ippfind_argv[i++] = NULL;

 /*
  * Create a pipe for passing the ippfind output to post-processing
  */

  if (pipe(post_proc_pipe)) {
    perror("ERROR: Unable to create pipe to post-processing");

    exit_status = 1;
    goto error;
  }

  if ((ippfind_pid = fork()) == 0) {
   /*
    * Child comes here...
    */

    dup2(post_proc_pipe[1], 1);

    close(post_proc_pipe[0]);
    close(post_proc_pipe[1]);

    execvp(CUPS_IPPFIND, ippfind_argv);
    perror("ERROR: Unable to execute ippfind utility");

    exit(1);
  } else if (ippfind_pid < 0) {
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

  close(post_proc_pipe[1]);

 /*
  * Reading the ippfind output into CUPS Arrays
  */
  fp = cupsFileOpenFd(post_proc_pipe[0], "r");
  if (fp) {
    while ((bytes = cupsFileGetLine(fp, buffer, sizeof(buffer))) > 0 ||
	   (bytes < 0 && (errno == EAGAIN || errno == EINTR))) {
      ippfind_output = (char *)malloc(MAX_OUTPUT_LEN*(sizeof(char)));
      ptr = buffer;
      while (ptr && !isalnum(*ptr & 255)) ptr ++;
      if ((!strncasecmp(ptr, "ipps", 4) && ptr[4] == '\t')) {
	ptr += 4;
	*ptr = '\0';
	ptr ++;
	snprintf(ippfind_output, MAX_OUTPUT_LEN, "%s", ptr);
	cupsArrayAdd(service_uri_list_ipps, ippfind_output);
      } else if ((!strncasecmp(ptr, "ipp", 3) && ptr[3] == '\t')) {
	ptr += 3;
	*ptr = '\0';
	ptr ++;
	snprintf(ippfind_output, MAX_OUTPUT_LEN, "%s", ptr);
	cupsArrayAdd(service_uri_list_ipp, ippfind_output);
      } else
	continue;
    }
    if (bytes < 0) {
      /* Read error - bail if we don't see EAGAIN or EINTR... */
      if (errno != EAGAIN && errno != EINTR)
      {
	perror("ERROR: Unable to read ippfind output");
	exit_status = -1;
	goto error;
      }
    }
  } else {
    perror("ERROR: Unable to open ippfind output data stream");
    exit_status = -1;
    goto error;
  }

  for (int j = 0; j < cupsArrayCount(service_uri_list_ipp); j ++)
  {
    if (cupsArrayFind(service_uri_list_ipps,
		      (char*)cupsArrayIndex(service_uri_list_ipp, j))
	== NULL)
      listPrintersInArray(0, mode, isFax,
			  (char *)cupsArrayIndex(service_uri_list_ipp, j));
  }

  for (int j = 0; j < cupsArrayCount(service_uri_list_ipps); j++)
  {
     listPrintersInArray(2, mode, isFax,
			 (char *)cupsArrayIndex(service_uri_list_ipps, j));
  }

 /*
  * Wait for the child process to exit...
  */

  while ((wait_res = waitpid(ippfind_pid, &wait_status, 0)) == -1 &&
	 errno == EINTR);
  if (wait_res == -1) {
    fprintf(stderr, "ERROR: ippfind (PID %d) stopped with an error: %s\n",
	    ippfind_pid, strerror(errno));
    exit_status = errno;
    goto error;
  }
  /* How did ippfind terminate */
  if (WIFEXITED(wait_status)) {
    /* Via exit() anywhere or return() in the main() function */
    exit_status = WEXITSTATUS(wait_status);
    if (exit_status)
      fprintf(stderr, "ERROR: ippfind (PID %d) stopped with status %d!\n",
	      ippfind_pid, exit_status);
  } else if (WIFSIGNALED(wait_status) && WTERMSIG(wait_status) != SIGTERM) {
    /* Via signal */
    exit_status = 256 * WTERMSIG(wait_status);
    if (exit_status)
      fprintf(stderr, "ERROR: ippfind (PID %d) stopped on signal %d!\n",
	      ippfind_pid, exit_status);
  }
  if (!exit_status && debug)
    fprintf(stderr, "DEBUG: ippfind (PID %d) exited with no errors.\n",
	    ippfind_pid);

 /*
  * Exit...
  */

 error:
  cupsArrayDelete(service_uri_list_ipps);
  cupsArrayDelete(service_uri_list_ipp);
  return (exit_status);
}

int
generate_ppd (const char *uri, int isFax)
{
  ipp_t *response = NULL;
  char buffer[65536], ppdname[1024], ppdgenerator_msg[1024];
  int  fd,
       bytes;
  char *ptr1,
       *ptr2;

  /* Tread prefixes (CUPS PPD/driver URIs) */

  if (!strncasecmp(uri, "driverless:", 11)) {
    uri += 11;
    isFax = 0;
  }
  else if (!strncasecmp(uri, "driverless-fax:", 15)) {
    uri += 15;
    isFax = 1;
  }

  /* Request printer properties via IPP to generate a PPD file for the
     printer */

  response = get_printer_attributes4(uri, NULL, 0, NULL, 0, 1, isFax);

  if (debug) {
    ptr1 = get_printer_attributes_log;
    while(ptr1) {
      ptr2 = strchr(ptr1, '\n');
      if (ptr2) *ptr2 = '\0';
      fprintf(stderr, "DEBUG2: %s\n", ptr1);
      if (ptr2) *ptr2 = '\n';
      ptr1 = ptr2 ? (ptr2 + 1) : NULL;
    }
  }
  if (response == NULL) {
    fprintf(stderr, "ERROR: Unable to create PPD file: Could not poll "
	    "sufficient capability info from the printer (%s, %s) via IPP!\n",
	    uri, resolve_uri(uri));
    goto fail;
  }

  /* Generate the PPD file */
  if (!cfCreatePPDFromIPP(ppdname, sizeof(ppdname), response, NULL, NULL, 0,
			  0, ppdgenerator_msg, sizeof(ppdgenerator_msg))) {
    if (strlen(ppdgenerator_msg) > 0)
      fprintf(stderr, "ERROR: Unable to create PPD file: %s\n",
	      ppdgenerator_msg);
    else if (errno != 0)
      fprintf(stderr, "ERROR: Unable to create PPD file: %s\n",
	      strerror(errno));
    else
      fprintf(stderr, "ERROR: Unable to create PPD file: Unknown reason\n");
    goto fail;
  } else if (debug) {
    fprintf(stderr, "DEBUG: PPD generation successful: %s\n", ppdgenerator_msg);
    fprintf(stderr, "DEBUG: Created temporary PPD file: %s\n", ppdname);
  }

  ippDelete(response);

  /* Output of PPD file to stdout */
  fd = open(ppdname, O_RDONLY);
  while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
    bytes = fwrite(buffer, 1, bytes, stdout);
  close(fd);
  unlink(ppdname);

  return 0;

 fail:
  if (response)
    ippDelete(response);

  return 1;
}

int
main(int argc, char*argv[]) {
  int i,
      reg_type_no = 1, /* reg_type 0 for only IPP
                                   1 for both IPPS/IPP
                                   2 for only IPPS        Default is 1*/
      isFax = 0;       /* if driverless-fax is called  0 - not called
			                               1 - called */
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

  if ((val = getenv("DEVICE_TYPE")) != NULL &&
      strncasecmp(val, "FAX", 3) == 0) {
    isFax = 1;
  }

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
	/* Exit immediatly when called as "driverless-fax", as CUPS always
	   also calls "driverless" and we list the fax PPDs already there,
	   to reduce the number of "ippfind" calls. */
	if (isFax) exit(0);
	uuids = cupsArrayNew((cups_array_func_t)strcmp, NULL);
	debug = 1;
	exit(list_printers(1, reg_type_no, 0));
      } else if (!strcasecmp(argv[i], "_ipps._tcp")) {
	/* reg_type_no = 2 for IPPS entries only*/
	reg_type_no = 2;
      } else if (!strcasecmp(argv[i], "_ipp._tcp")) {
	/* reg_type_no = 0 for IPP entries only*/
	reg_type_no = 0;
      } else if (!strcasecmp(argv[i], "--std-ipp-uris")) {
	/* Show URIS in standard form */
	exit(list_printers(-1, reg_type_no, isFax));
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
	  exit(generate_ppd(val, isFax));
	} else {
	  fprintf(stderr,
		  "Reading command line option \"cat\", no driver URI "
		  "supplied.\n\n");
	  goto help;
	}
      } else if (!strcasecmp(argv[i], "--version") ||
		 !strcasecmp(argv[i], "--help") ||
		 !strcasecmp(argv[i], "-h")) {
	/* Help!! */
	goto help;
      } else {
	/* Unknown option, consider as IPP printer URI */
	exit(generate_ppd(argv[i], isFax));
      }
  }

  /* Call without arguments, list printer URIs for all suitable printers
     when started manually, list printer URIs and metadata like CUPS
     backends do when started as CUPS backend (discovery mode only) */
  if ((val = getenv("SOFTWARE")) != NULL &&
      strncasecmp(val, "CUPS", 4) == 0) {
    /* CUPS backend in discovery mode */
    /* Exit immediatly when called as "driverless-fax", as CUPS always
       also calls "driverless" and the DNS-SD-service-name-based URIs
       are the same for both printer and fax. This way we reduce the
       number of "ippfind" calls. */
    if (isFax) exit(0);
    debug = 1;
    exit(list_printers(2, reg_type_no, 0));
  } else {
    /* Manual call */
    exit(list_printers(0, reg_type_no, isFax));
  }

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
	  "  list                    List the driver URIs and metadata for "
	                            "all available\n"
	  "                          IPP/IPPS printers supporting driverless "
	                            "printing\n"
	  "                          (to be used by CUPS).\n"
	  "  _ipps._tcp              Check for only IPPS printers supporting "
	                            "driverless\n"
	  "                          printing\n"
	  "  _ipp._tcp               Check for only IPP printers supporting "
	                            "driverless\n"
	  "                          printing\n"
	  "  --std-ipp-uris          Show URIS in standard form\n"
	  "  cat <driver URI>        Generate the PPD file for the driver URI\n"
	  "                          <driver URI> (to be used by CUPS).\n"
	  "  <printer URI>           Generate the PPD file for the IPP/IPPS "
	                            "printer URI\n"
	  "                          <printer URI>.\n"
	  "\n"
	  "When called without options, the IPP/IPPS printer URIs of all "
	  "available\n"
	  "IPP/IPPS printers will be listed.\n\n"
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

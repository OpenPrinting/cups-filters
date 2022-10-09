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
#include <ppd/ppd.h>
#include <cups/raster.h>
#include <cups/avahi.h>
#include <cupsfilters/ipp.h>
#include <cupsfilters/ipp.h>

/*
Local Globals
*/

#ifdef HAVE_MDNSRESPONDER
static DNSServiceRef dnssd_ref; /* Master service reference */
#elif defined(HAVE_AVAHI)
AvahiClient *avahi_client; /* Client information */
int avahi_got_data = 0;    /* Got data from poll? */
AvahiSimplePoll *avahi_poll;
#endif

int err;
static int bonjour_error = 0;        /* Error browsing/resolving? */
static double bonjour_timeout = 1.0; /* Timeout in seconds */
static double get_time(void);
static avahi_srv_t *get_service(cups_array_t *services, const char *serviceName, const char *regtype, const char *replyDomain) _CUPS_NONNULL(1, 2, 3, 4);
int reg_type_no = 1; /* reg_type 0 for only IPP
                                   1 for both IPPS/IPP
                                   2 for only IPPS        Default is 1*/

#define MAX_OUTPUT_LEN 8192

static int debug = 0;
static int job_canceled = 0;
static void cancel_job(int sig);
static cups_array_t *uuids = NULL;

static int
compare_service_uri(char *a, char *b)
{
  return (strcmp(a, b));
}

void listPrintersInArrayV2(int reg_type_no, int mode, int isFax,
                           avahi_srv_t *service)
{
  int port,
      is_local;
  char *ptr,          /* Pointer into string */
          *scheme = "\0",
          *service_name = "\0",
          *resource = "\0",
          *domain = "\0",
              *reg_type = "\0",
          *service_hostname = NULL,
          *txt_usb_mfg = "\0",
          *txt_usb_mdl = "\0",
          *txt_product = "\0",
          *txt_ty = "\0",
          *txt_pdl = "\0",
          *txt_uuid = "\0",
          *rp = "\0",
          *rfo = "\0",
          value[256],          /* Value string */
      *service_uri,            /* URI to list for this service */
      service_host_name[1024], /* "Host name" for assembling URI */
      make_and_model[1024],    /* Manufacturer and model */
      make[512],               /* Manufacturer */
      model[256],              /* Model */
      pdl[256],                /* PDL */
      device_id[2048];         /* 1284 device ID */

  service_uri = (char *)malloc(2048 * (sizeof(char)));
  /* Mark all the fields of the output of ippfind */
  reg_type = service->regtype;

  if(reg_type_no < 1)
  {
    scheme = "ipps";
    reg_type = "_ipp._tcp";
  }
  else if(reg_type_no > 1){
    scheme = "ipp";
    reg_type = "_ipps._tcp";
  }
    

  /*
      process txt key-value pairs
      */

  for (int i = 0; i < service->num_txt; i++)
  {
    char *currentKey = service->txt[i].name;
    char *currentValue = service->txt[i].value;

    // fprintf(stderr, "key[%d] = %s, value[%d] = %s\n", i, currentKey, i, currentValue);

    if (!strcmp(currentKey, "UUID"))
    {
      txt_uuid = currentValue;
    }
    else if (!strcmp(currentKey, "usb_MDL"))
    {
      txt_usb_mdl = currentValue;
    }
    else if (!strcmp(currentKey, "usb_MFG"))
    {
      txt_usb_mfg = currentValue;
    }
    else if (!strcmp(currentKey, "product"))
    {
      txt_product = currentValue;
    }
    else if (!strcmp(currentKey, "pdl"))
    {
      txt_pdl = currentValue;
    }
    else if (!strcmp(currentKey, "ty"))
    {
      txt_ty = currentValue;
    }
    else if (!strcmp(currentKey, "rp"))
    {
      rp = currentValue;
    }
    else if (!strcmp(currentKey, "rfo"))
    {
      rfo = currentValue;
    }
  
  }

  /* ... second, complete the output line, either URI-only or with
     extra info for CUPS */

  if (mode == -1)
  {

    /* Standard IPP URI (only manual call) */
    service_hostname = service->host;
    port = service->port;
    resource = rp; // for printer assign rp to resource

    /* Do we have a local service so that we have to set the host name to
       "localhost"? */
    is_local = service->is_local;

    httpAssembleURIf(HTTP_URI_CODING_ALL, service_uri,
                     2047,
                     scheme, NULL,
                     (is_local ? "localhost" : service_hostname),
                     port, "/%s", resource);
    printf("%s\n", service_uri);
  }
  else
  {
    /* DNS-SD-service-name-based URI */
    service_name = service->name;
    domain = service->domain;
    snprintf(service_host_name, sizeof(service_host_name) - 1, "%s.%s.%s",
             service_name, reg_type, domain);
    httpAssembleURIf(HTTP_URI_CODING_ALL, service_uri,
                     2047,
                     scheme, NULL,
                     service_host_name, 0, "/");

    if (mode == 0)
      /* Manual call, only show URI, nothing more */
      printf("%s\n", service_uri);
    else
    {
      /* Call by CUPS, either as PPD generator
   (/usr/lib/cups/driver/, with "list" command line argument)
   or as backend in discovery mode (/usr/lib/cups/backend/,
   env variable "SOFTWARE" starts with "CUPS") */

      /* We check only for a fax resource (rfo) here, if there is none,
   resource will stay blank meaning device does not support fax */
      resource = rfo;

      make_and_model[0] = '\0';
      make[0] = '\0';
      pdl[0] = '\0';
      device_id[0] = '\0';
      strncpy(model, "Unknown", sizeof(model) - 1);

      if (txt_usb_mfg[0] != '\0')
      {

        strncpy(make, txt_usb_mfg, sizeof(make) - 1);
        if (strlen(txt_usb_mfg) > 511)
          make[511] = '\0';
        ptr = device_id + strlen(device_id);
        snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
                 "MFG:%s;", txt_usb_mfg);
      }

      if (txt_usb_mdl[0] != '\0')
      {
        strncpy(model, txt_usb_mdl, sizeof(model) - 1);
        if (strlen(txt_usb_mdl) > 255)
          model[255] = '\0';
        ptr = device_id + strlen(device_id);
        snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id),
                 "MDL:%s;", txt_usb_mdl);
      }
      else if (txt_product[0] != '\0')
      {
        if (txt_product[0] == '(')
        {
          /* Strip parenthesis... */
          if ((ptr = txt_product + strlen(txt_product) - 1) > txt_product &&
              *ptr == ')')
            *ptr = '\0';
          strncpy(model, txt_product + 1, sizeof(model) - 1);
          if ((strlen(txt_product) + 1) > 255)
            model[255] = '\0';
        }
        else
          strncpy(model, txt_product, sizeof(model) - 1);
      }
      else if (txt_ty[0] != '\0')
      {
        strncpy(model, txt_ty, sizeof(model) - 1);
        if (strlen(txt_ty) > 255)
          model[255] = '\0';
        if ((ptr = strchr(model, ',')) != NULL)
          *ptr = '\0';
      }

      if (txt_pdl[0] != '\0')
      {
        strncpy(pdl, txt_pdl, sizeof(pdl) - 1);
        if (strlen(txt_pdl) > 255)
          pdl[255] = '\0';
      }

      if (!device_id[0] && strcasecmp(model, "Unknown"))
      {
        if (make[0])
          snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;",
                   make, model);
        else if (!strncasecmp(model, "designjet ", 10))
          snprintf(device_id, sizeof(device_id), "MFG:HP;MDL:%s;",
                   model + 10);
        else if (!strncasecmp(model, "stylus ", 7))
          snprintf(device_id, sizeof(device_id), "MFG:EPSON;MDL:%s;",
                   model + 7);
        else if ((ptr = strchr(model, ' ')) != NULL)
        {
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
           strcasestr(pdl, "image/")))
      {
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
             ptr = strcasestr(ptr, "image/"))
        {

          char *valptr = value + strlen(value);
          if (valptr < (value + sizeof(value) - 1))
            *valptr++ = ',';
          ptr += 6;
          while (isalnum(*ptr & 255) || *ptr == '-' || *ptr == '.')
          {
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

      if (mode == 1)
      {

        /* Only output the entry if we had this UUID not already */
        if (!txt_uuid[0] || !cupsArrayFind(uuids, txt_uuid))
        {
          /* Save UUID as if an entry with the same UUID appears again, it
             is the the same pair of print and fax PPDs */
          if (txt_uuid[0])
            cupsArrayAdd(uuids, strdup(txt_uuid));
          /* Call with "list" argument  (PPD generator in list mode)   */
          printf("\"%s%s\" en \"%s\" \"%s, %sdriverless, cups-filters " VERSION "\" \"%s\"\n",
                 ((isFax) ? "driverless-fax:" : "driverless:"),
                 service_uri, make, make_and_model,
                 ((isFax) ? "Fax, " : ""),
                 device_id);
          if (resource[0]) /* We have also fax on this device */
            printf("\"%s%s\" en \"%s\" \"%s, Fax, driverless, cups-filters " VERSION "\" \"%s\"\n",
                   "driverless-fax:",
                   service_uri, make, make_and_model,
                   device_id);
        }
      }
      else
      {
        /* Call without arguments and env variable "SOFTWARE" starting
           with "CUPS" (Backend in discovery mode) */
        printf("network %s \"%s\" \"%s (driverless)\" \"%s\" \"\"\n",
               service_uri, make_and_model, make_and_model,
               device_id);
      }
    }
  }

  free(service_uri);
  return;
}

void resolve_services(cups_array_t *ipps_services, cups_array_t *ipp_services, resolve_callback_t resolve_callback_ptr)
{

  double endTime;

  if (bonjour_timeout > 1.0)
    endTime = get_time() + bonjour_timeout;
  else
    endTime = get_time() + 300.0;

  while (get_time() < endTime)
  {

    avahi_got_data = 0;

    if (avahi_simple_poll_iterate(avahi_poll, 500) > 0)
    {
      /*
       * We've been told to exit the loop.  Perhaps the connection to
       * Avahi failed.
       */
      return;
    }

    if (!avahi_got_data)
    {

      int active = 0;
      int ipp_resolved = cupsArrayCount(ipp_services);
      int ipps_resolved = cupsArrayCount(ipps_services);

      if (reg_type_no > 1)
      {
        ipp_resolved = 0;
      }

      if (reg_type_no < 1)
      {
        ipps_resolved = 0;
      }

      for (avahi_srv_t *service = (avahi_srv_t *)cupsArrayFirst(ipps_services);
           service && reg_type_no >= 1;
           service = (avahi_srv_t *)cupsArrayNext(ipps_services))
      {

        if (!service->ref)
        {
          if (active < 50)
            resolveServices(&avahi_client, service, ipps_services, resolve_callback_ptr, &err);
        }
        else
        {
          active++;
        }

        if (service->is_resolved)
          ipps_resolved--;
      }

      for (avahi_srv_t *service = (avahi_srv_t *)cupsArrayFirst(ipp_services);
           service && reg_type_no <= 1;
           service = (avahi_srv_t *)cupsArrayNext(ipp_services))
      {

        if (!service->ref)
        {
          if (active < 50)
            resolveServices(&avahi_client, service, ipp_services, resolve_callback_ptr, &err);
        }
        else
        {
          active++;
        }

        if (service->is_resolved)
          ipp_resolved--;
      }

      if (!ipp_resolved && !ipps_resolved)
      {
        break;
      }
    }
  }
}

int list_printers(int mode, int reg_type_no, int isFax)
{
  int exit_status = 0;
  cups_array_t *service_uri_list_ipps, /* Array to store ippfind output for
            IPPS */
      *service_uri_list_ipp;           /* Array to store ippfind output for
            IPP */

  service_uri_list_ipps =
      cupsArrayNew3((cups_array_func_t)compare_service_uri, NULL, NULL, 0, NULL,
                    (cups_afree_func_t)free);
  service_uri_list_ipp =
      cupsArrayNew3((cups_array_func_t)compare_service_uri, NULL, NULL, 0, NULL,
                    (cups_afree_func_t)free);

   /*
        callback pointers
      */

  resolve_callback_t resolve_callback_ptr = &resolveCallback;
  poll_callback_t poll_callback_ptr = &pollCallback;
  browse_callback_t browse_callback_ptr = &browseCallback;
  client_callback_t client_callback_ptr = &clientCallback;


  /*
   * Use CUPS' ippfind utility to discover all printers designed for
   * driverless use (IPP Everywhere or Apple Raster), and only IPP
   * network printers, not CUPS queues, output all data elements needed
   * for our desired output.
   */

  /*
  add avahi calls to find services
  */

  char *regtype = reg_type_no >= 1 ? "_ipps._tcp" : "_ipp._tcp";

  avahiInitialize(&avahi_poll, &avahi_client, client_callback_ptr, poll_callback_ptr, &err);

  if(err){
    goto error;
  }

  if (reg_type_no >= 1)
  {
    regtype = "_ipps._tcp";
    browseServices(&avahi_client, regtype, NULL, service_uri_list_ipps, browse_callback_ptr, &err);

    if(err){
      goto error;
    }

  }

  if (reg_type_no <= 1)
  {
    regtype = "_ipp._tcp";
    browseServices(&avahi_client, regtype, NULL, service_uri_list_ipp, browse_callback_ptr, &err);

    if(err){
      goto error;
    }
  }

  resolve_services(service_uri_list_ipps, service_uri_list_ipp, resolve_callback_ptr);

  for (int i = 0; i < cupsArrayCount(service_uri_list_ipp) && reg_type_no <= 1; i++)
  {
    if(cupsArrayFind(service_uri_list_ipps, (char *)cupsArrayIndex(service_uri_list_ipp, i)) == NULL)
    listPrintersInArrayV2(0, mode, isFax, (avahi_srv_t *)cupsArrayIndex(service_uri_list_ipp, i));
  }

  for (int j = 0; j < cupsArrayCount(service_uri_list_ipps) && reg_type_no >= 1; j++)
  {
    listPrintersInArrayV2(2, mode, isFax,
                          (avahi_srv_t *)cupsArrayIndex(service_uri_list_ipps, j));
  }

error:
  cupsArrayDelete(service_uri_list_ipps);
  cupsArrayDelete(service_uri_list_ipp);
  return (exit_status);
}

int generate_ppd(const char *uri, int isFax)
{
  ipp_t *response = NULL;
  char buffer[65536], ppdname[1024], ppdgenerator_msg[1024];
  int fd,
      bytes;
  char *ptr1,
      *ptr2;

  /* Tread prefixes (CUPS PPD/driver URIs) */

  if (!strncasecmp(uri, "driverless:", 11))
  {
    uri += 11;
    isFax = 0;
  }
  else if (!strncasecmp(uri, "driverless-fax:", 15))
  {
    uri += 15;
    isFax = 1;
  }

  /* Request printer properties via IPP to generate a PPD file for the
     printer */

  response = cfGetPrinterAttributes4(uri, NULL, 0, NULL, 0, 1, isFax);

  if (debug)
  {
    ptr1 = cf_get_printer_attributes_log;
    while (ptr1)
    {
      ptr2 = strchr(ptr1, '\n');
      if (ptr2)
        *ptr2 = '\0';
      fprintf(stderr, "DEBUG2: %s\n", ptr1);
      if (ptr2)
        *ptr2 = '\n';
      ptr1 = ptr2 ? (ptr2 + 1) : NULL;
    }
  }
  if (response == NULL)
  {
    fprintf(stderr, "ERROR: Unable to create PPD file: Could not poll "
                    "sufficient capability info from the printer (%s, %s) via IPP!\n",
            uri, cfResolveURI(uri));
    goto fail;
  }

  /* Generate the PPD file */
  if (!ppdCreatePPDFromIPP(ppdname, sizeof(ppdname), response, NULL, NULL, 0,
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
  }
  else if (debug)
  {
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

int main(int argc, char *argv[])
{
  int i, isFax = 0; /* if driverless-fax is called  0 - not called
                               1 - called */

  char *val;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action; /* Actions for POSIX signals */
#endif                     /* HAVE_SIGACTION && !HAVE_SIGSET */

  /*
   * Do not run at all if the NO_DRIVERLESS_PPDS environment variable is set
   */

  if (getenv("NO_DRIVERLESS_PPDS"))
    return (0);

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
      strncasecmp(val, "FAX", 3) == 0)
  {
    isFax = 1;
  }

  /* Read command line options */
  if (argc >= 2)
  {
    for (i = 1; i < argc; i++)
      if (!strcasecmp(argv[i], "--debug") || !strcasecmp(argv[i], "-d") ||
          !strncasecmp(argv[i], "-v", 2))
      {
        /* Output debug messages on stderr also when not running under CUPS
           ("list" and "cat" options) */
        debug = 1;
      }
      else if (!strcasecmp(argv[i], "list"))
      {
        /* List a driver URI and metadata for each printer suitable for
           driverless printing */
        /* Exit immediatly when called as "driverless-fax", as CUPS always
           also calls "driverless" and we list the fax PPDs already there,
           to reduce the number of "ippfind" calls. */
        if (isFax)
          exit(0);
        uuids = cupsArrayNew((cups_array_func_t)strcmp, NULL);
        debug = 1;
        exit(list_printers(1, reg_type_no, 0));
      }
      else if (!strcasecmp(argv[i], "_ipps._tcp"))
      {
        /* reg_type_no = 2 for IPPS entries only*/
        reg_type_no = 2;
      }
      else if (!strcasecmp(argv[i], "_ipp._tcp"))
      {
        /* reg_type_no = 0 for IPP entries only*/
        reg_type_no = 0;
      }
      else if (!strcasecmp(argv[i], "--std-ipp-uris"))
      {
        /* Show URIS in standard form */
        exit(list_printers(-1, reg_type_no, isFax));
      }
      else if (!strncasecmp(argv[i], "cat", 3))
      {
        /* Generate the PPD file for the given driver URI */
        debug = 1;
        val = argv[i] + 3;
        if (strlen(val) == 0)
        {
          i++;
          if (i < argc && *argv[i] != '-')
            val = argv[i];
          else
            val = NULL;
        }
        if (val)
        {
          /* Generate PPD file */
          exit(generate_ppd(val, isFax));
        }
        else
        {
          fprintf(stderr,
                  "Reading command line option \"cat\", no driver URI "
                  "supplied.\n\n");
          goto help;
        }
      }
      else if (!strcasecmp(argv[i], "--version") ||
               !strcasecmp(argv[i], "--help") ||
               !strcasecmp(argv[i], "-h"))
      {
        /* Help!! */
        goto help;
      }
      else
      {
        /* Unknown option, consider as IPP printer URI */
        exit(generate_ppd(argv[i], isFax));
      }
  }

  /* Call without arguments, list printer URIs for all suitable printers
     when started manually, list printer URIs and metadata like CUPS
     backends do when started as CUPS backend (discovery mode only) */
  if ((val = getenv("SOFTWARE")) != NULL &&
      strncasecmp(val, "CUPS", 4) == 0)
  {
    /* CUPS backend in discovery mode */
    /* Exit immediatly when called as "driverless-fax", as CUPS always
       also calls "driverless" and the DNS-SD-service-name-based URIs
       are the same for both printer and fax. This way we reduce the
       number of "ippfind" calls. */
    if (isFax)
      exit(0);
    debug = 1;
    exit(list_printers(2, reg_type_no, 0));
  }
  else
  {
    /* Manual call */
    exit(list_printers(0, reg_type_no, isFax));
  }

help:

  fprintf(stderr,
          "\ndriverless of cups-filters version " VERSION "\n\n"
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
          "IPP/IPPS printers will be listed.\n\n");

  return 1;
}

/*
 * 'cancel_job()' - Flag the job as canceled.
 */

static void
cancel_job(int sig) /* I - Signal number (unused) */
{
  (void)sig;

  job_canceled = 1;
}

/*
  callback definitions
  */

/*
 * 'clientCallback()' - Avahi client callback function.
 */

void clientCallback(
    AvahiClient *client,    /* I - Client information (unused) */
    AvahiClientState state, /* I - Current state */
    void *context)          /* I - User data (unused) */
{
  (void)client;
  (void)context;

  /*
   * If the connection drops, quit.
   */

  if (state == AVAHI_CLIENT_FAILURE)
  {
    fputs("DEBUG: Avahi connection failed.\n", stderr);
    avahi_simple_poll_quit(avahi_poll);
  }
}

/*
 * 'browseCallback()' - Browse devices.
 */

void browseCallback(
    AvahiServiceBrowser *browser, /* I - Browser */
    AvahiIfIndex interface,       /* I - Interface index (unused) */
    AvahiProtocol protocol,       /* I - Network protocol (unused) */
    AvahiBrowserEvent event,      /* I - What happened */
    const char *name,             /* I - Service name */
    const char *type,             /* I - Registration type */
    const char *domain,           /* I - Domain */
    AvahiLookupResultFlags flags, /* I - Flags */
    void *context)                /* I - Services array */
{
  AvahiClient **client = (AvahiClient **)malloc(sizeof(AvahiClient *));
  *client = avahi_service_browser_get_client(browser);

  /* Client information */
  avahi_srv_t *service; /* Service information */

  (void)interface;
  (void)protocol;
  (void)context;

  switch (event)
  {
  case AVAHI_BROWSER_FAILURE:
    fprintf(stderr, "DEBUG: browseCallback: %s\n",
            avahi_strerror(avahi_client_errno(*client)));
    bonjour_error = 1;
    avahi_simple_poll_quit(avahi_poll);
    break;

  case AVAHI_BROWSER_NEW:
    /*
     * This object is new on the network. Create a device entry for it if
     * it doesn't yet exist.
     */

    service = get_service((cups_array_t *)context, name, type, domain);

    if (service == NULL)
      return;

    if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
      service->is_local = 1;
    break;

  case AVAHI_BROWSER_REMOVE:
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    break;
  }
}

#ifdef HAVE_AVAHI
/*
 * 'pollCallback()' - Wait for input on the specified file descriptors.
 *
 * Note: This function is needed because avahi_simple_poll_iterate is broken
 *       and always uses a timeout of 0 (!) milliseconds.
 *       (Avahi Ticket #364)
 */

int /* O - Number of file descriptors matching */
pollCallback(
    struct pollfd *pollfds,   /* I - File descriptors */
    unsigned int num_pollfds, /* I - Number of file descriptors */
    int timeout,              /* I - Timeout in milliseconds (unused) */
    void *context)            /* I - User data (unused) */
{
  int val; /* Return value */

  (void)timeout;
  (void)context;

  val = poll(pollfds, num_pollfds, 500);

  if (val > 0)
    avahi_got_data = 1;

  return (val);
}
#endif /* HAVE_AVAHI */

/*
 * 'resolveCallback()' - Process resolve data.
 */

#ifdef HAVE_MDNSRESPONDER
void DNSSD_API
resolveCallback(
    DNSServiceRef sdRef,            /* I - Service reference */
    DNSServiceFlags flags,          /* I - Data flags */
    uint32_t interfaceIndex,        /* I - Interface */
    DNSServiceErrorType errorCode,  /* I - Error, if any */
    const char *fullName,           /* I - Full service name */
    const char *hostTarget,         /* I - Hostname */
    uint16_t port,                  /* I - Port number (network byte order) */
    uint16_t txtLen,                /* I - Length of TXT record data */
    const unsigned char *txtRecord, /* I - TXT record data */
    void *context)                  /* I - Service */
{
  char key[256],               /* TXT key value */
      *value;                  /* Value from TXT record */
  const unsigned char *txtEnd; /* End of TXT record */
  uint8_t valueLen;            /* Length of value */
  avahi_srv_t *service = (avahi_srv_t *)context;
  /* Service */

  /*
   * Only process "add" data...
   */

  (void)sdRef;
  (void)flags;
  (void)interfaceIndex;
  (void)fullName;

  if (errorCode != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("ippfind: Unable to browse or resolve: %s"),
                    dnssd_error_string(errorCode));
    bonjour_error = 1;
    return;
  }

  service->is_resolved = 1;
  service->host = strdup(hostTarget);
  service->port = ntohs(port);

  value = service->host + strlen(service->host) - 1;
  if (value >= service->host && *value == '.')
    *value = '\0';

  /*
   * Loop through the TXT key/value pairs and add them to an array...
   */

  for (txtEnd = txtRecord + txtLen; txtRecord < txtEnd; txtRecord += valueLen)
  {
    /*
     * Ignore bogus strings...
     */

    valueLen = *txtRecord++;

    memcpy(key, txtRecord, valueLen);
    key[valueLen] = '\0';

    if ((value = strchr(key, '=')) == NULL)
      continue;

    *value++ = '\0';

    /*
     * Add to array of TXT values...
     */

    service->num_txt = cupsAddOption(key, value, service->num_txt,
                                     &(service->txt));
  }

  // set_service_uri(service);
}

#elif defined(HAVE_AVAHI)
void resolveCallback(
    AvahiServiceResolver *resolver, /* I - Resolver */
    AvahiIfIndex interface,         /* I - Interface */
    AvahiProtocol protocol,         /* I - Address protocol */
    AvahiResolverEvent event,       /* I - Event */
    const char *serviceName,        /* I - Service name */
    const char *regtype,            /* I - Registration type */
    const char *replyDomain,        /* I - Domain name */
    const char *hostTarget,         /* I - FQDN */
    const AvahiAddress *address,    /* I - Address */
    uint16_t port,                  /* I - Port number */
    AvahiStringList *txt,           /* I - TXT records */
    AvahiLookupResultFlags flags,   /* I - Lookup flags */
    void *context)                  /* I - Service */
{
  char key[256], /* TXT key */
      *value;    /* TXT value */
  avahi_srv_t *service = (avahi_srv_t *)context;

  /* Service */
  AvahiStringList *current; /* Current TXT key/value pair */

  (void)address;

  if (event != AVAHI_RESOLVER_FOUND)
  {
    bonjour_error = 1;

    avahi_service_resolver_free(resolver);
    avahi_simple_poll_quit(avahi_poll);
    return;
  }

  service->is_resolved = 1;

  if (hostTarget != NULL)
    service->host = strdup(hostTarget);

  service->port = port;

  value = service->host + strlen(service->host) - 1;
  if (value >= service->host && *value == '.')
    *value = '\0';

  /*
   * Loop through the TXT key/value pairs and add them to an array...
   */

  for (current = txt; current; current = current->next)
  {
    /*
     * Ignore bogus strings...
     */

    if (current->size > (sizeof(key) - 1))
      continue;

    memcpy(key, current->text, current->size);
    key[current->size] = '\0';

    if ((value = strchr(key, '=')) == NULL)
      continue;

    *value++ = '\0';

    /*
     * Add to array of TXT values...
     */

    service->num_txt = cupsAddOption(key, value, service->num_txt,
                                     &(service->txt));
  }

  // set_service_uri(service);
}
#endif /* HAVE_MDNSRESPONDER */

/*
 * 'get_service()' - Create or update a device.
 */

static avahi_srv_t *                 /* O - Service */
get_service(cups_array_t *services,  /* I - Service array */
            const char *serviceName, /* I - Name of service/device */
            const char *regtype,     /* I - Type of service */
            const char *replyDomain) /* I - Service domain */
{
  avahi_srv_t key, /* Search key */
      *service;    /* Service */
  char fullName[kDNSServiceMaxDomainName];
  /* Full name for query */

  /*
   * See if this is a new device...
   */

  key.name = (char *)serviceName;
  key.regtype = (char *)regtype;

  for (service = cupsArrayFind(services, &key);
       service;
       service = cupsArrayNext(services))
  {

    if (_cups_strcasecmp(service->name, key.name))
      break;
    else if (!strcmp(service->regtype, key.regtype))
    {
      return (service);
    }
  }
  /*
   * Yes, add the service...
   */

  if ((service = calloc(sizeof(avahi_srv_t), 1)) == NULL)
    return (NULL);

  service->name = strdup(serviceName);
  service->domain = strdup(replyDomain);
  service->regtype = strdup(regtype);

  cupsArrayAdd(services, service);

  /*
   * Set the "full name" of this service, which is used for queries and
   * resolves...
   */

#ifdef HAVE_MDNSRESPONDER
  DNSServiceConstructFullName(fullName, serviceName, regtype, replyDomain);
#else  /* HAVE_AVAHI */
  avahi_service_name_join(fullName, kDNSServiceMaxDomainName, serviceName,
                          regtype, replyDomain);
#endif /* HAVE_MDNSRESPONDER */

  service->fullName = strdup(fullName);

  return (service);
}

static double
get_time(void)
{
#ifdef _WIN32
  struct _timeb curtime; /* Current Windows time */

  _ftime(&curtime);

  return (curtime.time + 0.001 * curtime.millitm);

#else
  struct timeval curtime; /* Current UNIX time */

  if (gettimeofday(&curtime, NULL))
    return (0.0);
  else
    return (curtime.tv_sec + 0.000001 * curtime.tv_usec);
#endif /* _WIN32 */
}

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
  License along with avahi; if not, write to the Free Software
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
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <resolv.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <regex.h>
#include <pthread.h>

#include <glib.h>

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-glib/glib-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#endif /* HAVE_AVAHI */

#include <gio/gio.h>


#ifdef HAVE_LDAP
#  ifdef __sun
#    include <lber.h>
#  endif /* __sun */
#  include <ldap.h>
#  ifdef HAVE_LDAP_SSL_H
#    include <ldap_ssl.h>
#  endif /* HAVE_LDAP_SSL_H */
#endif /* HAVE_LDAP */


#ifdef HAVE_LDAP
LDAP		*BrowseLDAPHandle = NULL;
					/* Handle to LDAP server */
char		*BrowseLDAPBindDN = NULL,
					/* LDAP login DN */
			*BrowseLDAPDN = NULL,
					/* LDAP search DN */
			*BrowseLDAPPassword = NULL,
					/* LDAP login password */
			*BrowseLDAPServer = NULL,
					/* LDAP server to use */
			*BrowseLDAPFilter = NULL;
					/* LDAP query filter */
int			BrowseLDAPUpdate = TRUE,
					/* enables LDAP updates */
			BrowseLDAPInitialised = FALSE;
					/* the init stuff has been done */
#  ifdef HAVE_LDAP_SSL
char		*BrowseLDAPCACertFile = NULL;
					/* LDAP CA CERT file to use */
#  endif /* HAVE_LDAP_SSL */
#endif /* HAVE_LDAP */


#ifdef HAVE_LDAP
#define LDAP_BROWSE_FILTER "(objectclass=cupsPrinter)"
static LDAP	*ldap_connect(void);
static LDAP	*ldap_reconnect(void);
static void	ldap_disconnect(LDAP *ld);
static int	ldap_search_rec(LDAP *ld, char *base, int scope,
                                char *filter, char *attrs[],
                                int attrsonly, LDAPMessage **res);
static int	ldap_getval_firststring(LDAP *ld, LDAPMessage *entry,
                                        char *attr, char *retval,
                                        unsigned long maxsize);
static void	ldap_freeres(LDAPMessage *entry);
#  ifdef HAVE_LDAP_REBIND_PROC
#    if defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000)
static int	ldap_rebind_proc(LDAP *RebindLDAPHandle,
                                 LDAP_CONST char *refsp,
                                 ber_tag_t request,
                                 ber_int_t msgid,
                                 void *params);
#    else
static int	ldap_rebind_proc(LDAP *RebindLDAPHandle,
                                 char **dnp,
                                 char **passwdp,
                                 int *authmethodp,
                                 int freeit,
                                 void *arg);
#    endif /* defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000) */
#  endif /* HAVE_LDAP_REBIND_PROC */
#endif /* HAVE_LDAP */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <cupsfilters/ppdgenerator.h>

#include "cups-notifier.h"

/* Attribute to mark a CUPS queue as created by us */
#define CUPS_BROWSED_MARK "cups-browsed"

/* Attribute to tell the implicitclass backend the destination queue for
   the current job */
#define CUPS_BROWSED_DEST_PRINTER "cups-browsed-dest-printer"

/* Timeout values in sec */
#define TIMEOUT_IMMEDIATELY -1
#define TIMEOUT_CONFIRM     10
#define TIMEOUT_RETRY       10
#define TIMEOUT_REMOVE      -1
#define TIMEOUT_CHECK_LIST   2

#define NOTIFY_LEASE_DURATION (24 * 60 * 60)
#define CUPS_DBUS_NAME "org.cups.cupsd.Notifier"
#define CUPS_DBUS_PATH "/org/cups/cupsd/Notifier"
#define CUPS_DBUS_INTERFACE "org.cups.cupsd.Notifier"

#define DEFAULT_CACHEDIR "/var/cache/cups"
#define DEFAULT_LOGDIR "/var/log/cups"
#define LOCAL_DEFAULT_PRINTER_FILE "/cups-browsed-local-default-printer"
#define REMOTE_DEFAULT_PRINTER_FILE "/cups-browsed-remote-default-printer"
#define SAVE_OPTIONS_FILE "/cups-browsed-options-%s"
#define DEBUG_LOG_FILE "/cups-browsed_log"

/* Status of remote printer */
typedef enum printer_status_e {
  STATUS_UNCONFIRMED = 0,	/* Generated in a previous session */
  STATUS_CONFIRMED,		/* Avahi confirms UNCONFIRMED printer */
  STATUS_TO_BE_CREATED,		/* Scheduled for creation */
  STATUS_DISAPPEARED		/* Scheduled for removal */
} printer_status_t;

/* Data structure for remote printers */
typedef struct remote_printer_s {
  char *queue_name;
  char *location;
  char *info;
  char *uri;
  char *ppd;
  char *model;
  char *ifscript;
  int num_options;
  cups_option_t *options;
  printer_status_t status;
  time_t timeout;
  void *slave_of;
  int last_printer;
  char *host;
  char *ip;
  int port;
  char *service_name;
  char *type;
  char *domain;
  int no_autosave;
  int netprinter;
  int is_legacy;
} remote_printer_t;

/* Data structure for network interfaces */
typedef struct netif_s {
  char *address;
  http_addr_t broadcast;
} netif_t;

/* Data structures for browse allow/deny rules */
typedef enum browse_order_e {
  ORDER_ALLOW_DENY,
  ORDER_DENY_ALLOW
} browse_order_t;
typedef enum allow_type_e {
  ALLOW_IP,
  ALLOW_NET,
  ALLOW_INVALID
} allow_type_t;
typedef enum allow_sense_e {
  ALLOW_ALLOW,
  ALLOW_DENY
} allow_sense_t;
typedef struct allow_s {
  allow_type_t type;
  allow_sense_t sense;
  http_addr_t addr;
  http_addr_t mask;
} allow_t;

/* Data structures for browse filter rules */
typedef enum filter_sense_s {
  FILTER_MATCH,
  FILTER_NOT_MATCH
} filter_sense_t;
typedef struct browse_filter_s {
  filter_sense_t sense;
  char *field;
  char *regexp;
  regex_t *cregexp;
} browse_filter_t;

/* Data structure for a printer discovered using BrowsePoll */
typedef struct browsepoll_printer_s {
  char *uri_supported;
  char *location;
  char *info;
} browsepoll_printer_t;

/* Data structure for a BrowsePoll server */
typedef struct browsepoll_s {
  char *server;
  int port;
  int major;
  int minor;
  gboolean can_subscribe;
  int subscription_id;
  int sequence_number;

  /* Remember which printers we discovered. This way we can just ask
   * if anything has changed, and if not we know these printers are
   * still there. */
  GList *printers; /* of browsepoll_printer_t */
} browsepoll_t;

/* Data structure for destination list obtained with cupsEnumDests() */
typedef struct dest_list_s {
  int num_dests;
  cups_dest_t *dests;
} dest_list_t;

/* Local printer (key is name) */
typedef struct local_printer_s {
  char *device_uri;
  gboolean cups_browsed_controlled;
} local_printer_t;

/* Browse data to send for local printer */
typedef struct browse_data_s {
  int type;
  int state;
  char *uri;
  char *location;
  char *info;
  char *make_model;
  char *browse_options;
} browse_data_t;

/* Data structure for manual definition of load-balancing clusters */
typedef struct cluster_s {
  char *local_queue_name;
  cups_array_t *members;
} cluster_t;

/* Ways how to represent the remote printer's IP in the device URI */
typedef enum ip_based_uris_e {
  IP_BASED_URIS_NO,
  IP_BASED_URIS_ANY,
  IP_BASED_URIS_IPV4_ONLY,
  IP_BASED_URIS_IPV6_ONLY
} ip_based_uris_t;

/* Ways how to name local queues for remote printers */
typedef enum local_queue_naming_e {
  LOCAL_QUEUE_NAMING_DNSSD,
  LOCAL_QUEUE_NAMING_MAKE_MODEL,
  LOCAL_QUEUE_NAMING_REMOTE_NAME
} local_queue_naming_t;

/* Automatically create queues for IPP network printers: No, only for
   IPP printers, for all printers */
typedef enum create_ipp_printer_queues_e {
  IPP_PRINTERS_NO,
  IPP_PRINTERS_LOCAL_ONLY,
  IPP_PRINTERS_PWGRASTER,
  IPP_PRINTERS_APPLERASTER,
  IPP_PRINTERS_PCLM,
  IPP_PRINTERS_PDF,
  IPP_PRINTERS_DRIVERLESS,
  IPP_PRINTERS_ALL
} create_ipp_printer_queues_t;

/* Ways how to set up a queue for an IPP network printer */
typedef enum ipp_queue_type_e {
  PPD_YES,
  PPD_NO
} ipp_queue_type_t;

/* Ways how we can do load balancing on remote queues with the same name */
typedef enum load_balancing_type_e {
  QUEUE_ON_CLIENT,
  QUEUE_ON_SERVERS
} load_balancing_type_t;

/* Ways how inactivity for auto-shutdown is defined */
typedef enum autoshutdown_inactivity_type_e {
  NO_QUEUES,
  NO_JOBS
} autoshutdown_inactivity_type_t;

cups_array_t *remote_printers;
static char *alt_config_file = NULL;
static cups_array_t *command_line_config;
static cups_array_t *netifs;
static cups_array_t *browseallow;
static gboolean browseallow_all = FALSE;
static gboolean browsedeny_all = FALSE;
static browse_order_t browse_order;
static cups_array_t *browsefilter;

static GHashTable *local_printers;
static GHashTable *cups_supported_remote_printers;
static browsepoll_t *local_printers_context = NULL;
static http_t *local_conn = NULL;
static gboolean inhibit_local_printers_update = FALSE;

static GList *browse_data = NULL;

static CupsNotifier *cups_notifier = NULL;

static GMainLoop *gmainloop = NULL;
#ifdef HAVE_AVAHI
static AvahiGLibPoll *glib_poll = NULL;
static AvahiClient *client = NULL;
static AvahiServiceBrowser *sb1 = NULL, *sb2 = NULL;
static int avahi_present = 0;
#endif /* HAVE_AVAHI */
#ifdef HAVE_LDAP
static const char * const ldap_attrs[] =/* CUPS LDAP attributes */
		{
		  "printerDescription",
		  "printerLocation",
		  "printerMakeAndModel",
		  "printerType",
		  "printerURI",
		  NULL
		};
#endif /* HAVE_LDAP */
static guint queues_timer_id = 0;
static int browsesocket = -1;

#define BROWSE_DNSSD (1<<0)
#define BROWSE_CUPS  (1<<1)
#define BROWSE_LDAP  (1<<2)
static unsigned int BrowseLocalProtocols = 0;
static unsigned int BrowseRemoteProtocols = BROWSE_DNSSD;
static unsigned int BrowseInterval = 60;
static unsigned int BrowseTimeout = 300;
static uint16_t BrowsePort = 631;
static browsepoll_t **BrowsePoll = NULL;
static size_t NumBrowsePoll = 0;
static guint update_netifs_sourceid = 0;
static char local_server_str[1024];
static char *DomainSocket = NULL;
static unsigned int HttpLocalTimeout = 5;
static unsigned int HttpRemoteTimeout = 10;
static ip_based_uris_t IPBasedDeviceURIs = IP_BASED_URIS_NO;
static local_queue_naming_t LocalQueueNamingRemoteCUPS=LOCAL_QUEUE_NAMING_DNSSD;
static local_queue_naming_t LocalQueueNamingIPPPrinter=LOCAL_QUEUE_NAMING_DNSSD;
static unsigned int OnlyUnsupportedByCUPS = 0;
static unsigned int CreateRemoteRawPrinterQueues = 0;
static unsigned int CreateRemoteCUPSPrinterQueues = 1;
#ifdef DRIVERLESS_IPP_PRINTERS_AUTO_SETUP
static create_ipp_printer_queues_t CreateIPPPrinterQueues = IPP_PRINTERS_DRIVERLESS;
#else
static create_ipp_printer_queues_t CreateIPPPrinterQueues = IPP_PRINTERS_LOCAL_ONLY;
#endif
static ipp_queue_type_t IPPPrinterQueueType = PPD_YES;
static int NewIPPPrinterQueuesShared = 0;
static int AutoClustering = 1;
static cups_array_t *clusters;
static load_balancing_type_t LoadBalancingType = QUEUE_ON_CLIENT;
static const char *DefaultOptions = NULL;
static int terminating = 0; /* received SIGTERM, ignore callbacks,
			       break loops */
static int in_shutdown = 0;
static int autoshutdown = 0;
static int autoshutdown_avahi = 0;
static int autoshutdown_timeout = 30;
static autoshutdown_inactivity_type_t autoshutdown_on = NO_QUEUES;
static guint autoshutdown_exec_id = 0;
static const char *default_printer = NULL;

static int debug_stderr = 0;
static int debug_logfile = 0;
static FILE *lfp = NULL;

static char cachedir[1024];
static char logdir[1024];
static char local_default_printer_file[1024];
static char remote_default_printer_file[1024];
static char save_options_file[1024];
static char debug_log_file[1024];

static void recheck_timer (void);
static void browse_poll_create_subscription (browsepoll_t *context,
					     http_t *conn);
static gboolean browse_poll_get_notifications (browsepoll_t *context,
					       http_t *conn);
static remote_printer_t
*examine_discovered_printer_record(const char *host,
				   const char *ip,
				   uint16_t port,
				   char *resource,
				   const char *service_name,
				   const char *location,
				   const char *info,
				   const char *type,
				   const char *domain,
				   void *txt);

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

/*
 * CUPS 1.6 makes various structures private and
 * introduces these ippGet and ippSet functions
 * for all of the fields in these structures.
 * http://www.cups.org/str.php?L3928
 * We define (same signatures) our own accessors when CUPS < 1.6.
 */
#ifndef HAVE_CUPS_1_6
const char *
ippGetName(ipp_attribute_t *attr)
{
  return (attr->name);
}

ipp_op_t
ippGetOperation(ipp_t *ipp)
{
  return (ipp->request.op.operation_id);
}

ipp_status_t
ippGetStatusCode(ipp_t *ipp)
{
  return (ipp->request.status.status_code);
}

ipp_tag_t
ippGetGroupTag(ipp_attribute_t *attr)
{
  return (attr->group_tag);
}

ipp_tag_t
ippGetValueTag(ipp_attribute_t *attr)
{
  return (attr->value_tag);
}

int
ippGetCount(ipp_attribute_t *attr)
{
  return (attr->num_values);
}

int
ippGetInteger(ipp_attribute_t *attr,
              int             element)
{
  return (attr->values[element].integer);
}

int
ippGetBoolean(ipp_attribute_t *attr,
              int             element)
{
  return (attr->values[element].boolean);
}

const char *
ippGetString(ipp_attribute_t *attr,
             int             element,
             const char      **language)
{
  return (attr->values[element].string.text);
}

ipp_attribute_t	*
ippFirstAttribute(ipp_t *ipp)
{
  if (!ipp)
    return (NULL);
  return (ipp->current = ipp->attrs);
}

ipp_attribute_t *
ippNextAttribute(ipp_t *ipp)
{
  if (!ipp || !ipp->current)
    return (NULL);
  return (ipp->current = ipp->current->next);
}

int
ippSetVersion(ipp_t *ipp, int major, int minor)
{
  if (!ipp || major < 0 || minor < 0)
    return (0);
  ipp->request.any.version[0] = major;
  ipp->request.any.version[1] = minor;
  return (1);
}
#endif


#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

/*
 * The httpAddrPort() function was only introduced in CUPS 1.7.x
 */
#ifndef HAVE_CUPS_1_7
int                                     /* O - Port number */
httpAddrPort(http_addr_t *addr)         /* I - Address */
{
  if (!addr)
    return (-1);
#ifdef AF_INET6
  else if (addr->addr.sa_family == AF_INET6)
    return (ntohs(addr->ipv6.sin6_port));
#endif /* AF_INET6 */
  else if (addr->addr.sa_family == AF_INET)
    return (ntohs(addr->ipv4.sin_port));
  else
    return (0);
}
#endif


#if (CUPS_VERSION_MAJOR > 1)
#define HAVE_CUPS_2_0 1
#endif


void
start_debug_logging()
{
  if (debug_log_file[0] == '\0')
    return;
  if (lfp == NULL)
    lfp = fopen(debug_log_file, "a+");
  if (lfp == NULL) {
    fprintf(stderr, "cups-browsed: ERROR: Failed creating debug log file %s\n",
	    debug_log_file);
    exit(1);
  }
}

void
stop_debug_logging()
{
  debug_logfile = 0;
  if (lfp)
    fclose(lfp);
  lfp = NULL;
}

void
debug_printf(const char *format, ...) {
  if (debug_stderr || debug_logfile) {
    time_t curtime = time(NULL);
    char buf[64];
    ctime_r(&curtime, buf);
    while(isspace(buf[strlen(buf)-1])) buf[strlen(buf)-1] = '\0';
    va_list arglist;
    if (debug_stderr) {
      va_start(arglist, format);
      fprintf(stderr, "%s ", buf);
      vfprintf(stderr, format, arglist);
      fflush(stderr);
      va_end(arglist);
    }
    if (debug_logfile && lfp) {
      va_start(arglist, format);
      fprintf(lfp, "%s ", buf);
      vfprintf(lfp, format, arglist);
      fflush(lfp);
      va_end(arglist);
    }
  }
}

static const char *
password_callback (const char *prompt,
		   http_t *http,
		   const char *method,
		   const char *resource,
		   void *user_data)
{
  return NULL;
}

http_t *
httpConnectEncryptShortTimeout(const char *host, int port,
			       http_encryption_t encryption)
{
  return (httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 3000,
                       NULL));
}

int
http_timeout_cb(http_t *http, void *user_data)
{
  debug_printf("HTTP timeout! (consider increasing HttpLocalTimeout/HttpRemoteTimeout value)\n");
  return 0;
}

static http_t *
http_connect_local (void)
{
  if (!local_conn) {
    debug_printf("cups-browsed: Creating http connection to local CUPS daemon: %s:%d\n", cupsServer(), ippPort());
    local_conn = httpConnectEncryptShortTimeout(cupsServer(), ippPort(),
						cupsEncryption());
  }
  if (local_conn)
    httpSetTimeout(local_conn, HttpLocalTimeout, http_timeout_cb, NULL);
  else
    debug_printf("cups-browsed: Failed creating http connection to local CUPS daemon: %s:%d\n", cupsServer(), ippPort());

  return local_conn;
}

static void
http_close_local (void)
{
  if (local_conn) {
    httpClose (local_conn);
    local_conn = NULL;
  }
}


/*
 * Remove all illegal characters and replace each group of such characters
 * by a single separator character (dash or underscore), return a free()-able
 * string.
 *
 * mode = 0: Only allow letters, numbers, dashes, and underscores for
 *           turning make/model info into a valid print queue name or
 *           into a string which can be supplied as option value in a
 *           filter command line without need of quoting. Replace all
 *           groups of illegal characters by single dashes and remove
 *           leading and trailing dashes.
 * mode = 1: Allow also '/', '.', ',' for cleaning up MIME type
 *           strings (here available Page Description Languages, PDLs) to
 *           supply them on a filter command line without quoting.
 *           Replace all groups of illegal characters by single dashes
 *           and remove leading and trailing dashes.
 * mode = 2: Keep all locale-free alphanumeric characters (a-z, A-Z, 0-9)
 *           and replace everything else by underscores. Replace all
 *           groups of illegal characters by single underscores. This is
 *           for generating print queue names from DNS-SD service names
 *           to do it exactly as CUPS 2.2.x (or newer) does, so that CUPS
 *           does not create its own temporary queues in addition.
 *
 * Especially this prevents from arbitrary code execution by interface scripts
 * generated for print queues to native IPP printers when a malicious IPP
 * print service with forged PDL and/or make/model info gets broadcasted into
 * the local network.
 */

char *                                 /* O - Cleaned string */
remove_bad_chars(const char *str_orig, /* I - Original string */
		 int mode)             /* I - 0: Make/Model, queue name */
                                       /*     1: MIME types/PDLs */
                                       /*     2: Queue name from DNS-SD */
                                       /*        service name */
{
  int i, j;
  int havesep = 0;
  char sep, *str;

  if (str_orig == NULL)
    return NULL;

  str = strdup(str_orig);

  /* for later str[strlen(str)-1] access */
  if (strlen(str) < 1)
    return str;

  /* Select separator character */
  if (mode == 2)
    sep = '_';
  else
    sep = '-';

  for (i = 0, j = 0; i < strlen(str); i++, j++) {
    if (((str[i] >= 'A') && (str[i] <= 'Z')) ||
	((str[i] >= 'a') && (str[i] <= 'z')) ||
	((str[i] >= '0') && (str[i] <= '9')) ||
	(mode != 2 && (str[i] == '_' ||
		       str[i] == '.')) ||
	(mode == 1 && (str[i] == '/' ||
		       str[i] == ','))) {
      /* Allowed character, keep it */
      havesep = 0;
      str[j] = str[i];
    } else {
      /* Replace all other characters by a single separator */
      if (havesep == 1)
	j --;
      else {
	havesep = 1;
	str[j] = sep;
      }
    }
  }
  /* Add terminating zero */
  str[j] = '\0';

  i = 0;
  if (mode != 2) {
    /* Cut off trailing separators */
    while (strlen(str) > 0 && str[strlen(str)-1] == sep)
      str[strlen(str)-1] = '\0';

    /* Cut off leading separators */
    while (str[i] == sep)
      ++i;
  }

  /* keep a free()-able string. +1 for trailing \0 */
  return memmove(str, str + i, strlen(str) - i + 1);
}


static local_printer_t *
new_local_printer (const char *device_uri,
		   gboolean cups_browsed_controlled)
{
  local_printer_t *printer = g_malloc (sizeof (local_printer_t));
  printer->device_uri = strdup (device_uri);
  printer->cups_browsed_controlled = cups_browsed_controlled;
  return printer;
}

static void
free_local_printer (gpointer data)
{
  local_printer_t *printer = data;
  debug_printf("free_local_printer() in THREAD %ld\n", pthread_self());
  free (printer->device_uri);
  free (printer);
}

static gboolean
local_printer_has_uri (gpointer key,
		       gpointer value,
		       gpointer user_data)
{
  local_printer_t *printer = value;
  char *device_uri = user_data;
  debug_printf("local_printer_has_uri() in THREAD %ld\n", pthread_self());
  return g_str_equal (printer->device_uri, device_uri);
}

static gboolean
local_printer_service_name_matches (gpointer key,
				    gpointer value,
				    gpointer user_data)
{
  char *queue_name = key;
  char *service_name = user_data;
  char *p;
  debug_printf("local_printer_service_name_matches() in THREAD %ld\n",
	       pthread_self());
  p = remove_bad_chars(service_name, 2);
  if (p && strncasecmp(p, queue_name, 63) == 0) {
    free(p);
    return TRUE;
  }
  if (p) free(p);
  return FALSE;
}

static void
local_printers_create_subscription (http_t *conn)
{
  char temp[1024];
  if (!local_printers_context) {
    local_printers_context = g_malloc0 (sizeof (browsepoll_t));
    /* The httpGetAddr() function was introduced in CUPS 2.0.0 */
#ifdef HAVE_CUPS_2_0
    local_printers_context->server =
      strdup(httpAddrString(httpGetAddress(conn),
			    temp, sizeof(temp)));
    local_printers_context->port = httpAddrPort(httpGetAddress(conn));
#else
    local_printers_context->server = cupsServer();
    local_printers_context->port = ippPort();
#endif
    local_printers_context->can_subscribe = TRUE;
  }

  browse_poll_create_subscription (local_printers_context, conn);
}

int
add_dest_cb(dest_list_t *user_data, unsigned flags, cups_dest_t *dest)
{
  if (flags & CUPS_DEST_FLAGS_REMOVED)
    /* Remove destination from array */
    user_data->num_dests =
      cupsRemoveDest(dest->name, dest->instance, user_data->num_dests,
		     &(user_data->dests));
  else
    /* Add destination to array... */
    user_data->num_dests =
      cupsCopyDest(dest, user_data->num_dests,
		   &(user_data->dests));
  return (1);
}

static void
get_local_printers (void)
{
  dest_list_t dest_list = {0, NULL};
  /* We only want to have a list of actually existing CUPS queues, not of
     DNS-SD-discovered printers for which CUPS can auto-setup a driverless
     print queue */
  if (OnlyUnsupportedByCUPS)
    cupsEnumDests(CUPS_DEST_FLAGS_NONE, 1000, NULL, 0, 0,
		  (cups_dest_cb_t)add_dest_cb, &dest_list);
  else
    cupsEnumDests(CUPS_DEST_FLAGS_NONE, 1000, NULL, CUPS_PRINTER_LOCAL,
		  CUPS_PRINTER_DISCOVERED, (cups_dest_cb_t)add_dest_cb,
		  &dest_list);
  debug_printf ("cups-browsed (%s): cupsEnumDests\n", local_server_str);
  g_hash_table_remove_all (local_printers);
  if (OnlyUnsupportedByCUPS)
    g_hash_table_remove_all (cups_supported_remote_printers);
  int num_dests = dest_list.num_dests;
  cups_dest_t *dests = dest_list.dests;
  for (int i = 0; i < num_dests; i++) {
    const char *val;
    cups_dest_t *dest = &dests[i];
    local_printer_t *printer;
    gboolean cups_browsed_controlled;
    gboolean is_temporary;
    gboolean is_cups_supported_remote;

    const char *device_uri = cupsGetOption ("device-uri",
					    dest->num_options,
					    dest->options);

    /* Temporary CUPS queue? */
    val = cupsGetOption ("printer-is-temporary",
			 dest->num_options,
			 dest->options);
    is_temporary = (val && (!strcasecmp (val, "yes") ||
			    !strcasecmp (val, "on") ||
			    !strcasecmp (val, "true")));

    if (OnlyUnsupportedByCUPS) {
      /* Printer discovered by DNS-SD and supported by CUPS' temporary
	 queues? */
      val = cupsGetOption ("printer-uri-supported",
			   dest->num_options,
			   dest->options);
      /* Printer has no local CUPS queue but CUPS would create a
	 temporary queue on-demand */
      is_cups_supported_remote = (val == NULL || is_temporary);
    } else {
      is_cups_supported_remote = 0;
      if (is_temporary)
	continue;
    }

    val = cupsGetOption (CUPS_BROWSED_MARK,
			 dest->num_options,
			 dest->options);
    cups_browsed_controlled = val && (!strcasecmp (val, "yes") ||
				      !strcasecmp (val, "on") ||
				      !strcasecmp (val, "true"));
    printer = new_local_printer (device_uri,
				 cups_browsed_controlled);

    if (is_cups_supported_remote)
      g_hash_table_insert (cups_supported_remote_printers,
			   g_ascii_strdown (dest->name, -1),
			   printer);
    else
      g_hash_table_insert (local_printers,
			   g_ascii_strdown (dest->name, -1),
			   printer);
  }

  cupsFreeDests (num_dests, dests);
}

static browse_data_t *
new_browse_data (int type, int state, const gchar *uri,
		 const gchar *location, const gchar *info,
		 const gchar *make_model, const gchar *browse_options)
{
  browse_data_t *data = g_malloc (sizeof (browse_data_t));
  data->type = type;
  data->state = state;
  data->uri = g_strdup (uri);
  data->location = g_strdup (location);
  data->info = g_strdup (info);
  data->make_model = g_strdup (make_model);
  data->browse_options = g_strdup (browse_options);
  return data;
}

static void
browse_data_free (gpointer data)
{
  browse_data_t *bdata = data;
  debug_printf("browse_data_free() in THREAD %ld\n", pthread_self());
  g_free (bdata->uri);
  g_free (bdata->location);
  g_free (bdata->info);
  g_free (bdata->make_model);
  g_free (bdata->browse_options);
  g_free (bdata);
}

static void
prepare_browse_data (void)
{
  static const char * const rattrs[] = { "printer-type",
					 "printer-state",
					 "printer-uri-supported",
					 "printer-info",
					 "printer-location",
					 "printer-make-and-model",
					 "auth-info-required",
					 "printer-uuid",
					 "job-template" };
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;
  http_t *conn = NULL;

  conn = http_connect_local ();

  if (conn == NULL) {
    debug_printf("Browse send failed to connect to localhost\n");
    goto fail;
  }

  request = ippNewRequest(CUPS_GET_PRINTERS);
  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes", sizeof (rattrs) / sizeof (rattrs[0]),
		 NULL, rattrs);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());

  debug_printf("preparing browse data\n");
  response = cupsDoRequest (conn, request, "/");
  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("browse send failed for localhost: %s\n",
		 cupsLastErrorString ());
    goto fail;
  }

  g_list_free_full (browse_data, browse_data_free);
  browse_data = NULL;
  for (attr = ippFirstAttribute(response); attr;
       attr = ippNextAttribute(response)) {
    int type = -1, state = -1;
    const char *uri = NULL;
    gchar *location = NULL;
    gchar *info = NULL;
    gchar *make_model = NULL;
    GString *browse_options = g_string_new ("");

    /* Skip any non-printer attributes */
    while (attr && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
      attr = ippNextAttribute(response);

    if (!attr)
      break;

    while (attr && ippGetGroupTag(attr) == IPP_TAG_PRINTER) {
      const char *attrname = ippGetName(attr);
      int value_tag = ippGetValueTag(attr);

      if (!strcasecmp(attrname, "printer-type") &&
	  value_tag == IPP_TAG_ENUM) {
	type = ippGetInteger(attr, 0);
	if (type & CUPS_PRINTER_NOT_SHARED) {
	  /* Skip CUPS queues not marked as shared */
	  state = -1;
	  type = -1;
	  break;
	}
      } else if (!strcasecmp(attrname, "printer-state") &&
	       value_tag == IPP_TAG_ENUM)
	state = ippGetInteger(attr, 0);
      else if (!strcasecmp(attrname, "printer-uri-supported") &&
	       value_tag == IPP_TAG_URI)
	uri = ippGetString(attr, 0, NULL);
      else if (!strcasecmp(attrname, "printer-location") &&
	       value_tag == IPP_TAG_TEXT) {
	/* Remove quotes */
	gchar **tokens = g_strsplit (ippGetString(attr, 0, NULL), "\"", -1);
	location = g_strjoinv ("", tokens);
	g_strfreev (tokens);
      } else if (!strcasecmp(attrname, "printer-info") &&
		 value_tag == IPP_TAG_TEXT) {
	/* Remove quotes */
	gchar **tokens = g_strsplit (ippGetString(attr, 0, NULL), "\"", -1);
	info = g_strjoinv ("", tokens);
	g_strfreev (tokens);
      } else if (!strcasecmp(attrname, "printer-make-and-model") &&
		 value_tag == IPP_TAG_TEXT) {
	/* Remove quotes */
	gchar **tokens = g_strsplit (ippGetString(attr, 0, NULL), "\"", -1);
	make_model = g_strjoinv ("", tokens);
	g_strfreev (tokens);
      } else if (!strcasecmp(attrname, "auth-info-required") &&
		 value_tag == IPP_TAG_KEYWORD) {
	if (strcasecmp (ippGetString(attr, 0, NULL), "none"))
	  g_string_append_printf (browse_options, "auth-info-required=%s ",
				  ippGetString(attr, 0, NULL));
      } else if (!strcasecmp(attrname, "printer-uuid") &&
		 value_tag == IPP_TAG_URI)
	g_string_append_printf (browse_options, "uuid=%s ",
				ippGetString(attr, 0, NULL));
      else if (!strcasecmp(attrname, "job-sheets-default") &&
	       value_tag == IPP_TAG_NAME &&
	       ippGetCount(attr) == 2)
	g_string_append_printf (browse_options, "job-sheets=%s,%s ",
				ippGetString(attr, 0, NULL),
				ippGetString(attr, 1, NULL));
      else if (strstr(attrname, "-default")) {
	gchar *name = g_strdup (attrname);
	gchar *value = NULL;
	*strstr (name, "-default") = '\0';

	switch (value_tag) {
	  gchar **tokens;

	case IPP_TAG_KEYWORD:
	case IPP_TAG_STRING:
	case IPP_TAG_NAME:
	  /* Escape value */
	  tokens = g_strsplit_set (ippGetString(attr, 0, NULL),
				   " \"\'\\", -1);
	  value = g_strjoinv ("\\", tokens);
	  g_strfreev (tokens);
	  break;

	default:
	  /* other values aren't needed? */
	  debug_printf("skipping %s (%d)\n", name, value_tag);
	  break;
	}

	if (value) {
	  g_string_append_printf (browse_options, "%s=%s ", name, value);
	  g_free (value);
	}

	g_free (name);
      }

      attr = ippNextAttribute(response);
    }

    if (type != -1 && state != -1 && uri && location && info && make_model) {
      gchar *browse_options_str = g_string_free (browse_options, FALSE);
      browse_data_t *data;
      browse_options = NULL;
      g_strchomp (browse_options_str);
      data = new_browse_data (type, state, uri, location,
			      info, make_model, browse_options_str);
      browse_data = g_list_insert (browse_data, data, 0);
      g_free (browse_options_str);
    }

    if (make_model)
      g_free (make_model);

    if (info)
      g_free (info);

    if (location)
      g_free (location);

    if (browse_options)
      g_string_free (browse_options, TRUE);

    if (!attr)
      break;
  }

 fail:
  if (response)
    ippDelete(response);
}

static void
update_local_printers (void)
{
  gboolean get_printers = FALSE;
  http_t *conn;

  if (inhibit_local_printers_update)
    return;

  conn = http_connect_local ();
  if (conn &&
      (!local_printers_context || local_printers_context->can_subscribe)) {
    if (!local_printers_context ||
	local_printers_context->subscription_id == -1) {
      /* No subscription yet. First, create the subscription. */
      local_printers_create_subscription (conn);
      get_printers = TRUE;
    } else
      /* We already have a subscription, so use it. */

      /* Note: for the moment, browse_poll_get_notifications() just
       * tells us whether we should re-fetch the printer list, so it
       * is safe to use here. */
      get_printers = browse_poll_get_notifications (local_printers_context,
						    conn);
  } else
    get_printers = TRUE;

  if (get_printers) {
    get_local_printers ();

    if (BrowseLocalProtocols & BROWSE_CUPS)
      prepare_browse_data ();
  }
}

int
check_jobs () {
  int num_jobs = 0;
  cups_job_t *jobs = NULL;
  remote_printer_t *p;
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to check whether there are still jobs.\n");
    return 0;
  }

  if (cupsArrayCount(remote_printers) > 0)
    for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 p;
	 p = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!p->slave_of) {
	num_jobs = cupsGetJobs2(conn, &jobs, p->queue_name, 0,
				CUPS_WHICHJOBS_ACTIVE);
	if (num_jobs > 0) {
	  debug_printf("Queue %s still has jobs!\n", p->queue_name);
	  cupsFreeJobs(num_jobs, jobs);
	  return 1;
	}
      }

  debug_printf("All our remote printers are without jobs.\n");
  return 0;
}

gboolean
autoshutdown_execute (gpointer data)
{
  debug_printf("autoshutdown_execute() in THREAD %ld\n", pthread_self());
  /* Are we still in auto shutdown mode and are we still without queues or
     jobs*/
  if (autoshutdown &&
      (cupsArrayCount(remote_printers) == 0 ||
       (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
    debug_printf("Automatic shutdown as there are no print queues maintained by us or no jobs on them for %d sec.\n",
		 autoshutdown_timeout);
    g_main_loop_quit(gmainloop);
    g_main_context_wakeup(NULL);
  }

  /* Stop this timeout handler, we needed it only once */
  return FALSE;
}

int
color_space_score(const char *color_space)
{
  int score = 0;
  const char *p = color_space;

  if (!p) return -1;
  if (!strncasecmp(p, "s", 1)) {
    p += 1;
    score += 2;
  } else if (!strncasecmp(p, "adobe", 5)) {
    p += 5;
    score += 1;
  } else
    score += 3;
  if (!strncasecmp(p, "black", 5)) {
    p += 5;
    score += 1000;
  } else if (!strncasecmp(p, "gray", 4)) {
    p += 4;
    score += 2000;
  } else if (!strncasecmp(p, "cmyk", 4)) {
    p += 4;
    score += 4000;
  } else if (!strncasecmp(p, "cmy", 3)) {
    p += 3;
    score += 3000;
  } else if (!strncasecmp(p, "rgb", 3)) {
    p += 3;
    score += 5000;
  } 
  if (!strncasecmp(p, "-", 1) || !strncasecmp(p, "_", 1)) {
    p += 1;
  }
  score += strtol(p, (char **)&p, 10) * 10;
  debug_printf("Score for color space %s: %d\n", color_space, score);
  return score;
}


#ifdef HAVE_LDAP_REBIND_PROC
#  if defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000)
/*
 * 'ldap_rebind_proc()' - Callback function for LDAP rebind
 */

static int				/* O - Result code */
ldap_rebind_proc(
    LDAP            *RebindLDAPHandle,	/* I - LDAP handle */
    LDAP_CONST char *refsp,		/* I - ??? */
    ber_tag_t       request,		/* I - ??? */
    ber_int_t       msgid,		/* I - ??? */
    void            *params)		/* I - ??? */
{
  int		rc;			/* Result code */
#    if LDAP_API_VERSION > 3000
  struct berval	bval;			/* Bind value */
#    endif /* LDAP_API_VERSION > 3000 */


  (void)request;
  (void)msgid;
  (void)params;

 /*
  * Bind to new LDAP server...
  */

  debug_printf("ldap_rebind_proc: Rebind to %s\n", refsp);

#    if LDAP_API_VERSION > 3000
  bval.bv_val = BrowseLDAPPassword;
  bval.bv_len = (BrowseLDAPPassword == NULL) ? 0 : strlen(BrowseLDAPPassword);

  rc = ldap_sasl_bind_s(RebindLDAPHandle, BrowseLDAPBindDN, LDAP_SASL_SIMPLE,
                        &bval, NULL, NULL, NULL);
#    else
  rc = ldap_bind_s(RebindLDAPHandle, BrowseLDAPBindDN, BrowseLDAPPassword,
                   LDAP_AUTH_SIMPLE);
#    endif /* LDAP_API_VERSION > 3000 */

  return (rc);
}


#  else /* defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000) */
/*
 * 'ldap_rebind_proc()' - Callback function for LDAP rebind
 */

static int				/* O - Result code */
ldap_rebind_proc(
    LDAP *RebindLDAPHandle,		/* I - LDAP handle */
    char **dnp,				/* I - ??? */
    char **passwdp,			/* I - ??? */
    int  *authmethodp,			/* I - ??? */
    int  freeit,			/* I - ??? */
    void *arg)				/* I - ??? */
{
  switch (freeit)
  {
    case 1:
       /*
        * Free current values...
        */

        debug_printf("ldap_rebind_proc: Free values...\n");

        if (dnp && *dnp)
          free(*dnp);

        if (passwdp && *passwdp)
          free(*passwdp);
        break;

    case 0:
       /*
        * Return credentials for LDAP referal...
        */

        debug_printf("ldap_rebind_proc: Return necessary values...\n");

        *dnp         = strdup(BrowseLDAPBindDN);
        *passwdp     = strdup(BrowseLDAPPassword);
        *authmethodp = LDAP_AUTH_SIMPLE;
        break;

    default:
       /*
        * Should never happen...
        */

        debug_printf("LDAP rebind has been called with wrong freeit value!\n");
        break;
  }

  return (LDAP_SUCCESS);
}
#  endif /* defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000) */
#endif /* HAVE_LDAP_REBIND_PROC */


#ifdef HAVE_LDAP
/*
 * 'ldap_connect()' - Start new LDAP connection
 */

static LDAP *				/* O - LDAP handle */
ldap_connect(void)
{
  int		rc;			/* LDAP API status */
  int		version = 3;		/* LDAP version */
  struct berval	bv = {0, ""};		/* SASL bind value */
  LDAP		*TempBrowseLDAPHandle=NULL;
					/* Temporary LDAP Handle */
#  if defined(HAVE_LDAP_SSL) && defined (HAVE_MOZILLA_LDAP)
  int		ldap_ssl = 0;		/* LDAP SSL indicator */
  int		ssl_err = 0;		/* LDAP SSL error value */
#  endif /* defined(HAVE_LDAP_SSL) && defined (HAVE_MOZILLA_LDAP) */


#  ifdef HAVE_OPENLDAP
#    ifdef HAVE_LDAP_SSL
 /*
  * Set the certificate file to use for encrypted LDAP sessions...
  */

  if (BrowseLDAPCACertFile)
  {
    debug_printf("ldap_connect: Setting CA certificate file \"%s\"\n",
                    BrowseLDAPCACertFile);

    if ((rc = ldap_set_option(NULL, LDAP_OPT_X_TLS_CACERTFILE,
	                      (void *)BrowseLDAPCACertFile)) != LDAP_SUCCESS)
      debug_printf("Unable to set CA certificate file for LDAP "
		   "connections: %d - %s\n", rc, ldap_err2string(rc));
  }
#    endif /* HAVE_LDAP_SSL */

 /*
  * Initialize OPENLDAP connection...
  * LDAP stuff currently only supports ldapi EXTERNAL SASL binds...
  */

  if (!BrowseLDAPServer || !strcasecmp(BrowseLDAPServer, "localhost"))
    rc = ldap_initialize(&TempBrowseLDAPHandle, "ldapi:///");
  else
    rc = ldap_initialize(&TempBrowseLDAPHandle, BrowseLDAPServer);

#  else /* HAVE_OPENLDAP */

  int		ldap_port = 0;			/* LDAP port */
  char		ldap_protocol[11],		/* LDAP protocol */
		ldap_host[255];			/* LDAP host */

 /*
  * Split LDAP URI into its components...
  */

  if (!BrowseLDAPServer)
  {
    debug_printf("BrowseLDAPServer not configured!\n");
    debug_printf("Disabling LDAP browsing!\n");
    /*BrowseLocalProtocols  &= ~BROWSE_LDAP;*/
    BrowseRemoteProtocols &= ~BROWSE_LDAP;
    return (NULL);
  }

  sscanf(BrowseLDAPServer, "%10[^:]://%254[^:/]:%d", ldap_protocol, ldap_host,
         &ldap_port);

  if (!strcmp(ldap_protocol, "ldap"))
    ldap_ssl = 0;
  else if (!strcmp(ldap_protocol, "ldaps"))
    ldap_ssl = 1;
  else
  {
    debug_printf("Unrecognized LDAP protocol (%s)!\n",
		 ldap_protocol);
    debug_printf("Disabling LDAP browsing!\n");
    /*BrowseLocalProtocols &= ~BROWSE_LDAP;*/
    BrowseRemoteProtocols &= ~BROWSE_LDAP;
    return (NULL);
  }

  if (ldap_port == 0)
  {
    if (ldap_ssl)
      ldap_port = LDAPS_PORT;
    else
      ldap_port = LDAP_PORT;
  }

  debug_printf("ldap_connect: PROT:%s HOST:%s PORT:%d\n",
                  ldap_protocol, ldap_host, ldap_port);

 /*
  * Initialize LDAP connection...
  */

  if (!ldap_ssl)
  {
    if ((TempBrowseLDAPHandle = ldap_init(ldap_host, ldap_port)) == NULL)
      rc = LDAP_OPERATIONS_ERROR;
    else
      rc = LDAP_SUCCESS;

#    ifdef HAVE_LDAP_SSL
  }
  else
  {
   /*
    * Initialize SSL LDAP connection...
    */

    if (BrowseLDAPCACertFile)
    {
      rc = ldapssl_client_init(BrowseLDAPCACertFile, (void *)NULL);
      if (rc != LDAP_SUCCESS)
      {
        debug_printf("Failed to initialize LDAP SSL client!\n");
        rc = LDAP_OPERATIONS_ERROR;
      }
      else
      {
        if ((TempBrowseLDAPHandle = ldapssl_init(ldap_host, ldap_port,
                                                 1)) == NULL)
          rc = LDAP_OPERATIONS_ERROR;
        else
          rc = LDAP_SUCCESS;
      }
    }
    else
    {
      debug_printf("LDAP SSL certificate file/database not configured!\n");
      rc = LDAP_OPERATIONS_ERROR;
    }

#    else /* HAVE_LDAP_SSL */

   /*
    * Return error, because client libraries doesn't support SSL
    */

    debug_printf("LDAP client libraries do not support SSL\n");
    rc = LDAP_OPERATIONS_ERROR;

#    endif /* HAVE_LDAP_SSL */
  }
#  endif /* HAVE_OPENLDAP */

 /*
  * Check return code from LDAP initialize...
  */

  if (rc != LDAP_SUCCESS)
  {
    debug_printf("Unable to initialize LDAP!\n");

    if (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR)
      debug_printf("Temporarily disabling LDAP browsing...\n");
    else
    {
      debug_printf("Disabling LDAP browsing!\n");

      /*BrowseLocalProtocols  &= ~BROWSE_LDAP;*/
      BrowseRemoteProtocols &= ~BROWSE_LDAP;
    }

    ldap_disconnect(TempBrowseLDAPHandle);

    return (NULL);
  }

 /*
  * Upgrade LDAP version...
  */

  if (ldap_set_option(TempBrowseLDAPHandle, LDAP_OPT_PROTOCOL_VERSION,
                           (const void *)&version) != LDAP_SUCCESS)
  {
    debug_printf("Unable to set LDAP protocol version %d!\n",
		 version);
    debug_printf("Disabling LDAP browsing!\n");

    /*BrowseLocalProtocols  &= ~BROWSE_LDAP;*/
    BrowseRemoteProtocols &= ~BROWSE_LDAP;
    ldap_disconnect(TempBrowseLDAPHandle);

    return (NULL);
  }

 /*
  * Register LDAP rebind procedure...
  */

#  ifdef HAVE_LDAP_REBIND_PROC
#    if defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000)

  rc = ldap_set_rebind_proc(TempBrowseLDAPHandle, &ldap_rebind_proc,
                            (void *)NULL);
  if (rc != LDAP_SUCCESS)
    debug_printf("Setting LDAP rebind function failed with status %d: %s\n",
		 rc, ldap_err2string(rc));

#    else

  ldap_set_rebind_proc(TempBrowseLDAPHandle, &ldap_rebind_proc, (void *)NULL);

#    endif /* defined(LDAP_API_FEATURE_X_OPENLDAP) && (LDAP_API_VERSION > 2000) */
#  endif /* HAVE_LDAP_REBIND_PROC */

 /*
  * Start LDAP bind...
  */

#  if LDAP_API_VERSION > 3000
  struct berval bval;
  bval.bv_val = BrowseLDAPPassword;
  bval.bv_len = (BrowseLDAPPassword == NULL) ? 0 : strlen(BrowseLDAPPassword);

  if (!BrowseLDAPServer || !strcasecmp(BrowseLDAPServer, "localhost"))
    rc = ldap_sasl_bind_s(TempBrowseLDAPHandle, NULL, "EXTERNAL", &bv, NULL,
                          NULL, NULL);
  else
    rc = ldap_sasl_bind_s(TempBrowseLDAPHandle, BrowseLDAPBindDN, LDAP_SASL_SIMPLE, &bval, NULL, NULL, NULL);

#  else
    rc = ldap_bind_s(TempBrowseLDAPHandle, BrowseLDAPBindDN,
                     BrowseLDAPPassword, LDAP_AUTH_SIMPLE);
#  endif /* LDAP_API_VERSION > 3000 */

  if (rc != LDAP_SUCCESS)
  {
    debug_printf("LDAP bind failed with error %d: %s\n",
		 rc, ldap_err2string(rc));

#  if defined(HAVE_LDAP_SSL) && defined (HAVE_MOZILLA_LDAP)
    if (ldap_ssl && (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR))
    {
      ssl_err = PORT_GetError();
      if (ssl_err != 0)
        debug_printf("LDAP SSL error %d: %s\n", ssl_err,
		     ldapssl_err2string(ssl_err));
    }
#  endif /* defined(HAVE_LDAP_SSL) && defined (HAVE_MOZILLA_LDAP) */

    ldap_disconnect(TempBrowseLDAPHandle);

    return (NULL);
  }

  debug_printf("LDAP connection established\n");

  return (TempBrowseLDAPHandle);
}


/*
 * 'ldap_reconnect()' - Reconnect to LDAP Server
 */

static LDAP *				/* O - New LDAP handle */
ldap_reconnect(void)
{
  LDAP	*TempBrowseLDAPHandle = NULL;	/* Temp Handle to LDAP server */


 /*
  * Get a new LDAP Handle and replace the global Handle
  * if the new connection was successful.
  */

  debug_printf("Try LDAP reconnect...\n");

  TempBrowseLDAPHandle = ldap_connect();

  if (TempBrowseLDAPHandle != NULL)
  {
    if (BrowseLDAPHandle != NULL)
      ldap_disconnect(BrowseLDAPHandle);

    BrowseLDAPHandle = TempBrowseLDAPHandle;
  }

  return (BrowseLDAPHandle);
}


/*
 * 'ldap_disconnect()' - Disconnect from LDAP Server
 */

static void
ldap_disconnect(LDAP *ld)		/* I - LDAP handle */
{
  int	rc;				/* Return code */


 /*
  * Close LDAP handle...
  */

#  if defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000
  rc = ldap_unbind_ext_s(ld, NULL, NULL);
#  else
  rc = ldap_unbind_s(ld);
#  endif /* defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000 */

  if (rc != LDAP_SUCCESS)
    debug_printf("Unbind from LDAP server failed with status %d: %s\n",
		 rc, ldap_err2string(rc));
}

/*
 * 'cupsdUpdateLDAPBrowse()' - Scan for new printers via LDAP...
 */

void
cupsdUpdateLDAPBrowse(void)
{
  char		uri[HTTP_MAX_URI],	/* Printer URI */
		host[HTTP_MAX_URI],	/* Hostname */
		resource[HTTP_MAX_URI],	/* Resource path */
		local_resource[HTTP_MAX_URI],	/* Resource path */
		service_name[HTTP_MAX_URI],
		location[1024],		/* Printer location */
		info[1024],		/* Printer information */
		make_model[1024],	/* Printer make and model */
		type_num[30],		/* Printer type number */
		scheme[32],		/* URI's scheme */
		username[64];		/* URI's username */
  int port;				/* URI's port number */
  char *c;
  int		hl;
  int		rc;			/* LDAP status */
  int		limit;			/* Size limit */
  LDAPMessage	*res,			/* LDAP search results */
		*e;			/* Current entry from search */

  debug_printf("UpdateLDAPBrowse\n");

 /*
  * Reconnect if LDAP Handle is invalid...
  */

  if (! BrowseLDAPHandle)
  {
    ldap_reconnect();
    return;
  }

 /*
  * Search for cups printers in LDAP directory...
  */

  rc = ldap_search_rec(BrowseLDAPHandle, BrowseLDAPDN, LDAP_SCOPE_SUBTREE,
                       BrowseLDAPFilter, (char **)ldap_attrs, 0, &res);

 /*
  * If ldap search was successfull then exit function
  * and temporary disable LDAP updates...
  */

  if (rc != LDAP_SUCCESS)
  {
    if (BrowseLDAPUpdate && ((rc == LDAP_SERVER_DOWN) || (rc == LDAP_CONNECT_ERROR)))
    {
      BrowseLDAPUpdate = FALSE;
      debug_printf("LDAP update temporary disabled\n");
    }
    return;
  }

 /*
  * If LDAP updates were disabled, we will reenable them...
  */

  if (! BrowseLDAPUpdate)
  {
    BrowseLDAPUpdate = TRUE;
    debug_printf("LDAP update enabled\n");
  }

 /*
  * Count LDAP entries and return if no entry exist...
  */

  limit = ldap_count_entries(BrowseLDAPHandle, res);
  debug_printf("LDAP search returned %d entries\n", limit);
  if (limit < 1)
  {
    ldap_freeres(res);
    return;
  }

 /*
  * Loop through the available printers...
  */

  for (e = ldap_first_entry(BrowseLDAPHandle, res);
       e;
       e = ldap_next_entry(BrowseLDAPHandle, e))
  {
   /*
    * Get the required values from this entry...
    */

    if (ldap_getval_firststring(BrowseLDAPHandle, e,
                                "printerDescription", info, sizeof(info)) == -1)
      continue;

    if (ldap_getval_firststring(BrowseLDAPHandle, e,
                                "printerLocation", location, sizeof(location)) == -1)
      continue;

    if (ldap_getval_firststring(BrowseLDAPHandle, e,
                                "printerMakeAndModel", make_model, sizeof(make_model)) == -1)
      continue;

    if (ldap_getval_firststring(BrowseLDAPHandle, e,
                                "printerType", type_num, sizeof(type_num)) == -1)
      continue;

    if (ldap_getval_firststring(BrowseLDAPHandle, e,
                                "printerURI", uri, sizeof(uri)) == -1)
      continue;

   /*
    * Process the entry...
    */

    memset(scheme, 0, sizeof(scheme));
    memset(username, 0, sizeof(username));
    memset(host, 0, sizeof(host));
    memset(resource, 0, sizeof(resource));
    memset(local_resource, 0, sizeof(local_resource));

    httpSeparateURI (HTTP_URI_CODING_ALL, uri,
		     scheme, sizeof(scheme) - 1,
		     username, sizeof(username) - 1,
		     host, sizeof(host) - 1,
		     &port,
		     resource, sizeof(resource)- 1);

    if (strncasecmp (resource, "/printers/", 10) &&
	strncasecmp (resource, "/classes/", 9)) {
      debug_printf("don't understand URI: %s\n", uri);
      return;
    }

    strncpy (local_resource, resource + 1, sizeof (local_resource) - 1);
    local_resource[sizeof (local_resource) - 1] = '\0';
    c = strchr (local_resource, '?');
    if (c)
      *c = '\0';

    /* Build the DNS-SD service name which CUPS would give to this printer
       when DNS-SD-broadcasting it */
    snprintf(service_name, sizeof (service_name), "%s @ %s",
	     (strlen(info) > 0 ? info : strchr(local_resource, '/') + 1), host);
    /* Cut off trailing ".local" of host name */
    hl = strlen(service_name);
    if (hl > 6 && !strcasecmp(service_name + hl - 6, ".local"))
      service_name[hl - 6] = '\0';
    if (hl > 7 && !strcasecmp(service_name + hl - 7, ".local."))
      service_name[hl - 7] = '\0';
    /* DNS-SD service name has max. 63 characters */
    service_name[63] = '\0';

    debug_printf("LDAP: Remote host: %s; Port: %d; Remote queue name: %s; Service Name: %s\n",
		 host, port, strchr(local_resource, '/') + 1, service_name);

    examine_discovered_printer_record(host, NULL, port, local_resource,
				      service_name, location, info, "", "",
				      NULL);

  }

  ldap_freeres(res);
}

/*
 * 'ldap_search_rec()' - LDAP Search with reconnect
 */

static int				/* O - Return code */
ldap_search_rec(LDAP        *ld,	/* I - LDAP handler */
                char        *base,	/* I - Base dn */
                int         scope,	/* I - LDAP search scope */
                char        *filter,	/* I - Filter string */
                char        *attrs[],	/* I - Requested attributes */
                int         attrsonly,	/* I - Return only attributes? */
                LDAPMessage **res)	/* I - LDAP handler */
{
  int	rc;				/* Return code */
  LDAP  *ldr;				/* LDAP handler after reconnect */


#  if defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000
  rc = ldap_search_ext_s(ld, base, scope, filter, attrs, attrsonly, NULL, NULL,
                         NULL, LDAP_NO_LIMIT, res);
#  else
  rc = ldap_search_s(ld, base, scope, filter, attrs, attrsonly, res);
#  endif /* defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000 */

 /*
  * If we have a connection problem try again...
  */

  if (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR)
  {
    debug_printf("LDAP search failed with status %d: %s\n",
		 rc, ldap_err2string(rc));
    debug_printf("We try the LDAP search once again after reconnecting to "
		 "the server\n");
    ldap_freeres(*res);
    ldr = ldap_reconnect();

#  if defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000
    rc = ldap_search_ext_s(ldr, base, scope, filter, attrs, attrsonly, NULL,
                           NULL, NULL, LDAP_NO_LIMIT, res);
#  else
    rc = ldap_search_s(ldr, base, scope, filter, attrs, attrsonly, res);
#  endif /* defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000 */
  }

  if (rc == LDAP_NO_SUCH_OBJECT)
    debug_printf("ldap_search_rec: LDAP entry/object not found\n");
  else if (rc != LDAP_SUCCESS)
    debug_printf("ldap_search_rec: LDAP search failed with status %d: %s\n",
		 rc, ldap_err2string(rc));

  if (rc != LDAP_SUCCESS)
    ldap_freeres(*res);

  return (rc);
}


/*
 * 'ldap_freeres()' - Free LDAPMessage
 */

static void
ldap_freeres(LDAPMessage *entry)	/* I - LDAP handler */
{
  int	rc;				/* Return value */


  rc = ldap_msgfree(entry);
  if (rc == -1)
    debug_printf("Can't free LDAPMessage!\n");
  else if (rc == 0)
    debug_printf("Freeing LDAPMessage was unnecessary\n");
}


/*
 * 'ldap_getval_char()' - Get first LDAP value and convert to string
 */

static int				/* O - Return code */
ldap_getval_firststring(
    LDAP          *ld,			/* I - LDAP handler */
    LDAPMessage   *entry,		/* I - LDAP message or search result */
    char          *attr,		/* I - the wanted attribute  */
    char          *retval,		/* O - String to return */
    unsigned long maxsize)		/* I - Max string size */
{
  char			*dn;		/* LDAP DN */
  int			rc = 0;		/* Return code */
#  if defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000
  struct berval		**bval;		/* LDAP value array */
  unsigned long		size;		/* String size */


 /*
  * Get value from LDAPMessage...
  */

  if ((bval = ldap_get_values_len(ld, entry, attr)) == NULL)
  {
    rc = -1;
    dn = ldap_get_dn(ld, entry);
    debug_printf("Failed to get LDAP value %s for %s!\n",
		 attr, dn);
    ldap_memfree(dn);
  }
  else
  {
   /*
    * Check size and copy value into our string...
    */

    size = maxsize;
    if (size < (bval[0]->bv_len + 1))
    {
      rc = -1;
      dn = ldap_get_dn(ld, entry);
      debug_printf("Attribute %s is too big! (dn: %s)\n",
		   attr, dn);
      ldap_memfree(dn);
    }
    else
      size = bval[0]->bv_len + 1;

    strncpy(retval, bval[0]->bv_val, size);
    if (size > 0)
	retval[size - 1] = '\0';
    ldap_value_free_len(bval);
  }
#  else
  char	**value;			/* LDAP value */

 /*
  * Get value from LDAPMessage...
  */

  if ((value = (char **)ldap_get_values(ld, entry, attr)) == NULL)
  {
    rc = -1;
    dn = ldap_get_dn(ld, entry);
    debug_printf("Failed to get LDAP value %s for %s!\n",
		 attr, dn);
    ldap_memfree(dn);
  }
  else
  {
    strncpy(retval, *value, maxsize);
    if (maxsize > 0)
	retval[maxsize - 1] = '\0';
    ldap_value_free(value);
  }
#  endif /* defined(HAVE_OPENLDAP) && LDAP_API_VERSION > 3000 */

  return (rc);
}

#endif /* HAVE_LDAP */


static int
create_subscription ()
{
  ipp_t *req;
  ipp_t *resp;
  ipp_attribute_t *attr;
  int id = 0;
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to subscribe to notifications.\n");
    return 0;
  }

  req = ippNewRequest (IPP_CREATE_PRINTER_SUBSCRIPTION);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
		"notify-events", NULL, "all");
  ippAddString (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
		"notify-recipient-uri", NULL, "dbus://");
  ippAddInteger (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		 "notify-lease-duration", NOTIFY_LEASE_DURATION);

  resp = cupsDoRequest (conn, req, "/");
  if (!resp || cupsLastError() != IPP_STATUS_OK) {
    debug_printf ("Error subscribing to CUPS notifications: %s\n",
		  cupsLastErrorString ());
    return 0;
  }

  attr = ippFindAttribute (resp, "notify-subscription-id", IPP_TAG_INTEGER);
  if (attr)
    id = ippGetInteger (attr, 0);
  else
    debug_printf (""
		  "ipp-create-printer-subscription response doesn't contain "
		  "subscription id.\n");

  ippDelete (resp);
  return id;
}


static gboolean
renew_subscription (int id)
{
  ipp_t *req;
  ipp_t *resp;
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to renew subscriptions.\n");
    return FALSE;
  }

  req = ippNewRequest (IPP_RENEW_SUBSCRIPTION);
  ippAddInteger (req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-id", id);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
		"notify-recipient-uri", NULL, "dbus://");
  ippAddInteger (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		 "notify-lease-duration", NOTIFY_LEASE_DURATION);

  resp = cupsDoRequest (conn, req, "/");
  if (!resp || cupsLastError() != IPP_STATUS_OK) {
    debug_printf ("Error renewing CUPS subscription %d: %s\n",
		  id, cupsLastErrorString ());
    return FALSE;
  }

  ippDelete (resp);
  return TRUE;
}


static gboolean
renew_subscription_timeout (gpointer userdata)
{
  int *subscription_id = userdata;

  debug_printf("renew_subscription_timeout() in THREAD %ld\n", pthread_self());

  if (*subscription_id <= 0 || !renew_subscription (*subscription_id))
    *subscription_id = create_subscription ();

  return TRUE;
}


void
cancel_subscription (int id)
{
  ipp_t *req;
  ipp_t *resp;
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to cancel subscriptions.\n");
    return;
  }

  if (id <= 0)
    return;

  req = ippNewRequest (IPP_CANCEL_SUBSCRIPTION);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddInteger (req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-id", id);

  resp = cupsDoRequest (conn, req, "/");
  if (!resp || cupsLastError() != IPP_STATUS_OK) {
    debug_printf ("Error subscribing to CUPS notifications: %s\n",
		  cupsLastErrorString ());
    return;
  }

  ippDelete (resp);
}

int
is_created_by_cups_browsed (const char *printer) {
  remote_printer_t *p;

  if (printer == NULL)
    return 0;
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!p->slave_of && !strcasecmp(printer, p->queue_name))
      return 1;

  return 0;
}

remote_printer_t *
printer_record (const char *printer) {
  remote_printer_t *p;

  if (printer == NULL)
    return NULL;
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!p->slave_of && !strcasecmp(printer, p->queue_name))
      return p;

  return NULL;
}

void
log_cluster(remote_printer_t *p) {
  remote_printer_t *q, *r;
  int i;
  if (p == NULL || (!debug_stderr && !debug_logfile))
    return;
  if (p->netprinter) {
    debug_printf("Printer %s is not a remote CUPS printer, load-balanced clustering not supported.\n",
		 p->queue_name);
    return;
  }
  if (p->slave_of)
    q = p->slave_of;
  else
    q = p;
  debug_printf("Remote CUPS printers clustered as queue %s:\n", q->queue_name);
  for (r = (remote_printer_t *)cupsArrayFirst(remote_printers), i = 0;
       r; r = (remote_printer_t *)cupsArrayNext(remote_printers), i ++)
    if (r->status != STATUS_DISAPPEARED && r->status != STATUS_UNCONFIRMED &&
	(r == q || r->slave_of == q))
      debug_printf("  %s%s%s\n", r->uri,
		   (r == q ? "*" : ""),
		   (i == q->last_printer ? " (last job printed)" : ""));
}

void
log_all_printers() {
  remote_printer_t *p, *q;
  if (!debug_stderr && !debug_logfile)
    return;
  debug_printf("=== Remote printer overview ===\n");
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    debug_printf("Printer %s: Local queue %s, %s, Slave of %s%s\n", p->uri,
		 p->queue_name,
		 (p->netprinter ? "IPP Printer" : "Remote CUPS Printer"),
		 ((q = p->slave_of) != NULL ? q->uri : "None"),
		 (p->status == STATUS_UNCONFIRMED ? " (Unconfirmed)" :
		  (p->status == STATUS_DISAPPEARED ? " (Disappeared)" :
		   (p->status == STATUS_TO_BE_CREATED ?
		    " (To be created/updated)" : ""))));
  debug_printf("===============================\n");
}

char*
is_disabled(const char *printer, const char *reason) {
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *pname = NULL;
  ipp_pstate_t pstate = IPP_PRINTER_IDLE;
  const char *p;
  char *pstatemsg = NULL;
  static const char *pattrs[] =
                {
                  "printer-name",
                  "printer-state",
		  "printer-state-message"
                };
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to check whether the printer %s is disabled.\n",
		 printer);
    return NULL;
  }

  request = ippNewRequest(CUPS_GET_PRINTERS);
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		"requested-attributes",
		sizeof(pattrs) / sizeof(pattrs[0]),
		NULL, pattrs);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	       "requesting-user-name",
	       NULL, cupsUser());
  if ((response = cupsDoRequest(conn, request, "/")) != NULL) {
    for (attr = ippFirstAttribute(response); attr != NULL;
	 attr = ippNextAttribute(response)) {
      while (attr != NULL && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
	attr = ippNextAttribute(response);
      if (attr == NULL)
	break;
      pname = NULL;
      pstate = IPP_PRINTER_IDLE;
      if (pstatemsg) {
	free(pstatemsg);
	pstatemsg = NULL;
      }
      while (attr != NULL && ippGetGroupTag(attr) ==
	     IPP_TAG_PRINTER) {
	if (!strcmp(ippGetName(attr), "printer-name") &&
	    ippGetValueTag(attr) == IPP_TAG_NAME)
	  pname = ippGetString(attr, 0, NULL);
	else if (!strcmp(ippGetName(attr), "printer-state") &&
		 ippGetValueTag(attr) == IPP_TAG_ENUM)
	  pstate = (ipp_pstate_t)ippGetInteger(attr, 0);
	else if (!strcmp(ippGetName(attr), "printer-state-message") &&
		 ippGetValueTag(attr) == IPP_TAG_TEXT) {
	  free(pstatemsg);
	  p = ippGetString(attr, 0, NULL);
	  if (p != NULL) pstatemsg = strdup(p);
	}
	attr = ippNextAttribute(response);
      }
      if (pname == NULL) {
	if (attr == NULL)
	  break;
	else
	  continue;
      }
      if (!strcasecmp(pname, printer)) {
	switch (pstate) {
	case IPP_PRINTER_IDLE:
	case IPP_PRINTER_PROCESSING:
	  ippDelete(response);
	  free(pstatemsg);
	  return NULL;
	case IPP_PRINTER_STOPPED:
	  ippDelete(response);
	  if (reason == NULL)
	    return pstatemsg;
	  else if (strcasestr(pstatemsg, reason) != NULL)
	    return pstatemsg;
	  else {
	    free(pstatemsg);
	    return NULL;
	  }
	}
      }
    }
    debug_printf("No information regarding enabled/disabled found about the requested printer '%s'\n",
		 printer);
    ippDelete(response);
    free(pstatemsg);
    return NULL;
  }
  debug_printf("ERROR: Request for printer info failed: %s\n",
	       cupsLastErrorString());
  free(pstatemsg);
  return NULL;
}

int
enable_printer (const char *printer) {
  ipp_t *request;
  char uri[HTTP_MAX_URI];
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to enable printer %s.\n",
		 printer);
    return -1;
  }

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", ippPort(), "/printers/%s", printer);
  request = ippNewRequest (IPP_RESUME_PRINTER);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  ippDelete(cupsDoRequest (conn, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("ERROR: Failed enabling printer '%s': %s\n",
		 printer, cupsLastErrorString());
    return -1;
  }
  debug_printf("Enabled printer '%s'\n", printer);
  return 0;
}

int
disable_printer (const char *printer, const char *reason) {
  ipp_t *request;
  char uri[HTTP_MAX_URI];
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to disable printer %s.\n",
		 printer);
    return -1;
  }

  if (reason == NULL)
    reason = "Disabled by cups-browsed";
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", ippPort(), "/printers/%s", printer);
  request = ippNewRequest (IPP_PAUSE_PRINTER);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
		"printer-state-message", NULL, reason);
  ippDelete(cupsDoRequest (conn, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("ERROR: Failed disabling printer '%s': %s\n",
		 printer, cupsLastErrorString());
    return -1;
  }
  debug_printf("Disabled printer '%s'\n", printer);
  return 0;
}

int
set_cups_default_printer(const char *printer) {
  ipp_t	*request;
  char uri[HTTP_MAX_URI];
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to subscribe to set printer %s as default printer.\n",
		 printer);
    return -1;
  }

  if (printer == NULL)
    return 0;
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", ippPort(), "/printers/%s", printer);
  request = ippNewRequest(IPP_OP_CUPS_SET_DEFAULT);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());
  ippDelete(cupsDoRequest(conn, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("ERROR: Failed setting CUPS default printer to '%s': %s\n",
		 printer, cupsLastErrorString());
    return -1;
  }
  debug_printf("Successfully set CUPS default printer to '%s'\n",
	       printer);
  return 0;
}

char*
get_cups_default_printer() {
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *default_printer_name = NULL;
  char *name_string;
  http_t *conn = NULL;

  conn = http_connect_local ();
  if (conn == NULL) {
    debug_printf("Cannot connect to local CUPS to find out which is the default printer.\n");
    return NULL;
  }

  request = ippNewRequest(CUPS_GET_DEFAULT);
  /* Default user */
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	       "requesting-user-name", NULL, cupsUser());
  /* Do it */
  response = cupsDoRequest(conn, request, "/");
  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE || !response) {
    debug_printf("Could not determine system default printer!\n");
  } else {
    for (attr = ippFirstAttribute(response); attr != NULL;
	 attr = ippNextAttribute(response)) {
      while (attr != NULL && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
	attr = ippNextAttribute(response);
      if (attr) {
	for (; attr && ippGetGroupTag(attr) == IPP_TAG_PRINTER;
	     attr = ippNextAttribute(response)) {
	  if (!strcasecmp(ippGetName(attr), "printer-name") &&
	      ippGetValueTag(attr) == IPP_TAG_NAME) {
	    default_printer_name = ippGetString(attr, 0, NULL);
	    break;
	  }
	}
      }
      if (default_printer_name)
	break;
    }
  }
  
  if (default_printer_name != NULL) {  
    name_string = strdup(default_printer_name);
  } else {
    name_string = NULL;
  }
  
  ippDelete(response);
  
  return name_string;
}

int
is_cups_default_printer(const char *printer) {
  if (printer == NULL)
    return 0;
  char *cups_default = get_cups_default_printer();
  if (cups_default == NULL)
    return 0;
  if (!strcasecmp(printer, cups_default)) {
    free(cups_default);
    return 1;
  }
  free(cups_default);
  return 0;
}

int
invalidate_default_printer(int local) {
  const char *filename = local ? local_default_printer_file :
    remote_default_printer_file;
  unlink(filename);
  return 0;
}

int
record_default_printer(const char *printer, int local) {
  FILE *fp = NULL;
  const char *filename = local ? local_default_printer_file :
    remote_default_printer_file;

  if (printer == NULL || strlen(printer) == 0)
    return invalidate_default_printer(local);

  fp = fopen(filename, "w+");
  if (fp == NULL) {
    debug_printf("ERROR: Failed creating file %s\n",
		 filename);
    invalidate_default_printer(local);
    return -1;
  }
  fprintf(fp, "%s", printer);
  fclose(fp);
  
  return 0;
}

const char*
retrieve_default_printer(int local) {
  FILE *fp = NULL;
  const char *filename = local ? local_default_printer_file :
    remote_default_printer_file;
  const char *printer = NULL;
  char *p, buf[1024];
  int n;

  fp = fopen(filename, "r");
  if (fp == NULL) {
    debug_printf("Failed reading file %s\n",
		 filename);
    return NULL;
  }
  p = buf;
  n = fscanf(fp, "%s", p);
  if (n == 1) {
    if (strlen(p) > 0)
      printer = p;
  }
  fclose(fp);
  
  return printer;
}

int
invalidate_printer_options(const char *printer) {
  char filename[1024];

  snprintf(filename, sizeof(filename), save_options_file,
	   printer);
  unlink(filename);
  return 0;
}

int
record_printer_options(const char *printer) {
  remote_printer_t *p;
  char filename[1024];
  FILE *fp = NULL;
  char uri[HTTP_MAX_URI], *resource;
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *key;
  char buf[65536], *c;
  const char *ppdname = NULL;
  ppd_file_t *ppd;
  ppd_option_t *ppd_opt;
  cups_option_t *option;
  int i;
  /* List of IPP attributes to get recorded */
  static const char *attrs_to_record[] =
                {
                  "*-default",
		  "auth-info-required",
                  /*"device-uri",*/
                  "job-quota-period",
		  "job-k-limit",
		  "job-page-limit",
		  /*"port-monitor",*/
		  "printer-error-policy",
		  "printer-info",
		  "printer-is-accepting-jobs",
		  "printer-is-shared",
		  "printer-geo-location",
		  "printer-location",
		  "printer-op-policy",
		  "printer-organization",
		  "printer-organizational-unit",
		  /*"printer-state",
		  "printer-state-message",
		  "printer-state-reasons",*/
		  "requesting-user-name-allowed",
		  "requesting-user-name-denied",
		  NULL
                };
  const char **ptr;
  http_t *conn = NULL;

  if (printer == NULL || strlen(printer) == 0)
    return 0;

  /* Get our data about this printer */
  p = printer_record(printer);

  if (p == NULL) {
    debug_printf("Not recording printer options for %s: Unkown printer!\n",
		 printer);
    return 0;
  }
  
  snprintf(filename, sizeof(filename), save_options_file,
	   printer);

  debug_printf("Recording printer options for %s to %s\n",
	       printer, filename);

  /* If there is a PPD file for this printer, we save the local
     settings for the PPD options. */
  if (cups_notifier != NULL || (p && p->netprinter)) {
    if ((ppdname = cupsGetPPD(printer)) == NULL) {
      debug_printf("Unable to get PPD file for %s: %s\n",
		   printer, cupsLastErrorString());
    } else if ((ppd = ppdOpenFile(ppdname)) == NULL) {
      unlink(ppdname);
      debug_printf("Unable to open PPD file for %s.\n",
		   printer);
    } else {
      ppdMarkDefaults(ppd);
      for (ppd_opt = ppdFirstOption(ppd); ppd_opt; ppd_opt = ppdNextOption(ppd))
	if (strcasecmp(ppd_opt->keyword, "PageRegion") != 0) {
	  strncpy(buf, ppd_opt->keyword, sizeof(buf));
	  p->num_options = cupsAddOption(buf, ppd_opt->defchoice,
					 p->num_options, &(p->options));
	}
      ppdClose(ppd);
      unlink(ppdname);
    }
  }

  conn = http_connect_local ();
  if (conn) {
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		     "localhost", ippPort(), "/printers/%s", printer);
    resource = uri + (strlen(uri) - strlen(printer) - 10);
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
		 uri);
    response = cupsDoRequest(conn, request, resource);

    /* Write all supported printer attributes */
    if (response) {
      attr = ippFirstAttribute(response);
      while (attr) {
	key = ippGetName(attr);
	for (ptr = attrs_to_record; *ptr; ptr++)
	  if (strcasecmp(key, *ptr) == 0 ||
	      (*ptr[0] == '*' &&
	       strcasecmp(key + strlen(key) - strlen(*ptr) + 1, *ptr + 1) == 0))
	    break;
	if (*ptr != NULL) {
	  if (strcasecmp(key, CUPS_BROWSED_DEST_PRINTER "-default") != 0 &&
	      (ppdname == NULL ||
	       strncasecmp(key + strlen(key) - 8, "-default", 8))) {
	    ippAttributeString(attr, buf, sizeof(buf));
	    buf[sizeof(buf) - 1] = '\0';
	    c = buf;
	    while (*c) {
	      if (*c == '\\')
		memmove(c, c + 1, strlen(c));
	      if (*c) c ++;
	    }
	    p->num_options = cupsAddOption(key, buf, p->num_options,
					   &(p->options));
	  }
	}
	attr = ippNextAttribute(response);
      }
      ippDelete(response);
    }
  } else {
    debug_printf("Cannot connect to local CUPS to read out the IPP attributes for printer %s.\n",
		 printer);
  }

  if (p->num_options > 0) {
    fp = fopen(filename, "w+");
    if (fp == NULL) {
      debug_printf("ERROR: Failed creating file %s: %s\n",
		   filename, strerror(errno));
      return -1;
    }

    for (i = p->num_options, option = p->options; i > 0; i --, option ++)
      if (fprintf (fp, "%s=%s\n", option->name, option->value) < 0) {
	debug_printf("ERROR: Failed to write into file %s: %s\n",
		     filename, strerror(errno));
	fclose(fp);
	return -1;
      }

    fclose(fp);

    return 0;
  } else
    return -1;
}

int
load_printer_options(const char *printer, int num_options,
		     cups_option_t **options) {
  char filename[1024];
  FILE *fp = NULL;
  char *opt = NULL, *val;
  size_t optlen = 0;

  if (printer == NULL || strlen(printer) == 0 || options == NULL)
    return 0;

  /* Prepare reading file with saved option settings */
  snprintf(filename, sizeof(filename), save_options_file,
	   printer);

  debug_printf("Loading saved printer options for %s from %s\n",
	       printer, filename);

  /* Open the file with the saved option settings for this print queue */
  fp = fopen(filename, "r");
  if (fp == NULL) {
    debug_printf("Failed reading file %s, probably no options recorded yet\n",
		 filename);
  } else {
    /* Now read the lines of the file and add each setting to our request */
    errno = 0;
    debug_printf("Loading following option settings for printer %s:\n",
		 printer);
    while (getline(&opt, &optlen, fp) != -1) {
      if (strlen(opt) > 1 && (val = strchr(opt, '=')) != NULL) {
	*val = '\0';
	val ++;
	val[strlen(val)-1] = '\0';
	debug_printf("   %s=%s\n", opt, val);
	num_options = cupsAddOption(opt, val, num_options, options);
      }
    }
    debug_printf("\n");
    if (errno != 0)
      debug_printf("Failed reading saved options file %s: %s\n",
		   filename, strerror(errno));
    free(opt);
    fclose(fp);
  }
  return (num_options);
}

int
queue_creation_handle_default(const char *printer) {
  /* No default printer management if we cannot get D-Bus notifications
     from CUPS */
  if (cups_notifier == NULL)
    return 0;
  /* If this queue is recorded as the former default queue (and the current
     default is local), set it as default (the CUPS notification handler
     will record the local default printer then) */
  const char *recorded_default = retrieve_default_printer(0);
  if (recorded_default == NULL || strcasecmp(recorded_default, printer))
    return 0;
  char *current_default = get_cups_default_printer();
  if (current_default == NULL || !is_created_by_cups_browsed(current_default)) {
    if (set_cups_default_printer(printer) < 0) {
      debug_printf("ERROR: Could not set former default printer %s as default again.\n",
		   printer);
      free(current_default);
      return -1;
    } else {
      debug_printf("Former default printer %s re-appeared, set as default again.\n",
		   printer);
      invalidate_default_printer(0);
    }
  }
  free(current_default);
  return 0;
}

int
queue_removal_handle_default(const char *printer) {
  /* No default printer management if we cannot get D-Bus notifications
     from CUPS */
  if (cups_notifier == NULL)
    return 0;
  /* If the queue is the default printer, get back
     to the recorded local default printer, record this queue for getting the
     default set to this queue again if it re-appears. */
  /* We call this also if a queue is only conserved because on cups-browsed
     shutdown it still has jobs */
  if (!is_cups_default_printer(printer))
    return 0;
  /* Record the fact that this printer was default */
  if (record_default_printer(default_printer, 0) < 0) {
      /* Delete record file if recording failed */
    debug_printf("ERROR: Failed recording remote default printer (%s). Removing the file with possible old recording.\n",
		 printer);
    invalidate_default_printer(0);
  } else
    debug_printf("Recorded the fact that the current printer (%s) is the default printer before deleting the queue and returning to the local default printer.\n",
		 printer);
  /* Switch back to a recorded local printer, if available */
  const char *local_default = retrieve_default_printer(1);
  if (local_default != NULL) {
    if (set_cups_default_printer(local_default) >= 0)
      debug_printf("Switching back to %s as default printer.\n",
		   local_default);
    else {
      debug_printf("ERROR: Unable to switch back to %s as default printer.\n",
		   local_default);
      return -1;
    }
  }
  invalidate_default_printer(1);
  return 0;
}

static void
on_printer_state_changed (CupsNotifier *object,
                          const gchar *text,
                          const gchar *printer_uri,
                          const gchar *printer,
                          guint printer_state,
                          const gchar *printer_state_reasons,
                          gboolean printer_is_accepting_jobs,
                          gpointer user_data)
{
  int i;
  char *ptr, buf[1024];
  remote_printer_t *p, *q;
  http_t *http = NULL;
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *pname = NULL;
  char *remote_cups_queue;
  ipp_pstate_t pstate = IPP_PRINTER_IDLE;
  int paccept = 0;
  int num_jobs, min_jobs = 99999999;
  cups_job_t *jobs = NULL;
  const char *dest_host = NULL;
  int dest_port = 0;
  char dest_name[1024];
  int dest_index = 0;
  int valid_dest_found = 0;
  char uri[HTTP_MAX_URI];
  int job_id = 0;
  int num_options;
  cups_option_t *options;
  static const char *pattrs[] =
                {
                  "printer-name",
                  "printer-state",
                  "printer-is-accepting-jobs"
                };
  static const char *jattrs[] =
		{
		  "job-id",
		  "job-state"
		};
  http_t *conn = NULL;

  debug_printf("on_printer_state_changed() in THREAD %ld\n", pthread_self());

  debug_printf("[CUPS Notification] Printer state change on printer %s: %s\n",
	       printer, text);
  debug_printf("[CUPS Notification] Printer state reasons: %s\n",
	       printer_state_reasons);

  if (terminating) {
    debug_printf("[CUPS Notification]: Ignoring because cups-browsed is terminating.\n");
    return;
  }

  if (autoshutdown && autoshutdown_on == NO_JOBS) {
    if (check_jobs() == 0) {
      /* If auto shutdown is active for triggering on no jobs being left, we
	 schedule the shutdown in autoshutdown_timeout seconds */
      if (!autoshutdown_exec_id) {
	debug_printf ("No jobs there any more on printers made available by us, shutting down in %d sec...\n", autoshutdown_timeout);
	autoshutdown_exec_id =
	  g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
				 NULL);
      }
    } else {
      /* If auto shutdown is active for triggering on no jobs being left, we
	 cancel a shutdown in autoshutdown_timeout seconds as there are jobs
         again. */
      if (autoshutdown_exec_id) {
	debug_printf ("New jobs there on the printers made available by us, killing auto shutdown timer.\n");
	g_source_remove(autoshutdown_exec_id);
	autoshutdown_exec_id = 0;
      }
    }
  }
  
  if ((ptr = strstr(text, " is now the default printer")) != NULL) {
    /* Default printer has changed, we are triggered by the new default
       printer */
    strncpy(buf, text, ptr - text);
    buf[ptr - text] = '\0';
    debug_printf("[CUPS Notification] Default printer changed from %s to %s.\n",
		 default_printer, buf);
    if (is_created_by_cups_browsed(default_printer)) {
      /* Previous default printer created by cups-browsed */
      if (!is_created_by_cups_browsed(buf)) {
	/* New default printer local */
	/* Removed backed-up local default printer as we do not have a
	   remote printer as default any more */
	invalidate_default_printer(1);
	debug_printf("Manually switched default printer from a cups-browsed-generated one to a local printer.\n");
      }
    } else {
      /* Previous default printer local */
      if (is_created_by_cups_browsed(buf)) {
	/* New default printer created by cups-browsed */
	/* Back up the local default printer to be able to return to it
	   if the remote printer disappears */
	if (record_default_printer(default_printer, 1) < 0) {
	  /* Delete record file if recording failed */
	  debug_printf("ERROR: Failed recording local default printer. Removing the file with possible old recording.\n");
	  invalidate_default_printer(1);
	} else
	  debug_printf("Recorded previous default printer so that if the currently selected cups-browsed-generated one disappears, we can return to the old local one.\n");
	/* Remove a recorded remote printer as after manually selecting
	   another one as default this one is not relevant any more */
	invalidate_default_printer(0);
      }
    }
    if (default_printer != NULL)
      free((void *)default_printer);
    default_printer = strdup(buf);
  } else if ((ptr = strstr(text, " is no longer the default printer"))
	     != NULL) {
    /* Default printer has changed, we are triggered by the former default
       printer */
    strncpy(buf, text, ptr - text);
    buf[ptr - text] = '\0';
    debug_printf("[CUPS Notification] %s not default printer any more.\n", buf);
  } else if ((ptr = strstr(text, " state changed to processing")) != NULL) {
    /* Printer started processing a job, check if it uses the implicitclass
       backend and if so, we select the remote queue to which to send the job
       in a way so that we get load balancing between all remote queues
       associated with this queue.

       There are two methods to do that (configurable in cups-browsed.conf):

       Queuing of jobs on the client (LoadBalancingType = QUEUE_ON_CLIENT):

       Here we check all remote printers assigned to this printer and to its
       slaves which is currently accepting jobs and idle. If all are busy,
       we send a failure message and the backend will close with an error code
       after some seconds of delay, to make the job getting retried making us
       checking again here. If we find a destination, we tell the backend
       which remote queue this destination is, making the backend printing the
       job there immediately.

       With this all waiting jobs get queued up on the client, on the servers
       there will only be the jobs which are actually printing, as we do not
       send jobs to a server which is already printing. This is also the
       method which CUPS uses for classes. Advantage is a more even
       distribution of the job workload on the servers, and if a server fails,
       there are not several jobs stuck or lost. Disadvantage is that if one
       takes the client (laptop, mobile phone, ...) out of the local network,
       printing stops with the jobs waiting in the local queue.

       Queuing of jobs on the servers (LoadBalancingType = QUEUE_ON_SERVERS):

       Here we check all remote printers assigned to this printer and to its
       slaves which is currently accepting jobs and find the one with the
       lowest amount of jobs waiting and send the job to there. So on the
       local queue we have never jobs waiting if at least one remote printer
       accepts jobs.

       Not having jobs waiting locally has the advantage that we can take the
       local machine from the network and all jobs get printed. Disadvantage
       is that if a server with a full queue of jobs goes away, the jobs go
       away, too.

       Default is queuing the jobs on the client as this is what CUPS does
       with classes. */

    debug_printf("[CUPS Notification] %s starts processing a job.\n", printer);
    conn = http_connect_local ();
    if (conn == NULL) {
      debug_printf("Cannot connect to local CUPS to set destination for job in the load-balanced cluster %s.\n",
		   printer);
      return;
    }
    q = printer_record(printer);
    /* If we hit a slave and not the master, switch to the master */
    if (q && q->slave_of)
      q = q->slave_of;
    if (q && q->netprinter == 0) {
      /* We have remote CUPS queue(s) and so are using the implicitclass 
	 backend */
      debug_printf("[CUPS Notification] %s is using the \"implicitclass\" CUPS backend, so let us search for a destination for this job.\n", printer);
      /* We keep track of the printer which we used last time and start
	 checking with the next printer this time, to get a "round robin"
	 type of printer usage instead of having most jobs going to the first
	 printer in the list. Method taken from the cupsdFindAvailablePrinter()
	 function of the scheduler/classes.c file of CUPS. */
      if (q->last_printer < 0 ||
	  q->last_printer >= cupsArrayCount(remote_printers))
	q->last_printer = 0;
      log_cluster(q);
      for (i = q->last_printer + 1; ; i++) {
	if (i >= cupsArrayCount(remote_printers))
	  i = 0;
	p = (remote_printer_t *)cupsArrayIndex(remote_printers, i);
	if (!strcasecmp(p->queue_name, printer) &&
	    p->status == STATUS_CONFIRMED) {
	  remote_cups_queue = strrchr(p->uri, '/') + 1;
	  debug_printf("Checking state of remote printer %s on host %s, IP %s, port %d.\n", remote_cups_queue, p->host, p->ip, p->port);
	  http = httpConnectEncryptShortTimeout (p->ip ? p->ip : p->host,
						 p->port,
						 HTTP_ENCRYPT_IF_REQUESTED);
	  debug_printf("HTTP connection to %s:%d established.\n", p->host,
		       p->port);
	  if (http) {
	    /* Check whether the printer is idle, processing, or disabled */
	    httpSetTimeout(http, HttpRemoteTimeout, http_timeout_cb, NULL);
	    request = ippNewRequest(CUPS_GET_PRINTERS);
	    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
			  "requested-attributes",
			  sizeof(pattrs) / sizeof(pattrs[0]),
			  NULL, pattrs);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
			 "requesting-user-name",
			 NULL, cupsUser());
	    if ((response = cupsDoRequest(http, request, "/")) !=
		NULL) {
	      debug_printf("IPP request to %s:%d successful.\n", p->host,
			   p->port);
	      pname = NULL;
	      pstate = IPP_PRINTER_IDLE;
	      paccept = 0;
	      for (attr = ippFirstAttribute(response); attr != NULL;
		   attr = ippNextAttribute(response)) {
		while (attr != NULL && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
		  attr = ippNextAttribute(response);
		if (attr == NULL)
		  break;
		pname = NULL;
		pstate = IPP_PRINTER_IDLE;
		paccept = 0;
		while (attr != NULL && ippGetGroupTag(attr) ==
		       IPP_TAG_PRINTER) {
		  if (!strcmp(ippGetName(attr), "printer-name") &&
		      ippGetValueTag(attr) == IPP_TAG_NAME)
		    pname = ippGetString(attr, 0, NULL);
		  else if (!strcmp(ippGetName(attr), "printer-state") &&
			   ippGetValueTag(attr) == IPP_TAG_ENUM)
		    pstate = (ipp_pstate_t)ippGetInteger(attr, 0);
		  else if (!strcmp(ippGetName(attr),
				   "printer-is-accepting-jobs") &&
			   ippGetValueTag(attr) == IPP_TAG_BOOLEAN)
		    paccept = ippGetBoolean(attr, 0);
		  attr = ippNextAttribute(response);
		}
		if (pname == NULL) {
		  if (attr == NULL)
		    break;
		  else
		    continue;
		}
		if (!strcasecmp(pname, remote_cups_queue)) {
		  if (paccept) {
		    debug_printf("Printer %s on host %s, port %d is accepting jobs.\n", remote_cups_queue, p->host, p->port);
		    switch (pstate) {
		    case IPP_PRINTER_IDLE:
		      valid_dest_found = 1;
		      dest_host = p->ip ? p->ip : p->host;
		      dest_port = p->port;
		      strncpy(dest_name, remote_cups_queue, sizeof(dest_name));
		      dest_index = i;
		      debug_printf("Printer %s on host %s, port %d is idle, take this as destination and stop searching.\n",
				   remote_cups_queue, p->host, p->port);
		      break;
		    case IPP_PRINTER_PROCESSING:
		      valid_dest_found = 1;
		      if (LoadBalancingType == QUEUE_ON_SERVERS) {
			num_jobs = 0;
			jobs = NULL;
			num_jobs =
			  cupsGetJobs2(http, &jobs, remote_cups_queue, 0,
				       CUPS_WHICHJOBS_ACTIVE);
			if (num_jobs >= 0 && num_jobs < min_jobs) {
			  min_jobs = num_jobs;
			  dest_host = p->ip ? p->ip : p->host;
			  dest_port = p->port;
			  strncpy(dest_name, remote_cups_queue,
				  sizeof(dest_name));
			  dest_index = i;
			}
			debug_printf("Printer %s on host %s, port %d is printing and it has %d jobs.\n",
				     remote_cups_queue, p->host, p->port,
				     num_jobs);
		      } else
			debug_printf("Printer %s on host %s, port %d is printing.\n", remote_cups_queue, p->host, p->port);
		      break;
		    case IPP_PRINTER_STOPPED:
		      debug_printf("Printer %s on host %s, port %d is disabled, skip it.\n", remote_cups_queue, p->host, p->port);
		      break;
		    }
		  } else {
		    debug_printf("Printer %s on host %s, port %d is not accepting jobs, skip it.\n", remote_cups_queue, p->host, p->port);
		  }
		  break;
		}
	      }
	      if (pstate == IPP_PRINTER_IDLE && paccept) {
		q->last_printer = i;
		break;
	      }
	    } else
	      debug_printf("IPP request to %s:%d failed.\n", p->host,
			   p->port);
	    httpClose(http);
	    http = NULL;
	  }
	}
	if (i == q->last_printer)
	  break;
      }
      /* Find the ID of the current job */
      request = ippNewRequest(IPP_GET_JOBS);
      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		       "localhost", ippPort(), "/printers/%s", printer);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		   "printer-uri", NULL, uri);
      ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		    "requested-attributes",
		    sizeof(jattrs) / sizeof(jattrs[0]), NULL, jattrs);
      job_id = 0;
      if ((response = cupsDoRequest(conn, request, "/")) != NULL) {
	/* Get the current active job on this queue... */
	ipp_jstate_t jobstate = IPP_JOB_PENDING;
	for (attr = ippFirstAttribute(response); attr != NULL;
	     attr = ippNextAttribute(response)) {
	  if (!ippGetName(attr)) {
	    if (jobstate == IPP_JOB_PROCESSING)
	      break;
	    else
	      continue;
	  }
	  if (!strcmp(ippGetName(attr), "job-id") &&
	      ippGetValueTag(attr) == IPP_TAG_INTEGER)
	    job_id = ippGetInteger(attr, 0);
	  else if (!strcmp(ippGetName(attr), "job-state") &&
		   ippGetValueTag(attr) == IPP_TAG_ENUM)
	    jobstate = (ipp_jstate_t)ippGetInteger(attr, 0);
	}
	if (jobstate != IPP_JOB_PROCESSING)
	  job_id = 0;
	ippDelete(response);
      }
      if (job_id == 0)
	debug_printf("ERROR: could not determine ID of curremt job on %s\n",
		     printer);

      /* Write the selected destination host into an option of our implicit
	 class queue (cups-browsed-dest-printer="<dest>") so that the
	 implicitclass backend will pick it up */
      request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		   "printer-uri", NULL, uri);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		   "requesting-user-name", NULL, cupsUser());
      if (dest_host) {
	q->last_printer = dest_index;
	snprintf(buf, sizeof(buf), "\"%d %s:%d/%s\"", job_id, dest_host,
		 dest_port, dest_name);
	debug_printf("Destination for job %d to %s: %s:%d, queue %s\n",
		     job_id, printer, dest_host, dest_port, dest_name);
      } else if (valid_dest_found == 1) {
	snprintf(buf, sizeof(buf), "\"%d ALL_DESTS_BUSY\"", job_id);
	debug_printf("All destinations busy for job %d to %s\n",
		     job_id, printer);
      } else {
	snprintf(buf, sizeof(buf), "\"%d NO_DEST_FOUND\"", job_id);
	debug_printf("No destination found for job %d to %s\n",
		     job_id, printer);
      }
      num_options = 0;
      options = NULL;
      num_options = cupsAddOption(CUPS_BROWSED_DEST_PRINTER "-default", buf,
				  num_options, &options);
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
      ippDelete(cupsDoRequest(conn, request, "/admin/"));
      cupsFreeOptions(num_options, options);
      if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
	debug_printf("ERROR: Unable to set \"" CUPS_BROWSED_DEST_PRINTER
		     "-default\" option to communicate the destination server for this job (%s)!\n",
		     cupsLastErrorString());
	return;
      }
    }
  }
}

static void
on_printer_deleted (CupsNotifier *object,
		    const gchar *text,
		    const gchar *printer_uri,
		    const gchar *printer,
		    guint printer_state,
		    const gchar *printer_state_reasons,
		    gboolean printer_is_accepting_jobs,
		    gpointer user_data)
{
  remote_printer_t *p;
  const char* r;

  debug_printf("on_printer_deleted() in THREAD %ld\n", pthread_self());

  debug_printf("[CUPS Notification] Printer deleted: %s\n",
	       text);

  if (terminating) {
    debug_printf("[CUPS Notification]: Ignoring because cups-browsed is terminating.\n");
    return;
  }

  if (is_created_by_cups_browsed(printer)) {
    /* a cups-browsed-generated printer got deleted, re-create it */
    debug_printf("Printer %s got deleted, re-creating it.\n",
		 printer);
    /* If the deleted printer was the default printer, make sure it gets the
       default printer again */
    if (default_printer && !strcasecmp(printer, default_printer)) {
      if (record_default_printer(printer, 0) < 0) {
	/* Delete record file if recording failed */
	debug_printf("ERROR: Failed recording remote default printer. Removing the file with possible old recording.\n");
	invalidate_default_printer(0);
      } else
	debug_printf("Recorded %s as remote default printer so that it gets set as default after re-creating.\n");
      /* Make sure that a recorded local default printer does not get lost
	 during the recovery operation */
      if ((r = retrieve_default_printer(1)) != NULL) {
	if (default_printer != NULL)
	  free((void *)default_printer);
	default_printer = strdup(r);
      }
    }
    /* Schedule for immediate creation of the CUPS queue */
    p = printer_record(printer);
    if (p && p->status != STATUS_DISAPPEARED &&
	p->status != STATUS_UNCONFIRMED) {
      p->status = STATUS_TO_BE_CREATED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
      if (in_shutdown == 0)
	recheck_timer();
    }
  }
}

static void
on_printer_modified (CupsNotifier *object,
		     const gchar *text,
		     const gchar *printer_uri,
		     const gchar *printer,
		     guint printer_state,
		     const gchar *printer_state_reasons,
		     gboolean printer_is_accepting_jobs,
		     gpointer user_data)
{
  remote_printer_t *p;

  debug_printf("on_printer_modified() in THREAD %ld\n", pthread_self());

  debug_printf("[CUPS Notification] Printer modified: %s\n",
	       text);

  if (terminating) {
    debug_printf("[CUPS Notification]: Ignoring because cups-browsed is terminating.\n");
    return;
  }

  if (is_created_by_cups_browsed(printer)) {
    /* The user has changed settings of a printer which we have generated,
       backup the changes for the case of a crash or unclean shutdown of
       cups-browsed. */
    p = printer_record(printer);
    if (!p->no_autosave) {
      debug_printf("Settings of printer %s got modified, doing backup.\n",
		   printer);
      p->no_autosave = 1; /* Avoid infinite recursion */
      record_printer_options(printer);
      p->no_autosave = 0;
    }
  }
}


static remote_printer_t *
create_remote_printer_entry (const char *queue_name,
			     const char *location,
			     const char *info,
			     const char *uri,
			     const char *host,
			     const char *ip,
			     int port,
			     const char *service_name,
			     const char *type,
			     const char *domain,
			     const char *pdl,
			     int color,
			     int duplex,
			     const char *make_model,
			     int is_cups_queue)
{
  remote_printer_t *p;
  remote_printer_t *q;
  int		fd = 0;			/* Script file descriptor */
  char		tempfile[1024];		/* Temporary file */
  char		buffer[8192];		/* Buffer for creating script */
  int           bytes;
  const char	*cups_serverbin;	/* CUPS_SERVERBIN environment variable */
  int uri_status, host_port;
  http_t *http = NULL;
  char scheme[10], userpass[1024], host_name[1024], resource[1024];
  ipp_t *request, *response = NULL;
#ifdef HAVE_CUPS_1_6
  ipp_attribute_t *attr;
  char valuebuffer[65536];
  int i, count, left, right, bottom, top;
  const char *default_page_size = NULL, *best_color_space = NULL, *color_space;
  int is_pwgraster = 0;
  int is_appleraster = 0;
  int is_pclm = 0;
  int is_pdf = 0;
#endif /* HAVE_CUPS_1_6 */

  if (!queue_name || !location || !info || !uri || !host || !service_name ||
      !type || !domain) {
    debug_printf("ERROR: create_remote_printer_entry(): Input value missing!\n");
    return NULL;
  }

  /* Mark this as a queue to be created locally pointing to the printer */
  if ((p = (remote_printer_t *)calloc(1, sizeof(remote_printer_t))) == NULL) {
    debug_printf("ERROR: Unable to allocate memory.\n");
    return NULL;
  }

  /* Assure that, if we have forgotten to set a field in the printer
     record, that it is set to zero */
  memset(p, 0, sizeof(remote_printer_t));
  
  /* Queue name */
  p->queue_name = strdup(queue_name);
  if (!p->queue_name)
    goto fail;

  p->location = strdup(location);
  if (!p->location)
    goto fail;

  p->info = strdup(info);
  if (!p->info)
    goto fail;

  p->uri = strdup(uri);
  if (!p->uri)
    goto fail;

  p->slave_of = NULL;
  p->last_printer = -1;
  
  p->num_options = 0;
  p->options = NULL;
  
  p->host = strdup (host);
  if (!p->host)
    goto fail;

  p->ip = (ip != NULL ? strdup (ip) : NULL);

  p->port = (port != 0 ? port : ippPort());

  p->service_name = strdup (service_name);
  if (!p->service_name)
    goto fail;

  /* Record DNS-SD service parameters to identify print queue
     entry for removal when service disappears */
  p->type = strdup (type);
  if (!p->type)
    goto fail;

  p->domain = strdup (domain);
  if (!p->domain)
    goto fail;

  /* Schedule for immediate creation of the CUPS queue */
  p->status = STATUS_TO_BE_CREATED;
  p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;

  /* Flag which can be set to inhibit automatic saving of option settings
     by the on_printer_modified() notification handler function */
  p->no_autosave = 0;

  /* Flag to mark whether this printer was discovered through a legacy
     CUPS broadcast (1) or through DNS-SD (0) */
  p->is_legacy = 0;

  /* Remote CUPS printer or local queue remaining from previous cups-browsed
     session */
  /* is_cups_queue: -1: Unknown, 0: IPP printer, 1: Remote CUPS queue,
                     2: Remote CUPS queue in user-defined cluster      */
  if (is_cups_queue != 0) {
    if (is_cups_queue > 0 && CreateRemoteCUPSPrinterQueues == 0) {
      debug_printf("Printer %s (%s) is a remote CUPS printer and cups-browsed is not configured to set up such printers automatically, ignoring this printer.\n",
		   p->queue_name, p->uri);
      goto fail;
    }
    /* For a remote CUPS printer our local queue will be raw or get a
       PPD file from the remote CUPS server, so that the driver on the
       remote CUPS server gets used. So we will not generate a PPD file
       or interface script at this point. */
    p->netprinter = 0;
    p->ppd = NULL;
    p->model = NULL;
    p->ifscript = NULL;
    /* Check whether we have an equally named queue already from another
       server */
    for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 q;
	 q = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!strcasecmp(q->queue_name, p->queue_name) && /* Queue with same name
							  on server */
	  !q->slave_of) /* Find the master of the queues with this name,
			   to avoid "daisy chaining" */
	break;
    if (q && AutoClustering == 0 && is_cups_queue == 1) {
      debug_printf("We have already created a queue with the name %s for another remote CUPS printer but automatic clustering of equally named printers is turned off nor did we find a manually defined cluster this printer belongs to. Skipping this printer.\n", p->queue_name);
      debug_printf("In cups-browsed.conf try setting \"AutoClustering On\" to cluster equally-named remote CUPS printers, \"LocalQueueNamingRemoteCUPS DNS-SD\" to avoid queue name clashes, or define clusters with the \"Cluster\" directive.\n");
      goto fail;
    }
    if (q && q->netprinter == 1) {
      debug_printf("We have already created a queue with the name %s for another printer which is not a remote CUPS printer. Skipping this printer.\n", p->queue_name);
      debug_printf("Try setting \"LocalQueueNamingRemoteCUPS DNS-SD\" or \"LocalQueueNamingRemoteCUPS RemoteName\" in cups-browsed.conf.\n");
      goto fail;
    }
    p->slave_of = (q && q->status != STATUS_DISAPPEARED &&
		   q->status != STATUS_UNCONFIRMED) ? q : NULL;
    if (p->slave_of) {
      debug_printf("Printer %s already available through host %s, port %d.\n",
		   p->queue_name, q->host, q->port);
      /* Update q */
      q->status = STATUS_TO_BE_CREATED;
      q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
      log_cluster(p);
    } else if (q) {
      q->slave_of = p;
      debug_printf("Unconfirmed/disappeared printer %s already available through host %s, port %d, marking that printer a slave of the newly found one.\n",
		   p->queue_name, q->host, q->port);
      log_cluster(p);
    }
  } else {
#ifndef HAVE_CUPS_1_6
    /* The following code uses a lot of CUPS >= 1.6 specific stuff.
       For older CUPS <= 1.5.4 the following functionality is skipped
       which means for CUPS <= 1.5.4 only for CUPS printer broadcasts
       there are local queues created which should be sufficient
       on systems where traditional CUPS <= 1.5.4 is used. */
    goto fail;
#else /* HAVE_CUPS_1_6 */
    /* Non-CUPS printer broadcasts are most probably from printers
       directly connected to the network and using the IPP protocol.
       We check whether we can set them up without a device-specific
       driver, only using page description languages which the
       operating system provides: PCL 5c/5e/6/XL, PostScript, PDF, PWG
       Raster, Apple Raster, PCLm. Especially printers designed for
       driverless printing (DNS-SD + IPP 2.x + at least one of PWG
       Raster, Apple Raster, PCLm, PDF) will work this way. Making
       only driverless queues we can get an easy, configuration-less
       way to print from mobile devices, even if there is no CUPS
       server with shared printers around. */

    if (CreateIPPPrinterQueues == IPP_PRINTERS_NO) {
      debug_printf("Printer %s (%s) is an IPP network printer and cups-browsed is not configured to set up such printers automatically, ignoring this printer.\n",
		   p->queue_name, p->uri);
      goto fail;
    }

    if (!pdl || pdl[0] == '\0' ||
	(!strcasestr(pdl, "application/postscript") &&
	 !strcasestr(pdl, "application/pdf") &&
	 !strcasestr(pdl, "image/pwg-raster") &&
#ifdef CUPS_RASTER_HAVE_APPLERASTER
	 !strcasestr(pdl, "image/urf") &&
#endif
#ifdef QPDF_HAVE_PCLM
	 !strcasestr(pdl, "application/PCLm") &&
#endif
	 ((!strcasestr(pdl, "application/vnd.hp-PCL") &&
	   !strcasestr(pdl, "application/PCL") &&
	   !strcasestr(pdl, "application/x-pcl")) ||
	  ((!strncasecmp(make_model, "HP", 2) || /* HP inkjets not supported */
	    !strncasecmp(make_model, "Hewlett Packard", 15) ||
	    !strncasecmp(make_model, "Hewlett-Packard", 15)) &&
	   !strcasestr(make_model, "LaserJet") &&
	   !strcasestr(make_model, "Mopier"))) &&
	 !strcasestr(pdl, "application/vnd.hp-PCLXL"))) {
      debug_printf("Cannot create remote printer %s (URI: %s, Model: %s, Accepted data formats: %s) as its PDLs are not known, ignoring this printer.\n",
		   p->queue_name, p->uri, make_model, pdl);
      debug_printf("Supported PDLs: PWG Raster, %s%sPostScript, PDF, PCL XL, PCL 5c/e (HP inkjets report themselves as PCL printers but their PCL is not supported)\n",
#ifdef CUPS_RASTER_HAVE_APPLERASTER
		   "Apple Raster, ",
#else
		   "",
#endif
#ifdef QPDF_HAVE_PCLM
		   "PCLm, "
#else
		   ""
#endif
		   );
      goto fail;
    }

    /* Check whether we have an equally named queue already */
    for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 q;
	 q = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!strcasecmp(q->queue_name, p->queue_name)) {/* Queue with same name */
	debug_printf("We have already created a queue with the name %s for another printer. Skipping this printer.\n", p->queue_name);
	debug_printf("Try setting \"LocalQueueNamingIPPPrinter DNS-SD\" in cups-browsed.conf.\n");
	goto fail;
      }

    p->slave_of = NULL;
    p->model = NULL;
    p->netprinter = 1;

    /* Request printer properties via IPP to generate a PPD file for the
       printer (mainly driverless-capable printers)
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
      debug_printf("Cannot connect to remote printer %s (%s:%d), ignoring this printer.\n",
		   p->uri, host_name, host_port);
      goto fail;
    }
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    response = cupsDoRequest(http, request, resource);

    /* Log all printer attributes for debugging */
    if (debug_stderr || debug_logfile) {
      attr = ippFirstAttribute(response);
      while (attr) {
	debug_printf("Attr: %s\n",
		     ippGetName(attr));
	ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
	debug_printf("Value: %s\n", valuebuffer);
	for (i = 0; i < ippGetCount(attr); i ++)
	  debug_printf("Keyword: %s\n",
		       ippGetString(attr, i, NULL));
	attr = ippNextAttribute(response);
      }
    }

    /* If we have opted for only printers designed for driverless use (PWG
       Raster + Apple Raster + PCLm + PDF) being set up automatically, we check
       first, whether our printer supports IPP 2.0 or newer. If not, we
       skip this printer */
    if (CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS) {
      valuebuffer[0] = '\0';
      if ((attr = ippFindAttribute(response,
				   "ipp-versions-supported",
				   IPP_TAG_KEYWORD)) != NULL) {
	debug_printf("Checking whether printer %s supports IPP 2.x or newer: Attr: %s\n",
		     p->queue_name, ippGetName(attr));
	for (i = 0; i < ippGetCount(attr); i ++) {
	  strncpy(valuebuffer, ippGetString(attr, i, NULL),
		  sizeof(valuebuffer));
	  debug_printf("Checking whether printer %s supports IPP 2.x or newer: Keyword: %s\n",
		       p->queue_name, valuebuffer);
	  if (valuebuffer[0] > '1')
	    break;
	}
      }
      if (!attr || valuebuffer[0] == '\0' || valuebuffer[0] <= '1') {
	debug_printf("cups-browsed is configured to auto-setup only printers which are designed for driverless printing. These printers require IPP 2.x or newer, but this printer only supports IPP 1.x or older. Skipping.\n");
	goto fail;
      } else
	debug_printf("--> Printer supports IPP 2.x or newer.\n");
    }

    /* If we have opted for only PWG Raster printers or for only printers 
       designed for driverless use (PWG Raster + Apple Raster + PCLm + PDF)
       being set up automatically, we check whether the printer has a non-empty
       string in its "pwg-raster-document-resolution-supported" IPP attribute
       to see whether we have a PWG Raster printer. */
    if (CreateIPPPrinterQueues == IPP_PRINTERS_PWGRASTER ||
	CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS) {
      valuebuffer[0] = '\0';
      if ((attr = ippFindAttribute(response,
				   "pwg-raster-document-resolution-supported",
				   IPP_TAG_KEYWORD)) != NULL) {
	debug_printf("Checking whether printer %s is PWG Raster: Attr: %s\n",
		     p->queue_name, ippGetName(attr));
	ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
	debug_printf("Checking whether printer %s is PWG Raster: Value: %s\n",
		     p->queue_name, valuebuffer);
	if (valuebuffer[0] == '\0') {
	  for (i = 0; i < ippGetCount(attr); i ++) {
	    strncpy(valuebuffer, ippGetString(attr, i, NULL),
		    sizeof(valuebuffer));
	    debug_printf("Checking whether printer %s is PWG Raster: Keyword: %s\n",
			 p->queue_name, valuebuffer);
	    if (valuebuffer[0] != '\0')
	      break;
	  }
	}
      }
      if (attr && valuebuffer[0] != '\0')
        is_pwgraster = 1;
      debug_printf("--> Printer %s PWG Raster.\n",
		   is_pwgraster ? "supports" : "does not support");
    }

#ifdef CUPS_RASTER_HAVE_APPLERASTER
    /* If we have opted for only Apple Raster printers or for only printers 
       designed for driverless use (PWG Raster + Apple Raster + PCLm + PDF)
       being set up automatically, we check whether the printer has a non-empty
       string in its "urf-supported" IPP attribute to see whether we have an
       Apple Raster printer. */
    if (CreateIPPPrinterQueues == IPP_PRINTERS_APPLERASTER ||
	CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS) {
      valuebuffer[0] = '\0';
      if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL) {
	debug_printf("Checking whether printer %s understands Apple Raster: Attr: %s\n",
		     p->queue_name, ippGetName(attr));
	ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
	debug_printf("Checking whether printer %s understands Apple Raster: Value: %s\n",
		     p->queue_name, valuebuffer);
	if (valuebuffer[0] == '\0') {
	  for (i = 0; i < ippGetCount(attr); i ++) {
	    strncpy(valuebuffer, ippGetString(attr, i, NULL),
		    sizeof(valuebuffer));
	    debug_printf("Checking whether printer %s understands Apple Raster: Keyword: %s\n",
			 p->queue_name, valuebuffer);
	    if (valuebuffer[0] != '\0')
	      break;
	  }
	}
      }
      if (attr && valuebuffer[0] != '\0')
        is_appleraster = 1;
      debug_printf("--> Printer %s Apple Raster.\n",
		   is_appleraster ? "supports" : "does not support");
    }
#endif

#ifdef QPDF_HAVE_PCLM
    /* If we have opted for only PCLm printers or for only printers 
       designed for driverless use (PWG Raster + Apple Raster + PCLm + PDF)
       being set up automatically, we check whether the printer has a non-empty
       string in its "pclm-compression-method-preferred" IPP attribute to see
       whether we have a PCLm printer. */
    if (CreateIPPPrinterQueues == IPP_PRINTERS_PCLM ||
	CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS) {
      valuebuffer[0] = '\0';
      if ((attr = ippFindAttribute(response,
				   "pclm-compression-method-preferred",
				   IPP_TAG_KEYWORD)) != NULL) {
	debug_printf("Checking whether printer %s understands PCLm: Attr: %s\n",
		     p->queue_name, ippGetName(attr));
	ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
	debug_printf("Checking whether printer %s understands PCLm: Value: %s\n",
		     p->queue_name, valuebuffer);
	if (valuebuffer[0] == '\0') {
	  for (i = 0; i < ippGetCount(attr); i ++) {
	    strncpy(valuebuffer, ippGetString(attr, i, NULL),
		    sizeof(valuebuffer));
	    debug_printf("Checking whether printer %s understands PCLm: Keyword: %s\n",
			 p->queue_name, valuebuffer);
	    if (valuebuffer[0] != '\0')
	      break;
	  }
	}
      }
      if (attr && valuebuffer[0] != '\0')
        is_pclm = 1;
      debug_printf("--> Printer %s PCLm.\n",
		   is_pclm ? "supports" : "does not support");
    }
#endif

    /* If we have opted for only PDF printers or for only printers 
       designed for driverless use (PWG Raster + Apple Raster + PCLm + PDF)
       being set up automatically, we check whether the printer has 
       "application/pdf" under its PDLs. */
    if (CreateIPPPrinterQueues == IPP_PRINTERS_PDF ||
	CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS) {
	debug_printf("Checking whether printer %s understands PDF: PDLs: %s\n",
		     p->queue_name, pdl);
      if(strcasestr(pdl, "application/pdf"))
        is_pdf = 1;
      debug_printf("--> Printer %s PDF.\n",
		   is_pdf ? "supports" : "does not support");
    }

    /* If the printer is not the driverless printer we opted for, we skip
       this printer. */
    if ((CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS &&
	 is_pwgraster == 0 && is_appleraster == 0 && is_pclm == 0 &&
	 is_pdf == 0) ||
	(CreateIPPPrinterQueues == IPP_PRINTERS_PWGRASTER &&
	 is_pwgraster == 0) ||
	(CreateIPPPrinterQueues == IPP_PRINTERS_APPLERASTER &&
	 is_appleraster == 0) ||
	(CreateIPPPrinterQueues == IPP_PRINTERS_PCLM &&
	 is_pclm == 0) ||
	(CreateIPPPrinterQueues == IPP_PRINTERS_PDF &&
	 is_pdf == 0)) {
      debug_printf("Printer %s (%s%s%s%s%s%s%s%s%s%s%s%s%s) does not support the driverless printing protocol cups-browsed is configured to accept for setting up such printers automatically, ignoring this printer.\n",
		   p->queue_name, p->uri,
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PWGRASTER ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    ", " : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PWGRASTER ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    (is_pwgraster ? "" : "not ") : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PWGRASTER ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    "PWG Raster" : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_APPLERASTER ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    ", " : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_APPLERASTER ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    (is_appleraster ? "" : "not ") : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_APPLERASTER ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    "Apple Raster" : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PCLM ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    ", " : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PCLM ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    (is_pclm ? "" : "not ") : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PCLM ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    "PCLm" : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PDF ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    ", " : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PDF ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    (is_pdf ? "" : "not ") : ""),
		   (CreateIPPPrinterQueues == IPP_PRINTERS_PDF ||
		    CreateIPPPrinterQueues == IPP_PRINTERS_DRIVERLESS ?
		    "PDF" : ""));
      goto fail;
    }

    if (IPPPrinterQueueType == PPD_YES) {
      if (!ppdCreateFromIPP(buffer, sizeof(buffer), response, make_model,
			    pdl, color, duplex)) {
	if (errno != 0)
	  debug_printf("Unable to create PPD file: %s\n", strerror(errno));
	else
	  debug_printf("Unable to create PPD file: %s\n", ppdgenerator_msg);
	goto fail;
      } else {
	debug_printf("PPD generation successful: %s\n", ppdgenerator_msg);
	debug_printf("Created temporary PPD file: %s\n", buffer);
	p->ppd = strdup(buffer);
	p->ifscript = NULL;
      }
    } else if (IPPPrinterQueueType == PPD_NO) {
      p->ppd = NULL;

      /* Find default page size of the printer */
      attr = ippFindAttribute(response,
			      "media-default",
			      IPP_TAG_ZERO);
      if (attr) {
	default_page_size = ippGetString(attr, 0, NULL);
	debug_printf("Default page size: %s\n",
		     default_page_size);
	p->num_options = cupsAddOption("media-default",
				       strdup(default_page_size),
				       p->num_options, &(p->options));
      } else {
	attr = ippFindAttribute(response,
				"media-ready",
				IPP_TAG_ZERO);
	if (attr) {
	  default_page_size = ippGetString(attr, 0, NULL);
	  debug_printf("Default page size: %s\n",
		       default_page_size);
	  p->num_options = cupsAddOption("media-default",
					 strdup(default_page_size),
					 p->num_options, &(p->options));
	} else
	  debug_printf("No default page size found!\n");
      }

      /* Find maximum unprintable margins of the printer */
      if ((attr = ippFindAttribute(response, "media-bottom-margin-supported", IPP_TAG_INTEGER)) != NULL) {
	for (i = 1, bottom = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
	  if (ippGetInteger(attr, i) > bottom)
	    bottom = ippGetInteger(attr, i);
      } else
	bottom = 1270;
      snprintf(buffer, sizeof(buffer), "%d", bottom);
      p->num_options = cupsAddOption("media-bottom-margin-default",
				     strdup(buffer),
				     p->num_options, &(p->options));

      if ((attr = ippFindAttribute(response, "media-left-margin-supported", IPP_TAG_INTEGER)) != NULL) {
	for (i = 1, left = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
	  if (ippGetInteger(attr, i) > left)
	    left = ippGetInteger(attr, i);
      } else
	left = 635;
      snprintf(buffer, sizeof(buffer), "%d", left);
      p->num_options = cupsAddOption("media-left-margin-default",
				     strdup(buffer),
				     p->num_options, &(p->options));

      if ((attr = ippFindAttribute(response, "media-right-margin-supported", IPP_TAG_INTEGER)) != NULL) {
	for (i = 1, right = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
	  if (ippGetInteger(attr, i) > right)
	    right = ippGetInteger(attr, i);
      } else
	right = 635;
      snprintf(buffer, sizeof(buffer), "%d", right);
      p->num_options = cupsAddOption("media-right-margin-default",
				     strdup(buffer),
				     p->num_options, &(p->options));

      if ((attr = ippFindAttribute(response, "media-top-margin-supported", IPP_TAG_INTEGER)) != NULL) {
	for (i = 1, top = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
	  if (ippGetInteger(attr, i) > top)
	    top = ippGetInteger(attr, i);
      } else
	top = 1270;
      snprintf(buffer, sizeof(buffer), "%d", top);
      p->num_options = cupsAddOption("media-top-margin-default",
				     strdup(buffer),
				     p->num_options, &(p->options));

      debug_printf("Margins: Left: %d, Right: %d, Top: %d, Bottom: %d\n",
		   left, right, top, bottom);

      /* Find best color space of the printer */
      attr = ippFindAttribute(response,
			      "pwg-raster-document-type-supported",
			      IPP_TAG_ZERO);
      if (attr) {
	for (i = 0; i < ippGetCount(attr); i ++) {
	  color_space = ippGetString(attr, i, NULL);
	  debug_printf("Supported color space: %s\n", color_space);
	  if (color_space_score(color_space) >
	      color_space_score(best_color_space))
	    best_color_space = color_space;
	}
	debug_printf("Best color space: %s\n",
		     best_color_space);
	p->num_options = cupsAddOption("print-color-mode-default",
				       strdup(best_color_space),
				       p->num_options, &(p->options));
      } else {
	debug_printf("No info about supported color spaces found!\n");
	p->num_options = cupsAddOption("print-color-mode-default",
				       color == 1 ? "rgb" : "black",
				       p->num_options, &(p->options));
      }

      if (duplex)
	p->num_options = cupsAddOption("sides-default", "two-sided-long-edge",
				       p->num_options, &(p->options));
	
      p->num_options = cupsAddOption("output-format-default", strdup(pdl),
				     p->num_options, &(p->options));
      p->num_options = cupsAddOption("make-and-model-default",
				     remove_bad_chars(make_model, 0),
				     p->num_options, &(p->options));

      if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
      cups_serverbin = CUPS_SERVERBIN;

      if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0) {
	debug_printf("Unable to create interface script file\n");
	goto fail;
      }
      
      debug_printf("Creating temp script file \"%s\"\n", tempfile);

      snprintf(buffer, sizeof(buffer),
	       "#!/bin/sh\n"
	       "# System V interface script for printer %s generated by cups-browsed\n"
	       "\n"
	       "if [ $# -lt 5 -o $# -gt 6 ]; then\n"
	       "  echo \"ERROR: $0 job-id user title copies options [file]\" >&2\n"
	       "  exit 1\n"
	       "fi\n"
	       "\n"
	       "# Read from given file\n"
	       "if [ -n \"$6\" ]; then\n"
	       "  exec \"$0\" \"$1\" \"$2\" \"$3\" \"$4\" \"$5\" < \"$6\"\n"
	       "fi\n"
	       "\n"
	       "%s/filter/sys5ippprinter \"$1\" \"$2\" \"$3\" \"$4\" \"$5\"\n",
	       p->queue_name, cups_serverbin);

      bytes = write(fd, buffer, strlen(buffer));
      if (bytes != strlen(buffer)) {
	debug_printf("Unable to write interface script into the file\n");
	goto fail;
      }

      close(fd);

      p->ifscript = strdup(tempfile);
    }

    /*p->model = "drv:///sample.drv/laserjet.ppd";
      debug_printf("PPD from system for %s: %s\n", p->queue_name, p->model);*/

    /*p->ppd = "/usr/share/ppd/cupsfilters/pxlcolor.ppd";
      debug_printf("PPD from file for %s: %s\n", p->queue_name, p->ppd);*/

    /*p->ifscript = "/usr/lib/cups/filter/sys5ippprinter";
      debug_printf("System V Interface script for %s: %s\n", p->queue_name, p->ifscript);*/

#endif /* HAVE_CUPS_1_6 */
  }

  /* Add the new remote printer entry */
  log_all_printers();
  cupsArrayAdd(remote_printers, p);
  log_all_printers();

  /* If auto shutdown is active we have perhaps scheduled a timer to shut down
     due to not having queues any more to maintain, kill the timer now */
  if (autoshutdown && autoshutdown_exec_id &&
      autoshutdown_on == NO_QUEUES &&
      cupsArrayCount(remote_printers) > 0) {
    debug_printf ("New printers there to make available, killing auto shutdown timer.\n");
    g_source_remove(autoshutdown_exec_id);
    autoshutdown_exec_id = 0;
  }

  ippDelete(response);
  if (http)
    httpClose(http);
  return p;

 fail:
  debug_printf("ERROR: Unable to create print queue, ignoring printer.\n");
  if (response) ippDelete(response);
  if (http)
    httpClose(http);
  if (p->type) free (p->type);
  if (p->service_name) free (p->service_name);
  if (p->host) free (p->host);
  if (p->domain) free (p->domain);
  if (p->ip) free (p->ip);
  cupsFreeOptions(p->num_options, p->options);
  if (p->uri) free (p->uri);
  if (p->location) free (p->location);
  if (p->info) free (p->info);
  if (p->queue_name) free (p->queue_name);
  if (p->ppd) free (p->ppd);
  if (p->model) free (p->model);
  if (p->ifscript) free (p->ifscript);
  free (p);
  return NULL;
}

void
remove_printer_entry(remote_printer_t *p) {
  remote_printer_t *q = NULL, *r;

  if (p == NULL) {
    debug_printf ("ERROR: remove_printer_entry(): Supplied printer entry is NULL");
    return;
  }

  if (!p->slave_of) {
    /* Check whether this queue has a slave from another server and
       find it */
    for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 q;
	 q = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (q != p && q->slave_of == p &&
	  q->status != STATUS_DISAPPEARED && q->status != STATUS_UNCONFIRMED)
	break;
  }
  if (q) {
    /* Make q the master of the cluster and p a slave of q. This way
       removal of p does not delete the cluster's CUPS queue and update 
       of q makes sure the cluster's queue gets back into working state */
    for (r = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 r;
	 r = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (r != q && r->slave_of == p &&
	  r->status != STATUS_DISAPPEARED && r->status != STATUS_UNCONFIRMED)
	r->slave_of = q;
    q->slave_of = NULL;
    p->slave_of = q;
    q->num_options = p->num_options;
    q->options = p->options;
    p->num_options = 0;
    p->options = NULL;
    /* Schedule this printer for updating the CUPS queue */
    q->status = STATUS_TO_BE_CREATED;
    q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
    debug_printf("Printer %s (%s) diasappeared, replacing by backup on host %s, port %d with URI %s.\n",
		 p->queue_name, p->uri, q->host, q->port, q->uri);
  } else
    debug_printf("Printer %s (Host: %s, Port: %d, URI: %s) disappeared and no slave available (or it is a slave of another printer), removing entry.\n",
		 p->queue_name, p->host, p->port, p->uri);
    
  /* Schedule entry and its CUPS queue for removal */
  p->status = STATUS_DISAPPEARED;
  p->timeout = time(NULL) + TIMEOUT_REMOVE;
}

gboolean update_cups_queues(gpointer unused) {
  remote_printer_t *p, *q, *r;
  http_t *http, *remote_http;
  char uri[HTTP_MAX_URI], device_uri[HTTP_MAX_URI], buf[1024], line[1024];
  char *remote_cups_queue;
  int num_options;
  cups_option_t *options;
  int num_jobs;
  cups_job_t *jobs;
  ipp_t *request;
  time_t current_time = time(NULL);
  int i, new_cupsfilter_line_inserted, ap_remote_queue_id_line_inserted,
    cont_line_read, want_raw;
  char *disabled_str, *ptr, *prefix;
  const char *loadedppd = NULL;
  int pass_through_ppd;
  ppd_file_t *ppd;
  ppd_choice_t *choice;
  cups_file_t *in, *out;
  char keyword[1024], *keyptr;
  const char *customval;
  const char *val = NULL;
  cups_dest_t *dest = NULL;
  int is_temporary;

  debug_printf("update_cups_queues() in THREAD %ld\n", pthread_self());

  /* Create dummy entry to point slaves at when their master is about to
     get removed now (if we point them to NULL, we would try to remove
     the already removed CUPS queue again when it comes to the removal
     of the slave. */
  if ((r = (remote_printer_t *)calloc(1, sizeof(remote_printer_t))) == NULL) {
    debug_printf("ERROR: Unable to allocate memory.\n");
    if (in_shutdown == 0)
      recheck_timer ();
    return FALSE;
  }
  memset(r, 0, sizeof(remote_printer_t));
  r->uri = "<DELETED>";
  /* Now redirect the slave_of pointers of the masters which get deleted now
     to this dummy entry */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (p->status == STATUS_DISAPPEARED &&
	(q = p->slave_of) != NULL && q->status == STATUS_DISAPPEARED)
      p->slave_of = r;

  debug_printf("Processing printer list ...\n");
  log_all_printers();
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {

    /* terminating means we have received a signal and should shut down.
       in_shutdown means we have exited the main loop.
       update_cups_queues() is called after having exited the main loop
       in order to remove any queues we have set up */
    if (terminating && !in_shutdown) {
      debug_printf("Stopping processing printer list because cups-browsed is terminating.\n");
      break;
    }

    switch (p->status) {

    /* Print queue generated by us in a previous session */
    case STATUS_UNCONFIRMED:

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      /* Queue not reported again by DNS-SD, remove it */
      debug_printf("No remote printer named %s available, removing entry from previous session.\n",
		   p->queue_name);
      remove_printer_entry(p);

    /* DNS-SD has reported this printer as disappeared or we have replaced
       this printer by another one */
    case STATUS_DISAPPEARED:

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      debug_printf("Removing entry %s (%s)%s.\n", p->queue_name, p->uri,
		   (p->slave_of ? "" : " and its CUPS queue"));

      /* Remove the CUPS queue */
      /* Slaves do not have a CUPS queue */
      if ((q = p->slave_of) == NULL) {
	
	if ((http = http_connect_local ()) == NULL) {
	  debug_printf("Unable to connect to CUPS!\n");
	  if (in_shutdown == 0)
	    p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}

	/* Do not auto-save option settings due to the print queue removal
	   process */
	p->no_autosave = 1;

	/* Record the option settings to retrieve them when the remote
	   queue re-appears later or when cups-browsed gets started again */
	record_printer_options(p->queue_name);

	/* Check whether there are still jobs and do not remove the queue
	   then */
	num_jobs = 0;
	jobs = NULL;
	num_jobs = cupsGetJobs2(http, &jobs, p->queue_name, 0, CUPS_WHICHJOBS_ACTIVE);
	if (num_jobs != 0) { /* error or jobs */
	  debug_printf("Queue has still jobs or CUPS error!\n");
	  cupsFreeJobs(num_jobs, jobs);
	  /* Disable the queue */
#ifdef HAVE_AVAHI
	  if (avahi_present || p->domain == NULL || p->domain[0] == '\0')
	    /* If avahi has got shut down, do not disable queues which are,
	       created based on DNS-SD broadcasts as the server has most
	       probably not gone away */
#endif /* HAVE_AVAHI */
	    disable_printer(p->queue_name,
			    "Printer disappeared or cups-browsed shutdown");
	  /* Schedule the removal of the queue for later */
	  if (in_shutdown == 0) {
	    p->timeout = current_time + TIMEOUT_RETRY;
	    p->no_autosave = 0;
	    break;
	  }
	}

	/* If this queue was the default printer, note that fact so that
	   it gets the default printer again when it re-appears, also switch
	   back to the last local default printer */
	queue_removal_handle_default(p->queue_name);

	/* If we do not have a subscription to CUPS' D-Bus notifications and
	   so no default printer management, we simply do not remove this
	   CUPS queue if it is the default printer, to not cause a change
	   of the default printer or the loss of the information that this
	   printer is the default printer. */
	if (cups_notifier == NULL && is_cups_default_printer(p->queue_name)) {
	  /* Schedule the removal of the queue for later */
	  if (in_shutdown == 0) {
	    p->timeout = current_time + TIMEOUT_RETRY;
	    p->no_autosave = 0;
	    break;
	  }
	}

	/* No jobs, remove the CUPS queue */
	debug_printf("Removing local CUPS queue %s (%s).\n", p->queue_name,
		     p->uri);
	request = ippNewRequest(CUPS_DELETE_PRINTER);
	/* Printer URI: ipp://localhost:631/printers/<queue name> */
	httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
			 "localhost", ippPort(), "/printers/%s", p->queue_name);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		     "printer-uri", NULL, uri);
	/* Default user */
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		     "requesting-user-name", NULL, cupsUser());
	/* Do it */
	ippDelete(cupsDoRequest(http, request, "/admin/"));
	if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
	  debug_printf("Unable to remove CUPS queue!\n");
	  if (in_shutdown == 0) {
	    p->timeout = current_time + TIMEOUT_RETRY;
	    p->no_autosave = 0;
	    break;
	  }
	}
      }

      /* CUPS queue removed, remove the list entry */
      /* Note that we do not need to break out of the loop passing through
	 all elements of a CUPS array when we remove an element via the
	 cupsArrayRemove() function, as the function decreases the array-
	 internal index by one and so the cupsArrayNext() call gives us
	 the element right after the deleted element. So no skipping
         of an element and especially no reading beyond the  end of the
         array. */
      cupsArrayRemove(remote_printers, p);
      if (p->queue_name) free (p->queue_name);
      if (p->location) free (p->location);
      if (p->info) free (p->info);
      if (p->uri) free (p->uri);
      cupsFreeOptions(p->num_options, p->options);
      if (p->host) free (p->host);
      if (p->ip) free (p->ip);
      if (p->service_name) free (p->service_name);
      if (p->type) free (p->type);
      if (p->domain) free (p->domain);
      if (p->ppd) free (p->ppd);
      if (p->model) free (p->model);
      if (p->ifscript) free (p->ifscript);
      free(p);
      p = NULL;

      /* If auto shutdown is active and all printers we have set up got removed
	 again, schedule the shutdown in autoshutdown_timeout seconds 
         Note that in this case we also do not have jobs any more so if we
         auto shutdown on running out of jobs, trigger it here, too. */
      if (in_shutdown == 0 && autoshutdown && !autoshutdown_exec_id &&
	  (cupsArrayCount(remote_printers) == 0 ||
	   (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
	debug_printf ("No printers there any more to make available or no jobs, shutting down in %d sec...\n", autoshutdown_timeout);
	autoshutdown_exec_id =
	  g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
				 NULL);
      }

      break;

    /* DNS-SD has reported a new remote printer, create a CUPS queue for it,
       or upgrade an existing queue, or update a queue to use a backup host
       when it has disappeared on the currently used host */
      /* (...or, we've just received a CUPS Browsing packet for this queue) */
    case STATUS_TO_BE_CREATED:

      /* Do not create a queue for slaves */
      if (p->slave_of) {
	p->status = STATUS_CONFIRMED;
	if (p->is_legacy) {
	  p->timeout = time(NULL) + BrowseTimeout;
	  debug_printf("starting BrowseTimeout timer for %s (%ds)\n",
		       p->queue_name, BrowseTimeout);
	} else
	  p->timeout = (time_t) -1;
	break;
      }

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      debug_printf("Creating/Updating CUPS queue %s\n",
		   p->queue_name);

      /* Make sure to have a connection to the local CUPS daemon */
      if ((http = http_connect_local ()) == NULL) {
	debug_printf("Unable to connect to CUPS!\n");
	p->timeout = current_time + TIMEOUT_RETRY;
	break;
      }
      httpSetTimeout(http, HttpLocalTimeout, http_timeout_cb, NULL);

      /* Do not auto-save option settings due to the print queue creation
	 process */
      p->no_autosave = 1;

      /* Printer URI: ipp://localhost:631/printers/<queue name> */
      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		       "localhost", ippPort(), "/printers/%s", p->queue_name);

      /* If we have already a temporary CUPS queue our local queue we
	 are creating would overwrite the temporary queue, and so the
	 resulting queue will still be considered temporary by CUPS and
	 removed after one minute of inactivity. To avoid this we need
	 to convertthe queue into a permanent one and CUPS does this
	 only by sharing the queue (setting its boolean printer-is-shared
	 option. We unset the bit right after that to not actually share
	 the queue (if we want to share the queue we take care about this
	 later).
	 If the temporary queue is pointing to a remote CUPS printer
	 we cannot modify its printer-is-shared option as CUPS prevents
	 this. In this case we remove the temporary queue so that we
	 create a fresh one which will always be permanent.
	 If the temporary queue has still jobs we will not remove it to 
	 not loose the jobs and wait with creating our new queue until
	 the jobs are done. */

      /* Check whether there is a temporary CUPS queue which we would
         overwrite */
      dest = cupsGetNamedDest(http, p->queue_name, NULL);
      if (dest) {
	val = cupsGetOption ("printer-is-temporary",
			     dest->num_options,
			     dest->options);
	is_temporary = val && (!strcasecmp (val, "yes") ||
			       !strcasecmp (val, "on") ||
			       !strcasecmp (val, "true"));
	cupsFreeDests(1, dest);
	if (is_temporary) {
	  debug_printf("Our new queue overwrites the temporary CUPS queue %s, so we need to make the queue permanent.\n",
		       p->queue_name);
	  /* We need to modify the printer-is-shared bit twice if we need to
	     make a temporary queue permanent but not share this queue */
	  for (i = 0; i <= 1; i ++) {
	    if (i == 0)
	      debug_printf("Setting printer-is-shared bit to make this queue permanent.\n");
	    else
	      debug_printf("Unsetting printer-is-shared bit.\n");
	    request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
			 "printer-uri", NULL, uri);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
			 "requesting-user-name", NULL, cupsUser());
	    num_options = 0;
	    options = NULL;
	    num_options = cupsAddOption("printer-is-shared",
					(i == 0 ? "true" : "false"),
					num_options, &options);
	    cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
	    cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
	    ippDelete(cupsDoRequest(http, request, "/admin/"));
	    cupsFreeOptions(num_options, options);
	    if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
	      debug_printf("Unable change printer-is-shared bit to %s (%s)!\n",
			   (i == 0 ? "true" : "false"),
			   cupsLastErrorString());
	      break;
	    }
	  }
	  /* Error on modifying printer-is-shared bit, removing temporary
	     queue */
	  if (i <= 1) {
	    debug_printf("Removing the temporary CUPS queue.\n");
	    /* Check whether there are still jobs and do not remove the queue
	       then */
	    num_jobs = 0;
	    jobs = NULL;
	    num_jobs = cupsGetJobs2(http, &jobs, p->queue_name, 0,
				    CUPS_WHICHJOBS_ACTIVE);
	    if (num_jobs != 0) { /* error or jobs */
	      debug_printf("Temporary queue has still jobs or CUPS error, retrying later.\n");
	      cupsFreeJobs(num_jobs, jobs);
	      /* Schedule the removal of the queue for later */
	      if (in_shutdown == 0) {
		p->timeout = current_time + TIMEOUT_RETRY;
		p->no_autosave = 0;
		break;
	      }
	    }
	    /* No jobs, remove the CUPS queue */
	    request = ippNewRequest(CUPS_DELETE_PRINTER);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
			 "printer-uri", NULL, uri);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
			 "requesting-user-name", NULL, cupsUser());
	    ippDelete(cupsDoRequest(http, request, "/admin/"));
	    if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
	      debug_printf("Unable to remove temporary CUPS queue, retrying later\n");
	      if (in_shutdown == 0) {
		p->timeout = current_time + TIMEOUT_RETRY;
		p->no_autosave = 0;
		break;
	      }
	    }
	  }
	} else
	  debug_printf("Creating/Updating permanent CUPS queue %s.\n",
		       p ->queue_name);
      } else
	debug_printf("Creating permanent CUPS queue %s.\n",
		     p->queue_name);

      /* Loading saved option settings from last session */
      p->num_options = load_printer_options(p->queue_name, p->num_options,
					    &p->options);

      /* If we create a queue to a remote CUPS printer we need the queue
	 name on the remote server */
      if (p->netprinter == 0)
	remote_cups_queue = strrchr(p->uri, '/') + 1;

      /* Determine whether we have an IPP network printer. If not we
	 have remote CUPS queue(s) and so we use an implicit class for
	 load balancing. In this case we will assign an
	 implicitclass:...  device URI, which makes cups-browsed find
	 the best destination for each job. */
      loadedppd = NULL;
      pass_through_ppd = 0;
      if (cups_notifier != NULL && p->netprinter == 0) {
	/* We are not an IPP network printer, so we use the device URI
	   implicitclass:<queue name>
	   We never use the implicitclass backend if we do not have D-Bus
	   notification from CUPS as we cannot assign a destination printer
	   to an incoming job then. */
	snprintf(device_uri, sizeof(device_uri), "implicitclass:%s",
		 p->queue_name);
	debug_printf("Print queue %s is for remote CUPS queue(s) and we get notifications from CUPS, using implicit class device URI %s\n",
		     p->queue_name, device_uri);
	if (!p->ppd && !p->model && !p->ifscript) {
	  /* Having another backend than the CUPS "ipp" backend the
	     options from the PPD of the queue on the server are not
	     automatically used on the client any more, so we have to
	     explicitly load the PPD from one of the servers, apply it
	     to our local queue, and replace its "*cupsFilter(2): ..."
	     lines by one line making the print data get passed through
	     to the server without filtering on the client (where not
	     necessarily the right filters/drivers are installed) so
	     that it gets filtered on the server. In addition, we prefix
	     the PPD's NickName, so that automatic PPD updating by the
	     distribution's package installation/update infrastructure
	     is suppressed. */
	  /* Load the PPD file from one of the servers */
	  if ((remote_http =
	       httpConnectEncryptShortTimeout(p->ip ? p->ip : p->host,
					      p->port ? p->port :
					      ippPort(),
					      cupsEncryption()))
	      == NULL) {
	    debug_printf("Could not connect to the server %s:%d for %s!\n",
			 p->host, p->port, p->queue_name);
	    p->timeout = current_time + TIMEOUT_RETRY;
	    p->no_autosave = 0;
	    break;
	  }
	  httpSetTimeout(remote_http, HttpRemoteTimeout, http_timeout_cb, NULL);
	  if ((loadedppd = cupsGetPPD2(remote_http, remote_cups_queue))
	      == NULL &&
	      CreateRemoteRawPrinterQueues == 0) {
	    debug_printf("Unable to load PPD file for %s from the server %s:%d!\n",
			 p->queue_name, p->host, p->port);
	    p->timeout = current_time + TIMEOUT_RETRY;
	    p->no_autosave = 0;
	    httpClose(remote_http);
	    break;
	  } else if (loadedppd) {
	    debug_printf("Loaded PPD file %s for printer %s from server %s:%d!\n",
			 loadedppd, p->queue_name, p->host, p->port);
	    /* Modify PPD to not filter the job */
	    pass_through_ppd = 1;
	  }
	  httpClose(remote_http);
	}
      } else {
	/* Device URI: ipp(s)://<remote host>:631/printers/<remote queue> */
	strncpy(device_uri, p->uri, sizeof(device_uri));
	debug_printf("Print queue %s is for an IPP network printer, or we do not get notifications from CUPS, using direct device URI %s\n",
		     p->queue_name, device_uri);
      }
      /* PPD from system's CUPS installation */
      if (p->model) {
	debug_printf("Loading system PPD %s for queue %s.\n",
		     p->model, p->queue_name);
	loadedppd = cupsGetServerPPD(http, p->model);
      }
      /* PPD readily available */
      if (p->ppd) {
	debug_printf("Using PPD %s for queue %s.\n",
		     p->ppd, p->queue_name);
	loadedppd = p->ppd;
      }
      if (loadedppd) {
	if ((ppd = ppdOpenFile(loadedppd)) == NULL) {
	  int linenum; /* Line number of error */
	  ppd_status_t status = ppdLastError(&linenum);
	  debug_printf("Unable to open PPD \"%s\": %s on line %d.",
		       loadedppd, ppdErrorString(status), linenum);
	  p->timeout = current_time + TIMEOUT_RETRY;
	  p->no_autosave = 0;
	  unlink(loadedppd);
	  break;
	}
	ppdMarkDefaults(ppd);
	cupsMarkOptions(ppd, p->num_options, p->options);
	if ((out = cupsTempFile2(buf, sizeof(buf))) == NULL) {
	  debug_printf("Unable to create temporary file!\n");
	  p->timeout = current_time + TIMEOUT_RETRY;
	  p->no_autosave = 0;
	  ppdClose(ppd);
	  unlink(loadedppd);
	  break;
	}
	if ((in = cupsFileOpen(loadedppd, "r")) == NULL) {
	  debug_printf("Unable to open the downloaded PPD file!\n");
	  p->timeout = current_time + TIMEOUT_RETRY;
	  p->no_autosave = 0;
	  cupsFileClose(out);
	  ppdClose(ppd);
	  unlink(loadedppd);
	  break;
	}
	debug_printf("Editing PPD file %s for printer %s, setting the option defaults of the previous cups-browsed session%s, saving the resulting PPD in %s.\n",
		     loadedppd, p->queue_name,
		     (pass_through_ppd == 1 ?
		      " and inhibiting client-side filtering of the job" : ""),
		     buf);
	new_cupsfilter_line_inserted = 0;
	ap_remote_queue_id_line_inserted = 0;
	cont_line_read = 0;
	while (cupsFileGets(in, line, sizeof(line))) {
	  if (pass_through_ppd == 1 &&
	      (!strncmp(line, "*cupsFilter:", 12) ||
	       !strncmp(line, "*cupsFilter2:", 13))) {
	    cont_line_read = 0;
	    /* "*cupfFilter(2): ..." line: Remove it and replace the
	       first one by a line which passes through the data
	       unfiltered */
	    if (new_cupsfilter_line_inserted == 0) {
	      cupsFilePrintf(out, "*cupsFilter: \"*/* 0 -\"\n");
	      new_cupsfilter_line_inserted = 1;
	    }
	    /* Find the end of the "*cupsFilter(2): ..." entry in the
	       case it spans more than one line */
	    do {
	      if (strlen(line) != 0) {
		ptr = line + strlen(line) - 1;
		while(isspace(*ptr) && ptr > line)
		  ptr --;
		if (*ptr == '"')
		  break;
	      }
	      cont_line_read = 1;
	    } while (cupsFileGets(in, line, sizeof(line)));
	  } else if (pass_through_ppd == 1 &&
		     !strncmp(line, "*NickName:", 10)) {
	    cont_line_read = 0;
	    /* Prefix the "NickName" of the printer so that automatic
	       PPD updaters skip this PPD */
	    ptr = strchr(line, '"');
	    if (ptr) {
	      ptr ++;
	      prefix = "Remote printer: ";
	      line[sizeof(line) - strlen(prefix) - 1] = '\0';
	      memmove(ptr + strlen(prefix), ptr, strlen(ptr) + 1);
	      memmove(ptr, prefix, strlen(prefix));
	      ptr = line + strlen(line) - 1;
	      while(isspace(*ptr) && ptr > line) {
		ptr --;
		*ptr = '\0';
	      }
	      if (*ptr != '"') {
		if (ptr < line + sizeof(line) - 2) {
		  *(ptr + 1) = '"';
		  *(ptr + 2) = '\0';
		} else {
		  line[sizeof(line) - 2] = '"';
		  line[sizeof(line) - 1] = '\0';
		}
	      }
	    }
	    cupsFilePrintf(out, "%s\n", line);
	  } else if (!strncmp(line, "*Default", 8)) {
	    cont_line_read = 0;
	    strncpy(keyword, line + 8, sizeof(keyword));
	    for (keyptr = keyword; *keyptr; keyptr ++)
	      if (*keyptr == ':' || isspace(*keyptr & 255))
		break;
	    *keyptr++ = '\0';
	    while (isspace(*keyptr & 255))
	      keyptr ++;
	    if (!strcmp(keyword, "PageRegion") ||
		!strcmp(keyword, "PageSize") ||
		!strcmp(keyword, "PaperDimension") ||
		!strcmp(keyword, "ImageableArea")) {
	      if ((choice = ppdFindMarkedChoice(ppd, "PageSize")) == NULL)
		choice = ppdFindMarkedChoice(ppd, "PageRegion");
	    } else
	      choice = ppdFindMarkedChoice(ppd, keyword);
	    if (choice && strcmp(choice->choice, keyptr)) {
	      if (strcmp(choice->choice, "Custom"))
		cupsFilePrintf(out, "*Default%s: %s\n", keyword,
			       choice->choice);
	      else if ((customval = cupsGetOption(keyword, p->num_options,
						  p->options)) != NULL)
		cupsFilePrintf(out, "*Default%s: %s\n", keyword, customval);
	      else
		cupsFilePrintf(out, "%s\n", line);
	    } else
	      cupsFilePrintf(out, "%s\n", line);
	  } else if (cont_line_read == 0 || strncmp(line, "*End", 4)) {
	    cont_line_read = 0;
	    /* Write an "APRemoteQueueID" line to make this queue marked
	       as remote printer by CUPS */
	    if (p->netprinter == 0 &&
		strncmp(line, "*%", 2) &&
		strncmp(line, "*PPD-Adobe:", 11) &&
		ap_remote_queue_id_line_inserted == 0) {
	      ap_remote_queue_id_line_inserted = 1;
	      cupsFilePrintf(out, "*APRemoteQueueID: \"\"\n");
	    }
	    /* Simply write out the line as we read it */
	    cupsFilePrintf(out, "%s\n", line);
	  }
	}
	if (pass_through_ppd == 1 && new_cupsfilter_line_inserted == 0)
	  cupsFilePrintf(out, "*cupsFilter: \"*/* 0 -\"\n");
	cupsFileClose(in);
	cupsFileClose(out);
	ppdClose(ppd);
	unlink(loadedppd);
	loadedppd = NULL;
	if (p->ppd)
	  free(p->ppd);
	p->ppd = strdup(buf);
      }

      /* Create a new CUPS queue or modify the existing queue */
      request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
      /* Default user */
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		   "requesting-user-name", NULL, cupsUser());
      /* Queue should be enabled ... */
      ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state",
		    IPP_PRINTER_IDLE);
      /* ... and accepting jobs */
      ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);
      num_options = 0;
      options = NULL;
      /* Device URI: ipp(s)://<remote host>:631/printers/<remote queue>
         OR          implicitclass:<queue name> */
      num_options = cupsAddOption("device-uri", device_uri,
				  num_options, &options);
      /* Option cups-browsed=true, marking that we have created this queue */
      num_options = cupsAddOption(CUPS_BROWSED_MARK "-default", "true",
				  num_options, &options);
      /* Description */
      num_options = cupsAddOption("printer-info", p->info,
				  num_options, &options);
      /* Location */
      num_options = cupsAddOption("printer-location", p->location,
				  num_options, &options);
      /* Default option settings from printer entry */
      for (i = 0; i < p->num_options; i ++)
	if (strcasecmp(p->options[i].name, "printer-is-shared"))
	  num_options = cupsAddOption(strdup(p->options[i].name),
				      strdup(p->options[i].value),
				      num_options, &options);
      /* Encode option list into IPP attributes */
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
      /* Do it */
      if (p->ppd) {
	debug_printf("Non-raw queue %s with PPD file: %s\n", p->queue_name, p->ppd);
	ippDelete(cupsDoFileRequest(http, request, "/admin/", p->ppd));
	want_raw = 0;
	unlink(p->ppd);
	free(p->ppd);
	p->ppd = NULL;
      } else if (p->ifscript) {
	debug_printf("Non-raw queue %s with interface script: %s\n", p->queue_name, p->ifscript);
	ippDelete(cupsDoFileRequest(http, request, "/admin/", p->ifscript));
	want_raw = 0;
	unlink(p->ifscript);
	free(p->ifscript);
	p->ifscript = NULL;
      } else {
	if (p->netprinter == 0) {
	  debug_printf("Raw queue %s\n", p->queue_name);
	  want_raw = 1;
	} else {
	  debug_printf("Queue %s keeping its current PPD file/interface script\n", p->queue_name);
	  want_raw = 0;
	}	  
	ippDelete(cupsDoRequest(http, request, "/admin/"));
      }
      cupsFreeOptions(num_options, options);
      if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
	debug_printf("Unable to create/modify CUPS queue (%s)!\n",
		     cupsLastErrorString());
	p->timeout = current_time + TIMEOUT_RETRY;
	p->no_autosave = 0;
	break;
      }

      /* Do not share a queue which serves only to point to a remote CUPS
	 printer

	 We do this in a seperate IPP request as on newer CUPS versions we
         get an error when changing the printer-is-shared bit on a queue
         pointing to a remote CUPS printer, this way we assure all other
	 settings be applied amd when setting the printer-is-shared to
         false amd this errors, we can safely ignore the error as on queues
	 pointing to remote CUPS printers the bit is set to false by default
	 (these printers are never shared)

	 If our printer is an IPP network printer and not a CUPS queue, we
         keep track of whether the user has changed the printer-is-shared
         bit and recover this setting. The default setting for a new
         queue is configurable via the NewIPPPrinterQueuesShared directive
         in cups-browsed.conf */

      request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		   "printer-uri", NULL, uri);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		 "requesting-user-name", NULL, cupsUser());
      num_options = 0;
      options = NULL;
      if (p->netprinter == 1 &&
	  (val = cupsGetOption("printer-is-shared", p->num_options,
			       p->options)) != NULL) {
	num_options = cupsAddOption("printer-is-shared", val,
				    num_options, &options);
	debug_printf("Setting printer-is-shared bit to %s.\n", val);
      } else if (p->netprinter == 1 && NewIPPPrinterQueuesShared) { 
	num_options = cupsAddOption("printer-is-shared", "true",
				    num_options, &options);
	debug_printf("Setting printer-is-shared bit.\n");
      } else {
	num_options = cupsAddOption("printer-is-shared", "false",
				    num_options, &options);
	debug_printf("Unsetting printer-is-shared bit.\n");
      }
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
      ippDelete(cupsDoRequest(http, request, "/admin/"));
      cupsFreeOptions(num_options, options);
      if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE)
	debug_printf("Unable to set printer-is-shared bit to false (%s)!\n",
		     cupsLastErrorString());

      /* If we are about to create a raw queue or turn a non-raw queue
	 into a raw one, we apply the "ppd-name=raw" option to remove any
	 existing PPD file assigned to the queue.

         Also here we do a separate IPP request as it errors in some
         cases. */
      if (want_raw) {
	debug_printf("Removing local PPD file for printer %s\n", p->queue_name);
	request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		     "printer-uri", NULL, uri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		     "requesting-user-name", NULL, cupsUser());
	num_options = 0;
	options = NULL;
	num_options = cupsAddOption("ppd-name", "raw",
				    num_options, &options);
	cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
	cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
	ippDelete(cupsDoRequest(http, request, "/admin/"));
	cupsFreeOptions(num_options, options);
	if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE)
	  debug_printf("Unable to remove PPD file from the print queue (%s)!\n",
		       cupsLastErrorString());
      }

      /* If this queue was the default printer in its previous life, make
	 it the default printer again. */
      queue_creation_handle_default(p->queue_name);

      /* If cups-browsed or a failed backend has disabled this
	 queue, re-enable it. */
      if ((disabled_str = is_disabled(p->queue_name, "cups-browsed")) != NULL) {
	enable_printer(p->queue_name);
	free(disabled_str);
      } else if ((disabled_str = is_disabled(p->queue_name, "Printer stopped due to backend errors")) != NULL) {
	enable_printer(p->queue_name);
	free(disabled_str);
      }

      p->status = STATUS_CONFIRMED;
      if (p->is_legacy) {
	p->timeout = time(NULL) + BrowseTimeout;
	debug_printf("starting BrowseTimeout timer for %s (%ds)\n",
		     p->queue_name, BrowseTimeout);
      } else
	p->timeout = (time_t) -1;

      p->no_autosave = 0;
      break;

    case STATUS_CONFIRMED:
      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      if (p->is_legacy) {
	/* Remove a queue based on a legacy CUPS broadcast when the
	   broadcast timeout expires without a new broadcast of this
	   queue from the server */
	remove_printer_entry(p);
      } else
	p->timeout = (time_t) -1;

      break;

    }
  }
  log_all_printers();

  free(r);
  
  if (in_shutdown == 0)
    recheck_timer ();

  /* Don't run this callback again */
  return FALSE;
}

static void
recheck_timer (void)
{
  remote_printer_t *p;
  time_t timeout = (time_t) -1;
  time_t now = time(NULL);

  if (!gmainloop)
    return;

  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p;
       p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (p->timeout == (time_t) -1)
      continue;
    else if (now > p->timeout) {
      timeout = 0;
      break;
    } else if (timeout == (time_t) -1 || p->timeout - now < timeout)
      timeout = p->timeout - now;

  if (queues_timer_id)
    g_source_remove (queues_timer_id);

  if (timeout != (time_t) -1) {
    debug_printf("checking queues in %ds\n", timeout);
    queues_timer_id = g_timeout_add_seconds (timeout, update_cups_queues, NULL);
  } else {
    debug_printf("listening\n");
    queues_timer_id = 0;
  }
}

static gboolean
matched_filters (const char *queue_name,
		 const char *host,
		 uint16_t port,
		 const char *service_name,
		 const char *domain,
		 void *txt) {
  browse_filter_t *filter;
  const char *property = NULL;
  char buf[10];
#ifdef HAVE_AVAHI
  AvahiStringList *entry = NULL;
  char *key = NULL, *value = NULL;
#endif /* HAVE_AVAHI */

  debug_printf("Matching printer \"%s\" with properties Host = \"%s\", Port = %d, Service Name = \"%s\", Domain = \"%s\" with the BrowseFilter lines in cups-browsed.conf\n", queue_name, host, port, service_name, domain);
  /* Go through all BrowseFilter lines and stop if one line does not match,
     rejecting this printer */
  for (filter = cupsArrayFirst (browsefilter);
       filter;
       filter = cupsArrayNext (browsefilter)) {
    debug_printf("Matching with line \"BrowseFilter %s%s%s %s\"",
		 (filter->sense == FILTER_NOT_MATCH ? "NOT " : ""),
		 (filter->regexp && !filter->cregexp ? "EXACT " : ""),
		 filter->field, (filter->regexp ? filter->regexp : ""));
#ifdef HAVE_AVAHI
    /* Go through the TXT record to see whether this rule applies to a field
       in there */
    if (txt) {
      entry = avahi_string_list_find((AvahiStringList *)txt, filter->field);
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key) {
	  debug_printf(", TXT record entry: %s = %s",
		       key, (value ? value : ""));
	  if (filter->regexp) {
	    /* match regexp */
	    if (!value)
	      value = "";
	    if ((filter->cregexp &&
		 regexec(filter->cregexp, value, 0, NULL, 0) == 0) ||
		(!filter->cregexp && !strcasecmp(filter->regexp, value))) {
	      avahi_free(key);
	      avahi_free(value);
	      if (filter->sense == FILTER_NOT_MATCH)
		goto filter_failed;
	    } else {
	      avahi_free(key);
	      avahi_free(value);
	      if (filter->sense == FILTER_MATCH)
		goto filter_failed;
	    }	      
	  } else {
	    /* match boolean value */
	    if (filter->sense == FILTER_MATCH) {
 	      if (!value || strcasecmp(value, "T")) {
		avahi_free(key);
		avahi_free(value);
		goto filter_failed;
	      }
	    } else {
 	      if (value && !strcasecmp(value, "T")) {
		avahi_free(key);
		avahi_free(value);
		goto filter_failed;
	      }
	    }
	  }
	}
	avahi_free(key);
	avahi_free(value);
	goto filter_matched;
      }
    }
#endif /* HAVE_AVAHI */

    /* Does one of the properties outside the TXT record match? */
    property = buf;
    buf[0] = '\0';
    if (!strcasecmp(filter->field, "Name") ||
	!strcasecmp(filter->field, "Printer") ||
	!strcasecmp(filter->field, "PrinterName") ||
	!strcasecmp(filter->field, "Queue") ||
	!strcasecmp(filter->field, "QueueName")) {
      if (queue_name)
	property = queue_name;
    } else if (!strcasecmp(filter->field, "Host") ||
	       !strcasecmp(filter->field, "HostName") ||
	       !strcasecmp(filter->field, "RemoteHost") ||
	       !strcasecmp(filter->field, "RemoteHostName") ||
	       !strcasecmp(filter->field, "Server") ||
	       !strcasecmp(filter->field, "ServerName")) {
      if (host)
	property = host;
    } else if (!strcasecmp(filter->field, "Port")) {
      if (port)
	snprintf(buf, sizeof(buf), "%d", port);
    } else if (!strcasecmp(filter->field, "Service") ||
	       !strcasecmp(filter->field, "ServiceName")) {
      if (service_name)
	property = service_name;
    } else if (!strcasecmp(filter->field, "Domain")) {
      if (domain)
	property = domain;
    } else
      property = NULL;
    if (property) {
      if (!filter->regexp)
	filter->regexp = "";
      if ((filter->cregexp &&
	   regexec(filter->cregexp, property, 0, NULL, 0) == 0) ||
	  (!filter->cregexp && !strcasecmp(filter->regexp, property))) {
	if (filter->sense == FILTER_NOT_MATCH)
	  goto filter_failed;
      } else {
	if (filter->sense == FILTER_MATCH)
	  goto filter_failed;
      }
      goto filter_matched;
    }

    debug_printf(": Field not found --> SKIPPED\n");
    continue;

  filter_matched:
    debug_printf(" --> MATCHED\n");
  }

  /* All BrowseFilter lines matching, accept this printer */
  debug_printf("All BrowseFilter lines matched or skipped, accepting printer %s\n",
	       queue_name);
  return TRUE;

 filter_failed:
  debug_printf(" --> FAILED\n");
  debug_printf("One BrowseFilter line did not match, ignoring printer %s\n",
	       queue_name);
  return FALSE;
}

static remote_printer_t *
examine_discovered_printer_record(const char *host,
				  const char *ip,
				  uint16_t port,
				  char *resource,
				  const char *service_name,
				  const char *location,
				  const char *info,
				  const char *type,
				  const char *domain,
				  void *txt) {

  char uri[HTTP_MAX_URI];
  char *queue_name = NULL, *remote_host = NULL, *pdl = NULL,
    *make_model = NULL;
  int color = 1, duplex = 1;
#ifdef HAVE_AVAHI
  char *fields[] = { "product", "usb_MDL", "ty", NULL }, **f;
  AvahiStringList *entry = NULL;
  char *key = NULL, *value = NULL;
  char *note_value = NULL;
#endif /* HAVE_AVAHI */
  cluster_t *cluster = NULL;
  char *member = NULL, *str = NULL;
  remote_printer_t *p = NULL;
  local_printer_t *local_printer = NULL;
  char *backup_queue_name = NULL, *local_queue_name = NULL,
       *local_queue_name_lower = NULL;
  int is_cups_queue;
  
  if (!host || !resource || !service_name || !location || !info || !type ||
      !domain) {
    debug_printf("ERROR: examine_discovered_printer_record(): Input value missing!\n");
    return NULL;
  }


  is_cups_queue = 0;
  memset(uri, 0, sizeof(uri));

  /* Determine the device URI of the remote printer */
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri) - 1,
		   (strcasestr(type, "_ipps") ? "ipps" : "ipp"), NULL,
		   (ip != NULL ? ip : host), port, "/%s", resource);
  /* Find the remote host name.
   * Used in constructing backup_queue_name, so need to sanitize.
   * strdup() is called inside remove_bad_chars() and result is free()-able.
   */
  remote_host = remove_bad_chars(host, 1);
  /*hl = strlen(remote_host);
  if (hl > 6 && !strcasecmp(remote_host + hl - 6, ".local"))
    remote_host[hl - 6] = '\0';
  if (hl > 7 && !strcasecmp(remote_host + hl - 7, ".local."))
  remote_host[hl - 7] = '\0';*/

#ifdef HAVE_AVAHI
  if (txt) {
    for (f = fields; *f; f ++) {
      entry = avahi_string_list_find((AvahiStringList *)txt, *f);
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, *f) && strlen(value) >= 3) {
	  if (!strcasecmp(key, "product")) {
	    make_model = strdup(value + 1);
	    make_model[strlen(make_model) - 1] = '\0'; 
	  } else
	    make_model = strdup(value);
	  avahi_free(key);
	  avahi_free(value);
	  break;
	}
	avahi_free(key);
	avahi_free(value);
      }
    }
  }
#endif /* HAVE_AVAHI */
  /* Check by the resource whether the discovered printer is a CUPS queue */
  if (!strncasecmp(resource, "printers/", 9) ||
      !strncasecmp(resource, "classes/", 8)) {
    /* This is a remote CUPS queue or class */
    is_cups_queue = 1;
    debug_printf("Found CUPS queue/class: %s on host %s.\n",
		 strchr(resource, '/') + 1, remote_host);
#ifdef HAVE_AVAHI
    /* If the remote queue has a PPD file, the "product" field of the
       TXT record is populated. If it has no PPD file the remote queue
       is a raw queue and so we do not know enough about the printer
       behind it for auto-creating a local queue pointing to it. */
    int raw_queue = 0;
    if (txt) {
      entry = avahi_string_list_find((AvahiStringList *)txt, "product");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (!key || !value || strcasecmp(key, "product") || value[0] != '(' ||
	    value[strlen(value) - 1] != ')') {
	  raw_queue = 1;
	}
	avahi_free(key);
	avahi_free(value);
      } else
	raw_queue = 1;
    } else if (domain && domain[0] != '\0')
      raw_queue = 1;
    if (raw_queue && CreateRemoteRawPrinterQueues == 0) {
      /* The remote CUPS queue is raw, ignore it */
      debug_printf("Remote DNS-SD-advertised CUPS queue %s on host %s is raw, ignored.\n",
		   strchr(resource, '/') + 1, remote_host);
      free (remote_host);
      if (make_model) free (make_model);
      return NULL;
    }
#endif /* HAVE_AVAHI */
    /* Determine the queue name */
    if (LocalQueueNamingRemoteCUPS == LOCAL_QUEUE_NAMING_MAKE_MODEL &&
	make_model)
      /* Works only with DNS-SD-discovered queues as otherwise we have no
	 make/model info */
      queue_name = remove_bad_chars(make_model, 0);
    else if (LocalQueueNamingRemoteCUPS == LOCAL_QUEUE_NAMING_REMOTE_NAME)
      /* Not directly used in script generation input later, but taken from
	 packet, so better safe than sorry. (consider second loop with
	 backup_queue_name) */
      queue_name = remove_bad_chars(strchr(resource, '/') + 1, 0);
    else
      /* Convert DNS-SD service name into a CUPS queue name exactly
	 as CUPS would do it, to override CUPS' own temporary queue
	 generation mechanism */
      queue_name = remove_bad_chars(service_name, 2);
  } else {
    /* This is an IPP-based network printer */
    is_cups_queue = 0;
    /* Determine the queue name */
    if (LocalQueueNamingIPPPrinter == LOCAL_QUEUE_NAMING_MAKE_MODEL &&
	make_model)
      /* Works only if we actually have make/model info in the DNS-SD record*/
      queue_name = remove_bad_chars(make_model, 0);
    else
      /* Convert DNS-SD service name into a CUPS queue name exactly
	 as CUPS would do it, to override CUPS' own temporary queue
	 generation mechanism */
      queue_name = remove_bad_chars(service_name, 2);
#ifdef HAVE_AVAHI
    if (txt) {
      /* Find out which PDLs the printer understands */
      entry = avahi_string_list_find((AvahiStringList *)txt, "pdl");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, "pdl") && strlen(value) >= 3) {
	  pdl = remove_bad_chars(value, 1);
	}
	avahi_free(key);
	avahi_free(value);
      }
      /* Find out if we have a color printer */
      entry = avahi_string_list_find((AvahiStringList *)txt, "Color");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, "Color")) {
	  if (!strcasecmp(value, "T")) color = 1;
	  if (!strcasecmp(value, "F")) color = 0;
	}
	avahi_free(key);
	avahi_free(value);
      }
      /* Find out if we have a duplex printer */
      entry = avahi_string_list_find((AvahiStringList *)txt, "Duplex");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, "Duplex")) {
	  if (!strcasecmp(value, "T")) duplex = 1;
	  if (!strcasecmp(value, "F")) duplex = 0;
	}
	avahi_free(key);
	avahi_free(value);
      }
    }
#endif /* HAVE_AVAHI */
  }
  /* Extract location from DNS-SD TXT record's "note" field */
#ifdef HAVE_AVAHI
  if (!location) {
    if (txt) {
      entry = avahi_string_list_find((AvahiStringList *)txt, "note");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &note_value, NULL);
	if (key && note_value && !strcasecmp(key, "note")) {
	  debug_printf("examine_discovered_printer_record: TXT.note: |%s|\n", note_value); /* !! */
	  location = note_value;
	}
        avahi_free(key);
        /* don't avahi_free(note_value) here! */
      }
    }
    if (!location)
      location = "";
  }
  /* A NULL location is only passed in from resolve_callback(), which is HAVE_AVAHI */
#endif /* HAVE_AVAHI */
  /* Check if there exists already a CUPS queue with the
     requested name Try name@host in such a case and if
     this is also taken, ignore the printer */
  if ((backup_queue_name = malloc((strlen(queue_name) +
				   strlen(remote_host) + 2) *
				  sizeof(char))) == NULL) {
    debug_printf("ERROR: Unable to allocate memory.\n");
    exit(1);
  }
  sprintf(backup_queue_name, "%s@%s", queue_name, remote_host);

  /* Get available CUPS queues */
  update_local_printers ();

  /* We skip trying to use the queue name purely derived from the
     remote CUPS queue name or make and model for remote CUPS queues
     when automatic clustering of remote cUPS queues is turned off,
     to directly create queues with names containing the server name
     to avoid name clashes and with this remote queues skipped by
     cups-browsed. */
  if (!is_cups_queue ||
      AutoClustering == 1 ||
      LocalQueueNamingRemoteCUPS == LOCAL_QUEUE_NAMING_DNSSD) {
    local_queue_name = queue_name;
    /* Is there a local queue with the name of the remote queue? */
    local_queue_name_lower = g_ascii_strdown(local_queue_name, -1);
    local_printer = g_hash_table_lookup (local_printers,
				       local_queue_name_lower);
    free(local_queue_name_lower);
  }
  /* Use the originally chosen queue name plus the server name if
     the original name is already taken or if we had skipped using
     it. To decide on whether the queue name is already taken, only
     consider CUPS queues not created by us */
  if ((is_cups_queue &&
       AutoClustering == 0 &&
       LocalQueueNamingRemoteCUPS != LOCAL_QUEUE_NAMING_DNSSD) ||
      (local_printer && !local_printer->cups_browsed_controlled)) {
    /* Found local queue with same name as remote queue */
    /* Is there a local queue with the name <queue>@<host>? */
    local_queue_name = backup_queue_name;
    debug_printf("%s already taken, using fallback name: %s\n",
		 queue_name, local_queue_name);
    local_queue_name_lower = g_ascii_strdown(local_queue_name, -1);
    local_printer = g_hash_table_lookup (local_printers,
					 local_queue_name_lower);
    free(local_queue_name_lower);
    if (local_printer && !local_printer->cups_browsed_controlled) {
      /* Found also a local queue with name <queue>@<host>, so
	 ignore this remote printer */
      debug_printf("%s also taken, printer ignored.\n",
		   local_queue_name);
      goto fail;
    }
  }

  /* If we only want to create queues for printers for which CUPS does
     not already auto-create queues, we check here whether we can skip
     this printer */
  if (OnlyUnsupportedByCUPS) {
    if (g_hash_table_find (cups_supported_remote_printers,
			   local_printer_service_name_matches,
			   (gpointer *)service_name)) {
      /* Found a DNS-SD-discovered CUPS-supported printer whose URI matches
	 our discovered printer */
      debug_printf("Printer %s (DNS-SD service name \"%s\") does not need to be covered by us as it is already supported by CUPS, skipping.\n",
		   local_queue_name, service_name);
      goto fail;
    }
  }

  if (is_cups_queue) {
    /* Check whether our new printer matches one of the user-defined
       printer clusters */
    for (cluster = cupsArrayFirst(clusters);
	 cluster;
	 cluster = cupsArrayNext(clusters)) {
      for (member = cupsArrayFirst(cluster->members);
	   member;
	   member = cupsArrayNext(cluster->members)) {
	/* Match remote CUPS queue name */
	if ((str = strrchr(resource, '/')) != NULL && strlen(str) > 1) {
	  str = remove_bad_chars(str + 1, 2);
	  if (strcasecmp(member, str) == 0) /* Match */
	    break;
	  free(str);
	}
	/* Match make and model */
	if (make_model) {
	  str = remove_bad_chars(make_model, 2);
	  if (strcasecmp(member, str) == 0) /* Match */
	    break;
	  free(str);
	}
	/* Match DNS-SD service name */
	if (service_name) {
	  str = remove_bad_chars(service_name, 2);
	  if (strcasecmp(member, str) == 0) /* Match */
	    break;
	  free(str);
	}	  
      }
      if (member)
	break;
    }
    if (cluster) {
      local_queue_name = cluster->local_queue_name;
      is_cups_queue = 2;
      free(str);
    } else if (AutoClustering) {
      /* If we do automatic clustering by matching queue names, do not
	 add a queue to a manually defined cluster because it matches
	 the cluster's local queue name. Manually defined clusters can
	 only be joined by printers which match one of the cluster's
	 member names */
      for (cluster = cupsArrayFirst(clusters);
	   cluster;
	   cluster = cupsArrayNext(clusters)) {
	if (strcasecmp(local_queue_name, cluster->local_queue_name) == 0) {
	  debug_printf("We have already a manually defined printer cluster with the name %s. Automatic clustering does not add this printer to this cluster as it does not match any of the cluster's member names. Skipping this printer.\n", local_queue_name);
	  debug_printf("In cups-browsed.conf try \"LocalQueueNamingRemoteCUPS DNS-SD\" or give another name to your manually defined cluster (\"Cluster\" directive) to avoid name clashes.\n");
	  goto fail;
	}
      }
    }
  }

  if (!matched_filters (local_queue_name, remote_host, port, service_name, domain,
			txt)) {
    debug_printf("Printer %s does not match BrowseFilter lines in cups-browsed.conf, printer ignored.\n",
		 local_queue_name);
    goto fail;
  }

  /* Check if we have already created a queue for the discovered
     printer */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!strcasecmp(p->queue_name, local_queue_name) &&
	(p->host[0] == '\0' ||
	 p->status == STATUS_UNCONFIRMED ||
	 p->status == STATUS_DISAPPEARED ||
	 (!strcasecmp(p->host, remote_host) && p->port == port &&
	  strlen(p->uri) - strlen(resource) > 0 &&
	  !strcasecmp(p->uri + strlen(p->uri) - strlen(resource), resource))))
      break;

  /* Is there a local queue with the same URI as the remote queue? */
  if (!p && g_hash_table_find (local_printers,
			       local_printer_has_uri,
			       uri)) {
    /* Found a local queue with the same URI as our discovered printer
       would get, so ignore this remote printer */
    debug_printf("Printer with URI %s already exists, printer ignored.\n",
		 uri);
    goto fail;
  }

  if (p) {
    debug_printf("Entry for %s (URI: %s) already exists.\n",
		 p->queue_name, p->uri);
    /* We have already created a local queue, check whether the
       discovered service allows us to upgrade the queue to IPPS
       or whether the URI part after ipp(s):// has changed, or
       whether the discovered queue is discovered via DNS-SD
       having more info in contrary to the existing being
       discovered by legacy CUPS or LDAP */
    if ((strcasestr(type, "_ipps") &&
	 !strncasecmp(p->uri, "ipp:", 4)) ||
	strcasecmp(strchr(p->uri, ':'), strchr(uri, ':')) ||
	((p->domain == NULL || p->domain[0] == '\0') &&
	 domain != NULL && domain[0] != '\0' &&
	 (p->type == NULL || p->type[0] == '\0') &&
	 type != NULL && type[0] != '\0')) {

      /* Schedule local queue for upgrade to ipps: or for URI change */
      if (strcasestr(type, "_ipps") &&
	  !strncasecmp(p->uri, "ipp:", 4))
	debug_printf("Upgrading printer %s (Host: %s, Port :%d) to IPPS. New URI: %s\n",
		     p->queue_name, remote_host, port, uri);
      if (strcasecmp(strchr(p->uri, ':'), strchr(uri, ':')))
	debug_printf("Changing URI of printer %s (Host: %s, Port: %d) to %s.\n",
		     p->queue_name, remote_host, port, uri);
      if ((p->domain == NULL || p->domain[0] == '\0') &&
	  domain != NULL && domain[0] != '\0' &&
	  (p->type == NULL || p->type[0] == '\0') &&
	  type != NULL && type[0] != '\0') {
	debug_printf("Discovered printer %s (Host: %s, Port: %d, URI: %s) by DNS-SD now.\n",
		     p->queue_name, remote_host, port, uri);
	if (p->is_legacy) {
	  p->is_legacy = 0;
	  if (p->status == STATUS_CONFIRMED)
	    p->timeout = (time_t) -1;
	}
      }
      free(p->location);
      free(p->info);
      free(p->uri);
      free(p->host);
      free(p->ip);
      free(p->service_name);
      free(p->type);
      free(p->domain);
      p->location = strdup(location);
      p->info = strdup(info);
      p->uri = strdup(uri);
      p->status = STATUS_TO_BE_CREATED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
      p->host = strdup(remote_host);
      p->ip = (ip != NULL ? strdup(ip) : NULL);
      p->port = port;
      p->service_name = strdup(service_name);
      p->type = strdup(type);
      p->domain = strdup(domain);

    }

    /* Mark queue entry as confirmed if the entry
       is unconfirmed */
    if (p->status == STATUS_UNCONFIRMED ||
	p->status == STATUS_DISAPPEARED) {
      debug_printf("Marking entry for %s (URI: %s) as confirmed.\n",
		   p->queue_name, p->uri);
      p->status = STATUS_CONFIRMED;
      if (p->is_legacy) {
	p->timeout = time(NULL) + BrowseTimeout;
	debug_printf("starting BrowseTimeout timer for %s (%ds)\n",
		     p->queue_name, BrowseTimeout);
      } else
	p->timeout = (time_t) -1;
      /* If this queue was the default printer in its previous life, make
	 it the default printer again. */
      queue_creation_handle_default(p->queue_name);
      /* If this queue is disabled, re-enable it. */
      enable_printer(p->queue_name);
      /* Record the options, to record any changes which happened
	 while cups-browsed was not running */
      record_printer_options(p->queue_name);
    }

    if (p->location[0] == '\0') {
      free (p->location);
      p->location = strdup(location);
    }
    if (p->info[0] == '\0') {
      free (p->info);
      p->info = strdup(info);
    }
    if (p->host[0] == '\0') {
      free (p->host);
      p->host = strdup(remote_host);
    }
    if (p->ip == NULL || p->ip[0] == '\0') {
      if (p->ip) free (p->ip);
      p->ip = (ip != NULL ? strdup(ip) : NULL);
    }
    if (p->port == 0)
      p->port = port;
    if (p->service_name[0] == '\0' && service_name) {
      free (p->service_name);
      p->service_name = strdup(service_name);
    }
    if (p->type[0] == '\0' && type) {
      free (p->type);
      p->type = strdup(type);
    }
    if (p->domain[0] == '\0' && domain) {
      free (p->domain);
      p->domain = strdup(domain);
    }
    p->netprinter = is_cups_queue ? 0 : 1;
  } else {

    /* We need to create a local queue pointing to the
       discovered printer */
    p = create_remote_printer_entry (local_queue_name, location, info, uri,
				     remote_host, ip, port,
				     service_name ? service_name : "", type,
				     domain, pdl, color, duplex, make_model,
				     is_cups_queue);
  }

 fail:
  free (backup_queue_name);
  free (remote_host);
  free (pdl);
  free (queue_name);
  free (make_model);
#ifdef HAVE_AVAHI
  if (note_value) avahi_free(note_value);
#endif /* HAVE_AVAHI */

  if (p)
    debug_printf("DNS-SD IDs: Service name: \"%s\", "
		 "Service type: \"%s\", Domain: \"%s\"\n",
		 p->service_name, p->type, p->domain);

  return p;
}

static gboolean
allowed (struct sockaddr *srcaddr)
{
  allow_t *allow;
  int i;
  gboolean server_allowed;
  allow_sense_t sense;

  if (browse_order == ORDER_DENY_ALLOW)
    /* BrowseOrder Deny,Allow: Allow server, then apply BrowseDeny lines,
       after that BrowseAllow lines */
    server_allowed = TRUE;
  else
    /* BrowseOrder Allow,Deny: Deny server, then apply BrowseAllow lines,
       after that BrowseDeny lines */
    server_allowed = FALSE;

  for (i = 0; i <= 1; i ++) {
    if (browse_order == ORDER_DENY_ALLOW)
      /* Treat BrowseDeny lines first, then BrowseAllow lines */
      sense = (i == 0 ? ALLOW_DENY : ALLOW_ALLOW);
    else
      /* Treat BrowseAllow lines first, then BrowseDeny lines */
      sense = (i == 0 ? ALLOW_ALLOW : ALLOW_DENY);

    if (server_allowed == (sense == ALLOW_ALLOW ? TRUE : FALSE))
      continue;

    if (browseallow_all && sense == ALLOW_ALLOW) {
      server_allowed = TRUE;
      continue;
    }
    if (browsedeny_all && sense == ALLOW_DENY) {
      server_allowed = FALSE;
      continue;
    }

    for (allow = cupsArrayFirst (browseallow);
	 allow;
	 allow = cupsArrayNext (browseallow)) {
      if (allow->sense != sense)
	continue;

      switch (allow->type) {
      case ALLOW_INVALID:
	break;

      case ALLOW_IP:
	switch (srcaddr->sa_family) {
	case AF_INET:
	  if (((struct sockaddr_in *) srcaddr)->sin_addr.s_addr ==
	      allow->addr.ipv4.sin_addr.s_addr) {
	    server_allowed = (sense == ALLOW_ALLOW ? TRUE : FALSE);
	    goto match;
	  }
	  break;

	case AF_INET6:
	  if (!memcmp (&((struct sockaddr_in6 *) srcaddr)->sin6_addr,
		       &allow->addr.ipv6.sin6_addr,
		       sizeof (allow->addr.ipv6.sin6_addr))) {
	    server_allowed = (sense == ALLOW_ALLOW ? TRUE : FALSE);
	    goto match;
	  }
	  break;
	}
	break;

      case ALLOW_NET:
	switch (srcaddr->sa_family) {
	  struct sockaddr_in6 *src6addr;

	case AF_INET:
	  if ((((struct sockaddr_in *) srcaddr)->sin_addr.s_addr &
	       allow->mask.ipv4.sin_addr.s_addr) ==
	      allow->addr.ipv4.sin_addr.s_addr) {
	    server_allowed = (sense == ALLOW_ALLOW ? TRUE : FALSE);
	    goto match;
	  }
	  break;

	case AF_INET6:
	  src6addr = (struct sockaddr_in6 *) srcaddr;
	  if (((src6addr->sin6_addr.s6_addr[0] &
		allow->mask.ipv6.sin6_addr.s6_addr[0]) ==
	       allow->addr.ipv6.sin6_addr.s6_addr[0]) &&
	      ((src6addr->sin6_addr.s6_addr[1] &
		allow->mask.ipv6.sin6_addr.s6_addr[1]) ==
	       allow->addr.ipv6.sin6_addr.s6_addr[1]) &&
	      ((src6addr->sin6_addr.s6_addr[2] &
		allow->mask.ipv6.sin6_addr.s6_addr[2]) ==
	       allow->addr.ipv6.sin6_addr.s6_addr[2]) &&
	      ((src6addr->sin6_addr.s6_addr[3] &
		allow->mask.ipv6.sin6_addr.s6_addr[3]) ==
	       allow->addr.ipv6.sin6_addr.s6_addr[3])) {
	    server_allowed = (sense == ALLOW_ALLOW ? TRUE : FALSE);
	    goto match;
	  }
	  break;
	}
      }
    }
  match:
    continue;
  }

  return server_allowed;
}

#ifdef HAVE_AVAHI
static void resolve_callback(
  AvahiServiceResolver *r,
  AvahiIfIndex interface,
  AvahiProtocol protocol,
  AvahiResolverEvent event,
  const char *name,
  const char *type,
  const char *domain,
  const char *host_name,
  const AvahiAddress *address,
  uint16_t port,
  AvahiStringList *txt,
  AvahiLookupResultFlags flags,
  AVAHI_GCC_UNUSED void* userdata) {
  char ifname[IF_NAMESIZE];

  debug_printf("resolve_callback() in THREAD %ld\n", pthread_self());

  if (r == NULL || name == NULL || type == NULL || domain == NULL)
    return;

  /* Ignore local queues on the port of the cupsd we are serving for */
  if (flags & AVAHI_LOOKUP_RESULT_LOCAL && port == ippPort())
    goto ignore;

  /* Get the interface name */
  if (!if_indextoname(interface, ifname)) {
    debug_printf("Unable to find interface name for interface %d: %s\n",
		 interface, strerror(errno));
    strncpy(ifname, "Unknown", sizeof(ifname));
  }

  /* Called whenever a service has been resolved successfully or timed out */

  switch (event) {

  /* Resolver error */
  case AVAHI_RESOLVER_FAILURE:
    debug_printf("Avahi-Resolver: Failed to resolve service '%s' of type '%s' in domain '%s' on interface '%s' (%s): %s\n",
		 name, type, domain, ifname,
		 (address ?
		  (address->proto == AVAHI_PROTO_INET ? "IPv4" :
		   address->proto == AVAHI_PROTO_INET6 ? "IPv6" :
		   "IPv4/IPv6 Unknown") :
		  "IPv4/IPv6 Unknown"),
		 avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
    break;

  /* New remote printer found */
  case AVAHI_RESOLVER_FOUND: {
    AvahiStringList *rp_entry, *adminurl_entry;
    char *rp_key, *rp_value, *adminurl_key, *adminurl_value;

    debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s' on interface '%s' (%s).\n",
		 name, type, domain, ifname,
		 (address ?
		  (address->proto == AVAHI_PROTO_INET ? "IPv4" :
		   address->proto == AVAHI_PROTO_INET6 ? "IPv6" :
		   "IPv4/IPv6 Unknown") :
		  "IPv4/IPv6 Unknown"));

    /* Ignore if terminated (by SIGTERM) */
    if (terminating) {
      debug_printf("Avahi Resolver: Ignoring because cups-browsed is terminating.\n");
      break;
    }

    if (txt && (rp_entry = avahi_string_list_find(txt, "rp")))
      avahi_string_list_get_pair(rp_entry, &rp_key, &rp_value, NULL);
    else {
      rp_key = strdup("rp");
      rp_value = strdup("");
    }
    if (txt && (adminurl_entry = avahi_string_list_find(txt, "adminurl")))
      avahi_string_list_get_pair(adminurl_entry, &adminurl_key,
				 &adminurl_value, NULL);
    else {
      adminurl_key = strdup("adminurl");
      if (host_name && (adminurl_value = malloc(strlen(host_name) + 8)) != NULL)
	sprintf(adminurl_value, "http://%s", host_name);
      else
	adminurl_value = strdup("");
    }

    if (CreateIPPPrinterQueues == IPP_PRINTERS_LOCAL_ONLY &&
	strcasecmp(ifname, "lo")) {
      debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s' skipped, not a local service.\n",
		   name, type, domain);
      goto clean_up;
    }

    if (txt && rp_key && rp_value && adminurl_key && adminurl_value &&
	!strcasecmp(rp_key, "rp") && !strcasecmp(adminurl_key, "adminurl")) {
      char *p, instance[64];
      /* Extract instance from DNSSD service name (to serve as info field) */
      p = strstr(name, " @ ");
      if (p) {
	int n;
	n = p - name;
	if (n >= sizeof(instance))
	  n = sizeof(instance) - 1;
	strncpy(instance, name, n);
	instance[n] = '\0';
	debug_printf("Avahi-Resolver: instance: |%s|\n", instance); /* !! */
      } else {
	instance[0] = '\0';
      }
      /* Determine the remote printer's IP */
      if (IPBasedDeviceURIs != IP_BASED_URIS_NO || !strcasecmp(ifname, "lo") ||
	  (!browseallow_all && cupsArrayCount(browseallow) > 0)) {
	struct sockaddr saddr;
	struct sockaddr *addr = &saddr;
	char *addrstr;
	int addrlen;
	int addrfound = 0;
	if ((addrstr = calloc(256, sizeof(char))) == NULL) {
	  debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s' skipped, could not allocate memory to determine IP address.\n",
		       name, type, domain);
	  goto clean_up;
	}
	if (address &&
	    address->proto == AVAHI_PROTO_INET &&
	    IPBasedDeviceURIs != IP_BASED_URIS_IPV6_ONLY) {
	  avahi_address_snprint(addrstr, 256, address);
	  addr->sa_family = AF_INET;
	  if (inet_aton(addrstr,
			&((struct sockaddr_in *) addr)->sin_addr) &&
	      allowed(addr))
	    addrfound = 1;
	} else if (address &&
		   address->proto == AVAHI_PROTO_INET6 &&
		   interface != AVAHI_IF_UNSPEC &&
		   IPBasedDeviceURIs != IP_BASED_URIS_IPV4_ONLY) {
	  strncpy(addrstr, "[v1.", 256);
	  avahi_address_snprint(addrstr + 4, 256 - 6, address);
	  addrlen = strlen(addrstr + 4);
	  addr->sa_family = AF_INET6;
	  if (inet_pton(AF_INET6, addrstr + 4,
			&((struct sockaddr_in6 *) addr)->sin6_addr) &&
	      allowed(addr)) {
	    if (!strncasecmp(addrstr + 4, "fe", 2) &&
		(addrstr[6] == '8' || addrstr[6] == '9' ||
		 addrstr[6] == 'A' || addrstr[6] == 'B' ||
		 addrstr[6] == 'a' || addrstr[6] == 'B'))
	      /* Link-local address, needs specification of interface */
	      snprintf(addrstr + addrlen + 4, 256 -
		       addrlen - 4, "%%%s]",
		       ifname);
	    else {
	      addrstr[addrlen + 4] = ']';
	      addrstr[addrlen + 5] = '\0';
	    }
	    addrfound = 1;
	  }
	} else
	  debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s': No IP address information available.\n",
		       name, type, domain);
	if (addrfound == 1) {
	  /* Check remote printer type and create appropriate local queue to
	     point to it */
	  if (IPBasedDeviceURIs != IP_BASED_URIS_NO ||
	      !strcasecmp(ifname, "lo") ||
	      !host_name) {
	    debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s' with IP address %s.\n",
			 name, type, domain, addrstr);
	    examine_discovered_printer_record((strcasecmp(ifname, "lo") &&
					       host_name ? host_name : addrstr),
					      addrstr, port, rp_value, name,
					      "", instance, type, domain,
					      txt);
	  } else
	    examine_discovered_printer_record(host_name, NULL, port, rp_value,
					      name, "", instance, type,
					      domain, txt);
	} else
	  debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s' skipped, could not determine IP address.\n",
		       name, type, domain);
	free(addrstr);
      } else {
	/* Check remote printer type and create appropriate local queue to
	   point to it */
	if (host_name)
	  examine_discovered_printer_record(host_name, NULL, port, rp_value,
					    name, "", instance, type, domain,
					    txt);
	else
	  debug_printf("Avahi Resolver: Service '%s' of type '%s' in domain '%s' skipped, host name not supplied.\n",
		       name, type, domain);
      }
    }

    clean_up:

    /* Clean up */

    if (rp_entry) {
      avahi_free(rp_key);
      avahi_free(rp_value);
    } else {
      free(rp_key);
      free(rp_value);
    }
    if (adminurl_entry) {
      avahi_free(adminurl_key);
      avahi_free(adminurl_value);
    } else {
      free(adminurl_key);
      free(adminurl_value);
    }
    break;
  }
  }

 ignore:
  avahi_service_resolver_free(r);

  if (in_shutdown == 0)
    recheck_timer ();
}

static void browse_callback(
  AvahiServiceBrowser *b,
  AvahiIfIndex interface,
  AvahiProtocol protocol,
  AvahiBrowserEvent event,
  const char *name,
  const char *type,
  const char *domain,
  AvahiLookupResultFlags flags,
  void* userdata) {

  AvahiClient *c = userdata;
  char ifname[IF_NAMESIZE];

  debug_printf("browse_callback() in THREAD %ld\n", pthread_self());

  if (b == NULL)
    return;

  /* Get the interface name */
  if (!if_indextoname(interface, ifname)) {
    debug_printf("Unable to find interface name for interface %d: %s\n",
		 interface, strerror(errno));
    strncpy(ifname, "Unknown", sizeof(ifname));
  }

  /* Called whenever a new services becomes available on the LAN or
     is removed from the LAN */

  switch (event) {

  /* Avahi browser error */
  case AVAHI_BROWSER_FAILURE:

    debug_printf("Avahi Browser: ERROR: %s\n",
		 avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
    g_main_loop_quit(gmainloop);
    g_main_context_wakeup(NULL);
    return;

  /* New service (remote printer) */
  case AVAHI_BROWSER_NEW:

    if (c == NULL || name == NULL || type == NULL || domain == NULL)
      return;

    debug_printf("Avahi Browser: NEW: service '%s' of type '%s' in domain '%s' on interface '%s'\n",
		 name, type, domain, ifname);

    /* Ignore if terminated (by SIGTERM) */
    if (terminating) {
      debug_printf("Avahi Browser: Ignoring because cups-browsed is terminating.\n");
      break;
    }

    /* We ignore the returned resolver object. In the callback
       function we free it. If the server is terminated before
       the callback function is called the server will free
       the resolver for us. */

    if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, c)))
      debug_printf("Failed to resolve service '%s': %s\n",
		   name, avahi_strerror(avahi_client_errno(c)));
    break;

  /* A service (remote printer) has disappeared */
  case AVAHI_BROWSER_REMOVE: {
    remote_printer_t *p;

    if (name == NULL || type == NULL || domain == NULL)
      return;

    debug_printf("Avahi Browser: REMOVE: service '%s' of type '%s' in domain '%s' on interface '%s'\n",
		 name, type, domain, ifname);

    /* Ignore if terminated (by SIGTERM) */
    if (terminating) {
      debug_printf("Avahi Browser: Ignoring because cups-browsed is terminating.\n");
      break;
    }

    /* Check whether we have listed this printer */
    for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (p->status != STATUS_DISAPPEARED &&
	  !strcasecmp(p->service_name, name) &&
	  !strcasecmp(p->type, type) &&
	  !strcasecmp(p->domain, domain))
	break;
    if (p) {
      remove_printer_entry(p);
      debug_printf("DNS-SD IDs: Service name: \"%s\", Service type: \"%s\", Domain: \"%s\"\n",
		   p->service_name, p->type, p->domain);

      if (in_shutdown == 0)
	recheck_timer ();
    }
    break;
  }

  /* All cached Avahi events are treated now */
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    debug_printf("Avahi Browser: %s\n",
		 event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
		 "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
    break;
  }

}

void avahi_browser_shutdown() {
  remote_printer_t *p;

  avahi_present = 0;

  /* Remove all queues which we have set up based on DNS-SD discovery*/
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {
    if (p->type && p->type[0]) {
      p->status = STATUS_DISAPPEARED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
    }
  }
  if (in_shutdown == 0)
    recheck_timer();
  else
    update_cups_queues(NULL);

  /* Free the data structures for DNS-SD browsing */
  if (sb1) {
    avahi_service_browser_free(sb1);
    sb1 = NULL;
  }
  if (sb2) {
    avahi_service_browser_free(sb2);
    sb2 = NULL;
  }

  /* Switch on auto shutdown mode */
  if (autoshutdown_avahi && in_shutdown == 0) {
    autoshutdown = 1;
    debug_printf("Avahi server disappeared, switching to auto shutdown mode ...\n");
    /* If there are no printers or no jobs schedule the shutdown in
       autoshutdown_timeout seconds */
    if (!autoshutdown_exec_id &&
	(cupsArrayCount(remote_printers) == 0 ||
	 (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
      debug_printf ("We entered auto shutdown mode and no printers are there to make available or no jobs on them, shutting down in %d sec...\n", autoshutdown_timeout);
      autoshutdown_exec_id =
	g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
			       NULL);
    }
  }
}

void avahi_shutdown() {
  avahi_browser_shutdown();
  if (client) {
    avahi_client_free(client);
    client = NULL;
  }
  if (glib_poll) {
    avahi_glib_poll_free(glib_poll);
    glib_poll = NULL;
  }
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
  int error;

  if (c == NULL)
    return;

  /* Called whenever the client or server state changes */
  switch (state) {

  /* avahi-daemon available */
  case AVAHI_CLIENT_S_REGISTERING:
  case AVAHI_CLIENT_S_RUNNING:
  case AVAHI_CLIENT_S_COLLISION:

    debug_printf("Avahi server connection got available, setting up service browsers.\n");

    /* Create the service browsers */
    if (!sb1)
      if (!(sb1 =
	    avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				      "_ipp._tcp", NULL, 0, browse_callback,
				      c))) {
	debug_printf("ERROR: Failed to create service browser for IPP: %s\n",
		     avahi_strerror(avahi_client_errno(c)));
      }
    if (!sb2)
      if (!(sb2 =
	    avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				      "_ipps._tcp", NULL, 0, browse_callback,
				      c))) {
	debug_printf("ERROR: Failed to create service browser for IPPS: %s\n",
		     avahi_strerror(avahi_client_errno(c)));
      }

    avahi_present = 1;
    
    /* switch off auto shutdown mode */
    if (autoshutdown_avahi) {
      autoshutdown = 0;
      debug_printf("Avahi server available, switching to permanent mode ...\n");
      /* If there is still an active auto shutdown timer, kill it */
      if (autoshutdown_exec_id) {
	debug_printf ("We have left auto shutdown mode, killing auto shutdown timer.\n");
	g_source_remove(autoshutdown_exec_id);
	autoshutdown_exec_id = 0;
      }
    }

    break;

  /* Avahi client error */
  case AVAHI_CLIENT_FAILURE:

    if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED) {
      debug_printf("Avahi server disappeared, shutting down service browsers, removing DNS-SD-discovered print queues.\n");
      avahi_browser_shutdown();
      /* Renewing client */
      avahi_client_free(client);
      client = avahi_client_new(avahi_glib_poll_get(glib_poll),
				AVAHI_CLIENT_NO_FAIL,
				client_callback, NULL, &error);
      if (!client) {
	debug_printf("ERROR: Failed to create client: %s\n",
		     avahi_strerror(error));
	BrowseRemoteProtocols &= ~BROWSE_DNSSD;
	avahi_shutdown();
      }
    } else {
      debug_printf("ERROR: Avahi server connection failure: %s\n",
		   avahi_strerror(avahi_client_errno(c)));
      g_main_loop_quit(gmainloop);
      g_main_context_wakeup(NULL);
    }
    break;

  default:
    break;
  }
}

void avahi_init() {
  int error;

  if (BrowseRemoteProtocols & BROWSE_DNSSD) {
    /* Allocate main loop object */
    if (!glib_poll)
      if (!(glib_poll = avahi_glib_poll_new(NULL, G_PRIORITY_DEFAULT))) {
	debug_printf("ERROR: Failed to create glib poll object.\n");
	goto avahi_init_fail;
      }

    /* Allocate a new client */
    if (!client)
      client = avahi_client_new(avahi_glib_poll_get(glib_poll),
				AVAHI_CLIENT_NO_FAIL,
				client_callback, NULL, &error);

    /* Check wether creating the client object succeeded */
    if (!client) {
      debug_printf("ERROR: Failed to create client: %s\n",
		   avahi_strerror(error));
      goto avahi_init_fail;
    }

    return;

  avahi_init_fail:
    BrowseRemoteProtocols &= ~BROWSE_DNSSD;
    avahi_shutdown();
  }
}
#endif /* HAVE_AVAHI */

/*
 * A CUPS printer has been discovered via CUPS Browsing
 * or with BrowsePoll
 */
void
found_cups_printer (const char *remote_host, const char *uri,
		    const char *location, const char *info)
{
  char scheme[32];
  char username[64];
  char host[HTTP_MAX_HOST];
  char resource[HTTP_MAX_URI];
  int port;
  netif_t *iface;
  char local_resource[HTTP_MAX_URI];
  char service_name[HTTP_MAX_URI];
  char *c;
  int hl;
  remote_printer_t *printer;

  memset(scheme, 0, sizeof(scheme));
  memset(username, 0, sizeof(username));
  memset(host, 0, sizeof(host));
  memset(resource, 0, sizeof(resource));
  memset(local_resource, 0, sizeof(local_resource));

  httpSeparateURI (HTTP_URI_CODING_ALL, uri,
		   scheme, sizeof(scheme) - 1,
		   username, sizeof(username) - 1,
		   host, sizeof(host) - 1,
		   &port,
		   resource, sizeof(resource)- 1);

  /* Check this isn't one of our own broadcasts */
  for (iface = cupsArrayFirst (netifs);
       iface;
       iface = cupsArrayNext (netifs))
    if (!strcasecmp (host, iface->address))
      break;
  if (iface) {
    debug_printf("ignoring own broadcast on %s\n",
		 iface->address);
    return;
  }

  if (strncasecmp (resource, "/printers/", 10) &&
      strncasecmp (resource, "/classes/", 9)) {
    debug_printf("Don't understand URI: %s\n", uri);
    return;
  }

  strncpy (local_resource, resource + 1, sizeof (local_resource) - 1);
  local_resource[sizeof (local_resource) - 1] = '\0';
  c = strchr (local_resource, '?');
  if (c)
    *c = '\0';

  /* Build the DNS-SD service name which CUPS would give to this printer
     when DNS-SD-broadcasting it */
  snprintf(service_name, sizeof (service_name), "%s @ %s",
	   (info ? info : strchr(local_resource, '/') + 1), host);
  /* Cut off trailing ".local" of host name */
  hl = strlen(service_name);
  if (hl > 6 && !strcasecmp(service_name + hl - 6, ".local"))
    service_name[hl - 6] = '\0';
  if (hl > 7 && !strcasecmp(service_name + hl - 7, ".local."))
    service_name[hl - 7] = '\0';
  /* DNS-SD service name has max. 63 characters */
  service_name[63] = '\0';

  debug_printf("CUPS browsing: Remote host: %s; Port: %d; Remote queue name: %s; Service Name: %s\n",
	       host, port, strchr(local_resource, '/') + 1, service_name);

  printer = examine_discovered_printer_record(host, NULL, port, local_resource,
					      service_name,
					      location ? location : "",
					      info ? info : "", "", "", NULL);

  if (printer &&
      (printer->domain == NULL || printer->domain[0] == '\0' ||
       printer->type == NULL || printer->type[0] == '\0')) {
    printer->is_legacy = 1;
    if (printer->status != STATUS_TO_BE_CREATED) {
      printer->timeout = time(NULL) + BrowseTimeout;
      debug_printf("starting BrowseTimeout timer for %s (%ds)\n",
		   printer->queue_name, BrowseTimeout);
    }
  }
}

gboolean
process_browse_data (GIOChannel *source,
		     GIOCondition condition,
		     gpointer data)
{
  char packet[2048];
  http_addr_t srcaddr;
  socklen_t srclen;
  ssize_t got;
  unsigned int type;
  unsigned int state;
  char remote_host[256];
  char uri[1024];
  char location[1024];
  char info[1024];
  char *c = NULL, *end = NULL;

  debug_printf("process_browse_data() in THREAD %ld\n", pthread_self());

  memset(packet, 0, sizeof(packet));
  memset(remote_host, 0, sizeof(remote_host));
  memset(uri, 0, sizeof(uri));
  memset(info, 0, sizeof(info));

  srclen = sizeof (srcaddr);
  got = recvfrom (browsesocket, packet, sizeof (packet) - 1, 0,
		  &srcaddr.addr, &srclen);
  if (got == -1) {
    debug_printf ("cupsd-browsed: error receiving browse packet: %s\n",
		  strerror (errno));
    /* Remove this I/O source */
    return FALSE;
  }

  packet[got] = '\0';
  httpAddrString (&srcaddr, remote_host, sizeof (remote_host) - 1);

  /* Check this packet is allowed */
  if (!allowed ((struct sockaddr *) &srcaddr)) {
    debug_printf("browse packet from %s disallowed\n",
		 remote_host);
    return TRUE;
  }

  debug_printf("browse packet received from %s\n",
	       remote_host);

  if (sscanf (packet, "%x%x%1023s", &type, &state, uri) < 3) {
    debug_printf("incorrect browse packet format\n");
    return TRUE;
  }

  info[0] = '\0';

  /* do not read OOB */
  end = packet + sizeof(packet);
  c = strchr (packet, '\"');
  if (c >= end)
     return TRUE;

  if (c) {
    /* Extract location field */
    {
      int i;
      c++;
      for (i = 0;
	   i < sizeof (location) - 1 && *c != '\"' && c < end;
	   i++, c++)
	location[i] = *c;
      location[i] = '\0';
      debug_printf("process_browse_data: location: |%s|\n", location); /* !! */
    }
    for (; c < end && *c != '\"'; c++)
      ;

    if (c >= end)
       return TRUE;

    if (*c == '\"') {
      for (c++; c < end && isspace(*c); c++)
	;
    }

    if (c >= end)
      return TRUE;

    /* Is there an info field? */
    if (*c == '\"') {
      int i;
      c++;
      for (i = 0;
	   i < sizeof (info) - 1 && *c != '\"' && c < end;
	   i++, c++)
	info[i] = *c;
      info[i] = '\0';
      debug_printf("process_browse_data: info: |%s|\n", info); /* !! */
    }
  }
  if (c >= end)
    return TRUE;

  if (!(type & CUPS_PRINTER_DELETE))
    found_cups_printer (remote_host, uri, location, info);

  if (in_shutdown == 0)
    recheck_timer ();

  /* Don't remove this I/O source */
  return TRUE;
}

static gboolean
update_netifs (gpointer data)
{
  struct ifaddrs *ifaddr, *ifa;
  netif_t *iface, *iface2;
  int dupe;

  debug_printf("update_netifs() in THREAD %ld\n", pthread_self());

  update_netifs_sourceid = 0;
  if (getifaddrs (&ifaddr) == -1) {
    debug_printf("unable to get interface addresses: %s\n",
		 strerror (errno));
    return FALSE;
  }

  while ((iface = cupsArrayFirst (netifs)) != NULL) {
    cupsArrayRemove (netifs, iface);
    free (iface->address);
    free (iface);
  }

  for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    netif_t *iface;

    if (ifa->ifa_addr == NULL)
      continue;

    if (ifa->ifa_broadaddr == NULL)
      continue;

    if (ifa->ifa_flags & IFF_LOOPBACK)
      continue;

    if (!(ifa->ifa_flags & IFF_BROADCAST))
      continue;

    iface = malloc (sizeof (netif_t));
    if (iface == NULL) {
      debug_printf ("malloc failure\n");
      exit (1);
    }

    iface->address = malloc (HTTP_MAX_HOST);
    if (iface->address == NULL) {
      free (iface);
      debug_printf ("malloc failure\n");
      exit (1);
    }

    iface->address[0] = '\0';
    switch (ifa->ifa_addr->sa_family) {
    case AF_INET:
      /* copy broadcast addr/fill in port first to faciliate dupe compares */
      memcpy (&iface->broadcast, ifa->ifa_broadaddr,
	      sizeof (struct sockaddr_in));
      iface->broadcast.ipv4.sin_port = htons (BrowsePort);
      /* discard if we already have an interface sharing the broadcast address */
      dupe = 0;
      for (iface2 = (netif_t *)cupsArrayFirst (netifs);
           iface2 != NULL;
           iface2 = (netif_t *)cupsArrayNext (netifs)) {
	if (memcmp(&iface2->broadcast, &iface->broadcast,
	    sizeof(struct sockaddr_in)) == 0) {
	  dupe = 1;
	  break;
	}
      }
      if (dupe) break;
      getnameinfo (ifa->ifa_addr, sizeof (struct sockaddr_in),
		   iface->address, HTTP_MAX_HOST,
		   NULL, 0, NI_NUMERICHOST);
      break;

    case AF_INET6:
      if (IN6_IS_ADDR_LINKLOCAL (&((struct sockaddr_in6 *)(ifa->ifa_addr))
				 ->sin6_addr))
	break;

      /* see above for order */
      memcpy (&iface->broadcast, ifa->ifa_broadaddr,
	      sizeof (struct sockaddr_in6));
      iface->broadcast.ipv6.sin6_port = htons (BrowsePort);
      /* discard alias addresses (identical broadcast) */
      dupe = 0;
      for (iface2 = (netif_t *)cupsArrayFirst (netifs);
           iface2 != NULL;
           iface2 = (netif_t *)cupsArrayNext (netifs)) {
	if (memcmp(&iface2->broadcast, ifa->ifa_broadaddr,
	    sizeof(struct sockaddr_in6)) == 0) {
	  dupe = 1;
	  break;
	}
      }
      if (dupe) break;
      getnameinfo (ifa->ifa_addr, sizeof (struct sockaddr_in6),
		   iface->address, HTTP_MAX_HOST, NULL, 0, NI_NUMERICHOST);
      break;
    }

    if (iface->address[0]) {
      cupsArrayAdd (netifs, iface);
      debug_printf("network interface at %s\n", iface->address);
    } else {
      free (iface->address);
      free (iface);
    }
  }

  freeifaddrs (ifaddr);

  /* If run as a timeout, don't run it again. */
  return FALSE;
}

static void
broadcast_browse_packets (gpointer data, gpointer user_data)
{
  browse_data_t *bdata = data;
  netif_t *browse;
  char packet[2048];
  char uri[HTTP_MAX_URI];
  char scheme[32];
  char username[64];
  char host[HTTP_MAX_HOST];
  int port;
  char resource[HTTP_MAX_URI];

  debug_printf("broadcast_browse_packets() in THREAD %ld\n", pthread_self());

  for (browse = (netif_t *)cupsArrayFirst (netifs);
       browse != NULL;
       browse = (netif_t *)cupsArrayNext (netifs)) {
    /* Replace 'localhost' with our IP address on this interface */
    httpSeparateURI(HTTP_URI_CODING_ALL, bdata->uri,
		    scheme, sizeof(scheme),
		    username, sizeof(username),
		    host, sizeof(host),
		    &port,
		    resource, sizeof(resource));
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof (uri),
		    scheme, username, browse->address, port, resource);

    if (snprintf (packet, sizeof (packet),
		  "%x "     /* type */
		  "%x "     /* state */
		  "%s "     /* uri */
		  "\"%s\" " /* location */
		  "\"%s\" " /* info */
		  "\"%s\" " /* make-and-model */
		  "lease-duration=%d" /* BrowseTimeout */
		  "%s%s" /* other browse options */
		  "\n",
		  bdata->type,
		  bdata->state,
		  uri,
		  bdata->location,
		  bdata->info,
		  bdata->make_model,
		  BrowseTimeout,
		  bdata->browse_options ? " " : "",
		  bdata->browse_options ? bdata->browse_options : "")
	>= sizeof (packet)) {
      debug_printf ("oversize packet not sent\n");
      continue;
    }

    debug_printf("packet to send:\n%s", packet);

    int err = sendto (browsesocket, packet,
		      strlen (packet), 0,
		      &browse->broadcast.addr,
		      httpAddrLength (&browse->broadcast));
    if (err == -1)
      debug_printf("cupsd-browsed: sendto returned %d: %s\n",
		   err, strerror (errno));
  }
}

gboolean
send_browse_data (gpointer data)
{
  debug_printf("send_browse_data() in THREAD %ld\n", pthread_self());
  update_netifs (NULL);
  res_init ();
  update_local_printers ();
  g_list_foreach (browse_data, broadcast_browse_packets, NULL);
  g_timeout_add_seconds (BrowseInterval, send_browse_data, NULL);

  /* Stop this timeout handler, we called a new one */
  return FALSE;
}

static browsepoll_printer_t *
new_browsepoll_printer (const char *uri_supported,
			const char *location,
			const char *info)
{
  browsepoll_printer_t *printer = g_malloc (sizeof (browsepoll_printer_t));
  printer->uri_supported = g_strdup (uri_supported);
  printer->location = g_strdup (location);
  printer->info = g_strdup (info);
  return printer;
}

static void
browsepoll_printer_free (gpointer data)
{
  browsepoll_printer_t *printer = data;
  debug_printf("browsepoll_printer_free() in THREAD %ld\n", pthread_self());
  free (printer->uri_supported);
  free (printer->location);
  free (printer->info);
  free (printer);
}

static void
browse_poll_get_printers (browsepoll_t *context, http_t *conn)
{
  static const char * const rattrs[] = { "printer-uri-supported",
					 "printer-info"};
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;
  GList *printers = NULL;

  debug_printf ("cups-browsed [BrowsePoll %s:%d]: CUPS-Get-Printers\n",
		context->server, context->port);

  request = ippNewRequest(CUPS_GET_PRINTERS);
  if (context->major > 0) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: setting IPP version %d.%d\n",
		 context->server, context->port, context->major,
		 context->minor);
    ippSetVersion (request, context->major, context->minor);
  }

  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes", sizeof (rattrs) / sizeof (rattrs[0]),
		 NULL,
		 rattrs);

  /* Ask the server to exclude printers that are remote or not shared,
     or implicit classes. */
  ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_ENUM,
		 "printer-type-mask",
		 CUPS_PRINTER_REMOTE | CUPS_PRINTER_IMPLICIT |
		 CUPS_PRINTER_NOT_SHARED);
  ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_ENUM,
		 "printer-type", 0);

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());

  response = cupsDoRequest(conn, request, "/");
  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: failed: %s\n",
		 context->server, context->port, cupsLastErrorString ());
    goto fail;
  }

  for (attr = ippFirstAttribute(response); attr;
       attr = ippNextAttribute(response)) {
    browsepoll_printer_t *printer;
    const char *uri, *location, *info;

    while (attr && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
      attr = ippNextAttribute(response);

    if (!attr)
      break;

    uri = NULL;
    info = NULL;
    location = NULL;
    while (attr && ippGetGroupTag(attr) == IPP_TAG_PRINTER) {

      if (!strcasecmp (ippGetName(attr), "printer-uri-supported") &&
	  ippGetValueTag(attr) == IPP_TAG_URI)
	uri = ippGetString(attr, 0, NULL);
      else if (!strcasecmp (ippGetName(attr), "printer-location") &&
	       ippGetValueTag(attr) == IPP_TAG_TEXT)
	location = ippGetString(attr, 0, NULL);
      else if (!strcasecmp (ippGetName(attr), "printer-info") &&
	       ippGetValueTag(attr) == IPP_TAG_TEXT)
	info = ippGetString(attr, 0, NULL);

      attr = ippNextAttribute(response);
    }

    if (uri) {
      found_cups_printer (context->server, uri, location, info);
      printer = new_browsepoll_printer (uri, location, info);
      printers = g_list_insert (printers, printer, 0);
    }

    if (!attr)
      break;
  }

  g_list_free_full (context->printers, browsepoll_printer_free);
  context->printers = printers;

fail:
  if (response)
    ippDelete(response);
}

static void
browse_poll_create_subscription (browsepoll_t *context, http_t *conn)
{
  static const char * const events[] = { "printer-added",
					 "printer-changed",
					 "printer-config-changed",
					 "printer-modified",
					 "printer-deleted",
					 "printer-state-changed" };
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;

  debug_printf ("cups-browsed [BrowsePoll %s:%d]: IPP-Create-Subscription\n",
		context->server, context->port);

  request = ippNewRequest(IPP_CREATE_PRINTER_SUBSCRIPTION);
  if (context->major > 0) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: setting IPP version %d.%d\n",
		 context->server, context->port, context->major,
		 context->minor);
    ippSetVersion (request, context->major, context->minor);
  }

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
		"notify-pull-method", NULL, "ippget");
  ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_CHARSET,
		"notify-charset", NULL, "utf-8");
  ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  ippAddStrings (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
		 "notify-events", sizeof (events) / sizeof (events[0]),
		 NULL, events);
  ippAddInteger (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		 "notify-time-interval", BrowseInterval);

  response = cupsDoRequest (conn, request, "/");
  if (!response ||
      ippGetStatusCode (response) > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("cupsd-browsed [BrowsePoll %s:%d]: failed: %s\n",
		 context->server, context->port, cupsLastErrorString ());
    context->subscription_id = -1;
    context->can_subscribe = FALSE;
    goto fail;
  }

  for (attr = ippFirstAttribute(response); attr;
       attr = ippNextAttribute(response)) {
    if (ippGetGroupTag (attr) == IPP_TAG_SUBSCRIPTION) {
      if (ippGetValueTag (attr) == IPP_TAG_INTEGER &&
	  !strcasecmp (ippGetName (attr), "notify-subscription-id")) {
	context->subscription_id = ippGetInteger (attr, 0);
	debug_printf("cups-browsed [BrowsePoll %s:%d]: subscription ID=%d\n",
		     context->server, context->port, context->subscription_id);
	break;
      }
    }
  }

  if (!attr) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: no ID returned\n",
		 context->server, context->port);
    context->subscription_id = -1;
    context->can_subscribe = FALSE;
  }

fail:
  if (response)
    ippDelete(response);
}

static void
browse_poll_cancel_subscription (browsepoll_t *context)
{
  ipp_t *request, *response = NULL;
  http_t *conn = httpConnectEncryptShortTimeout (context->server, context->port,
						 HTTP_ENCRYPT_IF_REQUESTED);

  if (conn == NULL) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: connection failure "
		 "attempting to cancel\n", context->server, context->port);
    return;
  }

  httpSetTimeout(conn, HttpRemoteTimeout, http_timeout_cb, NULL);

  debug_printf ("cups-browsed [BrowsePoll %s:%d]: IPP-Cancel-Subscription\n",
		context->server, context->port);

  request = ippNewRequest(IPP_CANCEL_SUBSCRIPTION);
  if (context->major > 0) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: setting IPP version %d.%d\n",
		 context->server, context->port, context->major,
		 context->minor);
    ippSetVersion (request, context->major, context->minor);
  }

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-id", context->subscription_id);

  response = cupsDoRequest (conn, request, "/");
  if (!response ||
      ippGetStatusCode (response) > IPP_STATUS_OK_EVENTS_COMPLETE)
    debug_printf("cupsd-browsed [BrowsePoll %s:%d]: failed: %s\n",
		 context->server, context->port, cupsLastErrorString ());

  if (response)
    ippDelete(response);

  httpClose (conn);
}

static gboolean
browse_poll_get_notifications (browsepoll_t *context, http_t *conn)
{
  ipp_t *request, *response = NULL;
  ipp_status_t status;
  gboolean get_printers = FALSE;

  debug_printf ("cups-browsed [BrowsePoll %s:%d]: IPP-Get-Notifications\n",
		context->server, context->port);

  request = ippNewRequest(IPP_GET_NOTIFICATIONS);
  if (context->major > 0) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: setting IPP version %d.%d\n",
		 context->server, context->port, context->major,
		 context->minor);
    ippSetVersion (request, context->major, context->minor);
  }

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-ids", context->subscription_id);
  ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-sequence-numbers", context->sequence_number + 1);

  response = cupsDoRequest (conn, request, "/");
  if (!response)
    status = cupsLastError ();
  else
    status = ippGetStatusCode (response);

  if (status == IPP_STATUS_ERROR_NOT_FOUND) {
    /* Subscription lease has expired. */
    debug_printf ("cups-browsed [BrowsePoll %s:%d]: Lease expired\n",
		  context->server, context->port);
    browse_poll_create_subscription (context, conn);
    get_printers = TRUE;
  } else if (status > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: failed: %s\n",
		 context->server, context->port, cupsLastErrorString ());
    context->can_subscribe = FALSE;
    browse_poll_cancel_subscription (context);
    context->subscription_id = -1;
    context->sequence_number = 0;
    get_printers = TRUE;
  }

  if (!get_printers) {
    ipp_attribute_t *attr;
    gboolean seen_event = FALSE;
    int last_seq = context->sequence_number;
    if (response == NULL)
      return FALSE;
    for (attr = ippFirstAttribute(response); attr;
	 attr = ippNextAttribute(response))
      if (ippGetGroupTag (attr) == IPP_TAG_EVENT_NOTIFICATION) {
	/* There is a printer-* event here. */
	seen_event = TRUE;

	if (!strcmp (ippGetName (attr), "notify-sequence-number") &&
	    ippGetValueTag (attr) == IPP_TAG_INTEGER)
	  last_seq = ippGetInteger (attr, 0);
      }

    if (seen_event) {
      debug_printf("cups-browsed [BrowsePoll %s:%d]: printer-* event\n",
		   context->server, context->port);
      context->sequence_number = last_seq;
      get_printers = TRUE;
    } else
      debug_printf("cups-browsed [BrowsePoll %s:%d]: no events\n",
		   context->server, context->port);
  }

  if (response)
    ippDelete (response);

  return get_printers;
}

static void
browsepoll_printer_keepalive (gpointer data, gpointer user_data)
{
  browsepoll_printer_t *printer = data;
  const char *server = user_data;
  debug_printf("browsepoll_printer_keepalive() in THREAD %ld\n", pthread_self());
  found_cups_printer (server, printer->uri_supported, printer->location,
		      printer->info);
}

gboolean
browse_poll (gpointer data)
{
  browsepoll_t *context = data;
  http_t *conn = NULL;
  gboolean get_printers = FALSE;

  debug_printf("browse_poll() in THREAD %ld\n", pthread_self());

  debug_printf ("browse polling %s:%d\n",
		context->server, context->port);

  res_init ();

  conn = httpConnectEncryptShortTimeout (context->server, context->port,
					 HTTP_ENCRYPT_IF_REQUESTED);
  if (conn == NULL) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: failed to connect\n",
		 context->server, context->port);
    goto fail;
  }

  httpSetTimeout(conn, HttpRemoteTimeout, http_timeout_cb, NULL);

  if (context->can_subscribe) {
    if (context->subscription_id == -1) {
      /* The first time this callback is run we need to create the IPP
       * subscription to watch to printer-* events. */
      browse_poll_create_subscription (context, conn);
      get_printers = TRUE;
    } else
      /* On subsequent runs, check for notifications using our
       * subscription. */
      get_printers = browse_poll_get_notifications (context, conn);
  }
  else
    get_printers = TRUE;

  update_local_printers ();
  inhibit_local_printers_update = TRUE;
  if (get_printers)
    browse_poll_get_printers (context, conn);
  else
    g_list_foreach (context->printers, browsepoll_printer_keepalive,
		    context->server);

  inhibit_local_printers_update = FALSE;

  if (in_shutdown == 0)
    recheck_timer ();

 fail:

  if (conn)
    httpClose (conn);

  /* Call a new timeout handler so that we run again */
  g_timeout_add_seconds (BrowseInterval, browse_poll, data);

  /* Stop this timeout handler, we called a new one */
  return FALSE;
}

#ifdef HAVE_LDAP
gboolean
browse_ldap_poll (gpointer data)
{
  char                  *tmpFilter;     /* Query filter */
  int                   filterLen;

  debug_printf("browse_ldap_poll() in THREAD %ld\n", pthread_self());

  /* do real stuff here */
  if (!BrowseLDAPDN)
  {
    debug_printf("Need to set BrowseLDAPDN to use LDAP browsing!\n");
    BrowseLocalProtocols &= ~BROWSE_LDAP;
    BrowseRemoteProtocols &= ~BROWSE_LDAP;

    return FALSE;
  }
  else
  {
    if (!BrowseLDAPInitialised)
    {
      BrowseLDAPInitialised = TRUE;
      /*
       * Query filter string
       */
      if (BrowseLDAPFilter)
	filterLen = snprintf(NULL, 0, "(&%s%s)", LDAP_BROWSE_FILTER, BrowseLDAPFilter);
      else
	filterLen = strlen(LDAP_BROWSE_FILTER);

      tmpFilter = (char *)malloc(filterLen + 1);
      if (!tmpFilter)
	{
	  debug_printf("Could not allocate memory for LDAP browse query filter!\n");
	  BrowseLocalProtocols &= ~BROWSE_LDAP;
	  BrowseRemoteProtocols &= ~BROWSE_LDAP;

	  return FALSE;
	}

      if (BrowseLDAPFilter)
	{
	  snprintf(tmpFilter, filterLen + 1, "(&%s%s)", LDAP_BROWSE_FILTER, BrowseLDAPFilter);
	  free(BrowseLDAPFilter);
	  BrowseLDAPFilter = NULL;
	}
      else
	strcpy(tmpFilter, LDAP_BROWSE_FILTER);

      BrowseLDAPFilter = tmpFilter;

      /*
       * Open LDAP handle...
       */

      BrowseLDAPHandle = ldap_connect();
    }

    cupsdUpdateLDAPBrowse();
    if (in_shutdown == 0)
      recheck_timer();
  }

  /* Call a new timeout handler so that we run again */
  g_timeout_add_seconds (BrowseInterval, browse_ldap_poll, data);

  /* Stop this timeout handler, we called a new one */
  return FALSE;
}
#endif /* HAVE_LDAP */

static void
sigterm_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  if (terminating) {
    debug_printf("Caught signal %d while already terminating.\n", sig);
    return;
  }
  terminating = 1; /* ignore any further callbacks and break loops */
  /* Flag that we should stop and return... */
  g_main_loop_quit(gmainloop);
  g_main_context_wakeup(NULL);
  debug_printf("Caught signal %d, shutting down ...\n", sig);
}

static void
sigusr1_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  /* Turn off auto shutdown mode... */
  autoshutdown = 0;
  debug_printf("Caught signal %d, switching to permanent mode ...\n", sig);
  /* If there is still an active auto shutdown timer, kill it */
  if (autoshutdown_exec_id) {
    debug_printf ("We have left auto shutdown mode, killing auto shutdown timer.\n");
    g_source_remove(autoshutdown_exec_id);
    autoshutdown_exec_id = 0;
  }
}

static void
sigusr2_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  /* Turn on auto shutdown mode... */
  autoshutdown = 1;
  debug_printf("Caught signal %d, switching to auto shutdown mode ...\n", sig);
  /* If there are no printers or no jobs schedule the shutdown in
     autoshutdown_timeout seconds */
  if (!autoshutdown_exec_id &&
      (cupsArrayCount(remote_printers) == 0 ||
       (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
    debug_printf ("We entered auto shutdown mode and no printers are there to make available or no jobs on them, shutting down in %d sec...\n", autoshutdown_timeout);
    autoshutdown_exec_id =
      g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
			       NULL);
  }
}

static int
read_browseallow_value (const char *value, allow_sense_t sense)
{
  char *p;
  struct in_addr addr;
  allow_t *allow;

  if (value && !strcasecmp (value, "all")) {
    if (sense == ALLOW_ALLOW) {
      browseallow_all = TRUE;
      return 0;
    } else if (sense == ALLOW_DENY) {
      browsedeny_all = TRUE;
      return 0;
    } else
      return 1;
  }
  
  allow = calloc (1, sizeof (allow_t));
  allow->sense = sense;
  if (value == NULL)
    goto fail;
  p = strchr (value, '/');
  if (p) {
    char *s = strdup (value);
    s[p - value] = '\0';

    if (!inet_aton (s, &addr)) {
      free (s);
      goto fail;
    }

    free (s);
    allow->type = ALLOW_NET;
    allow->addr.ipv4.sin_addr.s_addr = addr.s_addr;

    p++;
    if (strchr (p, '.')) {
      if (inet_aton (p, &addr))
	allow->mask.ipv4.sin_addr.s_addr = addr.s_addr;
      else
	goto fail;
    } else {
      char *endptr;
      unsigned long bits = strtoul (p, &endptr, 10);
      if (p == endptr)
	goto fail;

      if (bits > 32)
	goto fail;

      allow->mask.ipv4.sin_addr.s_addr = htonl (((0xffffffff << (32 - bits)) &
						 0xffffffff));
    }
  } else if (inet_aton (value, &addr)) {
    allow->type = ALLOW_IP;
    allow->addr.ipv4.sin_addr.s_addr = addr.s_addr;
  } else
    goto fail;

  cupsArrayAdd (browseallow, allow);
  return 0;

fail:
  allow->type = ALLOW_INVALID;
  cupsArrayAdd (browseallow, allow);
  return 1;
}

void
read_configuration (const char *filename)
{
  cups_file_t *fp;
  int i, linenum = 0;
  char line[HTTP_MAX_BUFFER];
  char *value = NULL, *ptr, *start;
  const char *delim = " \t,";
  int browse_allow_line_found = 0;
  int browse_deny_line_found = 0;
  int browse_order_line_found = 0;
  int browse_line_found = 0;
  browse_filter_t *filter = NULL;
  int browse_filter_options, exact_match, err;
  char errbuf[1024];
  cluster_t *cluster = NULL;

  if (!filename)
    filename = CUPS_SERVERROOT "/cups-browsed.conf";

  if ((fp = cupsFileOpen(filename, "r")) == NULL) {
    debug_printf("unable to open configuration file; "
		 "using defaults\n");
    return;
  }

  i = 0;
  linenum = -1;
  /* First, we read the option settings supplied on the command line via
     "-o ..." in the order given on the command line, then we read the lines
     of the configuration file. This means that if there are contradicting
     settings on the command line and in the configuration file, the setting
     in the configuration file is used. */
  while ((i < cupsArrayCount(command_line_config) &&
	  (value = cupsArrayIndex(command_line_config, i++)) &&
	  strncpy(line, value, sizeof(line))) ||
	 cupsFileGetConf(fp, line, sizeof(line), &value, &linenum)) {
    if (linenum < 0) {
      /* We are still reading options from the command line ("-o ..."),
	 separate key (line) and value (value) */
      value = line;
      while (*value && !isspace(*value) && !(*value == '='))
	value ++;
      if (*value) {
	*value = '\0';
	value ++;
	while (*value && (isspace(*value) || (*value == '=')))
	  value ++;
      }
    }
    
    debug_printf("Reading config%s: %s %s\n",
		 (linenum < 0 ? " (from command line)" : ""), line, value);
    if (!strcasecmp(line, "DebugLogging") && value) {
      char *p, *saveptr;
      p = strtok_r (value, delim, &saveptr);
      while (p) {
	if (!strcasecmp(p, "file")) {
	  if (debug_logfile == 0) {
	    debug_logfile = 1;
	    start_debug_logging();
	  }
	} else if (!strcasecmp(p, "stderr"))
	  debug_stderr = 1;
	else if (strcasecmp(p, "none"))
	  debug_printf("Unknown debug logging mode '%s'\n", p);

	p = strtok_r (NULL, delim, &saveptr);
      }
    } else if (!strcasecmp(line, "CacheDir") && value) {
      if (value[0] != '\0')
	strncpy(cachedir, value, sizeof(cachedir) - 1);
    } else if (!strcasecmp(line, "LogDir") && value) {
      if (value[0] != '\0')
	strncpy(logdir, value, sizeof(logdir) - 1);
    } else if ((!strcasecmp(line, "BrowseProtocols") ||
	 !strcasecmp(line, "BrowseLocalProtocols") ||
	 !strcasecmp(line, "BrowseRemoteProtocols")) && value) {
      int protocols = 0;
      char *p, *saveptr;
      p = strtok_r (value, delim, &saveptr);
      while (p) {
	if (!strcasecmp(p, "dnssd"))
	  protocols |= BROWSE_DNSSD;
	else if (!strcasecmp(p, "cups"))
	  protocols |= BROWSE_CUPS;
	else if (!strcasecmp(p, "ldap"))
	  protocols |= BROWSE_LDAP;
	else if (strcasecmp(p, "none"))
	  debug_printf("Unknown protocol '%s'\n", p);

	p = strtok_r (NULL, delim, &saveptr);
      }

      if (!strcasecmp(line, "BrowseLocalProtocols"))
	BrowseLocalProtocols = protocols;
      else if (!strcasecmp(line, "BrowseRemoteProtocols"))
	BrowseRemoteProtocols = protocols;
      else
	BrowseLocalProtocols = BrowseRemoteProtocols = protocols;
    } else if (!strcasecmp(line, "BrowsePoll") && value) {
      browsepoll_t **old = BrowsePoll;
      BrowsePoll = realloc (BrowsePoll,
			    (NumBrowsePoll + 1) *
			    sizeof (browsepoll_t *));
      if (!BrowsePoll) {
	debug_printf("unable to realloc: ignoring BrowsePoll line\n");
	BrowsePoll = old;
      } else {
	char *colon, *slash;
	browsepoll_t *b = g_malloc0 (sizeof (browsepoll_t));
	debug_printf("Adding BrowsePoll server: %s\n", value);
	b->server = strdup (value);
	b->port = BrowsePort;
	b->can_subscribe = TRUE; /* first assume subscriptions work */
	b->subscription_id = -1;
	slash = strchr (b->server, '/');
	if (slash) {
	  *slash++ = '\0';
	  if (!strcasecmp (slash, "version=1.0")) {
	    b->major = 1;
	    b->minor = 0;
	  } else if (!strcasecmp (slash, "version=1.1")) {
	    b->major = 1;
	    b->minor = 1;
	  } else if (!strcasecmp (slash, "version=2.0")) {
	    b->major = 2;
	    b->minor = 0;
	  } else if (!strcasecmp (slash, "version=2.1")) {
	    b->major = 2;
	    b->minor = 1;
	  } else if (!strcasecmp (slash, "version=2.2")) {
	    b->major = 2;
	    b->minor = 2;
	  } else {
	    debug_printf ("ignoring unknown server option: %s\n", slash);
	  }
	} else
	  b->major = 0;

	colon = strchr (b->server, ':');
	if (colon) {
	  char *endptr;
	  unsigned long n;
	  *colon++ = '\0';
	  n = strtoul (colon, &endptr, 10);
	  if (endptr != colon && n < INT_MAX)
	    b->port = (int) n;
	}

	BrowsePoll[NumBrowsePoll++] = b;
      }
    } else if (!strcasecmp(line, "BrowseAllow")) {
      if (read_browseallow_value (value, ALLOW_ALLOW))
	debug_printf ("BrowseAllow value \"%s\" not understood\n",
		      value);
      else {
	browse_allow_line_found = 1;
	browse_line_found = 1;
      }
    } else if (!strcasecmp(line, "BrowseDeny")) {
      if (read_browseallow_value (value, ALLOW_DENY))
	debug_printf ("BrowseDeny value \"%s\" not understood\n",
		      value);
      else {
	browse_deny_line_found = 1;
	browse_line_found = 1;
      }
    } else if (!strcasecmp(line, "BrowseOrder") && value) {
      if (!strncasecmp(value, "Allow", 5) &&
	  strcasestr(value, "Deny")) { /* Allow,Deny */
	browse_order = ORDER_ALLOW_DENY;
	browse_order_line_found = 1;
	browse_line_found = 1;
      } else if (!strncasecmp(value, "Deny", 4) &&
		 strcasestr(value, "Allow")) { /* Deny,Allow */
	browse_order = ORDER_DENY_ALLOW;
	browse_order_line_found = 1;
	browse_line_found = 1;
      } else
	debug_printf ("BrowseOrder value \"%s\" not understood\n",
		      value);
    } else if (!strcasecmp(line, "BrowseFilter") && value) {
      ptr = value;
      /* Skip white space */
      while (*ptr && isspace(*ptr)) ptr ++;
      /* Premature line end */
      if (!*ptr) goto browse_filter_fail;
      filter = calloc (1, sizeof (browse_filter_t));
      if (!filter) goto browse_filter_fail;
      browse_filter_options = 1;
      filter->sense = FILTER_MATCH;
      exact_match = 0;
      while (browse_filter_options) {
	if (!strncasecmp(ptr, "NOT", 3) && *(ptr + 3) &&
	    isspace(*(ptr + 3))) {
	  /* Accept remote printers where regexp does NOT match or where
	     the boolean field is false */
	  filter->sense = FILTER_NOT_MATCH;
	  ptr += 4;
	  /* Skip white space until next word */
	  while (*ptr && isspace(*ptr)) ptr ++;
	  /* Premature line end without field name */
	  if (!*ptr) goto browse_filter_fail;
	} else if (!strncasecmp(ptr, "EXACT", 5) && *(ptr + 5) &&
		   isspace(*(ptr + 5))) {
	  /* Consider the rest of the line after the field name a string which
	     has to match the field exactly */
	  exact_match = 1;
	  ptr += 6;
	  /* Skip white space until next word */
	  while (*ptr && isspace(*ptr)) ptr ++;
	  /* Premature line end without field name */
	  if (!*ptr) goto browse_filter_fail;
	} else
	  /* No more options, consider next word the name of the field which
	     should match the regexp */
	  browse_filter_options = 0;
      }
      start = ptr;
      while (*ptr && !isspace(*ptr)) ptr ++;
      if (*ptr) {
	/* Mark end of the field name */
	*ptr = '\0';
	/* Skip white space until regexp or line end */
	ptr ++;
	while (*ptr && isspace(*ptr)) ptr ++;
      }
      filter->field = strdup(start);
      if (!*ptr) {
	/* Only field name and no regexp is given, so this rule is
	   about matching a boolean value */
	filter->regexp = NULL;
	filter->cregexp = NULL;
      } else {
	/* The rest of the line is the regexp, store and compile it */
	filter->regexp = strdup(ptr);
	if (!exact_match) {
	  /* Compile the regexp only if the line does not require an exact
	     match (using the EXACT option */
	  filter->cregexp = calloc(1, sizeof (regex_t));
	  if ((err = regcomp(filter->cregexp, filter->regexp,
			     REG_EXTENDED | REG_ICASE)) != 0) {
	    regerror(err, filter->cregexp, errbuf, sizeof(errbuf));
	    debug_printf ("BrowseFilter line with error in regular expression \"%s\": %s\n",
			  filter->regexp, errbuf);
	    goto browse_filter_fail;
	  }
	} else
	  filter->cregexp = NULL;
      }
      cupsArrayAdd (browsefilter, filter);
      continue;
    browse_filter_fail:
      if (filter) {
	if (filter->field)
	  free(filter->field);
	if (filter->regexp)
	  free(filter->regexp);
	if (filter->cregexp)
	  regfree(filter->cregexp);
	free(filter);
      }
    } else if ((!strcasecmp(line, "BrowseInterval") || !strcasecmp(line, "BrowseTimeout")) && value) {
      int t = atoi(value);
      if (t >= 0) {
	if (!strcasecmp(line, "BrowseInterval"))
	  BrowseInterval = t;
	else if (!strcasecmp(line, "BrowseTimeout"))
	  BrowseTimeout = t;

	debug_printf("Set %s to %d sec.\n",
		     line, t);
      } else
	debug_printf("Invalid %s value: %d\n",
		     line, t);
    } else if (!strcasecmp(line, "DomainSocket") && value) {
      if (value[0] != '\0')
	DomainSocket = strdup(value);
    } else if ((!strcasecmp(line, "HttpLocalTimeout") || !strcasecmp(line, "HttpRemoteTimeout")) && value) {
      int t = atoi(value);
      if (t >= 0) {
	if (!strcasecmp(line, "HttpLocalTimeout"))
	  HttpLocalTimeout = t;
	else if (!strcasecmp(line, "HttpRemoteTimeout"))
	  HttpRemoteTimeout = t;

	debug_printf("Set %s to %d sec.\n",
		     line, t);
      } else
	debug_printf("Invalid %s value: %d\n",
		     line, t);
    } else if (!strcasecmp(line, "IPBasedDeviceURIs") && value) {
      if (!strcasecmp(value, "IPv4") || !strcasecmp(value, "IPv4Only"))
	IPBasedDeviceURIs = IP_BASED_URIS_IPV4_ONLY;
      else if (!strcasecmp(value, "IPv6") || !strcasecmp(value, "IPv6Only"))
	IPBasedDeviceURIs = IP_BASED_URIS_IPV6_ONLY;
      else if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	       !strcasecmp(value, "on") || !strcasecmp(value, "1") ||
	       !strcasecmp(value, "IP") || !strcasecmp(value, "IPAddress"))
	IPBasedDeviceURIs = IP_BASED_URIS_ANY;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	       !strcasecmp(value, "off") || !strcasecmp(value, "0") ||
	       !strcasecmp(value, "Name") || !strcasecmp(value, "HostName"))
	IPBasedDeviceURIs = IP_BASED_URIS_NO;
    } else if (!strcasecmp(line, "LocalQueueNamingRemoteCUPS") && value) {
      if (strcasestr(value, "DNSSD") || strcasestr(value, "DNS-SD"))
	LocalQueueNamingRemoteCUPS = LOCAL_QUEUE_NAMING_DNSSD;
      else if (strcasestr(value, "Make") && strcasestr(value, "Model"))
	LocalQueueNamingRemoteCUPS = LOCAL_QUEUE_NAMING_MAKE_MODEL;
      else if (strcasestr(value, "Remote") || strcasestr(value, "Name"))
	LocalQueueNamingRemoteCUPS = LOCAL_QUEUE_NAMING_REMOTE_NAME;
    } else if (!strcasecmp(line, "LocalQueueNamingIPPPrinter") && value) {
      if (strcasestr(value, "DNSSD") || strcasestr(value, "DNS-SD"))
	LocalQueueNamingIPPPrinter = LOCAL_QUEUE_NAMING_DNSSD;
      else if (strcasestr(value, "Make") && strcasestr(value, "Model"))
	LocalQueueNamingIPPPrinter = LOCAL_QUEUE_NAMING_MAKE_MODEL;
    } else if (!strcasecmp(line, "OnlyUnsupportedByCUPS") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	OnlyUnsupportedByCUPS = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	OnlyUnsupportedByCUPS = 0;
    } else if (!strcasecmp(line, "CreateRemoteRawPrinterQueues") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	CreateRemoteRawPrinterQueues = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	CreateRemoteRawPrinterQueues = 0;
    } else if (!strcasecmp(line, "CreateRemoteCUPSPrinterQueues") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	CreateRemoteCUPSPrinterQueues = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	CreateRemoteCUPSPrinterQueues = 0;
    } else if (!strcasecmp(line, "CreateIPPPrinterQueues") && value) {
      if (!strcasecmp(value, "all") ||
	  !strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	CreateIPPPrinterQueues = IPP_PRINTERS_ALL;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	CreateIPPPrinterQueues = IPP_PRINTERS_NO;
      else if (strcasestr(value, "local") || strcasestr(value, "usb"))
	CreateIPPPrinterQueues = IPP_PRINTERS_LOCAL_ONLY;
      else if (strcasestr(value, "driver") && strcasestr(value, "less"))
	CreateIPPPrinterQueues = IPP_PRINTERS_DRIVERLESS;
      else if (strcasestr(value, "every") || strcasestr(value, "pwg"))
	CreateIPPPrinterQueues = IPP_PRINTERS_PWGRASTER;
      else if (strcasestr(value, "apple") || strcasestr(value, "air"))
	CreateIPPPrinterQueues = IPP_PRINTERS_APPLERASTER;
      else if (strcasestr(value, "pclm") || strcasestr(value, "pcl-m"))
	CreateIPPPrinterQueues = IPP_PRINTERS_PCLM;
      else if (strcasestr(value, "pdf"))
	CreateIPPPrinterQueues = IPP_PRINTERS_PDF;
    } else if (!strcasecmp(line, "IPPPrinterQueueType") && value) {
      if (!strncasecmp(value, "Auto", 4))
	IPPPrinterQueueType = PPD_YES;
      else if (!strncasecmp(value, "PPD", 3))
	IPPPrinterQueueType = PPD_YES;
      else if (!strncasecmp(value, "NoPPD", 5))
	IPPPrinterQueueType = PPD_NO;
      else if (!strncasecmp(value, "Interface", 9))
	IPPPrinterQueueType = PPD_NO;
    } else if (!strcasecmp(line, "NewIPPPrinterQueuesShared") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	NewIPPPrinterQueuesShared = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	NewIPPPrinterQueuesShared = 0;
    } else if (!strcasecmp(line, "AutoClustering") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	AutoClustering = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	AutoClustering = 0;
    } else if (!strcasecmp(line, "Cluster") && value) {
      ptr = value;
      /* Skip white space */
      while (*ptr && isspace(*ptr)) ptr ++;
      /* Premature line end */
      if (!*ptr) goto cluster_fail;
      /* Find the local queue name for the cluster */
      start = ptr;
      while (*ptr && !isspace(*ptr) && *ptr != ':') ptr ++;
      if (*ptr) {
	/* Mark end of the local queue name */
	*ptr = '\0';
	/* Skip colon and white space until next word or line end */
	ptr ++;
	while (*ptr && (isspace(*ptr) || *ptr == ':')) ptr ++;
      }
      /* Empty queue name */
      if (strlen(start) <= 0)
	goto cluster_fail;
      /* Clean queue name */
      start = remove_bad_chars(start, 0);
      /* Check whether we have already a cluster with this name */
      for (cluster = cupsArrayFirst(clusters);
	   cluster;
	   cluster = cupsArrayNext(clusters))
	if (!strcasecmp(start, cluster->local_queue_name)) {
	  debug_printf("Duplicate cluster with queue name \"%s\".\n",
		       start);
	  cluster = NULL;
	  goto cluster_fail;
	}
      /* Create the new cluster definition */
      cluster = calloc (1, sizeof (cluster_t));
      if (!cluster) goto cluster_fail;
      cluster->local_queue_name = start;
      cluster->members = cupsArrayNew(NULL, NULL);
      if (!*ptr) {
	/* Only local queue name given, so assume this name as the only
	   member name (only remote queues with this name match) */
	cupsArrayAdd(cluster->members, remove_bad_chars(start, 2));
      } else {
	/* The rest of the line lists one or more member queue names */
	while (*ptr) {
	  start = ptr;
	  while (*ptr && !isspace(*ptr)) ptr ++;
	  if (*ptr) {
	    /* Mark end of the current word */
	    *ptr = '\0';
	    /* Skip white space until next word or line end */
	    ptr ++;
	    while (*ptr && isspace(*ptr)) ptr ++;
	  }
	  /* Add member queue name to the list */
	  if (strlen(start) > 0)
	    cupsArrayAdd(cluster->members, remove_bad_chars(start, 2));
	}
      }
      cupsArrayAdd (clusters, cluster);
      continue;
    cluster_fail:
      if (cluster) {
	if (cluster->local_queue_name)
	  free(cluster->local_queue_name);
	if (cluster->members) {
	  while ((ptr = cupsArrayFirst (cluster->members)) != NULL) {
	    cupsArrayRemove (cluster->members, ptr);
	    free (ptr);
	  }
	  cupsArrayDelete (cluster->members);
	}
	free(cluster);
      }
    } else if (!strcasecmp(line, "LoadBalancing") && value) {
      if (!strncasecmp(value, "QueueOnClient", 13))
	LoadBalancingType = QUEUE_ON_CLIENT;
      else if (!strncasecmp(value, "QueueOnServers", 14))
	LoadBalancingType = QUEUE_ON_SERVERS;
    } else if (!strcasecmp(line, "DefaultOptions") && value) {
      if (strlen(value) > 0)
	DefaultOptions = strdup(value);
    } else if (!strcasecmp(line, "AutoShutdown") && value) {
      char *p, *saveptr;
      p = strtok_r (value, delim, &saveptr);
      while (p) {
	if (!strcasecmp(p, "On") || !strcasecmp(p, "Yes") ||
	    !strcasecmp(p, "True") || !strcasecmp(p, "1")) {
	  autoshutdown = 1;
	  debug_printf("Turning on auto shutdown mode.\n");
	} else if (!strcasecmp(p, "Off") || !strcasecmp(p, "No") ||
	    !strcasecmp(p, "False") || !strcasecmp(p, "0")) {
	  autoshutdown = 0;
	  debug_printf("Turning off auto shutdown mode (permanent mode).\n");
	} else if (!strcasecmp(p, "avahi")) {
	  autoshutdown_avahi = 1;
	  debug_printf("Turning on auto shutdown control by appearing and disappearing of the Avahi server.\n");
	} else if (strcasecmp(p, "none"))
	  debug_printf("Unknown mode '%s'\n", p);
	p = strtok_r (NULL, delim, &saveptr);
      }
    } else if (!strcasecmp(line, "AutoShutdownTimeout") && value) {
      int t = atoi(value);
      if (t >= 0) {
	autoshutdown_timeout = t;
	debug_printf("Set auto shutdown timeout to %d sec.\n",
		     t);
      } else
	debug_printf("Invalid auto shutdown timeout value: %d\n",
		     t);
    } else if (!strcasecmp(line, "AutoShutdownOn") && value) {
      int success = 0;
      if (!strncasecmp(value, "no", 2)) {
	if (strcasestr(value + 2, "queue")) {
	  autoshutdown_on = NO_QUEUES;
	  success = 1;
	} else if (strcasestr(value + 2, "job")) {
	  autoshutdown_on = NO_JOBS;
	  success = 1;
	}
      }
      if (success)
	debug_printf("Set auto shutdown inactivity type to no %s.\n",
		     autoshutdown_on == NO_QUEUES ? "queues" : "jobs");
      else
	debug_printf("Invalid auto shutdown inactivity type value: %s\n",
		     value);
    }
#ifdef HAVE_LDAP
    else if (!strcasecmp(line, "BrowseLDAPBindDN") && value) {
      if (value[0] != '\0')
	BrowseLDAPBindDN = strdup(value);
    }
#  ifdef HAVE_LDAP_SSL
    else if (!strcasecmp(line, "BrowseLDAPCACertFile") && value) {
      if (value[0] != '\0')
	BrowseLDAPCACertFile = strdup(value);
    }
#  endif /* HAVE_LDAP_SSL */
    else if (!strcasecmp(line, "BrowseLDAPDN") && value) {
      if (value[0] != '\0')
	BrowseLDAPDN = strdup(value);
    } else if (!strcasecmp(line, "BrowseLDAPPassword") && value) {
      if (value[0] != '\0')
	BrowseLDAPPassword = strdup(value);
    } else if (!strcasecmp(line, "BrowseLDAPServer") && value) {
      if (value[0] != '\0')
	BrowseLDAPServer = strdup(value);
    } else if (!strcasecmp(line, "BrowseLDAPFilter") && value) {
      if (value[0] != '\0')
	BrowseLDAPFilter = strdup(value);
    }
#endif /* HAVE_LDAP */
  }

  if (browse_line_found == 0) {
    /* No "Browse..." lines at all */
    browseallow_all = 1;
    browse_order = ORDER_DENY_ALLOW;
    debug_printf("No \"Browse...\" line at all, accept all servers (\"BrowseOrder Deny,Allow\").\n");
  } else if (browse_order_line_found == 0) {
    /* No "BrowseOrder" line */
    if (browse_allow_line_found == 0) {
      /* Only "BrowseDeny" lines */
      browse_order = ORDER_DENY_ALLOW;
      debug_printf("No \"BrowseOrder\" line and only \"BrowseDeny\" lines, accept all except what matches the \"BrowseDeny\" lines  (\"BrowseOrder Deny,Allow\").\n");
    } else if (browse_deny_line_found == 0) {
      /* Only "BrowseAllow" lines */
      browse_order = ORDER_ALLOW_DENY;
      debug_printf("No \"BrowseOrder\" line and only \"BrowseAllow\" lines, deny all except what matches the \"BrowseAllow\" lines  (\"BrowseOrder Allow,Deny\").\n");
    } else {
      /* Default for "BrowseOrder" */
      browse_order = ORDER_DENY_ALLOW;
      debug_printf("No \"BrowseOrder\" line, use \"BrowseOrder Deny,Allow\" as default.\n");
    }
  }

  cupsFileClose(fp);
}

static void
defer_update_netifs (void)
{
  if (update_netifs_sourceid)
    g_source_remove (update_netifs_sourceid);

  update_netifs_sourceid = g_timeout_add_seconds (10, update_netifs, NULL);
}

static void
nm_properties_changed (GDBusProxy *proxy,
		       GVariant *changed_properties,
		       const gchar *const *invalidated_properties,
		       gpointer user_data)
{
  GVariantIter *iter;
  const gchar *key;
  GVariant *value;
  debug_printf("nm_properties_changed() in THREAD %ld\n", pthread_self());
  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
    if (!strcmp (key, "ActiveConnections")) {
      debug_printf ("NetworkManager ActiveConnections changed\n");
      defer_update_netifs ();
      break;
    }
  }

  g_variant_iter_free (iter);
}

static void
find_previous_queue (gpointer key,
		     gpointer value,
		     gpointer user_data)
{
  const char *name = key;
  const local_printer_t *printer = value;
  remote_printer_t *p;
  debug_printf("find_previous_queue() in THREAD %ld\n", pthread_self());
  if (printer->cups_browsed_controlled) {
    /* Queue found, add to our list */
    p = create_remote_printer_entry (name, "", "", printer->device_uri, "", "",
				     0, "", "", "", NULL, 0, 0, NULL, -1);
    if (p) {
      /* Mark as unconfirmed, if no Avahi report of this queue appears
	 in a certain time frame, we will remove the queue */
      p->status = STATUS_UNCONFIRMED;

      if (BrowseRemoteProtocols & BROWSE_CUPS)
	p->timeout = time(NULL) + BrowseTimeout;
      else
	p->timeout = time(NULL) + TIMEOUT_CONFIRM;

      p->slave_of = NULL;
      debug_printf("Found CUPS queue %s (URI: %s) from previous session.\n",
		   p->queue_name, p->uri);
    } else {
      debug_printf("ERROR: Unable to allocate memory.\n");
      exit(1);
    }
  }
}

int main(int argc, char*argv[]) {
  int ret = 1;
  http_t *http;
  int i;
  char *val;
  remote_printer_t *p;
  GDBusProxy *proxy = NULL;
  GError *error = NULL;
  int subscription_id = 0;

  /* Initialise the command_line_config array */
  command_line_config = cupsArrayNew(NULL, NULL);

  /* Initialise the browseallow array */
  browseallow = cupsArrayNew(NULL, NULL);

  /* Initialise the browsefilter array */
  browsefilter = cupsArrayNew(NULL, NULL);

  /* Initialise the clusters array */
  clusters = cupsArrayNew(NULL, NULL);

  /* Read command line options */
  if (argc >= 2) {
    for (i = 1; i < argc; i++)
      if (!strcasecmp(argv[i], "--debug") || !strcasecmp(argv[i], "-d") ||
	  !strncasecmp(argv[i], "-v", 2)) {
	/* Turn on debug output mode if requested */
	debug_stderr = 1;
	debug_printf("Reading command line option %s, turning on debug mode (Log on standard error).\n",
		     argv[i]);
      } else if (!strcasecmp(argv[i], "--logfile") ||
		 !strcasecmp(argv[i], "-l")) {
	/* Turn on debug log file mode if requested */
	if (debug_logfile == 0) {
	  debug_logfile = 1;
	  start_debug_logging();
	  debug_printf("Reading command line option %s, turning on debug mode (Log into log file %s).\n",
		       argv[i], debug_log_file);
	}
      } else if (!strncasecmp(argv[i], "-c", 2)) {
	/* Alternative configuration file */
	val = argv[i] + 2;
	if (strlen(val) == 0) {
	  i ++;
	  if (i < argc && *argv[i] != '-')
	    val = argv[i];
	  else
	    val = NULL;
	}
	if (val) {
	  alt_config_file = strdup(val);
	  debug_printf("Reading command line option -c %s, using alternative configuration file.\n",
		       alt_config_file);
	} else {
	  fprintf(stderr,
		  "Reading command line option -c, no alternative configuration file name supplied.\n\n");
	  goto help;
	}     
      } else if (!strncasecmp(argv[i], "-o", 2)) {
	/* Configuration option via command line */
	val = argv[i] + 2;
	if (strlen(val) == 0) {
	  i ++;
	  if (i < argc && *argv[i] != '-')
	    val = argv[i];
	  else
	    val = NULL;
	}
	if (val) {
	  cupsArrayAdd (command_line_config, strdup(val));
	  debug_printf("Reading command line option -o %s, applying extra configuration option.\n",
		 val);
	} else {
	  fprintf(stderr,
		  "Reading command line option -o, no extra configuration option supplied.\n\n");
	  goto help;
	}     
      } else if (!strncasecmp(argv[i], "--autoshutdown-timeout", 22)) {
	debug_printf("Reading command line: %s\n", argv[i]);
	if (argv[i][22] == '=' && argv[i][23])
	  val = argv[i] + 23;
	else if (!argv[i][22] && i < argc -1) {
	  i++;
	  debug_printf("Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "Expected auto shutdown timeout setting after \"--autoshutdown-timeout\" option.\n\n");
	  goto help;
	}
	int t = atoi(val);
	if (t >= 0) {
	  autoshutdown_timeout = t;
	  debug_printf("Set auto shutdown timeout to %d sec.\n",
		       t);
	} else {
	  fprintf(stderr, "Invalid auto shutdown timeout value: %d\n\n",
		  t);
	  goto help;
	}
      } else if (!strncasecmp(argv[i], "--autoshutdown-on", 17)) {
	debug_printf("Reading command line: %s\n", argv[i]);
	if (argv[i][17] == '=' && argv[i][18])
	  val = argv[i] + 18;
	else if (!argv[i][17] && i < argc - 1) {
	  i++;
	  debug_printf("Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "Expected auto shutdown inactivity type (\"no-queues\" or \"no-jobs\") after \"--autoshutdown-on\" option.\n\n");
	  goto help;
	}
	int success = 0;
	if (!strncasecmp(val, "no", 2)) {
	  if (strcasestr(val + 2, "queue")) {
	    autoshutdown_on = NO_QUEUES;
	    success = 1;
	  } else if (strcasestr(val + 2, "job")) {
	    autoshutdown_on = NO_JOBS;
	    success = 1;
	  }
	}
	if (success)
	  debug_printf("Set auto shutdown inactivity type to no %s.\n",
		       autoshutdown_on == NO_QUEUES ? "queues" : "jobs");
	else
	  debug_printf("Invalid auto shutdown inactivity type value: %s\n",
		       val);
      } else if (!strncasecmp(argv[i], "--autoshutdown", 14)) {
	debug_printf("Reading command line: %s\n", argv[i]);
	if (argv[i][14] == '=' && argv[i][15])
	  val = argv[i] + 15;
	else if (!argv[i][14] && i < argc -1) {
	  i++;
	  debug_printf("Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "Expected auto shutdown setting after \"--autoshutdown\" option.\n\n");
	  goto help;
	}
	if (!strcasecmp(val, "On") || !strcasecmp(val, "Yes") ||
	    !strcasecmp(val, "True") || !strcasecmp(val, "1")) {
	  autoshutdown = 1;
	  debug_printf("Turning on auto shutdown mode.\n");
	} else if (!strcasecmp(val, "Off") || !strcasecmp(val, "No") ||
	    !strcasecmp(val, "False") || !strcasecmp(val, "0")) {
	  autoshutdown = 0;
	  debug_printf("Turning off auto shutdown mode (permanent mode).\n");
	} else if (!strcasecmp(val, "avahi")) {
	  autoshutdown_avahi = 1;
	  debug_printf("Turning on auto shutdown control by appearing and disappearing of the Avahi server.\n");
	} else if (strcasecmp(val, "none")) {
	  fprintf(stderr, "Unknown mode '%s'\n\n", val);
	  goto help;
	}
      } else if (!strcasecmp(argv[i], "--version") ||
		 !strcasecmp(argv[i], "--help") || !strcasecmp(argv[i], "-h")) {
	/* Help!! */
	goto help;
      } else {
	/* Unknown option */
	fprintf(stderr,
		"Reading command line option %s, unknown command line option.\n\n",
		argv[i]);
        goto help;
      }
  }

  debug_printf("cups-browsed of cups-filters version "VERSION" starting.\n");
  
  /* Read in cups-browsed.conf */
  read_configuration (alt_config_file);

  /* Set the paths of the auxiliary files */
  if (cachedir[0] == '\0')
    strncpy(cachedir, DEFAULT_CACHEDIR, sizeof(cachedir) - 1);
  if (logdir[0] == '\0')
    strncpy(logdir, DEFAULT_LOGDIR, sizeof(logdir) - 1);
  strncpy(local_default_printer_file, cachedir,
	  sizeof(local_default_printer_file) - 1);
  strncpy(local_default_printer_file + strlen(cachedir),
	  LOCAL_DEFAULT_PRINTER_FILE,
	  sizeof(local_default_printer_file) - strlen(cachedir) - 1);
  strncpy(remote_default_printer_file, cachedir,
	  sizeof(remote_default_printer_file) - 1);
  strncpy(remote_default_printer_file + strlen(cachedir),
	  REMOTE_DEFAULT_PRINTER_FILE,
	  sizeof(remote_default_printer_file) - strlen(cachedir) - 1);
  strncpy(save_options_file, cachedir,
	  sizeof(save_options_file) - 1);
  strncpy(save_options_file + strlen(cachedir),
	  SAVE_OPTIONS_FILE,
	  sizeof(save_options_file) - strlen(cachedir) - 1);
  strncpy(debug_log_file, logdir,
	  sizeof(debug_log_file) - 1);
  strncpy(debug_log_file + strlen(logdir),
	  DEBUG_LOG_FILE,
	  sizeof(debug_log_file) - strlen(logdir) - 1);
  if (debug_logfile == 1)
    start_debug_logging();

  debug_printf("main() in THREAD %ld\n", pthread_self());

  /* Point to selected CUPS server or domain socket via the CUPS_SERVER
     environment variable or DomainSocket configuration file option.
     Default to localhost:631 (and not to CUPS default to override
     client.conf files as cups-browsed works only with a local CUPS
     daemon, not with remote ones. */
  if (getenv("CUPS_SERVER") != NULL) {
    strncpy(local_server_str, getenv("CUPS_SERVER"), sizeof(local_server_str));
  } else {
#ifdef CUPS_DEFAULT_DOMAINSOCKET
    if (DomainSocket == NULL)
      DomainSocket = CUPS_DEFAULT_DOMAINSOCKET;
#endif
    if (DomainSocket != NULL) {
      struct stat sockinfo;               /* Domain socket information */
      if (strcasecmp(DomainSocket, "None") != 0 &&
	  strcasecmp(DomainSocket, "Off") != 0 &&
	  !stat(DomainSocket, &sockinfo) &&
	  (sockinfo.st_mode & S_IROTH) != 0 &&
	  (sockinfo.st_mode & S_IWOTH) != 0)
	strncpy(local_server_str, DomainSocket, sizeof(local_server_str));
      else
	strncpy(local_server_str, "localhost:631", sizeof(local_server_str));
    } else
      strncpy(local_server_str, "localhost:631", sizeof(local_server_str));
    setenv("CUPS_SERVER", local_server_str, 1);
  }
  cupsSetServer(local_server_str);
  BrowsePort = ippPort();

  if (BrowseLocalProtocols & BROWSE_DNSSD) {
    debug_printf("Local support for DNSSD not implemented\n");
    BrowseLocalProtocols &= ~BROWSE_DNSSD;
  }

  if (BrowseLocalProtocols & BROWSE_LDAP) {
    debug_printf("Local support for LDAP not implemented\n");
    BrowseLocalProtocols &= ~BROWSE_LDAP;
  }

#ifndef HAVE_AVAHI
  if (BrowseRemoteProtocols & BROWSE_DNSSD) {
    debug_printf("Remote support for DNSSD not supported\n");
    BrowseRemoteProtocols &= ~BROWSE_DNSSD;
  }
#endif /* HAVE_AVAHI */

#ifndef HAVE_LDAP
  if (BrowseRemoteProtocols & BROWSE_LDAP) {
    debug_printf("Remote support for LDAP not supported\n");
    BrowseRemoteProtocols &= ~BROWSE_LDAP;
  }
#endif /* HAVE_LDAP */

  /* Wait for CUPS daemon to start */
  while ((http = http_connect_local ()) == NULL)
    sleep(1);

  /* Initialise the array of network interfaces */
  netifs = cupsArrayNew(NULL, NULL);
  update_netifs (NULL);

  local_printers = g_hash_table_new_full (g_str_hash,
					  g_str_equal,
					  g_free,
					  free_local_printer);
  cups_supported_remote_printers = g_hash_table_new_full (g_str_hash,
							  g_str_equal,
							  g_free,
							  free_local_printer);

  /* Read out the currently defined CUPS queues and find the ones which we
     have added in an earlier session */
  update_local_printers ();
  if ((val = get_cups_default_printer()) != NULL) {
    default_printer = strdup(val);
    free(val);
  }
  remote_printers = cupsArrayNew(NULL, NULL);
  g_hash_table_foreach (local_printers, find_previous_queue, NULL);

  /* Redirect SIGINT and SIGTERM so that we do a proper shutdown, removing
     the CUPS queues which we have created
     Use SIGUSR1 and SIGUSR2 to turn off and turn on auto shutdown mode
     resp. */
#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, sigterm_handler);
  sigset(SIGINT, sigterm_handler);
  sigset(SIGUSR1, sigusr1_handler);
  sigset(SIGUSR2, sigusr2_handler);
  debug_printf("Using signal handler SIGSET\n");
#elif defined(HAVE_SIGACTION)
  struct sigaction action; /* Actions for POSIX signals */
  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGTERM);
  action.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &action, NULL);
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGINT);
  action.sa_handler = sigterm_handler;
  sigaction(SIGINT, &action, NULL);
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGUSR1);
  action.sa_handler = sigusr1_handler;
  sigaction(SIGUSR1, &action, NULL);
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGUSR2);
  action.sa_handler = sigusr2_handler;
  sigaction(SIGUSR2, &action, NULL);
  debug_printf("Using signal handler SIGACTION\n");
#else
  signal(SIGTERM, sigterm_handler);
  signal(SIGINT, sigterm_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);
  debug_printf("Using signal handler SIGNAL\n");
#endif /* HAVE_SIGSET */

#ifdef HAVE_AVAHI
  if (autoshutdown_avahi)
    autoshutdown = 1;
  avahi_init();
#endif /* HAVE_AVAHI */

  if (autoshutdown == 1) {
    /* If there are no printers or no jobs schedule the shutdown in
       autoshutdown_timeout seconds */
    if (!autoshutdown_exec_id &&
	(cupsArrayCount(remote_printers) == 0 ||
	 (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
      debug_printf ("We set auto shutdown mode and no printers are there to make available or no jobs on them, shutting down in %d sec...\n", autoshutdown_timeout);
      autoshutdown_exec_id =
	g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
			       NULL);
    }
  }
  
  if (BrowseLocalProtocols & BROWSE_CUPS ||
      BrowseRemoteProtocols & BROWSE_CUPS) {
    /* Set up our CUPS Browsing socket */
    browsesocket = socket (AF_INET, SOCK_DGRAM, 0);
    if (browsesocket == -1) {
      debug_printf("failed to create CUPS Browsing socket: %s\n",
		   strerror (errno));
    } else {
      struct sockaddr_in addr;
      memset (&addr, 0, sizeof (addr));
      addr.sin_addr.s_addr = htonl (INADDR_ANY);
      addr.sin_family = AF_INET;
      addr.sin_port = htons (BrowsePort);
      if (bind (browsesocket, (struct sockaddr *)&addr, sizeof (addr))) {
	debug_printf("failed to bind CUPS Browsing socket: %s\n",
		     strerror (errno));
	close (browsesocket);
	browsesocket = -1;
      } else {
	int on = 1;
	if (setsockopt (browsesocket, SOL_SOCKET, SO_BROADCAST,
			&on, sizeof (on))) {
	  debug_printf("failed to allow broadcast: %s\n",
		       strerror (errno));
	  BrowseLocalProtocols &= ~BROWSE_CUPS;
	}
      }
    }

    if (browsesocket == -1) {
      BrowseLocalProtocols &= ~BROWSE_CUPS;
      BrowseRemoteProtocols &= ~BROWSE_CUPS;
    }
  }

  if (BrowseLocalProtocols == 0 &&
      BrowseRemoteProtocols == 0 &&
      !BrowsePoll) {
    debug_printf("nothing left to do\n");
    ret = 0;
    goto fail;
  }

  /* Override the default password callback so we don't end up
   * prompting for it. */
  cupsSetPasswordCB2 (password_callback, NULL);

  /* Watch NetworkManager for network interface changes */
  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					 G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
					 NULL, /* GDBusInterfaceInfo */
					 "org.freedesktop.NetworkManager",
					 "/org/freedesktop/NetworkManager",
					 "org.freedesktop.NetworkManager",
					 NULL, /* GCancellable */
					 NULL); /* GError */

  if (proxy)
    g_signal_connect (proxy,
		      "g-properties-changed",
		      G_CALLBACK (nm_properties_changed),
		      NULL);

  /* Run the main loop */
  gmainloop = g_main_loop_new (NULL, FALSE);
  recheck_timer ();

  if (BrowseRemoteProtocols & BROWSE_CUPS) {
    GIOChannel *browse_channel = g_io_channel_unix_new (browsesocket);
    g_io_channel_set_close_on_unref (browse_channel, FALSE);
    g_io_add_watch (browse_channel, G_IO_IN, process_browse_data, NULL);
  }

  if (BrowseLocalProtocols & BROWSE_CUPS) {
      debug_printf ("will send browse data every %ds\n",
		    BrowseInterval);
      g_idle_add (send_browse_data, NULL);
  }

#ifdef HAVE_LDAP
  if (BrowseRemoteProtocols & BROWSE_LDAP) {
      debug_printf ("will browse poll LDAP every %ds\n",
		    BrowseInterval);
      g_idle_add (browse_ldap_poll, NULL);
  }
#endif /* HAVE_LDAP */

  if (BrowsePoll) {
    size_t index;
    for (index = 0;
	 index < NumBrowsePoll;
	 index++) {
      debug_printf ("will browse poll %s every %ds\n",
		    BrowsePoll[index]->server, BrowseInterval);
      g_idle_add (browse_poll, BrowsePoll[index]);
    }
  }

  /* Subscribe to CUPS' D-Bus notifications and create a proxy to receive
     the notifications */
  subscription_id = create_subscription ();
  g_timeout_add_seconds (NOTIFY_LEASE_DURATION - 60,
			 renew_subscription_timeout,
			 &subscription_id);
  cups_notifier = cups_notifier_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
							0,
							NULL,
							CUPS_DBUS_PATH,
							NULL,
							&error);
  if (error) {
    fprintf (stderr, "Error creating cups notify handler: %s", error->message);
    g_error_free (error);
    cups_notifier = NULL;
  }
  if (cups_notifier != NULL) {
    g_signal_connect (cups_notifier, "printer-state-changed",
		      G_CALLBACK (on_printer_state_changed), NULL);
    g_signal_connect (cups_notifier, "printer-deleted",
		      G_CALLBACK (on_printer_deleted), NULL);
    g_signal_connect (cups_notifier, "printer-modified",
		      G_CALLBACK (on_printer_modified), NULL);
  }

  /* If auto shutdown is active and we do not find any printers initially,
     schedule the shutdown in autoshutdown_timeout seconds */
  if (autoshutdown && !autoshutdown_exec_id &&
      cupsArrayCount(remote_printers) == 0) {
    debug_printf ("No printers found to make available, shutting down in %d sec...\n", autoshutdown_timeout);
    autoshutdown_exec_id =
      g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute, NULL);
  }

  g_main_loop_run (gmainloop);

  debug_printf("main loop exited\n");
  g_main_loop_unref (gmainloop);
  gmainloop = NULL;
  ret = 0;

fail:

  /* Clean up things */

  in_shutdown = 1;
  
  if (proxy)
    g_object_unref (proxy);

  /* Remove all queues which we have set up */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {
    p->status = STATUS_DISAPPEARED;
    p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
  }
  update_cups_queues(NULL);

  cancel_subscription (subscription_id);
  if (cups_notifier)
    g_object_unref (cups_notifier);

  if (BrowsePoll) {
    size_t index;
    for (index = 0;
	 index < NumBrowsePoll;
	 index++) {
      if (BrowsePoll[index]->can_subscribe &&
	  BrowsePoll[index]->subscription_id != -1)
	browse_poll_cancel_subscription (BrowsePoll[index]);

      free (BrowsePoll[index]->server);
      g_list_free_full (BrowsePoll[index]->printers,
			browsepoll_printer_free);
      free (BrowsePoll[index]);
    }

    free (BrowsePoll);
  }

  if (local_printers_context) {
    browse_poll_cancel_subscription (local_printers_context);
    g_list_free_full (local_printers_context->printers,
		      browsepoll_printer_free);
    free (local_printers_context);
  }

  http_close_local ();

#ifdef HAVE_AVAHI
  avahi_shutdown();
#endif /* HAVE_AVAHI */

#ifdef HAVE_LDAP
  if (((BrowseLocalProtocols | BrowseRemoteProtocols) & BROWSE_LDAP) &&
      BrowseLDAPHandle)
  {
    ldap_disconnect(BrowseLDAPHandle);
    BrowseLDAPHandle = NULL;
  }
#endif /* HAVE_LDAP */

  if (browsesocket != -1)
    close (browsesocket);

  g_hash_table_destroy (local_printers);
  g_hash_table_destroy (cups_supported_remote_printers);

  if (BrowseLocalProtocols & BROWSE_CUPS)
    g_list_free_full (browse_data, browse_data_free);

  /* Close log file if we have one */
  if (debug_logfile == 1)
    stop_debug_logging();

  return ret;

 help:

  fprintf(stderr,
	  "cups-browsed of cups-filters version "VERSION"\n\n"
	  "Usage: cups-browsed [options]\n"
	  "Options:\n"
	  "  -c cups-browsed.conf    Set alternative cups-browsed.conf file to use.\n"
	  "  -d\n"
	  "  -v\n"
	  "  --debug                 Run in debug mode (logging to stderr).\n"
	  "  -l\n"
	  "  --logfile               Run in debug mode (logging into file).\n"
	  "  -h\n"
	  "  --help\n"
	  "  --version               Show this usage message.\n"
	  "  -o Option=Value         Supply configuration option via command line,\n"
	  "                          options are the same as in cups-browsed.conf.\n"
	  "  --autoshutdown=<mode>   Automatically shut down cups-browsed when inactive:\n"
	  "                          <mode> can be set to Off, On, or avahi, where Off\n"
	  "                          means that cups-browsed stays running permanently\n"
	  "                          (default), On means that it shuts down after 30\n"
	  "                          seconds (or any given timeout) of inactivity, and\n"
	  "                          avahi means that cups-browsed shuts down when\n"
	  "                          avahi-daemon shuts down.\n"
	  "  --autoshutdown-timout=<time> Timeout (in seconds) for auto-shutdown.\n"
	  "  --autoshutdown-on=<type> Type of inactivity which leads to an auto-\n"
	  "                          shutdown: If <type> is \"no-queues\", the shutdown\n"
	  "                          is triggered by not having any cups-browsed-created\n"
	  "                          print queue any more. With <type> being \"no-jobs\"\n"
	  "                          shutdown is initiated by no job being printed\n"
	  "                          on any cups-browsed-generated print queue any more.\n"
	  "                          \"no-queues\" is the default.\n"
	  );

  return 1;
}


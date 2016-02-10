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

#include "cups-notifier.h"

/* Attribute to mark a CUPS queue as created by us */
#define CUPS_BROWSED_MARK "cups-browsed"

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

#define LOCAL_DEFAULT_PRINTER_FILE "/var/cache/cups/cups-browsed-local-default-printer"
#define REMOTE_DEFAULT_PRINTER_FILE "/var/cache/cups/cups-browsed-remote-default-printer"
#define SAVE_OPTIONS_FILE "/var/cache/cups/cups-browsed-options-%s"
#define IMPLICIT_CLASS_DEST_HOST_FILE "/var/cache/cups/cups-browsed-dest-host-%s"

/* Status of remote printer */
typedef enum printer_status_e {
  STATUS_UNCONFIRMED = 0,	/* Generated in a previous session */
  STATUS_CONFIRMED,		/* Avahi confirms UNCONFIRMED printer */
  STATUS_TO_BE_CREATED,		/* Scheduled for creation */
  STATUS_BROWSE_PACKET_RECEIVED,/* Scheduled for creation with timeout */
  STATUS_DISAPPEARED		/* Scheduled for removal */
} printer_status_t;

/* Data structure for remote printers */
typedef struct remote_printer_s {
  char *name;
  char *uri;
  char *ppd;
  char *model;
  char *ifscript;
  int num_options;
  cups_option_t *options;
  printer_status_t status;
  time_t timeout;
  void *duplicate_of;
  int num_duplicates;
  int last_printer;
  char *host;
  char *ip;
  char *service_name;
  char *type;
  char *domain;
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

/* Data struct for a printer discovered using BrowsePoll */
typedef struct browsepoll_printer_s {
  char *uri_supported;
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

/* Ways how to represent the remote printer's IP in the device URI */
typedef enum ip_based_uris_e {
  IP_BASED_URIS_NO,
  IP_BASED_URIS_ANY,
  IP_BASED_URIS_IPV4_ONLY,
  IP_BASED_URIS_IPV6_ONLY
} ip_based_uris_t;

/* Ways how to set up a queue for an IPP network printer */
typedef enum ipp_queue_type_e {
  PPD_AUTO,
  PPD_ONLY,
  PPD_NEVER
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
static char *DomainSocket = NULL;
static ip_based_uris_t IPBasedDeviceURIs = IP_BASED_URIS_NO;
static unsigned int CreateRemoteRawPrinterQueues = 0;
static unsigned int CreateIPPPrinterQueues = 0;
static ipp_queue_type_t IPPPrinterQueueType = PPD_AUTO;
static load_balancing_type_t LoadBalancingType = QUEUE_ON_CLIENT;
static const char *DefaultOptions = NULL;
static int autoshutdown = 0;
static int autoshutdown_avahi = 0;
static int autoshutdown_timeout = 30;
static autoshutdown_inactivity_type_t autoshutdown_on = NO_QUEUES;
static guint autoshutdown_exec_id = 0;
static const char *default_printer = NULL;

static int debug = 0;

static void recheck_timer (void);
static void browse_poll_create_subscription (browsepoll_t *context,
					     http_t *conn);
static gboolean browse_poll_get_notifications (browsepoll_t *context,
					       http_t *conn);
static remote_printer_t *generate_local_queue(const char *host,
					      const char *ip,
					      uint16_t port,
					      char *resource, const char *name,
					      const char *type,
					      const char *domain, void *txt);

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

#ifdef HAVE_CUPS_1_6
/* The following function uses a lot of CUPS >= 1.6 specific stuff.
   The following function is only called in create_local_queue()
   to set up local queues for non-CUPS printer broadcasts
   that is disabled in create_local_queue() for older CUPS <= 1.5.4.
   Accordingly the following function is also disabled here for CUPS < 1.6. */
char            *_ppdCreateFromIPP(char *buffer, size_t bufsize, ipp_t *response);
#endif /* HAVE_CUPS_1_6 */

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

void debug_printf(const char *format, ...) {
  if (debug) {
    va_list arglist;
    va_start(arglist, format);
    vfprintf(stderr, format, arglist);
    fflush(stderr);
    va_end(arglist);
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

static http_t *
http_connect_local (void)
{
  if (!local_conn)
    local_conn = httpConnectEncrypt(cupsServer(), ippPort(),
				    cupsEncryption());

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
  return g_str_equal (printer->device_uri, device_uri);
}

static void
local_printers_create_subscription (http_t *conn)
{
  if (!local_printers_context) {
    local_printers_context = g_malloc0 (sizeof (browsepoll_t));
    local_printers_context->server = "localhost";
    local_printers_context->port = BrowsePort;
    local_printers_context->can_subscribe = TRUE;
  }

  browse_poll_create_subscription (local_printers_context, conn);
}

static void
get_local_printers (void)
{
  cups_dest_t *dests = NULL;
  int num_dests = cupsGetDests (&dests);
  debug_printf ("cups-browsed [BrowsePoll localhost:631]: cupsGetDests\n");
  g_hash_table_remove_all (local_printers);
  for (int i = 0; i < num_dests; i++) {
    const char *val;
    cups_dest_t *dest = &dests[i];
    local_printer_t *printer;
    gboolean cups_browsed_controlled;
    const char *device_uri = cupsGetOption ("device-uri",
					    dest->num_options,
					    dest->options);
    val = cupsGetOption (CUPS_BROWSED_MARK,
			 dest->num_options,
			 dest->options);
    cups_browsed_controlled = val && (!strcasecmp (val, "yes") ||
				      !strcasecmp (val, "on") ||
				      !strcasecmp (val, "true"));
    printer = new_local_printer (device_uri,
				 cups_browsed_controlled);
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
    debug_printf("cups-browsed: browse send failed to connect to localhost\n");
    goto fail;
  }

  request = ippNewRequest(CUPS_GET_PRINTERS);
  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes", sizeof (rattrs) / sizeof (rattrs[0]),
		 NULL, rattrs);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());

  debug_printf("cups-browsed: preparing browse data\n");
  response = cupsDoRequest (conn, request, "/");
  if (cupsLastError() > IPP_OK_CONFLICT) {
    debug_printf("cups-browsed: browse send failed for localhost: %s\n",
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
	  debug_printf("cups-browsed: skipping %s (%d)\n", name, value_tag);
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

  if (cupsArrayCount(remote_printers) > 0)
    for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 p;
	 p = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!p->duplicate_of) {
	num_jobs = cupsGetJobs2(CUPS_HTTP_DEFAULT, &jobs, p->name, 0,
				CUPS_WHICHJOBS_ACTIVE);
	if (num_jobs > 0) {
	  debug_printf("cups-browsed: Queue %s still has jobs!\n", p->name);
	  cupsFreeJobs(num_jobs, jobs);
	  return 1;
	}
      }

  debug_printf("cups-browsed: All our remote printers are without jobs.\n");
  return 0;
}

gboolean
autoshutdown_execute (gpointer data)
{
  /* Are we still in auto shutdown mode and are we still without queues or
     jobs*/
  if (autoshutdown &&
      (cupsArrayCount(remote_printers) == 0 ||
       (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
    debug_printf("cups-browsed: Automatic shutdown as there are no print queues maintained by us or no jobs on them for %d sec.\n",
		 autoshutdown_timeout);
    g_main_loop_quit(gmainloop);
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

  debug_printf("cups-browsed: ldap_rebind_proc: Rebind to %s\n", refsp);

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

        debug_printf("cups-browsed: ldap_rebind_proc: Free values...\n");

        if (dnp && *dnp)
          free(*dnp);

        if (passwdp && *passwdp)
          free(*passwdp);
        break;

    case 0:
       /*
        * Return credentials for LDAP referal...
        */

        debug_printf("cups-browsed: "
                        "ldap_rebind_proc: Return necessary values...\n");

        *dnp         = strdup(BrowseLDAPBindDN);
        *passwdp     = strdup(BrowseLDAPPassword);
        *authmethodp = LDAP_AUTH_SIMPLE;
        break;

    default:
       /*
        * Should never happen...
        */

        fprintf(stderr, "cups-browsed: "
                        "LDAP rebind has been called with wrong freeit value!\n");
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
    debug_printf("cups-browsed: "
	            "ldap_connect: Setting CA certificate file \"%s\"\n",
                    BrowseLDAPCACertFile);

    if ((rc = ldap_set_option(NULL, LDAP_OPT_X_TLS_CACERTFILE,
	                      (void *)BrowseLDAPCACertFile)) != LDAP_SUCCESS)
      fprintf(stderr, "cups-browsed: "
                      "Unable to set CA certificate file for LDAP "
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
    fprintf(stderr, "cups-browsed: BrowseLDAPServer not configured!\n");
    fprintf(stderr, "cups-browsed: Disabling LDAP browsing!\n");
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
    fprintf(stderr, "cups-browsed: Unrecognized LDAP protocol (%s)!\n",
                    ldap_protocol);
    fprintf(stderr, "cups-browsed: Disabling LDAP browsing!\n");
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

  debug_printf("cups-browsed: ldap_connect: PROT:%s HOST:%s PORT:%d\n",
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
        fprintf(stderr, "cups-browsed: "
                        "Failed to initialize LDAP SSL client!\n");
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
      fprintf(stderr, "cups-browsed: "
                      "LDAP SSL certificate file/database not configured!\n");
      rc = LDAP_OPERATIONS_ERROR;
    }

#    else /* HAVE_LDAP_SSL */

   /*
    * Return error, because client libraries doesn't support SSL
    */

    fprintf(stderr, "cups-browsed: "
                    "LDAP client libraries do not support SSL\n");
    rc = LDAP_OPERATIONS_ERROR;

#    endif /* HAVE_LDAP_SSL */
  }
#  endif /* HAVE_OPENLDAP */

 /*
  * Check return code from LDAP initialize...
  */

  if (rc != LDAP_SUCCESS)
  {
    fprintf(stderr, "cups-browsed: Unable to initialize LDAP!\n");

    if (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR)
      fprintf(stderr, "cups-browsed: Temporarily disabling LDAP browsing...\n");
    else
    {
      fprintf(stderr, "cups-browsed: Disabling LDAP browsing!\n");

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
    fprintf(stderr, "cups-browsed: Unable to set LDAP protocol version %d!\n",
                   version);
    fprintf(stderr, "cups-browsed: Disabling LDAP browsing!\n");

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
    fprintf(stderr, "cups-browsed: "
                    "Setting LDAP rebind function failed with status %d: %s\n",
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
    fprintf(stderr, "cups-browsed: LDAP bind failed with error %d: %s\n",
                    rc, ldap_err2string(rc));

#  if defined(HAVE_LDAP_SSL) && defined (HAVE_MOZILLA_LDAP)
    if (ldap_ssl && (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR))
    {
      ssl_err = PORT_GetError();
      if (ssl_err != 0)
        fprintf(stderr, "cups-browsed: LDAP SSL error %d: %s\n", ssl_err,
                        ldapssl_err2string(ssl_err));
    }
#  endif /* defined(HAVE_LDAP_SSL) && defined (HAVE_MOZILLA_LDAP) */

    ldap_disconnect(TempBrowseLDAPHandle);

    return (NULL);
  }

  debug_printf("cups-browsed: LDAP connection established\n");

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

  debug_printf("cups-browsed: Try LDAP reconnect...\n");

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
    fprintf(stderr, "cups-browsed: "
                    "Unbind from LDAP server failed with status %d: %s\n",
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
		location[1024],		/* Printer location */
		info[1024],		/* Printer information */
		make_model[1024],	/* Printer make and model */
		type_num[30],		/* Printer type number */
		scheme[32],		/* URI's scheme */
		username[64];		/* URI's username */
  int port;				/* URI's port number */
  char *c;
  int		rc;			/* LDAP status */
  int		limit;			/* Size limit */
  LDAPMessage	*res,			/* LDAP search results */
		  *e;			/* Current entry from search */

  debug_printf("cups-browsed: UpdateLDAPBrowse\n");

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
      debug_printf("cups-browsed: "
                      "LDAP update temporary disabled\n");
    }
    return;
  }

 /*
  * If LDAP updates were disabled, we will reenable them...
  */

  if (! BrowseLDAPUpdate)
  {
    BrowseLDAPUpdate = TRUE;
    debug_printf("cups-browsed: "
                    "LDAP update enabled\n");
  }

 /*
  * Count LDAP entries and return if no entry exist...
  */

  limit = ldap_count_entries(BrowseLDAPHandle, res);
  debug_printf("cups-browsed: LDAP search returned %d entries\n", limit);
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
      debug_printf("cups-browsed: don't understand URI: %s\n", uri);
      return;
    }

    strncpy (local_resource, resource + 1, sizeof (local_resource) - 1);
    local_resource[sizeof (local_resource) - 1] = '\0';
    c = strchr (local_resource, '?');
    if (c)
      *c = '\0';

    debug_printf("cups-browsed: browsed LDAP queue name is %s\n",
		 local_resource + 9);

    generate_local_queue(host, NULL, port, local_resource, info, "", "", NULL);

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
    fprintf(stderr, "cups-browsed: "
                    "LDAP search failed with status %d: %s\n",
                     rc, ldap_err2string(rc));
    debug_printf("cups-browsed: "
                    "We try the LDAP search once again after reconnecting to "
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
    debug_printf("cups-browsed: "
                    "ldap_search_rec: LDAP entry/object not found\n");
  else if (rc != LDAP_SUCCESS)
    fprintf(stderr, "cups-browsed: "
                    "ldap_search_rec: LDAP search failed with status %d: %s\n",
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
    fprintf(stderr, "cups-browsed: Can't free LDAPMessage!\n");
  else if (rc == 0)
    debug_printf("cups-browsed: Freeing LDAPMessage was unnecessary\n");
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
    fprintf(stderr, "cups-browsed: "
                    "Failed to get LDAP value %s for %s!\n",
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
      fprintf(stderr, "cups-browsed: "
                      "Attribute %s is too big! (dn: %s)\n",
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
    fprintf(stderr, "cups-browsed: Failed to get LDAP value %s for %s!\n",
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

  req = ippNewRequest (IPP_CREATE_PRINTER_SUBSCRIPTION);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
		"notify-events", NULL, "all");
  ippAddString (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
		"notify-recipient-uri", NULL, "dbus://");
  ippAddInteger (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		 "notify-lease-duration", NOTIFY_LEASE_DURATION);

  resp = cupsDoRequest (CUPS_HTTP_DEFAULT, req, "/");
  if (!resp || cupsLastError() != IPP_OK) {
    debug_printf ("cups-browsed: Error subscribing to CUPS notifications: %s\n",
		  cupsLastErrorString ());
    return 0;
  }

  attr = ippFindAttribute (resp, "notify-subscription-id", IPP_TAG_INTEGER);
  if (attr)
    id = ippGetInteger (attr, 0);
  else
    debug_printf ("cups-browsed: "
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

  req = ippNewRequest (IPP_RENEW_SUBSCRIPTION);
  ippAddInteger (req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-id", id);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
		"notify-recipient-uri", NULL, "dbus://");
  ippAddInteger (req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		 "notify-lease-duration", NOTIFY_LEASE_DURATION);

  resp = cupsDoRequest (CUPS_HTTP_DEFAULT, req, "/");
  if (!resp || cupsLastError() != IPP_OK) {
    debug_printf ("cups-browsed: Error renewing CUPS subscription %d: %s\n",
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

  if (*subscription_id <= 0 || !renew_subscription (*subscription_id))
    *subscription_id = create_subscription ();

  return TRUE;
}


void
cancel_subscription (int id)
{
  ipp_t *req;
  ipp_t *resp;

  if (id <= 0)
    return;

  req = ippNewRequest (IPP_CANCEL_SUBSCRIPTION);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddInteger (req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-id", id);

  resp = cupsDoRequest (CUPS_HTTP_DEFAULT, req, "/");
  if (!resp || cupsLastError() != IPP_OK) {
    debug_printf ("cups-browsed: Error subscribing to CUPS notifications: %s\n",
		  cupsLastErrorString ());
    return;
  }

  ippDelete (resp);
}

const char*
is_disabled(const char *printer, const char *reason) {
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *pname = NULL;
  ipp_pstate_t pstate = IPP_PRINTER_IDLE;
  const char *pstatemsg = NULL;
  static const char *pattrs[] =
                {
                  "printer-name",
                  "printer-state",
		  "printer-state-message"
                };

  request = ippNewRequest(CUPS_GET_PRINTERS);
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		"requested-attributes",
		sizeof(pattrs) / sizeof(pattrs[0]),
		NULL, pattrs);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	       "requesting-user-name",
	       NULL, cupsUser());
  if ((response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/")) != NULL) {
    for (attr = ippFirstAttribute(response); attr != NULL;
	 attr = ippNextAttribute(response)) {
      while (attr != NULL && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
	attr = ippNextAttribute(response);
      if (attr == NULL)
	break;
      pname = NULL;
      pstate = IPP_PRINTER_IDLE;
      pstatemsg = NULL;
      while (attr != NULL && ippGetGroupTag(attr) ==
	     IPP_TAG_PRINTER) {
	if (!strcmp(ippGetName(attr), "printer-name") &&
	    ippGetValueTag(attr) == IPP_TAG_NAME)
	  pname = ippGetString(attr, 0, NULL);
	else if (!strcmp(ippGetName(attr), "printer-state") &&
		 ippGetValueTag(attr) == IPP_TAG_ENUM)
	  pstate = (ipp_pstate_t)ippGetInteger(attr, 0);
	else if (!strcmp(ippGetName(attr), "printer-state-message") &&
		 ippGetValueTag(attr) == IPP_TAG_TEXT)
	  pstatemsg = ippGetString(attr, 0, NULL);
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
	  return NULL;
	case IPP_PRINTER_STOPPED:
	  if (reason == NULL)
	    return pstatemsg;
	  else if (strcasestr(pstatemsg, reason) != NULL)
	    return pstatemsg;
	  else
	    return NULL;
	}
      }
    }
    debug_printf("cups-browsed: No information regarding enabled/disabled found about the requested printer '%s'\n",
		 printer);
    return NULL;
  }
  debug_printf("cups-browsed: ERROR: Request for printer info failed: %s\n",
	       cupsLastErrorString());
  return NULL;
}

int
enable_printer (const char *printer) {
  ipp_t *request;
  char uri[HTTP_MAX_URI];

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", 0, "/printers/%s", printer);
  request = ippNewRequest (IPP_RESUME_PRINTER);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  ippDelete(cupsDoRequest (CUPS_HTTP_DEFAULT, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING) {
    debug_printf("cups-browsed: ERROR: Failed enabling printer '%s': %s\n",
		 printer, cupsLastErrorString());
    return -1;
  }
  debug_printf("cups-browsed: Enabled printer '%s'\n", printer);
  return 0;
}

int
disable_printer (const char *printer, const char *reason) {
  ipp_t *request;
  char uri[HTTP_MAX_URI];

  if (reason == NULL)
    reason = "Disabled by cups-browsed";
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", 0, "/printers/%s", printer);
  request = ippNewRequest (IPP_PAUSE_PRINTER);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
		"printer-state-message", NULL, reason);
  ippDelete(cupsDoRequest (CUPS_HTTP_DEFAULT, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING) {
    debug_printf("cups-browsed: ERROR: Failed disabling printer '%s': %s\n",
		 printer, cupsLastErrorString());
    return -1;
  }
  debug_printf("cups-browsed: Disabled printer '%s'\n", printer);
  return 0;
}

int
set_cups_default_printer(const char *printer) {
  ipp_t	*request;
  char uri[HTTP_MAX_URI];

  if (printer == NULL)
    return 0;
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", printer);
  request = ippNewRequest(IPP_OP_CUPS_SET_DEFAULT);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());
  ippDelete(cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING) {
    debug_printf("cups-browsed: ERROR: Failed setting CUPS default printer to '%s': %s\n",
		 printer, cupsLastErrorString());
    return -1;
  }
  debug_printf("cups-browsed: Successfully set CUPS default printer to '%s'\n",
	       printer);
  return 0;
}

const char*
get_cups_default_printer() {
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *default_printer_name = NULL;
  
  request = ippNewRequest(CUPS_GET_DEFAULT);
  /* Default user */
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	       "requesting-user-name", NULL, cupsUser());
  /* Do it */
  response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/");
  if (cupsLastError() > IPP_OK_CONFLICT || !response) {
    debug_printf("cups-browsed: Could not determine system default printer!\n");
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
  return default_printer_name;
}

int
is_cups_default_printer(const char *printer) {
  if (printer == NULL)
    return 0;
  const char *cups_default = get_cups_default_printer();
  if (cups_default == NULL)
    return 0;
  if (!strcasecmp(printer, cups_default))
    return 1;
  return 0;
}

int
invalidate_default_printer(int local) {
  const char *filename = local ? LOCAL_DEFAULT_PRINTER_FILE :
    REMOTE_DEFAULT_PRINTER_FILE;
  unlink(filename);
  return 0;
}

int
record_default_printer(const char *printer, int local) {
  FILE *fp = NULL;
  const char *filename = local ? LOCAL_DEFAULT_PRINTER_FILE :
    REMOTE_DEFAULT_PRINTER_FILE;

  if (printer == NULL || strlen(printer) == 0)
    return invalidate_default_printer(local);

  fp = fopen(filename, "w+");
  if (fp == NULL) {
    debug_printf("cups-browsed: ERROR: Failed creating file %s\n",
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
  const char *filename = local ? LOCAL_DEFAULT_PRINTER_FILE :
    REMOTE_DEFAULT_PRINTER_FILE;
  const char *printer = NULL;
  char *p, buf[1024];
  int n;

  fp = fopen(filename, "r");
  if (fp == NULL) {
    debug_printf("cups-browsed: Failed reading file %s\n",
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

  snprintf(filename, sizeof(filename), SAVE_OPTIONS_FILE,
	   printer);
  unlink(filename);
  return 0;
}

int
record_printer_options(const char *printer) {
  char filename[1024];
  FILE *fp = NULL;
  char uri[HTTP_MAX_URI], *resource;
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *key;
  char valuebuffer[65536];
  const char *ppdname;
  ppd_file_t *ppd;
  ppd_option_t *ppd_opt;
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

  if (printer == NULL || strlen(printer) == 0)
    return 0;

  snprintf(filename, sizeof(filename), SAVE_OPTIONS_FILE,
	   printer);

  debug_printf("cups-browsed: Recording printer options for %s to %s\n",
	       printer, filename);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", 0, "/printers/%s", printer);
  resource = uri + (strlen(uri) - strlen(printer) - 10);
  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
	       uri);
  response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, resource);

  fp = fopen(filename, "w+");
  if (fp == NULL) {
    debug_printf("cups-browsed: ERROR: Failed creating file %s\n",
		 filename);
    return -1;
  }

  /* Write all supported printer attributes */
  attr = ippFirstAttribute(response);
  while (attr) {
    key = ippGetName(attr);
    for (ptr = attrs_to_record; *ptr; ptr++)
      if (strcasecmp(key, *ptr) == 0 ||
	  (*ptr[0] == '*' &&
	   strcasecmp(key + strlen(key) - strlen(*ptr) + 1, *ptr + 1) == 0))
	break;
    if (*ptr != NULL) {
      fprintf (fp, "%s=", key);
      ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
      fprintf (fp, "%s\n", valuebuffer);
    }
    attr = ippNextAttribute(response);
  }

  /* If there is a PPD file for this printer, either local for IPP network
     printers or on the remote CUPS server for remote CUPS printers, we
     save the local settings for the PPD options. */
  if ((ppdname = cupsGetPPD(printer)) == NULL) {
    debug_printf("cups-browsed: Unable to get PPD file for %s: %s\n",
		 printer, cupsLastErrorString());
  } else if ((ppd = ppdOpenFile(ppdname)) == NULL) {
    unlink(ppdname);
    debug_printf("cups-browsed: Unable to open PPD file for %s.\n",
		 printer);
  } else {
    ppdMarkDefaults(ppd);

    for (ppd_opt = ppdFirstOption(ppd); ppd_opt; ppd_opt = ppdNextOption(ppd))
      if (strcasecmp(ppd_opt->keyword, "PageRegion") != 0)
	fprintf (fp, "%s=%s\n", ppd_opt->keyword, ppd_opt->defchoice);

    ppdClose(ppd);
    unlink(ppdname);
  }

  fclose(fp);

  return 0;
}

int
retrieve_printer_options(const char *printer) {
  char filename[1024];
  FILE *fp = NULL;
  char uri[HTTP_MAX_URI];
  ipp_t *request;
  int num_options;
  cups_option_t *options;
  char opt[65536], *val;

  if (printer == NULL || strlen(printer) == 0)
    return 0;

  num_options = 0;
  options = NULL;

  /* Do we have default option settings in cups-browsed.conf? */
  if (DefaultOptions) {
    debug_printf("cups-browsed: Applying default option settings to printer %s: %s\n",
		 printer, DefaultOptions);
    num_options = cupsParseOptions(DefaultOptions, num_options, &options);
  }

  /* Prepare reading file with saved option settings */
  snprintf(filename, sizeof(filename), SAVE_OPTIONS_FILE,
	   printer);

  debug_printf("cups-browsed: Retrieving printer options for %s from %s\n",
	       printer, filename);

  /* Open the file with the saved option settings for this print queue */
  fp = fopen(filename, "r");
  if (fp == NULL) {
    debug_printf("cups-browsed: Failed reading file %s, probably no options recorded yet\n",
		 filename);
  } else {
    /* Now read the lines of the file and add each setting to our request */
    errno = 0;
    debug_printf("cups-browsed: Applying retrieved option settings to printer %s:", printer);
    while ((val = fgets(opt, sizeof(opt), fp)) != NULL) {
      if (strlen(opt) > 1 && (val = strchr(opt, '=')) != NULL) {
	*val = '\0';
	val ++;
	val[strlen(val)-1] = '\0';
	debug_printf(" %s=%s", opt, val);
	num_options = cupsAddOption(opt, val, num_options, &options);
      }
    }
    debug_printf("\n");
    if (errno != 0) {
      debug_printf("cups-browsed: Failed reading file %s: %s\n",
		   filename, strerror(errno));
      fclose(fp);
    }	 
    fclose(fp);
  }

  if (num_options > 0) {
    /* Do an IPP request to apply the option settings */
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		     "localhost", ippPort(), "/printers/%s", printer);
    request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
		 uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		 "requesting-user-name", NULL, cupsUser());
    cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
    cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
    ippDelete(cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/admin/"));
    cupsFreeOptions(num_options, options);
    if (cupsLastError() > IPP_OK_CONFLICT)
      debug_printf("cups-browsed: Unable to modify CUPS queue (%s)!\n",
		   cupsLastErrorString());
  }

  return 0;
}

int
is_created_by_cups_browsed (const char *printer) {
  remote_printer_t *p;

  if (printer == NULL)
    return 0;
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!strcasecmp(printer, p->name))
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
    if (!strcasecmp(printer, p->name))
      return p;

  return NULL;
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
  const char *current_default = get_cups_default_printer();
  if (current_default == NULL || !is_created_by_cups_browsed(current_default)) {
    if (set_cups_default_printer(printer) < 0) {
      debug_printf("cups-browsed: ERROR: Could not set former default printer %s as default again.\n",
		   printer);
      return -1;
    } else {
      debug_printf("cups-browsed: Former default printer %s re-appeared, set as default again.\n",
		   printer);
      invalidate_default_printer(0);
    }
  }
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
    debug_printf("cups-browsed: ERROR: Failed recording remote default printer (%s). Removing the file with possible old recording.\n",
		 printer);
    invalidate_default_printer(0);
  } else
    debug_printf("cups-browsed: Recorded the fact that the current printer (%s) is the default printer before deleting the queue and returning to the local default printer.\n",
		 printer);
  /* Switch back to a recorded local printer, if available */
  const char *local_default = retrieve_default_printer(1);
  if (local_default != NULL) {
    if (set_cups_default_printer(local_default) >= 0)
      debug_printf("cups-browsed: Switching back to %s as default printer.\n",
		   local_default);
    else {
      debug_printf("cups-browsed: ERROR: Unable to switch back to %s as default printer.\n",
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
  char server_str[1024];
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  const char *pname = NULL;
  ipp_pstate_t pstate = IPP_PRINTER_IDLE;
  int paccept = 0;
  int num_jobs, min_jobs = 99999999;
  cups_job_t *jobs = NULL;
  const char *dest_host = NULL;
  int dest_index = 0;
  int valid_dest_found = 0;
  char filename[1024];
  FILE *fp;
  static const char *pattrs[] =
                {
                  "printer-name",
                  "printer-state",
                  "printer-is-accepting-jobs"
                };

  debug_printf("cups-browsed: [CUPS Notification] Printer state change: %s\n",
	       text);

  if (autoshutdown && autoshutdown_on == NO_JOBS) {
    if (check_jobs() == 0) {
      /* If auto shutdown is active for triggering on no jobs being left, we
	 schedule the shutdown in autoshutdown_timeout seconds */
      if (!autoshutdown_exec_id) {
	debug_printf ("cups-browsed: No jobs there any more on printers made available by us, shutting down in %d sec...\n", autoshutdown_timeout);
	autoshutdown_exec_id =
	  g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
				 NULL);
      }
    } else {
      /* If auto shutdown is active for triggering on no jobs being left, we
	 cancel a shutdown in autoshutdown_timeout seconds as there are jobs
         again. */
      if (autoshutdown_exec_id) {
	debug_printf ("cups-browsed: New jobs there on the printers made available by us, killing auto shutdown timer.\n");
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
    debug_printf("cups-browsed: [CUPS Notification] Default printer changed from %s to %s.\n",
		 default_printer, buf);
    if (is_created_by_cups_browsed(default_printer)) {
      /* Previous default printer created by cups-browsed */
      if (!is_created_by_cups_browsed(buf)) {
	/* New default printer local */
	/* Removed backed-up local default printer as we do not have a
	   remote printer as default any more */
	invalidate_default_printer(1);
	debug_printf("cups-browsed: Manually switched default printer from a cups-browsed-generated one to a local printer.\n");
      }
    } else {
      /* Previous default printer local */
      if (is_created_by_cups_browsed(buf)) {
	/* New default printer created by cups-browsed */
	/* Back up the local default printer to be able to return to it
	   if the remote printer disappears */
	if (record_default_printer(default_printer, 1) < 0) {
	  /* Delete record file if recording failed */
	  debug_printf("cups-browsed: ERROR: Failed recording local default printer. Removing the file with possible old recording.\n");
	  invalidate_default_printer(1);
	} else
	  debug_printf("cups-browsed: Recorded previous default printer so that if the currently selected cups-browsed-generated one disappears, we can return to the old local one.\n");
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
    debug_printf("cups-browsed: [CUPS Notification] %s not default printer any more.\n", buf);
  } else if ((ptr = strstr(text, " state changed to processing")) != NULL) {
    /* Printer started processing a job, check if it uses the implicitclass
       backend and if so, we select the remote queue to which to send the job
       in a way so that we get load balancing between all remote queues
       associated with this queue.

       There are two methods to do that (configurable in cups-browsed.conf):

       Queuing of jobs on the client (LoadBalancingType = QUEUE_ON_CLIENT):

       Here we check all remote printers assigned to this printer and to its
       duplicates which is currently accepting jobs and idle. If all are busy,
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
       duplicates which is currently accepting jobs and find the one with the
       lowest amount of jobs waiting and send the job to there. So on the
       local queue we have never jobs waiting if at least one remote printer
       accepts jobs.

       Not having jobs waiting locally has the advantage that we can take the
       local machine from the network and all jobs get printed. Disadvantage
       is that if a server with a full queue of jobs goes away, the jobs go
       away, too.

       Default is queuing the jobs on the client as this is what CUPS does
       with classes. */

    debug_printf("cups-browsed: [CUPS Notification] %s starts processing a job.\n", printer);
    q = printer_record(printer);
    /* If we hit a duplicate and not the "master", switch to the "master" */
    if (q && q->duplicate_of)
      q = q->duplicate_of;
    /* Having duplicates means that we are using the implicitclass backend */
    if (q && q->num_duplicates > 0) {
      debug_printf("cups-browsed: [CUPS Notification] %s is using the \"implicitclass\" CUPS backend, so let us search for a destination for this job.\n", printer);
      /* We keep track of the printer which we used last time and start
	 checking with the next printer this time, to get a "round robin"
	 type of printer usage instead of having most jobs going to the first
	 printer in the list. Method taken from the cupsdFindAvailablePrinter()
	 function of the scheduler/classes.c file of CUPS. */
      if (q->last_printer < 0 ||
	  q->last_printer >= cupsArrayCount(remote_printers))
	q->last_printer = 0;
      for (i = q->last_printer + 1; ; i++) {
	if (i >= cupsArrayCount(remote_printers))
	  i = 0;
	p = (remote_printer_t *)cupsArrayIndex(remote_printers, i);
	if (!strcasecmp(p->name, printer) &&
	    p->status == STATUS_CONFIRMED) {
	  debug_printf("cups-browsed: Checking state of remote printer %s on host %s.\n", printer, p->host);
	  snprintf(server_str, sizeof(server_str), "%s:%d",
		   p->ip ? p->ip : p->host, ippPort());
	  cupsSetServer(server_str);
	  /* Check whether the printer is idle, processing, or disabled */
	  request = ippNewRequest(CUPS_GET_PRINTERS);
	  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
			"requested-attributes",
			sizeof(pattrs) / sizeof(pattrs[0]),
			NULL, pattrs);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		       "requesting-user-name",
		       NULL, cupsUser());
	  if ((response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/")) !=
	      NULL) {
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
	      if (!strcasecmp(pname, printer)) {
		if (paccept) {
		  debug_printf("cups-browsed: Printer %s on host %s is accepting jobs.\n", printer, p->host);
		  switch (pstate) {
		  case IPP_PRINTER_IDLE:
		    valid_dest_found = 1;
		    dest_host = p->ip ? p->ip : p->host;
		    dest_index = i;
		    debug_printf("cups-browsed: Printer %s on host %s is idle, take this as destination and stop searching.\n", printer, p->host);
		    break;
		  case IPP_PRINTER_PROCESSING:
		    valid_dest_found = 1;
		    if (LoadBalancingType == QUEUE_ON_SERVERS) {
		      num_jobs = 0;
		      jobs = NULL;
		      num_jobs =
			cupsGetJobs2(CUPS_HTTP_DEFAULT, &jobs, printer, 0,
				     CUPS_WHICHJOBS_ACTIVE);
		      if (num_jobs >= 0 && num_jobs < min_jobs) {
			min_jobs = num_jobs;
			dest_host = p->ip ? p->ip : p->host;
			dest_index = i;
		      }
		      debug_printf("cups-browsed: Printer %s on host %s is printing and it has %d jobs.\n", printer, p->host, num_jobs);
		    } else
		      debug_printf("cups-browsed: Printer %s on host %s is printing.\n", printer, p->host);
		    break;
		  case IPP_PRINTER_STOPPED:
		    debug_printf("cups-browsed: Printer %s on host %s is disabled, skip it.\n", printer, p->host);
		    break;
		  }
		} else {
		  debug_printf("cups-browsed: Printer %s on host %s is not accepting jobs, skip it.\n", printer, p->host);
		}
		break;
	      }
	    }
	    if (pstate == IPP_PRINTER_IDLE && paccept) {
	      q->last_printer = i;
	      break;
	    }
	  }
	}
	if (i == q->last_printer)
	  break;
      }
      /* Set CUPS server back to local */
      cupsSetServer(NULL);
      /* Write the selected destination host into a file so that the
	 implicitclass backend will pick it up */
      snprintf(filename, sizeof(filename), IMPLICIT_CLASS_DEST_HOST_FILE,
	       printer);
      fp = fopen(filename, "w+");
      if (fp == NULL) {
	debug_printf("cups-browsed: ERROR: Failed creating file %s\n",
		     filename);
	return;
      }
      if (dest_host) {
	q->last_printer = dest_index;
	fprintf(fp, "\"%s\"", dest_host);
	debug_printf("cups-browsed: Wrote destination for job to %s: %s (to file %s)\n",
		     printer, dest_host, filename);
      } else if (valid_dest_found == 1) {
	fprintf(fp, "\"ALL_DESTS_BUSY\"");
	debug_printf("cups-browsed: All destinations busy for job to %s (file %s)\n",
		     printer, filename);
      } else {
	fprintf(fp, "\"NO_DEST_FOUND\"");
	debug_printf("cups-browsed: No destination found for job to %s (file %s)\n",
		     printer, filename);
      }
      fclose(fp);
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

  debug_printf("cups-browsed: [CUPS Notification] Printer deleted: %s\n",
	       text);

  if (is_created_by_cups_browsed(printer)) {
    /* a cups-browsed-generated printer got deleted, re-create it */
    debug_printf("cups-browsed: Printer %s got deleted, re-creating it.\n",
		 printer);
    /* If the deleted printer was the default printer, make sure it gets the
       default printer again */
    if (default_printer && !strcasecmp(printer, default_printer)) {
      if (record_default_printer(printer, 0) < 0) {
	/* Delete record file if recording failed */
	debug_printf("cups-browsed: ERROR: Failed recording remote default printer. Removing the file with possible old recording.\n");
	invalidate_default_printer(0);
      } else
	debug_printf("cups-browsed: Recorded %s as remote default printer so that it gets set as default after re-creating.\n");
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
    if (p && p->status != STATUS_DISAPPEARED) {
      p->status = STATUS_TO_BE_CREATED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
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
  debug_printf("cups-browsed: [CUPS Notification] Printer modified: %s\n",
	       text);

  if (is_created_by_cups_browsed(printer)) {
    /* The user has changed settings of a printer which we have generated,
       backup the changes for the case of a crash or unclean shutdown of
       cups-browsed. */
    debug_printf("cups-browsed: Settings of printer %s got modified, doing backup.\n",
		 printer);
    record_printer_options(printer);
  }
}


static remote_printer_t *
create_local_queue (const char *name,
		    const char *uri,
		    const char *host,
		    const char *ip,
		    const char *info,
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
  int uri_status, port;
  http_t *http;
  char scheme[10], userpass[1024], host_name[1024], resource[1024];
  ipp_t *request, *response;
#ifdef HAVE_CUPS_1_6
  ipp_attribute_t *attr;
  char valuebuffer[65536];
  int i, count, left, right, bottom, top;
  const char *default_page_size = NULL, *best_color_space = NULL, *color_space;
#endif /* HAVE_CUPS_1_6 */

  /* Mark this as a queue to be created locally pointing to the printer */
  if ((p = (remote_printer_t *)calloc(1, sizeof(remote_printer_t))) == NULL) {
    debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
    return NULL;
  }

  /* Queue name */
  p->name = strdup(name);
  if (!p->name)
    goto fail;

  p->uri = strdup(uri);
  if (!p->uri)
    goto fail;

  p->duplicate_of = NULL;
  p->num_duplicates = 0;
  
  p->num_options = 0;
  p->options = NULL;
  
  p->host = strdup (host);
  if (!p->host)
    goto fail;

  p->ip = (ip != NULL ? strdup (ip) : NULL);

  p->service_name = strdup (info);
  if (!p->service_name)
    goto fail;

  /* Record Bonjour service parameters to identify print queue
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

  if (is_cups_queue) {
    /* Our local queue must be raw, so that the PPD file and driver
       on the remote CUPS server get used */
    p->ppd = NULL;
    p->model = NULL;
    p->ifscript = NULL;
    /* Check whether we have an equally named queue already from another
       server */
    for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 q;
	 q = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!strcasecmp(q->name, p->name) &&
	  q != p)
	break;
    p->duplicate_of = (q && q->status != STATUS_DISAPPEARED &&
		       q->status != STATUS_UNCONFIRMED) ? q : NULL;
    if (p->duplicate_of) {
      debug_printf("cups-browsed: Printer %s already available through host %s.\n",
		   p->name, q->host);
      if (!q->duplicate_of) {
	/* Update q to get implicitclass:... URI */
	q->num_duplicates ++;
	if (q->status != STATUS_DISAPPEARED) {
	  q->status = STATUS_TO_BE_CREATED;
	  q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
	}
      }
    } else if (q) {
      q->duplicate_of = p;
      debug_printf("cups-browsed: Unconfirmed/disappeared printer %s already available through host %s, marking that printer duplicate of the newly found one.\n",
		   p->name, q->host);
      /* Update p to get implicitclass:... URI */
      p->num_duplicates ++;
      if (p->status != STATUS_DISAPPEARED) {
	p->status = STATUS_TO_BE_CREATED;
	p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
      }
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
       Raster. Especially IPP Everywhere printers and PDF-capable 
       AirPrint printers will work this way. Making only driverless
       queues we can get an easy, configuration-less way to print
       from mobile devices, even if there is no CUPS server with
       shared printers around. */

    if (CreateIPPPrinterQueues == 0) {
      debug_printf("cups-browsed: Printer %s (%s) is an IPP network printer and cups-browsed is not configured to set up such printers automatically, ignoring this printer.\n",
		   p->name, p->uri);
      goto fail;
    }

    if (!pdl || pdl[0] == '\0' ||
	(!strcasestr(pdl, "application/postscript") &&
	 !strcasestr(pdl, "application/pdf") &&
	 !strcasestr(pdl, "image/pwg-raster") &&
	 ((!strcasestr(pdl, "application/vnd.hp-PCL") &&
	   !strcasestr(pdl, "application/PCL") &&
	   !strcasestr(pdl, "application/x-pcl")) ||
	  ((strncasecmp(make_model, "HP", 2) ||
	    strncasecmp(make_model, "Hewlett Packard", 15) ||
	    strncasecmp(make_model, "Hewlett-Packard", 15)) &&
	   !strcasestr(make_model, "LaserJet") &&
	   !strcasestr(make_model, "Mopier"))) &&
	 !strcasestr(pdl, "application/vnd.hp-PCLXL"))) {
      debug_printf("cups-browsed: Cannot create remote printer %s (%s) as its PDLs are not known, ignoring this printer.\n",
		   p->name, p->uri);
      goto fail;
    }

    p->duplicate_of = NULL;
    p->num_duplicates = 0;
    p->model = NULL;

    /* Request printer properties and try to generate a PPD file for the
       printer (mainly IPP Everywhere printers) */
    uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, uri,
				 scheme, sizeof(scheme),
				 userpass, sizeof(userpass),
				 host_name, sizeof(host_name),
				 &(port),
				 resource, sizeof(resource));
    if (uri_status != HTTP_URI_OK)
      goto fail;
    if ((http = httpConnect(host_name, port)) ==
	NULL) {
      debug_printf("cups-browsed: Cannot connect to remote printer %s (%s:%d), ignoring this printer.\n",
		   p->uri, host_name, port);
      goto fail;
    }
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    response = cupsDoRequest(http, request, resource);

    /* Log all printer attributes for debugging */
    if (debug) {
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

    if (IPPPrinterQueueType == PPD_NEVER || !_ppdCreateFromIPP(buffer, sizeof(buffer), response)) {
      if (IPPPrinterQueueType == PPD_AUTO || IPPPrinterQueueType == PPD_ONLY) {
	debug_printf("cups-browsed: Unable to create PPD file: %s\n", strerror(errno));
	if (IPPPrinterQueueType == PPD_ONLY)
	  goto fail;
      }
      
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
				     strdup(make_model),
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
	       p->name, cups_serverbin);

      bytes = write(fd, buffer, strlen(buffer));
      if (bytes != strlen(buffer)) {
	debug_printf("Unable to write interface script into the file\n");
	goto fail;
      }

      close(fd);

      p->ifscript = strdup(tempfile);
    } else {
      debug_printf("cups-browsed: Created temporary IPP Everywhere PPD: %s\n", buffer);
      p->ppd = strdup(buffer);
      p->ifscript = NULL;
    }

    /*p->model = "drv:///sample.drv/laserjet.ppd";
      debug_printf("cups-browsed: PPD from system for %s: %s\n", p->name, p->model);*/

    /*p->ppd = "/usr/share/ppd/cupsfilters/pxlcolor.ppd";
      debug_printf("cups-browsed: PPD from file for %s: %s\n", p->name, p->ppd);*/

    /*p->ifscript = "/usr/lib/cups/filter/sys5ippprinter";
      debug_printf("cups-browsed: System V Interface script for %s: %s\n", p->name, p->ifscript);*/

#endif /* HAVE_CUPS_1_6 */
  }

  /* Add the new remote printer entry */
  cupsArrayAdd(remote_printers, p);

  /* If auto shutdown is active we have perhaps scheduled a timer to shut down
     due to not having queues any more to maintain, kill the timer now */
  if (autoshutdown && autoshutdown_exec_id &&
      autoshutdown_on == NO_QUEUES &&
      cupsArrayCount(remote_printers) > 0) {
    debug_printf ("cups-browsed: New printers there to make available, killing auto shutdown timer.\n");
    g_source_remove(autoshutdown_exec_id);
    autoshutdown_exec_id = 0;
  }

  return p;

 fail:
  debug_printf("cups-browsed: ERROR: Unable to create print queue, ignoring printer.\n");
  free (p->type);
  free (p->service_name);
  free (p->host);
  if (p->ip) free (p->ip);
  cupsFreeOptions(p->num_options, p->options);
  free (p->uri);
  free (p->name);
  if (p->ppd) free (p->ppd);
  if (p->model) free (p->model);
  if (p->ifscript) free (p->ifscript);
  free (p);
  return NULL;
}

/*
 * Remove all illegal characters and replace each group of such characters
 * by a single dash, return a free()-able string.
 *
 * mode = 0: Only allow letters, numbers, dashes, and underscores for
 *           turning make/model info into a valid print queue name or
 *           into a string which can be supplied as option value in a
 *           filter command line without need of quoting
 * mode = 1: Allow also '/', '.', ',' for cleaning up MIME type
 *           strings (here available Page Description Languages, PDLs) to
 *           supply them on a filter command line without quoting
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
{
  int i, j;
  int havedash = 0;
  char *str;

  if (str_orig == NULL)
    return NULL;

  str = strdup(str_orig);

  /* for later str[strlen(str)-1] access */
  if (strlen(str) < 1)
    return str;

  for (i = 0, j = 0; i < strlen(str); i++, j++) {
    if (((str[i] >= 'A') && (str[i] <= 'Z')) ||
	((str[i] >= 'a') && (str[i] <= 'z')) ||
	((str[i] >= '0') && (str[i] <= '9')) ||
	str[i] == '_' ||
	(mode == 1 && (str[i] == '/' ||
		       str[i] == '.' || str[i] == ','))) {
      /* Allowed character, keep it */
      havedash = 0;
      str[j] = str[i];
    } else {
      /* Replace all other characters by a single '-' */
      if (havedash == 1)
	j --;
      else {
	havedash = 1;
	str[j] = '-';
      }
    }
  }
  /* Add terminating zero */
  str[j] = '\0';
  /* Cut off trailing dashes */
  while (str[strlen(str)-1] == '-')
    str[strlen(str)-1] = '\0';

  /* Cut off leading dashes */
  i = 0;
  while (str[i] == '-')
    ++i;

  /* keep a free()-able string. +1 for trailing \0 */
  return memmove(str, str + i, strlen(str) - i + 1);
}

gboolean handle_cups_queues(gpointer unused) {
  remote_printer_t *p, *q;
  http_t *http;
  char uri[HTTP_MAX_URI], device_uri[HTTP_MAX_URI];
  int num_options;
  cups_option_t *options;
  int num_jobs;
  cups_job_t *jobs;
  ipp_t *request;
  time_t current_time = time(NULL);
  int i;

  debug_printf("cups-browsed: Processing printer list ...\n");
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {
    switch (p->status) {

    /* Print queue generated by us in a previous session */
    case STATUS_UNCONFIRMED:

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      /* Queue not reported again by Bonjour, remove it */
      p->status = STATUS_DISAPPEARED;
      p->timeout = current_time + TIMEOUT_IMMEDIATELY;

      debug_printf("cups-browsed: No remote printer named %s available, removing entry from previous session.\n",
		   p->name);

    /* Bonjour has reported this printer as disappeared or we have replaced
       this printer by another one */
    case STATUS_DISAPPEARED:

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      debug_printf("cups-browsed: Removing entry %s%s.\n", p->name,
		   (p->duplicate_of ? "" : " and its CUPS queue"));

      /* Remove the CUPS queue */
      if (!p->duplicate_of) { /* Duplicates do not have a CUPS queue */
	if ((http = http_connect_local ()) == NULL) {
	  debug_printf("cups-browsed: Unable to connect to CUPS!\n");
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}

	/* Record the option settings to retrieve them when the remote
	   queue re-appears later or when cups-browsed gets started again */
	record_printer_options(p->name);

	/* Check whether there are still jobs and do not remove the queue
	   then */
	num_jobs = 0;
	jobs = NULL;
	num_jobs = cupsGetJobs2(http, &jobs, p->name, 0, CUPS_WHICHJOBS_ACTIVE);
	if (num_jobs != 0) { /* error or jobs */
	  debug_printf("cups-browsed: Queue has still jobs or CUPS error!\n");
	  cupsFreeJobs(num_jobs, jobs);
	  /* Disable the queue */
#ifdef HAVE_AVAHI
	  if (avahi_present || p->domain == NULL || p->domain[0] == '\0')
	    /* If avahi has got shut down, do not disable queues which are,
	       created based on DNS-SD broadcasts as the server has most
	       probably not gone away */
#endif /* HAVE_AVAHI */
	    disable_printer(p->name,
			    "Printer disappeared or cups-browsed shutdown");
	  /* Schedule the removal of the queue for later */
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}

	/* If this queue was the default printer, note that fact so that
	   it gets the default printer again when it re-appears, also switch
	   back to the last local default printer */
	queue_removal_handle_default(p->name);

	/* If we do not have a subscription to CUPS' D-Bus notifications and
	   so no default printer management, we simply do not remove this
	   CUPS queue if it is the default printer, to not cause a change
	   of the default printer or the loss of the information that this
	   printer is the default printer. */
	if (cups_notifier == NULL && is_cups_default_printer(p->name)) {
	  /* Schedule the removal of the queue for later */
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}
	
	/* No jobs, remove the CUPS queue */
	request = ippNewRequest(CUPS_DELETE_PRINTER);
	/* Printer URI: ipp://localhost:631/printers/<queue name> */
	httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
			 "localhost", 0, "/printers/%s", p->name);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		     "printer-uri", NULL, uri);
	/* Default user */
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		     "requesting-user-name", NULL, cupsUser());
	/* Do it */
	ippDelete(cupsDoRequest(http, request, "/admin/"));
	if (cupsLastError() > IPP_OK_CONFLICT) {
	  debug_printf("cups-browsed: Unable to remove CUPS queue!\n");
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}
      } else {
	/* "master printer" of this duplicate */
	q = p->duplicate_of;
	if (q->status != STATUS_DISAPPEARED) {
	  debug_printf("cups-browsed: Removed a duplicate of printer %s on %s, scheduling its master printer %s on host %s for update, to assure it will have the correct device URI.\n",
		       p->name, p->host, q->name, q->host);
	  /* Schedule for update, so that an implicitclass:... URI gets
	     removed if not needed any more */
	  q->status = STATUS_TO_BE_CREATED;
	  q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
	}
      }

      /* CUPS queue removed, remove the list entry */
      cupsArrayRemove(remote_printers, p);
      if (p->name) free (p->name);
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
      if (autoshutdown && !autoshutdown_exec_id &&
	  (cupsArrayCount(remote_printers) == 0 ||
	   (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
	debug_printf ("cups-browsed: No printers there any more to make available or no jobs, shutting down in %d sec...\n", autoshutdown_timeout);
	autoshutdown_exec_id =
	  g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute,
				 NULL);
      }

      break;

    /* Bonjour has reported a new remote printer, create a CUPS queue for it,
       or upgrade an existing queue, or update a queue to use a backup host
       when it has disappeared on the currently used host */
    case STATUS_TO_BE_CREATED:
      /* (...or, we've just received a CUPS Browsing packet for this queue) */
    case STATUS_BROWSE_PACKET_RECEIVED:

      /* Do not create a queue for duplicates */
      if (p->duplicate_of) {
	p->status = STATUS_CONFIRMED;
	p->timeout = (time_t) -1;
	break;
      }

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      debug_printf("cups-browsed: Creating/Updating CUPS queue for %s\n",
		   p->name);
      debug_printf("cups-browsed: Queue has %d duplicates\n",
		   p->num_duplicates);

      /* Determine whether we we have duplicates, and so an implicit class
         for load balancing. In this case we will assign an implicitclass:...
	 device URI, which makes cups-browsed find the best destination for
	 each job. */
      if (cups_notifier != NULL && p->num_duplicates > 0) {
	/* We have duplicates, so we use the device URI
	   implicitclass:<queue name>
	   We never use the implicitclass backend if we do not have D-Bus
	   notification from CUPS as we cannot assign a destination printer
	   to an incoming job then. */
	snprintf(device_uri, sizeof(device_uri), "implicitclass:%s",
		 p->name);
	debug_printf("cups-browsed: Print queue %s has duplicates, using implicit class device URI %s\n",
		     p->name, device_uri);
      } else {
	/* Device URI: ipp(s)://<remote host>:631/printers/<remote queue> */
	strncpy(device_uri, p->uri, sizeof(device_uri));
	debug_printf("cups-browsed: Print queue %s has no duplicates, using direct device URI %s\n",
		     p->name, device_uri);
      }

      /* Create a new CUPS queue or modify the existing queue */
      if ((http = http_connect_local ()) == NULL) {
	debug_printf("cups-browsed: Unable to connect to CUPS!\n");
	p->timeout = current_time + TIMEOUT_RETRY;
	break;
      }
      request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
      /* Printer URI: ipp://localhost:631/printers/<queue name> */
      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		       "localhost", ippPort(), "/printers/%s", p->name);
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
      /* Do not share a queue which serves only to point to a remote printer */
      num_options = cupsAddOption("printer-is-shared", "false",
				  num_options, &options);
      /* Description: <Bonjour service name> */
      num_options = cupsAddOption("printer-info", p->service_name,
				  num_options, &options);
      /* Location: <Remote host name> */
      num_options = cupsAddOption("printer-location", p->host,
				  num_options, &options);
      /* Default option settings from printer entry */
      for (i = 0; i < p->num_options; i ++)
	num_options = cupsAddOption(strdup(p->options[i].name),
				    strdup(p->options[i].value),
				    num_options, &options);
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
      /* PPD from system's CUPS installation */
      if (p->model) {
	debug_printf("cups-browsed: Non-raw queue %s with system PPD: %s\n", p->name, p->model);
	p->ppd = cupsGetServerPPD(http, p->model);
      }
      /* Do it */
      if (p->ppd) {
	debug_printf("cups-browsed: Non-raw queue %s with PPD file: %s\n", p->name, p->ppd);
	ippDelete(cupsDoFileRequest(http, request, "/admin/", p->ppd));
	if (p->model) {
	  unlink(p->ppd);
	  free(p->ppd);
	  p->ppd = NULL;
	}
      } else if (p->ifscript) {
	debug_printf("cups-browsed: Non-raw queue %s with interface script: %s\n", p->name, p->ifscript);
	ippDelete(cupsDoFileRequest(http, request, "/admin/", p->ifscript));
	unlink(p->ifscript);
	free(p->ifscript);
	p->ifscript = NULL;
      } else {
	debug_printf("cups-browsed: Raw queue %s\n", p->name);
	ippDelete(cupsDoRequest(http, request, "/admin/"));
      }
      cupsFreeOptions(num_options, options);
      if (cupsLastError() > IPP_OK_CONFLICT) {
	debug_printf("cups-browsed: Unable to create CUPS queue!\n");
	p->timeout = current_time + TIMEOUT_RETRY;
	break;
      }

      /* Option settings which we have recorded from the previous session */
      retrieve_printer_options(p->name);

      /* If this queue was the default printer in its previous life, make
	 it the default printer again. */
      queue_creation_handle_default(p->name);

      /* If cups-browsed or a failed backend has disabled this
	 queue, re-enable it. */
      if (is_disabled(p->name, "cups-browsed") ||
	  is_disabled(p->name, "Printer stopped due to backend errors"))
	enable_printer(p->name);

      if (p->status == STATUS_BROWSE_PACKET_RECEIVED) {
	p->status = STATUS_DISAPPEARED;
	p->timeout = time(NULL) + BrowseTimeout;
	debug_printf("cups-browsed: starting BrowseTimeout timer for %s (%ds)\n",
		     p->name, BrowseTimeout);
      } else {
	p->status = STATUS_CONFIRMED;
	p->timeout = (time_t) -1;
      }

      break;

    case STATUS_CONFIRMED:
      /* If this queue was the default printer in its previous life, make
	 it the default printer again. */
      queue_creation_handle_default(p->name);

      /* If this queue is disabled, re-enable it. */
      enable_printer(p->name);

      /* Record the options, to record any changes which happened
	 while cups-browsed was not running */
      record_printer_options(p->name);

      break;

    }
  }

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
    queues_timer_id = g_timeout_add_seconds (timeout, handle_cups_queues, NULL);
    debug_printf("cups-browsed: checking queues in %ds\n", timeout);
  } else {
    queues_timer_id = 0;
    debug_printf("cups-browsed: listening\n");
  }
}

static gboolean
matched_filters (const char *name,
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

  debug_printf("cups-browsed: Matching printer \"%s\" with properties Host = \"%s\", Port = %d, Service Name = \"%s\", Domain = \"%s\" with the BrowseFilter lines in cups-browsed.conf\n", name, host, port, service_name, domain);
  /* Go through all BrowseFilter lines and stop if one line does not match,
     rejecting this printer */
  for (filter = cupsArrayFirst (browsefilter);
       filter;
       filter = cupsArrayNext (browsefilter)) {
    debug_printf("cups-browsed: Matching with line \"BrowseFilter %s%s%s %s\"",
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
	      if (filter->sense == FILTER_NOT_MATCH)
		goto filter_failed;
	    } else {
	      if (filter->sense == FILTER_MATCH)
		goto filter_failed;
	    }	      
	  } else {
	    /* match boolean value */
	    if (filter->sense == FILTER_MATCH) {
 	      if (!value || strcasecmp(value, "T"))
		goto filter_failed;
	    } else {
 	      if (value && !strcasecmp(value, "T"))
		goto filter_failed;
	    }
	  }
	}
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
      if (name)
	property = name;
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
  debug_printf("cups-browsed: All BrowseFilter lines matched or skipped, accepting printer %s\n",
	       name);
  return TRUE;

 filter_failed:
  debug_printf(" --> FAILED\n");
  debug_printf("cups-browsed: One BrowseFilter line did not match, ignoring printer %s\n",
	       name);
  return FALSE;
}

static remote_printer_t *
generate_local_queue(const char *host,
		     const char *ip,
		     uint16_t port,
		     char *resource,
		     const char *name,
		     const char *type,
		     const char *domain,
		     void *txt) {

  char uri[HTTP_MAX_URI];
  char *remote_queue = NULL, *remote_host = NULL, *pdl = NULL;
  int color = 0, duplex = 0;
#ifdef HAVE_AVAHI
  char *fields[] = { "product", "usb_MDL", "ty", NULL }, **f;
  AvahiStringList *entry = NULL;
  char *key = NULL, *value = NULL;
#endif /* HAVE_AVAHI */
  remote_printer_t *p;
  local_printer_t *local_printer;
  char *backup_queue_name = NULL, *local_queue_name = NULL,
       *local_queue_name_lower = NULL;
  int is_cups_queue;
  /*size_t hl = 0;*/
  gboolean create = TRUE;
  

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

  /* Check by the resource whether the discovered printer is a CUPS queue */
  if (!strncasecmp(resource, "printers/", 9)) {
    /* This is a remote CUPS queue, use the remote queue name for the
       local queue */
    is_cups_queue = 1;
    /* Not directly used in script generation input later, but taken from
       packet, so better safe than sorry. (consider second loop with
       backup_queue_name) */
    remote_queue = remove_bad_chars(resource + 9, 0);
    debug_printf("cups-browsed: Found CUPS queue: %s on host %s.\n",
		 remote_queue, remote_host);
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
      } else
	raw_queue = 1;
    } else if (domain && domain[0] != '\0')
      raw_queue = 1;
    if (raw_queue && CreateRemoteRawPrinterQueues == 0) {
      /* The remote CUPS queue is raw, ignore it */
      debug_printf("cups-browsed: Remote Bonjour-advertised CUPS queue %s on host %s is raw, ignored.\n",
		   remote_queue, remote_host);
      free (remote_queue);
      free (remote_host);
      return NULL;
    }
#endif /* HAVE_AVAHI */
  } else if (!strncasecmp(resource, "classes/", 8)) {
    /* This is a remote CUPS queue, use the remote queue name for the
       local queue */
    is_cups_queue = 1;
    /* Not directly used in script generation input later, but taken from
       packet, so better safe than sorry. (consider second loop with
       backup_queue_name) */
    remote_queue = remove_bad_chars(resource + 8, 0);
    debug_printf("cups-browsed: Found CUPS queue: %s on host %s.\n",
		 remote_queue, remote_host);
  } else {
    /* This is an IPP-based network printer */
    is_cups_queue = 0;
    /* Determine the queue name by the model */
    remote_queue = strdup("printer");
#ifdef HAVE_AVAHI
    if (txt) {
      for (f = fields; *f; f ++) {
	entry = avahi_string_list_find((AvahiStringList *)txt, *f);
	if (entry) {
	  avahi_string_list_get_pair(entry, &key, &value, NULL);
	  if (key && value && !strcasecmp(key, *f) && strlen(value) >= 3) {
            free (remote_queue);
	    remote_queue = remove_bad_chars(value, 0);
	    break;
	  }
	}
      }
      /* Find out which PDLs the printer understands */
      entry = avahi_string_list_find((AvahiStringList *)txt, "pdl");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, "pdl") && strlen(value) >= 3) {
	  pdl = remove_bad_chars(value, 1);
	}
      }
      /* Find out if we have a color printer */
      entry = avahi_string_list_find((AvahiStringList *)txt, "Color");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, "Color")) {
	  if (!strcasecmp(value, "T")) color = 1;
	  if (!strcasecmp(value, "F")) color = 0;
	}
      }
      /* Find out if we have a duplex printer */
      entry = avahi_string_list_find((AvahiStringList *)txt, "Duplex");
      if (entry) {
	avahi_string_list_get_pair(entry, &key, &value, NULL);
	if (key && value && !strcasecmp(key, "Duplex")) {
	  if (!strcasecmp(value, "T")) duplex = 1;
	  if (!strcasecmp(value, "F")) duplex = 0;
	}
      }
    }
#endif /* HAVE_AVAHI */
  }
  /* Check if there exists already a CUPS queue with the
     requested name Try name@host in such a case and if
     this is also taken, ignore the printer */
  if ((backup_queue_name = malloc((strlen(remote_queue) +
				   strlen(remote_host) + 2) *
				  sizeof(char))) == NULL) {
    debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
    exit(1);
  }
  sprintf(backup_queue_name, "%s@%s", remote_queue, remote_host);

  /* Get available CUPS queues */
  update_local_printers ();

  local_queue_name = remote_queue;

  /* Is there a local queue with the same URI as the remote queue? */
  if (g_hash_table_find (local_printers,
			 local_printer_has_uri,
			 uri))
    create = FALSE;

  if (create) {
    /* Is there a local queue with the name of the remote queue? */
    local_queue_name_lower = g_ascii_strdown(local_queue_name, -1);
    local_printer = g_hash_table_lookup (local_printers,
					 local_queue_name_lower);
    free(local_queue_name_lower);
    /* Only consider CUPS queues not created by us */
    if (local_printer && !local_printer->cups_browsed_controlled) {
      /* Found local queue with same name as remote queue */
      /* Is there a local queue with the name <queue>@<host>? */
      local_queue_name = backup_queue_name;
      debug_printf("cups-browsed: %s already taken, using fallback name: %s\n",
		   remote_queue, local_queue_name);
      local_queue_name_lower = g_ascii_strdown(local_queue_name, -1);
      local_printer = g_hash_table_lookup (local_printers,
					   local_queue_name_lower);
      free(local_queue_name_lower);
      if (local_printer && !local_printer->cups_browsed_controlled) {
	/* Found also a local queue with name <queue>@<host>, so
	   ignore this remote printer */
	debug_printf("cups-browsed: %s also taken, printer ignored.\n",
		     local_queue_name);
	free (backup_queue_name);
	free (remote_host);
	free (pdl);
	free (remote_queue);
	return NULL;
      }
    }
  }

  if (!matched_filters (local_queue_name, remote_host, port, name, domain,
			txt)) {
    debug_printf("cups-browsed: Printer %s does not match BrowseFilter lines in cups-browsed.conf, printer ignored.\n",
		 local_queue_name);
    free (backup_queue_name);
    free (remote_host);
    free (pdl);
    free (remote_queue);
    return NULL;
  }

  /* Check if we have already created a queue for the discovered
     printer */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!strcasecmp(p->name, local_queue_name) &&
	(p->host[0] == '\0' ||
	 p->status == STATUS_UNCONFIRMED ||
	 p->status == STATUS_DISAPPEARED ||
	 !strcasecmp(p->host, remote_host)))
      break;

  if (!create) {
    free (remote_host);
    free (backup_queue_name);
    free (pdl);
    free (remote_queue);
    if (p) {
      return p;
    } else {
      /* Found a local queue with the same URI as our discovered printer
	 would get, so ignore this remote printer */
      debug_printf("cups-browsed: Printer with URI %s already exists, printer ignored.\n",
		   uri);
      return NULL;
    }
  }

  if (p) {
    /* We have already created a local queue, check whether the
       discovered service allows us to upgrade the queue to IPPS
       or whether the URI part after ipp(s):// has changed */
    if ((strcasestr(type, "_ipps") &&
	 !strncasecmp(p->uri, "ipp:", 4)) ||
	strcasecmp(strchr(p->uri, ':'), strchr(uri, ':'))) {

      /* Schedule local queue for upgrade to ipps: or for URI change */
      if (strcasestr(type, "_ipps") &&
	  !strncasecmp(p->uri, "ipp:", 4))
	debug_printf("cups-browsed: Upgrading printer %s (Host: %s) to IPPS. New URI: %s\n",
		     p->name, remote_host, uri);
      if (strcasecmp(strchr(p->uri, ':'), strchr(uri, ':')))
	debug_printf("cups-browsed: Changing URI of printer %s (Host: %s) to %s.\n",
		     p->name, remote_host, uri);
      free(p->uri);
      free(p->host);
      free(p->ip);
      free(p->service_name);
      free(p->type);
      free(p->domain);
      p->uri = strdup(uri);
      p->status = STATUS_TO_BE_CREATED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
      p->host = strdup(remote_host);
      p->ip = (ip != NULL ? strdup(ip) : NULL);
      p->service_name = strdup(name);
      p->type = strdup(type);
      p->domain = strdup(domain);

    } else {

      /* Nothing to do, mark queue entry as confirmed if the entry
	 is unconfirmed */
      debug_printf("cups-browsed: Entry for %s (URI: %s) already exists.\n",
		   p->name, p->uri);
      if (p->status == STATUS_UNCONFIRMED ||
	  p->status == STATUS_DISAPPEARED) {
	p->status = STATUS_CONFIRMED;
	p->timeout = (time_t) -1;
	debug_printf("cups-browsed: Marking entry for %s (URI: %s) as confirmed.\n",
		     p->name, p->uri);
      }

    }
    if (p->host[0] == '\0') {
      free (p->host);
      p->host = strdup(remote_host);
    }
    if (p->ip && p->ip[0] == '\0') {
      free (p->ip);
      p->ip = (ip !=NULL ? strdup(ip) : NULL);
    }
    if (p->service_name[0] == '\0' && name) {
      free (p->service_name);
      p->service_name = strdup(name);
    }
    if (p->type[0] == '\0' && type) {
      free (p->type);
      p->type = strdup(type);
    }
    if (p->domain[0] == '\0' && domain) {
      free (p->domain);
      p->domain = strdup(domain);
    }
  } else {

    /* We need to create a local queue pointing to the
       discovered printer */
    p = create_local_queue (local_queue_name, uri, remote_host, ip,
			    name ? name : "", type, domain, pdl, color, duplex,
			    remote_queue, is_cups_queue);
  }

  free (backup_queue_name);
  free (remote_host);
  free (pdl);
  free (remote_queue);

  if (p)
    debug_printf("cups-browsed: Bonjour IDs: Service name: \"%s\", "
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

  if (r == NULL)
    return;

  /* Called whenever a service has been resolved successfully or timed out */

  switch (event) {

  /* Resolver error */
  case AVAHI_RESOLVER_FAILURE:
    debug_printf("cups-browsed: Avahi-Resolver: Failed to resolve service '%s' of type '%s' in domain '%s': %s\n",
		 name, type, domain,
		 avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
    break;

  /* New remote printer found */
  case AVAHI_RESOLVER_FOUND: {
    AvahiStringList *rp_entry, *adminurl_entry;
    char *rp_key, *rp_value, *adminurl_key, *adminurl_value;

    debug_printf("cups-browsed: Avahi Resolver: Service '%s' of type '%s' in domain '%s'.\n",
		 name, type, domain);

    rp_entry = avahi_string_list_find(txt, "rp");
    if (rp_entry)
      avahi_string_list_get_pair(rp_entry, &rp_key, &rp_value, NULL);
    else {
      rp_key = strdup("rp");
      rp_value = strdup("");
    }
    adminurl_entry = avahi_string_list_find(txt, "adminurl");
    if (adminurl_entry)
      avahi_string_list_get_pair(adminurl_entry, &adminurl_key,
				 &adminurl_value, NULL);
    else {
      adminurl_key = strdup("adminurl");
      if ((adminurl_value = malloc(strlen(host_name) + 8)) != NULL)
	sprintf(adminurl_value, "http://%s", host_name);
      else
	adminurl_value = strdup("");
    }

    if (rp_key && rp_value && adminurl_key && adminurl_value &&
	!strcasecmp(rp_key, "rp") && !strcasecmp(adminurl_key, "adminurl")) {
      /* Determine the remote printer's IP */
      if (IPBasedDeviceURIs != IP_BASED_URIS_NO ||
	  (!browseallow_all && cupsArrayCount(browseallow) > 0)) {
	struct sockaddr saddr;
	struct sockaddr *addr = &saddr;
	char addrstr[256];
	int addrfound = 0;
	if (address->proto == AVAHI_PROTO_INET &&
	    IPBasedDeviceURIs != IP_BASED_URIS_IPV6_ONLY) {
	  avahi_address_snprint(addrstr, sizeof(addrstr), address);
	  addr->sa_family = AF_INET;
	  if (inet_aton(addrstr,
			&((struct sockaddr_in *) addr)->sin_addr) &&
	      allowed(addr))
	    addrfound = 1;
	} else if (address->proto == AVAHI_PROTO_INET6 &&
		   interface != AVAHI_IF_UNSPEC &&
		   IPBasedDeviceURIs != IP_BASED_URIS_IPV4_ONLY) {
	  char ifname[IF_NAMESIZE];
	  addrstr[0] = '[';
	  avahi_address_snprint(addrstr + 1, sizeof(addrstr) - 1, address);
	  addr->sa_family = AF_INET6;
	  if (inet_pton(AF_INET6, addrstr + 1,
			&((struct sockaddr_in6 *) addr)->sin6_addr) &&
	      allowed(addr)) {
	    snprintf(addrstr + strlen(addrstr), sizeof(addrstr) -
		     strlen(addrstr), "+%s]",
		     if_indextoname(interface, ifname));
	    addrfound = 1;
	  }
	}
	if (addrfound == 1) {
	  /* Check remote printer type and create appropriate local queue to
	     point to it */
	  if (IPBasedDeviceURIs != IP_BASED_URIS_NO) {
	    debug_printf("cups-browsed: Avahi Resolver: Service '%s' of type '%s' in domain '%s' with IP address %s.\n",
			 name, type, domain, addrstr);
	    generate_local_queue(host_name, addrstr, port, rp_value, name, type, domain, txt);
	  } else
	    generate_local_queue(host_name, NULL, port, rp_value, name, type, domain, txt);
	} else
	  debug_printf("cups-browsed: Avahi Resolver: Service '%s' of type '%s' in domain '%s' skipped, could not determine IP address.\n",
		       name, type, domain);
      } else {
	/* Check remote printer type and create appropriate local queue to
	   point to it */
	generate_local_queue(host_name, NULL, port, rp_value, name, type, domain, txt);
      }
    }

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

  avahi_service_resolver_free(r);

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
  if (b == NULL)
    return;

  /* Called whenever a new services becomes available on the LAN or
     is removed from the LAN */

  switch (event) {

  /* Avahi browser error */
  case AVAHI_BROWSER_FAILURE:

    debug_printf("cups-browsed: Avahi Browser: ERROR: %s\n",
		 avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
    g_main_loop_quit(gmainloop);
    return;

  /* New service (remote printer) */
  case AVAHI_BROWSER_NEW:

    /* Ignore events from the local machine */
    if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
      break;

    debug_printf("cups-browsed: Avahi Browser: NEW: service '%s' of type '%s' in domain '%s'\n",
		 name, type, domain);

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
    remote_printer_t *p, *q, *r;
    int i;

    /* Ignore events from the local machine */
    if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
      break;

    debug_printf("cups-browsed: Avahi Browser: REMOVE: service '%s' of type '%s' in domain '%s'\n",
		 name, type, domain);

    /* Check whether we have listed this printer */
    for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!strcasecmp(p->service_name, name) &&
	  !strcasecmp(p->type, type) &&
	  !strcasecmp(p->domain, domain))
	break;
    if (p) {
      q = NULL;
      if (p->duplicate_of) {
	/* "master printer" of this duplicate */
	q = p->duplicate_of;
	q->num_duplicates --;
	if (q->status != STATUS_DISAPPEARED) {
	  debug_printf("cups-browsed: Removing the duplicate printer %s on host %s, scheduling its master printer %s on host %s for update, to assure it will have the correct device URI.\n",
		       p->name, p->host, q->name, q->host);
	  /* Schedule for update, so that an implicitclass:... URI gets
	     removed if not needed any more */
	  q->status = STATUS_TO_BE_CREATED;
	  q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
	}
	q = NULL;
      } else {
	/* Check whether this queue has a duplicate from another server and
	   find it */
	for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
	     q;
	     q = (remote_printer_t *)cupsArrayNext(remote_printers))
	  if (!strcasecmp(q->name, p->name) &&
	      strcasecmp(q->host, p->host) &&
	      q->duplicate_of)
	    break;
      }
      if (q) {
	/* Remove the data of the disappeared remote printer */
	free (p->uri);
	free (p->host);
	if (p->ip) free (p->ip);
	free (p->service_name);
	free (p->type);
	free (p->domain);
	cupsFreeOptions(p->num_options, p->options);
	if (p->ppd) free (p->ppd);
	if (p->model) free (p->model);
	if (p->ifscript) free (p->ifscript);
	/* Replace the data with the data of the duplicate printer */
	p->uri = strdup(q->uri);
	p->host = strdup(q->host);
	p->ip = (q->ip != NULL ? strdup(q->ip) : NULL);
	p->service_name = strdup(q->service_name);
	p->type = strdup(q->type);
	p->domain = strdup(q->domain);
	p->num_duplicates --;
	for (i = 0; i < q->num_options; i ++)
	  p->num_options = cupsAddOption(strdup(q->options[i].name),
					 strdup(q->options[i].value),
					 p->num_options, &(p->options));
	if (q->ppd) p->ppd = strdup(q->ppd);
	if (q->model) p->model = strdup(q->model);
	if (q->ifscript) p->ifscript = strdup(q->ifscript);
	for (r = (remote_printer_t *)cupsArrayFirst(remote_printers);
	     r;
	     r = (remote_printer_t *)cupsArrayNext(remote_printers))
	  if (!strcasecmp(p->name, r->name) &&
	      strcasecmp(p->host, r->host) &&
	      r->duplicate_of)
	    r->duplicate_of = p;
	/* Schedule this printer for updating the CUPS queue */
	p->status = STATUS_TO_BE_CREATED;
	p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
	/* Schedule the duplicate printer entry for removal */
	q->status = STATUS_DISAPPEARED;
	q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;

	debug_printf("cups-browsed: Printer %s diasappeared, replacing by backup on host %s with URI %s.\n",
		     p->name, p->host, p->uri);
      } else {

	/* Schedule CUPS queue for removal */
	p->status = STATUS_DISAPPEARED;
	p->timeout = time(NULL) + TIMEOUT_REMOVE;

	debug_printf("cups-browsed: Printer %s (Host: %s, URI: %s) disappeared and no duplicate available, or a duplicate of another printer, removing entry.\n",
		     p->name, p->host, p->uri);

      }

      debug_printf("cups-browsed: Bonjour IDs: Service name: \"%s\", Service type: \"%s\", Domain: \"%s\"\n",
		   p->service_name, p->type, p->domain);

      recheck_timer ();
    }
    break;
  }

  /* All cached Avahi events are treated now */
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    debug_printf("cups-browsed: Avahi Browser: %s\n",
		 event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
		 "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
    break;
  }

}

void avahi_browser_shutdown() {
  remote_printer_t *p;

  avahi_present = 0;

  /* Remove all queues which we have set up based on Bonjour discovery*/
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {
    if (p->type && p->type[0]) {
      p->status = STATUS_DISAPPEARED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
    }
  }
  handle_cups_queues(NULL);

  /* Free the data structures for Bonjour browsing */
  if (sb1) {
    avahi_service_browser_free(sb1);
    sb1 = NULL;
  }
  if (sb2) {
    avahi_service_browser_free(sb2);
    sb2 = NULL;
  }

  /* Switch on auto shutdown mode */
  if (autoshutdown_avahi) {
    autoshutdown = 1;
    debug_printf("cups-browsed: Avahi server disappeared, switching to auto shutdown mode ...\n");
    /* If there are no printers or no jobs schedule the shutdown in
       autoshutdown_timeout seconds */
    if (!autoshutdown_exec_id &&
	(cupsArrayCount(remote_printers) == 0 ||
	 (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
      debug_printf ("cups-browsed: We entered auto shutdown mode and no printers are there to make available or no jobs on them, shutting down in %d sec...\n", autoshutdown_timeout);
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

    debug_printf("cups-browsed: Avahi server connection got available, setting up service browsers.\n");

    /* Create the service browsers */
    if (!sb1)
      if (!(sb1 =
	    avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				      "_ipp._tcp", NULL, 0, browse_callback,
				      c))) {
	debug_printf("cups-browsed: ERROR: Failed to create service browser for IPP: %s\n",
		     avahi_strerror(avahi_client_errno(c)));
      }
    if (!sb2)
      if (!(sb2 =
	    avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				      "_ipps._tcp", NULL, 0, browse_callback,
				      c))) {
	debug_printf("cups-browsed: ERROR: Failed to create service browser for IPPS: %s\n",
		     avahi_strerror(avahi_client_errno(c)));
      }

    avahi_present = 1;
    
    /* switch off auto shutdown mode */
    if (autoshutdown_avahi) {
      autoshutdown = 0;
      debug_printf("cups-browsed: Avahi server available, switching to permanent mode ...\n");
      /* If there is still an active auto shutdown timer, kill it */
      if (autoshutdown_exec_id) {
	debug_printf ("cups-browsed: We have left auto shutdown mode, killing auto shutdown timer.\n");
	g_source_remove(autoshutdown_exec_id);
	autoshutdown_exec_id = 0;
      }
    }

    break;

  /* Avahi client error */
  case AVAHI_CLIENT_FAILURE:

    if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED) {
      debug_printf("cups-browsed: Avahi server disappeared, shutting down service browsers, removing Bonjour-discovered print queues.\n");
      avahi_browser_shutdown();
      /* Renewing client */
      avahi_client_free(client);
      client = avahi_client_new(avahi_glib_poll_get(glib_poll),
				AVAHI_CLIENT_NO_FAIL,
				client_callback, NULL, &error);
      if (!client) {
	debug_printf("cups-browsed: ERROR: Failed to create client: %s\n",
		     avahi_strerror(error));
	BrowseRemoteProtocols &= ~BROWSE_DNSSD;
	avahi_shutdown();
      }
    } else {
      debug_printf("cups-browsed: ERROR: Avahi server connection failure: %s\n",
		   avahi_strerror(avahi_client_errno(c)));
      g_main_loop_quit(gmainloop);
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
	debug_printf("cups-browsed: ERROR: Failed to create glib poll object.\n");
	goto avahi_init_fail;
      }

    /* Allocate a new client */
    if (!client)
      client = avahi_client_new(avahi_glib_poll_get(glib_poll),
				AVAHI_CLIENT_NO_FAIL,
				client_callback, NULL, &error);

    /* Check wether creating the client object succeeded */
    if (!client) {
      debug_printf("cups-browsed: ERROR: Failed to create client: %s\n",
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
		    const char *info)
{
  char scheme[32];
  char username[64];
  char host[HTTP_MAX_HOST];
  char resource[HTTP_MAX_URI];
  int port;
  netif_t *iface;
  char local_resource[HTTP_MAX_URI];
  char *c;
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
    debug_printf("cups-browsed: ignoring own broadcast on %s\n",
		 iface->address);
    return;
  }

  if (strncasecmp (resource, "/printers/", 10) &&
      strncasecmp (resource, "/classes/", 9)) {
    debug_printf("cups-browsed: don't understand URI: %s\n", uri);
    return;
  }

  strncpy (local_resource, resource + 1, sizeof (local_resource) - 1);
  local_resource[sizeof (local_resource) - 1] = '\0';
  c = strchr (local_resource, '?');
  if (c)
    *c = '\0';

  debug_printf("cups-browsed: browsed queue name is %s\n",
	       local_resource + 9);

  printer = generate_local_queue(host, NULL, port, local_resource,
				 info ? info : "",
				 "", "", NULL);

  if (printer) {
    if (printer->status == STATUS_TO_BE_CREATED)
      printer->status = STATUS_BROWSE_PACKET_RECEIVED;
    else {
      printer->status = STATUS_DISAPPEARED;
      printer->timeout = time(NULL) + BrowseTimeout;
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
  char info[1024];
  char *c = NULL, *end = NULL;

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
    debug_printf("cups-browsed: browse packet from %s disallowed\n",
		 remote_host);
    return TRUE;
  }

  debug_printf("cups-browsed: browse packet received from %s\n",
	       remote_host);

  if (sscanf (packet, "%x%x%1023s", &type, &state, uri) < 3) {
    debug_printf("cups-browsed: incorrect browse packet format\n");
    return TRUE;
  }

  info[0] = '\0';

  /* do not read OOB */
  end = packet + sizeof(packet);
  c = strchr (packet, '\"');
  if (c >= end)
     return TRUE;

  if (c) {
    /* Skip location field */
    for (c++; c < end && *c != '\"'; c++)
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
    }
  }
  if (c >= end)
    return TRUE;

  if (!(type & CUPS_PRINTER_DELETE))
    found_cups_printer (remote_host, uri, info);

  recheck_timer ();

  /* Don't remove this I/O source */
  return TRUE;
}

static gboolean
update_netifs (gpointer data)
{
  struct ifaddrs *ifaddr, *ifa;
  netif_t *iface;

  update_netifs_sourceid = 0;
  if (getifaddrs (&ifaddr) == -1) {
    debug_printf("cups-browsed: unable to get interface addresses: %s\n",
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
      debug_printf ("cups-browsed: malloc failure\n");
      exit (1);
    }

    iface->address = malloc (HTTP_MAX_HOST);
    if (iface->address == NULL) {
      free (iface);
      debug_printf ("cups-browsed: malloc failure\n");
      exit (1);
    }

    iface->address[0] = '\0';
    switch (ifa->ifa_addr->sa_family) {
    case AF_INET:
      getnameinfo (ifa->ifa_addr, sizeof (struct sockaddr_in),
		   iface->address, HTTP_MAX_HOST,
		   NULL, 0, NI_NUMERICHOST);
      memcpy (&iface->broadcast, ifa->ifa_broadaddr,
	      sizeof (struct sockaddr_in));
      iface->broadcast.ipv4.sin_port = htons (BrowsePort);
      break;

    case AF_INET6:
      if (IN6_IS_ADDR_LINKLOCAL (&((struct sockaddr_in6 *)(ifa->ifa_addr))
				 ->sin6_addr))
	break;

      getnameinfo (ifa->ifa_addr, sizeof (struct sockaddr_in6),
		   iface->address, HTTP_MAX_HOST, NULL, 0, NI_NUMERICHOST);
      memcpy (&iface->broadcast, ifa->ifa_broadaddr,
	      sizeof (struct sockaddr_in6));
      iface->broadcast.ipv6.sin6_port = htons (BrowsePort);
      break;
    }

    if (iface->address[0]) {
      cupsArrayAdd (netifs, iface);
      debug_printf("cups-browsed: network interface at %s\n", iface->address);
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
      debug_printf ("cups-browsed: oversize packet not sent\n");
      continue;
    }

    debug_printf("cups-browsed: packet to send:\n%s", packet);

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
			const char *info)
{
  browsepoll_printer_t *printer = g_malloc (sizeof (browsepoll_printer_t));
  printer->uri_supported = g_strdup (uri_supported);
  printer->info = g_strdup (info);
  return printer;
}

static void
browsepoll_printer_free (gpointer data)
{
  browsepoll_printer_t *printer = data;
  free (printer->uri_supported);
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
  if (cupsLastError() > IPP_OK_CONFLICT) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: failed: %s\n",
		 context->server, context->port, cupsLastErrorString ());
    goto fail;
  }

  for (attr = ippFirstAttribute(response); attr;
       attr = ippNextAttribute(response)) {
    browsepoll_printer_t *printer;
    const char *uri, *info;

    while (attr && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
      attr = ippNextAttribute(response);

    if (!attr)
      break;

    uri = NULL;
    info = NULL;
    while (attr && ippGetGroupTag(attr) == IPP_TAG_PRINTER) {

      if (!strcasecmp (ippGetName(attr), "printer-uri-supported") &&
	  ippGetValueTag(attr) == IPP_TAG_URI)
	uri = ippGetString(attr, 0, NULL);
      else if (!strcasecmp (ippGetName(attr), "printer-info") &&
	       ippGetValueTag(attr) == IPP_TAG_TEXT)
	info = ippGetString(attr, 0, NULL);

      attr = ippNextAttribute(response);
    }

    if (uri) {
      found_cups_printer (context->server, uri, info);
      printer = new_browsepoll_printer (uri, info);
      printers = g_list_insert (printers, printer, 0);
    }

    if (!attr)
      break;
  }

  g_list_free_full (context->printers, browsepoll_printer_free);
  context->printers = printers;
  recheck_timer ();

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
  if (!response || ippGetStatusCode (response) > IPP_OK_CONFLICT) {
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
  http_t *conn = httpConnectEncrypt (context->server, context->port,
				     HTTP_ENCRYPT_IF_REQUESTED);

  if (conn == NULL) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: connection failure "
		 "attempting to cancel\n", context->server, context->port);
    return;
  }

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
  if (!response || ippGetStatusCode (response) > IPP_OK_CONFLICT)
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

  if (status == IPP_NOT_FOUND) {
    /* Subscription lease has expired. */
    debug_printf ("cups-browsed [BrowsePoll %s:%d]: Lease expired\n",
		  context->server, context->port);
    browse_poll_create_subscription (context, conn);
    get_printers = TRUE;
  } else if (status > IPP_OK_CONFLICT) {
    debug_printf("cupsd-browsed [BrowsePoll %s:%d]: failed: %s\n",
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
  found_cups_printer (server, printer->uri_supported, printer->info);
}

gboolean
browse_poll (gpointer data)
{
  browsepoll_t *context = data;
  http_t *conn = NULL;
  gboolean get_printers = FALSE;

  debug_printf ("cups-browsed: browse polling %s:%d\n",
		context->server, context->port);

  res_init ();

  conn = httpConnectEncrypt (context->server, context->port,
			     HTTP_ENCRYPT_IF_REQUESTED);
  if (conn == NULL) {
    debug_printf("cups-browsed [BrowsePoll %s:%d]: failed to connect\n",
		 context->server, context->port);
    goto fail;
  }

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

  /* do real stuff here */
  if (!BrowseLDAPDN)
  {
    fprintf(stderr, "cups-browsed: "
		    "Need to set BrowseLDAPDN to use LDAP browsing!\n");
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
	  fprintf(stderr, "cups-browsed: "
			  "Could not allocate memory for LDAP browse query filter!\n");
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
    recheck_timer();
  }

  /* Call a new timeout handler so that we run again */
  g_timeout_add_seconds (BrowseInterval, browse_ldap_poll, data);

  /* Stop this timeout handler, we called a new one */
  return FALSE;
}
#endif /* HAVE_LDAP */

int
compare_pointers (void *a, void *b, void *data)
{
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

int compare_remote_printers (remote_printer_t *a, remote_printer_t *b) {
  return strcasecmp(a->name, b->name);
}

static void
sigterm_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  /* Flag that we should stop and return... */
  g_main_loop_quit(gmainloop);
  debug_printf("cups-browsed: Caught signal %d, shutting down ...\n", sig);
}

static void
sigusr1_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  /* Turn off auto shutdown mode... */
  autoshutdown = 0;
  debug_printf("cups-browsed: Caught signal %d, switching to permanent mode ...\n", sig);
  /* If there is still an active auto shutdown timer, kill it */
  if (autoshutdown_exec_id) {
    debug_printf ("cups-browsed: We have left auto shutdown mode, killing auto shutdown timer.\n");
    g_source_remove(autoshutdown_exec_id);
    autoshutdown_exec_id = 0;
  }
}

static void
sigusr2_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  /* Turn on auto shutdown mode... */
  autoshutdown = 1;
  debug_printf("cups-browsed: Caught signal %d, switching to auto shutdown mode ...\n", sig);
  /* If there are no printers or no jobs schedule the shutdown in
     autoshutdown_timeout seconds */
  if (!autoshutdown_exec_id &&
      (cupsArrayCount(remote_printers) == 0 ||
       (autoshutdown_on == NO_JOBS && check_jobs() == 0))) {
    debug_printf ("cups-browsed: We entered auto shutdown mode and no printers are there to make available or no jobs on them, shutting down in %d sec...\n", autoshutdown_timeout);
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
  char *value = NULL, *ptr, *field;
  const char *delim = " \t,";
  int browse_allow_line_found = 0;
  int browse_deny_line_found = 0;
  int browse_order_line_found = 0;
  int browse_line_found = 0;
  browse_filter_t *filter = NULL;
  int browse_filter_options, exact_match, err;
  char errbuf[1024];

  if (!filename)
    filename = CUPS_SERVERROOT "/cups-browsed.conf";

  if ((fp = cupsFileOpen(filename, "r")) == NULL) {
    debug_printf("cups-browsed: unable to open configuration file; "
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
      /* We are still readin options from the command line ("-o ..."),
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
    
    debug_printf("cups-browsed: Reading config%s: %s %s\n",
		 (linenum < 0 ? " (from command line)" : ""), line, value);
    if ((!strcasecmp(line, "BrowseProtocols") ||
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
	  debug_printf("cups-browsed: Unknown protocol '%s'\n", p);

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
	debug_printf("cups-browsed: unable to realloc: ignoring BrowsePoll line\n");
	BrowsePoll = old;
      } else {
	char *colon, *slash;
	browsepoll_t *b = g_malloc0 (sizeof (browsepoll_t));
	debug_printf("cups-browsed: Adding BrowsePoll server: %s\n", value);
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
	debug_printf ("cups-browsed: BrowseAllow value \"%s\" not understood\n",
		      value);
      else {
	browse_allow_line_found = 1;
	browse_line_found = 1;
      }
    } else if (!strcasecmp(line, "BrowseDeny")) {
      if (read_browseallow_value (value, ALLOW_DENY))
	debug_printf ("cups-browsed: BrowseDeny value \"%s\" not understood\n",
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
	debug_printf ("cups-browsed: BrowseOrder value \"%s\" not understood\n",
		      value);
    } else if (!strcasecmp(line, "BrowseFilter") && value) {
      ptr = value;
      /* Skip whitw space */
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
      field = ptr;
      while (*ptr && !isspace(*ptr)) ptr ++;
      if (*ptr) {
	/* Mark end of the field name */
	*ptr = '\0';
	/* Skip white space until regexp or line end */
	ptr ++;
	while (*ptr && isspace(*ptr)) ptr ++;
      }
      filter->field = strdup(field);
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
	    debug_printf ("cups-browsed: BrowseFilter line with error in regular expression \"%s\": %s\n",
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
    } else if (!strcasecmp(line, "DomainSocket") && value) {
      if (value[0] != '\0')
	DomainSocket = strdup(value);
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
    } else if (!strcasecmp(line, "CreateRemoteRawPrinterQueues") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	CreateRemoteRawPrinterQueues = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	CreateRemoteRawPrinterQueues = 0;
    } else if (!strcasecmp(line, "CreateIPPPrinterQueues") && value) {
      if (!strcasecmp(value, "yes") || !strcasecmp(value, "true") ||
	  !strcasecmp(value, "on") || !strcasecmp(value, "1"))
	CreateIPPPrinterQueues = 1;
      else if (!strcasecmp(value, "no") || !strcasecmp(value, "false") ||
	  !strcasecmp(value, "off") || !strcasecmp(value, "0"))
	CreateIPPPrinterQueues = 0;
    } else if (!strcasecmp(line, "IPPPrinterQueueType") && value) {
      if (!strncasecmp(value, "Auto", 4))
	IPPPrinterQueueType = PPD_AUTO;
      else if (!strncasecmp(value, "PPD", 3))
	IPPPrinterQueueType = PPD_ONLY;
      else if (!strncasecmp(value, "NoPPD", 5))
	IPPPrinterQueueType = PPD_NEVER;
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
	  debug_printf("cups-browsed: Turning on auto shutdown mode.\n");
	} else if (!strcasecmp(p, "Off") || !strcasecmp(p, "No") ||
	    !strcasecmp(p, "False") || !strcasecmp(p, "0")) {
	  autoshutdown = 0;
	  debug_printf("cups-browsed: Turning off auto shutdown mode (permanent mode).\n");
	} else if (!strcasecmp(p, "avahi")) {
	  autoshutdown_avahi = 1;
	  debug_printf("cups-browsed: Turning on auto shutdown control by appearing and disappearing of the Avahi server.\n");
	} else if (strcasecmp(p, "none"))
	  debug_printf("cups-browsed: Unknown mode '%s'\n", p);
	p = strtok_r (NULL, delim, &saveptr);
      }
    } else if (!strcasecmp(line, "AutoShutdownTimeout") && value) {
      int t = atoi(value);
      if (t >= 0) {
	autoshutdown_timeout = t;
	debug_printf("cups-browsed: Set auto shutdown timeout to %d sec.\n",
		     t);
      } else
	debug_printf("cups-browsed: Invalid auto shutdown timeout value: %d\n",
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
	debug_printf("cups-browsed: Set auto shutdown inactivity type to no %s.\n",
		     autoshutdown_on == NO_QUEUES ? "queues" : "jobs");
      else
	debug_printf("cups-browsed: Invalid auto shutdown inactivity type value: %s\n",
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
    debug_printf("cups-browsed: No \"Browse...\" line at all, accept all servers (\"BrowseOrder Deny,Allow\").\n");
  } else if (browse_order_line_found == 0) {
    /* No "BrowseOrder" line */
    if (browse_allow_line_found == 0) {
      /* Only "BrowseDeny" lines */
      browse_order = ORDER_DENY_ALLOW;
      debug_printf("cups-browsed: No \"BrowseOrder\" line and only \"BrowseDeny\" lines, accept all except what matches the \"BrowseDeny\" lines  (\"BrowseOrder Deny,Allow\").\n");
    } else if (browse_deny_line_found == 0) {
      /* Only "BrowseAllow" lines */
      browse_order = ORDER_ALLOW_DENY;
      debug_printf("cups-browsed: No \"BrowseOrder\" line and only \"BrowseAllow\" lines, deny all except what matches the \"BrowseAllow\" lines  (\"BrowseOrder Allow,Deny\").\n");
    } else {
      /* Default for "BrowseOrder" */
      browse_order = ORDER_DENY_ALLOW;
      debug_printf("cups-browsed: No \"BrowseOrder\" line, use \"BrowseOrder Deny,Allow\" as default.\n");
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
  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
    if (!strcmp (key, "ActiveConnections")) {
      debug_printf ("cups-browsed: NetworkManager ActiveConnections changed\n");
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
  if (printer->cups_browsed_controlled) {
    /* Queue found, add to our list */
    p = create_local_queue (name,
			    printer->device_uri,
			    "", "", "", "", "", NULL, 0, 0, NULL, 1);
    if (p) {
      /* Mark as unconfirmed, if no Avahi report of this queue appears
	 in a certain time frame, we will remove the queue */
      p->status = STATUS_UNCONFIRMED;

      if (BrowseRemoteProtocols & BROWSE_CUPS)
	p->timeout = time(NULL) + BrowseTimeout;
      else
	p->timeout = time(NULL) + TIMEOUT_CONFIRM;

      p->duplicate_of = NULL;
      p->num_duplicates = 0;
      debug_printf("cups-browsed: Found CUPS queue %s (URI: %s) from previous session.\n",
		   p->name, p->uri);
    } else {
      debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
      exit(1);
    }
  }
}

int main(int argc, char*argv[]) {
  int ret = 1;
  http_t *http;
  int i;
  const char *val;
  remote_printer_t *p;
  GDBusProxy *proxy = NULL;
  GError *error = NULL;
  int subscription_id = 0;

  /* Initialise the command_line_config array */
  command_line_config = cupsArrayNew(compare_pointers, NULL);

  /* Initialise the browseallow array */
  browseallow = cupsArrayNew(compare_pointers, NULL);

  /* Initialise the browsefilter array */
  browsefilter = cupsArrayNew(compare_pointers, NULL);

  /* Read command line options */
  if (argc >= 2)
    for (i = 1; i < argc; i++)
      if (!strcasecmp(argv[i], "--debug") || !strcasecmp(argv[i], "-d") ||
	  !strncasecmp(argv[i], "-v", 2)) {
	/* Turn on debug mode if requested */
	debug = 1;
	debug_printf("cups-browsed: Reading command line option %s, turning on debug mode.\n",
		     argv[i]);
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
	  debug_printf("cups-browsed: Reading command line option -c %s, using alternative configuration file.\n",
		       alt_config_file);
	} else {
	  fprintf(stderr,
		  "cups-browsed: Reading command line option -c, no alternative configuration file name supplied.\n\n");
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
	  debug_printf("cups-browsed: Reading command line option -o %s, applying extra configuration option.\n",
		 val);
	} else {
	  fprintf(stderr,
		  "cups-browsed: Reading command line option -o, no extra configuration option supplied.\n\n");
	  goto help;
	}     
      } else if (!strncasecmp(argv[i], "--autoshutdown-timeout", 22)) {
	debug_printf("cups-browsed: Reading command line: %s\n", argv[i]);
	if (argv[i][22] == '=' && argv[i][23])
	  val = argv[i] + 23;
	else if (!argv[i][22] && i < argc -1) {
	  i++;
	  debug_printf("cups-browsed: Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "cups-browsed: Expected auto shutdown timeout setting after \"--autoshutdown-timeout\" option.\n\n");
	  goto help;
	}
	int t = atoi(val);
	if (t >= 0) {
	  autoshutdown_timeout = t;
	  debug_printf("cups-browsed: Set auto shutdown timeout to %d sec.\n",
		       t);
	} else {
	  debug_printf("cups-browsed: Invalid auto shutdown timeout value: %d\n\n",
		       t);
	  goto help;
	}
      } else if (!strncasecmp(argv[i], "--autoshutdown-on", 17)) {
	debug_printf("cups-browsed: Reading command line: %s\n", argv[i]);
	if (argv[i][17] == '=' && argv[i][18])
	  val = argv[i] + 18;
	else if (!argv[i][17] && i < argc - 1) {
	  i++;
	  debug_printf("cups-browsed: Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "cups-browsed: Expected auto shutdown inactivity type (\"no-queues\" or \"no-jobs\") after \"--autoshutdown-on\" option.\n\n");
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
	  debug_printf("cups-browsed: Set auto shutdown inactivity type to no %s.\n",
		       autoshutdown_on == NO_QUEUES ? "queues" : "jobs");
	else
	  debug_printf("cups-browsed: Invalid auto shutdown inactivity type value: %s\n",
		       val);
      } else if (!strncasecmp(argv[i], "--autoshutdown", 14)) {
	debug_printf("cups-browsed: Reading command line: %s\n", argv[i]);
	if (argv[i][14] == '=' && argv[i][15])
	  val = argv[i] + 15;
	else if (!argv[i][14] && i < argc -1) {
	  i++;
	  debug_printf("cups-browsed: Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "cups-browsed: Expected auto shutdown setting after \"--autoshutdown\" option.\n\n");
	  goto help;
	}
	if (!strcasecmp(val, "On") || !strcasecmp(val, "Yes") ||
	    !strcasecmp(val, "True") || !strcasecmp(val, "1")) {
	  autoshutdown = 1;
	  debug_printf("cups-browsed: Turning on auto shutdown mode.\n");
	} else if (!strcasecmp(val, "Off") || !strcasecmp(val, "No") ||
	    !strcasecmp(val, "False") || !strcasecmp(val, "0")) {
	  autoshutdown = 0;
	  debug_printf("cups-browsed: Turning off auto shutdown mode (permanent mode).\n");
	} else if (!strcasecmp(val, "avahi")) {
	  autoshutdown_avahi = 1;
	  debug_printf("cups-browsed: Turning on auto shutdown control by appearing and disappearing of the Avahi server.\n");
	} else if (strcasecmp(val, "none")) {
	  debug_printf("cups-browsed: Unknown mode '%s'\n\n", val);
	  goto help;
	}
      } else if (!strcasecmp(argv[i], "--version") ||
		 !strcasecmp(argv[i], "--help") || !strcasecmp(argv[i], "-h")) {
	/* Help!! */
	goto help;
      } else {
	/* Unknown option */
	fprintf(stderr,
		"cups-browsed: Reading command line option %s, unknown command line option.\n\n",
		argv[i]);
        goto help;
      }

  debug_printf("cups-browsed of cups-filters version "VERSION" starting.\n");
  
  /* Read in cups-browsed.conf */
  read_configuration (alt_config_file);

  /* Set the CUPS_SERVER environment variable to assure that cups-browsed
     always works with the local CUPS daemon and never with a remote one
     specified by a client.conf file */
#ifdef CUPS_DEFAULT_DOMAINSOCKET
  if (DomainSocket == NULL)
    DomainSocket = CUPS_DEFAULT_DOMAINSOCKET;
#endif
  if (DomainSocket != NULL) {
    struct stat sockinfo;               /* Domain socket information */
    if (!stat(DomainSocket, &sockinfo) &&
        (sockinfo.st_mode & S_IROTH) != 0 &&
        (sockinfo.st_mode & S_IWOTH) != 0)
      setenv("CUPS_SERVER", DomainSocket, 1);
    else
      setenv("CUPS_SERVER", "localhost", 1);
  } else
    setenv("CUPS_SERVER", "localhost", 1);

  if (BrowseLocalProtocols & BROWSE_DNSSD) {
    fprintf(stderr, "cups-browsed: Local support for DNSSD not implemented\n");
    BrowseLocalProtocols &= ~BROWSE_DNSSD;
  }

  if (BrowseLocalProtocols & BROWSE_LDAP) {
    fprintf(stderr, "cups-browsed: Local support for LDAP not implemented\n");
    BrowseLocalProtocols &= ~BROWSE_LDAP;
  }

#ifndef HAVE_AVAHI
  if (BrowseRemoteProtocols & BROWSE_DNSSD) {
    fprintf(stderr, "cups-browsed: Remote support for DNSSD not supported\n");
    BrowseRemoteProtocols &= ~BROWSE_DNSSD;
  }
#endif /* HAVE_AVAHI */

#ifndef HAVE_LDAP
  if (BrowseRemoteProtocols & BROWSE_LDAP) {
    fprintf(stderr, "cups-browsed: Remote support for LDAP not supported\n");
    BrowseRemoteProtocols &= ~BROWSE_LDAP;
  }
#endif /* HAVE_LDAP */

  /* Wait for CUPS daemon to start */
  while ((http = http_connect_local ()) == NULL)
    sleep(1);

  /* Initialise the array of network interfaces */
  netifs = cupsArrayNew(compare_pointers, NULL);
  update_netifs (NULL);

  local_printers = g_hash_table_new_full (g_str_hash,
					  g_str_equal,
					  g_free,
					  free_local_printer);

  /* Read out the currently defined CUPS queues and find the ones which we
     have added in an earlier session */
  update_local_printers ();
  if ((val = get_cups_default_printer()) != NULL)
    default_printer = strdup(val);
  remote_printers = cupsArrayNew((cups_array_func_t)compare_remote_printers,
				 NULL);
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
  debug_printf("cups-browsed: Using signal handler SIGSET\n");
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
  debug_printf("cups-browsed: Using signal handler SIGACTION\n");
#else
  signal(SIGTERM, sigterm_handler);
  signal(SIGINT, sigterm_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);
  debug_printf("cups-browsed: Using signal handler SIGNAL\n");
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
      debug_printf ("cups-browsed: We set auto shutdown mode and no printers are there to make available or no jobs on them, shutting down in %d sec...\n", autoshutdown_timeout);
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
      debug_printf("cups-browsed: failed to create CUPS Browsing socket: %s\n",
		   strerror (errno));
    } else {
      struct sockaddr_in addr;
      memset (&addr, 0, sizeof (addr));
      addr.sin_addr.s_addr = htonl (INADDR_ANY);
      addr.sin_family = AF_INET;
      addr.sin_port = htons (BrowsePort);
      if (bind (browsesocket, (struct sockaddr *)&addr, sizeof (addr))) {
	debug_printf("cups-browsed: failed to bind CUPS Browsing socket: %s\n",
		     strerror (errno));
	close (browsesocket);
	browsesocket = -1;
      } else {
	int on = 1;
	if (setsockopt (browsesocket, SOL_SOCKET, SO_BROADCAST,
			&on, sizeof (on))) {
	  debug_printf("cups-browsed: failed to allow broadcast: %s\n",
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
    debug_printf("cups-browsed: nothing left to do\n");
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
      debug_printf ("cups-browsed: will send browse data every %ds\n",
		    BrowseInterval);
      g_idle_add (send_browse_data, NULL);
  }

#ifdef HAVE_LDAP
  if (BrowseRemoteProtocols & BROWSE_LDAP) {
      debug_printf ("cups-browsed: will browse poll LDAP every %ds\n",
		    BrowseInterval);
      g_idle_add (browse_ldap_poll, NULL);
  }
#endif /* HAVE_LDAP */

  if (BrowsePoll) {
    size_t index;
    for (index = 0;
	 index < NumBrowsePoll;
	 index++) {
      debug_printf ("cups-browsed: will browse poll %s every %ds\n",
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
    fprintf (stderr, "cups-browsed: Error creating cups notify handler: %s", error->message);
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
    debug_printf ("cups-browsed: No printers found to make available, shutting down in %d sec...\n", autoshutdown_timeout);
    autoshutdown_exec_id =
      g_timeout_add_seconds (autoshutdown_timeout, autoshutdown_execute, NULL);
  }

  g_main_loop_run (gmainloop);

  debug_printf("cups-browsed: main loop exited\n");
  g_main_loop_unref (gmainloop);
  gmainloop = NULL;
  ret = 0;

fail:

  /* Clean up things */

  if (proxy)
    g_object_unref (proxy);

  /* Remove all queues which we have set up */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {
    p->status = STATUS_DISAPPEARED;
    p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
  }
  handle_cups_queues(NULL);

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

  if (BrowseLocalProtocols & BROWSE_CUPS)
    g_list_free_full (browse_data, browse_data_free);

  return ret;

 help:

  fprintf(stderr,
	  "cups-browsed of cups-filters version "VERSION"\n\n"
	  "Usage: cups-browsed [options]\n"
	  "Options:\n"
	  "  -c cups-browsed.conf    Set alternative cups-browsed.conf file to use.\n"
	  "  -d\n"
	  "  -v\n"
	  "  --debug                 Run in debug mode (verbose logging).\n"
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

#ifdef HAVE_CUPS_1_6
/* The following code uses a lot of CUPS >= 1.6 specific stuff.
   The following code is only needed for create_local_queue()
   to set up local queues for non-CUPS printer broadcasts
   that is disabled in create_local_queue() for older CUPS <= 1.5.4.
   Accordingly the following code is also disabled here for CUPS < 1.6. */

/*
 * The code below is borrowed from the CUPS 2.1.x upstream repository
 * (via patches attached to https://www.cups.org/str.php?L4258). This
 * allows for automatic PPD generation already with CUPS versions older
 * than CUPS 2.1.x. We have also an additional test and development
 * platform for this code. Taken from cups/ppd-cache.c,
 * cups/string-private.h, cups/string.c.
 * 
 * The advantage of PPD generation instead of working with System V
 * interface scripts is that the print dialogs of the clients do not
 * need to ask the printer for its options via IPP. So we have access
 * to the options with the current PPD-based dialogs and can even share
 * the automatically created print queue to other CUPS-based machines
 * without problems.
 */


typedef struct _pwg_finishings_s	/**** PWG finishings mapping data ****/
{
  ipp_finishings_t	value;		/* finishings value */
  int			num_options;	/* Number of options to apply */
  cups_option_t		*options;	/* Options to apply */
} _pwg_finishings_t;

#define _PWG_EQUIVALENT(x, y)	(abs((x)-(y)) < 2)

static void	pwg_ppdize_name(const char *ipp, char *name, size_t namesize);
static void	pwg_ppdize_resolution(ipp_attribute_t *attr, int element, int *xres, int *yres, char *name, size_t namesize);

int			/* O - 1 on match, 0 otherwise */
_cups_isalnum(int ch)			/* I - Character to test */
{
  return ((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

int			/* O - 1 on match, 0 otherwise */
_cups_isalpha(int ch)			/* I - Character to test */
{
  return ((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

int			/* O - 1 on match, 0 otherwise */
_cups_islower(int ch)			/* I - Character to test */
{
  return (ch >= 'a' && ch <= 'z');
}

int			/* O - 1 on match, 0 otherwise */
_cups_isspace(int ch)			/* I - Character to test */
{
  return (ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' ||
          ch == '\v');
}

int			/* O - 1 on match, 0 otherwise */
_cups_isupper(int ch)			/* I - Character to test */
{
  return (ch >= 'A' && ch <= 'Z');
}

int			/* O - Converted character */
_cups_tolower(int ch)			/* I - Character to convert */
{
  return (_cups_isupper(ch) ? ch - 'A' + 'a' : ch);
}

int			/* O - Converted character */
_cups_toupper(int ch)			/* I - Character to convert */
{
  return (_cups_islower(ch) ? ch - 'a' + 'A' : ch);
}

#ifndef HAVE_STRLCPY
/*
 * '_cups_strlcpy()' - Safely copy two strings.
 */

size_t					/* O - Length of string */
strlcpy(char       *dst,		/* O - Destination string */
	const char *src,		/* I - Source string */
	size_t      size)		/* I - Size of destination string buffer */
{
  size_t	srclen;			/* Length of source string */


 /*
  * Figure out how much room is needed...
  */

  size --;

  srclen = strlen(src);

 /*
  * Copy the appropriate amount...
  */

  if (srclen > size)
    srclen = size;

  memmove(dst, src, srclen);
  dst[srclen] = '\0';

  return (srclen);
}
#endif /* !HAVE_STRLCPY */

/*
 * '_cupsStrFormatd()' - Format a floating-point number.
 */

char *					/* O - Pointer to end of string */
_cupsStrFormatd(char         *buf,	/* I - String */
                char         *bufend,	/* I - End of string buffer */
		double       number,	/* I - Number to format */
                struct lconv *loc)	/* I - Locale data */
{
  char		*bufptr,		/* Pointer into buffer */
		temp[1024],		/* Temporary string */
		*tempdec,		/* Pointer to decimal point */
		*tempptr;		/* Pointer into temporary string */
  const char	*dec;			/* Decimal point */
  int		declen;			/* Length of decimal point */


 /*
  * Format the number using the "%.12f" format and then eliminate
  * unnecessary trailing 0's.
  */

  snprintf(temp, sizeof(temp), "%.12f", number);
  for (tempptr = temp + strlen(temp) - 1;
       tempptr > temp && *tempptr == '0';
       *tempptr-- = '\0');

 /*
  * Next, find the decimal point...
  */

  if (loc && loc->decimal_point)
  {
    dec    = loc->decimal_point;
    declen = (int)strlen(dec);
  }
  else
  {
    dec    = ".";
    declen = 1;
  }

  if (declen == 1)
    tempdec = strchr(temp, *dec);
  else
    tempdec = strstr(temp, dec);

 /*
  * Copy everything up to the decimal point...
  */

  if (tempdec)
  {
    for (tempptr = temp, bufptr = buf;
         tempptr < tempdec && bufptr < bufend;
	 *bufptr++ = *tempptr++);

    tempptr += declen;

    if (*tempptr && bufptr < bufend)
    {
      *bufptr++ = '.';

      while (*tempptr && bufptr < bufend)
        *bufptr++ = *tempptr++;
    }

    *bufptr = '\0';
  }
  else
  {
    strlcpy(buf, temp, (size_t)(bufend - buf + 1));
    bufptr = buf + strlen(buf);
  }

  return (bufptr);
}


/*
 * '_cups_strcasecmp()' - Do a case-insensitive comparison.
 */

int				/* O - Result of comparison (-1, 0, or 1) */
_cups_strcasecmp(const char *s,	/* I - First string */
                 const char *t)	/* I - Second string */
{
  while (*s != '\0' && *t != '\0')
  {
    if (_cups_tolower(*s) < _cups_tolower(*t))
      return (-1);
    else if (_cups_tolower(*s) > _cups_tolower(*t))
      return (1);

    s ++;
    t ++;
  }

  if (*s == '\0' && *t == '\0')
    return (0);
  else if (*s != '\0')
    return (1);
  else
    return (-1);
}

/*
 * '_cups_strncasecmp()' - Do a case-insensitive comparison on up to N chars.
 */

int					/* O - Result of comparison (-1, 0, or 1) */
_cups_strncasecmp(const char *s,	/* I - First string */
                  const char *t,	/* I - Second string */
		  size_t     n)		/* I - Maximum number of characters to compare */
{
  while (*s != '\0' && *t != '\0' && n > 0)
  {
    if (_cups_tolower(*s) < _cups_tolower(*t))
      return (-1);
    else if (_cups_tolower(*s) > _cups_tolower(*t))
      return (1);

    s ++;
    t ++;
    n --;
  }

  if (n == 0)
    return (0);
  else if (*s == '\0' && *t == '\0')
    return (0);
  else if (*s != '\0')
    return (1);
  else
    return (-1);
}


/*
 * '_ppdCreateFromIPP()' - Create a PPD file describing the capabilities
 *                         of an IPP printer.
 */

char *					/* O - PPD filename or NULL on error */
_ppdCreateFromIPP(char   *buffer,	/* I - Filename buffer */
                  size_t bufsize,	/* I - Size of filename buffer */
		  ipp_t  *response)	/* I - Get-Printer-Attributes response */
{
  cups_file_t		*fp;		/* PPD file */
  ipp_attribute_t	*attr,		/* xxx-supported */
			*defattr,	/* xxx-default */
			*x_dim, *y_dim;	/* Media dimensions */
  ipp_t			*media_size;	/* Media size collection */
  char			make[256],	/* Make and model */
			*model,		/* Model name */
			ppdname[PPD_MAX_NAME];
		    			/* PPD keyword */
  int			i, j,		/* Looping vars */
			count,		/* Number of values */
			bottom,		/* Largest bottom margin */
			left,		/* Largest left margin */
			right,		/* Largest right margin */
			top;		/* Largest top margin */
  pwg_media_t		*pwg;		/* PWG media size */
  int			xres, yres;	/* Resolution values */
  struct lconv		*loc = localeconv();
					/* Locale data */


 /*
  * Range check input...
  */

  if (buffer)
    *buffer = '\0';

  if (!buffer || bufsize < 1 || !response)
    return (NULL);

 /*
  * Open a temporary file for the PPD...
  */

  if ((fp = cupsTempFile2(buffer, (int)bufsize)) == NULL)
    return (NULL);

 /*
  * Standard stuff for PPD file...
  */

  cupsFilePuts(fp, "*PPD-Adobe: \"4.3\"\n");
  cupsFilePuts(fp, "*FormatVersion: \"4.3\"\n");
  cupsFilePrintf(fp, "*FileVersion: \"%d.%d\"\n", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
  cupsFilePuts(fp, "*LanguageVersion: English\n");
  cupsFilePuts(fp, "*LanguageEncoding: ISOLatin1\n");
  cupsFilePuts(fp, "*PSVersion: \"(3010.000) 0\"\n");
  cupsFilePuts(fp, "*LanguageLevel: \"3\"\n");
  cupsFilePuts(fp, "*FileSystem: False\n");
  cupsFilePuts(fp, "*PCFileName: \"ippeve.ppd\"\n");

  if ((attr = ippFindAttribute(response, "printer-make-and-model", IPP_TAG_TEXT)) != NULL)
    strlcpy(make, ippGetString(attr, 0, NULL), sizeof(make));
  else
    strlcpy(make, "Unknown Printer", sizeof(make));

  if (!_cups_strncasecmp(make, "Hewlett Packard ", 16) ||
      !_cups_strncasecmp(make, "Hewlett-Packard ", 16))
  {
    model = make + 16;
    strlcpy(make, "HP", sizeof(make));
  }
  else if ((model = strchr(make, ' ')) != NULL)
    *model++ = '\0';
  else
    model = make;

  cupsFilePrintf(fp, "*Manufacturer: \"%s\"\n", make);
  cupsFilePrintf(fp, "*ModelName: \"%s\"\n", model);
  cupsFilePrintf(fp, "*Product: \"(%s)\"\n", model);
  cupsFilePrintf(fp, "*NickName: \"%s\"\n", model);
  cupsFilePrintf(fp, "*ShortNickName: \"%s\"\n", model);

  if ((attr = ippFindAttribute(response, "color-supported", IPP_TAG_BOOLEAN)) != NULL && ippGetBoolean(attr, 0))
    cupsFilePuts(fp, "*ColorDevice: True\n");
  else
    cupsFilePuts(fp, "*ColorDevice: False\n");

  cupsFilePrintf(fp, "*cupsVersion: %d.%d\n", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
  cupsFilePuts(fp, "*cupsSNMPSupplies: False\n");
  cupsFilePuts(fp, "*cupsLanguages: \"en\"\n");

 /*
  * Filters...
  */

  if ((attr = ippFindAttribute(response, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL)
  {
    int formatfound = 0;

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char *format = ippGetString(attr, i, NULL);
					/* PDL */
#if 0
      if (!_cups_strcasecmp(format, "application/pdf"))
        cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-pdf application/pdf 10 -\"\n");
      else if (!_cups_strcasecmp(format, "application/postscript"))
        cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-postscript application/postscript 10 -\"\n");
      else if (_cups_strcasecmp(format, "application/octet-stream") && _cups_strcasecmp(format, "application/vnd.hp-pcl") && _cups_strcasecmp(format, "text/plain"))
        cupsFilePrintf(fp, "*cupsFilter2: \"%s %s 10 -\"\n", format, format);
#endif
      /* For now cups-filters only supports these PPD files with PWG Raster
	 and PostScript, so create a PPD only for these formats */
      if (!_cups_strcasecmp(format, "image/pwg-raster")) {
        cupsFilePuts(fp, "*cupsFilter2: \"image/pwg-raster image/pwg-raster 10 -\"\n");
	formatfound = 1;
      } else if (!_cups_strcasecmp(format, "application/postscript")) {
        cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-postscript application/postscript 10 -\"\n");
	formatfound = 1;
      }
    }
    if (formatfound == 0) {
      debug_printf("cups-browsed: No data format suitable for PPD auto-generation supported by the printer, not generating PPD\n");
      unlink(buffer);
      return(NULL);
    }
  }

 /*
  * PageSize/PageRegion/ImageableArea/PaperDimension
  */

  if ((attr = ippFindAttribute(response, "media-bottom-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, bottom = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > bottom)
        bottom = ippGetInteger(attr, i);
  }
  else
    bottom = 1270;

  if ((attr = ippFindAttribute(response, "media-left-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, left = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > left)
        left = ippGetInteger(attr, i);
  }
  else
    left = 635;

  if ((attr = ippFindAttribute(response, "media-right-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, right = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > right)
        right = ippGetInteger(attr, i);
  }
  else
    right = 635;

  if ((attr = ippFindAttribute(response, "media-top-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, top = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > top)
        top = ippGetInteger(attr, i);
  }
  else
    top = 1270;

  if ((defattr = ippFindAttribute(response, "media-col-default", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-size", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      media_size = ippGetCollection(attr, 0);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

      if (x_dim && y_dim)
      {
        pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));
	strlcpy(ppdname, pwg->ppd, sizeof(ppdname));
      }
      else
	strlcpy(ppdname, "Unknown", sizeof(ppdname));
    }
    else
      strlcpy(ppdname, "Unknown", sizeof(ppdname));
  }

  if ((attr = ippFindAttribute(response, "media-size-supported", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    cupsFilePrintf(fp, "*OpenUI *PageSize: PickOne\n"
		       "*OrderDependency: 10 AnySetup *PageSize\n"
                       "*DefaultPageSize: %s\n", ppdname);
    if (ippGetCount(attr) == 0) {
      debug_printf("cups-browsed: No page sizes reported as supported by the printer, not generating PPD\n");
      unlink(buffer);
      return(NULL);
    }
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      media_size = ippGetCollection(attr, i);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

      if (x_dim && y_dim)
      {
        char	twidth[256],		/* Width string */
		tlength[256];		/* Length string */

        pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));

        _cupsStrFormatd(twidth, twidth + sizeof(twidth), pwg->width * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tlength, tlength + sizeof(tlength), pwg->length * 72.0 / 2540.0, loc);

        cupsFilePrintf(fp, "*PageSize %s: \"<</PageSize[%s %s]>>setpagedevice\"\n", pwg->ppd, twidth, tlength);
      }
    }
    cupsFilePuts(fp, "*CloseUI: *PageSize\n");

    cupsFilePrintf(fp, "*OpenUI *PageRegion: PickOne\n"
                       "*OrderDependency: 10 AnySetup *PageRegion\n"
                       "*DefaultPageRegion: %s\n", ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      media_size = ippGetCollection(attr, i);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

      if (x_dim && y_dim)
      {
        char	twidth[256],		/* Width string */
		tlength[256];		/* Length string */

        pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));

        _cupsStrFormatd(twidth, twidth + sizeof(twidth), pwg->width * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tlength, tlength + sizeof(tlength), pwg->length * 72.0 / 2540.0, loc);

        cupsFilePrintf(fp, "*PageRegion %s: \"<</PageSize[%s %s]>>setpagedevice\"\n", pwg->ppd, twidth, tlength);
      }
    }
    cupsFilePuts(fp, "*CloseUI: *PageRegion\n");

    cupsFilePrintf(fp, "*DefaultImageableArea: %s\n"
		       "*DefaultPaperDimension: %s\n", ppdname, ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      media_size = ippGetCollection(attr, i);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

      if (x_dim && y_dim)
      {
        char	tleft[256],		/* Left string */
		tbottom[256],		/* Bottom string */
		tright[256],		/* Right string */
		ttop[256],		/* Top string */
		twidth[256],		/* Width string */
		tlength[256];		/* Length string */

        pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));

        _cupsStrFormatd(tleft, tleft + sizeof(tleft), left * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tbottom, tbottom + sizeof(tbottom), bottom * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tright, tright + sizeof(tright), (pwg->width - right) * 72.0 / 2540.0, loc);
        _cupsStrFormatd(ttop, ttop + sizeof(ttop), (pwg->length - top) * 72.0 / 2540.0, loc);
        _cupsStrFormatd(twidth, twidth + sizeof(twidth), pwg->width * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tlength, tlength + sizeof(tlength), pwg->length * 72.0 / 2540.0, loc);

        cupsFilePrintf(fp, "*ImageableArea %s: \"%s %s %s %s\"\n", pwg->ppd, tleft, tbottom, tright, ttop);
        cupsFilePrintf(fp, "*PaperDimension %s: \"%s %s\"\n", pwg->ppd, twidth, tlength);
      }
    }
  } else {
    debug_printf("cups-browsed: No page sizes reported as supported by the printer, not generating PPD\n");
    unlink(buffer);
    return(NULL);
  }

 /*
  * InputSlot...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-source", IPP_TAG_KEYWORD)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "media-source-supported", IPP_TAG_KEYWORD)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    static const char * const sources[][2] =
    {
      { "Auto", "Automatic" },
      { "Main", "Main" },
      { "Alternate", "Alternate" },
      { "LargeCapacity", "Large Capacity" },
      { "Manual", "Manual" },
      { "Envelope", "Envelope" },
      { "Disc", "Disc" },
      { "Photo", "Photo" },
      { "Hagaki", "Hagaki" },
      { "MainRoll", "Main Roll" },
      { "AlternateRoll", "Alternate Roll" },
      { "Top", "Top" },
      { "Middle", "Middle" },
      { "Bottom", "Bottom" },
      { "Side", "Side" },
      { "Left", "Left" },
      { "Right", "Right" },
      { "Center", "Center" },
      { "Rear", "Rear" },
      { "ByPassTray", "Multipurpose" },
      { "Tray1", "Tray 1" },
      { "Tray2", "Tray 2" },
      { "Tray3", "Tray 3" },
      { "Tray4", "Tray 4" },
      { "Tray5", "Tray 5" },
      { "Tray6", "Tray 6" },
      { "Tray7", "Tray 7" },
      { "Tray8", "Tray 8" },
      { "Tray9", "Tray 9" },
      { "Tray10", "Tray 10" },
      { "Tray11", "Tray 11" },
      { "Tray12", "Tray 12" },
      { "Tray13", "Tray 13" },
      { "Tray14", "Tray 14" },
      { "Tray15", "Tray 15" },
      { "Tray16", "Tray 16" },
      { "Tray17", "Tray 17" },
      { "Tray18", "Tray 18" },
      { "Tray19", "Tray 19" },
      { "Tray20", "Tray 20" },
      { "Roll1", "Roll 1" },
      { "Roll2", "Roll 2" },
      { "Roll3", "Roll 3" },
      { "Roll4", "Roll 4" },
      { "Roll5", "Roll 5" },
      { "Roll6", "Roll 6" },
      { "Roll7", "Roll 7" },
      { "Roll8", "Roll 8" },
      { "Roll9", "Roll 9" },
      { "Roll10", "Roll 10" }
    };

    cupsFilePrintf(fp, "*OpenUI *InputSlot: PickOne\n"
                       "*OrderDependency: 10 AnySetup *InputSlot\n"
                       "*DefaultInputSlot: %s\n", ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      pwg_ppdize_name(ippGetString(attr, i, NULL), ppdname, sizeof(ppdname));

      for (j = 0; j < (int)(sizeof(sources) / sizeof(sources[0])); j ++)
        if (!strcmp(sources[j][0], ppdname))
	{
	  cupsFilePrintf(fp, "*InputSlot %s/%s: \"<</MediaPosition %d>>setpagedevice\"\n", ppdname, sources[j][1], j);
	  break;
	}
    }
    cupsFilePuts(fp, "*CloseUI: *InputSlot\n");
  }

 /*
  * MediaType...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-type", IPP_TAG_KEYWORD)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "media-type-supported", IPP_TAG_KEYWORD)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    static const char * const types[][2] =
    {					/* Media type strings (far from complete) */
      { "Auto", "Automatic" },
      { "Cardstock", "Cardstock" },
      { "Disc", "CD/DVD/Bluray" },
      { "Envelope", "Envelope" },
      { "Labels", "Label" },
      { "Other", "Other" },
      { "Photographic", "Photo" },
      { "PhotographicGlossy", "Glossy Photo" },
      { "PhotographicHighGloss", "High-Gloss Photo" },
      { "PhotographicMatte", "Matte Photo" },
      { "PhotographicSatin", "Satin Photo" },
      { "PhotographicSemiGloss", "Semi-Gloss Photo" },
      { "Stationery", "Plain Paper" },
      { "StationeryLetterhead", "Letterhead" },
      { "Transparency", "Transparency" }
    };

    cupsFilePrintf(fp, "*OpenUI *MediaType: PickOne\n"
                       "*OrderDependency: 10 AnySetup *MediaType\n"
                       "*DefaultMediaType: %s\n", ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      pwg_ppdize_name(ippGetString(attr, i, NULL), ppdname, sizeof(ppdname));

      for (j = 0; j < (int)(sizeof(types) / sizeof(types[0])); j ++)
        if (!strcmp(types[j][0], ppdname))
	{
	  cupsFilePrintf(fp, "*MediaType %s/%s: \"<</MediaType(%s)>>setpagedevice\"\n", ppdname, types[j][1], ppdname);
	  break;
	}

      if (j >= (int)(sizeof(types) / sizeof(types[0])))
	cupsFilePrintf(fp, "*MediaType %s: \"<</MediaType(%s)>>setpagedevice\"\n", ppdname, ppdname);

    }
    cupsFilePuts(fp, "*CloseUI: *MediaType\n");
  }

 /*
  * ColorModel...
  */

  if ((attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(response, "print-color-mode-supported", IPP_TAG_KEYWORD);

  if (attr && ippGetCount(attr) > 0)
  {
    const char *default_color = NULL;	/* Default */

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char *keyword = ippGetString(attr, i, NULL);
					/* Keyword for color/bit depth */

      if (!strcmp(keyword, "black_1") || !strcmp(keyword, "bi-level") || !strcmp(keyword, "process-bi-level"))
      {
	if (!default_color)
	  cupsFilePuts(fp, "*OpenUI *ColorModel/Color Mode: PickOne\n"
		       "*OrderDependency: 10 AnySetup *ColorModel\n");
        cupsFilePuts(fp, "*ColorModel FastGray/Fast Grayscale: \"<</cupsColorSpace 3/cupsBitsPerColor 1/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n");

        if (!default_color)
	  default_color = "FastGray";
      }
      else if (!strcmp(keyword, "sgray_8") || !strcmp(keyword, "monochrome") || !strcmp(keyword, "process-monochrome"))
      {
	if (!default_color)
	  cupsFilePuts(fp, "*OpenUI *ColorModel/Color Mode: PickOne\n"
		       "*OrderDependency: 10 AnySetup *ColorModel\n");
        cupsFilePuts(fp, "*ColorModel Gray/Grayscale: \"<</cupsColorSpace 18/cupsBitsPerColor 8/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n");

        if (!default_color || !strcmp(default_color, "FastGray"))
	  default_color = "Gray";
      }
      else if (!strcmp(keyword, "srgb_8") || !strcmp(keyword, "color"))
      {
	if (!default_color)
	  cupsFilePuts(fp, "*OpenUI *ColorModel/Color Mode: PickOne\n"
		       "*OrderDependency: 10 AnySetup *ColorModel\n");
        cupsFilePuts(fp, "*ColorModel RGB/Color: \"<</cupsColorSpace 19/cupsBitsPerColor 8/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n");

	default_color = "RGB";
      }
    }

    if (default_color)
    {
      cupsFilePrintf(fp, "*DefaultColorModel: %s\n", default_color);
      cupsFilePuts(fp, "*CloseUI: *ColorModel\n");
    }
  }

 /*
  * Duplex...
  */

  if ((attr = ippFindAttribute(response, "sides-supported", IPP_TAG_KEYWORD)) != NULL && ippContainsString(attr, "two-sided-long-edge"))
  {
    cupsFilePuts(fp, "*OpenUI *Duplex/2-Sided Printing: PickOne\n"
                     "*OrderDependency: 10 AnySetup *Duplex\n"
                     "*DefaultDuplex: None\n"
                     "*Duplex None/Off (1-Sided): \"<</Duplex false>>setpagedevice\"\n"
                     "*Duplex DuplexNoTumble/Long-Edge (Portrait): \"<</Duplex true/Tumble false>>setpagedevice\"\n"
                     "*Duplex DuplexTumble/Short-Edge (Landscape): \"<</Duplex true/Tumble true>>setpagedevice\"\n"
                     "*CloseUI: *Duplex\n");

    if ((attr = ippFindAttribute(response, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD)) != NULL)
    {
      const char *keyword = ippGetString(attr, 0, NULL);
					/* Keyword value */

      if (!strcmp(keyword, "flipped"))
        cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
      else if (!strcmp(keyword, "manual-tumble"))
        cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
      else if (!strcmp(keyword, "normal"))
        cupsFilePuts(fp, "*cupsBackSide: Normal\n");
      else
        cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
    }
    else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
    {
      for (i = 0, count = ippGetCount(attr); i < count; i ++)
      {
	const char *dm = ippGetString(attr, i, NULL);
					  /* DM value */

	if (!_cups_strcasecmp(dm, "DM1"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: Normal\n");
	  break;
	}
	else if (!_cups_strcasecmp(dm, "DM2"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
	  break;
	}
	else if (!_cups_strcasecmp(dm, "DM3"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
	  break;
	}
	else if (!_cups_strcasecmp(dm, "DM4"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
	  break;
	}
      }
    }
  }

 /*
  * cupsPrintQuality and DefaultResolution...
  */

  if ((attr = ippFindAttribute(response, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL)
  {
    count = ippGetCount(attr);

    pwg_ppdize_resolution(attr, count / 2, &xres, &yres, ppdname, sizeof(ppdname));
    cupsFilePrintf(fp, "*DefaultResolution: %s\n", ppdname);

    cupsFilePuts(fp, "*OpenUI *cupsPrintQuality/Print Quality: PickOne\n"
                     "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
                     "*DefaultcupsPrintQuality: Normal\n");
    if (count > 2)
    {
      pwg_ppdize_resolution(attr, 0, &xres, &yres, NULL, 0);
      cupsFilePrintf(fp, "*cupsPrintQuality Draft: \"<</HWResolution[%d %d]>>setpagedevice\"\n", xres, yres);
    }
    pwg_ppdize_resolution(attr, count / 2, &xres, &yres, NULL, 0);
    cupsFilePrintf(fp, "*cupsPrintQuality Normal: \"<</HWResolution[%d %d]>>setpagedevice\"\n", xres, yres);
    if (count > 1)
    {
      pwg_ppdize_resolution(attr, count - 1, &xres, &yres, NULL, 0);
      cupsFilePrintf(fp, "*cupsPrintQuality High: \"<</HWResolution[%d %d]>>setpagedevice\"\n", xres, yres);
    }

    cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
  }
  else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
  {
    int lowdpi = 0, hidpi = 0;		/* Lower and higher resolution */

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char *rs = ippGetString(attr, i, NULL);
					/* RS value */

      if (_cups_strncasecmp(rs, "RS", 2))
        continue;

      lowdpi = atoi(rs + 2);
      if ((rs = strrchr(rs, '-')) != NULL)
        hidpi = atoi(rs + 1);
      else
        hidpi = lowdpi;
      break;
    }

    if (lowdpi == 0)
    {
     /*
      * Invalid "urf-supported" value...
      */

      cupsFilePuts(fp, "*DefaultResolution: 300dpi\n");
    }
    else
    {
     /*
      * Generate print qualities based on low and high DPIs...
      */

      cupsFilePrintf(fp, "*DefaultResolution: %ddpi\n", lowdpi);

      cupsFilePuts(fp, "*OpenUI *cupsPrintQuality/Print Quality: PickOne\n"
		       "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
		       "*DefaultcupsPrintQuality: Normal\n");
      if ((lowdpi & 1) == 0)
	cupsFilePrintf(fp, "*cupsPrintQuality Draft: \"<</HWResolution[%d %d]>>setpagedevice\"\n", lowdpi, lowdpi / 2);
      cupsFilePrintf(fp, "*cupsPrintQuality Normal: \"<</HWResolution[%d %d]>>setpagedevice\"\n", lowdpi, lowdpi);
      if (hidpi > lowdpi)
	cupsFilePrintf(fp, "*cupsPrintQuality High: \"<</HWResolution[%d %d]>>setpagedevice\"\n", hidpi, hidpi);
      cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
    }
  }
  else if ((attr = ippFindAttribute(response, "printer-resolution-default", IPP_TAG_RESOLUTION)) != NULL)
  {
    pwg_ppdize_resolution(attr, 0, &xres, &yres, ppdname, sizeof(ppdname));
    cupsFilePrintf(fp, "*DefaultResolution: %s\n", ppdname);
  }
  else
    cupsFilePuts(fp, "*DefaultResolution: 300dpi\n");

 /*
  * Close up and return...
  */

  cupsFileClose(fp);

  return (buffer);
}


/*
 * '_pwgInputSlotForSource()' - Get the InputSlot name for the given PWG
 *                              media-source.
 */

const char *				/* O - InputSlot name */
_pwgInputSlotForSource(
    const char *media_source,		/* I - PWG media-source */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
 /*
  * Range check input...
  */

  if (!media_source || !name || namesize < PPD_MAX_NAME)
    return (NULL);

  if (_cups_strcasecmp(media_source, "main"))
    strlcpy(name, "Cassette", namesize);
  else if (_cups_strcasecmp(media_source, "alternate"))
    strlcpy(name, "Multipurpose", namesize);
  else if (_cups_strcasecmp(media_source, "large-capacity"))
    strlcpy(name, "LargeCapacity", namesize);
  else if (_cups_strcasecmp(media_source, "bottom"))
    strlcpy(name, "Lower", namesize);
  else if (_cups_strcasecmp(media_source, "middle"))
    strlcpy(name, "Middle", namesize);
  else if (_cups_strcasecmp(media_source, "top"))
    strlcpy(name, "Upper", namesize);
  else if (_cups_strcasecmp(media_source, "rear"))
    strlcpy(name, "Rear", namesize);
  else if (_cups_strcasecmp(media_source, "side"))
    strlcpy(name, "Side", namesize);
  else if (_cups_strcasecmp(media_source, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (_cups_strcasecmp(media_source, "main-roll"))
    strlcpy(name, "Roll", namesize);
  else if (_cups_strcasecmp(media_source, "alternate-roll"))
    strlcpy(name, "Roll2", namesize);
  else
    pwg_ppdize_name(media_source, name, namesize);

  return (name);
}


/*
 * '_pwgMediaTypeForType()' - Get the MediaType name for the given PWG
 *                            media-type.
 */

const char *				/* O - MediaType name */
_pwgMediaTypeForType(
    const char *media_type,		/* I - PWG media-type */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
 /*
  * Range check input...
  */

  if (!media_type || !name || namesize < PPD_MAX_NAME)
    return (NULL);

  if (_cups_strcasecmp(media_type, "auto"))
    strlcpy(name, "Auto", namesize);
  else if (_cups_strcasecmp(media_type, "cardstock"))
    strlcpy(name, "Cardstock", namesize);
  else if (_cups_strcasecmp(media_type, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-glossy"))
    strlcpy(name, "Glossy", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-high-gloss"))
    strlcpy(name, "HighGloss", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-matte"))
    strlcpy(name, "Matte", namesize);
  else if (_cups_strcasecmp(media_type, "stationery"))
    strlcpy(name, "Plain", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-coated"))
    strlcpy(name, "Coated", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-inkjet"))
    strlcpy(name, "Inkjet", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-letterhead"))
    strlcpy(name, "Letterhead", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-preprinted"))
    strlcpy(name, "Preprinted", namesize);
  else if (_cups_strcasecmp(media_type, "transparency"))
    strlcpy(name, "Transparency", namesize);
  else
    pwg_ppdize_name(media_type, name, namesize);

  return (name);
}


/*
 * '_pwgPageSizeForMedia()' - Get the PageSize name for the given media.
 */

const char *				/* O - PageSize name */
_pwgPageSizeForMedia(
    pwg_media_t *media,		/* I - Media */
    char         *name,			/* I - PageSize name buffer */
    size_t       namesize)		/* I - Size of name buffer */
{
  const char	*sizeptr,		/* Pointer to size in PWG name */
		*dimptr;		/* Pointer to dimensions in PWG name */


 /*
  * Range check input...
  */

  if (!media || !name || namesize < PPD_MAX_NAME)
    return (NULL);

 /*
  * Copy or generate a PageSize name...
  */

  if (media->ppd)
  {
   /*
    * Use a standard Adobe name...
    */

    strlcpy(name, media->ppd, namesize);
  }
  else if (!media->pwg || !strncmp(media->pwg, "custom_", 7) ||
           (sizeptr = strchr(media->pwg, '_')) == NULL ||
	   (dimptr = strchr(sizeptr + 1, '_')) == NULL ||
	   (size_t)(dimptr - sizeptr) > namesize)
  {
   /*
    * Use a name of the form "wNNNhNNN"...
    */

    snprintf(name, namesize, "w%dh%d", (int)PWG_TO_POINTS(media->width),
             (int)PWG_TO_POINTS(media->length));
  }
  else
  {
   /*
    * Copy the size name from class_sizename_dimensions...
    */

    memcpy(name, sizeptr + 1, (size_t)(dimptr - sizeptr - 1));
    name[dimptr - sizeptr - 1] = '\0';
  }

  return (name);
}


/*
 * 'pwg_ppdize_name()' - Convert an IPP keyword to a PPD keyword.
 */

static void
pwg_ppdize_name(const char *ipp,	/* I - IPP keyword */
                char       *name,	/* I - Name buffer */
		size_t     namesize)	/* I - Size of name buffer */
{
  char	*ptr,				/* Pointer into name buffer */
	*end;				/* End of name buffer */


  *name = (char)toupper(*ipp++);

  for (ptr = name + 1, end = name + namesize - 1; *ipp && ptr < end;)
  {
    if (*ipp == '-' && _cups_isalpha(ipp[1]))
    {
      ipp ++;
      *ptr++ = (char)toupper(*ipp++ & 255);
    }
    else
      *ptr++ = *ipp++;
  }

  *ptr = '\0';
}



/*
 * 'pwg_ppdize_resolution()' - Convert PWG resolution values to PPD values.
 */

static void
pwg_ppdize_resolution(
    ipp_attribute_t *attr,		/* I - Attribute to convert */
    int             element,		/* I - Element to convert */
    int             *xres,		/* O - X resolution in DPI */
    int             *yres,		/* O - Y resolution in DPI */
    char            *name,		/* I - Name buffer */
    size_t          namesize)		/* I - Size of name buffer */
{
  ipp_res_t units;			/* Units for resolution */


  *xres = ippGetResolution(attr, element, yres, &units);

  if (units == IPP_RES_PER_CM)
  {
    *xres = (int)(*xres * 2.54);
    *yres = (int)(*yres * 2.54);
  }

  if (name && namesize > 4)
  {
    if (*xres == *yres)
      snprintf(name, namesize, "%ddpi", *xres);
    else
      snprintf(name, namesize, "%dx%ddpi", *xres, *yres);
  }
}
#endif /* HAVE_CUPS_1_6 */



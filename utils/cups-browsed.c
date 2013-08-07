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
#include <ifaddrs.h>
#if defined(__OpenBSD__)
#include <sys/socket.h>
#endif /* __OpenBSD__ */
#include <net/if.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>

#include <glib.h>

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-glib/glib-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#endif /* HAVE_AVAHI */

#include <cups/cups.h>

/* Attribute to mark a CUPS queue as created by us */
#define CUPS_BROWSED_MARK "cups-browsed"

/* Timeout values in sec */
#define TIMEOUT_IMMEDIATELY -1
#define TIMEOUT_CONFIRM     10
#define TIMEOUT_RETRY       10
#define TIMEOUT_REMOVE      -1
#define TIMEOUT_CHECK_LIST   2

/* Status of remote printer */
typedef enum printer_status_e {
  STATUS_UNCONFIRMED = 0,
  STATUS_CONFIRMED,
  STATUS_TO_BE_CREATED,
  STATUS_BROWSE_PACKET_RECEIVED,
  STATUS_DISAPPEARED
} printer_status_t;

/* Data structure for remote printers */
typedef struct remote_printer_s {
  char *name;
  char *uri;
  printer_status_t status;
  time_t timeout;
  int duplicate;
  char *host;
  char *service_name;
  char *type;
  char *domain;
} remote_printer_t;

/* Data structure for network interfaces */
typedef struct netif_s {
  char *address;
  http_addr_t broadcast;
} netif_t;

/* Data structure for browse allow/deny rules */
typedef enum allow_type_e {
  ALLOW_IP,
  ALLOW_NET
} allow_type_t;
typedef struct allow_s {
  allow_type_t type;
  http_addr_t addr;
  http_addr_t mask;
} allow_t;

cups_array_t *remote_printers;
static cups_array_t *netifs;
static cups_array_t *browseallow;

static GMainLoop *gmainloop = NULL;
#ifdef HAVE_AVAHI
static AvahiGLibPoll *glib_poll = NULL;
#endif /* HAVE_AVAHI */
static guint queues_timer_id = (guint) -1;
static int browsesocket = -1;

#define BROWSE_DNSSD (1<<0)
#define BROWSE_CUPS  (1<<1)
static unsigned int BrowseLocalProtocols = 0;
static unsigned int BrowseRemoteProtocols = BROWSE_DNSSD;
static unsigned int BrowseInterval = 60;
static unsigned int BrowseTimeout = 300;
static uint16_t BrowsePort = 631;
static char **BrowsePoll = NULL;
static size_t NumBrowsePoll = 0;
static char *DomainSocket = NULL;

static int debug = 0;

static void recheck_timer (void);

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

static remote_printer_t *
create_local_queue (const char *name,
		    const char *uri,
		    const char *host,
		    const char *info,
		    const char *type,
		    const char *domain)
{
  remote_printer_t *p;
  remote_printer_t *q;

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

  p->host = strdup (host);
  if (!p->host)
    goto fail;

  p->service_name = strdup (info);
  if (!p->service_name)
    goto fail;

  /* Record Bonjour service parameters to identify print queue
     entry for removal when service disappears */
  p->type = strdup (type);
  if (!p->type)
    goto fail;

  p->domain = strdup (domain);
  if (!p->domain) {
  fail:
    debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
    free (p->type);
    free (p->service_name);
    free (p->host);
    free (p->uri);
    free (p->name);
    free (p);
    return NULL;
  }

  /* Schedule for immediate creation of the CUPS queue */
  p->status = STATUS_TO_BE_CREATED;
  p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;

  /* Check whether we have an equally named queue already from another
     server */
  for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
       q;
       q = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!strcmp(q->name, p->name))
      break;
  p->duplicate = q ? 1 : 0;

  /* Add the new remote printer entry */
  cupsArrayAdd(remote_printers, p);

  if (p->duplicate)
    debug_printf("cups-browsed: Printer already available through host %s.\n",
		 q->host);

  return p;
}

gboolean handle_cups_queues(gpointer unused) {
  remote_printer_t *p;
  http_t *http;
  char uri[HTTP_MAX_URI];
  int num_options;
  cups_option_t *options;
  int num_jobs;
  cups_job_t *jobs;
  ipp_t *request, *response;
  time_t current_time = time(NULL);
  const char *default_printer_name;
  ipp_attribute_t *attr;

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
		   (p->duplicate ? "" : " and its CUPS queue"));

      /* Remove the CUPS queue */
      if (!p->duplicate) { /* Duplicates do not have a CUPS queue */
	if ((http = httpConnectEncrypt(cupsServer(), ippPort(),
				       cupsEncryption())) == NULL) {
	  debug_printf("cups-browsed: Unable to connect to CUPS!\n");
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}

	/* Check whether there are still jobs and do not remove the queue
	   then */
	num_jobs = 0;
	jobs = NULL;
	num_jobs = cupsGetJobs2(http, &jobs, p->name, 0, CUPS_WHICHJOBS_ACTIVE);
	if (num_jobs != 0) { /* error or jobs */
	  debug_printf("cups-browsed: Queue has still jobs or CUPS error!\n");
	  cupsFreeJobs(num_jobs, jobs);
	  httpClose(http);
	  /* Schedule the removal of the queue for later */
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}

	/* Check whether the queue is the system default. In this case do not
	   remove it, so that this user setting does not get lost */
	default_printer_name = NULL;
	request = ippNewRequest(CUPS_GET_DEFAULT);
	/* Default user */
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		     "requesting-user-name", NULL, cupsUser());
	/* Do it */
	response = cupsDoRequest(http, request, "/");
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
		if (!strcmp(ippGetName(attr), "printer-name") &&
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
	if (default_printer_name &&
	    !strcasecmp(default_printer_name, p->name)) {
	  /* Printer is currently the system's default printer,
	     do not remove it */
	  httpClose(http);
	  /* Schedule the removal of the queue for later */
	  p->timeout = current_time + TIMEOUT_RETRY;
	  break;
	}
	if (response)
	  ippDelete(response);

	/* No jobs, not default printer, remove the CUPS queue */
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
	  httpClose(http);
	  break;
	}
	httpClose(http);
      }

      /* CUPS queue removed, remove the list entry */
      cupsArrayRemove(remote_printers, p);
      if (p->name) free (p->name);
      if (p->uri) free (p->uri);
      if (p->host) free (p->host);
      if (p->service_name) free (p->service_name);
      if (p->type) free (p->type);
      if (p->domain) free (p->domain);
      free(p);
      break;

    /* Bonjour has reported a new remote printer, create a CUPS queue for it,
       or upgrade an existing queue, or update a queue to use a backup host
       when it has disappeared on the currently used host */
    case STATUS_TO_BE_CREATED:
      /* (...or, we've just received a CUPS Browsing packet for this queue) */
    case STATUS_BROWSE_PACKET_RECEIVED:

      /* Do not create a queue for duplicates */
      if (p->duplicate) {
	p->timeout = (time_t) -1;
	break;
      }

      /* Only act if the timeout has passed */
      if (p->timeout > current_time)
	break;

      debug_printf("cups-browsed: Creating/Updating CUPS queue for %s\n",
		   p->name);

      /* Create a new CUPS queue or modify the existing queue */
      if ((http = httpConnectEncrypt(cupsServer(), ippPort(),
				     cupsEncryption())) == NULL) {
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
      /* Device URI: ipp(s)://<remote host>:631/printers/<remote queue> */
      num_options = cupsAddOption("device-uri", p->uri,
				  num_options, &options);
      /* Option cups-browsed=true, marking that we have created this queue */
      num_options = cupsAddOption("cups-browsed-default", "true",
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
      cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);
      /* Do it */
      ippDelete(cupsDoRequest(http, request, "/admin/"));
      cupsFreeOptions(num_options, options);
      if (cupsLastError() > IPP_OK_CONFLICT) {
	debug_printf("cups-browsed: Unable to create CUPS queue!\n");
	p->timeout = current_time + TIMEOUT_RETRY;
	httpClose(http);
	break;
      }
      httpClose(http);

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

    /* Nothing to do */
    case STATUS_CONFIRMED:
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

  if (queues_timer_id != (guint) -1)
    g_source_remove (queues_timer_id);

  if (timeout != (time_t) -1) {
    queues_timer_id = g_timeout_add_seconds (timeout, handle_cups_queues, NULL);
    debug_printf("cups-browsed: checking queues in %ds\n", timeout);
  } else {
    queues_timer_id = (guint) -1;
    debug_printf("cups-browsed: listening\n");
  }
}

void generate_local_queue(const char *host,
			  uint16_t port,
			  char *resource,
			  const char *name,
			  const char *type,
			  const char *domain) {
  char *remote_queue, *remote_host;
  remote_printer_t *p;
  char *backup_queue_name, *local_queue_name = NULL;
  cups_dest_t *dests, *dest;
  int i, num_dests;
  const char *val;

  /* This is a remote CUPS queue, find queue name and host name */
  remote_queue = resource + 9;
  remote_host = strdup(host);
  if (!strcmp(remote_host + strlen(remote_host) - 6, ".local"))
    remote_host[strlen(remote_host) - 6] = '\0';
  if (!strcmp(remote_host + strlen(remote_host) - 7, ".local."))
    remote_host[strlen(remote_host) - 7] = '\0';
  debug_printf("cups-browsed: Found CUPS queue: %s on host %s.\n",
	       remote_queue, remote_host);

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
  num_dests = cupsGetDests(&dests);

  local_queue_name = remote_queue;
  if (num_dests > 0) {
    /* Is there a local queue with the name of the remote queue? */
    for (i = num_dests, dest = dests; i > 0; i --, dest ++)
      /* Only consider CUPS queues not created by us */
      if ((((val =
	     cupsGetOption(CUPS_BROWSED_MARK, dest->num_options,
			   dest->options)) == NULL) ||
	   (strcasecmp(val, "yes") != 0 &&
	    strcasecmp(val, "on") != 0 &&
	    strcasecmp(val, "true") != 0)) &&
	  !strcmp(local_queue_name, dest->name))
	break;
    if (i > 0) {
      /* Found local queue with same name as remote queue */
      /* Is there a local queue with the name <queue>@<host>? */
      local_queue_name = backup_queue_name;
      debug_printf("cups-browsed: %s already taken, using fallback name: %s\n",
		   remote_queue, local_queue_name);
      for (i = num_dests, dest = dests; i > 0; i --, dest ++)
	/* Only consider CUPS queues not created by us */
	if ((((val =
	       cupsGetOption(CUPS_BROWSED_MARK, dest->num_options,
			     dest->options)) == NULL) ||
	     (strcasecmp(val, "yes") != 0 &&
	      strcasecmp(val, "on") != 0 &&
	      strcasecmp(val, "true") != 0)) &&
	    !strcmp(local_queue_name, dest->name))
	  break;
      if (i > 0) {
	/* Found also a local queue with name <queue>@<host>, so
	   ignore this remote printer */
	debug_printf("cups-browsed: %s also taken, printer ignored.\n",
		     local_queue_name);
	free (backup_queue_name);
	free (remote_host);
	cupsFreeDests(num_dests, dests);
	return;
      }
    }
    cupsFreeDests(num_dests, dests);
  }

  /* Check if we have already created a queue for the discovered
     printer */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
    if (!strcmp(p->name, local_queue_name) &&
	(p->host[0] == '\0' ||
	 !strcmp(p->host, remote_host)))
      break;

  if (p) {
    /* We have already created a local queue, check whether the
       discovered service allows us to upgrade the queue to IPPS */
    if (strcasestr(type, "_ipps") &&
	!strncmp(p->uri, "ipp:", 4)) {

      /* Schedule local queue for upgrade to ipps: */
      if ((p->uri = realloc(p->uri, strlen(p->uri) + 2)) == NULL){
	debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
	exit(1);
      }
      memmove((void *)(p->uri + 4), (const void *)(p->uri + 3),
	      strlen(p->uri) - 2);
      p->uri[3] = 's';
      p->status = STATUS_TO_BE_CREATED;
      p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
      p->host = strdup(remote_host);
      p->service_name = strdup(name);
      p->type = strdup(type);
      p->domain = strdup(domain);
      debug_printf("cups-browsed: Upgrading printer %s (Host: %s) to IPPS. New URI: %s\n",
		   p->name, p->host, p->uri);

    } else {

      /* Nothing to do, mark queue entry as confirmed if the entry
	 is unconfirmed */
      debug_printf("cups-browsed: Entry for %s (Host: %s, URI: %s) already exists.\n",
		   p->name, p->host, p->uri);
      if (p->status == STATUS_UNCONFIRMED) {
	p->status = STATUS_CONFIRMED;
	p->timeout = (time_t) -1;
	debug_printf("cups-browsed: Marking entry for %s (Host: %s, URI: %s) as confirmed.\n",
		     p->name, p->host, p->uri);
      }

    }
    if (p->host[0] == '\0')
      p->host = strdup(remote_host);
    if (p->service_name[0] == '\0' && name)
      p->service_name = strdup(name);
    if (p->type[0] == '\0' && type)
      p->type = strdup(type);
    if (p->domain[0] == '\0' && domain)
      p->domain = strdup(domain);

  } else {

    /* We need to create a local queue pointing to the
       discovered printer */

    /* Device URI: ipp(s)://<remote host>:631/printers/<remote queue> */
    char *uri = malloc(strlen(host) +
		       strlen(remote_queue) + 34);
    if (uri == NULL) {
      debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
      exit(1);
    }
    sprintf(uri, "ipp%s://%s:%u/printers/%s",
	    (strcasestr(type, "_ipps") ? "s" : ""), host,
	    port, remote_queue);

    p = create_local_queue (local_queue_name, uri, remote_host,
			    name ? name : "", type, domain);
    free (uri);
  }

  free (backup_queue_name);
  free (remote_host);

  if (p)
    debug_printf("cups-browsed: Bonjour IDs: Service name: \"%s\", "
		 "Service type: \"%s\", Domain: \"%s\"\n",
		 p->service_name, p->type, p->domain);
}

#ifdef HAVE_AVAHI
static void resolve_callback(
  AvahiServiceResolver *r,
  AVAHI_GCC_UNUSED AvahiIfIndex interface,
  AVAHI_GCC_UNUSED AvahiProtocol protocol,
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

  assert(r);

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

    /* Check if we have a remote CUPS queue, other remote printers are not
       handled by us */
    rp_entry = avahi_string_list_find(txt, "rp");
    adminurl_entry = avahi_string_list_find(txt, "adminurl");
    if (rp_entry && adminurl_entry) {
      avahi_string_list_get_pair(rp_entry, &rp_key, &rp_value, NULL);
      avahi_string_list_get_pair(adminurl_entry, &adminurl_key,
				 &adminurl_value, NULL);

      /* Check by "rp" and "adminurl" TXT record fields whether
	 the discovered printer is a CUPS queue */
      if (rp_key && rp_value && adminurl_key && adminurl_value &&
	  !strcmp(rp_key, "rp") && !strncmp(rp_value, "printers/", 9) &&
	  !strcmp(adminurl_key, "adminurl") &&
	  !strcmp(adminurl_value + strlen(adminurl_value) -
		  strlen(rp_value), rp_value)) {
	generate_local_queue(host_name, port, rp_value, name, type, domain);
      }

      /* Clean up */
      avahi_free(rp_key);
      avahi_free(rp_value);
      avahi_free(adminurl_key);
      avahi_free(adminurl_value);
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
  assert(b);

  /* Called whenever a new services becomes available on the LAN or
     is removed from the LAN */

  switch (event) {

  /* Avah browser error */
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
    remote_printer_t *p, *q;

    /* Ignore events from the local machine */
    if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
      break;

    debug_printf("cups-browsed: Avahi Browser: REMOVE: service '%s' of type '%s' in domain '%s'\n",
		 name, type, domain);

    /* Check whether we have listed this printer */
    for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
	 p; p = (remote_printer_t *)cupsArrayNext(remote_printers))
      if (!strcmp(p->service_name, name) &&
	  !strcmp(p->type, type) &&
	  !strcmp(p->domain, domain))
	break;
    if (p) {
      /* Check whether this queue has a duplicate from another server */
      q = NULL;
      if (!p->duplicate) {
	for (q = (remote_printer_t *)cupsArrayFirst(remote_printers);
	     q;
	     q = (remote_printer_t *)cupsArrayNext(remote_printers))
	  if (!strcmp(q->name, p->name) &&
	      strcmp(q->host, p->host) &&
	      q->duplicate)
	    break;
      }
      if (q) {
	/* Remove the data of the disappeared remote printer */
	free (p->uri);
	free (p->host);
	free (p->service_name);
	free (p->type);
	free (p->domain);
	/* Replace the data with the data of the duplicate printer */
	p->uri = strdup(q->uri);
	p->host = strdup(q->host);
	p->service_name = strdup(q->service_name);
	p->type = strdup(q->type);
	p->domain = strdup(q->domain);
	/* Schedule this printer for updating the CUPS queue */
	p->status = STATUS_TO_BE_CREATED;
	p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
	/* Schedule the remote printer for removal */
	q->status = STATUS_DISAPPEARED;
	q->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;

	debug_printf("cups-browsed: Printer %s diasappeared, replacing by backup on host %s with URI %s.\n",
		     p->name, p->host, p->uri);
      } else {

	/* Schedule CUPS queue for removal */
	p->status = STATUS_DISAPPEARED;
	p->timeout = time(NULL) + TIMEOUT_REMOVE;

	debug_printf("cups-browsed: Printer %s (Host: %s, URI: %s) disappeared and no backup available, removing entry.\n",
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

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
  assert(c);

  /* Called whenever the client or server state changes */

  if (state == AVAHI_CLIENT_FAILURE) {
    debug_printf("cups-browsed: ERROR: Avahi server connection failure: %s\n",
		 avahi_strerror(avahi_client_errno(c)));
    g_main_loop_quit(gmainloop);
  }

}
#endif /* HAVE_AVAHI */

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

  httpSeparateURI (HTTP_URI_CODING_ALL, uri,
		   scheme, sizeof(scheme),
		   username, sizeof(username),
		   host, sizeof(host),
		   &port,
		   resource, sizeof(resource));

  /* Check this isn't one of our own broadcasts */
  for (iface = cupsArrayFirst (netifs);
       iface;
       iface = cupsArrayNext (netifs))
    if (!strcmp (host, iface->address))
      break;
  if (iface) {
    debug_printf("cups-browsed: ignoring own broadcast on %s\n",
		 iface->address);
    return;
  }

  if (strncmp (resource, "/printers/", 10)) {
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

  generate_local_queue(host, port, local_resource, info ? info : "", "", "");
}

static gboolean
allowed (struct sockaddr *srcaddr)
{
  allow_t *allow;
  if (cupsArrayCount(browseallow) == 0) {
    /* No "BrowseAllow" line, allow all servers */
    return TRUE;
  }
  for (allow = cupsArrayFirst (browseallow);
       allow;
       allow = cupsArrayNext (browseallow)) {
    switch (allow->type) {
    case ALLOW_IP:
      switch (srcaddr->sa_family) {
      case AF_INET:
	if (((struct sockaddr_in *) srcaddr)->sin_addr.s_addr ==
	    allow->addr.ipv4.sin_addr.s_addr)
	  return TRUE;
	break;

      case AF_INET6:
	if (!memcmp (&((struct sockaddr_in6 *) srcaddr)->sin6_addr,
		     &allow->addr.ipv6.sin6_addr,
		     sizeof (allow->addr.ipv6.sin6_addr)))
	  return TRUE;
	break;
      }
      break;

    case ALLOW_NET:
      switch (srcaddr->sa_family) {
	struct sockaddr_in6 *src6addr;

      case AF_INET:
	if ((((struct sockaddr_in *) srcaddr)->sin_addr.s_addr &
	     allow->mask.ipv4.sin_addr.s_addr) ==
	    allow->addr.ipv4.sin_addr.s_addr)
	  return TRUE;
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
	     allow->addr.ipv6.sin6_addr.s6_addr[3]))
	  return TRUE;
	break;
      }
    }
  }

  return FALSE;
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
  char *c;

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
  httpAddrString (&srcaddr, remote_host, sizeof (remote_host));

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
  c = strchr (packet, '\"');
  if (c) {
    /* Skip location field */
    for (c++; *c != '\"'; c++)
      ;

    if (*c == '\"') {
      for (c++; isspace(*c); c++)
	;
    }

    /* Is there an info field? */
    if (*c == '\"') {
      int i;
      c++;
      for (i = 0;
	   i < sizeof (info) - 1 && *c != '\"';
	   i++, c++)
	info[i] = *c;
      info[i] = '\0';
    }
  }

  found_cups_printer (remote_host, uri, info);
  recheck_timer ();

  /* Don't remove this I/O source */
  return TRUE;
}

void
update_netifs (void)
{
  struct ifaddrs *ifaddr, *ifa;
  netif_t *iface;

  if (getifaddrs (&ifaddr) == -1) {
    debug_printf("cups-browsed: unable to get interface addresses: %s\n",
		 strerror (errno));
    return;
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
}

void
broadcast_browse_packets (int type, int state,
			  const char *local_uri, const char *location,
			  const char *info, const char *make_model,
			  const char *browse_options)
{
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
    httpSeparateURI(HTTP_URI_CODING_ALL, local_uri,
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
		  type, state, uri, location,
		  info, make_model,
		  BrowseTimeout,
		  browse_options ? " " : "",
		  browse_options ? browse_options : "") >= sizeof (packet)) {
      debug_printf ("cups-browsed: oversize packet not sent\n");
      continue;
    }

    debug_printf("cups-browsed: packet to send:\n%s", packet);

    int err = sendto (browsesocket, packet,
		      strlen (packet), 0,
		      &browse->broadcast.addr,
		      httpAddrLength (&browse->broadcast));
    if (err)
      debug_printf("cupsd-browsed: sendto returned %d: %s\n",
		   err, strerror (errno));
  }
}

gboolean
send_browse_data (gpointer data)
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

  update_netifs ();
  res_init ();
  conn = httpConnectEncrypt ("localhost", BrowsePort,
			     HTTP_ENCRYPT_IF_REQUESTED);

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

  response = cupsDoRequest (conn, request, "/");
  if (cupsLastError() > IPP_OK_CONFLICT) {
    debug_printf("cups-browsed: browse send failed for localhost: %s\n",
		 cupsLastErrorString ());
    goto fail;
  }

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
      
      if (!strcmp(attrname, "printer-type") &&
	  value_tag == IPP_TAG_ENUM) {
	type = ippGetInteger(attr, 0);
	if (type & CUPS_PRINTER_NOT_SHARED) {
	  /* Skip CUPS queues not marked as shared */
	  state = -1;
	  type = -1;
	  break;
	}
      } else if (!strcmp(attrname, "printer-state") &&
	       value_tag == IPP_TAG_ENUM)
	state = ippGetInteger(attr, 0);
      else if (!strcmp(attrname, "printer-uri-supported") &&
	       value_tag == IPP_TAG_URI)
	uri = ippGetString(attr, 0, NULL);
      else if (!strcmp(attrname, "printer-location") &&
	       value_tag == IPP_TAG_TEXT) {
	/* Remove quotes */
	gchar **tokens = g_strsplit (ippGetString(attr, 0, NULL), "\"", -1);
	location = g_strjoinv ("", tokens);
	g_strfreev (tokens);
      } else if (!strcmp(attrname, "printer-info") &&
		 value_tag == IPP_TAG_TEXT) {
	/* Remove quotes */
	gchar **tokens = g_strsplit (ippGetString(attr, 0, NULL), "\"", -1);
	info = g_strjoinv ("", tokens);
	g_strfreev (tokens);
      } else if (!strcmp(attrname, "printer-make-and-model") &&
		 value_tag == IPP_TAG_TEXT) {
	/* Remove quotes */
	gchar **tokens = g_strsplit (ippGetString(attr, 0, NULL), "\"", -1);
	make_model = g_strjoinv ("", tokens);
	g_strfreev (tokens);
      } else if (!strcmp(attrname, "auth-info-required") &&
		 value_tag == IPP_TAG_KEYWORD) {
	if (strcmp (ippGetString(attr, 0, NULL), "none"))
	  g_string_append_printf (browse_options, "auth-info-required=%s ",
				  ippGetString(attr, 0, NULL));
      } else if (!strcmp(attrname, "printer-uuid") &&
		 value_tag == IPP_TAG_URI)
	g_string_append_printf (browse_options, "uuid=%s ",
				ippGetString(attr, 0, NULL));
      else if (!strcmp(attrname, "job-sheets-default") &&
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
      browse_options = NULL;
      g_strchomp (browse_options_str);

      broadcast_browse_packets (type, state, uri, location,
				info, make_model,
				browse_options_str);

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

  if (conn)
    httpClose (conn);

  g_timeout_add_seconds (BrowseInterval, send_browse_data, NULL);

  /* Stop this timeout handler, we called a new one */
  return FALSE;
}

gboolean
browse_poll (gpointer data)
{
  static const char * const rattrs[] = { "printer-uri-supported" };
  char *server = strdup (data);
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;
  http_t *conn;
  int port = BrowsePort;
  char *colon,*slash;
  int major = 0;
  int minor = 0;

  slash = strchr (server, '/');
  if (slash) {
    *slash++ = '\0';
    if (!strcmp(slash, "version=1.0")) {
      major = 1;
      minor = 0;
    } else if (!strcmp(slash, "version=1.1")) {
      major = 1;
      minor = 1;
    } else if (!strcmp(slash, "version=2.0")) {
      major = 2;
      minor = 0;
    } else if (!strcmp(slash, "version=2.1")) {
      major = 2;
      minor = 1;
    } else if (!strcmp(slash, "version=2.2")) {
      major = 2;
      minor = 2;
    } else {
      debug_printf ("ignoring unknown server option: %s\n", slash);
    }
  }

  debug_printf ("cups-browsed: browse polling %s\n", server);

  colon = strchr (server, ':');
  if (colon) {
    char *endptr;
    unsigned long n;
    *colon++ = '\0';
    n = strtoul (colon, &endptr, 10);
    if (endptr != colon && n < INT_MAX)
      port = (int) n;
  }

  res_init ();
  conn = httpConnectEncrypt (server, port, HTTP_ENCRYPT_IF_REQUESTED);

  if (conn == NULL) {
    debug_printf("cups-browsed: browse poll failed to connect to %s\n", server);
    goto fail;
  }

  request = ippNewRequest(CUPS_GET_PRINTERS);
  if (major > 0) {
    debug_printf("cups-browsed: setting IPP version %d.%d\n", major, minor);
    ippSetVersion (request, major, minor);
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
    debug_printf("cups-browsed: browse poll failed for server %s: %s\n",
		 server, cupsLastErrorString ());
    goto fail;
  }

  for (attr = ippFirstAttribute(response); attr;
       attr = ippNextAttribute(response)) {
    const char *uri, *info;

    while (attr && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
      attr = ippNextAttribute(response);

    if (!attr)
      break;

    uri = NULL;
    info = NULL;
    while (attr && ippGetGroupTag(attr) == IPP_TAG_PRINTER) {
      if (!strcmp (ippGetName(attr), "printer-uri-supported") &&
	  ippGetValueTag(attr) == IPP_TAG_URI)
	uri = ippGetString(attr, 0, NULL);
      else if (!strcmp (ippGetName(attr), "printer-info") &&
	       ippGetValueTag(attr) == IPP_TAG_TEXT)
	info = ippGetString(attr, 0, NULL);

      attr = ippNextAttribute(response);
    }

    if (uri)
      found_cups_printer (server, uri, info);

    if (!attr)
      break;
  }

  recheck_timer ();

fail:
  if (response)
    ippDelete(response);

  if (conn)
    httpClose (conn);

  if (server)
    free (server);

  /* Call a new timeout handler so that we run again */
  g_timeout_add_seconds (BrowseInterval, browse_poll, data);

  /* Stop this timeout handler, we called a new one */
  return FALSE;
}

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
  return strcmp(a->name, b->name);
}

static void
sigterm_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  /* Flag that we should stop and return... */
  g_main_loop_quit(gmainloop);
  debug_printf("cups-browsed: Caught signal %d, shutting down ...\n", sig);
}

static int
read_browseallow_value (const char *value)
{
  char *p;
  struct in_addr addr;
  allow_t *allow = calloc (1, sizeof (allow_t));
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
  free (allow);
  return 1;
}

void
read_configuration (const char *filename)
{
  cups_file_t *fp;
  int linenum;
  char line[HTTP_MAX_BUFFER];
  char *value;
  const char *delim = " \t,";

  if (!filename)
    filename = CUPS_SERVERROOT "/cups-browsed.conf";

  if ((fp = cupsFileOpen(filename, "r")) == NULL) {
    debug_printf("cups-browsed: unable to open configuration file; "
		 "using defaults\n");
    return;
  }

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum)) {
    debug_printf("cups-browsed: Reading config: %s %s\n", line, value);
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
	else if (strcasecmp(p, "none"))
	  debug_printf("cups-browsed: unknown protocol '%s'\n", p);

	p = strtok_r (NULL, delim, &saveptr);
      }

      if (!strcasecmp(line, "BrowseLocalProtocols"))
	BrowseLocalProtocols = protocols;
      else if (!strcasecmp(line, "BrowseRemoteProtocols"))
	BrowseRemoteProtocols = protocols;
      else
	BrowseLocalProtocols = BrowseRemoteProtocols = protocols;
    } else if (!strcasecmp(line, "BrowsePoll") && value) {
      char **old = BrowsePoll;
      BrowsePoll = realloc (BrowsePoll, (NumBrowsePoll + 1) * sizeof (char *));
      if (!BrowsePoll) {
	debug_printf("cups-browsed: unable to realloc: ignoring BrowsePoll line\n");
	BrowsePoll = old;
      } else {
	debug_printf("cups-browsed: Adding BrowsePoll server: %s\n", value);
	BrowsePoll[NumBrowsePoll++] = strdup (value);
      }
    } else if (!strcasecmp(line, "BrowseAllow") && value) {
      if (read_browseallow_value (value))
	debug_printf ("cups-browsed: BrowseAllow value \"%s\" not understood\n",
		      value);
    } else if (!strcasecmp(line, "DomainSocket") && value) {
      if (value[0] != '\0')
	DomainSocket = strdup(value);
    }
  }

  cupsFileClose(fp);
}

int main(int argc, char*argv[]) {
#ifdef HAVE_AVAHI
  AvahiClient *client = NULL;
  AvahiServiceBrowser *sb1 = NULL, *sb2 = NULL;
  int error;
#endif /* HAVE_AVAHI */
  int ret = 1;
  http_t *http;
  cups_dest_t *dests,
              *dest;
  int i,
      num_dests;
  const char *val;
  remote_printer_t *p;

  /* Turn on debug mode if requested */
  if (argc >= 2 &&
      (!strcmp(argv[1], "--debug") || !strcmp(argv[1], "-d") ||
       !strncmp(argv[1], "-v", 2)))
    debug = 1;

  /* Initialise the browseallow array */
  browseallow = cupsArrayNew(compare_pointers, NULL);

  /* Read in cups-browsed.conf */
  read_configuration (NULL);

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
        (sockinfo.st_mode & S_IRWXO) == S_IRWXO)
      setenv("CUPS_SERVER", DomainSocket, 1);
    else
      setenv("CUPS_SERVER", "localhost", 1);
  } else
    setenv("CUPS_SERVER", "localhost", 1);

  if (BrowseLocalProtocols & BROWSE_DNSSD) {
    fprintf(stderr, "Local support for DNSSD not implemented\n");
    BrowseLocalProtocols &= ~BROWSE_DNSSD;
  }

#ifndef HAVE_AVAHI
  if (BrowseRemoteProtocols & BROWSE_DNSSD) {
    fprintf(stderr, "Remote support for DNSSD not supported\n");
    BrowseRemoteProtocols &= ~BROWSE_DNSSD;
  }
#endif /* HAVE_AVAHI */

  /* Wait for CUPS daemon to start */
  while ((http = httpConnectEncrypt(cupsServer(), ippPort(),
				    cupsEncryption())) == NULL)
    sleep(1);

  /* Initialise the array of network interfaces */
  netifs = cupsArrayNew(compare_pointers, NULL);
  update_netifs ();

  /* Read out the currently defined CUPS queues and find the ones which we
     have added in an earlier session */
  num_dests = cupsGetDests(&dests);
  remote_printers = cupsArrayNew((cups_array_func_t)compare_remote_printers,
				 NULL);
  if (num_dests > 0) {
    for (i = num_dests, dest = dests; i > 0; i --, dest ++) {
      if ((val = cupsGetOption(CUPS_BROWSED_MARK, dest->num_options,
			       dest->options)) != NULL) {
	if (strcasecmp(val, "no") != 0 && strcasecmp(val, "off") != 0 &&
	    strcasecmp(val, "false") != 0) {
	  /* Queue found, add to our list */
	  p = create_local_queue (dest->name,
				  strdup(cupsGetOption("device-uri",
						       dest->num_options,
						       dest->options)),
				  "", "", "", "");
	  if (p) {
	    /* Mark as unconfirmed, if no Avahi report of this queue appears
	       in a certain time frame, we will remove the queue */
	    p->status = STATUS_UNCONFIRMED;

	    if (BrowseRemoteProtocols & BROWSE_CUPS)
	      p->timeout = time(NULL) + BrowseTimeout;
	    else
	      p->timeout = time(NULL) + TIMEOUT_CONFIRM;

	    p->duplicate = 0;
	    debug_printf("cups-browsed: Found CUPS queue %s (URI: %s) from previous session.\n",
			 p->name, p->uri);
	  } else {
	    debug_printf("cups-browsed: ERROR: Unable to allocate memory.\n");
	    exit(1);
	  }
	}
      }
    }
    cupsFreeDests(num_dests, dests);
  }
  httpClose(http);

  /* Redirect SIGINT and SIGTERM so that we do a proper shutdown, removing
     the CUPS queues which we have created */
#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, sigterm_handler);
  sigset(SIGINT, sigterm_handler);
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
  debug_printf("cups-browsed: Using signal handler SIGACTION\n");
#else
  signal(SIGTERM, sigterm_handler);
  signal(SIGINT, sigterm_handler);
  debug_printf("cups-browsed: Using signal handler SIGNAL\n");
#endif /* HAVE_SIGSET */

#ifdef HAVE_AVAHI
  if (BrowseRemoteProtocols & BROWSE_DNSSD) {
    /* Allocate main loop object */
    if (!(glib_poll = avahi_glib_poll_new(NULL, G_PRIORITY_DEFAULT))) {
      debug_printf("cups-browsed: ERROR: Failed to create glib poll object.\n");
      goto avahi_fail;
    }

    /* Allocate a new client */
    client = avahi_client_new(avahi_glib_poll_get(glib_poll), 0,
			      client_callback, NULL, &error);

    /* Check wether creating the client object succeeded */
    if (!client) {
      debug_printf("cups-browsed: ERROR: Failed to create client: %s\n",
		   avahi_strerror(error));
      goto avahi_fail;
    }

    /* Create the service browsers */
    if (!(sb1 =
	  avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				    "_ipp._tcp", NULL, 0, browse_callback,
				    client))) {
      debug_printf("cups-browsed: ERROR: Failed to create service browser for IPP: %s\n",
		   avahi_strerror(avahi_client_errno(client)));
      goto avahi_fail;
    }
    if (!(sb2 =
	  avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				    "_ipps._tcp", NULL, 0, browse_callback,
				    client))) {
      debug_printf("cups-browsed: ERROR: Failed to create service browser for IPPS: %s\n",
		   avahi_strerror(avahi_client_errno(client)));
    avahi_fail:
      BrowseRemoteProtocols &= ~BROWSE_DNSSD;
      if (sb2) {
	avahi_service_browser_free(sb2);
	sb2 = NULL;
      }

      if (client) {
	avahi_client_free(client);
	client = NULL;
      }

      if (glib_poll) {
	avahi_glib_poll_free(glib_poll);
	glib_poll = NULL;
      }
    }
  }
#endif /* HAVE_AVAHI */

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

  if (BrowsePoll) {
    size_t index;
    for (index = 0;
	 index < NumBrowsePoll;
	 index++) {
      debug_printf ("cups-browsed: will browse poll %s every %ds\n",
		    BrowsePoll[index], BrowseInterval);
      g_idle_add (browse_poll, BrowsePoll[index]);
    }
  }

  g_main_loop_run (gmainloop);

  debug_printf("cups-browsed: main loop exited\n");
  g_main_loop_unref (gmainloop);
  gmainloop = NULL;
  ret = 0;

fail:

  /* Clean up things */

  /* Remove all queues which we have set up */
  for (p = (remote_printer_t *)cupsArrayFirst(remote_printers);
       p; p = (remote_printer_t *)cupsArrayNext(remote_printers)) {
    p->status = STATUS_DISAPPEARED;
    p->timeout = time(NULL) + TIMEOUT_IMMEDIATELY;
  }
  handle_cups_queues(NULL);

#ifdef HAVE_AVAHI
  /* Free the data structures for Bonjour browsing */
  if (sb1)
    avahi_service_browser_free(sb1);
  if (sb2)
    avahi_service_browser_free(sb2);

  if (client)
    avahi_client_free(client);

  if (glib_poll)
    avahi_glib_poll_free(glib_poll);
#endif /* HAVE_AVAHI */

  if (browsesocket != -1)
      close (browsesocket);

  return ret;
}

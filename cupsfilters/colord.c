//
// Common routines to access the colord CMS framework for libcupsfilter.
//
// Copyright (c) 2011, Tim Waugh
// Copyright (c) 2011-2013, Richard Hughes
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cups/raster.h>
#include <stdio.h>
#include <sys/types.h>
#include <cupsfilters/filter.h>
#include <cupsfilters/ipp.h>
#ifdef HAVE_DBUS
  #include <dbus/dbus.h>
#endif

#include "colord.h"

#define QUAL_COLORSPACE   0
#define QUAL_MEDIA        1
#define QUAL_RESOLUTION   2
#define QUAL_SIZE         3

char **
cfColordGetQualifier(cf_filter_data_t *data,
		     const char *color_space,
		     const char *media_type,
		     int x_res,
		     int y_res)
{
  int i, len;
  const char *val;
  const char *ptr1, *ptr2;
  char buf[64];
  char **tuple = NULL;
  int 			num_options = 0;
  cups_option_t 	*options = NULL;


  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

  // Get data from "cm-profile-qualifier" option
  if ((val =
       cupsGetOption("cm-profile-qualifier",
		     data->num_options, data->options)) != NULL &&
      val[0] != '\0')
  {
    tuple = calloc(QUAL_SIZE + 1, sizeof(char*));
    ptr1 = ptr2 = val;
    for (i = 0; i < QUAL_SIZE; i ++)
    {
      while (*ptr2 && *ptr2 != '.') ptr2 ++;
      len = ptr2 - ptr1;
      tuple[i] = malloc((len + 1) * sizeof(char));
      memcpy(tuple[i], ptr1, len);
      tuple[i][len] = '\0';
      if (*ptr2)
	ptr2 ++;
      ptr1 = ptr2; 
    }
  }
  else
  {
    // String for resolution
    if (x_res <= 0)
      buf[0] = '\0';
    else if (y_res <= 0 || y_res == x_res)
      snprintf(buf, sizeof(buf), "%ddpi", x_res);
    else
      snprintf(buf, sizeof(buf), "%dx%ddpi", x_res, y_res);

    // return a NULL terminated array so we don't have to break it up later
    tuple = calloc(QUAL_SIZE + 1, sizeof(char*));
    tuple[QUAL_COLORSPACE] = strdup(color_space ? color_space : "");
    tuple[QUAL_MEDIA]      = strdup(media_type ? media_type : "");
    tuple[QUAL_RESOLUTION] = strdup(buf);
  }
  
  cupsFreeOptions(num_options, options);

  return (tuple);
}

#ifdef HAVE_DBUS

static char *
get_filename_for_profile_path(cf_filter_data_t *data,
			      DBusConnection *con,
			      const char *object_path)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  char *filename = NULL;
  const char *interface = "org.freedesktop.ColorManager.Profile";
  const char *property = "Filename";
  const char *tmp;
  DBusError error;
  DBusMessageIter args;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;
  DBusMessageIter sub;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
					 object_path,
					 "org.freedesktop.DBus.Properties",
					 "Get");

  dbus_message_iter_init_append(message, &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property);

  // send syncronous
  dbus_error_init(&error);
  if (log) log(ld, CF_LOGLEVEL_DEBUG, "Calling %s.Get(%s)", interface, property);

  reply = dbus_connection_send_with_reply_and_block(con,
						    message,
						    -1,
						    &error);
  if (reply == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "DEBUG: Failed to send: %s:%s",
		 error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  // get reply data
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Incorrect reply type");
    goto out;
  }

  dbus_message_iter_recurse(&args, &sub);
  dbus_message_iter_get_basic(&sub, &tmp);
  filename = strdup(tmp);
 out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  return (filename);
}

static char *
get_profile_for_device_path(cf_filter_data_t *data,
			    DBusConnection *con,
			    const char *object_path,
			    const char **split)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  char **key = NULL;
  char *profile = NULL;
  char str[256];
  const char *tmp;
  DBusError error;
  DBusMessageIter args;
  DBusMessageIter entry;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;
  int i = 0;
  const int max_keys = 7;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
                                         object_path,
                                         "org.freedesktop.ColorManager.Device",
                                         "GetProfileForQualifiers");
  dbus_message_iter_init_append(message, &args);

  // create the fallbacks
  key = calloc(max_keys + 1, sizeof(char*));

  // exact match
  i = 0;
  snprintf(str, sizeof(str), "%s.%s.%s",
           split[QUAL_COLORSPACE],
           split[QUAL_MEDIA],
           split[QUAL_RESOLUTION]);
  key[i++] = strdup(str);
  snprintf(str, sizeof(str), "%s.%s.*",
           split[QUAL_COLORSPACE],
           split[QUAL_MEDIA]);
  key[i++] = strdup(str);
  snprintf(str, sizeof(str), "%s.*.%s",
           split[QUAL_COLORSPACE],
           split[QUAL_RESOLUTION]);
  key[i++] = strdup(str);
  snprintf(str, sizeof(str), "%s.*.*",
           split[QUAL_COLORSPACE]);
  key[i++] = strdup(str);
  key[i++] = strdup("*");
  dbus_message_iter_open_container(&args,
                                   DBUS_TYPE_ARRAY,
                                   "s",
                                   &entry);
  for (i = 0; key[i] != NULL; i ++)
  {
    dbus_message_iter_append_basic(&entry,
                                   DBUS_TYPE_STRING,
                                   &key[i]);
  }
  dbus_message_iter_close_container(&args, &entry);

  // send syncronous
  dbus_error_init(&error);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Calling GetProfileForQualifiers(%s...)", key[0]);
  reply = dbus_connection_send_with_reply_and_block(con,
                                                    message,
                                                    -1,
                                                    &error);
  if (reply == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG, "Failed to send: %s:%s",
		 error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  // get reply data
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Incorrect reply type");
    goto out;
  }
  dbus_message_iter_get_basic(&args, &tmp);
  if (log) log(ld, CF_LOGLEVEL_DEBUG, "Found profile %s", tmp);

  // get filename
  profile = get_filename_for_profile_path(data, con, tmp);

 out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  if (key != NULL)
  {
    for (i = 0; i < max_keys; i ++)
      free(key[i]);
    free(key);
  }
  return (profile);
}

static char *
get_device_path_for_device_id(cf_filter_data_t *data,
			      DBusConnection *con,
			      const char *device_id)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  char *device_path = NULL;
  const char *device_path_tmp;
  DBusError error;
  DBusMessageIter args;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
                                         "/org/freedesktop/ColorManager",
                                         "org.freedesktop.ColorManager",
                                         "FindDeviceById");
  dbus_message_iter_init_append(message, &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &device_id);

  // send syncronous
  dbus_error_init(&error);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Calling FindDeviceById(%s)", device_id);
  reply = dbus_connection_send_with_reply_and_block(con,
						    message,
						    -1,
						    &error);
  if (reply == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Failed to send: %s:%s",
		 error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  // get reply data
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Incorrect reply type");
    goto out;
  }
  dbus_message_iter_get_basic(&args, &device_path_tmp);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Found device %s", device_path_tmp);
  device_path = strdup(device_path_tmp);
 out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  return (device_path);
}

char *
cfColordGetProfileForDeviceID(cf_filter_data_t *data,
			      const char *device_id,
			      const char **qualifier_tuple)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  DBusConnection *con = NULL;
  char *device_path = NULL;
  char *filename = NULL;

  if (device_id == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "No colord device ID available");
    goto out;
  }

  // connect to system bus
  con = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
  if (con == NULL)
  {
    // If D-Bus is not reachable, gracefully leave and ignore error
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Failed to connect to system bus");
    goto out;
  }

  // find the device
  device_path = get_device_path_for_device_id (data, con, device_id);
  if (device_path == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Failed to get device %s", device_id);
    goto out;
  }

  // get the best profile for the device
  filename = get_profile_for_device_path(data, con, device_path,
					 qualifier_tuple);
  if (filename == NULL || !filename[0])
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Failed to get profile filename for %s", device_id);
    goto out;
  }
  if (log) log(ld, CF_LOGLEVEL_ERROR,
	       "Use profile filename: '%s'", filename);
 out:
  free(device_path);
  if (con != NULL)
    dbus_connection_unref(con);
  return (filename);
}

static int
get_profile_inhibitors(cf_filter_data_t *data,
		       DBusConnection *con,
		       const char *object_path)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  char *tmp;
  const char *interface = "org.freedesktop.ColorManager.Device";
  const char *property = "ProfilingInhibitors";
  DBusError error;
  DBusMessageIter args;
  DBusMessageIter sub;
  DBusMessageIter sub2;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;
  int inhibitors = 0;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
                                         object_path,
                                         "org.freedesktop.DBus.Properties",
                                         "Get");

  dbus_message_iter_init_append(message, &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property);

  // send syncronous
  dbus_error_init(&error);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Calling %s.Get(%s)", interface, property);
  reply = dbus_connection_send_with_reply_and_block(con,
                                                    message,
                                                    -1,
                                                    &error);
  if (reply == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Failed to send: %s:%s",
		 error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  // get reply data
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Incorrect reply type");
    goto out;
  }

  // count the size of the array
  dbus_message_iter_recurse(&args, &sub2);
  dbus_message_iter_recurse(&sub2, &sub);
  while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID)
  {
    dbus_message_iter_get_basic(&sub, &tmp);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Inhibitor %s exists", tmp);
    dbus_message_iter_next(&sub);
    inhibitors++;
  }
 out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  return (inhibitors);
}

int
cfColordGetInhibitForDeviceID(cf_filter_data_t *data,
			      const char *device_id)
{
  cf_logfunc_t log = data->logfunc;
  void* ld = data->logdata;
  DBusConnection *con;
  char *device_path = NULL;
  int has_inhibitors = FALSE;

  // connect to system bus
  con = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
  if (con == NULL)
  {
    // If D-Bus is not reachable, gracefully leave and ignore error
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "Failed to connect to system bus");
    goto out;
  }

  // find the device
  device_path = get_device_path_for_device_id (data, con, device_id);
  if (device_path == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Failed to get find device %s", device_id);
    goto out;
  }

  // get the best profile for the device
  has_inhibitors = get_profile_inhibitors(data, con, device_path);
 out:
  free(device_path);
  if (con != NULL)
    dbus_connection_unref(con);
  return (has_inhibitors);
}

#else

char *
cfColordGetProfileForDeviceID(cf_filter_data_t *data,
			      const char *device_id,
			      const char **qualifier_tuple)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  if (log) log(ld, CF_LOGLEVEL_WARN,
	       "not compiled with DBus support");
  return (NULL);
}

int
cfColordGetInhibitForDeviceID(cf_filter_data_t *data,
			      const char *device_id)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  if (log) log(ld, CF_LOGLEVEL_WARN,
	       "not compiled with DBus support");
  return (0);
}

#endif

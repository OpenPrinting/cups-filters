//
// Log functions header file for libcupsfilters.
//
// Copyright 2020-2022 by Till Kamppeter.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_FILTERS_LOG_H_
#  define _CUPS_FILTERS_LOG_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Types...
//

typedef enum cf_loglevel_e           // Log levels, same as PAPPL, similar
{				     // to CUPS
  CF_LOGLEVEL_UNSPEC = -1,           // Not specified
  CF_LOGLEVEL_DEBUG,                 // Debug message
  CF_LOGLEVEL_INFO,                  // Informational message
  CF_LOGLEVEL_WARN,                  // Warning message
  CF_LOGLEVEL_ERROR,                 // Error message
  CF_LOGLEVEL_FATAL,                 // Fatal message
  CF_LOGLEVEL_CONTROL                // Control message
} cf_loglevel_t;

typedef void (*cf_logfunc_t)(void *data, cf_loglevel_t level,
			     const char *message, ...);

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_LOG_H_

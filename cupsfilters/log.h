/*
 *   Log functions header file for cups-filters.
 *
 *   Copyright 2020 by Till Kamppeter.
 *
 *   Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_FILTERS_LOG_H_
#  define _CUPS_FILTERS_LOG_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Types...
 */

typedef enum filter_loglevel_e {        /* Log levels, same as PAPPL, similar
					   to CUPS */
 FILTER_LOGLEVEL_UNSPEC = -1,           /* Not specified */
 FILTER_LOGLEVEL_DEBUG,                 /* Debug message */
 FILTER_LOGLEVEL_INFO,                  /* Informational message */
 FILTER_LOGLEVEL_WARN,                  /* Warning message */
 FILTER_LOGLEVEL_ERROR,                 /* Error message */
 FILTER_LOGLEVEL_FATAL,                 /* Fatal message */
 FILTER_LOGLEVEL_CONTROL                /* Control message */
} filter_loglevel_t;

typedef void (*filter_logfunc_t)(void *data, filter_loglevel_t level,
				 const char *message, ...);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_LOG_H_ */

/*
 * End
 */

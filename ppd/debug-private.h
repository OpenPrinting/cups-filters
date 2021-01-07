/*
 * Private debugging APIs for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2005 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_DEBUG_PRIVATE_H_
#  define _PPD_DEBUG_PRIVATE_H_

/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * The debug macros are used if you compile with DEBUG defined.
 *
 * Usage:
 *
 *   DEBUG_set("logfile", "level", "filter", 1)
 *
 * The DEBUG_set macro allows an application to programmatically enable (or
 * disable) debug logging.  The arguments correspond to the PPD_DEBUG_LOG,
 * PPD_DEBUG_LEVEL, and PPD_DEBUG_FILTER environment variables.  The 1 on the
 * end forces the values to override the environment.
 */

#  ifdef DEBUG
#    define DEBUG_set(logfile,level,filter) _ppd_debug_set(logfile,level,filter,1)
#  else
#    define DEBUG_set(logfile,level,filter)
#  endif /* DEBUG */


/*
 * Prototypes...
 */

extern void	_ppd_debug_set(const char *logfile, const char *level, const char *filter, int force);
#  ifdef _WIN32
extern int	_ppd_gettimeofday(struct timeval *tv, void *tz);
#    define gettimeofday(a,b) _ppd_gettimeofday(a, b)
#  endif /* _WIN32 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_PPD_DEBUG_PRIVATE_H_ */

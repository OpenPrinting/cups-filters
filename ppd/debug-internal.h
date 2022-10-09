/*
 * Internal debugging macros for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2005 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_DEBUG_INTERNAL_H_
#  define _PPD_DEBUG_INTERNAL_H_


/*
 * Include necessary headers...
 */

#  include "debug-private.h"


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
 *   DEBUG_puts("string")
 *   DEBUG_printf(("format string", arg, arg, ...));
 *
 * Note the extra parenthesis around the DEBUG_printf macro...
 *
 * Newlines are not required on the end of messages, as both add one when
 * writing the output.
 *
 * If the first character is a digit, then it represents the "log level" of the
 * message from 0 to 9.  The default level is 1.  The following defines the
 * current levels we use:
 *
 * 0 = public APIs, other than value accessor functions
 * 1 = return values for public APIs
 * 2 = public value accessor APIs, progress for public APIs
 * 3 = return values for value accessor APIs
 * 4 = private APIs, progress for value accessor APIs
 * 5 = return values for private APIs
 * 6 = progress for private APIs
 * 7 = static functions
 * 8 = return values for static functions
 * 9 = progress for static functions
 */

#  ifdef DEBUG
#    include <assert.h>
#    define DEBUG_puts(x) _ppd_debug_puts(x)
#    define DEBUG_printf(x) _ppd_debug_printf x
#    define DEBUG_assert(x) assert(x)
#  else
#    define DEBUG_puts(x)
#    define DEBUG_printf(x)
#    define DEBUG_assert(x)
#  endif /* DEBUG */


/*
 * Prototypes...
 */

#  ifdef DEBUG
extern int	_ppd_debug_fd;
extern int	_ppd_debug_level;
extern void	_ppd_debug_printf(const char *format, ...);
extern void	_ppd_debug_puts(const char *s);
#  endif /* DEBUG */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_PPD_DEBUG_INTERNAL_H_ */

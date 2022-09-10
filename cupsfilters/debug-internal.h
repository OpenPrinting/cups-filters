//
// Internal debugging macros for libcupsfilters.
//
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_FILTERS_DEBUG_INTERNAL_H_
#  define _CUPS_FILTERS_DEBUG_INTERNAL_H_


//
// C++ magic...
//

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// The debug macros are used if you compile with DEBUG defined.
//
// Usage:
//
//   DEBUG_puts("string");
//   DEBUG_printf(("format string", arg, arg, ...));
//   DEBUG_assert(boolean expression);
//
// Note the extra parenthesis around the DEBUG_printf macro...
//

#  ifdef DEBUG
#    include <assert.h>
#    define DEBUG_puts(x) _cf_debug_puts(x)
#    define DEBUG_printf(x) _cf_debug_printf x
#    define DEBUG_assert(x) assert(x)
#  else
#    define DEBUG_puts(x)
#    define DEBUG_printf(x)
#    define DEBUG_assert(x)
#  endif // DEBUG

#  ifdef DEBUG
extern void	_cf_debug_printf(const char *format, ...);
extern void	_cf_debug_puts(const char *s);
#  endif // DEBUG

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_DEBUG_INTERNAL_H_

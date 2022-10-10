//
// Internal debugging macros for libfontembed.
//
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _FONTEMBED_DEBUG_INTERNAL_H_
#  define _FONTEMBED_DEBUG_INTERNAL_H_


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
//   DEBUG_assert(boolean expression);
//

#  ifdef DEBUG
#    include <assert.h>
#    define DEBUG_assert(x) assert(x)
#  else
#    define DEBUG_assert(x)
#  endif // DEBUG

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_FONTEMBED_DEBUG_INTERNAL_H_

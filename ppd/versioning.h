/*
 * API versioning definitions for libppd.
 *
 * Copyright © 2007-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_VERSIONING_H_
#  define _PPD_VERSIONING_H_

/*
 * This header defines several macros that add compiler-specific attributes for
 * functions:
 *
 *   - _PPD_INTERNAL: Function is internal with no replacement API.
 *   - _PPD_PRIVATE: Specifies the function is private to CUPS.
 *   - _PPD_PUBLIC: Specifies the function is public API.
 */

/*
 * Determine which compiler is being used and what annotation features are
 * available...
 */

#  ifdef __APPLE__
#    include <os/availability.h>
#  endif /* __APPLE__ */

#  ifdef __has_extension		/* Clang */
#    define _PPD_HAS_VISIBILITY
#  elif defined(__GNUC__)		/* GCC and compatible */
#    if __GNUC__ >= 3			/* GCC 3.0 or higher */
#      define _PPD_HAS_VISIBILITY
#    endif /* __GNUC__ >= 3 */
#  elif defined(_WIN32)
#    define __attribute__(...)
#  endif /* __has_extension */


/*
 * Define _PPD_INTERNAL, _PPD_PRIVATE, and _PPD_PUBLIC visibilty macros for
 * internal/private/public functions...
 */

#  ifdef _PPD_HAS_VISIBILITY
#    define _PPD_INTERNAL	__attribute__ ((visibility("hidden")))
#    define _PPD_PRIVATE	__attribute__ ((visibility("default")))
#    define _PPD_PUBLIC	__attribute__ ((visibility("default")))
#  elif defined(_WIN32) && defined(LIBCUPS2_EXPORTS) && 0
#    define _PPD_INTERNAL
#    define _PPD_PRIVATE	__declspec(dllexport)
#    define _PPD_PUBLIC	__declspec(dllexport)
#  else
#    define _PPD_INTERNAL
#    define _PPD_PRIVATE
#    define _PPD_PUBLIC
#  endif /* _PPD_HAS_VISIBILITY */

#endif /* !_PPD_VERSIONING_H_ */

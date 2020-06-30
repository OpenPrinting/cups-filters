/*
 * Private array definitions for libppd.
 *
 * Copyright 2011-2012 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

#ifndef _PPD_ARRAY_PRIVATE_H_
#  define _PPD_ARRAY_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include <cups/array.h>
#  include "versioning.h"


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Functions...
 */

extern int		_ppdArrayAddStrings(cups_array_t *a, const char *s,
			                     char delim) _PPD_PRIVATE;
extern cups_array_t	*_ppdArrayNewStrings(const char *s, char delim)
			                      _PPD_PRIVATE;

#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_PPD_ARRAY_PRIVATE_H_ */

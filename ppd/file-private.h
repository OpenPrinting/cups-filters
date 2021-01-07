/*
 * Private file check definitions for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_FILE_PRIVATE_H_
#  define _PPD_FILE_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include "string-private.h"
#  include <cupsfilters/log.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <stdarg.h>
#  include <fcntl.h>

#  ifdef _WIN32
#    include <io.h>
#    include <sys/locking.h>
#  endif /* _WIN32 */


#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Types and structures...
 */

typedef enum				/**** _ppdFileCheck return values ****/
{
  _PPD_FILE_CHECK_OK = 0,		/* Everything OK */
  _PPD_FILE_CHECK_MISSING = 1,		/* File is missing */
  _PPD_FILE_CHECK_PERMISSIONS = 2,	/* File (or parent dir) has bad perms */
  _PPD_FILE_CHECK_WRONG_TYPE = 3,	/* File has wrong type */
  _PPD_FILE_CHECK_RELATIVE_PATH = 4	/* File contains a relative path */
} _ppd_fc_result_t;

typedef enum				/**** _ppdFileCheck file type
					      values ****/
{
  _PPD_FILE_CHECK_FILE = 0,		/* Check the file and parent
					   directory */
  _PPD_FILE_CHECK_PROGRAM = 1,		/* Check the program and parent
					   directory */
  _PPD_FILE_CHECK_FILE_ONLY = 2,	/* Check the file only */
  _PPD_FILE_CHECK_DIRECTORY = 3		/* Check the directory */
} _ppd_fc_filetype_t;

typedef void (*_ppd_fc_func_t)(void *context, _ppd_fc_result_t result,
				const char *message);

/*
 * Prototypes...
 */

extern _ppd_fc_result_t	_ppdFileCheck(const char *filename,
				      _ppd_fc_filetype_t filetype,
				      int dorootchecks,
				      filter_logfunc_t log,
				      void *ld);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_PPD_FILE_PRIVATE_H_ */

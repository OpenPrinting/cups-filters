/*
 * File functions for libppd
 *
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "file-private.h"
#include "language-private.h"
#include "debug-internal.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <cups/language.h>

#  ifdef HAVE_LIBZ
#    include <zlib.h>
#  endif /* HAVE_LIBZ */


#ifndef _WIN32
/*
 * '_ppdFileCheck()' - Check the permissions of the given filename.
 */

_ppd_fc_result_t			/* O - Check result */
_ppdFileCheck(
    const char          *filename,	/* I - Filename to check */
    _ppd_fc_filetype_t  filetype,	/* I - Type of file checks? */
    int                 dorootchecks,	/* I - Check for root permissions? */
    cf_logfunc_t    log,		/* I - Log function */
    void                *ld)		/* I - Data pointer for log function */
{
  struct stat		fileinfo;	/* File information */
  char			message[1024],	/* Message string */
			temp[1024],	/* Parent directory filename */
			*ptr;		/* Pointer into parent directory */
  _ppd_fc_result_t	result;		/* Check result */


 /*
  * Does the filename contain a relative path ("../")?
  */

  if (strstr(filename, "../"))
  {
   /*
    * Yes, fail it!
    */

    result = _PPD_FILE_CHECK_RELATIVE_PATH;
    goto finishup;
  }

 /*
  * Does the program even exist and is it accessible?
  */

  if (stat(filename, &fileinfo))
  {
   /*
    * Nope...
    */

    result = _PPD_FILE_CHECK_MISSING;
    goto finishup;
  }

 /*
  * Check the execute bit...
  */

  result = _PPD_FILE_CHECK_OK;

  switch (filetype)
  {
    case _PPD_FILE_CHECK_DIRECTORY :
        if (!S_ISDIR(fileinfo.st_mode))
	  result = _PPD_FILE_CHECK_WRONG_TYPE;
        break;

    default :
        if (!S_ISREG(fileinfo.st_mode))
	  result = _PPD_FILE_CHECK_WRONG_TYPE;
        break;
  }

  if (result)
    goto finishup;

 /*
  * Are we doing root checks?
  */

  if (!dorootchecks)
  {
   /*
    * Nope, so anything (else) goes...
    */

    goto finishup;
  }

 /*
  * Verify permission of the file itself:
  *
  * 1. Must be owned by root
  * 2. Must not be writable by group
  * 3. Must not be setuid
  * 4. Must not be writable by others
  */

  if (fileinfo.st_uid ||		/* 1. Must be owned by root */
      (fileinfo.st_mode & S_IWGRP)  ||	/* 2. Must not be writable by group */
      (fileinfo.st_mode & S_ISUID) ||	/* 3. Must not be setuid */
      (fileinfo.st_mode & S_IWOTH))	/* 4. Must not be writable by others */
  {
    result = _PPD_FILE_CHECK_PERMISSIONS;
    goto finishup;
  }

  if (filetype == _PPD_FILE_CHECK_DIRECTORY ||
      filetype == _PPD_FILE_CHECK_FILE_ONLY)
    goto finishup;

 /*
  * Now check the containing directory...
  */

  strlcpy(temp, filename, sizeof(temp));
  if ((ptr = strrchr(temp, '/')) != NULL)
  {
    if (ptr == temp)
      ptr[1] = '\0';
    else
      *ptr = '\0';
  }

  if (stat(temp, &fileinfo))
  {
   /*
    * Doesn't exist?!?
    */

    result   = _PPD_FILE_CHECK_MISSING;
    filetype = _PPD_FILE_CHECK_DIRECTORY;
    filename = temp;

    goto finishup;
  }

  if (fileinfo.st_uid ||		/* 1. Must be owned by root */
      (fileinfo.st_mode & S_IWGRP) ||	/* 2. Must not be writable by group */
      (fileinfo.st_mode & S_ISUID) ||	/* 3. Must not be setuid */
      (fileinfo.st_mode & S_IWOTH))	/* 4. Must not be writable by others */
  {
    result   = _PPD_FILE_CHECK_PERMISSIONS;
    filetype = _PPD_FILE_CHECK_DIRECTORY;
    filename = temp;
  }

 /*
  * Common return point...
  */

  finishup:

  if (log)
  {
    cups_lang_t *lang = cupsLangDefault();
					/* Localization information */
    cf_loglevel_t loglevel;

    switch (result)
    {
      case _PPD_FILE_CHECK_OK :
	  loglevel = CF_LOGLEVEL_DEBUG;
	  if (filetype == _PPD_FILE_CHECK_DIRECTORY)
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("Directory \"%s\" permissions OK "
					     "(0%o/uid=%d/gid=%d).")),
		     filename, fileinfo.st_mode, (int)fileinfo.st_uid,
		     (int)fileinfo.st_gid);
	  else
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("File \"%s\" permissions OK "
					     "(0%o/uid=%d/gid=%d).")),
		     filename, fileinfo.st_mode, (int)fileinfo.st_uid,
		     (int)fileinfo.st_gid);
          break;

      case _PPD_FILE_CHECK_MISSING :
	  loglevel = CF_LOGLEVEL_ERROR;
	  if (filetype == _PPD_FILE_CHECK_DIRECTORY)
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("Directory \"%s\" not available: "
					     "%s")),
		     filename, strerror(errno));
	  else
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("File \"%s\" not available: %s")),
		     filename, strerror(errno));
          break;

      case _PPD_FILE_CHECK_PERMISSIONS :
	  loglevel = CF_LOGLEVEL_ERROR;
	  if (filetype == _PPD_FILE_CHECK_DIRECTORY)
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("Directory \"%s\" has insecure "
					     "permissions "
					     "(0%o/uid=%d/gid=%d).")),
		     filename, fileinfo.st_mode, (int)fileinfo.st_uid,
		     (int)fileinfo.st_gid);
	  else
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("File \"%s\" has insecure "
		                             "permissions "
					     "(0%o/uid=%d/gid=%d).")),
		     filename, fileinfo.st_mode, (int)fileinfo.st_uid,
		     (int)fileinfo.st_gid);
          break;

      case _PPD_FILE_CHECK_WRONG_TYPE :
	  loglevel = CF_LOGLEVEL_ERROR;
	  if (filetype == _PPD_FILE_CHECK_DIRECTORY)
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("Directory \"%s\" is a file.")),
		     filename);
	  else
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("File \"%s\" is a directory.")),
		     filename);
          break;

      case _PPD_FILE_CHECK_RELATIVE_PATH :
	  loglevel = CF_LOGLEVEL_ERROR;
	  if (filetype == _PPD_FILE_CHECK_DIRECTORY)
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("Directory \"%s\" contains a "
					     "relative path.")), filename);
	  else
	    snprintf(message, sizeof(message),
		     _ppdLangString(lang, _("File \"%s\" contains a relative "
					     "path.")), filename);
          break;
    }

    log(ld, loglevel, message);
  }

  return (result);
}
#endif /* !_WIN32 */

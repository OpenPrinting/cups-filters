/*
 * Private localization support for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_LANGUAGE_PRIVATE_H_
#  define _PPD_LANGUAGE_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include "config.h"
#  include <stdio.h>
#  include <cups/transcode.h>
#  ifdef __APPLE__
#    include <CoreFoundation/CoreFoundation.h>
#  endif /* __APPLE__ */
#  include "versioning.h"

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Macro for localized text...
 */

#  define _(x) x


/*
 * Constants...
 */

#  define _PPD_MESSAGE_PO	0	/* Message file is in GNU .po format */
#  define _PPD_MESSAGE_UNQUOTE	1	/* Unescape \foo in strings? */
#  define _PPD_MESSAGE_STRINGS	2	/* Message file is in Apple .strings format */
#  define _PPD_MESSAGE_EMPTY	4	/* Allow empty localized strings */


/*
 * Types...
 */

typedef struct _ppd_message_s		/**** Message catalog entry ****/
{
  char	*msg,				/* Original string */
	*str;				/* Localized string */
} _ppd_message_t;


/*
 * Prototypes...
 */

extern const char	*_ppdLangString(cups_lang_t *lang, const char *message) _PPD_PRIVATE;
extern void		_ppdMessageFree(cups_array_t *a) _PPD_PRIVATE;
extern cups_array_t	*_ppdMessageLoad(const char *filename, int flags) _PPD_PRIVATE;
extern const char	*_ppdMessageLookup(cups_array_t *a, const char *m) _PPD_PRIVATE;
extern cups_array_t	*_ppdMessageNew(void *context) _PPD_PRIVATE;


#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_PPD_LANGUAGE_PRIVATE_H_ */

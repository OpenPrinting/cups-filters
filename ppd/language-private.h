/*
 * Private localization support for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _CUPS_LANGUAGE_PRIVATE_H_
#  define _CUPS_LANGUAGE_PRIVATE_H_

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

#  define _CUPS_MESSAGE_PO	0	/* Message file is in GNU .po format */
#  define _CUPS_MESSAGE_UNQUOTE	1	/* Unescape \foo in strings? */
#  define _CUPS_MESSAGE_STRINGS	2	/* Message file is in Apple .strings format */
#  define _CUPS_MESSAGE_EMPTY	4	/* Allow empty localized strings */


/*
 * Types...
 */

typedef struct _cups_message_s		/**** Message catalog entry ****/
{
  char	*msg,				/* Original string */
	*str;				/* Localized string */
} _cups_message_t;


/*
 * Prototypes...
 */

extern const char	*_cupsLangString(cups_lang_t *lang, const char *message) _CUPS_PRIVATE;
extern void		_cupsMessageFree(cups_array_t *a) _CUPS_PRIVATE;
extern cups_array_t	*_cupsMessageLoad(const char *filename, int flags) _CUPS_PRIVATE;
extern const char	*_cupsMessageLookup(cups_array_t *a, const char *m) _CUPS_PRIVATE;
extern cups_array_t	*_cupsMessageNew(void *context) _CUPS_PRIVATE;


#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_LANGUAGE_PRIVATE_H_ */

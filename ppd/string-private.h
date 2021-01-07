/*
 * Private string definitions for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_STRING_PRIVATE_H_
#  define _PPD_STRING_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include "config.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <stdarg.h>
#  include <ctype.h>
#  include <errno.h>
#  include <locale.h>
#  include <time.h>

#  ifdef HAVE_STRING_H
#    include <string.h>
#  endif /* HAVE_STRING_H */

#  ifdef HAVE_STRINGS_H
#    include <strings.h>
#  endif /* HAVE_STRINGS_H */

#  ifdef HAVE_BSTRING_H
#    include <bstring.h>
#  endif /* HAVE_BSTRING_H */

#  if defined(_WIN32) && !defined(__CUPS_SSIZE_T_DEFINED)
#    define __CUPS_SSIZE_T_DEFINED
#    include <stddef.h>
/* Windows does not support the ssize_t type, so map it to long... */
typedef long ssize_t;			/* @private@ */
#  endif /* _WIN32 && !__CUPS_SSIZE_T_DEFINED */


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * String pool structures...
 */

#  define _PPD_STR_GUARD	0x56788765

typedef struct _ppd_sp_item_s		/**** String Pool Item ****/
{
#  ifdef DEBUG_GUARDS
  unsigned int	guard;			/* Guard word */
#  endif /* DEBUG_GUARDS */
  unsigned int	ref_count;		/* Reference count */
  char		str[1];			/* String */
} _ppd_sp_item_t;


/*
 * Replacements for the ctype macros that are not affected by locale, since we
 * really only care about testing for ASCII characters when parsing files, etc.
 *
 * The _PPD_INLINE definition controls whether we get an inline function body,
 * and external function body, or an external definition.
 */

#  if defined(__GNUC__) || __STDC_VERSION__ >= 199901L
#    define _PPD_INLINE static inline
#  elif defined(_MSC_VER)
#    define _PPD_INLINE static __inline
#  elif defined(_PPD_STRING_C_)
#    define _PPD_INLINE
#  endif /* __GNUC__ || __STDC_VERSION__ */

#  ifdef _PPD_INLINE
_PPD_INLINE int			/* O - 1 on match, 0 otherwise */
_ppd_isalnum(int ch)			/* I - Character to test */
{
  return ((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

_PPD_INLINE int			/* O - 1 on match, 0 otherwise */
_ppd_isalpha(int ch)			/* I - Character to test */
{
  return ((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

_PPD_INLINE int			/* O - 1 on match, 0 otherwise */
_ppd_islower(int ch)			/* I - Character to test */
{
  return (ch >= 'a' && ch <= 'z');
}

_PPD_INLINE int			/* O - 1 on match, 0 otherwise */
_ppd_isspace(int ch)			/* I - Character to test */
{
  return (ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' ||
          ch == '\v');
}

_PPD_INLINE int			/* O - 1 on match, 0 otherwise */
_ppd_isupper(int ch)			/* I - Character to test */
{
  return (ch >= 'A' && ch <= 'Z');
}

_PPD_INLINE int			/* O - Converted character */
_ppd_tolower(int ch)			/* I - Character to convert */
{
  return (_ppd_isupper(ch) ? ch - 'A' + 'a' : ch);
}

_PPD_INLINE int			/* O - Converted character */
_ppd_toupper(int ch)			/* I - Character to convert */
{
  return (_ppd_islower(ch) ? ch - 'a' + 'A' : ch);
}
#  else
extern int _ppd_isalnum(int ch);
extern int _ppd_isalpha(int ch);
extern int _ppd_islower(int ch);
extern int _ppd_isspace(int ch);
extern int _ppd_isupper(int ch);
extern int _ppd_tolower(int ch);
extern int _ppd_toupper(int ch);
#  endif /* _PPD_INLINE */


/*
 * Prototypes...
 */

extern ssize_t	_ppd_safe_vsnprintf(char *buffer, size_t bufsize, const char *format, va_list args);
extern void	_ppd_strcpy(char *dst, const char *src);

#  ifndef HAVE_STRDUP
extern char	*_ppd_strdup(const char *);
#    define strdup _ppd_strdup
#  endif /* !HAVE_STRDUP */

extern int	_ppd_strcasecmp(const char *, const char *);

extern int	_ppd_strncasecmp(const char *, const char *, size_t n);

#  ifndef HAVE_STRLCAT
extern size_t _ppd_strlcat(char *, const char *, size_t);
#    define strlcat _ppd_strlcat
#  endif /* !HAVE_STRLCAT */

#  ifndef HAVE_STRLCPY
extern size_t _ppd_strlcpy(char *, const char *, size_t);
#    define strlcpy _ppd_strlcpy
#  endif /* !HAVE_STRLCPY */

#  ifndef HAVE_SNPRINTF
extern int	_ppd_snprintf(char *, size_t, const char *, ...);
#    define snprintf _ppd_snprintf
#  endif /* !HAVE_SNPRINTF */

#  ifndef HAVE_VSNPRINTF
extern int	_ppd_vsnprintf(char *, size_t, const char *, va_list);
#    define vsnprintf _ppd_vsnprintf
#  endif /* !HAVE_VSNPRINTF */

/*
 * String pool functions...
 */

extern char	*_ppdStrAlloc(const char *s);
extern void	_ppdStrFlush(void);
extern void	_ppdStrFree(const char *s);
extern char	*_ppdStrRetain(const char *s);
extern size_t	_ppdStrStatistics(size_t *alloc_bytes, size_t *total_bytes);


/*
 * Floating point number functions...
 */

extern char	*_ppdStrFormatd(char *buf, char *bufend, double number,
		                 struct lconv *loc);
extern double	_ppdStrScand(const char *buf, char **bufptr,
		              struct lconv *loc);


/*
 * C++ magic...
 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_PPD_STRING_H_ */

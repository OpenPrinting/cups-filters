//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "dynstring-private.h"
#include <cupsfilters/debug-internal.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
__cfFontEmbedDynInit(__cf_fontembed_dyn_string_t *ds,
		     int reserve_size) // {{{
{
  DEBUG_assert(ds);
  DEBUG_assert(reserve_size > 0);

  ds->len = 0;
  ds->alloc = reserve_size;
  ds->buf = malloc(ds->alloc + 1);
  if (!ds->buf)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    DEBUG_assert(0);
    ds->len = -1;
    return (-1);
  }
  return (0);
}
// }}}


void
__cfFontEmbedDynFree(__cf_fontembed_dyn_string_t *ds) // {{{
{
  DEBUG_assert(ds);

  ds->len= -1;
  ds->alloc = 0;
  free(ds->buf);
  ds->buf = NULL;
}
// }}}


int
__cfFontEmbedDynEnsure(__cf_fontembed_dyn_string_t *ds,
		       int free_space) // {{{
{
  DEBUG_assert(ds);
  DEBUG_assert(free_space);

  if (ds->len < 0)
    return (-1);
  if (ds->alloc - ds->len >= free_space)
    return (0); // done
  ds->alloc += free_space;
  char *tmp = realloc(ds->buf, ds->alloc + 1);
  if (!tmp)
  {
    ds->len = -1;
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    DEBUG_assert(0);
    return (-1);
  }
  ds->buf = tmp;
  return (0);
}
// }}}


static int
dyn_vprintf(__cf_fontembed_dyn_string_t *ds,
	    const char *fmt,
	    va_list ap) // {{{
{
  DEBUG_assert(ds);

  int need, len = strlen(fmt) + 100;
  va_list va;

  if (__cfFontEmbedDynEnsure(ds, len) == -1)
    return (-1);

  while (1)
  {
    va_copy(va, ap);
    need = vsnprintf(ds->buf + ds->len, ds->alloc-ds->len + 1, fmt, va);
    va_end(va);
    if (need == -1)
      len += 100;
    else if (need >= len)
      len = need;
    else {
      ds->len += need;
      break;
    }
    if (__cfFontEmbedDynEnsure(ds, len) == -1)
      return (-1);
  }
  return (0);
}
// }}}

int
__cfFontEmbedDynPrintF(__cf_fontembed_dyn_string_t *ds,
		       const char *fmt,
		       ...) // {{{
{
  va_list va;
  int ret;

  va_start(va, fmt);
  ret = dyn_vprintf(ds, fmt, va);
  va_end(va);

  return (ret);
}
// }}}

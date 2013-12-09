#include "dynstring.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int dyn_init(DYN_STRING *ds,int reserve_size) // {{{
{
  assert(ds);
  assert(reserve_size>0);

  ds->len=0;
  ds->alloc=reserve_size;
  ds->buf=malloc(ds->alloc+1);
  if (!ds->buf) {
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
    assert(0);
    ds->len=-1;
    return -1;
  }
  return 0;
}
// }}}

void dyn_free(DYN_STRING *ds) // {{{
{
  assert(ds);

  ds->len=-1;
  ds->alloc=0;
  free(ds->buf);
  ds->buf=NULL;
}
// }}}

int dyn_ensure(DYN_STRING *ds,int free_space) // {{{
{
  assert(ds);
  assert(free_space);

  if (ds->len<0) {
    return -1;
  }
  if (ds->alloc - ds->len >= free_space) {
    return 0; // done
  }
  ds->alloc+=free_space;
  char *tmp=realloc(ds->buf,ds->alloc+1);
  if (!tmp) {
    ds->len=-1;
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
    assert(0);
    return -1;
  }
  ds->buf=tmp;
  return 0;
}
// }}}

int dyn_vprintf(DYN_STRING *ds,const char *fmt,va_list ap) // {{{
{
  assert(ds);

  int need,len=strlen(fmt)+100;
  va_list va;

  if (dyn_ensure(ds,len)==-1) {
    return -1;
  }

  while (1) {
    va_copy(va,ap);
    need=vsnprintf(ds->buf+ds->len,ds->alloc-ds->len+1,fmt,va);
    va_end(va);
    if (need==-1) {
      len+=100;
    } else if (need>=len) {
      len=need;
    } else {
      ds->len+=need;
      break;
    }
    if (dyn_ensure(ds,len)==-1) {
      return -1;
    }
  }
  return 0;
}
// }}}

int dyn_printf(DYN_STRING *ds,const char *fmt,...) // {{{
{
  va_list va;
  int ret;

  va_start(va,fmt);
  ret=dyn_vprintf(ds,fmt,va);
  va_end(va);

  return ret;
}
// }}}


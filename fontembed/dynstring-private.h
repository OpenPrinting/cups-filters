#ifndef _DYNSTRING_H
#define _DYNSTRING_H

typedef struct {
  int len,alloc;
  char *buf;
} DYN_STRING;

int dyn_init(DYN_STRING *ds,int reserve_size); // -1 on error
void dyn_free(DYN_STRING *ds);
int dyn_ensure(DYN_STRING *ds,int free_space);
int dyn_printf(DYN_STRING *ds,const char *fmt,...) // appends
  __attribute__((format(printf, 2, 3)));

#endif


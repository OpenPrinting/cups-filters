//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _FONTEMBED_DYNSTRING_H_
#define _FONTEMBED_DYNSTRING_H_

typedef struct
{
  int len, alloc;
  char *buf;
} __cf_fontembed_dyn_string_t;

int __cfFontEmbedDynInit(__cf_fontembed_dyn_string_t *ds,
			 int reserve_size); // -1 on error
void __cfFontEmbedDynFree(__cf_fontembed_dyn_string_t *ds);
int __cfFontEmbedDynEnsure(__cf_fontembed_dyn_string_t *ds, int free_space);
int __cfFontEmbedDynPrintF(__cf_fontembed_dyn_string_t *ds,
			   const char *fmt, ...) // appends
  __attribute__((format(printf, 2, 3)));

#endif // !_FONTEMBED_DYNSTRING_H_

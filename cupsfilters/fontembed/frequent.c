//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "frequent-private.h"
#include <cupsfilters/debug-internal.h>
#include <stdlib.h>


// misra-gries
// http://www2.research.att.com/~marioh/papers/vldb08-2.pdf

struct __cf_fontembed_frequent_s
{
  int size, czero;
  char sorted;
  struct
  {
    intptr_t key;
    int count,
        zero;
  } pair[];
};


// size is the precision/return size: in sequence with n _add(),
// it will find at most >size elements with occurence > n / (size + 1) times

__cf_fontembed_frequent_t *
__cfFontEmbedFrequentNew(int size) // {{{ - just free() it
{
  DEBUG_assert(size>0);
  __cf_fontembed_frequent_t *ret =
    malloc(sizeof(ret[0]) + sizeof(ret->pair[0]) * size);
  if (!ret)
    return (NULL);
  ret->size = size;
  ret->czero = 0;
  ret->sorted = 1;
  int iA;
  for (iA = 0; iA < size; iA ++)
  {
    ret->pair[iA].key = INTPTR_MIN;
    ret->pair[iA].count = 0;
    ret->pair[iA].zero = 0;
  }

  return (ret);
}
// }}}


void
__cfFontEmbedFrequentAdd(__cf_fontembed_frequent_t *freq,
			 intptr_t key) // {{{
{
  DEBUG_assert(freq);
  int iA, zero = -1;
  for (iA = freq->size - 1; iA >= 0; iA --)
  {
    if (freq->pair[iA].key == key)
    {
      freq->pair[iA].count ++;
      freq->sorted = 0;
      return;
    }
    else if (freq->pair[iA].count == freq->czero)
      zero = iA;
  }
  if (zero >= 0) // insert into set
  {
    freq->pair[zero].key = key;
    freq->pair[zero].count ++; // i.e. czero + 1
    freq->pair[zero].zero = freq->czero;
    // if it was sorted, the free entries are at the end. zero points to the
    // first free entry, because of the loop direction
  }
  else // out-of-set count
    freq->czero ++;
}
// }}}


static int
frequent_cmp(const void *a, const void *b) // {{{
{
  const typeof(((__cf_fontembed_frequent_t *)0)->pair[0]) *aa = a;
  const typeof(((__cf_fontembed_frequent_t *)0)->pair[0]) *bb = b;
  return ((bb->count - bb->zero) - (aa->count - aa->zero));
}
// }}}


// true frequency is somewhere between (count-zero) and count

intptr_t
__cfFontEmbedFrequentGet(__cf_fontembed_frequent_t *freq,
			 int pos) // {{{
{
  DEBUG_assert(freq);
  if (!freq->sorted)
  {
    // sort by (count-zero)
    qsort(freq->pair, freq->size, sizeof(freq->pair[0]), frequent_cmp);
    freq->sorted = 1;
  }
  if ((pos < 0) || (pos >= freq->size))
    return (INTPTR_MIN);
  return (freq->pair[pos].key);
}
// }}}

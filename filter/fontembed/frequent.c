#include "frequent.h"
#include <assert.h>
#include <stdlib.h>

// misra-gries
// http://www2.research.att.com/~marioh/papers/vldb08-2.pdf

struct _FREQUENT {
  int size,czero;
  char sorted;
  struct { intptr_t key; int count,zero; } pair[];
};

// size is the precision/return size: in sequence with n _add(), it will find at most >size elements with occurence > n/(size+1) times
FREQUENT *frequent_new(int size) // {{{ - just free() it
{
  assert(size>0);
  FREQUENT *ret=malloc(sizeof(ret[0])+sizeof(ret->pair[0])*size);
  if (!ret) {
    return NULL;
  }
  ret->size=size;
  ret->czero=0;
  ret->sorted=1;
  int iA;
  for (iA=0;iA<size;iA++) {
    ret->pair[iA].key=INTPTR_MIN;
    ret->pair[iA].count=0;
    ret->pair[iA].zero=0;
  }

  return ret;
}
// }}}

void frequent_add(FREQUENT *freq,intptr_t key) // {{{
{
  assert(freq);
  int iA,zero=-1;
  for (iA=freq->size-1;iA>=0;iA--) {
    if (freq->pair[iA].key==key) {
      freq->pair[iA].count++;
      freq->sorted=0;
      return;
    } else if (freq->pair[iA].count==freq->czero) {
      zero=iA;
    }
  }
  if (zero>=0) { // insert into set
    freq->pair[zero].key=key;
    freq->pair[zero].count++; // i.e. czero+1
    freq->pair[zero].zero=freq->czero;
    // if it was sorted, the free entries are at the end. zero points to the first free entry, because of the loop direction
  } else { // out-of-set count
    freq->czero++;
  }
}
// }}}

static int frequent_cmp(const void *a,const void *b) // {{{
{
  const typeof(((FREQUENT *)0)->pair[0]) *aa=a;
  const typeof(((FREQUENT *)0)->pair[0]) *bb=b;
  return (bb->count-bb->zero)-(aa->count-aa->zero);
}
// }}}

// true frequency is somewhere between (count-zero) and count
intptr_t frequent_get(FREQUENT *freq,int pos) // {{{
{
  assert(freq);
  if (!freq->sorted) {
    // sort by (count-zero)
    qsort(freq->pair,freq->size,sizeof(freq->pair[0]),frequent_cmp);
    freq->sorted=1;
  }
  if ( (pos<0)||(pos>=freq->size) ) {
    return INTPTR_MIN;
  }
  return freq->pair[pos].key;
}
// }}}


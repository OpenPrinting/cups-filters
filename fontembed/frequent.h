#ifndef _FREQUENT_H
#define _FREQUENT_H

#include <stdint.h>

typedef struct _FREQUENT FREQUENT;

// size is the precision/return size: it will find at most >size elements (i.e. all, if there) with frequency > 1/(size+1)
FREQUENT *frequent_new(int size); // - just free() it

void frequent_add(FREQUENT *freq,intptr_t key);

// might return INTPTR_MIN, if not populated
// this is only an approximation!
intptr_t frequent_get(FREQUENT *freq,int pos); // 0 is "most frequent"

#endif

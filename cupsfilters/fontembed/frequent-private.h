//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _FONTEMBED_FREQUENT_H_
#define _FONTEMBED_FREQUENT_H_

#include <stdint.h>

typedef struct __cf_fontembed_frequent_s __cf_fontembed_frequent_t;

// size is the precision/return size: it will find at most >size
// elements (i.e. all, if there) with frequency > 1 / (size + 1)
__cf_fontembed_frequent_t *__cfFontEmbedFrequentNew(int size);
                                                       // - just free() it

void __cfFontEmbedFrequentAdd(__cf_fontembed_frequent_t *freq, intptr_t key);

// might return INTPTR_MIN, if not populated
// this is only an approximation!
intptr_t __cfFontEmbedFrequentGet(__cf_fontembed_frequent_t *freq, int pos);
                                                       // 0 is "most frequent"

#endif // !_FONTEMBED_FREQUENT_H_

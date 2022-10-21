//
// Character conversion table generator for imageubrltoindexv[34]
//
// Copyright (c) 2015 Samuel Thibault <samuel.thibault@ens-lyon.org>
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <locale.h>
#include <pthread.h>
#include <stdio.h>

int
main(int argc,
     char *argv[])
{
  int i, j, lo, hi;
  setlocale(LC_ALL, "");
  for (i = 0; i < 256; i ++)
  {
    j = (i & 0x7) | ((i & 0x8) << 3) | ((i & 0x70) >> 1) | (i & 0x80);
    lo = i & 0xf;
    hi = (i & 0xf0) >> 4;
    printf("	-e 's/%lc/%c%c/g' \\\n", 0x2800 + j, '@' + lo, '@' + hi);
  }
  return (0);
}

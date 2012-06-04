/*

Copyright (c) 2006-2007, BBR Inc.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/
/*
 P2POutputStream.h
 pdftopdf stream for output
*/
#ifndef _P2POUTPUTSTREAM_H_
#define _P2POUTPUTSTREAM_H_

#include "goo/gtypes.h"

#include <config.h>
#ifndef HAVE_ZLIB_H
#undef HAVE_LIBZ
#endif
#include <stdio.h>
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

class P2POutputStream {
public:
  P2POutputStream(FILE *fpA);
  ~P2POutputStream();
  int write(const void *buf, int n);
  int printf(const char *format, ...);
  int putchar(char c);
  int puts(const char *str);
  int getPosition() { return position; }
  GBool canDeflate() {
#ifdef HAVE_LIBZ
    return gTrue;
#else
    return gFalse;
#endif
  }
  void startDeflate();
  void endDeflate();
private:
  FILE *fp;
  /* output position */
  int position;
#ifdef HAVE_LIBZ
  z_stream zstream;
  GBool deflating;
#endif
};

#endif

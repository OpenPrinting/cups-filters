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
 P2POutputStream.cc
 pdftopdf stream for output
*/

#include <config.h>
#include "goo/gmem.h"
#include "P2POutputStream.h"
#include <stdarg.h>
#include <string.h>
#include "P2PError.h"

#define LINEBUFSIZE 1024


/* Constructor */
P2POutputStream::P2POutputStream(FILE *fpA)
{
  fp = fpA;
  position = 0;
  deflating = gFalse;
}

P2POutputStream::~P2POutputStream()
{
}

int P2POutputStream::puts(const char *str)
{
  return write(str,strlen(str));
}

int P2POutputStream::putchar(char c)
{
  int r;

  if ((r = write(&c,1)) < 0) return r;
  return c & 0xff;
}

int P2POutputStream::printf(const char *format,...)
{
  char linebuf[LINEBUFSIZE];
  va_list ap;
  int r;

  va_start(ap,format);
  r = vsnprintf(linebuf,LINEBUFSIZE,format,ap);
  va_end(ap);
  if (r > 0) {
    if (puts(linebuf) < 0) {
      return EOF;
    }
  }
  return r;
}

int P2POutputStream::write(const void *buf, int n)
{
  int r;
  if (n <= 0) return 0;
#ifdef HAVE_LIBZ
  char outputBuf[LINEBUFSIZE*2];

  if (deflating) {
    zstream.next_in = const_cast<Bytef *>(static_cast<const Bytef *>(buf));
    zstream.avail_in = n;
    do {
      int outputLen;

      zstream.next_out = reinterpret_cast<Bytef *>(outputBuf);
      zstream.avail_out = LINEBUFSIZE*2;
      if ((r = deflate(&zstream,0)) != Z_OK) {
	p2pError(-1,const_cast<char *>("ZLIB:deflate error"));
	return r;
      }
      /* Z_OK */
      /* output data */
      outputLen = LINEBUFSIZE*2-zstream.avail_out;
      if ((r = fwrite(outputBuf,1,outputLen,fp)) != outputLen) {
	p2pError(-1,const_cast<char *>("P2POutputStream: write  error"));
	break;
      }
      position += r;
    } while (zstream.avail_in > 0);
    return n;
  } else {
#endif
    if ((r = fwrite(buf,1,n,fp)) != n) {
      p2pError(-1,const_cast<char *>("P2POutputStream: write  error"));
      return r;
    }
    position += r;
    return r;
#ifdef HAVE_LIBZ
  }
#endif
}

/* define alloc functions */
namespace {
  voidpf gmalloc_zalloc(voidpf opaque, uInt items, uInt size)
  {
    return gmalloc(items*size);
  }
  void gmalloc_zfree(voidpf opqaue, voidpf address)
  {
    gfree(address);
  }
};

void P2POutputStream::startDeflate()
{
#ifdef HAVE_LIBZ
  zstream.zalloc = gmalloc_zalloc;
  zstream.zfree = gmalloc_zfree;
  zstream.opaque = 0;

  if (deflateInit(&zstream,Z_DEFAULT_COMPRESSION) != Z_OK) {
    p2pError(-1,const_cast<char *>("ZLIB:deflateInit error"));
    return;
  }
  deflating = gTrue;
#endif
}

void P2POutputStream::endDeflate()
{
#ifdef HAVE_LIBZ
  char outputBuf[LINEBUFSIZE*2];
  int r;

  if (!deflating) return;
  do {
    int n;
    int outputLen;

    zstream.next_out = reinterpret_cast<Bytef *>(outputBuf);
    zstream.avail_out = LINEBUFSIZE*2;
    zstream.next_in = 0;
    zstream.avail_in = 0;
    if ((r = deflate(&zstream,Z_FINISH)) != Z_STREAM_END && r != Z_OK) {
      p2pError(-1,const_cast<char *>("ZLIB:deflate error"));
      break;
    }
    /* Z_OK or Z_STREAM_END */
    /* output data */
    outputLen = LINEBUFSIZE*2-zstream.avail_out;
    if ((n = fwrite(outputBuf,1,outputLen,fp)) != outputLen) {
      p2pError(-1,const_cast<char *>("P2POutputStream: write  error"));
      break;
    }
    position += n;
  } while (r != Z_STREAM_END);
  if (deflateEnd(&zstream) != Z_OK) {
      p2pError(-1,const_cast<char *>("ZLIB:deflateEnd error"));
  }
  deflating = gFalse;
#endif
}

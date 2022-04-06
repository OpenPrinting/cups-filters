/*
Copyright (c) 2020, Vikrant Malik

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

MIT Open Source License  -  http://www.opensource.org/

*/



#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

#include <cups/raster.h>

unsigned char *cfConvertBits(unsigned char *src, unsigned char *dst,
			     unsigned int x, unsigned int y,
			     unsigned int cupsNumColors,unsigned int bits);
void cfWritePixel(unsigned char *dst, unsigned int plane, unsigned int pixeli,
		  unsigned char *pixelBuf, unsigned int cupsNumColors,
		  unsigned int bits, cups_order_t colororder);
unsigned char *cfReverseOneBitLine(unsigned char *src, unsigned char *dst,
				   unsigned int pixels, unsigned int size);
unsigned char *cfReverseOneBitLineSwap(unsigned char *src, unsigned char *dst,
				       unsigned int pixels, unsigned int size);
void *cfOneBitLine(unsigned char *src, unsigned char *dst, unsigned int width,
		   unsigned int row, int bi_level);
void *cfOneBitToGrayLine(unsigned char *src, unsigned char *dst,
			 unsigned int width);
unsigned char *cfRGB8toKCMYcm(unsigned char *src, unsigned char *dst,
			      unsigned int x, unsigned int y);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */


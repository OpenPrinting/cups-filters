//
// Copyright (c) 2020, Vikrant Malik
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_FILTERS_BITMAP_H_
#  define _CUPS_FILTERS_BITMAP_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus

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
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_BITMAP_H_

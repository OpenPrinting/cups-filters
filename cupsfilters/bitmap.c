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

#include "image.h"
#include <stdio.h>
#include <cups/raster.h>

unsigned int dither1[16][16] = {
  {0,128,32,160,8,136,40,168,2,130,34,162,10,138,42,170},
  {192,64,224,96,200,72,232,104,194,66,226,98,202,74,234,106},
  {48,176,16,144,56,184,24,152,50,178,18,146,58,186,26,154},
  {240,112,208,80,248,120,216,88,242,114,210,82,250,122,218,90},
  {12,140,44,172,4,132,36,164,14,142,46,174,6,134,38,166},
  {204,76,236,108,196,68,228,100,206,78,238,110,198,70,230,102},
  {60,188,28,156,52,180,20,148,62,190,30,158,54,182,22,150},
  {252,124,220,92,244,116,212,84,254,126,222,94,246,118,214,86},
  {3,131,35,163,11,139,43,171,1,129,33,161,9,137,41,169},
  {195,67,227,99,203,75,235,107,193,65,225,97,201,73,233,105},
  {51,179,19,147,59,187,27,155,49,177,17,145,57,185,25,153},
  {243,115,211,83,251,123,219,91,241,113,209,81,249,121,217,89},
  {15,143,47,175,7,135,39,167,13,141,45,173,5,133,37,165},
  {207,79,239,111,199,71,231,103,205,77,237,109,197,69,229,101},
  {63,191,31,159,55,183,23,151,61,189,29,157,53,181,21,149},
  {255,127,223,95,247,119,215,87,253,125,221,93,245,117,213,85}
};
unsigned int dither2[8][8] = {
  {0,32,8,40,2,34,10,42},
  {48,16,56,24,50,18,58,26},
  {12,44,4,36,14,46,6,38},
  {60,28,52,20,62,30,54,22},
  {3,35,11,43,1,33,9,41},
  {51,19,59,27,49,17,57,25},
  {15,47,7,39,13,45,5,37},
  {63,31,55,23,61,29,53,21}
};
unsigned int dither4[4][4] = {
  {0,8,2,10},
  {12,4,14,6},
  {3,11,1,9},
  {15,7,13,5}
};
unsigned char revTable[256] = {
  0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0,
  0x08,0x88,0x48,0xc8,0x28,0xa8,0x68,0xe8,0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8,
  0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,
  0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec,0x1c,0x9c,0x5c,0xdc,0x3c,0xbc,0x7c,0xfc,
  0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2,0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
  0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa,
  0x06,0x86,0x46,0xc6,0x26,0xa6,0x66,0xe6,0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6,
  0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,
  0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1,0x11,0x91,0x51,0xd1,0x31,0xb1,0x71,0xf1,
  0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9,0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
  0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5,
  0x0d,0x8d,0x4d,0xcd,0x2d,0xad,0x6d,0xed,0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd,
  0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,
  0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb,0x1b,0x9b,0x5b,0xdb,0x3b,0xbb,0x7b,0xfb,
  0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7,0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
  0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff
};

/*
 * 'cfConvertBits()' - Convert 8 bit raster data to bitspercolor raster data using
 *                   ordered dithering.
 */

unsigned char *                        /* O - Output string */
cfConvertBits(unsigned char *src,        /* I - Input string */
	    unsigned char *dst,        /* I - Destination string */
	    unsigned int x,            /* I - Column */
	    unsigned int y,            /* I - Row */
	    unsigned int cupsNumColors,/* I - Number of color components of output data */
	    unsigned int bitspercolor) /* I - Bitspercolor of output data */
{
  /* assumed that max number of colors is 4 */
  unsigned char c = 0;
  unsigned short s = 0;
  switch (bitspercolor) {
  case 1:
   if (cupsNumColors != 1) {
     for (unsigned int i = 0;i < cupsNumColors; i++) {
       c <<= 1;
       /* ordered dithering */
       if (src[i] > dither1[y & 0xf][x & 0xf]) {
         c |= 0x1;
       }
     }
     *dst = c;
   }
   else {
     return src; /*Do not convert bits if both bitspercolor and numcolors are 1*/
   }
   break;
  case 2:
   for (unsigned int i = 0;i < cupsNumColors;i++) {
     unsigned int d;
     c <<= 2;
     /* ordered dithering */
     d = src[i] + dither2[y & 0x7][x & 0x7];
     if (d > 255) d = 255;
     c |= d >> 6;
   }
   *dst = c;
   break;
  case 4:
   for (unsigned int i = 0;i < cupsNumColors;i++) {
     unsigned int d;
     s <<= 4;
     /* ordered dithering */
     d = src[i] + dither4[y & 0x3][x & 0x3];
     if (d > 255) d = 255;
     s |= d >> 4;
   }
   if (cupsNumColors < 3) {
     dst[0] = s;
   } else {
     dst[0] = s >> 8;
     dst[1] = s;
   }
   break;
  case 16:
   for (unsigned int i = 0;i < cupsNumColors;i++) {
     dst[i*2] = src[i];
     dst[i*2+1] = src[i];
   }
   break;
  case 8:
  case 0:
  default:
   return src;
   break;
  }
  return dst;
}

/*
 * 'cfWritePixel()' - Write a pixel from pixelBuf to dst based on color order.
 */

void                                  /* O - Exit status */
cfWritePixel(unsigned char *dst,        /* I - Destination string */
	   unsigned int plane,        /* I - Plane/Band */
	   unsigned int pixeli,       /* I - Pixel */
	   unsigned char *pixelBuf,   /* I - Input string */
	   unsigned int cupsNumColors,/* I - Number of color components of output data */
	   unsigned int bitspercolor, /* I - Bitspercolor of output data */
	   cups_order_t colororder)   /* I - Color Order of output data */
{
  unsigned int bo;
  unsigned char so;
  switch (colororder) {
  case CUPS_ORDER_PLANAR:
  case CUPS_ORDER_BANDED:
   if (cupsNumColors != 1) {
     switch (bitspercolor) {
     case 1:
       bo = pixeli & 0x7;
       so = cupsNumColors - plane - 1;
       if ((pixeli & 7) == 0) dst[pixeli/8] = 0;
       dst[pixeli/8] |= ((*pixelBuf >> so) & 1) << (7-bo);
      break;
     case 2:
       bo = (pixeli & 0x3)*2;
       so = (cupsNumColors - plane - 1)*2;
       if ((pixeli & 3) == 0) dst[pixeli/4] = 0;
       dst[pixeli/4] |= ((*pixelBuf >> so) & 3) << (6-bo);
      break;
     case 4:
       {
         unsigned short c = (pixelBuf[0] << 8) | pixelBuf[1];
         bo = (pixeli & 0x1)*4;
         so = (cupsNumColors - plane - 1)*4;
         if ((pixeli & 1) == 0) dst[pixeli/2] = 0;
         dst[pixeli/2] |= ((c >> so) & 0xf) << (4-bo);
       }
      break;
     case 8:
       dst[pixeli] = pixelBuf[plane];
      break;
     case 16:
     default:
       dst[pixeli*2] = pixelBuf[plane*2];
       dst[pixeli*2+1] = pixelBuf[plane*2+1];
      break;
     }
     break;
   }
  case CUPS_ORDER_CHUNKED:
  default:
    switch (bitspercolor)
    {
    case 1:
      switch (cupsNumColors) {
      case 1:
        {
          unsigned int bo = pixeli & 0x7;
          if ((pixeli & 7) == 0) dst[pixeli/8] = 0;
          dst[pixeli/8] |= *pixelBuf << (7-bo);
        }
        break;
      case 6:
        dst[pixeli] = *pixelBuf;
        break;
      case 3:
      case 4:
      default:
        {
          unsigned int qo = (pixeli & 0x1)*4;
          if ((pixeli & 1) == 0) dst[pixeli/2] = 0;
          dst[pixeli/2] |= *pixelBuf << (4-qo);
        }
        break;
      }
     break;
    case 2:
      switch (cupsNumColors) {
      case 1:
        {
          unsigned int bo = (pixeli & 0x3)*2;
          if ((pixeli & 3) == 0) dst[pixeli/4] = 0;
          dst[pixeli/4] |= *pixelBuf << (6-bo);
        }
        break;
      case 3:
      case 4:
      default:
        dst[pixeli] = *pixelBuf;
        break;
      }
     break;
    case 4:
      switch (cupsNumColors) {
      case 1:
        {
          unsigned int bo = (pixeli & 0x1)*4;
          if ((pixeli & 1) == 0) dst[pixeli/2] = 0;
          dst[pixeli/2] |= *pixelBuf << (4-bo);
        }
        break;
      case 3:
      case 4:
      default:
        dst[pixeli*2] = pixelBuf[0];
        dst[pixeli*2+1] = pixelBuf[1];
        break;
      }
     break;
    case 8:
      {
        unsigned char *dp = dst + pixeli*cupsNumColors;
        for (unsigned int i = 0;i < cupsNumColors;i++) {
          dp[i] = pixelBuf[i];
        }
      }
     break;
    case 16:
    default:
      {
        unsigned char *dp = dst + pixeli*cupsNumColors*2;
        for (unsigned int i = 0;i < cupsNumColors*2;i++) {
          dp[i] = pixelBuf[i];
        }
      }
     break;
    }
   break;
  }
}

/*
 * 'cfReverseOneBitLine()' - Reverse the order of pixels in one line of 1-bit raster data.
 */

unsigned char *                       /* O - Output string */
cfReverseOneBitLine(unsigned char *src, /* I - Input line */
		  unsigned char *dst, /* I - Destination string */
		  unsigned int pixels,/* I - Number of pixels */
		  unsigned int size)  /* I - Bytesperline */
{
  unsigned char *bp;
  unsigned char *dp;
  unsigned int npadbits = (size*8)-pixels;

  if (npadbits == 0) {
    bp = src+size-1;
    dp = dst;
    for (unsigned int j = 0;j < size;j++,bp--,dp++) {
      *dp = revTable[*bp];
    }
  } else {
    unsigned int pd,d;
    unsigned int sw;

    size = (pixels+7)/8;
    sw = (size*8)-pixels;
    bp = src+size-1;
    dp = dst;

    pd = *bp--;
    for (unsigned int j = 1;j < size;j++,bp--,dp++) {
      d = *bp;
      *dp = revTable[(((d << 8) | pd) >> sw) & 0xff];
      pd = d;
    }
    *dp = revTable[(pd >> sw) & 0xff];
  }
  return dst;
}


/*
 * 'cfReverseOneBitLineSwap()' - Reverse the order of pixels in one line of 1-bit raster data
 *                             and invert the colors.
 */

unsigned char *                           /* O - Output string */
cfReverseOneBitLineSwap(unsigned char *src, /* I - Input line */
		      unsigned char *dst, /* I - Destination string */
		      unsigned int pixels,/* I - Number of pixels */
		      unsigned int size)  /* I - Bytesperline */
{
  unsigned char *bp;
  unsigned char *dp;
  unsigned int npadbits = (size*8)-pixels;

  if (npadbits == 0) {
    bp = src+size-1;
    dp = dst;
    for (unsigned int j = 0;j < size;j++,bp--,dp++) {
      *dp = revTable[(unsigned char)(~*bp)];
    }
  } else {
    unsigned int pd,d;
    unsigned int sw;

    size = (pixels+7)/8;
    sw = (size*8)-pixels;
    bp = src+size-1;
    dp = dst;

    pd = *bp--;
    for (unsigned int j = 1;j < size;j++,bp--,dp++) {
      d = *bp;
      *dp = ~revTable[(((d << 8) | pd) >> sw) & 0xff];
      pd = d;
    }
    *dp = ~revTable[(pd >> sw) & 0xff];
  }
  return dst;
}

/*
 * 'cfOneBitLine()' - Convert one line of 8-bit raster data to 1-bit raster data using ordered dithering.
 */

void    			/* O - Output line */
cfOneBitLine(unsigned char *src,  /* I - Input line */
	   unsigned char *dst,  /* O - Destination line */
	   unsigned int width,  /* I - Width of raster image in pixels */
	   unsigned int row,    /* I - Current Row */
	   int bi_level)	/* I - Bi-level option */
{
  // If bi_level is true, do threshold dithering to produce black and white output
  // else, do ordered dithering.
  unsigned char t = 0;
  unsigned int threshold = 0;
  for(unsigned int w = 0; w < width; w+=8){
    t = 0;
    for (int k = 0; k < 8; k++) {
      t <<= 1;
      if (w + k < width) {
	if (bi_level)
	  threshold = 128;
        else
	  threshold = dither1[row & 0xf][(w + k) & 0xf];
        if (*src > threshold)
          t |= 0x1;
        src += 1;
      }
    }
    *dst = t;
    dst += 1;
  }
}

/*
 * 'cfOneBitToGrayLine()' - Convert one line of 1-bit raster data to 8-bit
 *                        raster data.
 */

void    			      /* O - Output line */
cfOneBitToGrayLine(unsigned char *src,  /* I - Input line */
		 unsigned char *dst,  /* O - Destination line */
		 unsigned int width)  /* I - Width of raster image in pixels */
{
  unsigned char mask = 0x80;
  for (unsigned int w = 0; w < width; w += 1) {
    if (mask == 0) {
      mask = 0x80;
      src ++;
    }
    *dst = (*src & mask) ? 0xff : 0;
    mask >>= 1;
    dst ++;
  }
}

/*
 * 'cfRGB8toKCMYcm()' - Convert one pixel of 8-bit RGB data to KCMYcm raster data.
 */

unsigned char *cfRGB8toKCMYcm(unsigned char *src,
			    unsigned char *dst,
			    unsigned int x,
			    unsigned int y)
{
  unsigned char cmyk[4];
  unsigned char c;
  unsigned char d;

  cfImageRGBToCMYK(src,cmyk,1);
  c = 0;
  d = dither1[y & 0xf][x & 0xf];
  /* K */
  if (cmyk[3] > d) {
    c |= 0x20;
  }
  /* C */
  if (cmyk[0] > d) {
    c |= 0x10;
  }
  /* M */
  if (cmyk[1] > d) {
    c |= 0x08;
  }
  /* Y */
  if (cmyk[2] > d) {
    c |= 0x04;
  }
  if (c == 0x18) { /* Blue */
    c = 0x11; /* cyan + light magenta */
  } else if (c == 0x14) { /* Green */
    c = 0x06; /* light cyan + yellow */
  }
  *dst = c;
  return dst;
}

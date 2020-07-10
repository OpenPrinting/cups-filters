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

unsigned char *convertbits(unsigned char *src, unsigned char *dst,
    unsigned int x, unsigned int y, unsigned int cupsNumColors, unsigned int bitspercolor)
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

void writepixel(unsigned char *dst,
    unsigned int plane, unsigned int pixeli, unsigned char *pixelBuf,
    unsigned int cupsNumColors, unsigned int bitspercolor, cups_order_t colororder)
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

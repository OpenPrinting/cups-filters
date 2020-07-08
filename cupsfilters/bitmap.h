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

/* Common routines for accessing the colord CMS framework */

unsigned char 	*convertBitsNoop			(unsigned char *src, unsigned char *dst,
    							unsigned int x, unsigned int y, unsigned int cupsNumColors);
unsigned char 	*convert8to1				(unsigned char *src, unsigned char *dst,
    							unsigned int x, unsigned int y, unsigned int cupsNumColors);
unsigned char 	*convert8to2				(unsigned char *src, unsigned char *dst,
    							unsigned int x, unsigned int y, unsigned int cupsNumColors);
unsigned char 	*convert8to4				(unsigned char *src, unsigned char *dst,
    							unsigned int x, unsigned int y, unsigned int cupsNumColors);
unsigned char 	*convert8to16				(unsigned char *src, unsigned char *dst,
    							unsigned int x, unsigned int y, unsigned int cupsNumColors);
void 		writePixel1				(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePlanePixel1			(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePixel2				(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePlanePixel2			(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePixel4				(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePlanePixel4			(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePixel8				(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePlanePixel8			(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePixel16				(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);
void 		writePlanePixel16			(unsigned char *dst, unsigned int plane, 
							unsigned int pixeli, unsigned char *pixelBuf, unsigned int cupsNumColors);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */


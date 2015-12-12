/*
  Copyright (c) 2015 Samuel Thibault <samuel.thibault@ens-lyon.org>
  j
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:


  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.


  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <locale.h>
#include <pthread.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	int i, j, lo, hi;
	setlocale(LC_ALL,"");
	for (i = 0; i < 256; i++)
	{
		j = (i&0x7) | ((i&0x8) << 3) | ((i&0x70) >> 1) | (i&0x80);
		lo = i & 0xf;
		hi = (i & 0xf0) >> 4;
		printf("	-e 's/%lc/%c%c/g' \\\n", 0x2800+j, '@'+lo, '@'+hi);
	}
	return 0;
}

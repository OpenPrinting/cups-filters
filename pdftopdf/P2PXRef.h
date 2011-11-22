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
 P2PXRef.h
 pdftopdf cross reference
*/
#ifndef _P2PXREF_H_
#define _P2PXREF_H_

#include "P2POutputStream.h"
#include "XRef.h"

class P2PObject;

class P2PXRef {
public:
  /* get object from objects table */
  static P2PObject *get(int num, int gen) {
    if (num >= objectsSize) return 0;
    return objects[num];
  }

  /* return number of objects */
  static int getNObjects() { return currentNum+1; }
  /* put object to table */
  static void put(P2PObject *obj);
  static void put(P2PObject *obj, P2POutputStream *str);
  /* remove object */
  static void remove(P2PObject *obj);

  static int output(P2POutputStream *str);

  /* output objects that are not output yet. */
  static void flush(P2POutputStream *str, XRef *xref);

  /* clean objects */
  static void clean();


private:
  /* objects table */
  static P2PObject **objects;
  /* objects table size */
  static int objectsSize;
  /* current object number */
  static int currentNum;
};

#endif

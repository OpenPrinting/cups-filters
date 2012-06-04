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
 P2POutput.h
 pdftopdf output pdf routines
*/
#ifndef _P2POUTPUT_H_
#define _P2POUTPUT_H_

#include "goo/gmem.h"
#include "Object.h"
#include "P2POutputStream.h"
#include "Dict.h"
#include "Stream.h"
#include "Array.h"
#include "Page.h"
#include "XRef.h"

class P2PObject;

namespace P2POutput {
  void outputDict(Dict *dict, const char **keys,
    P2PObject **objs, int len, P2POutputStream *str, XRef *xref);
  void outputDict(Dict *dict, P2POutputStream *str, XRef *xref);
  void outputArray(Array *array, P2POutputStream *str, XRef *xref, Dict *mapDict = 0);
  void outputStream(Stream *stream, P2POutputStream *str, XRef *xref);
  void outputRef(Ref *ref, P2POutputStream *str, XRef *xref);
  void outputString(const char *s, int len, P2POutputStream *str);
  void outputName(const char *name, P2POutputStream *str, Dict *mapDict = 0);
  void outputObject(Object *obj, P2POutputStream *str, XRef *xref, Dict *mapDict = 0);
};

#endif

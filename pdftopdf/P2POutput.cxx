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
 P2POutput.cc
 pdftopdf output pdf routines
*/

#include <config.h>
#include <string.h>
#include "goo/gmem.h"
#include "UGooString.h"
#include "P2POutput.h"
#include "P2PObject.h"
#include <ctype.h>
#include "P2PError.h"

void P2POutput::outputDict(Dict *dict, const char **keys,
  P2PObject **objs, int len, P2POutputStream *str, XRef *xref)
{
  int i;
  int n = dict->getLength();
  int j;

  str->puts("<<");
  for (i = 0;i < n;i++) {
#ifdef HAVE_UGOOSTRING_H
    char *key = dict->getKey(i)->getCString();
#else
    char *key = dict->getKey(i);
#endif
    Object obj;

    for (j = 0;j < len;j++) {
      if (strcmp(keys[j],key) == 0) {
	goto next_key;
      }
    }
    str->putchar('\n');
    outputName(key,str);
    str->putchar(' ');
    dict->getValNF(i,&obj);
    outputObject(&obj,str,xref);
    obj.free();
next_key:;
#ifdef HAVE_UGOOSTRING_H
    delete[] key;
#endif
  }
  for (j = 0;j < len;j++) {
    int num, gen;

    str->putchar('\n');
    outputName(keys[j],str);
    str->putchar(' ');
    objs[j]->getNum(&num,&gen);
    if (num > 0) {
      objs[j]->outputRef(str);
    } else {
      objs[j]->output(str,xref);
    }
  }
  str->puts("\n>>");
}

void P2POutput::outputDict(Dict *dict, P2POutputStream *str, XRef *xref)
{
  int i;
  int n = dict->getLength();

  str->puts("<< ");
  for (i = 0;i < n;i++) {
#ifdef HAVE_UGOOSTRING_H
    char *key = dict->getKey(i)->getCString();
#else
    char *key = dict->getKey(i);
#endif
    Object obj;

    outputName(key,str);
    str->putchar(' ');
    dict->getValNF(i,&obj);
    outputObject(&obj,str,xref);
    obj.free();
    str->putchar('\n');
#ifdef HAVE_UGOOSTRING_H
    delete[] key;
#endif
  }
  str->puts(">>");
}

void P2POutput::outputStream(Stream *stream, P2POutputStream *str, XRef *xref)
{
  Dict *dict;
  int len;
  int c;

#ifdef HAVE_GETUNDECODEDSTREAM
  stream = stream->getUndecodedStream();
#else
  stream = stream->getBaseStream();
#endif
  dict = stream->getDict();
  /* output dictionary part */
  outputDict(dict,str,xref);
  str->puts("\nstream\n");
  stream->reset();
  for (len = 0;(c = stream->getChar()) != EOF;len++) {
    str->putchar(c);
  }
  str->puts("endstream");
}

void P2POutput::outputRef(Ref *ref, P2POutputStream *str, XRef *xref)
{
  P2PObject *p2pobj;
  Object obj;

  if ((p2pobj = P2PObject::find(ref->num,ref->gen)) == 0) {
    obj.initRef(ref->num, ref->gen);
    p2pobj = new P2PObj(&obj, ref->num, ref->gen);
    obj.free();
  }
  P2PXRef::put(p2pobj);
  p2pobj->outputRef(str);
}

void P2POutput::outputArray(Array *array, P2POutputStream *str, XRef *xref,
  Dict *mapDict)
{
  int i;
  int n = array->getLength();

  str->puts("[ ");
  for (i = 0;i < n;i++) {
    Object obj;

    array->getNF(i,&obj);
    outputObject(&obj,str,xref,mapDict);
    obj.free();
    str->putchar('\n');
  }
  str->puts("]");
}

void P2POutput::outputName(const char *name, P2POutputStream *str, Dict *mapDict)
{
  const char *p;
#ifdef HAVE_UGOOSTRING_H
  UGooString nameStr(name);
#else
  char *nameStr = const_cast<char *>(name);
#endif
  Object obj;
  static const char *punctures = "()<>[]{}/%#";

  if (mapDict != 0 && mapDict->lookupNF(nameStr,&obj) != 0) {
    if (obj.isName()) {
      /* map name */
      name = obj.getName();
    }
  }
  str->putchar('/');
  for (p = name;*p != '\0';p++) {
    if (*p >= 33 && *p <= 126 && strchr(punctures,*p) == 0) {
      str->putchar(*p);
    } else {
      str->printf("#%02x",*p & 0xff);
    }
  }
  obj.free();
}

void P2POutput::outputString(const char *s, int len, P2POutputStream *str)
{
  const char *p = s;
  int i;

  str->putchar('(');
  for (i = 0;i < len;i++,p++) {
    switch (*p) {
    case '\n':
      str->puts("\\n");
      break;
    case '\r':
      str->puts("\\r");
      break;
    case '\t':
      str->puts("\\t");
      break;
    case '\b':
      str->puts("\\b");
      break;
    case '\f':
      str->puts("\\f");
      break;
    case '(':
      str->puts("\\(");
      break;
    case ')':
      str->puts("\\)");
      break;
    case '\\':
      str->puts("\\\\");
      break;
    default:
      if (isprint(*p)) {
	str->putchar(*p);
      } else {
	str->printf("\\%03o",*p & 0xff);
      }
      break;
    }
  }
  str->putchar(')');
}

void P2POutput::outputObject(Object *obj, P2POutputStream *str, XRef *xref,
  Dict *mapDict)
{
  switch (obj->getType()) {
  case objBool:
    if (obj->getBool()) {
      str->puts("true");
    } else {
      str->puts("false");
    }
    break;
  case objInt:
    str->printf("%d",obj->getInt());
    break;
  case objUint:
    str->printf("%u",obj->getUint());
    break;
  case objReal:
    str->printf("%f",obj->getReal());
    break;
  case objString:
    outputString(obj->getString()->getCString(),
      obj->getString()->getLength(), str);
    break;
  case objName:
    outputName(obj->getName(),str,mapDict);
    break;
  case objNull:
    str->puts("null");
    break;
  case objArray:
    outputArray(obj->getArray(),str,xref,mapDict);
    break;
  case objDict:
    outputDict(obj->getDict(),str,xref);
    break;
  case objStream:
    outputStream(obj->getStream(),str,xref);
    break;
  case objRef:
    {
      Ref ref = obj->getRef();

      outputRef(&ref,str,xref);
    }
    break;
  case objCmd:
    str->puts(obj->getCmd());
    break;
  case objError:
    str->puts("<error>");
    break;
  case objEOF:
    str->puts("<EOF>");
    break;
  case objNone:
    str->puts("<none>");
    break;
  }
}

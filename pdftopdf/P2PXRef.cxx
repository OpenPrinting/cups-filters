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
 P2PXRef.cc
 pdftopdf cross reference
*/

#include <config.h>
#include <string.h>
#include "goo/gmem.h"
#include "P2PXRef.h"
#include "P2PObject.h"
#include "P2PError.h"

/* increment size for object table */
#define INCSIZE 1024

P2PObject **P2PXRef::objects = 0;
int P2PXRef::objectsSize = 0;
int P2PXRef::currentNum = 1;

void P2PXRef::put(P2PObject *obj)
{
  int num, gen;

  obj->getNum(&num,&gen);
  if (num > 0) return; /* already registered */
  if (objectsSize == 0) {
    /* first time, alloc table */
    objects = new P2PObject *[INCSIZE];
    objectsSize = INCSIZE;
    memset(objects,0,objectsSize*sizeof(P2PObject *));
  } else if (++currentNum >= objectsSize) {
    /* enlarge table */
    P2PObject **oldp = objects;
    objectsSize += INCSIZE;
    objects = new P2PObject *[objectsSize];
    memcpy(objects,oldp,(objectsSize-INCSIZE)*sizeof(P2PObject *));
    delete[] oldp;
  }
  obj->setNum(currentNum,0);
  objects[currentNum] = obj;
}

void P2PXRef::put(P2PObject *obj, P2POutputStream *str)
{
  put(obj);
  obj->setOffset(str);
}

void P2PXRef::remove(P2PObject *obj)
{
  int num, gen;

  obj->getNum(&num,&gen);
  if (num > 0) {
    objects[num] = 0;
  }
}

void P2PXRef::flush(P2POutputStream *str, XRef *xref)
{
  int i;
  int n;
  GBool f;

  /* first phase */
  do {
    f = gFalse;
    n = getNObjects();
    for (i = 1;i < n;i++) {
      if (objects[i] != 0 && objects[i]->getOffset() < 0
         && !objects[i]->isSecondPhase()) {
	objects[i]->output(str,xref);
	f = gTrue;
      }
    }
  } while (f);
  /* second phase */
  do {
    f = gFalse;
    n = getNObjects();
    for (i = 1;i < n;i++) {
      if (objects[i] != 0 && objects[i]->getOffset() < 0) {
	objects[i]->output(str,xref);
	f = gTrue;
      }
    }
  } while (f);
}

int P2PXRef::output(P2POutputStream *str)
{
  int xrefOffset = str->getPosition();
  int i;
  int n = getNObjects();

  str->puts("xref\n");
  str->printf("0 %d\n", n);
  str->puts("0000000000 65535 f \n");
  for (i = 1;i < n;i++) {
    int num, gen;
    int offset;

    if (objects[i] == 0) {
      /* freed object */
      /* this situation is error. */
      /* but continue */
      str->puts("0000000000 00000 f \n");
      p2pError(-1,const_cast<char *>("freed object:%d found\n"),i);
    } else {
      objects[i]->getNum(&num,&gen);
      offset = objects[i]->getOffset();

      if (offset < 0) {
	/* not output yet. error */
	p2pError(-1,const_cast<char *>("not output object:%d found\n"),i);
      } else {
	str->printf("%010d %05d n \n",offset,gen);
      }
    }
  }
  return xrefOffset;
}

void P2PXRef::clean()
{
  int i;
  int n = getNObjects();

  for (i = 1;i < n;i++) {
    if (objects[i] != 0) {
      delete objects[i];
      objects[i] = 0;
    }
  }
}

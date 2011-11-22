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
 P2PObject.cc
 pdftopdf Object
*/

#include <config.h>
#include <string.h>
#include "goo/gmem.h"
#include "P2PObject.h"
#include "P2PXRef.h"

/* orginal number hash table size. should be 2^n */
#define ORGTABLESIZE 4096

P2PObject **P2PObject::orgTable = 0;

int P2PObject::hash(int orgNumA, int orgGenA)
{
  return orgNumA & (ORGTABLESIZE-1);
}

void P2PObject::put()
{
  if (orgTable == 0) {
    /* first time, alloc table */
    orgTable = new P2PObject *[ORGTABLESIZE];
    memset(orgTable,0,ORGTABLESIZE*sizeof(P2PObject *));
  }

  if (orgNum > 0 && orgGen >=0) {
    /* put this into hashtable */
    int hv = hash(orgNum, orgGen);
    P2PObject **p;

    for (p = &orgTable[hv];*p != 0;p = &((*p)->next));
    *p = this;
  }
}

void P2PObject::remove()
{
  if (orgNum > 0 && orgGen >=0) {
    /* remove this from hashtable */
    int hv = hash(orgNum, orgGen);
    P2PObject **p;

    for (p = &orgTable[hv];*p != 0;p = &((*p)->next)) {
      if (*p == this) {
	*p = next;
	next = 0;
	break;
      }
    }
  }
}

P2PObject *P2PObject::find(int orgNumA, int orgGenA)
{
  if (orgNumA > 0 && orgGenA >=0) {
    /* remove this from hashtable */
    int hv = hash(orgNumA, orgGenA);
    P2PObject *p;

    for (p = orgTable[hv];p != 0;p = p->next) {
      if (p->orgNum == orgNumA && p->orgGen == orgGenA) {
	return p;
      }
    }
  }
  return 0;
}

void P2PObject::outputBegin(P2POutputStream *str)
{
  int num, gen;

  P2PXRef::put(this,str);
  getNum(&num,&gen);
  str->printf("%d %d obj\n",num,gen);
}

void P2PObject::outputEnd(P2POutputStream *str)
{
  str->puts("endobj\n");
}

void P2PObject::outputRef(P2POutputStream *str)
{
  int num, gen;

  getNum(&num,&gen);
  str->printf("%d %d R",num,gen);
}

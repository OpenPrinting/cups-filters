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
 P2PObject.h
 pdftopdf object
*/
#ifndef _P2POBJECT_H_
#define _P2POBJECT_H_

#include "P2POutputStream.h"
#include "P2PXRef.h"
#include "Object.h"
#include "P2POutput.h"
#include "Error.h"
#include "XRef.h"

class P2PObject {
public:
  /* find object with original object number and generation */
  static P2PObject *find(int orgNumA, int orgGenA);

  P2PObject() {
    next = 0;
    offset = -1;
    orgNum = -1;
    orgGen = -1;
    num = -1;
    gen = -1;
    secondPhase = gFalse;
    put();
  }
  P2PObject(int orgNumA, int orgGenA) {
    next = 0;
    offset = -1;
    orgNum = orgNumA;
    orgGen = orgGenA;
    num = -1;
    gen = -1;
    secondPhase = gFalse;
    put();
  }

  virtual ~P2PObject() {
    remove();
    P2PXRef::remove(this);
  }
  void setOffset(int offsetA) {
    offset = offsetA;
  }
  void setOffset(P2POutputStream *str) {
    offset = str->getPosition();
  }
  int getOffset() { return offset; }

  void setNum(int numA, int genA) {
    num = numA;
    gen = genA;
  }

  void getNum(int *numA, int *genA) {
    *numA = num;
    *genA = gen;
  }

  /* output begin object */
  void outputBegin(P2POutputStream *str);
  /* output end object */
  void outputEnd(P2POutputStream *str);
  /* output reference */
  void outputRef(P2POutputStream *str);

  /* output as an object */
  virtual void output(P2POutputStream *str, XRef *xref) {
    error(-1,const_cast<char *>("Illegal output call of P2PObject:%d"),num);
  }

  GBool isSecondPhase() { return secondPhase; }
  void setSecondPhase(GBool v) { secondPhase = v; }

private:
  /* original object number hash table */
  static P2PObject **orgTable;
  /* hash function */
  static int hash(int num, int gen);

  /* put this into original object number table */
  void put();
  /* remove this from original object number able */
  void remove();

  /* original object number */
  int orgNum;
  /* original object generation */
  int orgGen;

  /* object number */
  int num;
  /* object generation */
  int gen;
  /* offset in output file */
  /* when minus value, not output this yet. */
  int offset;
  /* next object pointer for list */
  P2PObject *next;
  /* flag indicating that should be output in second phase */
  GBool secondPhase;
};

class P2PObj : public P2PObject {
public:
  P2PObj() {}
  P2PObj(Object *objA, int orgNumA = -1, int orgGenA = -1) 
     : P2PObject(orgNumA, orgGenA) {
    objA->copy(&obj);
  }

  virtual ~P2PObj() {
    obj.free();
  }

  virtual void output(P2POutputStream *str, XRef *xref) {
    if (getOffset() < 0) {
      int num, gen;

      getNum(&num,&gen);
      if (num > 0) outputBegin(str);
      if (obj.isRef()) {
        Object fobj;
        Ref ref;

        ref = obj.getRef();
        xref->fetch(ref.num,ref.gen,&fobj);
        P2POutput::outputObject(&fobj,str,xref);
        fobj.free();
      } else {
        P2POutput::outputObject(&obj,str,xref);
      }
      str->putchar('\n');
      if (num > 0) outputEnd(str);
    }
  }
  void setObj(Object *objA) {
    obj.free();
    objA->copy(&obj);
  }
  Object *getObj() {
    return &obj;
  }

private:
  Object obj;
};

#endif

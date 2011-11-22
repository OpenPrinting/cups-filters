/*

Copyright (c) 2006-2010, BBR Inc.  All rights reserved.

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
 P2PForm.h
 pdftopdf form
 Note: Only use this for Forms without Resource entry.
*/
#include "P2PDoc.h"
#include "P2PGfx.h"
#include "P2PXRef.h"
#include "P2PForm.h"

P2PForm::P2PForm(Object *orgFormA, P2PResources *resourcesA,
  P2PResourceMap *mapA)
{
  /* assumed orgFormA is Reference */
  orgFormA->copy(&orgForm);
  resources = resourcesA;
  mappingTable = mapA;
  P2PXRef::put(this);
}

P2PForm::~P2PForm()
{
  /* resources and mappingTable are deleted in other class.
     Don't delete them here */
  orgForm.free();
}

void P2PForm::output(P2POutputStream *str, XRef *xref)
{
  int i;
  int n;
  Dict *dict;
  int len;
  Object lenobj;
  P2PObj *pobj = new P2PObj();

  outputBegin(str);

  str->puts("<< /Length ");
  P2PXRef::put(pobj);
  pobj->outputRef(str);
  if (P2PDoc::options.contentsCompress && str->canDeflate()) {
    str->puts(" /Filter /FlateDecode ");
  }

  dict = orgForm.streamGetDict();
  n = dict->getLength();
  for (i = 0;i < n;i++) {
#ifdef HAVE_UGOOSTRING_H
    char *key = dict->getKey(i)->getCString();
#else
    char *key = dict->getKey(i);
#endif
    Object obj;

    if (strcmp(key,"Filter") == 0 || strcmp(key,"Length") == 0) continue;
    P2POutput::outputName(key,str);
    str->putchar(' ');
    dict->getValNF(i,&obj);
    P2POutput::outputObject(&obj,str,xref);
    obj.free();
    str->putchar('\n');
#ifdef HAVE_UGOOSTRING_H
    delete[] key;
#endif
  }
  str->puts("/Resources ");
  resources->output(str);
  str->puts(" >>\n");


  /* output contents */
  P2PGfx output(xref,str,resources->getP2PFontResource(),resources);
  int start;

  str->puts("stream\n");
  start = str->getPosition();
  if (P2PDoc::options.contentsCompress) str->startDeflate();

  P2PMatrix identMatrix;

  output.outputContents(&orgForm,mappingTable,0,&identMatrix);

  if (P2PDoc::options.contentsCompress) str->endDeflate();
  len = str->getPosition()-start;
  str->puts("\nendstream\n");

  outputEnd(str);

  /* set length object value */
  lenobj.initInt(len);
  pobj->setObj(&lenobj);
  lenobj.free();
  pobj->output(str,xref);
}

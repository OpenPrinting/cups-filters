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
 P2PResources.cc
 pdftopdf resources dictionary
*/

#include <config.h>
#include <string.h>
#include "goo/gmem.h"
#include "P2PResources.h"
#include "P2PFont.h"
#include "P2PForm.h"

P2PResourceMap::P2PResourceMap()
{
  int i;

  for (i = 0;i < P2PResources::NDict;i++) {
    tables[i] = 0;
  }
}

P2PResourceMap::~P2PResourceMap()
{
  int i;

  for (i = 0;i < P2PResources::NDict;i++) {
    if (tables[i] != 0) {
      delete tables[i];
    }
  }
}

const char *P2PResources::dictNames[NDict] = {
  "ExtGState",
  "ColorSpace",
  "Pattern",
  "Shading",
  "XObject",
  "Font"
};

P2PResources::P2PResources(XRef *xrefA)
{
  int i;

  xref = xrefA;
  for (i = 0;i < NDict;i++) {
    dictionaries[i] = 0;
  }
  resourceNo = 0;
  patternDict = 0;
  nPattern = 0;
  fontResource = 0;
  oldForms = 0;
  nOldForms = 0;
}

P2PResources::~P2PResources()
{
  int i;

  for (i = 0;i < NDict;i++) {
    if (dictionaries[i] != 0) {
      delete dictionaries[i];
    }
  }
  if (patternDict != 0) delete[] patternDict;
  if (oldForms != 0) delete[] oldForms;
  /* fontResource is deleted in other Class,
     Don't delete it */
}

void P2PResources::output(P2POutputStream *str)
{
  int i;
  str->puts("<<");

  for (i = 0;i < NDict;i++) {
    if (i == Font && fontResource != 0 && fontResource->getNDicts() > 0) {
      str->printf(" /%s ",dictNames[i]);
      fontResource->output(str,xref);
      str->putchar('\n');
    } else if (i == Pattern) {
      if (nPattern > 0) {
	int j;

	str->printf(" /%s << ",dictNames[i]);
	for (j = 0;j < nPattern;j++) {
	  if (patternDict[j].pattern != 0) {
	    /* output body later */
	    P2PXRef::put(patternDict[j].pattern);
	    /* output refrence here */
	    P2POutput::outputName(patternDict[j].name,str);
	    str->putchar(' ');
	    patternDict[j].pattern->outputRef(str);
	    str->putchar('\n');
	  }
	}
	str->puts(">>\n");
      }
    } else if (i == XObject && nOldForms > 0 && dictionaries[i] != 0) {
      int j;

      /* There is any old form in XObject dictionary */
      Dict *xobjDict = dictionaries[i];
      int n = xobjDict->getLength();
      str->printf(" /%s << ",dictNames[i]);
      for (j = 0;j < n;j++) {
        char *key = xobjDict->getKey(j);

        P2POutput::outputName(key,str);
        str->putchar(' ');
        if (j < nOldForms && oldForms[j] != 0) {
          oldForms[j]->outputRef(str);
        } else {
          Object obj;

          xobjDict->getValNF(j,&obj);
          P2POutput::outputObject(&obj,str,xref);
          obj.free();
        }
        str->putchar('\n');
      }
      str->puts(">>\n");
    } else {
      if (dictionaries[i] != 0) {
	str->printf(" /%s ",dictNames[i]);
	P2POutput::outputDict(dictionaries[i],str,xref);
	str->putchar('\n');
      }
    }
  }
  /* output ProcSet */ 
  /* Notice: constant values */
  str->puts(" /ProcSet [ /PDF /Text /ImageB /ImageC /ImageI ] ");

  /* Notice: no Properties */

  str->puts(">>");
}

void P2PResources::handleOldForm(P2PResourceMap *map)
{
  int n;
  int i;

  /* find Forms without Resources and replace them */
  Dict *xobjDict = dictionaries[XObject];
  if (xobjDict == 0) return;
  n = xobjDict->getLength();
  for (i = 0;i < n;i++) {
    Object xobj;
    Object obj;
    Dict *strDict;
    P2PForm *form;

    xobjDict->getVal(i,&xobj);
    if (!xobj.isStream()) {
        xobj.free();
        continue;
    }
    strDict = xobj.streamGetDict();
    strDict->lookupNF("Subtype",&obj);
    if (!obj.isName() || strcmp("Form",obj.getName()) != 0) {
        obj.free();
        xobj.free();
        continue;
    }
    obj.free();
    strDict->lookupNF("Resources",&obj);
    if (!obj.isNull()) {
        obj.free();
        xobj.free();
        continue;
    }
    /* found a Form without Resource,
      replace it with a refrence to a P2PForm */
    form = new P2PForm(&xobj,this,map);
    xobj.free();
    if (nOldForms < n) {
        P2PObject **oldp = oldForms;
        nOldForms = n;
        oldForms = new P2PObject *[n];
        if (oldp != 0) {
            memcpy(oldForms,oldp,n*sizeof(P2PForm *));
        } else {
            memset(oldForms,0,n*sizeof(P2PForm *));
        }
    }
    oldForms[i] = form;
  }
}

P2PResourceMap *P2PResources::merge(Dict *res)
{
  P2PResourceMap *map = 0;
  Object obj;
  int i;

  if (res == 0) return 0;
  for (i = 0;i < NDict;i++) {
    if (res->lookup(const_cast<char *>(dictNames[i]),&obj) != 0
         && obj.isDict()) {
      if (map == 0) {
	map = new P2PResourceMap();
      }
      if (dictionaries[i] == 0) {
	dictionaries[i] = new Dict(xref);
      }
      if (map->tables[i] == 0) {
	map->tables[i] = new Dict((XRef *)0);
      }
      mergeOneDict(dictionaries[i],obj.getDict(),map->tables[i],i != Pattern);
      obj.free();
    }
  }
  handleOldForm(map);
  return map;
}

P2PResourceMap *P2PResources::merge(P2PResources *res)
{
  P2PResourceMap *map = 0;
  int i;

  for (i = 0;i < NDict;i++) {
    if (res->dictionaries[i] != 0) {
      if (map == 0) {
	map = new P2PResourceMap();
      }
      if (dictionaries[i] == 0) {
	dictionaries[i] = new Dict(xref);
      }
      if (map->tables[i] == 0) {
	map->tables[i] = new Dict((XRef *)0);
      }
      mergeOneDict(dictionaries[i],res->dictionaries[i],map->tables[i],
        i != Pattern);
    }
  }
  handleOldForm(map);
  return map;
}

void P2PResources::mergeOneDict(Dict *dst, Dict *src, Dict *map, GBool unify)
{
  int i,j;
  int srcn = src->getLength();
  int dstn;

  for (i = 0;i < srcn;i++) {
    Object srcobj;
    GBool found = gFalse;
#ifdef HAVE_UGOOSTRING_H
    UGooString *srckey;
#else
    char *srckey;
#endif

    src->getValNF(i,&srcobj);
    srckey = src->getKey(i);
    if (srcobj.isRef() && unify) {
      dstn = dst->getLength();
      for (j = 0;j < dstn;j++) {
	Object dstobj;

	dst->getValNF(j,&dstobj);
	if (dstobj.isRef() && srcobj.getRefNum() == dstobj.getRefNum()
	  && srcobj.getRefGen() == dstobj.getRefGen()) {
	  /* add to map */
#ifdef HAVE_UGOOSTRING_H
	  addMap(srckey,dst->getKey(j)->getCString(),map);
#else
	  addMap(srckey,dst->getKey(j),map);
#endif
	  found = gTrue;
	}
	dstobj.free();
      }
    }
    if (!found) {
      /* add to dst */
      addDict(dst,&srcobj,srckey,map);
    } else {
      srcobj.free();
    }
  }
}

#ifdef HAVE_UGOOSTRING_H
void P2PResources::addDict(Dict *dict, Object *obj, 
  UGooString *srckey, Dict *map)
{
  char name[20];
  Object tobj;

  snprintf(name,20,"R%d",resourceNo++);
  dict->addOwnVal(name,obj);
  addMap(srckey,name,map);
}
#else
void P2PResources::addDict(Dict *dict, Object *obj, 
  char *srckey, Dict *map)
{
  char name[20];
  Object tobj;

  snprintf(name,20,"R%d",resourceNo++);
  dict->add(strdup(name),obj);
  addMap(srckey,name,map);
}
#endif

#ifdef HAVE_UGOOSTRING_H
void P2PResources::addMap(UGooString *org, char *mapped, Dict *map)
{
  Object obj;

  obj.initName(mapped);
  map->add(*org,&obj);
}
#else
void P2PResources::addMap(char *org, char *mapped, Dict *map)
{
  Object obj;

  obj.initName(mapped);
  map->add(strdup(org),&obj);
}
#endif

void P2PResources::setupPattern()
{
  int i;

  if (patternDict != NULL) {
    return;
  }
  if (dictionaries[Pattern] == 0) return;
  nPattern = dictionaries[Pattern]->getLength();
  patternDict = new P2PPatternDict[nPattern];
  /* setting names */
  for (i = 0;i < nPattern;i++) {
#ifdef HAVE_UGOOSTRING_H
    char *p = dictionaries[Pattern]->getKey(i)->getCString();
#else
    char *p = strdup(dictionaries[Pattern]->getKey(i));
#endif
    patternDict[i].name = p;
  }
}

void P2PResources::refPattern(char *name, P2PMatrix *matA)
{
  int i;

  if (dictionaries[Pattern] == 0) return; /* no pattern */
  for (i = 0;i < nPattern;i++) {
    if (strcmp(name,patternDict[i].name) == 0) {
      if (patternDict[i].pattern == 0) {
	Object patObj;

	dictionaries[Pattern]->lookupNF(name,&patObj);
	if (patObj.isRef() || patObj.isDict()) {
	  patternDict[i].pattern = new P2PPattern(&patObj,xref,matA);
	}
	patObj.free();
      }
    }
  }
}

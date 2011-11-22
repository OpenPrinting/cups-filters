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
#ifndef _P2PFORM_H_
#define _P2PFORM_H_

#include "Object.h"
#include "P2PObject.h"
#include "P2POutputStream.h"
#include "XRef.h"
#include "P2PResources.h"

class P2PForm : public P2PObject {
public:
  P2PForm(Object *orgFormA, P2PResources *resourcesA, P2PResourceMap *mapA);
  virtual ~P2PForm();
  void output(P2POutputStream *str, XRef *xref);
private:
  Object orgForm;
  P2PResources *resources;
  P2PResourceMap *mappingTable; /* resource name mapping table */
};

#endif

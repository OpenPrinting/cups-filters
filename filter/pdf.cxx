/*
 * Copyright 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pdf.h"

#include <PDFDoc.h>


extern "C" pdf_t * pdf_load_template(const char *filename)
{
    PDFDoc *doc = new PDFDoc(new GooString(filename));

    if (!doc->isOk()) {
        fprintf(stderr,
                "Error: unable to open template document '%s'\n",
                filename);
        delete doc;
        return NULL;
    }

    if (doc->getNumPages() != 1) {
        fprintf(stderr,
                "Error: template documents must contain exactly one page\n",
                filename);
        delete doc;
        return NULL;
    }

    return doc;
}


extern "C" void pdf_append_stream(pdf_t *doc,
                                  int page,
                                  char *buf,
                                  size_t len)
{
    XRef *xref = doc->getXRef();
    Ref *pageref = doc->getCatalog()->getPageRef(page);
    Object dict, lenobj, stream;
    Object pageobj, contents;

    xref->fetch(pageref->num, pageref->gen, &pageobj);
    if (!pageobj.isDict() || !pageobj.dictLookupNF("Contents", &contents)) {
        fprintf(stderr, "Error: malformed pdf\n");
        return;
    }

    if (contents.isRef())
        xref->fetch(contents.getRefNum(), contents.getRefGen(), &contents);

    lenobj.initInt(len);
    dict.initDict(xref);
    dict.dictSet("Length", &lenobj);
    stream.initStream(new MemStream(buf, 0, len, &dict));

    if (contents.isStream()) {
        Object array;
        array.initArray(xref);
        array.arrayAdd(&contents);
        array.arrayAdd(&stream);
        pageobj.dictSet("Contents", &array);
    }
    else if (contents.isArray())
        contents.arrayAdd(&stream);
    else
        fprintf(stderr, "Error: malformed pdf\n");

    xref->setModifiedObject(&pageobj, *pageref);
    pageobj.free();
}


static Object * name_object(const char *s)
{
    Object *o = new Object();
    o->initName((char *)s);
    return o;
}


static Object * get_resource_dict(XRef *xref,
                                  Dict *pagedict,
                                  Object *resdict,
                                  Ref *resref)
{
    Object res;

    /* TODO resource dict can also be inherited */
    if (!pagedict->lookupNF("Resources", &res))
        return NULL;

    if (res.isRef()) {
        *resref = res.getRef();
        xref->fetch(resref->num, resref->gen, resdict);
    }
    else if (res.isDict()) {
        res.copy(resdict);
        resref->num = 0;
    }
    else
        resdict = NULL;

    res.free();
    return resdict;
}


extern "C" void pdf_add_type1_font(pdf_t *doc,
                                   int page,
                                   const char *name)
{
    XRef *xref = doc->getXRef();
    Ref *pageref = doc->getCatalog()->getPageRef(page);
    Object pageobj, font, fonts;

    Object resdict;
    Ref resref;

    xref->fetch(pageref->num, pageref->gen, &pageobj);
    if (!pageobj.isDict()) {
        fprintf(stderr, "Error: malformed pdf\n");
        return;
    }

    if (!get_resource_dict(xref, pageobj.getDict(), &resdict, &resref)) {
        fprintf(stderr, "Error: malformed pdf\n");
        pageobj.free();
        return;
    }

    font.initDict(xref);
    font.dictSet("Type", name_object("Font"));
    font.dictSet("Subtype", name_object("Type1"));
    font.dictSet("BaseFont", name_object(name));
    xref->addIndirectObject(&font);

    resdict.dictLookup("Font", &fonts);
    if (fonts.isNull()) {
        fonts.initDict(xref);
        resdict.dictSet("Font", &fonts);
    }
    fonts.dictSet("bannertopdf-font", &font);

    if (resref.num == 0)
        xref->setModifiedObject(&pageobj, *pageref);
    else
        xref->setModifiedObject(&resdict, resref);

    pageobj.free();
}


extern "C" void pdf_write(pdf_t *doc,
                          FILE *file)
{
    FileOutStream outs(file, 0);
    doc->saveAs(&outs, writeForceRewrite);
}


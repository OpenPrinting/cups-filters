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


extern "C" void pdf_prepend_stream(pdf_t *doc,
                                   int page,
                                   char *buf,
                                   size_t len)
{
    XRef *xref = doc->getXRef();
    Ref *pageref = doc->getCatalog()->getPageRef(page);
    Object dict, lenobj, stream;
    Object pageobj, contents;
    Object array;

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

    array.initArray(xref);
    array.arrayAdd(&stream);

    if (contents.isStream()) {
        array.arrayAdd(&contents);
    }
    else if (contents.isArray()) {
        int i, len = contents.arrayGetLength();
        Object obj;
        for (i = 0; i < len; i++) {
            contents.arrayGet(i, &obj);
            array.arrayAdd(&obj);
        }
    }
    else
        fprintf(stderr, "Error: malformed pdf\n");

    pageobj.dictSet("Contents", &array);

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


static bool dict_lookup_rect(Object *dict,
                             char *key,
                             float rect[4])
{
    Object o;
    Array *array;
    int i;

    if (!dict->dictLookup(key, &o))
        return false;

    if (!o.isArray()) {
        o.free();
        return false;
    }

    array = o.getArray();
    for (i = 0; i < 4; i++) {
        Object el;
        if (array->get(i, &el) && el.isNum())
            rect[i] = el.getNum();
        el.free();
    }

    o.free();
    return i == 4;
}


static void dict_set_rect(XRef *xref,
                          Object *dict,
                          char *key,
                          float rect[4])
{
    Object array;
    int i;

    array.initArray(xref);

    for (i = 0; i < 4; i++) {
        Object el;
        el.initReal(rect[i]);
        array.arrayAdd(&el);
    }

    dict->dictSet(key, &array);
}


static void fit_rect(float oldrect[4],
                     float newrect[4],
                     float *scale)
{
    float oldwidth = oldrect[2] - oldrect[0];
    float oldheight = oldrect[3] - oldrect[1];
    float newwidth = newrect[2] - newrect[0];
    float newheight = newrect[3] - newrect[1];

    *scale = newwidth / oldwidth;
    if (oldheight * *scale > newheight)
        *scale = newheight / oldheight;
}


extern "C" void pdf_resize_page (pdf_t *doc,
                                 int page,
                                 float width,
                                 float length,
                                 float *scale)
{
    XRef *xref = doc->getXRef();
    Ref *pageref = doc->getCatalog()->getPageRef(page);
    Object pageobj;
    float mediabox[4] = { 0.0, 0.0, width, length };
    float old_mediabox[4];

    xref->fetch(pageref->num, pageref->gen, &pageobj);
    if (!pageobj.isDict()) {
        fprintf(stderr, "Error: malformed pdf\n");
        return;
    }

    if (!dict_lookup_rect (&pageobj, "MediaBox", old_mediabox)) {
        fprintf(stderr, "Error: pdf doesn't contain a valid mediabox\n");
        return;
    }

    fit_rect(old_mediabox, mediabox, scale);

    dict_set_rect (xref, &pageobj, "MediaBox", mediabox);
    dict_set_rect (xref, &pageobj, "CropBox", mediabox);
    dict_set_rect (xref, &pageobj, "TrimBox", mediabox);
    dict_set_rect (xref, &pageobj, "ArtBox", mediabox);
    dict_set_rect (xref, &pageobj, "BleedBox", mediabox);

    xref->setModifiedObject(&pageobj, *pageref);
    pageobj.free();
}


extern "C" void pdf_write(pdf_t *doc,
                          FILE *file)
{
    FileOutStream outs(file, 0);
    doc->saveAs(&outs, writeForceRewrite);
}


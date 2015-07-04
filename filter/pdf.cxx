/*
 * Copyright 2012 Canonical Ltd.
 * Copyright 2013 ALT Linux, Andrew V. Stepanov <stanv@altlinux.com>
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

#include <config.h>
#include "pdf.h"

#include <PDFDoc.h>
#include <GlobalParams.h>
#include <Form.h>
#include <Gfx.h>
#include <GfxFont.h>
#include <Page.h>
#include <PDFDocEncoding.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif

extern "C" {
#include <embed.h>
#include <sfnt.h>
}

#include <fontconfig/fontconfig.h>

/*
 * Useful reference:
 *
 * http://www.gnupdf.org/Indirect_Object
 * http://www.gnupdf.org/Introduction_to_PDF
 * http://blog.idrsolutions.com/2011/05/understanding-the-pdf-file-format-%E2%80%93-pdf-xref-tables-explained
 * http://labs.appligent.com/pdfblog/pdf-hello-world/
*/

static EMB_PARAMS *get_font(const char *font);

static const char *emb_pdf_escape_name(const char *name, int len);

static int utf8_to_utf16(const char *utf8, unsigned short **out_ptr);

static const char* get_next_wide(const char *utf8, int *unicode_out);

extern "C" {
    static int pdf_embed_font(
            pdf_t *doc,
            Page *page,
            const char *typeface);
}

static void fill_font_stream(
        const char *buf,
        int len,
        void *context);

static Object *make_fontdescriptor_dic(pdf_t *doc,
        EMB_PARAMS *emb,
        EMB_PDF_FONTDESCR *fdes,
        Ref fontfile_obj_ref);

static Object *make_font_dic(pdf_t *doc,
        EMB_PARAMS *emb,
        EMB_PDF_FONTDESCR *fdes,
        EMB_PDF_FONTWIDTHS *fwid,
        Ref fontdescriptor_obj_ref);

static Object *make_cidfont_dic(pdf_t *doc,
        EMB_PARAMS *emb,
        const char *fontname,
        Ref fontdescriptor_obj_ref);

/* Font to use to fill form */
static EMB_PARAMS *Font;

extern "C" pdf_t * pdf_load_template(const char *filename)
{
    /* Init poppler */
    globalParams = new GlobalParams();

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
                "Error: PDF template must contain exactly 1 page: %s\n",
                filename);
        delete doc;
        return NULL;
    }

    return doc;
}


extern "C" void pdf_free(pdf_t *pdf)
{
    delete pdf;
}


extern "C" void pdf_prepend_stream(pdf_t *doc,
                                   int page,
                                   char *buf,
                                   size_t len)
{
    XRef *xref = doc->getXRef();
    Ref *pageref = doc->getCatalog()->getPageRef(page);
    Object dict, lenobj, stream, streamrefobj;
    Object pageobj, contents;
    Object array;
    Ref r;

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

    r = xref->addIndirectObject(&stream);
    streamrefobj.initRef(r.num, r.gen);

    array.initArray(xref);
    array.arrayAdd(&streamrefobj);

    if (contents.isStream()) {
        pageobj.dictLookupNF("Contents", &contents); // streams must be indirect, i.e. not fetch()-ed
        array.arrayAdd(&contents);
    }
    else if (contents.isArray()) {
        int i, len = contents.arrayGetLength();
        Object obj;
        for (i = 0; i < len; i++) {
            contents.arrayGetNF(i, &obj);
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

/*
 * Create new PDF integer type object.
 */
static Object * int_object(int i)
{
    Object *o = new Object();
    o->initInt(i);
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

    resdict.dictLookupNF("Font", &fonts);
    if (fonts.isNull()) {
        /* Create new font dic obj in page's resources */
        fonts.initDict(xref);
        resdict.dictSet("Font", &fonts);
    }

    Object *fonts_dic;
    Object dereferenced_obj;

    if ( fonts.isDict() ) {
        /* "Font" resource is dictionary object */
        fonts_dic = &fonts;
    } else if ( fonts.isRef() ) {
        /* "Font" resource is indirect reference object */
        xref->fetch(fonts.getRefNum(), fonts.getRefGen(), &dereferenced_obj);
        fonts_dic = &dereferenced_obj;
    }

    if ( ! fonts_dic->isDict() ) {
        fprintf(stderr, "Can't recognize Font resource in PDF template.\n");
        return;
    }

    /* Add new entry to "Font" resource */
    fonts_dic->dictSet("bannertopdf-font", &font);

    /* Notify poppler about changes */
    if ( fonts.isRef() ) {
        xref->setModifiedObject(fonts_dic, fonts.getRef());
    }

    if (resref.num == 0)
        xref->setModifiedObject(&pageobj, *pageref);
    else
        xref->setModifiedObject(&resdict, resref);

    pageobj.free();
}


static bool dict_lookup_rect(Object *dict,
                             const char *key,
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
                          const char *key,
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


extern "C" void pdf_duplicate_page (pdf_t *doc,
                                    int pagenr,
                                    int count)
{
    XRef *xref = doc->getXRef();
    Ref *pageref = doc->getCatalog()->getPageRef(pagenr);
    Object page, parentref, parent, kids, ref, countobj;
    int i;

    xref->fetch(pageref->num, pageref->gen, &page);
    if (!page.isDict("Page")) {
        fprintf(stderr, "Error: malformed pdf (invalid Page object)\n");
        return;
    }

    page.dictLookupNF("Parent", &parentref);
    parentref.fetch(xref, &parent);
    if (!parent.isDict("Pages")) {
        fprintf(stderr, "Error: malformed pdf (Page.Parent must point to a "
                        "Pages object)\n");
        return;
    }

    parent.dictLookup("Kids", &kids);
    if (!kids.isArray()) {
        fprintf(stderr, "Error: malformed pdf (Pages.Kids must be an array)\n");
        return;
    }

    // Since we're dealing with single page pdfs, simply append the same page
    // object to the end of the array
    // Note: We must make a (shallow) copy of the page object to avoid loops in
    // the pages tree (not supported by major pdf implementations).
    for (i = 1; i < count; i++) {
        Ref r = xref->addIndirectObject(&page);
        ref.initRef(r.num, r.gen);
        kids.arrayAdd(&ref);
        ref.free();
    }

    countobj.initInt(count);
    parent.dictSet("Count", &countobj);
    countobj.free();

    xref->setModifiedObject(&parent, parentref.getRef());
}


class NonSeekableFileOutStream: public OutStream
{
public:
    NonSeekableFileOutStream(FILE *f):
        file(f), pos(0)
    {
    }

    void close()
    {
    }

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
    Goffset getPos()
#else
    int getPos()
#endif
    {
        return this->pos;
    }

    void put(char c)
    {
        fputc(c, this->file);
        this->pos++;
    }

    void printf(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        this->pos += vfprintf(this->file, fmt, args);
        va_end(args);
    }

private:
    FILE *file;
    int pos;
};


extern "C" void pdf_write(pdf_t *doc,
                          FILE *file)
{
    NonSeekableFileOutStream outs(file);
    doc->saveAs(&outs, writeForceRewrite);
}

/*
 * Get value according to key.
 */
const char *lookup_opt(opt_t *opt, const char *key) {
    if ( ! opt || ! key ) {
        return NULL;
    }

    while (opt) {
        if (opt->key && opt->val) {
            if ( strcmp(opt->key, key) == 0 ) {
                return opt->val;
            }
        }
        opt = opt->next;
    }

    return NULL;
}

/*
 * 1. Lookup in PDF template file for form.
 * 2. Lookup for form fields' names.
 * 3. Fill recognized fields with information.
 */
extern "C" int pdf_fill_form(pdf_t *doc, opt_t *opt)
{
    XRef *xref = doc->getXRef();
    Catalog *catalog = doc->getCatalog();
    Catalog::FormType form_type = catalog->getFormType();
    if ( form_type == Catalog::NoForm ) {
        fprintf(stderr, "PDF template file doesn't have form. It's okay.\n");
        return 0;
    }

    Page *page = catalog->getPage(1);
    if ( !page ) {
        fprintf(stderr, "Can't get page from PDF tamplate file.\n");
        return 0;
    }
    Object pageobj;
    Ref pageref = page->getRef();
    xref->fetch(pageref.num, pageref.gen, &pageobj);

    const char *font_size = lookup_opt(opt, "banner-font-size");
    if ( ! font_size ) {
        /* Font size isn't specified use next one. */
        font_size = "14";
    }

    /* Embed font into PDF */
    const char *font = lookup_opt(opt, "banner-font");
    if ( ! font ) {
        /* Font isn't specified use next one. */
        font = "FreeMono";
    }
    int res = pdf_embed_font(doc, page, font);
    if ( ! res ) {
        fprintf(stderr, "Can't integrate %s font into PDF file.\n", font);
        return 0;
    }

    /* Page's resources dictionary */
    Object resdict;
    Ref resref;
    Object *ret = get_resource_dict(xref, pageobj.getDict(), &resdict, &resref);

    FormPageWidgets *widgets = page->getFormWidgets();
    if ( !widgets ) {
        fprintf(stderr, "Can't get page's widgets.\n");
        return 0;
    }
    int num_widgets = widgets->getNumWidgets();

    /* Go through widgets and fill them as necessary */
    for (int i=0; i < num_widgets; ++i)
    {
        FormWidget *fm = widgets->getWidget(i);

        /* Take into consideration only Text widgets */
        if ( fm->getType() != formText ) {
            continue;
        }

        FormWidgetText *fm_text = static_cast<FormWidgetText*>(fm);

        /* Ignore R/O widget */
        if ( fm_text->isReadOnly() ) {
            continue;
        }

        FormField *ff = fm_text->getField();
        GooString *field_name;
        field_name = ff->getFullyQualifiedName();
        if ( ! field_name )
            field_name = ff->getPartialName();
        if ( ! field_name ) {
            fprintf(stderr, "Ignore widget #%d (unknown name)\n", i);
            continue;
        }

        const char *name = field_name->getCString();
        const char *fill_with = lookup_opt(opt, name);
        if ( ! fill_with ) {
            fprintf(stderr, "Lack information for widget: %s.\n", name);
            fill_with = "N/A";
        }

        fprintf(stderr, "Fill widget name %s with value %s.\n", name, fill_with);

        unsigned short *fill_with_w;
        int len = utf8_to_utf16(fill_with, &fill_with_w);
        if ( !len ) {
            fprintf(stderr, "Bad data for widget: %s.\n", name);
            continue;
        }

        GooString *content = new GooString((char*)fill_with_w, len);
        fm_text->setContent(content);

        /* Object for form field */
        Object *field_obj = ff->getObj();
        Ref field_ref = ff->getRef();

        /* Construct appearance object in form of: "/stanv_font 12 Tf" */
        GooString *appearance = new GooString();
        appearance->append("/stanv_font ");
        appearance->append(font_size);
        appearance->append(" Tf");

        /* Modify field's appearance */
        Object appearance_obj;
        appearance_obj.initString(appearance);
        field_obj->getDict()->set("DA", &appearance_obj);

        /*
         * Create /AP - entry stuff.
         * This is right way to display characters other then Latin1
         */

        /* UTF8 '/0' ending string */
        const char *ptr_text = fill_with;

        GooString *ap_text = new GooString("<");
        while ( *ptr_text ) {
            int unicode;
            /* Get next character in Unicode */
            ptr_text = get_next_wide(ptr_text, &unicode);
            const unsigned short gid = emb_get(Font, unicode);
            char text[5];
            memset(text, 0, sizeof(text));
            sprintf(text,"%04x", gid);
            ap_text->append(text, 4);
        }
        ap_text->append("> Tj\n");

        /* Create empty string for stream */
        GooString *appearance_stream = new GooString();

        /* Orde has matter */
        appearance_stream->append("/Tx BMC \n");
        appearance_stream->append("BT\n");	// Begin text object
        appearance_stream->append("/stanv_font ");
        appearance_stream->append(font_size);
        appearance_stream->append(" Tf\n");
        appearance_stream->append("2 12.763 Td\n");
        appearance_stream->append(ap_text);
        appearance_stream->append("ET\n");
        appearance_stream->append("EMC\n");

        Object appearance_stream_dic;
        appearance_stream_dic.initDict(xref);

        /*
         * Appearance stream dic.
         * See: 4.9 Form XObjects
         * TABLE 4.41 Additional entries specific to a type 1 form dictionary
         */
        appearance_stream_dic.dictSet("Type", name_object("XObject"));
        appearance_stream_dic.dictSet("Subtype", name_object("Form"));
        appearance_stream_dic.dictSet("FormType", int_object(1));
        Object obj_ref_x;
        obj_ref_x.initRef(resref.num, resref.gen);
        appearance_stream_dic.dictSet("Resources", &obj_ref_x);

        /* BBox array: TODO. currently out of the head. */
        Object array;
        array.initArray(xref);
        Object el;
        el.initReal(0);
        array.arrayAdd(&el);
        el.initReal(0);
        array.arrayAdd(&el);
        el.initReal(237);
        array.arrayAdd(&el);
        el.initReal(25);
        array.arrayAdd(&el);
        appearance_stream_dic.dictSet("BBox", &array);
        appearance_stream_dic.dictSet("Length", int_object(appearance_stream->getLength()));

        MemStream *mem_stream = new MemStream(appearance_stream->getCString(),
                0, appearance_stream->getLength(), &appearance_stream_dic);

        /* Make obj stream */
        Object stream;
        stream.initStream(mem_stream);

        Ref r;
        r = xref->addIndirectObject(&stream);

        /* Update Xref table */
        Object obj_ref;
        obj_ref.initRef(r.num, r.gen);

        /* 
         * Fill Annotation's appearance streams dic /AP
         * See: 8.4.4 Appearance Streams
         */
        Object appearance_streams_dic;
        appearance_streams_dic.initDict(xref);
        appearance_streams_dic.dictSet("N", &obj_ref);

        field_obj->getDict()->set("AP", &appearance_streams_dic);

        /* Notify poppler about changes */
        xref->setModifiedObject(field_obj, field_ref);
    }

    /*
     * Adjust form's NeedAppearances flag.
     * We need to fill form's fields with specified font.
     * The right way to this is via /AP.
     *
     * false - is default value for PDF. See:
     * PDFReference.pdf - table 8.47 Entries in the interactive form dictionary
     *
     * OpenOffice - by default sets it to 'true'.
     */
    Object *obj_form = catalog->getAcroForm();
    Object obj1;
    obj1.initBool(gFalse);
    obj_form->dictSet("NeedAppearances", &obj1);
    /* Add AccroForm as indirect obj */
    Ref ref_form = xref->addIndirectObject(obj_form);

    /*
     * So update Catalog object.
     */
    Object* catObj = new Object();
    catObj = xref->getCatalog(catObj);
    Ref catRef;
    catRef.gen = xref->getRootGen();
    catRef.num = xref->getRootNum();
    Object obj2;
    obj2.initRef(ref_form.num, ref_form.gen);
    catObj->dictSet("AcroForm", &obj2);
    xref->setModifiedObject(catObj, catRef);

    /* Success */
    return 1;
}

/* Embeded font into PDF */
static int pdf_embed_font(pdf_t *doc,
        Page *page,
        const char *typeface) {

    /* Load font using libfontconfig */
    Font = get_font(typeface);
    if ( ! Font ) {
        fprintf(stderr, "Can't load font: %s\n", typeface);
        return 0;
    }

    /* Font's description */
    EMB_PDF_FONTDESCR *Fdes = emb_pdf_fontdescr(Font);
    if ( ! Fdes ) {
        return 0;
    }

    /* Font's widths description */
    EMB_PDF_FONTWIDTHS *Fwid=emb_pdf_fontwidths(Font);
    if ( ! Fwid ) {
        return 0;
    }

    /* Create empty string for stream */
    GooString *font_stream = new GooString();

    /* Fill stream */
    const int outlen = emb_embed(Font, fill_font_stream, font_stream);
    assert( font_stream->getLength() == outlen );

    /* Get XREF table */
    XRef *xref = doc->getXRef();

    /* Font dictionary object for embeded font */
    Object f_dic;
    f_dic.initDict(xref);
    f_dic.dictSet("Type", name_object("Font"));

    /* Stream lenght */
    f_dic.dictSet("Length", int_object(outlen));

    /* Lenght for EMB_FMT_TTF font type */
    if ( Font->outtype == EMB_FMT_TTF ) {
        f_dic.dictSet("Length1", int_object(outlen));
    }

    /* Add font subtype */
    const char *subtype = emb_pdf_get_fontfile_subtype(Font);
    if ( subtype ) {
        f_dic.dictSet("Subtype", name_object(copyString(subtype)));
    }

    /* Create memory stream font. Add it to font dic. */
    MemStream *mem_stream = new MemStream(font_stream->getCString(),
            0, outlen, &f_dic);

    /* Make obj stream */
    Object stream;
    stream.initStream(mem_stream);

    Ref r;

    /* Update Xref table */
    r = xref->addIndirectObject(&stream);

    /* Get page object */
    Object pageobj;
    Ref pageref = page->getRef();
    xref->fetch(pageref.num, pageref.gen, &pageobj);
    if (!pageobj.isDict()) {
        fprintf(stderr, "Error: malformed pdf.\n");
        return 0;
    }

    /* Page's resources dictionary */
    Object resdict;
    Ref resref;
    Object *ret = get_resource_dict(xref, pageobj.getDict(), &resdict, &resref);
    if ( !ret ) {
        fprintf(stderr, "Error: malformed pdf\n");
        pageobj.free();
        return 0;
    }

    /* Dictionary for all fonts in page's resources */
    Object fonts;

    resdict.dictLookupNF("Font", &fonts);
    if (fonts.isNull()) {
        /* Create new one, if doesn't exists */
        fonts.initDict(xref);
        resdict.dictSet("Font", &fonts);
        fprintf(stderr, "Create new font dict in page's resources.\n");
    }

    /*
     * For embeded font there are 4 inderect objects and 4 reference obj.
     * Each next point to previsious one.
     * Last one object goes to Font dic.
     *
     * 1. Font stream obj + reference obj
     * 2. FontDescriptor obj + reference obj
     * 3. Width resource obj + reference obj
     * 4. Multibyte resourcse obj + reference obj
     *
     */

    /* r - indirect object refrence to dict with stream */
    Object *font_desc_resource_dic = make_fontdescriptor_dic(doc, Font, Fdes, r);
    r = xref->addIndirectObject(font_desc_resource_dic);

    /* r - indirect object reference to dict font descriptor resource */
    Object *font_resource_dic = make_font_dic(doc, Font, Fdes, Fwid, r);
    r = xref->addIndirectObject(font_resource_dic);

    /* r - widths resource dic */
    Object *cidfont_resource_dic = make_cidfont_dic(doc, Font, Fdes->fontname, r);
    r = xref->addIndirectObject(cidfont_resource_dic);

    /* r - cid resource dic */
    Object font_res_obj_ref;
    font_res_obj_ref.initRef(r.num, r.gen);

    Object *fonts_dic;
    Object dereferenced_obj;

    if ( fonts.isDict() ) {
        /* "Font" resource is dictionary object */
        fonts_dic = &fonts;
    } else if ( fonts.isRef() ) {
        /* "Font" resource is indirect reference object */
        xref->fetch(fonts.getRefNum(), fonts.getRefGen(), &dereferenced_obj);
        fonts_dic = &dereferenced_obj;
    }

    if ( ! fonts_dic->isDict() ) {
        fprintf(stderr, "Can't recognize Font resource in PDF template.\n");
        return 0;
    }

    /* Add to fonts dic new font */
    fonts_dic->dictSet("stanv_font", &font_res_obj_ref);

    /* Notify poppler about changes in fonts dic */
    if ( fonts.isRef() ) {
        xref->setModifiedObject(fonts_dic, fonts.getRef());
    }

    /* Notify poppler about changes in resources dic */
    xref->setModifiedObject(&resdict, resref);
    fprintf(stderr, "Resource dict was changed.\n");

    pageobj.free();

    /* Success */
    return 1;
}

/*
 * Call-back function to fill font stream object.
 */
static void fill_font_stream(const char *buf, int len, void *context)
{
    GooString *s = (GooString *)context;
    s->append(buf, len);
}

/*
 * Use libfontconfig to pick up suitable font.
 * Memory should be freed.
 * XXX: doesn't work correctly. Need to do some revise.
 */
static char *get_font_libfontconfig(const char *font) {
    FcPattern *pattern = NULL;
    FcFontSet *candidates = NULL;
    FcChar8* found_font = NULL;
    FcResult result;

    FcInit ();
    pattern = FcNameParse ((const FcChar8 *)font);

    /* guide fc, in case substitution becomes necessary */
    FcPatternAddInteger (pattern, FC_SPACING, FC_MONO);
    FcConfigSubstitute (0, pattern, FcMatchPattern);
    FcDefaultSubstitute (pattern);

    /* Receive a sorted list of fonts matching our pattern */
    candidates = FcFontSort (0, pattern, FcFalse, 0, &result);
    FcPatternDestroy (pattern);

    /* In the list of fonts returned by FcFontSort()
       find the first one that is both in TrueType format and monospaced */
    for (int i = 0; i < candidates->nfont; i++) {

        /* TODO? or just try? */
        FcChar8 *fontformat=NULL;

        /* sane default, as FC_MONO == 100 */
        int spacing=0;
        FcPatternGetString(
                candidates->fonts[i],
                FC_FONTFORMAT,
                0,
                &fontformat);

        FcPatternGetInteger(
                candidates->fonts[i],
                FC_SPACING,
                0,
                &spacing);

        if ( (fontformat) && (spacing == FC_MONO) ) {
            if (strcmp((const char *)fontformat, "TrueType") == 0) {
                found_font = FcPatternFormat (
                        candidates->fonts[i],
                        (const FcChar8 *)"%{file|cescape}/%{index}");

                /* Don't take into consideration remain candidates */
                break;
            } else if (strcmp((const char *)fontformat, "CFF") == 0) {

                /* TTC only possible with non-cff glyphs! */
                found_font = FcPatternFormat (
                        candidates->fonts[i],
                        (const FcChar8 *)"%{file|cescape}");

                /* Don't take into consideration remain candidates */
                break;
            }
        }
    }

    FcFontSetDestroy (candidates);

    if ( ! found_font ) {
        fprintf(stderr,"No viable font found\n");
        return NULL;
    }

    return (char *)found_font;
}

/*
 * Load font file using fontembed file.
 * Test for requirements.
 */
static EMB_PARAMS *load_font(const char *font) {
    assert(font);

    OTF_FILE *otf;
    otf = otf_load(font);
    if ( ! otf ) {
        return NULL;
    }

    FONTFILE *ff=fontfile_open_sfnt(otf);
    if ( ! ff ) {
        return NULL;
    }

    EMB_PARAMS *emb=emb_new(ff,
            EMB_DEST_PDF16,
            static_cast<EMB_CONSTRAINTS>( /* bad fontembed */
                EMB_C_FORCE_MULTIBYTE|
                EMB_C_TAKE_FONTFILE|
                EMB_C_NEVER_SUBSET));
    if ( ! emb ) {
        return NULL;
    }

    if ( ! (emb->plan & EMB_A_MULTIBYTE) ) {
        return NULL;
    }

    EMB_PDF_FONTDESCR *fdes=emb_pdf_fontdescr(emb);
    if ( ! fdes ) {
        return NULL;
    }

    /* Success */
    return emb;
}

/*
 * Return fontembed library object that corresponds requirements.
 */
static EMB_PARAMS *get_font(const char *font)
{
    assert(font);

    char *fontname = NULL;
    EMB_PARAMS *emb = NULL;

    /* Font file specified. */
    if ( (font[0]=='/') || (font[0]=='.') ) {
        fontname = strdup(font);
        emb = load_font(fontname);
    }

    /* Use libfontconfig. */
    if ( ! emb ) {
        fontname = get_font_libfontconfig(font);
        emb = load_font(fontname);
    }

    free(fontname);

    return emb;
}

/*
 * Was taken from ./fontembed/embed_pdf.c
 */
static const char *emb_pdf_escape_name(const char *name, int len)
{
    assert(name);
    if (len==-1) {
        len=strlen(name);
    }

    /* PDF implementation limit */
    assert(len<=127);

    static char buf[128*3];
    int iA,iB;
    const char hex[]="0123456789abcdef";

    for (iA=0,iB=0;iA<len;iA++,iB++) {
        if ( ((unsigned char)name[iA]<33)||((unsigned char)name[iA]>126)||
                (strchr("#()<>[]{}/%",name[iA])) ) {
            buf[iB]='#';
            buf[++iB]=hex[(name[iA]>>4)&0x0f];
            buf[++iB]=hex[name[iA]&0xf];
        } else {
            buf[iB]=name[iA];
        }
    }
    buf[iB]=0;
    return buf;
}

/*
 * Construct font description dictionary.
 * Translated to Poppler function emb_pdf_simple_fontdescr() from
 * cups-filters/fontembed/embed_pdf.c
 */
static Object *make_fontdescriptor_dic(
        pdf_t *doc,
        EMB_PARAMS *emb,
        EMB_PDF_FONTDESCR *fdes,
        Ref fontfile_obj_ref)
{
    assert(emb);
    assert(fdes);

    /* Get XREF table */
    XRef *xref = doc->getXRef();

    /* Font dictionary for embeded font */
    Object *dic = new Object();
    dic->initDict(xref);

    dic->dictSet("Type", name_object("FontDescriptor"));
    dic->dictSet(
            "FontName",
            name_object(copyString(emb_pdf_escape_name(fdes->fontname,-1))));
    dic->dictSet("Flags", int_object(fdes->flags));
    dic->dictSet("ItalicAngle", int_object(fdes->italicAngle));
    dic->dictSet("Ascent", int_object(fdes->ascent));
    dic->dictSet("Descent", int_object(fdes->descent));
    dic->dictSet("CapHeight", int_object(fdes->capHeight));
    dic->dictSet("StemV", int_object(fdes->stemV));

    /* FontBox array */
    Object array;
    array.initArray(xref);

    Object el;

    el.initReal(fdes->bbxmin);
    array.arrayAdd(&el);

    el.initReal(fdes->bbymin);
    array.arrayAdd(&el);

    el.initReal(fdes->bbxmax);
    array.arrayAdd(&el);

    el.initReal(fdes->bbymax);
    array.arrayAdd(&el);

    dic->dictSet("FontBBox", &array);

    if (fdes->xHeight) {
        dic->dictSet("XHeight", int_object(fdes->xHeight));
    }

    if (fdes->avgWidth) {
        dic->dictSet("AvgWidth", int_object(fdes->avgWidth));
    }

    if (fdes->panose) {
        /* Font dictionary for embeded font */
        Object style_dic;
        style_dic.initDict(xref);

        Object panose;

        GooString *panose_str = new GooString(fdes->panose, 12);
        panose.initString(panose_str);
        style_dic.dictSet("Panose", &panose);

        dic->dictSet("Style", &style_dic);
    }

    Object ref_obj;
    ref_obj.initRef(fontfile_obj_ref.num, fontfile_obj_ref.gen);
    dic->dictSet(emb_pdf_get_fontfile_key(emb), &ref_obj);

    return dic;
}

static Object *make_font_dic(
        pdf_t *doc,
        EMB_PARAMS *emb,
        EMB_PDF_FONTDESCR *fdes,
        EMB_PDF_FONTWIDTHS *fwid,
        Ref fontdescriptor_obj_ref)
{
    assert(emb);
    assert(fdes);
    assert(fwid);

    /* Get XREF table */
    XRef *xref = doc->getXRef();

    Object *dic = new Object();
    dic->initDict(xref);

    dic->dictSet("Type", name_object("Font"));
    dic->dictSet(
            "Subtype",
            name_object(copyString(emb_pdf_get_font_subtype(emb))));
    dic->dictSet(
            "BaseFont",
            name_object(copyString(emb_pdf_escape_name(fdes->fontname,-1))));

    Object ref_obj;
    ref_obj.initRef(fontdescriptor_obj_ref.num, fontdescriptor_obj_ref.gen);
    dic->dictSet("FontDescriptor", &ref_obj);

    if ( emb->plan & EMB_A_MULTIBYTE ) {
        assert(fwid->warray);

        Object CIDSystemInfo_dic;
        CIDSystemInfo_dic.initDict(xref);

        Object registry;
        Object ordering;

        GooString *str;

        str = new GooString(copyString(fdes->registry));
        registry.initString(str);
        CIDSystemInfo_dic.dictSet("Registry", &registry);

        str = new GooString(copyString(fdes->ordering));
        ordering.initString(str);
        CIDSystemInfo_dic.dictSet("Ordering", &ordering);

        CIDSystemInfo_dic.dictSet("Supplement", int_object(fdes->supplement));

        dic->dictSet("CIDSystemInfo", &CIDSystemInfo_dic);

        dic->dictSet("DW", int_object(fwid->default_width));
    }

    return dic;
}


static Object *make_cidfont_dic(
        pdf_t *doc,
        EMB_PARAMS *emb,
        const char *fontname,
        Ref fontdescriptor_obj_ref)
{
    assert(emb);
    assert(fontname);

    /*
     * For CFF: one of:
     * UniGB-UCS2-H, UniCNS-UCS2-H, UniJIS-UCS2-H, UniKS-UCS2-H
     */
    const char *encoding="Identity-H";
    const char *addenc="-";

    if ( emb->outtype == EMB_FMT_TTF ) { // !=CidType0
        addenc="";
    }

    /* Get XREF table */
    XRef *xref = doc->getXRef();

    Object *dic = new Object();
    dic->initDict(xref);

    dic->dictSet("Type", name_object("Font"));
    dic->dictSet("Subtype", name_object("Type0"));


    GooString * basefont = new GooString();
    basefont->append(emb_pdf_escape_name(fontname,-1));
    basefont->append(addenc);
    basefont->append((addenc[0])?encoding:"");

    dic->dictSet("BaseFont",
            name_object(copyString(basefont->getCString())));

    dic->dictSet("Encoding", name_object(copyString(encoding)));

    Object obj;
    obj.initRef(fontdescriptor_obj_ref.num, fontdescriptor_obj_ref.gen);

    Object array;
    array.initArray(xref);
    array.arrayAdd(&obj);

    dic->dictSet("DescendantFonts", &array);

    return dic;
}


/*
 * Convert UTF8 to UTF16.
 * Version for poppler - UTF16BE.
 *
 * Reference:
 * http://stackoverflow.com/questions/7153935/how-to-convert-utf-8-stdstring-to-utf-16-stdwstring
 */
static int utf8_to_utf16(const char *utf8, unsigned short **out_ptr)
{
    unsigned long *unicode, *head;

    int strl = strlen(utf8);

    unicode = head = (unsigned long*) malloc(strl * sizeof(unsigned long));

    if ( ! head ) {
        fprintf(stderr,"stanv: 1\n");
        return 0;
    }

    while (*utf8){
        unsigned long uni;
        size_t todo;
        unsigned char ch = *utf8;

        if (ch <= 0x7F)
        {
            uni = ch;
            todo = 0;
        }
        else if (ch <= 0xBF)
        {
            /* not a UTF-8 string */
            return 0;
        }
        else if (ch <= 0xDF)
        {
            uni = ch&0x1F;
            todo = 1;
        }
        else if (ch <= 0xEF)
        {
            uni = ch&0x0F;
            todo = 2;
        }
        else if (ch <= 0xF7)
        {
            uni = ch&0x07;
            todo = 3;
        }
        else
        {
            /* not a UTF-8 string */
            return 0;
        }

        for (size_t j = 0; j < todo; ++j)
        {
            utf8++;
            if ( ! *utf8 ) {
                /* not a UTF-8 string */
                return 0;
            }

            unsigned char ch = *utf8;

            if (ch < 0x80 || ch > 0xBF) {
                /* not a UTF-8 string */
                return 0;
            }

            uni <<= 6;
            uni += ch & 0x3F;
        }

        if (uni >= 0xD800 && uni <= 0xDFFF) {
            /* not a UTF-8 string */
            return 0;
        }

        if (uni > 0x10FFFF) {
            /* not a UTF-8 string */
            return 0;
        }

        *unicode = uni;
        unicode++;
        utf8++;
    }

    ssize_t len = sizeof(unsigned short) * (unicode - head + 1) * 2;
    unsigned short * out = (unsigned short *)malloc(len);

    if ( ! out ) {
        return 0;
    }

    *out_ptr = out;

    while ( head != unicode ) {
        unsigned long uni = *head;

        if (uni <= 0xFFFF)
        {
            *out = (unsigned short)uni;
            *out = ((0xff & uni) << 8) | ((uni & 0xff00) >> 8);
        }
        else
        {
            uni -= 0x10000;

            *out += (unsigned short)((uni >> 10) + 0xD800);
            *out = ((0xff & uni) << 8) | ((uni & 0xff00) >> 8);
            out++;
            *out += (unsigned short)((uni >> 10) + 0xD800);
            *out = ((0xff & uni) << 8) | ((uni & 0xff00) >> 8);
        }

        head++;
        out++;
    }

    return (out - *out_ptr) * sizeof (unsigned short);
}

const char *get_next_wide(const char *utf8, int *unicode_out) {

    unsigned long uni;
    size_t todo;

    if ( !utf8 || !*utf8 ) {
        return utf8;
    }

    unsigned char ch = *utf8;

    *unicode_out = 0;

    if (ch <= 0x7F)
    {
        uni = ch;
        todo = 0;
    }
    else if (ch <= 0xBF)
    {
        /* not a UTF-8 string */
        return utf8;
    }
    else if (ch <= 0xDF)
    {
        uni = ch&0x1F;
        todo = 1;
    }
    else if (ch <= 0xEF)
    {
        uni = ch&0x0F;
        todo = 2;
    }
    else if (ch <= 0xF7)
    {
        uni = ch&0x07;
        todo = 3;
    }
    else
    {
        /* not a UTF-8 string */
        return utf8;
    }

    for (size_t j = 0; j < todo; ++j)
    {
        utf8++;
        if ( ! *utf8 ) {
            /* not a UTF-8 string */
            return utf8;
        }

        unsigned char ch = *utf8;

        if (ch < 0x80 || ch > 0xBF) {
            /* not a UTF-8 string */
            return utf8;
        }

        uni <<= 6;
        uni += ch & 0x3F;
    }

    if (uni >= 0xD800 && uni <= 0xDFFF) {
        /* not a UTF-8 string */
        return utf8;
    }

    if (uni > 0x10FFFF) {
        /* not a UTF-8 string */
        return utf8;
    }

    *unicode_out = (int)uni;
    utf8++;

    return utf8;
}

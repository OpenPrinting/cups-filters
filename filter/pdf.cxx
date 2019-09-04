/*
 * Copyright 2012 Canonical Ltd.
 * Copyright 2013 ALT Linux, Andrew V. Stepanov <stanv@altlinux.com>
 * Copyright 2018 Sahil Arora <sahilarora.535@gmail.com>
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
#include <vector>
#include <string>
#include <cstring>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

/*
 * Useful reference:
 *
 * http://www.gnupdf.org/Indirect_Object
 * http://www.gnupdf.org/Introduction_to_PDF
 * http://blog.idrsolutions.com/2011/05/understanding-the-pdf-file-format-%E2%80%93-pdf-xref-tables-explained
 * http://labs.appligent.com/pdfblog/pdf-hello-world/
 * https://github.com/OpenPrinting/cups-filters/pull/25
*/


/**
 * 'makeRealBox()' - Return a QPDF array object of real values for a box.
 * O - QPDFObjectHandle for an array
 * I - Dimensions of the box in a float array
 */
static QPDFObjectHandle makeRealBox(float values[4])
{
  QPDFObjectHandle ret = QPDFObjectHandle::newArray();
  for (int i = 0; i < 4; ++i) {
    ret.appendItem(QPDFObjectHandle::newReal(values[i]));
  }
  return ret;
}


/**
 * 'pdf_load_template()' - Load an existing PDF file and do initial parsing
 *                         using QPDF.
 * I - Filename to open
 */
extern "C" pdf_t * pdf_load_template(const char *filename)
{
  QPDF *pdf = new QPDF();
  pdf->processFile(filename);
  unsigned pages = (pdf->getAllPages()).size();

  if (pages != 1) {
    fprintf(stderr, "ERROR: PDF template must contain exactly 1 page: %s\n",
            filename);
    delete pdf;
    return NULL;
  }

  return pdf;
}


/**
 * 'pdf_free()' - Free pointer used by PDF object
 * I - Pointer to PDF object
 */
extern "C" void pdf_free(pdf_t *pdf)
{
  delete pdf;
}


/**
 * 'pdf_prepend_stream' - Prepend a stream to the contents of a specified
 *                        page in PDF file.
 * I - Pointer to QPDF object
 * I - page number of page to prepend stream to
 * I - buffer containing data to be prepended
 * I - length of buffer
 */
extern "C" void pdf_prepend_stream(pdf_t *pdf,
                                   unsigned page_num,
                                   char const *buf,
                                   size_t len)
{
  std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
  if (pages.empty() || page_num > pages.size()) {
    fprintf(stderr, "ERROR: Unable to prepend stream to requested PDF page\n");
    return;
  }

  QPDFObjectHandle page = pages[page_num - 1];

  // get page contents stream / array  
  QPDFObjectHandle contents = page.getKey("/Contents");
  if (!contents.isStream() && !contents.isArray())
  {
    fprintf(stderr, "ERROR: Malformed PDF.\n");
    return;
  }

  // prepare the new stream which is to be prepended
  PointerHolder<Buffer> stream_data = PointerHolder<Buffer>(new Buffer(len));
  memcpy(stream_data->getBuffer(), buf, len);
  QPDFObjectHandle stream = QPDFObjectHandle::newStream(pdf, stream_data);
  stream = pdf->makeIndirectObject(stream);

  // if the contents is an array, prepend the new stream to the array,
  // else convert the contents to an array and the do the same.
  if (contents.isStream())
  {
    QPDFObjectHandle old_streamdata = contents;
    contents = QPDFObjectHandle::newArray();
    contents.appendItem(old_streamdata);
  }

  contents.insertItem(0, stream);
  page.replaceKey("/Contents", contents);
}


/**
 * 'pdf_add_type1_font()' - Add the specified type1 fontface to the specified
 *                          page in a PDF document.
 * I - QPDF object
 * I - page number of the page to which the font is to be added
 * I - name of the font to be added
 */
extern "C" void pdf_add_type1_font(pdf_t *pdf,
                                   unsigned page_num,
                                   const char *name)
{
  std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
  if (pages.empty() || page_num > pages.size()) {
    fprintf(stderr, "ERROR: Unable to add type1 font to requested PDF page\n");
    return;
  }

  QPDFObjectHandle page = pages[page_num - 1];

  QPDFObjectHandle resources = page.getKey("/Resources");
  if (!resources.isDictionary())
  {
    fprintf(stderr, "ERROR: Malformed PDF.\n");
    return;
  }

  QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
  font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
  font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
  font.replaceKey("/BaseFont",
                  QPDFObjectHandle::newName(std::string("/") + std::string(name)));

  QPDFObjectHandle fonts = resources.getKey("/Font");
  if (fonts.isNull())
  {
    fonts = QPDFObjectHandle::newDictionary();
  }
  else if (!fonts.isDictionary())
  {
    fprintf(stderr, "ERROR: Can't recognize Font resource in PDF template.\n");
    return;
  }

  font = pdf->makeIndirectObject(font);
  fonts.replaceKey("/bannertopdf-font", font);
  resources.replaceKey("/Font", fonts);
}


/**
 * 'dict_lookup_rect()' - Lookup for an array of rectangle dimensions in a QPDF
 *                        dictionary object. If it is found, store the values in
 *                        an array and return true, else return false.
 * O - True / False, depending on whether the key is found in the dictionary
 * I - PDF dictionary object
 * I - Key to lookup for
 * I - array to store values if key is found
 */
static bool dict_lookup_rect(QPDFObjectHandle object,
                             std::string const& key,
                             float rect[4])
{
  // preliminary checks
  if (!object.isDictionary() || !object.hasKey(key))
    return false;

  // check if the key is array or some other type
  QPDFObjectHandle value = object.getKey(key);
  if (!value.isArray())
    return false;
  
  // get values in a vector and assign it to rect
  std::vector<QPDFObjectHandle> array = value.getArrayAsVector();
  for (int i = 0; i < 4; ++i) {
    // if the value in the array is not real, we have an invalid array 
    if (!array[i].isReal() && !array[i].isInteger())
      return false;
    
    rect[i] = array[i].getNumericValue();
  }

  return array.size() == 4;
}


/**
 * 'fit_rect()' - Update the scale of the page using old media box dimensions
 *                and new media box dimensions.
 * I - Old media box
 * I - New media box
 * I - Pointer to scale which needs to be updated
 */
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


/**
 * 'pdf_resize_page()' - Resize page in a PDF with the given dimensions.
 * I - Pointer to QPDF object
 * I - Page number
 * I - Width of page to set
 * I - Length of page to set
 * I - Scale of page to set
 */
extern "C" void pdf_resize_page (pdf_t *pdf,
                                 unsigned page_num,
                                 float width,
                                 float length,
                                 float *scale)
{
  std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
  if (pages.empty() || page_num > pages.size()) {
    fprintf(stderr, "ERROR: Unable to resize requested PDF page\n");
    return;
  }

  QPDFObjectHandle page = pages[page_num - 1];
  float new_mediabox[4] = { 0.0, 0.0, width, length };
  float old_mediabox[4];
  QPDFObjectHandle media_box;

  if (!dict_lookup_rect(page, "/MediaBox", old_mediabox)) {
    fprintf(stderr, "ERROR: pdf doesn't contain a valid mediabox\n");
    return;
  }

  fit_rect(old_mediabox, new_mediabox, scale);
  media_box = makeRealBox(new_mediabox);

  page.replaceKey("/ArtBox", media_box);
  page.replaceKey("/BleedBox", media_box);
  page.replaceKey("/CropBox", media_box);
  page.replaceKey("/MediaBox", media_box);
  page.replaceKey("/TrimBox", media_box);
}


/**
 * 'pdf_duplicate_page()' - Duplicate a specified pdf page in a PDF
 * I - Pointer to QPDF object
 * I - page number of the page to be duplicated
 * I - number of copies to be duplicated
 */
extern "C" void pdf_duplicate_page (pdf_t *pdf,
                                    unsigned page_num,
                                    unsigned count)
{
  std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
  if (pages.empty() || page_num > pages.size()) {
    fprintf(stderr, "ERROR: Unable to duplicate requested PDF page\n");
    return;
  }

  QPDFObjectHandle page = pages[page_num - 1];
  for (unsigned i = 0; i < count; ++i)
  {
    page = pdf->makeIndirectObject(page);
    pdf->addPage(page, false);
  }
}


/**
 * 'pdf_write()' - Write the contents of PDF object to an already open FILE*.
 * I - pointer to QPDF structure
 * I - File pointer to write to
 */
extern "C" void pdf_write(pdf_t *pdf, FILE *file)
{
  QPDFWriter output(*pdf, "pdf_write", file, false);
  output.write();
}



/*
 * 'lookup_opt()' - Get value according to key in the options list.
 * I - pointer to the opt_t type list
 * I - key to be found in the list
 * O - character string which corresponds to the value of the key or
 *     NULL if key is not found in the list.
 */
std::string lookup_opt(opt_t *opt, std::string const& key) {
    if ( ! opt || key.empty() ) {
        return "";
    }

    while (opt) {
        if (opt->key && opt->val) {
            if ( strcmp(opt->key, key.c_str()) == 0 ) {
                return std::string(opt->val);
            }
        }
        opt = opt->next;
    }

    return "";
}


/*
 * 'pdf_fill_form()' -  1. Lookup in PDF template file for form.
 *                      2. Lookup for form fields' names.
 *                      3. Fill recognized fields with information.
 * I - Pointer to the QPDF structure
 * I - Pointer to the opt_t type list
 * O - status of form fill - 0 for failure, 1 for success
 */
extern "C" int pdf_fill_form(pdf_t *doc, opt_t *opt)
{
    // initialize AcroFormDocumentHelper and PageDocumentHelper objects
    // to work with forms in the PDF
    QPDFAcroFormDocumentHelper afdh(*doc);
    QPDFPageDocumentHelper pdh(*doc);

    // check if the PDF has a form or not
    if ( !afdh.hasAcroForm() ) {
        fprintf(stderr, "DEBUG: PDF template file doesn't have form. It's okay.\n");
        return 0;
    }

    // get the first page from the PDF to fill the form. Since this
    // is a banner file,it must contain only a single page, and that
    // check has already been performed in the `pdf_load_template()` function
    std::vector<QPDFPageObjectHelper> pages = pdh.getAllPages();
    if (pages.empty()) {
        fprintf(stderr, "ERROR: Can't get page from PDF tamplate file.\n");
        return 0;
    }
    QPDFPageObjectHelper page = pages.front();

    // get the annotations in the page
    std::vector<QPDFAnnotationObjectHelper> annotations =
                  afdh.getWidgetAnnotationsForPage(page);

    for (std::vector<QPDFAnnotationObjectHelper>::iterator annot_iter =
                     annotations.begin();
                 annot_iter != annotations.end(); ++annot_iter) {
        // For each annotation, find its associated field. If it's a
        // text field, we try to set its value. This will automatically
        // update the document to indicate that appearance streams need
        // to be regenerated. At the time of this writing, qpdf doesn't
        // have any helper code to assist with appearance stream generation,
        // though there's nothing that prevents it from being possible.
        QPDFFormFieldObjectHelper ffh =
            afdh.getFieldForAnnotation(*annot_iter);
        if (ffh.getFieldType() == "/Tx") {
            // Lookup the options setting for value of this field and fill the
            // value accordingly. This will automatically set
            // /NeedAppearances to true.
            std::string const name = ffh.getFullyQualifiedName();
            std::string fill_with = lookup_opt(opt, name);
            if (fill_with.empty()) {
                std::cerr << "DEBUG: Lack information for widget: " << name << ".\n";
                fill_with = "N/A";
            }

            // convert the 'fill_with' string to UTF16 before filling to the widget 
            QPDFObjectHandle fill_with_utf_16 = QPDFObjectHandle::newUnicodeString(fill_with);
            ffh.setV(fill_with_utf_16);
            std::cerr << "DEBUG: Fill widget name " << name << " with value "
                      << fill_with_utf_16.getUTF8Value() << ".\n";
        }
    }

    // status 1 notifies that the function successfully filled all the
    // identifiable fields in the form
    return 1;
}

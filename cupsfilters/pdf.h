/*
 * Copyright 2012 Canonical Ltd.
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

#ifndef _CUPS_FILTERS_PDF_H_
#define _CUPS_FILTERS_PDF_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QPDF cf_pdf_t;

typedef struct _cf_opt cf_opt_t;

/*
 * Type to bunch PDF form field name and its value.
 */
struct _cf_opt {
    const char* key;
    const char* val;
    cf_opt_t *next;
};

cf_pdf_t *cfPDFLoadTemplate(const char *filename);
void cfPDFFree(cf_pdf_t *pdf);
void cfPDFWrite(cf_pdf_t *doc, FILE *file);
int cfPDFPrependStream(cf_pdf_t *doc, unsigned page, char const *buf,
		       size_t len);
int cfPDFAddType1Font(cf_pdf_t *doc, unsigned page, const char *name);
int cfPDFResizePage(cf_pdf_t *doc, unsigned page, float width, float length,
		    float *scale);
int cfPDFDuplicatePage(cf_pdf_t *doc, unsigned page, unsigned count);
int cfPDFFillForm(cf_pdf_t *doc, cf_opt_t *opt);
int cfPDFPages(const char *filename);
int cfPDFPagesFP(FILE *file);

#ifdef __cplusplus
}
#endif

#endif /* !_CUPS_FILTERS_PDF_H_ */

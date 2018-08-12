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

#ifndef pdf_h
#define pdf_h

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QPDF pdf_t;

typedef struct _opt opt_t;

/*
 * Type to bunch PDF form field name and its value.
 */
struct _opt {
    const char* key;
    const char* val;
    opt_t *next;
};

pdf_t * pdf_load_template(const char *filename);
void pdf_free(pdf_t *pdf);
void pdf_write(pdf_t *doc, FILE *file);
void pdf_prepend_stream(pdf_t *doc, unsigned page, char const *buf, size_t len);
void pdf_add_type1_font(pdf_t *doc, unsigned page, const char *name);
void pdf_resize_page(pdf_t *doc, unsigned page, float width, float length, float *scale);
void pdf_duplicate_page (pdf_t *doc, unsigned page, unsigned count);
int pdf_fill_form(pdf_t *doc, opt_t *opt);

#ifdef __cplusplus
}
#endif

#endif

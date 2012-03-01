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

#ifndef pdf_h
#define pdf_h

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PDFDoc pdf_t;

pdf_t * pdf_load_template(const char *filename);
void pdf_write(pdf_t *doc, FILE *file);
void pdf_prepend_stream(pdf_t *doc, int page, char *buf, size_t len);
void pdf_add_type1_font(pdf_t *doc, int page, const char *name);
void pdf_resize_page (pdf_t *doc, int page, float width, float length, float *scale);

#ifdef __cplusplus
}
#endif

#endif


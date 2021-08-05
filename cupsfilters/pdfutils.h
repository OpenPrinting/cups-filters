/*
 *   PDF file output routines.
 *
 *   Copyright 2008 by Tobias Hoffmann.
 *
 *   This file is licensed as noted in "COPYING" 
 *   which should have been included with this file.
 *
 */
#include <time.h>

struct keyval_t {
  char *key,*value;
};

typedef struct {
  long filepos;

  int pagessize,pagesalloc;
  int *pages;

  int xrefsize,xrefalloc;
  long *xref;

  int kvsize,kvalloc;
  struct keyval_t *kv;
} pdfOut;

/* allocates a new pdfOut structure
 * returns NULL on error
 */
pdfOut *pdfOut_new();
void pdfOut_free(pdfOut *pdf);

/* start outputting a pdf
 * returns false on error
 */
int pdfOut_begin_pdf(pdfOut *pdf);
void pdfOut_finish_pdf(pdfOut *pdf);

/* General output routine for our pdf.
 * Keeps track of characters actually written out
 */
void pdfOut_printf(pdfOut *pdf,const char *fmt,...)
  __attribute__((format(printf, 2, 3)));

/* write out an escaped pdf string: e.g.  (Text \(Test\)\n)
 * >len==-1: use strlen(str) 
 */
void pdfOut_putString(pdfOut *pdf,const char *str,int len);
void pdfOut_putHexString(pdfOut *pdf,const char *str,int len);

/* Format the broken up timestamp according to
 * pdf requirements for /CreationDate
 * NOTE: uses statically allocated buffer 
 */
const char *pdfOut_to_pdfdate(struct tm *curtm);

/* begin a new object at current point of the 
 * output stream and add it to the xref table.
 * returns the obj number.
 */
int pdfOut_add_xref(pdfOut *pdf);

/* adds page dictionary >obj to the global Pages tree
 * returns false on error
 */
int pdfOut_add_page(pdfOut *pdf,int obj);

/* add a >key,>val pair to the document's Info dictionary
 * returns false on error
 */
int pdfOut_add_kv(pdfOut *pdf,const char *key,const char *val);

/* Writes the font >emb including descriptor to the pdf 
 * and returns the object number.
 * On error 0 is returned.
 */
struct _EMB_PARAMS;
int pdfOut_write_font(pdfOut *pdf,struct _EMB_PARAMS *emb);

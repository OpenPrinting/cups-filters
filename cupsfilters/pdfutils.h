//
// PDF file output routines for libcupsfilters.
//
// Copyright 2008 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_FILTERS_PDFUTILS_H_
#  define _CUPS_FILTERS_PDFUTILS_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Include necessary headers...
//

#include <time.h>
#include <cupsfilters/fontembed-private.h>


//
// Types and structures...
//

struct cf_keyval_t
{
  char *key, *value;
};

typedef struct
{
  long filepos;

  int pagessize, pagesalloc;
  int *pages;

  int xrefsize, xrefalloc;
  long *xref;

  int kvsize, kvalloc;
  struct cf_keyval_t *kv;
} cf_pdf_out_t;


//
// Prototypes...
//

// allocates a new cf_pdf_out_t structure
// returns NULL on error

cf_pdf_out_t *cfPDFOutNew();
void cfPDFOutFree(cf_pdf_out_t *pdf);

// start outputting a pdf
// returns false on error

int cfPDFOutBeginPDF(cf_pdf_out_t *pdf);
void cfPDFOutFinishPDF(cf_pdf_out_t *pdf);

// General output routine for our pdf.
// Keeps track of characters actually written out

void cfPDFOutPrintF(cf_pdf_out_t *pdf, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));

// write out an escaped pdf string: e.g.  (Text \(Test\)\n)
// > len == -1: use strlen(str) 

void cfPDFOutputString(cf_pdf_out_t *pdf, const char *str, int len);
void cfPDFOutputHexString(cf_pdf_out_t *pdf, const char *str, int len);

// Format the broken up timestamp according to
// pdf requirements for /CreationDate
// NOTE: uses statically allocated buffer 

const char *cfPDFOutToPDFDate(struct tm *curtm);

// begin a new object at current point of the 
// output stream and add it to the xref table.
// returns the obj number.

int cfPDFOutAddXRef(cf_pdf_out_t *pdf);

// adds page dictionary >obj to the global Pages tree
// returns false on error

int cfPDFOutAddPage(cf_pdf_out_t *pdf, int obj);

// add a >key,>val pair to the document's Info dictionary
// returns false on error

int cfPDFOutAddKeyValue(cf_pdf_out_t *pdf, const char *key, const char *val);

// Writes the font >emb including descriptor to the pdf 
// and returns the object number.
// On error 0 is returned.

int cfPDFOutWriteFont(cf_pdf_out_t *pdf,
		      struct _cf_fontembed_emb_params_s *emb);

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_PDFUTILS_H_

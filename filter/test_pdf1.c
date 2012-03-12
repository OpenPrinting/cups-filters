#include "pdfutils.h"
#include <assert.h>
#include <string.h>

int main()
{
  pdfOut *pdf;

  pdf=pdfOut_new();
  assert(pdf);

  pdfOut_begin_pdf(pdf);

  // bad font
  int font_obj=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Type/Font\n"
                    "  /Subtype /Type1\n" // /TrueType,/Type3
                    "  /BaseFont /%s\n"
                    ">>\n"
                    "endobj\n"
                    ,font_obj,"Courier");
  // test
  const int PageWidth=595,PageLength=842;
  int cobj=pdfOut_add_xref(pdf);
  const char buf[]="BT /a 10 Tf (abc) Tj ET";
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Length %d\n"
                    ">>\n"
                    "stream\n"
                    "%s\n"
                    "endstream\n"
                    "endobj\n"
                    ,cobj,strlen(buf),buf);

  int obj=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Type/Page\n"
                    "  /Parent 1 0 R\n"
                    "  /MediaBox [0 0 %d %d]\n"
                    "  /Contents %d 0 R\n"
                    "  /Resources << /Font << /a %d 0 R >> >>\n"
                    ">>\n"
                    "endobj\n"
                    ,obj,PageWidth,PageLength,cobj,font_obj); // TODO: into pdf->
  pdfOut_add_page(pdf,obj);
  pdfOut_finish_pdf(pdf);

  pdfOut_free(pdf);

  return 0;
}

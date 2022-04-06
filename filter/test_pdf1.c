#include "pdfutils.h"
#include <assert.h>
#include <string.h>

int main()
{
  cf_pdf_out_t *pdf;

  pdf=cfPDFOutNew();
  assert(pdf);

  cfPDFOutBeginPDF(pdf);

  // bad font
  int font_obj=cfPDFOutAddXRef(pdf);
  cfPDFOutPrintF(pdf,"%d 0 obj\n"
                    "<</Type/Font\n"
                    "  /Subtype /Type1\n" // /TrueType,/Type3
                    "  /BaseFont /%s\n"
                    ">>\n"
                    "endobj\n"
                    ,font_obj,"Courier");
  // test
  const int PageWidth=595,PageLength=842;
  int cobj=cfPDFOutAddXRef(pdf);
  const char buf[]="BT /a 10 Tf (abc) Tj ET";
  cfPDFOutPrintF(pdf,"%d 0 obj\n"
                    "<</Length %d\n"
                    ">>\n"
                    "stream\n"
                    "%s\n"
                    "endstream\n"
                    "endobj\n"
                    ,cobj,strlen(buf),buf);

  int obj=cfPDFOutAddXRef(pdf);
  cfPDFOutPrintF(pdf,"%d 0 obj\n"
                    "<</Type/Page\n"
                    "  /Parent 1 0 R\n"
                    "  /MediaBox [0 0 %d %d]\n"
                    "  /Contents %d 0 R\n"
                    "  /Resources << /Font << /a %d 0 R >> >>\n"
                    ">>\n"
                    "endobj\n"
                    ,obj,PageWidth,PageLength,cobj,font_obj); // TODO: into pdf->
  cfPDFOutAddPage(pdf,obj);
  cfPDFOutFinishPDF(pdf);

  cfPDFOutFree(pdf);

  return 0;
}

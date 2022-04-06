#include "pdfutils.h"
#include "config.h"
#include <assert.h>
#include "fontembed/embed.h"
#include "fontembed/sfnt.h"

#include <stdio.h>

static inline void write_string(cf_pdf_out_t *pdf,EMB_PARAMS *emb,const char *str) // {{{
{
  assert(pdf);
  assert(emb);
  int iA;

  if (emb->plan&EMB_A_MULTIBYTE) {
    putc('<',stdout); 
    for (iA=0;str[iA];iA++) {
      const unsigned short gid=emb_get(emb,(unsigned char)str[iA]);
      fprintf(stdout,"%04x",gid);
    }
    putc('>',stdout); 
    pdf->filepos+=4*iA+2;
  } else { 
    for (iA=0;str[iA];iA++) {
      emb_get(emb,(unsigned char)str[iA]);
      // TODO: pdf: otf_from_pdf_default_encoding
    }
    cfPDFOutPutString(pdf,str,-1);
  }
}
// }}}

int main()
{
  cf_pdf_out_t *pdf;

  pdf=cfPDFOutNew();
  assert(pdf);

  cfPDFOutBeginPDF(pdf);

  // font, pt.1 
  const char *fn=TESTFONT;
  OTF_FILE *otf=NULL;
/*
  if (argc==2) {
    fn=argv[1];
  }
*/
  otf=otf_load(fn);
  if (!otf)
  {
    printf("Font %s was not loaded, exiting.\n", TESTFONT);
    return 1;
  }
  assert(otf);
  FONTFILE *ff=fontfile_open_sfnt(otf);
  EMB_PARAMS *emb=emb_new(ff,
                          EMB_DEST_PDF16,
                          EMB_C_FORCE_MULTIBYTE|
                          EMB_C_TAKE_FONTFILE);

  // test
  const int PageWidth=595,PageLength=842;
  const int cobj=cfPDFOutAddXRef(pdf);
  cfPDFOutPrintF(pdf,"%d 0 obj\n"
                    "<</Length %d 0 R\n"
                    ">>\n"
                    "stream\n"
                    ,cobj,cobj+1);
  long streamlen=-pdf->filepos;
  cfPDFOutPrintF(pdf,"BT /a 10 Tf ");
  write_string(pdf,emb,"Test");
  cfPDFOutPrintF(pdf," Tj ET");

  streamlen+=pdf->filepos;
  cfPDFOutPrintF(pdf,"\nendstream\n"
                    "endobj\n");
  const int clobj=cfPDFOutAddXRef(pdf);
  assert(clobj==cobj+1);
  cfPDFOutPrintF(pdf,"%d 0 obj\n"
                    "%d\n"
                    "endobj\n"
                    ,clobj,streamlen);

  // font
  int font_obj=cfPDFOutWriteFont(pdf,emb);
  assert(font_obj);

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

  emb_close(emb);

  return 0;
}

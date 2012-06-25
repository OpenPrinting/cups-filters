#include "pdfutils.h"
#include "config.h"
#include <assert.h>
#include "fontembed/embed.h"
#include "fontembed/sfnt.h"

#include <stdio.h>

static inline void write_string(pdfOut *pdf,EMB_PARAMS *emb,const char *str) // {{{
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
    pdfOut_putString(pdf,str,-1);
  }
}
// }}}

int main()
{
  pdfOut *pdf;

  pdf=pdfOut_new();
  assert(pdf);

  pdfOut_begin_pdf(pdf);

  // font, pt.1 
  const char *fn=TESTFONT;
/*
  if (argc==2) {
    fn=argv[1];
  }
*/
  OTF_FILE *otf=otf_load(fn);
  assert(otf);
  FONTFILE *ff=fontfile_open_sfnt(otf);
  EMB_PARAMS *emb=emb_new(ff,
                          EMB_DEST_PDF16,
                          EMB_C_FORCE_MULTIBYTE|
                          EMB_C_TAKE_FONTFILE);

  // test
  const int PageWidth=595,PageLength=842;
  const int cobj=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Length %d 0 R\n"
                    ">>\n"
                    "stream\n"
                    ,cobj,cobj+1);
  long streamlen=-pdf->filepos;
  pdfOut_printf(pdf,"BT /a 10 Tf ");
  write_string(pdf,emb,"Test");
  pdfOut_printf(pdf," Tj ET");

  streamlen+=pdf->filepos;
  pdfOut_printf(pdf,"\nendstream\n"
                    "endobj\n");
  const int clobj=pdfOut_add_xref(pdf);
  assert(clobj==cobj+1);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "%d\n"
                    "endobj\n"
                    ,clobj,streamlen);

  // font
  int font_obj=pdfOut_write_font(pdf,emb);
  assert(font_obj);

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

  emb_close(emb);

  return 0;
}

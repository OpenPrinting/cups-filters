#include "pdfutils.h"
#include "config.h"
#include "debug-internal.h"
#include "cupsfilters/fontembed-private.h"

#include <stdio.h>

static inline void write_string(cf_pdf_out_t *pdf,_cf_fontembed_emb_params_t *emb,const char *str) // {{{
{
  DEBUG_assert(pdf);
  DEBUG_assert(emb);
  int iA;

  if (emb->plan&_CF_FONTEMBED_EMB_A_MULTIBYTE) {
    putc('<',stdout); 
    for (iA=0;str[iA];iA++) {
      const unsigned short gid=_cfFontEmbedEmbGet(emb,(unsigned char)str[iA]);
      fprintf(stdout,"%04x",gid);
    }
    putc('>',stdout); 
    pdf->filepos+=4*iA+2;
  } else { 
    for (iA=0;str[iA];iA++) {
      _cfFontEmbedEmbGet(emb,(unsigned char)str[iA]);
      // TODO: pdf: otf_from_pdf_default_encoding
    }
    cfPDFOutputString(pdf,str,-1);
  }
}
// }}}

int main()
{
  cf_pdf_out_t *pdf;

  pdf=cfPDFOutNew();
  DEBUG_assert(pdf);

  cfPDFOutBeginPDF(pdf);

  // font, pt.1 
  const char *fn=TESTFONT;
  _cf_fontembed_otf_file_t *otf=NULL;
/*
  if (argc==2) {
    fn=argv[1];
  }
*/
  otf=_cfFontEmbedOTFLoad(fn);
  if (!otf)
  {
    printf("Font %s was not loaded, exiting.\n", TESTFONT);
    return 1;
  }
  DEBUG_assert(otf);
  _cf_fontembed_fontfile_t *ff=_cfFontEmbedFontFileOpenSFNT(otf);
  _cf_fontembed_emb_params_t *emb=_cfFontEmbedEmbNew(ff,
                          _CF_FONTEMBED_EMB_DEST_PDF16,
                          _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE|
                          _CF_FONTEMBED_EMB_C_TAKE_FONTFILE);

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
  DEBUG_assert(clobj==cobj+1);
  cfPDFOutPrintF(pdf,"%d 0 obj\n"
                    "%ld\n"
                    "endobj\n"
                    ,clobj,streamlen);

  // font
  int font_obj=cfPDFOutWriteFont(pdf,emb);
  DEBUG_assert(font_obj);

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

  _cfFontEmbedEmbClose(emb);

  return 0;
}

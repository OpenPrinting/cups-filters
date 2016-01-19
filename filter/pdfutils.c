/*
 *   PDF file output routines.
 *
 *   Copyright 2008 by Tobias Hoffmann.
 *
 *   This file is licensed as noted in "COPYING" 
 *   which should have been included with this file.
 *
 */
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>
#include "pdfutils.h"
#include "fontembed/embed.h"

void pdfOut_printf(pdfOut *pdf,const char *fmt,...) // {{{
{
  assert(pdf);
  int len;
  va_list ap;

  va_start(ap,fmt);
  len=vprintf(fmt,ap);
  va_end(ap);
  pdf->filepos+=len;
}
// }}}

void pdfOut_putString(pdfOut *pdf,const char *str,int len) // {{{ - >len==-1: strlen()
{
  assert(pdf);
  assert(str);
  if (len==-1) {
    len=strlen(str);
  }
  putc('(',stdout);
  // escape special chars: \0 \\ \( \)  -- don't bother about balanced parens
  int iA=0;
  for (;len>0;iA++,len--) {
    if ( (str[iA]<32)||(str[iA]>126) ) {
      fwrite(str,1,iA,stdout);
      fprintf(stdout,"\\%03o",(unsigned char)str[iA]);
      pdf->filepos+=iA+4;
      str+=iA+1;
      iA=-1;
    } else if ( (str[iA]=='(')||(str[iA]==')')||(str[iA]=='\\') ) {
      fwrite(str,1,iA,stdout);
      fprintf(stdout,"\\%c",str[iA]);
      pdf->filepos+=iA+2;
      str+=iA+1;
      iA=-1;
    }
  }
  pdf->filepos+=iA+2;
  fwrite(str,1,iA,stdout);
  putc(')',stdout);
}
// }}}

void pdfOut_putHexString(pdfOut *pdf,const char *str,int len) // {{{ - >len==-1: strlen()
{
  assert(pdf);
  assert(str);
  if (len==-1) {
    len=strlen(str);
  }
  pdf->filepos+=2*len+2;
  putc('<',stdout);
  for (;len>0;str++,len--) {
    fprintf(stdout,"%02x",(unsigned char)*str);
  }
  putc('>',stdout);
}
// }}}

pdfOut *pdfOut_new() // {{{ -  NULL on error 
{
  pdfOut *ret=malloc(sizeof(pdfOut));
  if (ret) {
    memset(ret,0,sizeof(pdfOut));
  }

  return ret;
}
// }}}

// NOTE: uses statically allocated buffer
const char *pdfOut_to_pdfdate(struct tm *curtm) // {{{
{
  static char curdate[250];
  if (!curtm) {
    time_t curtime;
    curtime = time(NULL);
    curtm   = localtime(&curtime);
  }
  strftime(curdate, sizeof(curdate), "D:%Y%m%d%H%M%S%z", curtm);
  curdate[23]=0;
  curdate[22]='\'';
  curdate[21]=curdate[18];
  curdate[20]=curdate[17];
  curdate[19]='\'';
  return curdate;
}
// }}}

int pdfOut_add_xref(pdfOut *pdf) // {{{  -  returns obj_no
{
  assert(pdf);
  assert(pdf->xrefsize<=pdf->xrefalloc);

  if (pdf->xrefsize==pdf->xrefalloc) {
    long *tmp;
    pdf->xrefalloc+=50;
    tmp=realloc(pdf->xref,sizeof(long)*pdf->xrefalloc);
    if (!tmp) {
      pdf->xrefalloc=-1;
      return -1;
    }
    pdf->xref=tmp;
  }
  pdf->xref[pdf->xrefsize++]=pdf->filepos;
  return pdf->xrefsize; // xrefsize+1
}
// }}}

int pdfOut_add_page(pdfOut *pdf,int obj) // {{{ -  returns false on error
{
  assert(pdf);
  assert(obj>0);
  assert(pdf->pagessize<=pdf->pagesalloc);

  if (pdf->pagessize==pdf->pagesalloc) {
    int *tmp;
    pdf->pagesalloc+=10;
    tmp=realloc(pdf->pages,sizeof(int)*pdf->pagesalloc);
    if (!tmp) {
      pdf->pagesalloc=-1;
      return 0;
    }
    pdf->pages=tmp;
  }
  pdf->pages[pdf->pagessize++]=obj;
  return 1;
}
// }}}

int pdfOut_add_kv(pdfOut *pdf,const char *key,const char *val) // {{{ -  returns false on error
{
  assert(pdf);
  assert(pdf->kvsize<=pdf->kvalloc);

  if (pdf->kvsize==pdf->kvalloc) {
    struct keyval_t *tmp;
    pdf->kvalloc+=10;
    tmp=realloc(pdf->kv,sizeof(struct keyval_t)*pdf->kvalloc);
    if (!tmp) {
      pdf->kvalloc=-1;
      return 0;
    }
    pdf->kv=tmp;
  }
  pdf->kv[pdf->kvsize].key=strdup(key);
  pdf->kv[pdf->kvsize].value=strdup(val);
  if ( (!pdf->kv[pdf->kvsize].key)||(!pdf->kv[pdf->kvsize].value) ) {
    return 0;
  }
  pdf->kvsize++;
  return 1;
}
// }}}

int pdfOut_begin_pdf(pdfOut *pdf) // ,...output_device?...) // {{{ - false on error
{
  assert(pdf);
  assert(pdf->kvsize==0); // otherwise: finish_pdf has not been called
  int pages_obj;

  pdf->xrefsize=pdf->pagessize=0;
  pdf->filepos=0;
  pages_obj=pdfOut_add_xref(pdf); // fixed later
  if (pages_obj!=1) {
    return 0;
  }
  pdfOut_printf(pdf,"%%PDF-1.3\n");
  return 1;
}
// }}}

void pdfOut_finish_pdf(pdfOut *pdf) // {{{
{
  int iA;
  int root_obj,info_obj=0,xref_start;
  assert( (pdf)&&(pdf->filepos!=-1) );

  // pages 
  const int pages_obj=1;
  pdf->xref[0]=pdf->filepos; // now fix it
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Type/Pages\n"
                    "  /Count %d\n"
                    "  /Kids [",
                    pages_obj,pdf->pagessize);
  for (iA=0;iA<pdf->pagessize;iA++) {
    pdfOut_printf(pdf,"%d 0 R ",pdf->pages[iA]);
  }
  pdfOut_printf(pdf,"]\n"
                    ">>\n"
                    "endobj\n");

  // rootdict
  root_obj=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Type/Catalog\n"
                    "  /Pages %d 0 R\n"
                    ">>\n"
                    "endobj\n",
                    root_obj,pages_obj);

  // info 
  if (pdf->kvsize) {
    info_obj=pdfOut_add_xref(pdf);
    pdfOut_printf(pdf,"%d 0 obj\n"
                      "<<\n",
                      info_obj);
    for (iA=0;iA<pdf->kvsize;iA++) {
      pdfOut_printf(pdf,"  /%s ",pdf->kv[iA].key);
      pdfOut_putString(pdf,pdf->kv[iA].value,-1);
      pdfOut_printf(pdf,"\n");
    }
    pdfOut_printf(pdf,">>\n"
                      "endobj\n");
  }
  // TODO: some return-value checking (??)
 
  // write xref
  xref_start=pdf->filepos;
  pdfOut_printf(pdf,"xref\n"
                    "%d %d\n"
                    "%010d 65535 f \n",
                    0,pdf->xrefsize+1,0);
  for (iA=0;iA<pdf->xrefsize;iA++) {
    pdfOut_printf(pdf,"%010ld 00000 n \n",
                      pdf->xref[iA]);
  }
  pdfOut_printf(pdf,"trailer\n"
                    "<<\n"
                    "  /Size %d\n"
                    "  /Root %d 0 R\n",
                    pdf->xrefsize+1,
                    root_obj);
  if (info_obj) {
    pdfOut_printf(pdf,"  /Info %d 0 R\n",info_obj);
  }
  pdfOut_printf(pdf,">>\n"
                    "startxref\n"
                    "%d\n"
                    "%%%%EOF\n",
                    xref_start);

  // set to done
  pdf->filepos=-1;
  for (iA=0;iA<pdf->kvsize;iA++) {
    free(pdf->kv[iA].key);
    free(pdf->kv[iA].value);
  }
  pdf->kvsize=0;
}
// }}}

void pdfOut_free(pdfOut *pdf) // {{{
{
  if (pdf) {
    assert(pdf->kvsize==0); // otherwise: finish_pdf has not been called
    free(pdf->kv);
    free(pdf->pages);
    free(pdf->xref);
    free(pdf);
  }
}
// }}}

static void pdfOut_outfn(const char *buf,int len,void *context) // {{{
{
  pdfOut *pdf=(pdfOut *)context;

  if (fwrite(buf,1,len,stdout)!=len) {
    perror("Short write");
    assert(0);
    return;
  }
  pdf->filepos+=len;
}
// }}}

int pdfOut_write_font(pdfOut *pdf,EMB_PARAMS *emb) // {{{ 
{
  assert(pdf);
  assert(emb);

  EMB_PDF_FONTDESCR *fdes=emb_pdf_fontdescr(emb);
  if (!fdes) {
    if (emb->outtype==EMB_FMT_STDFONT) { // std-14 font
      const int f_obj=pdfOut_add_xref(pdf);
      char *res=emb_pdf_simple_stdfont(emb);
      if (!res) {
        return 0;
      }

      pdfOut_printf(pdf,"%d 0 obj\n"
                        "%s"
                        "endobj\n"
                        ,f_obj,res);
      free(res);
      return f_obj;
    }
    return 0;
  }

  const int ff_obj=pdfOut_add_xref(pdf);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Length %d 0 R\n"
                    ,ff_obj,ff_obj+1);
  if (emb_pdf_get_fontfile_subtype(emb)) {
    pdfOut_printf(pdf,"  /Subtype /%s\n",
                      emb_pdf_get_fontfile_subtype(emb));
  }
  if (emb->outtype==EMB_FMT_TTF) {
    pdfOut_printf(pdf,"  /Length1 %d 0 R\n"
                      ,ff_obj+2);
  } else if (emb->outtype==EMB_FMT_T1) { // TODO
    pdfOut_printf(pdf,"  /Length1 ?\n"
                      "  /Length2 ?\n"
                      "  /Length3 ?\n"
                      );
  }
  pdfOut_printf(pdf,">>\n"
                    "stream\n");
  long streamsize=-pdf->filepos;
  const int outlen=emb_embed(emb,pdfOut_outfn,pdf);
  streamsize+=pdf->filepos;
  pdfOut_printf(pdf,"\nendstream\n"
                    "endobj\n");

  const int l0_obj=pdfOut_add_xref(pdf);
  assert(l0_obj==ff_obj+1);
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "%ld\n"
                    "endobj\n"
                    ,l0_obj,streamsize);

  if (emb->outtype==EMB_FMT_TTF) {
    const int l1_obj=pdfOut_add_xref(pdf);
    assert(l1_obj==ff_obj+2);
    pdfOut_printf(pdf,"%d 0 obj\n"
                      "%d\n"
                      "endobj\n"
                      ,l1_obj,outlen);
  }

  const int fd_obj=pdfOut_add_xref(pdf);
  char *res=emb_pdf_simple_fontdescr(emb,fdes,ff_obj);
  if (!res) {
    free(fdes);
    return 0;
  }
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "%s"
                    "endobj\n"
                    ,fd_obj,res);
  free(res);

  EMB_PDF_FONTWIDTHS *fwid=emb_pdf_fontwidths(emb);
  if (!fwid) {
    free(fdes);
    return 0;
  }
  const int f_obj=pdfOut_add_xref(pdf);
  res=emb_pdf_simple_font(emb,fdes,fwid,fd_obj);
  if (!res) {
    free(fwid);
    free(fdes);
    return 0;
  }
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "%s"
                    "endobj\n"
                    ,f_obj,res);
  free(res);
  free(fwid);

  if (emb->plan&EMB_A_MULTIBYTE) {
    res=emb_pdf_simple_cidfont(emb,fdes->fontname,f_obj);
    if (!res) {
      free(fdes);
      return 0;
    }
    const int cf_obj=pdfOut_add_xref(pdf);
    pdfOut_printf(pdf,"%d 0 obj\n"
                      "%s"
                      "endobj\n"
                      ,cf_obj,res);
    free(res);
    free(fdes);
    return cf_obj;
  }

  free(fdes);
  return f_obj;
}
// }}}

#if 0
one_page(...parent,resources,mediabox,contents);
{
//                    "  /Resources %d 0 R\n"
  pdfOut_printf(pdf,"%d 0 obj\n"
                    "<</Type/Page\n"
                    "  /Parent 1 0 R\n"
                    "  /MediaBox [0 0 %d %d]\n"
                    "  /Contents %d 0 R\n"
                    ">>\n"
                    "endobj\n"
                    ,,,PageWidth,PageLength // TODO: into pdf->
  ...
}

... pfb_embedder ... pfa?
#endif

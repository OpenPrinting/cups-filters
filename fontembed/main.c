#include "config.h"
#include "sfnt.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include "embed.h"

#if 0
enum { TTF_OTF, TYPE1 } inputFile;
if (TTF_OTF) {
  assert(!TTC);
  if (CFF/OTF) {
    // or EMB_PDF_FONTFILE3_OTF [unstripped]
    if (CIDfont) {
      asset(multiBYTE);
      strip_sfnt() // "CIDFontType0"  EMB_PDF_FONTFILE3_CID0C
    } else {
      ... strip_sfnt();
    }
  } else {
    ...
  }
} else if (TYPE1) {
  assert(!MMType1);
  assert(!OCF);
  assert(!WrappedCID_CFF);
  ... convert_to_cff()
}
// not supported: MMType1 Type3
#endif

#include <string.h>

static void example_outfn(const char *buf,int len,void *context) // {{{
{
  FILE *f=(FILE *)context;
  if (fwrite(buf,1,len,f)!=len) {
    perror("Short write");
    assert(0);
    return;
  }
}
// }}}

void example_write_fontdescr(OTF_FILE *otf,const char *outfile) // {{{
{
  FONTFILE *ff=fontfile_open_sfnt(otf);
  EMB_PARAMS *emb=emb_new(ff,
                          EMB_DEST_PDF16,
//                          EMB_C_KEEP_T1
                          EMB_C_FORCE_MULTIBYTE

                          );
  EMB_PDF_FONTDESCR *fdes=emb_pdf_fontdescr(emb);
  assert(fdes);

  emb_get(emb,'a');
  emb_get(emb,0x400);

  EMB_PDF_FONTWIDTHS *fwid=emb_pdf_fontwidths(emb);
  assert(fwid);

  printf("0 0 obj\n");
  char *res=emb_pdf_simple_fontdescr(emb,fdes,1);
  assert(res);
  fputs(res,stdout);
  free(res);
  printf("endobj\n");

  printf("1 0 obj\n"
         "<<\n");
  if (emb_pdf_get_fontfile_subtype(emb)) {
    printf("  /Subtype /%s\n",
           emb_pdf_get_fontfile_subtype(emb));
  }
  if (emb->outtype==EMB_FMT_T1) {
    printf("  /Length1 ?\n"
           "  /Length2 ?\n"
           "  /Length3 ?\n");
  } else if (emb->outtype==EMB_FMT_TTF) {
    printf("  /Length1 2 0 R\n");
  }
  printf("  /Length 2 0 R\n" // maybe compress it...
         ">>\n"
         "stream\n");
  int outlen=0; // TODO
// TODO
  if (outfile) {
    FILE *f=fopen(outfile,"w");
    if (!f) {
      fprintf(stderr,"Opening \"%s\" for writing failed: %s\n",outfile, strerror(errno));
      assert(0);
      emb_close(emb);
      return;
    }
    outlen=emb_embed(emb,example_outfn,f);
//    outlen=otf_ttc_extract(emb->font->sfnt,example_outfn,f);
    fclose(f);
  }
puts("...");
  printf("endstream\n"
         "endobj\n");
  printf("2 0 obj\n"
         "%d\n"
         "endobj\n",
         outlen
         );

  printf("3 0 obj\n");
  res=emb_pdf_simple_font(emb,fdes,fwid,0);
  assert(res);
  fputs(res,stdout);
  free(res);
  printf("endobj\n");

  if (emb->plan&EMB_A_MULTIBYTE) {
    printf("4 0 obj\n");
    res=emb_pdf_simple_cidfont(emb,fdes->fontname,3);
    assert(res);
    fputs(res,stdout);
    free(res);
    printf("endobj\n");
  }

  free(fdes);
  free(fwid);
  emb_close(emb);
#if 1
  free(ff); // TODO
#else
  ff->sfnt=NULL; // TODO
  fontfile_close(ff);
#endif
}
// }}}

// TODO? reencode?
int main(int argc,char **argv)
{
  const char *fn=TESTFONT;
  if (argc==2) {
    fn=argv[1];
  }
  OTF_FILE *otf=otf_load(fn);
  assert(otf);
  printf("width(4): %d\n",otf_get_width(otf,4));


  if (strcmp(fn,"test.ttf")!=0) {
    example_write_fontdescr(otf,"test.ttf");
  } else {
    example_write_fontdescr(otf,NULL);
  }

  // show_post(otf);

  // show_name(otf);

  // show_cmap(otf);
  // printf("%d %d\n",otf_from_unicode(otf,'A'),0);

  // ... name 6 -> FontName  /20(cid)
  // ? StemV Flags(?) from FontName

  otf_close(otf);

  return 0;
}

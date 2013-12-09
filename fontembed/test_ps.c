#include "embed.h"
#include "config.h"
#include "sfnt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

const char *emb_otf_get_fontname(OTF_FILE *otf); // TODO

static void example_outfn(const char *buf,int len,void *context) // {{{
{
  FILE *f=(FILE *)context;
  if (fwrite(buf,1,len,f)!=len) {
    fprintf(stderr,"Short write: %m\n");
    assert(0);
    return;
  }
}
// }}}

static inline void write_string(FILE *f,EMB_PARAMS *emb,const char *str) // {{{
{
  assert(f);
  assert(emb);
  int iA;

  if (emb->plan&EMB_A_MULTIBYTE) {
    putc('<',f);
    for (iA=0;str[iA];iA++) {
      const unsigned short gid=emb_get(emb,(unsigned char)str[iA]);
      fprintf(f,"%04x",gid);
    }
    putc('>',f);
  } else {
    putc('(',f);
    for (iA=0;str[iA];iA++) {
      emb_get(emb,(unsigned char)str[iA]);
    }
    fprintf(f,"%s",str); // TODO
    putc(')',f);
  }
}
// }}}

int main(int argc,char **argv)
{
  const char *fn=TESTFONT;
  if (argc==2) {
    fn=argv[1];
  }
  OTF_FILE *otf=otf_load(fn);
  assert(otf);
  FONTFILE *ff=fontfile_open_sfnt(otf);
  EMB_PARAMS *emb=emb_new(ff,
                          EMB_DEST_PS,
//                          EMB_C_FORCE_MULTIBYTE| // not yet...
                          EMB_C_TAKE_FONTFILE);

  FILE *f=fopen("test.ps","w");
  assert(f);

  fprintf(f,"%%!PS-Adobe-2.0\n");

  char *str="Hallo";

  emb_get(emb,'a');

  int iA;
  for (iA=0;str[iA];iA++) {
    emb_get(emb,(unsigned char)str[iA]);
  }

  emb_embed(emb,example_outfn,f);

  // content
  fprintf(f,"  100 100 moveto\n" // content
            "  /%s findfont 10 scalefont setfont\n",emb_otf_get_fontname(emb->font->sfnt));
  write_string(f,emb,"Hallo");
// Note that write_string sets subset bits, but it's too late
  fprintf(f," show\n"
            "showpage\n");

  fprintf(f,"%%%%EOF\n");
  fclose(f);

  emb_close(emb);

  return 0;
}

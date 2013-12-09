#include "sfnt.h"
#include "sfnt_int.h"
#include "embed.h"
#include "config.h"
#include "embed_sfnt_int.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { WEIGHT_THIN=100,
       WEIGHT_EXTRALIGHT=200, WEIGHT_ULTRALIGHT=200,
       WEIGHT_LIGHT=300,
       WEIGHT_NORMAL=400, WEIGHT_REGULAR=400,
       WEIGHT_MEDIUM=500,
       WEIGHT_SEMIBOLD=600, // DEMI
       WEIGHT_BOLD=700,
       WEIGHT_EXTRABOLD=800, WEIGHT_ULTRABOLD=800,
       WEIGHT_BLACK=900, WEIGHT_HEAVY=900 };

void show_post(OTF_FILE *otf) // {{{
{
  assert(otf);
  int len=0;
  char *buf;

  buf=otf_get_table(otf,OTF_TAG('p','o','s','t'),&len);
  if (!buf) {
    assert(len==-1);
    printf("No post table\n");
    return;
  }
  // TODO: check len
  printf("POST: (%d bytes)\n"
         "  version: %08x\n"
         "  italicAngle: %d.%d\n"
         "  underlinePosition: %d\n"
         "  underlineThickness: %d\n"
         "  isFixedPitch: %d\n"
         "  vmType42: %d %d\n"
         "  vmType1: %d %d\n",len,
         get_ULONG(buf),
         get_LONG(buf+4)>>16,get_ULONG(buf+4)&0xffff,
         get_SHORT(buf+8),
         get_SHORT(buf+10),
         get_ULONG(buf+12),
         get_ULONG(buf+16),get_ULONG(buf+20),
         get_ULONG(buf+24),get_ULONG(buf+38));
  free(buf);
}
// }}}

void show_name(OTF_FILE *otf) // {{{
{
  assert(otf);
  int iA,len=0;
  char *buf;

  buf=otf_get_table(otf,OTF_TAG('n','a','m','e'),&len);
  if (!buf) {
    assert(len==-1);
    printf("No name table\n");
    return;
  }
  printf("NAME:\n");
  int name_count=get_USHORT(buf+2);
  const char *nstore=buf+get_USHORT(buf+4);
  for (iA=0;iA<name_count;iA++) {
    const char *nrec=buf+6+12*iA;
    printf("  { platformID/encodingID/languageID/nameID: %d/%d/%d/%d\n"
           "    length: %d, offset: %d, data                       :",
           get_USHORT(nrec),
           get_USHORT(nrec+2),
           get_USHORT(nrec+4),
           get_USHORT(nrec+6),
           get_USHORT(nrec+8),
           get_USHORT(nrec+10));
    if (  (get_USHORT(nrec)==0)||
          ( (get_USHORT(nrec)==3) )  ) { // WCHAR
      int nlen=get_USHORT(nrec+8);
      int npos=get_USHORT(nrec+10);
      for (;nlen>0;nlen-=2,npos+=2) {
        if (nstore[npos]!=0x00) {
          printf("?");
        } else {
          printf("%c",nstore[npos+1]);
        }
      }
      printf(" }\n");
    } else {
      printf("%.*s }\n",
             get_USHORT(nrec+8),nstore+get_USHORT(nrec+10));
    }
  }
  free(buf);
}
// }}}

void show_cmap(OTF_FILE *otf) // {{{
{
  assert(otf);
  int iA,len=0;

  char *cmap=otf_get_table(otf,OTF_TAG('c','m','a','p'),&len);
  if (!cmap) {
    assert(len==-1);
    printf("No cmap table\n");
    return;
  }
  printf("cmap:\n");
  assert(get_USHORT(cmap)==0x0000); // version
  const int numTables=get_USHORT(cmap+2);
  printf("  numTables: %d\n",numTables);
  for (iA=0;iA<numTables;iA++) {
    const char *nrec=cmap+4+8*iA;
    const char *ndata=cmap+get_ULONG(nrec+4);
    assert(ndata>=cmap+4+8*numTables);
    printf("  platformID/encodingID: %d/%d\n"
           "  offset: %d  data (format: %d, length: %d, language: %d);\n",
           get_USHORT(nrec),get_USHORT(nrec+2),
           get_ULONG(nrec+4),
           get_USHORT(ndata),get_USHORT(ndata+2),get_USHORT(ndata+4));
  }
  free(cmap);
}
// }}}

void show_glyf(OTF_FILE *otf,int full) // {{{
{
  assert(otf);

  // ensure >glyphOffsets and >gly is there
  if ( (!otf->gly)||(!otf->glyphOffsets) ) {
    if (otf_load_glyf(otf)!=0) {
      assert(0);
      return;
    }
  }

  int iA;
  int compGlyf=0,zeroGlyf=0;

  // {{{ glyf
  assert(otf->gly);
  for (iA=0;iA<otf->numGlyphs;iA++) {
    int len=otf_get_glyph(otf,iA);
    if (len==0) {
      zeroGlyf++;
    } else if (get_SHORT(otf->gly)==-1) {
      compGlyf++;
    }
    if (full) {
      printf("%d(%d) ",get_SHORT(otf->gly),len);
    }
  }
  if (full) {
    printf("\n");
  }
  printf("numGlyf(nonnull): %d(%d), composites: %d\n",otf->numGlyphs,otf->numGlyphs-zeroGlyf,compGlyf);
  // }}}
}
// }}}

void show_hmtx(OTF_FILE *otf) // {{{
{
  assert(otf);
  int iA;

  otf_get_width(otf,0); // load table.
  if (!otf->hmtx) {
    printf("NOTE: no hmtx table!\n");
    return;
  }
  printf("hmtx (%d):\n",otf->numberOfHMetrics);
  for (iA=0;iA<otf->numberOfHMetrics;iA++) {
    printf("(%d,%d) ",
           get_USHORT(otf->hmtx+iA*4),
           get_SHORT(otf->hmtx+iA*4+2));
  }
  printf(" (last is repeated for the remaining %d glyphs)\n",otf->numGlyphs-otf->numberOfHMetrics);
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
  if (otf->numTTC) {
    printf("TTC has %d fonts, using %d\n",otf->numTTC,otf->useTTC);
  }
  if (otf->version==0x00010000) {
    printf("Got TTF 1.0\n");
  } else if (otf->version==OTF_TAG('O','T','T','O')) {
    printf("Got OTF(CFF)\n");
  } else if (otf->version==OTF_TAG('t','r','u','e')) {
    printf("Got TTF (true)\n");
  } else if (otf->version==OTF_TAG('t','y','p','1')) {
    printf("Got SFNT(Type1)\n");
  }

  printf("Has %d tables\n",otf->numTables);

  int iA;
  for (iA=0;iA<otf->numTables;iA++) {
    printf("%c%c%c%c %d @%d\n",OTF_UNTAG(otf->tables[iA].tag),otf->tables[iA].length,otf->tables[iA].offset);
  }
  printf("unitsPerEm: %d, indexToLocFormat: %d\n",
         otf->unitsPerEm,otf->indexToLocFormat);
  printf("num glyphs: %d\n",otf->numGlyphs);
  otf_get_width(otf,0); // load table.
  printf("numberOfHMetrics: %d\n",otf->numberOfHMetrics);

  printf("Embedding rights: %x\n",emb_otf_get_rights(otf));

  show_post(otf);

  show_name(otf);

  show_cmap(otf);
  // printf("%d %d\n",otf_from_unicode(otf,'A'),0);

  if (!(otf->flags&OTF_F_FMT_CFF)) {
    show_glyf(otf,1);
  }

  show_hmtx(otf);

  otf_close(otf);

  return 0;
}

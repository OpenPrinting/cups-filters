#include "embed.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dynstring.h"

#include "sfnt.h"

// from embed_sfnt.c
EMB_RIGHT_TYPE emb_otf_get_rights(OTF_FILE *otf);
const char *emb_otf_get_fontname(OTF_FILE *otf);
EMB_PDF_FONTWIDTHS *emb_otf_get_pdf_widths(OTF_FILE *otf,const unsigned short *encoding,int len,const BITSET glyphs);
EMB_PDF_FONTWIDTHS *emb_otf_get_pdf_cidwidths(OTF_FILE *otf,const BITSET glyph);
int emb_otf_ps(OTF_FILE *otf,unsigned short *encoding,int len,unsigned short *to_unicode,OUTPUT_FN output,void *context);

void emb_otf_get_pdf_fontdescr(OTF_FILE *otf,EMB_PDF_FONTDESCR *ret);

static inline int copy_file(FILE *f,OUTPUT_FN output,void *context) // {{{
{
  assert(f);
  assert(output);

  char buf[4096];
  int iA,ret=0;

  ret=0;
  rewind(f);
  do {
    iA=fread(buf,1,4096,f);
    (*output)(buf,iA,context);
    ret+=iA;
  } while (iA>0);
  return ret;
}
// }}}

EMB_PARAMS *emb_new(FONTFILE *font,EMB_DESTINATION dest,EMB_CONSTRAINTS mode) // {{{
{
  assert(font);

  EMB_PARAMS *ret=calloc(1,sizeof(EMB_PARAMS));
  if (!ret) {
    fprintf(stderr,"Bad alloc: %m\n");
    if (mode&EMB_C_TAKE_FONTFILE) {
      fontfile_close(font);
    }
    return NULL;
  }
  ret->dest=dest;
  ret->font=font;
  if (mode&EMB_C_TAKE_FONTFILE) {
    ret->plan|=EMB_A_CLOSE_FONTFILE;
  }

  // check parameters
  if ( (mode&EMB_C_KEEP_T1)&&(mode&EMB_C_FORCE_MULTIBYTE) ) {
    fprintf(stderr,"Incompatible mode: KEEP_T1 and FORCE_MULTIBYTE\n");
    emb_close(ret);
    return NULL;
  }
  if ((mode&0x07)>5) {
    fprintf(stderr,"Bad subset specification\n");
    emb_close(ret);
    return NULL;
  }

  // determine intype
  int numGlyphs=0;
  if (font->sfnt) {
    ret->intype=EMB_INPUT_TTF; // for now
    ret->rights=emb_otf_get_rights(ret->font->sfnt);
    numGlyphs=ret->font->sfnt->numGlyphs; // TODO
  } else if (font->stdname) {
    ret->intype=EMB_INPUT_STDFONT;
    ret->rights=EMB_RIGHT_NONE;
  } else {
    assert(0);
  }
/*
  if ( (ret->intype==EMB_INPUT_CFF)&&
       (ret->cffFont.is_cid()) ) {
    ret->plan|=EMB_A_MULTIBYTE;
  }
*/

  // determine outtype
  if ( (ret->intype==EMB_INPUT_T1)&&(mode&EMB_C_KEEP_T1) ) {
    ret->outtype=EMB_OUTPUT_T1;
  } else if (ret->intype==EMB_INPUT_TTF) {
    if (mode&EMB_C_PDF_OT) {
      ret->outtype=EMB_OUTPUT_SFNT;
    } else {
      ret->outtype=EMB_OUTPUT_TTF;
    }
  } else if (ret->intype==EMB_INPUT_STDFONT) {
    // the stdfonts are treated as Type1 for now
    ret->outtype=EMB_OUTPUT_T1;
    if (mode&EMB_C_FORCE_MULTIBYTE) {
      fprintf(stderr,"Multibyte stdfonts are not possible\n");
      emb_close(ret);
      return NULL;
    }
    return ret; // never subset
  } else { // T1, OTF, CFF
    if (ret->intype==EMB_INPUT_T1) {
      ret->plan|=EMB_A_CONVERT_CFF;
    }
    if (mode&EMB_C_PDF_OT) {
      ret->outtype=EMB_OUTPUT_SFNT;
      ret->plan|=EMB_A_WRAP_SFNT;
    } else {
      ret->outtype=EMB_OUTPUT_CFF;
    }
  }

  if (mode&EMB_C_FORCE_MULTIBYTE) {
    ret->plan|=EMB_A_MULTIBYTE;
  }

  // check rights
  if (  (ret->rights&EMB_RIGHT_NONE)||
        (ret->rights&EMB_RIGHT_BITMAPONLY)||
        ( (ret->rights&EMB_RIGHT_READONLY)&&(mode&EMB_C_EDITABLE_SUBSET) )||
        ( (ret->rights&EMB_RIGHT_NO_SUBSET)&&(mode&EMB_C_MUST_SUBSET) )  ) {
    fprintf(stderr,"The font does not permit the requested embedding\n");
    emb_close(ret);
    return NULL;
  } else if ( (!(ret->rights&EMB_RIGHT_NO_SUBSET))&&
              (!(mode&EMB_C_NEVER_SUBSET)) ) {
    ret->plan|=EMB_A_SUBSET;
  }

  // alloc subset
  if (ret->plan&EMB_A_SUBSET) {
    ret->subset=bitset_new(numGlyphs);
    if (!ret->subset) {
      fprintf(stderr,"Bad alloc: %m\n");
      emb_close(ret);
      return NULL;
    }
  }

  return ret;
}
// }}}

int emb_embed(EMB_PARAMS *emb,OUTPUT_FN output,void *context) // {{{
{
  assert(emb);

  if (emb->dest==EMB_DEST_PS) {
    int ret=0;
    const char *fontname=emb_otf_get_fontname(emb->font->sfnt); // TODO!!
    (*output)("%%BeginFont: ",13,context);
    (*output)(fontname,strlen(fontname),context);
    (*output)("\n",1,context);
    if (emb->intype==EMB_INPUT_TTF) {
      // do Type42
      ret=emb_otf_ps(emb->font->sfnt,NULL,4,NULL,output,context); // TODO?
    } else {
      assert(0);
      ret=-1;
    }
    (*output)("%%EndFont\n",10,context);
    return ret;
  }

  if (emb->intype==EMB_INPUT_TTF) {
    assert(emb->font->sfnt);
    if (emb->plan&EMB_A_SUBSET) {
      return otf_subset(emb->font->sfnt,emb->subset,output,context);
    } else if (emb->font->sfnt->numTTC) { // 
      return otf_ttc_extract(emb->font->sfnt,output,context);
    } else {
      // copy verbatim
      return copy_file(emb->font->sfnt->f,output,context);
    }
  } else {
    fprintf(stderr,"NOT IMPLEMENTED\n");
    assert(0);
    return -1;
  }
}
// }}}

void emb_close(EMB_PARAMS *emb) // {{{
{
  if (emb) {
    free(emb->subset);
    if (emb->plan&EMB_A_CLOSE_FONTFILE) {
      fontfile_close(emb->font);
    }
    free(emb);
  }
}
// }}}

/*** PDF out stuff ***/
static const int emb_pdf_font_format[]={0,1,0,0}; // {{{ (input_format) }}}

static const char *emb_pdf_font_subtype[][2]={ // {{{ (format,multibyte)
        {"Type1","CIDFontType0"},
        {"TrueType","CIDFontType2"}};
// }}}

static const char *emb_pdf_fontfile_key[]={ // {{{ (output_format)
        "FontFile1","FontFile2","FontFile3","Fontfile3"};
// }}}

static const char *emb_pdf_fontfile_subtype[][2]={ // {{{ (output_format,multibyte)
        {NULL,NULL},
        {NULL,NULL},
        {"Type1C","CIDFontType0C"},
        {"OpenType","OpenType"}};
// }}}

static inline int emb_multibyte(EMB_PARAMS *emb) // {{{
{
  return (emb->plan&EMB_A_MULTIBYTE)?1:0;
}
// }}}

static const char *emb_pdf_escape_name(const char *name,int len) // {{{ // - statically allocated buffer
{
  assert(name);
  if (len==-1) {
    len=strlen(name);
  }
  assert(len<=127); // pdf implementation limit

  static char buf[128*3];
  int iA,iB;
  const char hex[]="0123456789abcdef";

  for (iA=0,iB=0;iA<len;iA++,iB++) {
    if ( ((unsigned char)name[iA]<33)||((unsigned char)name[iA]>126)||
         (strchr("#()<>[]{}/%",name[iA])) ) {
      buf[iB]='#';
      buf[++iB]=hex[(name[iA]>>4)&0x0f];
      buf[++iB]=hex[name[iA]&0xf];
    } else {
      buf[iB]=name[iA];
    }
  }
  buf[iB]=0;
  return buf;
}
// }}}

const char *emb_pdf_get_font_subtype(EMB_PARAMS *emb) // {{{
{
  assert(emb);
  return emb_pdf_font_subtype[emb_pdf_font_format[emb->intype]][emb_multibyte(emb)];
}
// }}}

const char *emb_pdf_get_fontfile_key(EMB_PARAMS *emb) // {{{
{
  assert(emb);
  return emb_pdf_fontfile_key[emb->outtype];
}
// }}}

const char *emb_pdf_get_fontfile_subtype(EMB_PARAMS *emb) // {{{
{
  assert(emb);
  return emb_pdf_fontfile_subtype[emb->outtype][emb_multibyte(emb)];
}
// }}}

// {{{ EMB_PDF_FONTDESCR *emb_pdf_fd_new(fontname,subset_tag,cid_registry,cid_ordering,cid_supplement,panose)
EMB_PDF_FONTDESCR *emb_pdf_fd_new(const char *fontname,
                                  const char *subset_tag,
                                  const char *cid_registry, // or supplement==-1
                                  const char *cid_ordering, // or supplement==-1
                                  int cid_supplement) // -1 for non-cid
{
  assert(fontname);
  EMB_PDF_FONTDESCR *ret;

  int len=sizeof(EMB_PDF_FONTDESCR);
  if (subset_tag) {
    assert(strlen(subset_tag)==6);
    len+=7;
  }
  len+=strlen(fontname)+1;
  if (cid_supplement>=0) { // cid font
    len+=12; // space for panose
    assert(cid_registry);
    assert(cid_ordering);
    len+=strlen(cid_registry)+1;
    len+=strlen(cid_ordering)+1;
  }
  ret=calloc(1,len);
  if (!ret) {
    fprintf(stderr,"Bad alloc: %m\n");
    assert(0);
    return NULL;
  }

  // now fill the struct
  len=0;
  if (cid_supplement>=0) { // free space for panose is at beginning
    len+=12;
  }
  ret->fontname=ret->data+len;
  len+=strlen(fontname)+1;
  if (subset_tag) {
    strncpy(ret->fontname,subset_tag,6);
    ret->fontname[6]='+';
    strcpy(ret->fontname+7,fontname);
    len+=7;
  } else {
    strcpy(ret->fontname,fontname);
  }
  ret->italicAngle=90;
  if (cid_supplement>=0) {
    ret->registry=ret->data+len;
    strcpy(ret->registry,cid_registry);
    len+=strlen(cid_registry)+1;

    ret->ordering=ret->data+len;
    strcpy(ret->ordering,cid_ordering);
    len+=strlen(cid_registry)+1;
  }
  ret->supplement=cid_supplement;

  return ret;
}
// }}}

EMB_PDF_FONTDESCR *emb_pdf_fontdescr(EMB_PARAMS *emb) // {{{ -  to be freed by user
{
  assert(emb);

  const char *subset_tag=NULL;
  // {{{ generate pdf subtag
  static unsigned int rands=0;
  if (!rands) {
    rands=time(NULL);
  }
  
  char subtag[7];
  subtag[6]=0;
  if (emb->plan&EMB_A_SUBSET) {
    int iA;
    for (iA=0;iA<6;iA++) {
      const int x=(int)(26.0*(rand_r(&rands)/(RAND_MAX+1.0)));
      subtag[iA]='A'+x;
    }
    subset_tag=subtag;
  }
  // }}}

  const char *fontname=NULL;
  if (emb->intype==EMB_INPUT_TTF) {
    assert(emb->font->sfnt);
    fontname=emb_otf_get_fontname(emb->font->sfnt);
  } else if (emb->intype==EMB_INPUT_STDFONT) {
    return NULL;
  } else {
    fprintf(stderr,"NOT IMPLEMENTED\n");
    assert(0);
    return NULL;
  }

  EMB_PDF_FONTDESCR *ret;
  if (emb->plan&EMB_A_MULTIBYTE) { // multibyte
    ret=emb_pdf_fd_new(fontname,subset_tag,"Adobe","Identity",0); // TODO /ROS
  } else {
    ret=emb_pdf_fd_new(fontname,subset_tag,NULL,NULL,-1);
  }
  if (!ret) {
    return NULL;
  }

  if (emb->intype==EMB_INPUT_TTF) {
    emb_otf_get_pdf_fontdescr(emb->font->sfnt,ret);
  } else {
    assert(0); 
  }
  return ret;
}
// }}}

// TODO: encoding into EMB_PARAMS
EMB_PDF_FONTWIDTHS *emb_pdf_fw_new(int datasize); 

EMB_PDF_FONTWIDTHS *emb_pdf_fw_new(int datasize) // {{{
{
  assert(datasize>=0);
  EMB_PDF_FONTWIDTHS *ret=calloc(1,sizeof(EMB_PDF_FONTWIDTHS)+datasize*sizeof(int));
  if (!ret) {
    fprintf(stderr,"Bad alloc: %m\n");
    assert(0);
    return NULL;
  }
  return ret;
}
// }}}

EMB_PDF_FONTWIDTHS *emb_pdf_fontwidths(EMB_PARAMS *emb) // {{{
{
  assert(emb);

  if (emb->intype==EMB_INPUT_TTF) {
    assert(emb->font->sfnt);
    if (emb->plan&EMB_A_MULTIBYTE) {
      return emb_otf_get_pdf_cidwidths(emb->font->sfnt,emb->subset);
    } else {
      return emb_otf_get_pdf_widths(emb->font->sfnt,NULL,emb->font->sfnt->numGlyphs,emb->subset); // TODO: encoding
    }
  } else {
    fprintf(stderr,"NOT IMPLEMENTED\n");
    assert(0);
    return NULL;
  }
}
// }}}

#define NEXT /* {{{ */ \
  if ( (len<0)||(len>=size) ) { \
    assert(0); \
    free(ret); \
    return NULL; \
  } \
  pos+=len; \
  size-=len; /* }}} */ 

char *emb_pdf_simple_fontdescr(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,int fontfile_obj_ref) // {{{ - to be freed by user
{
  assert(emb);
  assert(fdes);

  char *ret=NULL,*pos;
  int len,size;

  size=300;
  pos=ret=malloc(size);
  if (!ret) {
    fprintf(stderr,"Bad alloc: %m\n");
    return NULL;
  }

  len=snprintf(pos,size,
               "<</Type /FontDescriptor\n"
               "  /FontName /%s\n" // TODO? handle quoting in struct?
               "  /Flags %d\n"
               "  /ItalicAngle %d\n",
               emb_pdf_escape_name(fdes->fontname,-1),
               fdes->flags,
               fdes->italicAngle);
  NEXT;

  if (1) { // TODO type!=EMB_PDF_TYPE3
    len=snprintf(pos,size,
                 "  /FontBBox [%d %d %d %d]\n"
                 "  /Ascend %d\n"
                 "  /Descend %d\n"
                 "  /CapHeight %d\n" // if font has Latin chars
                 "  /StemV %d\n",
                 fdes->bbxmin,fdes->bbymin,fdes->bbxmax,fdes->bbymax,
                 fdes->ascend,
                 fdes->descend,
                 fdes->capHeight,
                 fdes->stemV);
    NEXT;
  }
  if (fdes->xHeight) {
    len=snprintf(pos,size,"  /XHeight %d\n",fdes->xHeight);
    NEXT;
  }
  if (fdes->avgWidth) {
    len=snprintf(pos,size,"  /AvgWidth %d\n",fdes->avgWidth);
    NEXT;
  }
  if (fdes->panose) {
    int iA;
    len=snprintf(pos,size,"  /Style << /Panose <");
    NEXT;
    if (size<30) {
      assert(0);
      free(ret);
      return NULL;
    }
    for (iA=0;iA<12;iA++) {
      snprintf(pos+iA*2,size-iA*2,"%02x",fdes->panose[iA]);
    }
    size-=24;
    pos+=24;
    len=snprintf(pos,size,"> >>\n");
    NEXT;
  }
  len=snprintf(pos,size,
               "  /%s %d 0 R\n"
               ">>\n",
               emb_pdf_get_fontfile_key(emb),
               fontfile_obj_ref);
  NEXT;

  return ret;
}
// }}}

char *emb_pdf_simple_font(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,EMB_PDF_FONTWIDTHS *fwid,int fontdescr_obj_ref) // {{{ - to be freed by user
{
  assert(emb);
  assert(fdes);
  assert(fwid);

  int iA,iB;
  DYN_STRING ret;

  if (dyn_init(&ret,500)==-1) {
    return NULL;
  }

  dyn_printf(&ret,"<</Type /Font\n"
                  "  /Subtype /%s\n"
                  "  /BaseFont /%s\n"
                  "  /FontDescriptor %d 0 R\n",
                  emb_pdf_get_font_subtype(emb),
                  emb_pdf_escape_name(fdes->fontname,-1),
                  fontdescr_obj_ref);

  if (emb->plan&EMB_A_MULTIBYTE) { // multibyte
    assert(fwid->warray);
    dyn_printf(&ret,"  /CIDSystemInfo <<\n"
                    "    /Registry (%s)\n"
                    "    /Ordering (%s)\n"
                    "    /Supplement %d\n"
                    "  >>\n"
                    "  /DW %d\n",
//                    "  /CIDToGIDMap /Id...\n" // TrueType only, default /Identity
                    fdes->registry,
                    fdes->ordering,
                    fdes->supplement,
                    fwid->default_width);

    if (fwid->warray[0]) {
      dyn_printf(&ret,"  /W [");
      for (iA=0;fwid->warray[iA];) {
        if (fwid->warray[iA]<0) { // c1 (c1-len) w
          dyn_printf(&ret," %d %d %d",
                          fwid->warray[iA+1],
                          fwid->warray[iA+1]-fwid->warray[iA],
                          fwid->warray[iA+2]);
          iA+=3;
        } else { // c [w ... w]
          iB=fwid->warray[iA++]; // len
          dyn_printf(&ret," %d [",fwid->warray[iA++]); // c
          for (;iB>0;iB--) {
            dyn_printf(&ret," %d",fwid->warray[iA++]);
          }
          dyn_printf(&ret,"]");
        }
      }
      dyn_printf(&ret,"]\n");
    }
  } else { // "not std14"
    assert(fwid->widths);
    dyn_printf(&ret,
                    "  /Encoding /MacRomanEncoding\n"  // optional; TODO!!!!!
//                    "  /ToUnicode ?\n"  // optional
                    "  /FirstChar %d\n"
                    "  /LastChar %d\n"
                    "  /Widths [",
                    fwid->first,
                    fwid->last);
    for (iA=0,iB=fwid->first;iB<=fwid->last;iA++,iB++) {
      dyn_printf(&ret," %d",fwid->widths[iA]);
    }
    dyn_printf(&ret,"]\n");
  }
  dyn_printf(&ret,">>\n");
  if (ret.len==-1) {
    dyn_free(&ret);
    assert(0);
    return NULL;
  }

  return ret.buf;
}
// }}}

char *emb_pdf_simple_cidfont(EMB_PARAMS *emb,const char *fontname,int descendant_obj_ref) // {{{ - to be freed by user
{
  assert(emb);
  assert(fontname);

  char *ret=NULL,*pos;
  int len,size;

  size=200;
  pos=ret=malloc(size);
  if (!ret) {
    fprintf(stderr,"Bad alloc: %m\n");
    return NULL;
  }
  
  len=snprintf(pos,size,
               "<</Type /Font\n"
               "  /Subtype /Type0\n"
               "  /BaseFont /%s\n"  // TODO? "-CMap-name"(/Encoding) for CidType0
               "  /Encoding /Identity-H\n"
         // for CFF: one of:
         // UniGB-UCS2-H, UniCNS-UCS2-H, UniJIS-UCS2-H, UniKS-UCS2-H
               "  /DescendantFonts [%d 0 R]\n",
//               "  /ToUnicode ?\n" // TODO
               emb_pdf_escape_name(fontname,-1),
               descendant_obj_ref);
  NEXT;

  len=snprintf(pos,size,">>\n");
  NEXT;

  return ret;
}
// }}}

char *emb_pdf_simple_stdfont(EMB_PARAMS *emb) // {{{ - to be freed by user
{
  assert(emb);
  assert(emb->font->stdname);

  char *ret=NULL,*pos;
  int len,size;

  size=300;
  pos=ret=malloc(size);
  if (!ret) {
    fprintf(stderr,"Bad alloc: %m\n");
    return NULL;
  }

  len=snprintf(pos,size,
               "<</Type/Font\n"
               "  /Subtype /Type1\n"
               "  /BaseFont /%s\n"
               ">>\n",
               emb->font->stdname);
  NEXT;

  return ret;
}
// }}}
#undef NEXT

//...


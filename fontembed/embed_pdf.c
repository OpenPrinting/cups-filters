#include "embed.h"
#include "embed_pdf.h" // already included fron embed.h ...
#include "embed_pdf_int.h"
#include "embed_sfnt_int.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "frequent.h"

// NOTE: these must be in sync with the EMB_FORMAT enum
static const char *emb_pdf_font_subtype[][2]={ // {{{ (output_format,multibyte)
        {"Type1",NULL},
        {"TrueType","CIDFontType2"},
        {"Type1","CIDFontType0"},
        {"Type1","CIDFontType0"},
        {"Type1",NULL}};
// }}}

static const char *emb_pdf_fontfile_key[]={ // {{{ (output_format)
        "FontFile","FontFile2","FontFile3","FontFile3",NULL};
// }}}

// ... PDF1.6 here
static const char *emb_pdf_fontfile_subtype[][2]={ // {{{ (output_format,multibyte)
        {NULL,NULL},
        {NULL,NULL},
        {"OpenType","OpenType"},
        {"Type1C","CIDFontType0C"},
        {NULL,NULL}};
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

// this is in the font dict
const char *emb_pdf_get_font_subtype(EMB_PARAMS *emb) // {{{
{
  assert(emb);
  return emb_pdf_font_subtype[emb->outtype][emb_multibyte(emb)];
}
// }}}

// in font descriptor
const char *emb_pdf_get_fontfile_key(EMB_PARAMS *emb) // {{{
{
  assert(emb);
  return emb_pdf_fontfile_key[emb->outtype];
}
// }}}

// this is what to put in the font-stream dict
const char *emb_pdf_get_fontfile_subtype(EMB_PARAMS *emb) // {{{
{
  assert(emb);
  return emb_pdf_fontfile_subtype[emb->outtype][emb_multibyte(emb)];
}
// }}}

// {{{ static EMB_PDF_FONTDESCR *emb_pdf_fd_new(fontname,subset_tag,cid_registry,cid_ordering,cid_supplement,panose)
static EMB_PDF_FONTDESCR *emb_pdf_fd_new(const char *fontname,
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
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
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
  if ( (emb->intype==EMB_FMT_TTF)||(emb->intype==EMB_FMT_OTF) ) { // TODO? use fontinfo from CFF when outtype==CFT, etc.?
    assert(emb->font->sfnt);
    fontname=emb_otf_get_fontname(emb->font->sfnt);
  } else if (emb->outtype==EMB_FMT_STDFONT) {
    return NULL;
  } else {
    fprintf(stderr,"NOT IMPLEMENTED\n");
    assert(0);
    return NULL;
  }

  EMB_PDF_FONTDESCR *ret;
  if (emb->plan&EMB_A_MULTIBYTE) { // multibyte
    ret=emb_pdf_fd_new(fontname,subset_tag,"Adobe","Identity",0); // TODO other /ROS ?
  } else {
    ret=emb_pdf_fd_new(fontname,subset_tag,NULL,NULL,-1);
  }
  if (!ret) {
    return NULL;
  }

  if ( (emb->intype==EMB_FMT_TTF)||(emb->intype==EMB_FMT_OTF) ) {
    emb_otf_get_pdf_fontdescr(emb->font->sfnt,ret);
  } else {
    assert(0);
  }
  return ret;
}
// }}}

EMB_PDF_FONTWIDTHS *emb_pdf_fw_new(int datasize) // {{{
{
  assert(datasize>=0);
  EMB_PDF_FONTWIDTHS *ret=calloc(1,sizeof(EMB_PDF_FONTWIDTHS)+datasize*sizeof(int));
  if (!ret) {
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
    assert(0);
    return NULL;
  }
  return ret;
}
// }}}

// if default_width==-1: default_width will be estimated
EMB_PDF_FONTWIDTHS *emb_pdf_fw_cidwidths(const BITSET glyphs,int len,int default_width,int (*getGlyphWidth)(void *context,int gid),void *context) // {{{ glyphs==NULL -> output all
{
  assert(getGlyphWidth);

  FREQUENT *freq=NULL;
  if (default_width<0) {
    freq=frequent_new(3);
  }

  int iA,b,c;
  int size=0,in_region=0; // current number of elements in after region start

  // first pass: find continuous regions, calculate needed size, estimate dw
  for (iA=0,b=0,c=1;iA<len;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }
    if ( (!glyphs)||(glyphs[b]&c) ) {
      if (freq) {
        const int w=(*getGlyphWidth)(context,iA);
        frequent_add(freq,w);
      }
      if (in_region) {
        in_region++;
      } else { // start new region
        size+=2; // len c
        in_region=1;
      }
    } else { // region end
      size+=in_region;
      in_region=0;
    }
  }
  size+=in_region;

  if (freq) {
    default_width=frequent_get(freq,0);
    free(freq);
  }
  assert(default_width>0);

  // now create the array
  EMB_PDF_FONTWIDTHS *ret=emb_pdf_fw_new(size+1);
  if (!ret) {
    return NULL;
  }
  ret->default_width=default_width;
  ret->warray=ret->data;

  // second pass
  in_region=0;
  size=0;
  int *rlen=0; // position of current len field  (only valid if in_region!=0)
  for (iA=0,b=0,c=1;iA<len;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }
    if ( (!glyphs)||(glyphs[b]&c) ) {
      const int w=(*getGlyphWidth)(context,iA);
      if (in_region>0) { // in array region
        if ( (w==default_width)&&(ret->warray[size-1]==default_width) ) { // omit this and prev entry
          size--;
          *rlen=in_region-1; // !=0, as it does not start with >default_width
          in_region=0; // end region, immediate restart will take just the same amount of space
        } else if ( (in_region>=4)&&
                    (ret->warray[size-1]==w)&&(ret->warray[size-2]==w)&&
                    (ret->warray[size-3]==w)&&(ret->warray[size-4]==w) ) {
          // five in a row.  c1 c2 w [l c] is equally short and can be extended (-len c1 w)  [w/ cost of array-region restart]
          if (in_region==4) { // completely replace
            size-=6;
          } else { // first end previous region
            size-=4;
            *rlen=in_region-4;
          }
          in_region=-4; // start range region instead
          rlen=&ret->warray[size++];
          ret->warray[size++]=iA-4;
          ret->warray[size++]=w;
        } else { // just add
          in_region++;
          ret->warray[size++]=w;
        }
        continue;
      } else if (in_region<0) { // in range region
        if (ret->warray[size-1]==w) {
          in_region--; // just add
          continue;
        }
        *rlen=in_region; // end
        in_region=0;
      }
      if (w!=default_width) { // start new array region
        in_region=1;
        rlen=&ret->warray[size++];
        ret->warray[size++]=iA; // c
        ret->warray[size++]=w;
      }
    } else if (in_region) {
      // TODO? no need to stop range region? } else if (in_region<0) { inregion--; }
      *rlen=in_region;
      in_region=0;
    }
  }
  if (in_region) {
    *rlen=in_region;
  }
  ret->warray[size]=0; // terminator
  return ret;
}
// }}}

// TODO: encoding into EMB_PARAMS  (emb_new_enc(...,encoding,len ,to_unicode));
//   -> will then change interpretation of BITSET...(?really?); can we allow dynamic encoding map generation?
//   -> encoding has a "len";  len<256
EMB_PDF_FONTWIDTHS *emb_pdf_fontwidths(EMB_PARAMS *emb) // {{{
{
  assert(emb);

  if ( (emb->intype==EMB_FMT_TTF)||(emb->intype==EMB_FMT_OTF) ) {
    assert(emb->font->sfnt);
    if (emb->plan&EMB_A_MULTIBYTE) {
      return emb_otf_get_pdf_cidwidths(emb->font->sfnt,emb->subset);
    } else {
      return emb_otf_get_pdf_widths(emb->font->sfnt,/*encoding*/NULL,emb->font->sfnt->numGlyphs,emb->subset); // TODO: encoding
    }
  } else {
    fprintf(stderr,"NOT IMPLEMENTED\n");
    assert(0);
    return NULL;
  }
}
// }}}

/*** PDF out stuff ***/
#include "dynstring.h"

#define NEXT /* {{{ */ \
  if ( (len<0)||(len>=size) ) { \
    assert(0); \
    free(ret); \
    return NULL; \
  } \
  pos+=len; \
  size-=len; /* }}} */

// TODO? /CIDSet    TODO... /FontFamily /FontStretch /FontWeight (PDF1.5?) would be nice...
char *emb_pdf_simple_fontdescr(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,int fontfile_obj_ref) // {{{ - to be freed by user
{
  assert(emb);
  assert(fdes);

  char *ret=NULL,*pos;
  int len,size;

  size=300;
  pos=ret=malloc(size);
  if (!ret) {
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
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
                 "  /Ascent %d\n"
                 "  /Descent %d\n"
                 "  /CapHeight %d\n" // if font has Latin chars
                 "  /StemV %d\n",
                 fdes->bbxmin,fdes->bbymin,fdes->bbxmax,fdes->bbymax,
                 fdes->ascent,
                 fdes->descent,
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
  // TODO (for Type0)? CIDSet  -> simply our glyphs BITSET  (ok. endianess?)
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
//                    "  /CIDToGIDMap /Id...\n" // TrueType only, default /Identity  [optional?  which PDF version says what?]
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

// TODO? + encoding as param?  TODO + ToUnicode cmap    => we need another struct EMB_PDF_FONTMAP
// (TODO?? fontname here without subset-tag [_some_ pdfs out there seem to be that way])
// TODO? don't do the CidType0 check here?
// NOTE: this is _additionally_ to emb_pdf_simple_font()!
char *emb_pdf_simple_cidfont(EMB_PARAMS *emb,const char *fontname,int descendant_obj_ref) // {{{ - to be freed by user
{
  assert(emb);
  assert(fontname);

  char *ret=NULL,*pos;
  int len,size;

  size=250;
  pos=ret=malloc(size);
  if (!ret) {
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
    return NULL;
  }
     // for CFF: one of:
     // UniGB-UCS2-H, UniCNS-UCS2-H, UniJIS-UCS2-H, UniKS-UCS2-H
  const char *encoding="Identity-H",*addenc="-";
  if (emb->outtype==EMB_FMT_TTF) { // !=CidType0
    addenc="";
  }

  len=snprintf(pos,size,
               "<</Type /Font\n"
               "  /Subtype /Type0\n"
               "  /BaseFont /%s%s%s\n"
               "  /Encoding /%s\n"
               "  /DescendantFonts [%d 0 R]\n",
//               "  /ToUnicode ?\n" // TODO
               emb_pdf_escape_name(fontname,-1),
               addenc,((addenc[0])?encoding:""),
               encoding,
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
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
    return NULL;
  }

  len=snprintf(pos,size,
               "<</Type/Font\n"
               "  /Subtype /Type1\n"
               "  /BaseFont /%s\n"
               ">>\n",
//               emb_pdf_get_font_subtype(emb),
               emb->font->stdname);
  NEXT;

  return ret;
}
// }}}
#undef NEXT


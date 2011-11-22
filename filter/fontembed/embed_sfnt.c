#include "embed.h"
#include "sfnt.h"
#include "sfnt_int.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// from embed.c
EMB_PDF_FONTWIDTHS *emb_pdf_fw_new(int datasize);

EMB_RIGHT_TYPE emb_otf_get_rights(OTF_FILE *otf) // {{{
{
  EMB_RIGHT_TYPE ret=EMB_RIGHT_FULL;

  int len;
  char *os2=otf_get_table(otf,OTF_TAG('O','S','/','2'),&len);
  if (os2) {
    const unsigned short os2_version=get_USHORT(os2);
    // check len
    assert( (os2_version!=0x0000)||(len==78) );
    assert( (os2_version!=0x0001)||(len==86) );
    assert( (os2_version<0x0002)||(os2_version>0x0004)||(len==96) );
    if (os2_version<=0x0004) {
      // get rights
      unsigned short fsType=get_USHORT(os2+8);
      ret=fsType&0x0200;
      if ((fsType&0x0f)==0x0002) {
        ret|=EMB_RIGHT_NONE;
      } else if ((fsType&0x0f)==0x0004) {
        ret|=EMB_RIGHT_READONLY;
      } 
    }
    free(os2);
  }
  return ret;
}
// }}}

// NOTE: statically allocated buffer
const char *emb_otf_get_fontname(OTF_FILE *otf) // {{{ 
{
  static char fontname[64];

  int len;
  const char *fname=otf_get_name(otf,3,1,0x409,6,&len); // microsoft
  if (fname) {
    int iA,iB=0;
    for (iA=0;(iA<63)&&(iA*2<len);iA++) {
      if ( (fname[2*iA]==0)&&
           (fname[2*iA+1]>=33)&&(fname[2*iA+1]<=126)&&
           (!strchr("[](){}<>/%",fname[iA*2+1])) ) {
        fontname[iB++]=fname[iA*2+1];
      }
    }
    fontname[iB]=0;
  } else if ((fname=otf_get_name(otf,1,0,0,6,&len))) { // mac
    int iA,iB=0;
    for (iA=0;(iA<63)&&(iA<len);iA++) {
      if ( (fname[iA]>=33)&&(fname[iA]<=126)&&
           (!strchr("[](){}<>/%",fname[iA])) ) {
        fontname[iB++]=fname[iA];
      }
    }
    fontname[iB]=0;
  } else {
    fontname[0]=0;
  }
  if (!*fontname) {
    // TODO construct a fontname, eg from */*/*/4
    fprintf(stderr,"WARNING: no fontName\n");
  }
  return fontname;
}
// }}}

void emb_otf_get_pdf_fontdescr(OTF_FILE *otf,EMB_PDF_FONTDESCR *ret) // {{{
{
  int len;

//  TODO
//  ... fill in struct
  char *head=otf_get_table(otf,OTF_TAG('h','e','a','d'),&len);
  assert(head); // version is 1.0 from otf_load
  ret->bbxmin=get_SHORT(head+36)*1000/otf->unitsPerEm;
  ret->bbymin=get_SHORT(head+38)*1000/otf->unitsPerEm;
  ret->bbxmax=get_SHORT(head+40)*1000/otf->unitsPerEm;
  ret->bbymax=get_SHORT(head+42)*1000/otf->unitsPerEm;
  const int macStyle=get_USHORT(head+44);
  free(head);

  char *post=otf_get_table(otf,OTF_TAG('p','o','s','t'),&len);
  assert(post);
  const unsigned int post_version=get_ULONG(post);
  // check length
  assert( (post_version!=0x00010000)||(len==32) );
  assert( (post_version!=0x00020000)||(len>=34+2*otf->numGlyphs) );
  assert( (post_version!=0x00025000)||(len==35+otf->numGlyphs) );
  assert( (post_version!=0x00030000)||(len==32) );
  assert( (post_version!=0x00020000)||(get_USHORT(post+32)==otf->numGlyphs) );
//  assert( (post_version==0x00030000)==(!!(otf->flags&OTF_F_FMT_CFF)) ); // ghostscript embedding does this..
  // TODO: v4 (apple) :  uint16 reencoding[numGlyphs]
  if ( (post_version==0x00010000)||
       (post_version==0x00020000)||
       (post_version==0x00025000)||
       (post_version==0x00030000) ) {
    ret->italicAngle=get_LONG(post+4)>>16;
    if (get_ULONG(post+12)>0) { // monospaced
      ret->flags|=1;
    }
  } else {
    fprintf(stderr,"WARNING: no italicAngle, no monospaced flag\n");
  }
  free(post);

  char *os2=otf_get_table(otf,OTF_TAG('O','S','/','2'),&len);
  if (os2) {
    const unsigned short os2_version=get_USHORT(os2);
    // check len
    assert( (os2_version!=0x0000)||(len==78) );
    assert( (os2_version!=0x0001)||(len==86) );
    assert( (os2_version<0x0002)||(os2_version>0x0004)||(len==96) );
    if (os2_version<=0x0004) {

      // from PDF14Deltas.pdf, pg 113
      const int weightClass=get_USHORT(os2+4);
      ret->stemV=50+weightClass*weightClass/(65*65); // TODO, really bad
//printf("a %d\n",weightClass);

      if (ret->supplement>=0) { // cid
        ret->panose=ret->data;
        memcpy(ret->panose,os2+30,12); // sFamilyClass + panose
      }
      const unsigned short fsSelection=get_USHORT(os2+62);
      if (fsSelection&0x01) { // italic
        ret->flags|=0x0040;
      }
      if ( (fsSelection&0x10)&&(weightClass>600) ) { // force bold
        ret->flags|=0x0400;
      }
      const unsigned char family_class=get_USHORT(os2+30)>>8;
      if (family_class==10) { // script
        ret->flags|=0x0008;
      }
      if (family_class!=8) { // not sans-serif
        ret->flags|=0x0002;
      }

      ret->avgWidth=get_SHORT(os2+2);
      ret->ascend=get_SHORT(os2+68)*1000/otf->unitsPerEm;
      ret->descend=get_SHORT(os2+70)*1000/otf->unitsPerEm;
      if (os2_version>=0x0002) {
        ret->xHeight=get_SHORT(os2+86)*1000/otf->unitsPerEm;
        ret->capHeight=get_SHORT(os2+88)*1000/otf->unitsPerEm;
      } // else capHeight fixed later
    } else {
      free(os2);
      os2=NULL;
    }
  } else { 
    fprintf(stderr,"WARNING: no OS/2 table\n");
    // e.g. subsetted font from ghostscript // e.g. CFF
  }
  if (os2) {
    free(os2);
  } else { // TODO (if(CFF))
    fprintf(stderr,"WARNING: no ascend/descend, capHeight, stemV, flags\n");
    if (macStyle&0x01) { // force bold - just do it on bold
      ret->flags|=0x0400;
    }
    if (macStyle&0x02) { // italic
      ret->flags|=0x0004;
    }
    //  ... flags TODO? (Serif, Script, Italic, AllCap,SmallCap, ForceBold)
  }

// ? maybe get ascend,descend,capHeight,xHeight,stemV directly from cff
  // Fallbacks
  if ( (!ret->ascend)||(!ret->descend) ) {
    char *hhea=otf_get_table(otf,OTF_TAG('h','h','e','a'),&len);
    if (hhea) {
      ret->ascend=get_SHORT(hhea+4)*1000/otf->unitsPerEm;
      ret->descend=get_SHORT(hhea+6)*1000/otf->unitsPerEm;
    }
    free(hhea);
  }
  if (!ret->stemV) { // TODO? use name
    const unsigned short d_gid=otf_from_unicode(otf,'.');
    if (d_gid) { // stemV=bbox['.'].width;
      len=otf_get_glyph(otf,d_gid);
      assert(len>=10);
      ret->stemV=(get_SHORT(otf->gly+6)-get_SHORT(otf->gly+2))*1000/otf->unitsPerEm;
    } else {
      if (macStyle&1) { // bold
        ret->stemV=165;
      } else {
        ret->stemV=109; // TODO... unserious values...
      }
    }
  }
  if (!ret->capHeight) { // TODO? only reqd. for fonts with latin...
    ret->capHeight=ret->ascend;
  }
  if (0) { // TODO? uses only adobe latin standard? ?? e.g. Type1
    ret->flags|=0x0020;
  } else {
    ret->flags|=0x0004;
  }
  // TODO SmallCap by font name(?)  

// TODO ;   ? cid ?
}
// }}}

EMB_PDF_FONTWIDTHS *emb_otf_get_pdf_widths(OTF_FILE *otf,const unsigned short *encoding,int len,const BITSET glyphs) // {{{ glyphs==NULL -> all from 0 to len
{
  assert(otf);

  int first=len,last=0;
  int iA;

  if (glyphs) {
    for (iA=0;iA<len;iA++) {
      const int gid=(encoding)?encoding[iA]:otf_from_unicode(otf,iA); // TODO
      if (bit_check(glyphs,gid)) {
        if (first>iA) {
          first=iA;
        }
        if (last<iA) {
          last=iA;
        }
      }
    }
  } else {
    first=0;
    last=len;
  }
  if (last<first) {
    // empty
    fprintf(stderr,"WARNING: empty embedding range\n");
    return NULL;
  }

  // ensure hmtx is there
  if (!otf->hmtx) {
    if (otf_load_more(otf)!=0) {
      assert(0);
      return NULL;
    }
  }

  // now create the array
  EMB_PDF_FONTWIDTHS *ret=emb_pdf_fw_new(last-first+1);
  if (!ret) {
    return NULL;
  }
  ret->first=first;
  ret->last=last;
  ret->widths=ret->data;
  for (iA=0;first<=last;iA++,first++) {
    const int gid=(encoding)?encoding[first]:otf_from_unicode(otf,first); // TODO
    if (gid>=otf->numGlyphs) {
      fprintf(stderr,"Bad glyphid\n");
      assert(0);
      free(ret);
      return NULL;
    }
    if ( (!glyphs)||(bit_check(glyphs,gid)) ) {
      ret->widths[iA]=get_width_fast(otf,gid)*1000/otf->unitsPerEm;
    } // else 0 from calloc
  }

  return ret;
}
// }}}

// TODO: split into general part and otf specific part
EMB_PDF_FONTWIDTHS *emb_otf_get_pdf_cidwidths(OTF_FILE *otf,const BITSET glyphs) // {{{ // glyphs==NULL -> output all
{
  assert(otf);

  int iA,b,c;
  int dw=otf_get_width(otf,0)*1000/otf->unitsPerEm,size=0; // also ensures otf->hmtx
  assert(dw>=0);
  // TODO? dw from  hmtx(otf->numberOfHMetrics);

  int in_array=0; // current number of elements in array mode

  // first pass
  for (iA=0,b=0,c=1;iA<otf->numGlyphs;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }
    if ( (!glyphs)||(glyphs[b]&c) ) {
      if (in_array) {
        in_array++;
      } else {
        size+=2; // len c
        in_array=1;
      }
    } else {
      size+=in_array;
      in_array=0;
    }
  }
  size+=in_array;

  // now create the array
  EMB_PDF_FONTWIDTHS *ret=emb_pdf_fw_new(size+1);
  if (!ret) {
    return NULL;
  }
  ret->default_width=dw;
  ret->warray=ret->data;

  // second pass
  in_array=0;
  size=0;
  for (iA=0,b=0,c=1;iA<otf->numGlyphs;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }
    if ( (!glyphs)||(glyphs[b]&c) ) {
      const int w=get_width_fast(otf,iA)*1000/otf->unitsPerEm;
      if ( (in_array<0)&&(ret->warray[size-1]==w) ) {
        in_array--; // just add
        ret->warray[size-3]=in_array; // fix len;
        continue;
      }
      if (in_array>0) {
        if ( (w==dw)&&(ret->warray[size-1]==dw) ) { // omit this and prev
          size--;
          in_array--; // !=0, as it does not start with >dw
          ret->warray[size-in_array-2]=in_array; // fix len
        } else if ( (in_array>=2)&&
                    (ret->warray[size-1]==w)&&
                    (ret->warray[size-2]==w) ) {
          // three in a row.  c1 c2 w is equally short
          if (in_array==2) { // completely replace
            size-=4; 
          } else {
            size-=2;
            ret->warray[size-in_array-2]=in_array; // fix len
            in_array=-2;
          }
          in_array=-2;
          ret->warray[size++]=in_array;
          ret->warray[size++]=iA-2;
          ret->warray[size++]=w;
        } else { // just add
          in_array++;
          ret->warray[size++]=w;
          ret->warray[size-in_array-2]=in_array; // fix len
        }
      } else if (w!=dw) {
        in_array=1;
        ret->warray[size++]=in_array; // len
        ret->warray[size++]=iA; // c
        ret->warray[size++]=w;
      } else { // especially for in_array<0
        in_array=0;
      }
    } else {
      in_array=0;
    }
  }
  ret->warray[size]=0; // terminator
  return ret;
}
// }}}

/*** PS stuff ***/

#include "dynstring.h"

// NOTE: statically allocated string
const char *get_glyphname(const char *post,unsigned short *to_unicode,unsigned short gid)
{
  if (gid==0) {
    return ".notdef";
  }
  /*
  ... TODO: consult post table, if there.
  ... otherwise consult fallback table
  ... otherwise generate "uni...".
  ... otherwise unique name c01...
  */
  static char ret[255];
  snprintf(ret,250,"c%d",gid);
  return ret;
}

struct OUTFILTER_PS {
  OUTPUT_FN out;
  void *ctx;
  int len;
};

static void outfilter_ascii_ps(const char *buf,int len,void *context)  // {{{
{
  struct OUTFILTER_PS *of=context;
  OUTPUT_FN out=of->out;
  int iA;

  if ((of->len/64000)!=(len*2+of->len)/64000) {
    (*out)("00>\n",4,of->ctx);
    (*out)("<",1,of->ctx);
    of->len+=5;
  }
  char tmp[256];
  while (len>0) {
    for (iA=0;(iA<40)&&(len>0);iA++,len--) {
      sprintf(tmp+2*iA,"%02x",(unsigned char)buf[iA]);
    }
    tmp[2*iA]='\n';
    (*out)(tmp,iA*2+1,of->ctx);
    of->len+=iA*2+1;
    buf+=iA;
  }
}
// }}}

static void outfilter_binary_ps(const char *buf,int len,void *context)  // {{{
{
  struct OUTFILTER_PS *of=context;
  OUTPUT_FN out=of->out;

  char tmp[100];
  const int l=sprintf(tmp,"%d RD ",len);

  (*out)(tmp,l,of->ctx);
  of->len+=l;

  (*out)(buf,len,of->ctx);
  (*out)("\n",1,of->ctx);
  of->len+=len+1;
}
// }}}

/*
  encoding:  character-code -> glyph id  ["required", NULL: identity(?)[or: from_unicode()]] // TODO: respect subsetting
  to_unicode:  character-code -> unicode  [NULL: no char names]
*/
int emb_otf_ps(OTF_FILE *otf,unsigned short *encoding,int len,unsigned short *to_unicode,OUTPUT_FN output,void *context) // {{{
{
  const int binary=0; // binary format? // TODO
  if (len>256) {
    fprintf(stderr,"Encoding too big(%d) for Type42\n",len);
    return -1;
  }
  int iA,ret=0;

  DYN_STRING ds;
  if (dyn_init(&ds,1024)==-1) {
    return -1;
  }

  int rlen=0;
  char *head=otf_get_table(otf,OTF_TAG('h','e','a','d'),&rlen);
  if (!head) {
    free(ds.buf);
    return -1;
  }
  dyn_printf(&ds,"%!PS-TrueTypeFont-%d-%d\n",
                 otf->version,get_ULONG(head+4));
  const int bbxmin=get_SHORT(head+36)*1000/otf->unitsPerEm,
            bbymin=get_SHORT(head+38)*1000/otf->unitsPerEm,
            bbxmax=get_SHORT(head+40)*1000/otf->unitsPerEm,
            bbymax=get_SHORT(head+42)*1000/otf->unitsPerEm;
  free(head);

  char *post=otf_get_table(otf,OTF_TAG('p','o','s','t'),&rlen);
  if ( (!post)&&(rlen!=-1) ) { // other error than "not found"
    free(ds.buf);
    return -1;
  }
  if (post) {
    const unsigned int minMem=get_ULONG(post+16),maxMem=get_ULONG(post+20);
    if (minMem) {
      dyn_printf(&ds,"%%VMusage: %d %d\n",minMem,maxMem);
    }
  }

  dyn_printf(&ds,"11 dict begin\n"
                 "/FontName /%s def\n"
                 "/Encoding 256 array\n"
                 "0 1 255 { 1 index exch /.notdef put } for\n",
                 emb_otf_get_fontname(otf));
  for (iA=0;iA<len;iA++) {
    const int gid=(encoding)?encoding[iA]:iA;
    dyn_printf(&ds,"dup %d /%s put\n",
                   iA,get_glyphname(post,to_unicode,gid));
  }
  dyn_printf(&ds,"readonly def\n");

  dyn_printf(&ds,"/PaintType 0 def\n"
                 "/FontMatrix [1 0 0 1 0 0] def\n"
                 "/FontBBox [%d %d %d %d] def\n"
                 "/FontType 42 def\n",
//                 "/XUID\n"  // TODO?!?
                 bbxmin,bbymin,bbxmax,bbymax);
  if (post) {
    dyn_printf(&ds,"/FontInfo 4 dict dup begin\n"
                   "  /ItalicAngle %d def\n"
                   "  /isFixedPitch %d def\n"
                   "  /UnderlinePosition %d def\n"
                   "  /UnderlineThickness %d def\n"
                   "end readonly def\n",
                   get_LONG(post+4)>>16,
                   get_ULONG(post+12),
                   (get_SHORT(post+8)-get_SHORT(post+10)/2)*1000/otf->unitsPerEm,
                   get_SHORT(post+10)*1000/otf->unitsPerEm);
  }
  if (binary) {
    dyn_printf(&ds,"/RD { string currentfile exch readstring pop } executeonly def\n");
    dyn_printf(&ds,"/sfnts[");
  } else {
    dyn_printf(&ds,"/sfnts[<");
  }

  if (ds.len<0) {
    free(post);
    free(ds.buf);
    return -1;
  }
  (*output)(ds.buf,ds.len,context);
  ret+=ds.len;
  ds.len=0;

  // {{{ copy tables verbatim
  struct _OTF_WRITE *otw;
  otw=malloc(sizeof(struct _OTF_WRITE)*otf->numTables);
  if (!otw) {
    fprintf(stderr,"Bad alloc: %m\n");
    free(post);
    free(ds.buf);
    return -1;
  }
  // just copy everything
  for (iA=0;iA<otf->numTables;iA++) {
    otw[iA].tag=otf->tables[iA].tag;
    otw[iA].action=otf_action_copy;
    otw[iA].param=otf;
    otw[iA].length=iA;
  }

  struct OUTFILTER_PS of;
  of.out=output;
  of.ctx=context;
  of.len=0;
  if (binary) {
    iA=otf_write_sfnt(otw,otf->version,otf->numTables,outfilter_binary_ps,&of);
  } else {
    iA=otf_write_sfnt(otw,otf->version,otf->numTables,outfilter_ascii_ps,&of);
  }
  free(otw);
  if (iA==-1) {
    free(post);
    free(ds.buf);
    return -1;
  }
  ret+=of.len;

  if (binary) {
    dyn_printf(&ds,"] def\n");
  } else {
    dyn_printf(&ds,">] def\n");
  }
  // }}} done copying

  const int num_chars=1;
  dyn_printf(&ds,"/CharStrings %d dict dup begin\n",num_chars);
  for (iA=0;iA<num_chars;iA++) {
    const int gid=(encoding)?encoding[iA]:iA;
    dyn_printf(&ds,"/%s %d def\n",get_glyphname(post,to_unicode,gid),gid);
// ... from cmap [respecting subsetting...]
  }
  dyn_printf(&ds,"end readonly def\n");
  dyn_printf(&ds,"FontName currentdict end definefont pop\n");
  free(post);

  if (ds.len<0) {
    free(ds.buf);
    return -1;
  }
  (*output)(ds.buf,ds.len,context);
  ret+=ds.len;
  ds.len=0;

  free(ds.buf);
  return ret; 
}
// }}}


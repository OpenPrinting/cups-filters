#include "sfnt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "sfnt_int.h"

// TODO?
// get_SHORT(head+48) // fontDirectionHint
/* reqd. Tables: cmap, head, hhea, hmtx, maxp, name, OS/2, post     .
 OTF: glyf,loca [cvt,fpgm,prep]
 */

static void otf_bsearch_params(int num, // {{{
                               int recordSize,
                               int *searchRange,
                               int *entrySelector,
                               int *rangeShift)
{
  assert(num>0);
  assert(searchRange);
  assert(entrySelector);
  assert(rangeShift);

  int iA,iB;
  for (iA=1,iB=0;iA<=num;iA<<=1,iB++) {}

  *searchRange=iA*recordSize/2;
  *entrySelector=iB-1;
  *rangeShift=num*recordSize-*searchRange;
}
// }}}

static char *otf_bsearch(char *table, // {{{
                         const char *target,int len,
                         int searchRange,
                         int entrySelector,
                         int rangeShift,
                         int lower_bound) // return lower_bound, if !=0
{
  char *ret=table+rangeShift;
  if (memcmp(target,ret,len)<0) {
    ret=table;
  }

  for (;entrySelector>0;entrySelector--) {
    searchRange>>=1;
    ret+=searchRange;
    if (memcmp(target,ret,len)<0) {
      ret-=searchRange;
    }
  }
  const int result=memcmp(target,ret,len);
  if (result==0) {
    return ret;
  } else if (lower_bound) {
    if (result>0) {
      return ret+searchRange;
    }
    return ret;
  }
  return NULL; // not found;
}
// }}}

static OTF_FILE *otf_new(FILE *f) // {{{
{
  assert(f);

  OTF_FILE *ret;
  ret=calloc(1,sizeof(OTF_FILE));
  if (ret) {
    ret->f=f;
  }
  ret->version=0x00010000;

  return ret;
}
// }}}

static char *otf_read(OTF_FILE *otf,char *buf,long pos,int length) // {{{ -  will alloc, if >buf ==NULL, returns >buf, or NULL on error
{
  char *ours=NULL;

  if (length==0) {
    return buf;
  } else if (length<0) {
    assert(0);
    return NULL;
  }

  int res=fseek(otf->f,pos,SEEK_SET);
  if (res==-1) {
    fprintf(stderr,"Seek failed: %m\n");
    return NULL;
  }

  // (+3)&~3 for checksum...
  const int pad_len=(length+3)&~3;
  if (!buf) {
    ours=buf=malloc(sizeof(char)*pad_len);
    if (!buf) {
      fprintf(stderr,"Bad alloc: %m\n");
      return NULL;
    }
  }

  res=fread(buf,1,pad_len,otf->f);
  if (res!=pad_len) {
    if (res==length) { // file size not multiple of 4, pad with zero
      memset(buf+res,0,pad_len-length);
    } else {
      fprintf(stderr,"Short read\n");
      free(ours);
      return NULL;
    }
  }
 
  return buf;
}
// }}}

static unsigned int otf_checksum(const char *buf, unsigned int len) // {{{
{
  unsigned int ret=0;

  for (len=(len+3)/4;len>0;len--,buf+=4) {
    ret+=get_ULONG(buf);
  }

  return ret;
}
// }}}


static int otf_get_ttc_start(OTF_FILE *otf,int use_ttc) // {{{
{
  char buf[4];

  if (!otf->numTTC) { // >0 if TTC...
    return 0;
  }

  int pos=0;
  if ( (use_ttc<0)||(use_ttc>=otf->numTTC)||
       (!otf_read(otf,buf,pos+12+4*use_ttc,4)) ) {
    fprintf(stderr,"Bad TTC subfont number\n");
    return -1;
  }
  return get_ULONG(buf);
}
// }}}

OTF_FILE *otf_do_load(OTF_FILE *otf,int pos) // {{{
{
  int iA;
  char buf[16];

  // {{{ read offset table
  if (otf_read(otf,buf,pos,12)) {
    otf->version=get_ULONG(buf);
    if (otf->version==0x00010000) { // 1.0 truetype
    } else if (otf->version==OTF_TAG('O','T','T','O')) { // OTF(CFF)
      otf->flags|=OTF_F_FMT_CFF;
    } else if (otf->version==OTF_TAG('t','r','u','e')) { // (old mac)
    } else if (otf->version==OTF_TAG('t','y','p','1')) { // sfnt wrapped type1
      // TODO: unsupported
    } else {
      otf_close(otf);
      otf=NULL;
    }
    pos+=12;
  } else {
    otf_close(otf);
    otf=NULL;
  }
  if (!otf) {
    fprintf(stderr,"Not a ttf font\n");
    return NULL;
  }
  otf->numTables=get_USHORT(buf+4);
  // }}}

  // {{{ read directory
  otf->tables=malloc(sizeof(OTF_DIRENT)*otf->numTables);
  if (!otf->tables) {
    fprintf(stderr,"Bad alloc: %m\n");
    otf_close(otf);
    return NULL;
  }
  for (iA=0;iA<otf->numTables;iA++) {
    if (!otf_read(otf,buf,pos,16)) {
      otf_close(otf);
      return NULL;
    }
    otf->tables[iA].tag=get_ULONG(buf);
    otf->tables[iA].checkSum=get_ULONG(buf+4);
    otf->tables[iA].offset=get_ULONG(buf+8);
    otf->tables[iA].length=get_ULONG(buf+12);
    if ( (otf->tables[iA].tag==OTF_TAG('C','F','F',' '))&&
         ((otf->flags&OTF_F_FMT_CFF)==0) ) {
      fprintf(stderr,"Wrong magic\n");
      otf_close(otf);
      return NULL;
    } else if ( (otf->tables[iA].tag==OTF_TAG('g','l','y','p'))&&
                (otf->flags&OTF_F_FMT_CFF) ) {
      fprintf(stderr,"Wrong magic\n");
      otf_close(otf);
      return NULL;
    }
    pos+=16;
  }
  // }}}
  
//  otf->flags|=OTF_F_DO_CHECKSUM;
  // {{{ check head table
  int len=0;
  char *head=otf_get_table(otf,OTF_TAG('h','e','a','d'),&len);
  if ( (!head)||
       (get_ULONG(head+0)!=0x00010000)||  // version
       (len!=54)||
       (get_ULONG(head+12)!=0x5F0F3CF5)|| // magic
       (get_SHORT(head+52)!=0x0000) ) {   // glyphDataFormat
    fprintf(stderr,"Unsupported OTF font / head table \n");
    free(head);
    otf_close(otf);
    return NULL;
  }
  // }}}
  otf->unitsPerEm=get_USHORT(head+18);
  otf->indexToLocFormat=get_SHORT(head+50);

  // {{{ checksum whole file
  if (otf->flags&OTF_F_DO_CHECKSUM) {
    unsigned int csum=0;
    char tmp[1024];
    rewind(otf->f);
    while (!feof(otf->f)) {
      len=fread(tmp,1,1024,otf->f);
      if (len&3) { // zero padding reqd.
        memset(tmp+len,0,4-(len&3));
      }
      csum+=otf_checksum(tmp,len);
    }
    if (csum!=0xb1b0afba) {
      fprintf(stderr,"Wrong global checksum\n");
      free(head);
      otf_close(otf);
      return NULL;
    }
  }
  // }}}
  free(head);

  // {{{ read maxp table / numGlyphs
  char *maxp=otf_get_table(otf,OTF_TAG('m','a','x','p'),&len);
  if (maxp) {
    const unsigned int maxp_version=get_ULONG(maxp);
    if ( (maxp_version==0x00005000)&&(len==6) ) { // version 0.5
      otf->numGlyphs=get_USHORT(maxp+4);
      if ( (otf->flags&OTF_F_FMT_CFF)==0) { // only CFF
        free(maxp);
        maxp=NULL;
      }
    } else if ( (maxp_version==0x00010000)&&(len==32) ) { // version 1.0
      otf->numGlyphs=get_USHORT(maxp+4);
      if (otf->flags&OTF_F_FMT_CFF) { // only TTF
        free(maxp);
        maxp=NULL;
      }
    } else {
      free(maxp);
      maxp=NULL;
    }
  }
  if (!maxp) {
    fprintf(stderr,"Unsupported OTF font / maxp table \n");
    free(maxp);
    otf_close(otf);
    return NULL;
  }
  free(maxp);
  // }}}

  return otf;
}
// }}}

OTF_FILE *otf_load(const char *file) // {{{
{
  FILE *f;
  OTF_FILE *otf;
 
  int use_ttc=-1;
  if ((f=fopen(file,"rb"))==NULL) {
    // check for TTC
    char *tmp=strrchr(file,'/'),*end;
    if (tmp) {
      use_ttc=strtoul(tmp+1,&end,10);
      if (!*end) {
        end=malloc((tmp-file+1)*sizeof(char));
        if (!end) {
          fprintf(stderr,"Bad alloc: %m\n");
          return NULL;
        }
        strncpy(end,file,tmp-file);
        end[tmp-file]=0;
        f=fopen(end,"rb");
        free(end);
      }
    }
    if (!f) {
      fprintf(stderr,"Could not open \"%s\": %m\n",file);
      return NULL;
    }
  }
  otf=otf_new(f);
  if (!otf) {
    fprintf(stderr,"Bad alloc: %m\n");
    fclose(f);
    return NULL;
  }

  char buf[12];
  int pos=0;
  // {{{ check for TTC
  if (otf_read(otf,buf,pos,12)) {
    const unsigned int version=get_ULONG(buf);
    if (version==OTF_TAG('t','t','c','f')) {
      const unsigned int ttc_version=get_ULONG(buf+4);
      if ( (ttc_version!=0x00010000)&&(ttc_version!=0x00020000) ) {
        fprintf(stderr,"Unsupported TTC version\n");
        otf_close(otf);
        return NULL;
      }
      otf->numTTC=get_ULONG(buf+8);
      otf->useTTC=use_ttc;
      pos=otf_get_ttc_start(otf,use_ttc);
      if (pos==-1) {
        otf_close(otf);
        return NULL;
      }
    }
  } else {
    fprintf(stderr,"Not a ttf font\n");
    otf_close(otf);
    return NULL;
  }
  // }}}

  return otf_do_load(otf,pos);
}
// }}}

void otf_close(OTF_FILE *otf) // {{{
{
  assert(otf);
  if (otf) {
    free(otf->gly);
    free(otf->cmap);
    free(otf->name);
    free(otf->hmtx);
    free(otf->glyphOffsets);
    fclose(otf->f);
    free(otf->tables);
    free(otf);
  }
}
// }}}

static int otf_find_table(OTF_FILE *otf,unsigned int tag) // {{{ -1 on error
{
#if 0
  // binary search would require raw table
  int pos=0;
  char buf[12];
  if (!otf_read(otf,buf,pos,12)) {
    return -1;
  }
  pos=12;
  const unsigned int numTables=get_USHORT(buf+4);
  char *tables=malloc(16*numTables);
  if (!tables) {
    return -1;
  }
  if (!otf_read(otf,tables,pos,16*numTables)) {
    free(tables);
    return -1;
  }
  char target[]={(tag>>24),(tag>>16),(tag>>8),tag};
  //  assert(get_USHORT(buf+6)+get_USHORT(buf+10)==16*numTables);
  char *result=otf_bsearch(tables,target,4,
                           get_USHORT(buf+6),
                           get_USHORT(buf+8),
                           get_USHORT(buf+10),0);
  free(tables);
  if (result) {
    return (result-tables)/16;
  }
#else
  int iA;
  for (iA=0;iA<otf->numTables;iA++) {
    if (otf->tables[iA].tag==tag) {
      return iA;
    }
  }
#endif
  return -1;
}
// }}}

char *otf_get_table(OTF_FILE *otf,unsigned int tag,int *ret_len) // {{{
{
  assert(otf);
  assert(ret_len);

  const int idx=otf_find_table(otf,tag);
  if (idx==-1) {
    *ret_len=-1;
    return NULL;
  }
  const OTF_DIRENT *table=otf->tables+idx;

  char *ret=otf_read(otf,NULL,table->offset,table->length);
  if (!ret) {
    return NULL;
  }
  if (otf->flags&OTF_F_DO_CHECKSUM) {
    unsigned int csum=otf_checksum(ret,table->length);
    if (tag==OTF_TAG('h','e','a','d')) { // special case
      csum-=get_ULONG(ret+8);
    }
    if (csum!=table->checkSum) {
      fprintf(stderr,"Wrong checksum for %c%c%c%c\n",OTF_UNTAG(tag));
      free(ret);
      return NULL;
    }
  }
  *ret_len=table->length;
  return ret;
}
// }}}

int otf_load_glyf(OTF_FILE *otf) // {{{ - 0 on success
{
  assert((otf->flags&OTF_F_FMT_CFF)==0); // not for CFF
  int iA,len;
  // {{{ find glyf table
  iA=otf_find_table(otf,OTF_TAG('g','l','y','f'));
  if (iA==-1) {
    fprintf(stderr,"Unsupported OTF font / glyf table \n");
    return -1;
  }
  otf->glyfTable=otf->tables+iA;
  // }}}

  // {{{ read loca table
  char *loca=otf_get_table(otf,OTF_TAG('l','o','c','a'),&len);
  if ( (!loca)||
       (otf->indexToLocFormat>=2)||
       (((len+3)&~3)!=((((otf->numGlyphs+1)*(otf->indexToLocFormat+1)*2)+3)&~3)) ) {
    fprintf(stderr,"Unsupported OTF font / loca table \n");
    return -1;
  }
  if (otf->glyphOffsets) {
    free(otf->glyphOffsets);
    assert(0);
  }
  otf->glyphOffsets=malloc((otf->numGlyphs+1)*sizeof(unsigned int));
  if (!otf->glyphOffsets) {
    fprintf(stderr,"Bad alloc: %m\n");
    return -1;
  }
  if (otf->indexToLocFormat==0) {
    for (iA=0;iA<=otf->numGlyphs;iA++) {
      otf->glyphOffsets[iA]=get_USHORT(loca+iA*2)*2;
    }
  } else { // indexToLocFormat==1
    for (iA=0;iA<=otf->numGlyphs;iA++) {
      otf->glyphOffsets[iA]=get_ULONG(loca+iA*4);
    }
  }
  free(loca);
  if (otf->glyphOffsets[otf->numGlyphs]>otf->glyfTable->length) {
    fprintf(stderr,"Bad loca table \n");
    return -1;
  }
  // }}}
  
  // {{{ allocate otf->gly slot
  int maxGlyfLen=0;  // no single glyf takes more space
  for (iA=1;iA<=otf->numGlyphs;iA++) {
    const int glyfLen=otf->glyphOffsets[iA]-otf->glyphOffsets[iA-1];
    if (glyfLen<0) {
      fprintf(stderr,"Bad loca table: glyph len %d\n",glyfLen);
      return -1;
    }
    if (maxGlyfLen<glyfLen) {
      maxGlyfLen=glyfLen;
    }
  }
  if (otf->gly) {
    free(otf->gly);
    assert(0);
  }
  otf->gly=malloc(maxGlyfLen*sizeof(char));
  if (!otf->gly) {
    fprintf(stderr,"Bad alloc: %m\n");
    return -1;
  }
  // }}}

  return 0;
}
// }}}

int otf_load_more(OTF_FILE *otf) // {{{ -  0 on success
{
  int iA;

  int len;
  if ((otf->flags&OTF_F_FMT_CFF)==0) { // not for CFF
    if (otf_load_glyf(otf)==-1) {
      return -1;
    }
  }

  // {{{ read hhea table
  char *hhea=otf_get_table(otf,OTF_TAG('h','h','e','a'),&len);
  if ( (!hhea)||
       (get_ULONG(hhea)!=0x00010000)|| // version
       (len!=36)||
       (get_SHORT(hhea+32)!=0) ) { // metric format
    fprintf(stderr,"Unsupported OTF font / hhea table \n");
    return -1;
  }
  otf->numberOfHMetrics=get_USHORT(hhea+34);
  free(hhea);
  // }}}

  // {{{ read hmtx table
  char *hmtx=otf_get_table(otf,OTF_TAG('h','m','t','x'),&len);
  if ( (!hmtx)||
       (len!=otf->numberOfHMetrics*2+otf->numGlyphs*2) ) {
    fprintf(stderr,"Unsupported OTF font / hmtx table \n");
    return -1;
  }
  if (otf->hmtx) {
    free(otf->hmtx);
    assert(0);
  }
  otf->hmtx=hmtx;
  // }}}

  // {{{ read name table
  char *name=otf_get_table(otf,OTF_TAG('n','a','m','e'),&len);
  if ( (!name)||
       (get_USHORT(name)!=0x0000)|| // version
       (len<get_USHORT(name+2)*12+6)||
       (len<=get_USHORT(name+4)) ) {
    fprintf(stderr,"Unsupported OTF font / name table \n");
    return -1;
  }
  // check bounds
  int name_count=get_USHORT(name+2);
  const char *nstore=name+get_USHORT(name+4);
  for (iA=0;iA<name_count;iA++) {
    const char *nrec=name+6+12*iA;
    if (nstore-name+get_USHORT(nrec+10)+get_USHORT(nrec+8)>len) {
      fprintf(stderr,"Bad name table \n");
      free(name);
      return -1;
    }
  }
  if (otf->name) {
    free(otf->name);
    assert(0);
  }
  otf->name=name;
  // }}}

  return 0;
}
// }}}

int otf_load_cmap(OTF_FILE *otf) // {{{ -  0 on success
{
  int iA;
  int len;

  char *cmap=otf_get_table(otf,OTF_TAG('c','m','a','p'),&len);
  if ( (!cmap)||
       (get_USHORT(cmap)!=0x0000)|| // version
       (len<get_USHORT(cmap+2)*8+4) ) {
    fprintf(stderr,"Unsupported OTF font / cmap table \n");
    assert(0);
    return -1;
  }
  // check bounds, find (3,0) or (3,1) [TODO?]
  const int numTables=get_USHORT(cmap+2);
  for (iA=0;iA<numTables;iA++) {
    const char *nrec=cmap+4+8*iA;
    const unsigned int offset=get_ULONG(nrec+4);
    const char *ndata=cmap+offset;
    if ( (ndata<cmap+4+8*numTables)||
         (offset>=len)||
         (offset+get_USHORT(ndata+2)>len) ) {
      fprintf(stderr,"Bad cmap table \n");
      free(cmap);
      assert(0);
      return -1;
    }
    if ( (get_USHORT(nrec)==3)&&
         (get_USHORT(nrec+2)<=1)&&
         (get_USHORT(ndata)==4)&&
         (get_USHORT(ndata+4)==0) ) {
      otf->unimap=ndata;
    }
  }
  if (otf->cmap) {
    free(otf->cmap);
    assert(0);
  }
  otf->cmap=cmap;

  return 0;
}
// }}}

int otf_get_width(OTF_FILE *otf,unsigned short gid) // {{{  -1 on error
{
  assert(otf);

  if (gid>=otf->numGlyphs) {
    return -1;
  }

  // ensure hmtx is there
  if (!otf->hmtx) {
    if (otf_load_more(otf)!=0) {
      assert(0);
      return -1;
    }
  }

  return get_width_fast(otf,gid);
#if 0
  if (gid>=otf->numberOfHMetrics) {
    return get_USHORT(otf->hmtx+(otf->numberOfHMetrics-1)*2);
    // TODO? lsb=get_SHORT(otf->hmtx+otf->numberOfHMetrics*2+gid*2);
  }
  return get_USHORT(otf->hmtx+gid*4);
  // TODO? lsb=get_SHORT(otf->hmtx+gid*4+2);
#endif
}
// }}}

static int otf_name_compare(const void *a,const void *b) // {{{
{
  return memcmp(a,b,8);
}
// }}}

const char *otf_get_name(OTF_FILE *otf,int platformID,int encodingID,int languageID,int nameID,int *ret_len) // {{{
{
  assert(otf);
  assert(ret_len);

  // ensure name is there
  if (!otf->name) {
    if (otf_load_more(otf)!=0) {
      *ret_len=-1;
      assert(0);
      return NULL;
    }
  }

  char key[8];
  set_USHORT(key,platformID);
  set_USHORT(key+2,encodingID);
  set_USHORT(key+4,languageID);
  set_USHORT(key+6,nameID);

  char *res=bsearch(key,otf->name+6,get_USHORT(otf->name+2),12,otf_name_compare);
  if (res) {
    *ret_len=get_USHORT(res+8);
    int npos=get_USHORT(res+10);
    const char *nstore=otf->name+get_USHORT(otf->name+4);
    return nstore+npos;
  }
  *ret_len=0;
  return NULL;
}
// }}}

int otf_get_glyph(OTF_FILE *otf,unsigned short gid) // {{{ result in >otf->gly, returns length, -1 on error
{
  assert(otf);
  assert((otf->flags&OTF_F_FMT_CFF)==0); // not for CFF

  if (gid>=otf->numGlyphs) {
    return -1;
  }

  // ensure >glyphOffsets and >gly is there
  if ( (!otf->gly)||(!otf->glyphOffsets) ) {
    if (otf_load_more(otf)!=0) {
      assert(0);
      return -1;
    }
  }

  const int len=otf->glyphOffsets[gid+1]-otf->glyphOffsets[gid];
  if (len==0) {
    return 0;
  }

  assert(otf->glyfTable->length>=otf->glyphOffsets[gid+1]);
  if (!otf_read(otf,otf->gly,
                otf->glyfTable->offset+otf->glyphOffsets[gid],len)) {
    return -1;
  }

  return len;
}
// }}}

unsigned short otf_from_unicode(OTF_FILE *otf,int unicode) // {{{ 0 = missing
{
  assert(otf);
  assert( (unicode>=0)&&(unicode<65536) );
//  assert((otf->flags&OTF_F_FMT_CFF)==0); // not for CFF, other method!

  // ensure >cmap and >unimap is there
  if (!otf->cmap) {
    if (otf_load_cmap(otf)!=0) {
      assert(0);
      return 0; // TODO?
    }
  }
  if (!otf->unimap) {
    fprintf(stderr,"Unicode (3,1) cmap in format 4 not found\n");
    return 0;
  }

#if 0
  // linear search is cache friendly and should be quite fast
#else
  const unsigned short segCountX2=get_USHORT(otf->unimap+6);
  char target[]={unicode>>8,unicode}; // set_USHORT(target,unicode);
  char *result=otf_bsearch((char *)otf->unimap+14,target,2,
                           get_USHORT(otf->unimap+8),
                           get_USHORT(otf->unimap+10),
                           get_USHORT(otf->unimap+12),1);
  if (result>=otf->unimap+14+segCountX2) {
    assert(0); // bad font, no 0xffff
    return 0;
  }

  result+=2+segCountX2;
  const unsigned short startCode=get_USHORT(result);
  if (startCode>unicode) {
    return 0;
  }
  result+=2*segCountX2;
  const unsigned short rangeOffset=get_USHORT(result);
  if (rangeOffset) {
    return get_USHORT(result+rangeOffset+2*(unicode-startCode));
  } else {
    const short delta=get_SHORT(result-segCountX2);
    return (delta+unicode)&0xffff;
  }
#endif
}
// }}}

#include "bitset.h"

// include components (set bit in >glyphs) of currently loaded compound glyph (with >curgid)
// returns additional space requirements (when bits below >donegid are touched)
static int otf_subset_glyf(OTF_FILE *otf,int curgid,int donegid,BITSET glyphs) // {{{
{
  int ret=0;
  if (get_SHORT(otf->gly)>=0) { // not composite
    return ret; // done
  }

  char *cur=otf->gly+10;

  unsigned short flags;
  do {
    flags=get_USHORT(cur);
    const unsigned short sub_gid=get_USHORT(cur+2);
    assert(sub_gid<otf->numGlyphs);
    if (!bit_check(glyphs,sub_gid)) {
      // bad: temporarily load sub glyph
      const int len=otf_get_glyph(otf,sub_gid);
      assert(len>0);
      bit_set(glyphs,sub_gid);
      if (sub_gid<donegid) {
        ret+=len;
        ret+=otf_subset_glyf(otf,sub_gid,donegid,glyphs); // composite of composites?, e.g. in DejaVu
      }
      const int res=otf_get_glyph(otf,curgid); // reload current glyph
      assert(res);
    }
    
    // skip parameters
    cur+=6;
    if (flags&0x01) {
      cur+=2;
    }
    if (flags&0x08) {
      cur+=2;
    } else if (flags&0x40) {
      cur+=4;
    } else if (flags&0x80) {
      cur+=8;
    }
  } while (flags&0x20); // more components

  return ret;
}
// }}}

int otf_subset2(OTF_FILE *otf,BITSET glyphs,OUTPUT_FN output,void *context) // {{{ - returns number of bytes written
{
  assert(otf);
  assert(glyphs);
  assert(output);

  int iA,b,c;
  int ret=0;

  // first pass
  bit_set(glyphs,0); // .notdef always required
  int glyfSize=0;
  for (iA=0,b=0,c=1;iA<otf->numGlyphs;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }
    if (glyphs[b]&c) {
      int len=otf_get_glyph(otf,iA);
      if (len<0) {
        assert(0);
        return -1;
      } else if (len>0) {
        glyfSize+=len;
        len=otf_subset_glyf(otf,iA,iA,glyphs);
        if (len<0) {
          assert(0);
          return -1;
        }
        glyfSize+=len;
      }
    }
  }

  int locaSize=((otf->numGlyphs+1)*(otf->indexToLocFormat+1)*2+3)&~3;
  glyfSize=(glyfSize+3)&~3;
  // second pass
  char *new_loca=malloc(locaSize);
  char *new_glyf=malloc(glyfSize);
  if ( (!new_loca)||(!new_glyf) ) {
    fprintf(stderr,"Bad alloc: %m\n");
    assert(0);
    free(new_loca);
    free(new_glyf);
    return -1;
  }

  int offset=0;
  for (iA=0,b=0,c=1;iA<otf->numGlyphs;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }

    assert(offset%2==0);
    // TODO? change format? if glyfSize<0x20000 
    if (otf->indexToLocFormat==0) {
      set_USHORT(new_loca+iA*2,offset/2);
    } else { // ==1
      set_ULONG(new_loca+iA*4,offset);
    }

    if (glyphs[b]&c) {
      const int len=otf_get_glyph(otf,iA);
      assert(len>=0);
      memcpy(new_glyf+offset,otf->gly,len);
      offset+=len;
    }
  }
  // last entry
  if (otf->indexToLocFormat==0) {
    set_USHORT(new_loca+otf->numGlyphs*2,offset/2);
  } else { // ==1
    set_ULONG(new_loca+otf->numGlyphs*4,offset);
  }
  // zero padding
  assert(glyfSize-offset<4);
  for (iA=offset;iA<glyfSize;iA++) {
    new_glyf[iA]=0;
  }
  assert(offset<=glyfSize);
//assert(offset==glyfSize); // else TODO 0 padding

  // copy some tables
#define MAX_TABLES 11
  int iB,new_numTables=MAX_TABLES;
  OTF_DIRENT new_tables[MAX_TABLES]={
      {OTF_TAG('c','m','a','p'),},
      {OTF_TAG('c','v','t',' '),},
      {OTF_TAG('f','p','g','m'),},
      {OTF_TAG('g','l','y','f'),},
      {OTF_TAG('h','e','a','d'),},
      {OTF_TAG('h','h','e','a'),},
      {OTF_TAG('h','m','t','x'),},
      {OTF_TAG('l','o','c','a'),},
      {OTF_TAG('m','a','x','p'),},
      {OTF_TAG('n','a','m','e'),},
      {OTF_TAG('p','r','e','p'),}};
  for (iA=0,iB=0;(iA<otf->numTables)&&(iB<MAX_TABLES);) {
    if (otf->tables[iA].tag==new_tables[iB].tag) {
      new_tables[iB].checkSum=otf->tables[iA].checkSum;
      new_tables[iB].offset=iA;
      new_tables[iB].length=otf->tables[iA].length;
      iA++;
      iB++;
    } else if (otf->tables[iA].tag<new_tables[iB].tag) {
      iA++;
    } else {
      new_tables[iB].tag=0; // don't output
      iB++;
      new_numTables--;
    }
  }

  // now we have to know all the checksums and lengths
  //TODO ? reduce cmap [to (1,0) ;-)]
  // glyf
  new_tables[3].checkSum=otf_checksum(new_glyf,glyfSize);
  new_tables[3].length=offset;
  // loca
  new_tables[7].checkSum=otf_checksum(new_loca,locaSize);
  new_tables[7].length=(otf->numGlyphs+1)*(otf->indexToLocFormat+1)*2;

  // offset table + directory
  char new_start[12+MAX_TABLES*16];
  set_ULONG(new_start,0x00010000); // TODO ? true / original value
  set_USHORT(new_start+4,new_numTables);
  otf_bsearch_params(new_numTables,16,&iA,&iB,&c); 
  set_USHORT(new_start+6,iA);
  set_USHORT(new_start+8,iB);
  set_USHORT(new_start+10,c);

  unsigned int csum=0;
  offset=12+16*new_numTables;
  iB=12;
  for (iA=0;iA<MAX_TABLES;iA++) {
    if (!new_tables[iA].tag) {
      continue;
    }
    set_ULONG(new_start+iB,new_tables[iA].tag);
    set_ULONG(new_start+iB+4,new_tables[iA].checkSum);
    set_ULONG(new_start+iB+8,offset);
    set_ULONG(new_start+iB+12,new_tables[iA].length);
    iB+=16;
    offset+=(new_tables[iA].length+3)&~3;
    csum+=new_tables[iA].checkSum;
  }
  assert(iB==12+16*new_numTables);
  (*output)(new_start,iB,context);
  ret+=iB;
  csum+=otf_checksum(new_start,iB);

  // now output the tables / copy them
  iB=12+8;
  for (iA=0;iA<MAX_TABLES;iA++) {
    if (!new_tables[iA].tag) {
      continue;
    }
    if (iA==3) { // glyf
      (*output)(new_glyf,glyfSize,context);
      ret+=glyfSize;
    } else if (iA==7) { // loca
      (*output)(new_loca,locaSize,context);
      ret+=locaSize;
    } else { // just copy
      const OTF_DIRENT *table=otf->tables+new_tables[iA].offset;
      char *data=otf_read(otf,NULL,table->offset,table->length);
      assert(data);
      if (iA==4) { // head. fix global checksum
        set_ULONG(data+8,0xb1b0afba-csum);
      }
      assert(ret==get_ULONG(new_start+iB));
      (*output)(data,(table->length+3)&~3,context);
      ret+=(table->length+3)&~3;
      free(data);
    }
    iB+=16;
  }

  // TODO? suggested ordering
  // head, hhea, maxp, hmtx, cmap, fpgm, prep, cvt, loca, glyf, name

  // copy some tables [cvt,fpgm,(glyf),head!,hhea,hmtx,(loca),maxp,name(?),prep]

  //TODO (cmap for non-cid)

  free(new_loca);
  free(new_glyf);
  return ret;
#undef MAX_TABLES
}
// }}}

int otf_action_copy(void *param,int table_no,OUTPUT_FN output,void *context) // {{{
{
  OTF_FILE *otf=param;
  const OTF_DIRENT *table=otf->tables+table_no;

  if (!output) { // get checksum and unpadded length
    *(unsigned int *)context=table->checkSum;
    return table->length;
  }

  char *data=otf_read(otf,NULL,table->offset,table->length);
  if (!data) {
    return -1;
  }
  int ret=(table->length+3)&~3;
  (*output)(data,ret,context);
  free(data);
  return ret; // padded length
}
// }}}

// TODO? >modified time-stamp?
int otf_action_copy_head(void *param,int csum,OUTPUT_FN output,void *context) // {{{
{
  OTF_FILE *otf=param;
  const int table_no=otf_find_table(otf,OTF_TAG('h','e','a','d'));
  assert(table_no!=-1);
  const OTF_DIRENT *table=otf->tables+table_no;

  if (!output) { // get checksum and unpadded length
    *(unsigned int *)context=table->checkSum;
    return table->length;
  }

  char *data=otf_read(otf,NULL,table->offset,table->length);
  if (!data) {
    return -1;
  }
  set_ULONG(data+8,0xb1b0afba-csum); // head. fix global checksum
  int ret=(table->length+3)&~3;
  (*output)(data,ret,context);
  free(data);
  return ret; // padded length
}
// }}}

int otf_action_replace(void *param,int length,OUTPUT_FN output,void *context) // {{{
{
  char *data=param;
  char pad[4]={0,0,0,0};

  int ret=(length+3)&~3;
  if (!output) { // get checksum and unpadded length
    if (ret!=length) {
      unsigned int csum=otf_checksum(data,ret-4);
      memcpy(pad,data+ret-4,ret-length);
      csum+=get_ULONG(pad);
      *(unsigned int *)context=csum;
    } else {
      *(unsigned int *)context=otf_checksum(data,length);
    }
    return length;
  }

  (*output)(data,length,context);
  if (ret!=length) {
    (*output)(pad,ret-length,context);
  }

  return ret; // padded length
}
// }}}

int otf_write_sfnt(struct _OTF_WRITE *otw,unsigned int version,int numTables,OUTPUT_FN output,void *context) // {{{
{
  int iA;
  int ret;

  // find head
  int headAt=-1;
  for (iA=0;iA<numTables;iA++) {
    if ( (otw[iA].tag==OTF_TAG('h','e','a','d'))&&
         (otw[iA].action==otf_action_copy) ) {
      // only this supported for now
      headAt=iA;
    }
  }

  // TODO? sort tables 

  char *start=malloc(12+16*numTables);
  if (!start) {
    fprintf(stderr,"Bad alloc: %m\n");
    return -1;
  }

  // the header
  set_ULONG(start,version);
  set_USHORT(start+4,numTables);
  int a,b,c;
  otf_bsearch_params(numTables,16,&a,&b,&c); 
  set_USHORT(start+6,a);
  set_USHORT(start+8,b);
  set_USHORT(start+10,c);

  // first pass: calculate table directory / offsets and checksums
  unsigned int globalSum=0,csum;
  int pos,res;
  int offset=12+16*numTables;
  pos=12;
  for (iA=0;iA<numTables;iA++) {
    set_ULONG(start+pos,otw[iA].tag);
    res=(*otw[iA].action)(otw[iA].param,otw[iA].length,NULL,&csum);
    assert(res>=0);
    set_ULONG(start+pos+4,csum);
    set_ULONG(start+pos+8,offset);
    set_ULONG(start+pos+12,res);
    pos+=16;
    offset+=(res+3)&~3; // padding
    globalSum+=csum;
  }

  // second pass: write actual data
  // write header + directory
  (*output)(start,pos,context);
  ret=pos;
  globalSum+=otf_checksum(start,pos);
  if (headAt!=-1) {
    otw[headAt].action=otf_action_copy_head;
    otw[headAt].length=globalSum;
  }

  // write tables
  for (iA=0;iA<numTables;iA++) {
    assert(ret==get_ULONG(start+12+16*iA+8)); // table directory correct?
    res=(*otw[iA].action)(otw[iA].param,otw[iA].length,output,context);
    if (res<0) {
      free(start);
      return -1;
    }
    assert(((res+3)&~3)==res); // correctly padded?
    ret+=(res+3)&~3;
  }
  assert(offset==ret);
  free(start);

  return ret;
}
// }}}

int otf_ttc_extract(OTF_FILE *otf,OUTPUT_FN output,void *context) // {{{
{
  assert(otf);
  assert(output);
  assert(otf->numTTC);
  int iA;

  struct _OTF_WRITE *otw;
  otw=malloc(sizeof(struct _OTF_WRITE)*otf->numTables);
  if (!otw) {
    fprintf(stderr,"Bad alloc: %m\n");
    return -1;
  }

  // just copy everything
  for (iA=0;iA<otf->numTables;iA++) {
    otw[iA].tag=otf->tables[iA].tag;
    otw[iA].action=otf_action_copy;
    otw[iA].param=otf;
    otw[iA].length=iA;
  }
  iA=otf_write_sfnt(otw,otf->version,otf->numTables,output,context);
  free(otw);

  return iA;
}
// }}}

int otf_subset(OTF_FILE *otf,BITSET glyphs,OUTPUT_FN output,void *context) // {{{ - returns number of bytes written
{
  assert(otf);
  assert(glyphs);
  assert(output);

  int iA,b,c;
  int ret=0;

  // first pass: include all required glyphs
  bit_set(glyphs,0); // .notdef always required
  int glyfSize=0;
  for (iA=0,b=0,c=1;iA<otf->numGlyphs;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }
    if (glyphs[b]&c) {
      int len=otf_get_glyph(otf,iA);
      if (len<0) {
        assert(0);
        return -1;
      } else if (len>0) {
        glyfSize+=len;
        len=otf_subset_glyf(otf,iA,iA,glyphs);
        if (len<0) {
          assert(0);
          return -1;
        }
        glyfSize+=len;
      }
    }
  }

  // second pass: calculate new glyf and loca
  int locaSize=(otf->numGlyphs+1)*(otf->indexToLocFormat+1)*2;

  char *new_loca=malloc(locaSize);
  char *new_glyf=malloc(glyfSize);
  if ( (!new_loca)||(!new_glyf) ) {
    fprintf(stderr,"Bad alloc: %m\n");
    assert(0);
    free(new_loca);
    free(new_glyf);
    return -1;
  }

  int offset=0;
  for (iA=0,b=0,c=1;iA<otf->numGlyphs;iA++,c<<=1) {
    if (!c) {
      b++;
      c=1;
    }

    assert(offset%2==0);
    // TODO? change format? if glyfSize<0x20000 
    if (otf->indexToLocFormat==0) {
      set_USHORT(new_loca+iA*2,offset/2);
    } else { // ==1
      set_ULONG(new_loca+iA*4,offset);
    }

    if (glyphs[b]&c) {
      const int len=otf_get_glyph(otf,iA);
      assert(len>=0);
      memcpy(new_glyf+offset,otf->gly,len);
      offset+=len;
    }
  }
  // last entry
  if (otf->indexToLocFormat==0) {
    set_USHORT(new_loca+otf->numGlyphs*2,offset/2);
  } else { // ==1
    set_ULONG(new_loca+otf->numGlyphs*4,offset);
  }
  assert(offset==glyfSize);

  // determine new tables.
#define MAX_TABLES 12
  int numTables=0;
  struct _OTF_WRITE otw[MAX_TABLES]={
      {OTF_TAG('c','m','a','p'),otf_action_copy,otf,},
      {OTF_TAG('c','v','t',' '),otf_action_copy,otf,},
      {OTF_TAG('f','p','g','m'),otf_action_copy,otf,},
      {OTF_TAG('g','l','y','f'),otf_action_replace,new_glyf,glyfSize},
      {OTF_TAG('h','e','a','d'),otf_action_copy,otf,}, // _copy_head
      {OTF_TAG('h','h','e','a'),otf_action_copy,otf,},
      {OTF_TAG('h','m','t','x'),otf_action_copy,otf,},
      {OTF_TAG('l','o','c','a'),otf_action_replace,new_loca,locaSize},
      {OTF_TAG('m','a','x','p'),otf_action_copy,otf,},
      {OTF_TAG('n','a','m','e'),otf_action_copy,otf,},
      {OTF_TAG('p','r','e','p'),otf_action_copy,otf,},
      {0,0,0,0}};

  for (iA=0;(iA<otf->numTables)&&(otw[numTables].tag);) {
    if (otf->tables[iA].tag==otw[numTables].tag) {
      if (otw[numTables].param==otf) {
        otw[numTables].length=iA;
      }
      iA++;
      numTables++;
    } else if (otf->tables[iA].tag<otw[numTables].tag) {
      iA++;
    } else {
      memmove(otw+numTables,otw+numTables+1,sizeof(struct _OTF_WRITE)*(MAX_TABLES-numTables-1)); // don't output
    } 
  }
  // write them
  ret=otf_write_sfnt(otw,otf->version,numTables,output,context);

  free(new_loca);
  free(new_glyf);
  return ret;

  // TODO? suggested ordering
  // head, hhea, maxp, hmtx, cmap, fpgm, prep, cvt, loca, glyf, name
  // copy some tables [cvt,fpgm,(glyf),head!,hhea,hmtx,(loca),maxp,name(?),prep]

  //TODO ? reduce cmap [to (1,0) ;-)]
  //TODO (cmap for non-cid)
}
// }}}


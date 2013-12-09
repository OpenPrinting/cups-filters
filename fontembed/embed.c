#include "embed.h"
#include "embed_sfnt_int.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/* certain profiles: (=> constraints to be auto-applied in emb_new via >dest)
  PSold: T1->T1, TTF->T1, OTF->CFF->T1, STD->STD   // output limit: T1  (maybe length, binary/text, ... limit)
  PS1: T1->T1, TTF->T1, OTF->CFF, STD->STD    // output limit: T1,CFF [does this really exists?]
  PS2: T1->T1, TTF->TTF, OTF->T1, STD->STD    // output limit: T1,TTF
  PS3: T1->T1, TTF->TTF, OTF->CFF, STD->STD
  PDF12/13?: OTF->CFF
  PDF16: OTF->OTF (,T1->CFF?)
    --- rename KEEP_T1 to NEED_T1?  NO_T42?

  converters:
  OTF->CFF, CFF->OTF (extract metrics, etc)
  (T1->CFF, CFF->T1)
  ((TTF->T1 ['good'; T1->TTF: not good]))
  [subsetTTF,subsetCFF,subsetT1]

  output modes:
  subset,CID(multibyte),(PS:)text/binary,(PS:)incremental

  TODO: remove dest from emb_new, replace with EMB_ACTIONS constraints:
     - bitfield mask which ACTIONS are allowed.  (problem: we want to force certain ones, e.g. MULTIBYTE)
     - e.g. currently EMB_C_PDF_OT has to functions
     - the only (other) 'difference' to now is the subsetting spec
     - another issue is, that emb_pdf_ might want some pdf version informatino (->extra flag?)
   and funtion to determine appropriate mask for certain destination
     EMB_ACTIONS emb_mask_for_dest(EMB_DESTINATION)
  TODO? determine viability before trying emb_embed
    (idea: use emb_embed(,NULL) -> will just return true/false  [same codepath!])

  TODO?! "always subset CJK"
*/

EMB_PARAMS *emb_new(FONTFILE *font,EMB_DESTINATION dest,EMB_CONSTRAINTS mode) // {{{
{
  assert(font);

  EMB_PARAMS *ret=calloc(1,sizeof(EMB_PARAMS));
  if (!ret) {
    fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
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
    if (font->sfnt->flags&OTF_F_FMT_CFF) {
      ret->intype=EMB_FMT_OTF;
    } else {
      ret->intype=EMB_FMT_TTF;
    }
    ret->rights=emb_otf_get_rights(ret->font->sfnt);
    numGlyphs=ret->font->sfnt->numGlyphs; // TODO
  } else if (font->stdname) {
    ret->intype=EMB_FMT_STDFONT;
    ret->rights=EMB_RIGHT_NONE;
  } else {
    assert(0);
  }
/*
  if ( (ret->intype==EMB_FMT_CFF)&&
       (ret->cffFont.is_cid()) ) {
     ?= || ( (ret->intype==EMB_FMT_OTF)&&(ret->sfnt->cffFont.is_cid()) ) // TODO?
    ret->plan|=EMB_A_MULTIBYTE;
  }
*/

  // determine outtype
  if (ret->intype==EMB_FMT_STDFONT) {
    ret->outtype=ret->intype;
    if (mode&EMB_C_FORCE_MULTIBYTE) {
      fprintf(stderr,"Multibyte stdfonts are not possible\n");
      emb_close(ret);
      return NULL;
    }
    return ret; // never subset, no multibyte
  } else if (ret->intype==EMB_FMT_T1) {
    if (mode&EMB_C_KEEP_T1) {
      ret->outtype=EMB_FMT_T1;
    } else {
      ret->plan|=EMB_A_T1_TO_CFF;
      ret->outtype=EMB_FMT_CFF;
    }
  } else {
    ret->outtype=ret->intype;
  }
  if (ret->outtype==EMB_FMT_CFF) {
    if (mode&EMB_C_PDF_OT) {
      ret->outtype=EMB_FMT_OTF;
      ret->plan|=EMB_A_CFF_TO_OTF;
    }
  } else if (ret->outtype==EMB_FMT_OTF) {
    // TODO: no support yet;  but we want to get the FontDescriptor/Name right
    mode|=EMB_C_NEVER_SUBSET;
    if (!(mode&EMB_C_PDF_OT)) { // TODO!?!
      ret->outtype=EMB_FMT_CFF;
      ret->plan|=EMB_A_OTF_TO_CFF;
    }
  }

  if (mode&EMB_C_FORCE_MULTIBYTE) {
    ret->plan|=EMB_A_MULTIBYTE;
  }

  // check rights (for subsetting)
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
      fprintf(stderr,"Bad alloc: %s\n", strerror(errno));
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

  if (emb->dest==EMB_DEST_NATIVE) {
  } else if (emb->dest<=EMB_DEST_PS) {
    int ret=-2;
    const char *fontname=emb_otf_get_fontname(emb->font->sfnt); // TODO!!
    (*output)("%%BeginFont: ",13,context);
    (*output)(fontname,strlen(fontname),context);
    (*output)("\n",1,context);
    if (emb->outtype==EMB_FMT_T1) {
    } else if (emb->outtype==EMB_FMT_TTF) { // emb->outtype==EMB_OUTPUT_OTF  is stupid (?)
      // do Type42
      ret=emb_otf_ps(emb->font->sfnt,NULL,256,NULL,output,context); // TODO?
    } else if (emb->outtype==EMB_FMT_CFF) {
    } else if (emb->outtype==EMB_FMT_STDFONT) {
    }
    if (ret!=-2) {
      if (ret!=-1) {
        (*output)("%%EndFont\n",10,context);
      } else {
        fprintf(stderr,"Failed\n");
      }
      return ret;
    }
  } else if (emb->dest<=EMB_DEST_PDF16) {
    if (emb->outtype==EMB_FMT_TTF) {
      assert(emb->font->sfnt);
      if (emb->plan&EMB_A_SUBSET) {
        return otf_subset(emb->font->sfnt,emb->subset,output,context);
      } else if (emb->font->sfnt->numTTC) { //
        return otf_ttc_extract(emb->font->sfnt,output,context);
      } else { // copy verbatim
        return copy_file(emb->font->sfnt->f,output,context);
      }
    } else if (emb->outtype==EMB_FMT_OTF) {
      if (emb->plan&EMB_A_CFF_TO_OTF) {
        if (emb->plan&EMB_A_T1_TO_CFF) {
          // TODO
        } else {
          // assert(emb->font->cff);
          // TODO
        }
      } else {
        assert(emb->font->sfnt);
        if (emb->plan&EMB_A_SUBSET) {
          return otf_subset_cff(emb->font->sfnt,emb->subset,output,context);
        } else {
          return copy_file(emb->font->sfnt->f,output,context);
        }
      }
    } else if (emb->outtype==EMB_FMT_CFF) {
      if (emb->plan&EMB_A_OTF_TO_CFF) {
        assert(emb->font->sfnt);
        if (emb->plan&EMB_A_SUBSET) {
          // TODO
        } else {
          return otf_cff_extract(emb->font->sfnt,output,context);
        }
      } else {
        // TODO
      }
    }
  }

  fprintf(stderr,"NOT IMPLEMENTED\n");
  assert(0);
  return -1;
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


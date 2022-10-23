//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/fontembed-private.h>
#include "embed-sfnt-private.h"
#include <cupsfilters/debug-internal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static inline int
copy_file(FILE *f,
	  _cf_fontembed_output_fn_t output,
	  void *context) // {{{
{
  DEBUG_assert(f);
  DEBUG_assert(output);

  char buf[4096];
  int iA, ret = 0;

  ret = 0;
  rewind(f);
  do
  {
    iA = fread(buf, 1, 4096, f);
    (*output)(buf, iA, context);
    ret += iA;
  }
  while (iA > 0);
  return (ret);
}
// }}}


//
// certain profiles: (=> constraints to be auto-applied in
// _cfFontEmbedEmbNew via >dest)
// PSold: T1->T1, TTF->T1, OTF->CFF->T1, STD->STD // output limit: T1
//                                                // (maybe length,
//                                                //  binary/text, ... limit)
// PS1: T1->T1, TTF->T1, OTF->CFF, STD->STD    // output limit: T1,CFF
//                                             // [does this really exists?]
// PS2: T1->T1, TTF->TTF, OTF->T1, STD->STD    // output limit: T1, TTF
// PS3: T1->T1, TTF->TTF, OTF->CFF, STD->STD
// PDF12/13?: OTF->CFF
// PDF16: OTF->OTF (, T1->CFF?)
//   --- rename KEEP_T1 to NEED_T1?  NO_T42?
//
// converters:
// OTF->CFF, CFF->OTF (extract metrics, etc)
// (T1->CFF, CFF->T1)
// ((TTF->T1 ['good'; T1->TTF: not good]))
// [subsetTTF, subsetCFF, subsetT1]
//
// output modes:
// subset,CID(multibyte),(PS:)text/binary,(PS:)incremental
//
// TODO: remove dest from _cfFontEmbedEmbNew, replace with
//       _cf_fontembed_emb_action_t constraints:
//    - bitfield mask which ACTIONS are allowed. (problem: we want to force
//      certain ones, e.g. MULTIBYTE)
//    - e.g. currently _CF_FONTEMBED_EMB_C_PDF_OT has to functions
//    - the only (other) 'difference' to now is the subsetting spec
//    - another issue is, that emb_pdf_ might want some pdf version
//      informatino (-> extra flag?)
//      and funtion to determine appropriate mask for certain destination
//      _cf_fontembed_emb_action_t emb_mask_for_dest(_cf_fontembed_emb_dest_t)
// TODO? determine viability before trying _cfFontEmbedEmbEmbed
//   (idea: use _cfFontEmbedEmbEmbed(, NULL) -> will just return true/false
//          [same codepath!])
//
// TODO?! "always subset CJK"
//


_cf_fontembed_emb_params_t *
_cfFontEmbedEmbNew(_cf_fontembed_fontfile_t *font,
		   _cf_fontembed_emb_dest_t dest,
		   _cf_fontembed_emb_constraint_t mode) // {{{
{
  DEBUG_assert(font);

  _cf_fontembed_emb_params_t *ret =
    calloc(1, sizeof(_cf_fontembed_emb_params_t));
  if (!ret)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    if (mode & _CF_FONTEMBED_EMB_C_TAKE_FONTFILE)
      _cfFontEmbedFontFileClose(font);
    return (NULL);
  }
  ret->dest = dest;
  ret->font = font;
  if (mode & _CF_FONTEMBED_EMB_C_TAKE_FONTFILE)
    ret->plan |= _CF_FONTEMBED_EMB_A_CLOSE_FONTFILE;

  // check parameters
  if ((mode & _CF_FONTEMBED_EMB_C_KEEP_T1) &&
      (mode & _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE))
  {
    fprintf(stderr, "Incompatible mode: KEEP_T1 and FORCE_MULTIBYTE\n");
    _cfFontEmbedEmbClose(ret);
    return (NULL);
  }
  if ((mode & 0x07) > 5)
  {
    fprintf(stderr, "Bad subset specification\n");
    _cfFontEmbedEmbClose(ret);
    return (NULL);
  }

  // determine intype
  int numGlyphs = 0;
  if (font->sfnt)
  {
    if (font->sfnt->flags & _CF_FONTEMBED_OTF_F_FMT_CFF)
      ret->intype = _CF_FONTEMBED_EMB_FMT_OTF;
    else
      ret->intype = _CF_FONTEMBED_EMB_FMT_TTF;
    ret->rights = __cfFontEmbedEmbOTFGetRights(ret->font->sfnt);
    numGlyphs = ret->font->sfnt->numGlyphs; // TODO
  }
  else if (font->stdname)
  {
    ret->intype = _CF_FONTEMBED_EMB_FMT_STDFONT;
    ret->rights = _CF_FONTEMBED_EMB_RIGHT_NONE;
  } else
    DEBUG_assert(0);

#if 0
  if ((ret->intype == _CF_FONTEMBED_EMB_FMT_CFF) &&
      (ret->cffFont.is_cid()))
  {
     ?= || ((ret->intype == _CF_FONTEMBED_EMB_FMT_OTF) &&
	    (ret->sfnt->cffFont.is_cid())) // TODO?
      ret->plan |= _CF_FONTEMBED_EMB_A_MULTIBYTE;
  }
#endif // 0

  // determine outtype
  if (ret->intype == _CF_FONTEMBED_EMB_FMT_STDFONT)
  {
    ret->outtype = ret->intype;
    if (mode & _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE)
    {
      fprintf(stderr, "Multibyte stdfonts are not possible\n");
      _cfFontEmbedEmbClose(ret);
      return (NULL);
    }
    return (ret); // never subset, no multibyte
  }
  else if (ret->intype == _CF_FONTEMBED_EMB_FMT_T1)
  {
    if (mode & _CF_FONTEMBED_EMB_C_KEEP_T1)
      ret->outtype = _CF_FONTEMBED_EMB_FMT_T1;
    else {
      ret->plan |= _CF_FONTEMBED_EMB_A_T1_TO_CFF;
      ret->outtype = _CF_FONTEMBED_EMB_FMT_CFF;
    }
  }
  else
    ret->outtype = ret->intype;
  if (ret->outtype == _CF_FONTEMBED_EMB_FMT_CFF)
  {
    if (mode & _CF_FONTEMBED_EMB_C_PDF_OT)
    {
      ret->outtype = _CF_FONTEMBED_EMB_FMT_OTF;
      ret->plan |= _CF_FONTEMBED_EMB_A_CFF_TO_OTF;
    }
  }
  else if (ret->outtype == _CF_FONTEMBED_EMB_FMT_OTF)
  {
    // TODO: no support yet;  but we want to get the FontDescriptor/Name right
    mode |= _CF_FONTEMBED_EMB_C_NEVER_SUBSET;
    if (!(mode & _CF_FONTEMBED_EMB_C_PDF_OT))
    { // TODO!?!
      ret->outtype = _CF_FONTEMBED_EMB_FMT_CFF;
      ret->plan |= _CF_FONTEMBED_EMB_A_OTF_TO_CFF;
    }
  }

  if (mode & _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE)
    ret->plan |= _CF_FONTEMBED_EMB_A_MULTIBYTE;

  // check rights (for subsetting)
  if ((ret->rights & _CF_FONTEMBED_EMB_RIGHT_NONE) ||
      (ret->rights & _CF_FONTEMBED_EMB_RIGHT_BITMAPONLY) ||
      ((ret->rights & _CF_FONTEMBED_EMB_RIGHT_READONLY) &&
       (mode & _CF_FONTEMBED_EMB_C_EDITABLE_SUBSET)) ||
      ((ret->rights & _CF_FONTEMBED_EMB_RIGHT_NO_SUBSET) &&
       (mode & _CF_FONTEMBED_EMB_C_MUST_SUBSET)))
  {
    fprintf(stderr, "The font does not permit the requested embedding\n");
    _cfFontEmbedEmbClose(ret);
    return (NULL);
  }
  else if ((!(ret->rights & _CF_FONTEMBED_EMB_RIGHT_NO_SUBSET)) &&
	   (!(mode & _CF_FONTEMBED_EMB_C_NEVER_SUBSET)))
    ret->plan |= _CF_FONTEMBED_EMB_A_SUBSET;

  // alloc subset
  if (ret->plan & _CF_FONTEMBED_EMB_A_SUBSET)
  {
    ret->subset = _cfFontEmbedBitSetNew(numGlyphs);
    if (!ret->subset)
    {
      fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
      _cfFontEmbedEmbClose(ret);
      return (NULL);
    }
  }

  return (ret);
}
// }}}


int
_cfFontEmbedEmbEmbed(_cf_fontembed_emb_params_t *emb,
		     _cf_fontembed_output_fn_t output,
		     void *context) // {{{
{
  DEBUG_assert(emb);

  if (emb->dest == _CF_FONTEMBED_EMB_DEST_NATIVE)
  {
  }
  else if (emb->dest <= _CF_FONTEMBED_EMB_DEST_PS)
  {
    int ret =- 2;
    const char *fontname =
      __cfFontEmbedEmbOTFGetFontName(emb->font->sfnt); // TODO!!
    (*output)("%%BeginFont: ", 13, context);
    (*output)(fontname, strlen(fontname), context);
    (*output)("\n", 1, context);
    if (emb->outtype == _CF_FONTEMBED_EMB_FMT_T1)
    {
    }
    else if (emb->outtype == _CF_FONTEMBED_EMB_FMT_TTF)
    { // emb->outtype==EMB_OUTPUT_OTF
                                            // is stupid (?)
                                            // do Type42
      ret = __cfFontEmbedEmbOTFPS(emb->font->sfnt, NULL, 256, NULL, output,
				  context); // TODO?
    }
    else if (emb->outtype == _CF_FONTEMBED_EMB_FMT_CFF)
    {
    }
    else if (emb->outtype == _CF_FONTEMBED_EMB_FMT_STDFONT)
    {
    }
    if (ret != -2)
    {
      if (ret != -1)
        (*output)("%%EndFont\n", 10, context);
      else
        fprintf(stderr, "Failed\n");
      return (ret);
    }
  }
  else if (emb->dest <= _CF_FONTEMBED_EMB_DEST_PDF16)
  {
    if (emb->outtype == _CF_FONTEMBED_EMB_FMT_TTF)
    {
      DEBUG_assert(emb->font->sfnt);
      if (emb->plan & _CF_FONTEMBED_EMB_A_SUBSET)
        return (_cfFontEmbedOTFSubSet(emb->font->sfnt, emb->subset, output,
				      context));
      else if (emb->font->sfnt->numTTC)
        return (_cfFontEmbedOTFTTCExtract(emb->font->sfnt, output, context));
      else // copy verbatim
        return (copy_file(emb->font->sfnt->f, output, context));
    }
    else if (emb->outtype == _CF_FONTEMBED_EMB_FMT_OTF)
    {
      if (emb->plan & _CF_FONTEMBED_EMB_A_CFF_TO_OTF)
      {
        if (emb->plan & _CF_FONTEMBED_EMB_A_T1_TO_CFF)
	{
          // TODO
        }
	else
	{
          // DEBUG_assert(emb->font->cff);
          // TODO
        }
      }
      else
      {
        DEBUG_assert(emb->font->sfnt);
        if (emb->plan & _CF_FONTEMBED_EMB_A_SUBSET)
          return (_cfFontEmbedOTFSubSetCFF(emb->font->sfnt, emb->subset, output,
				 context));
        else
          return (copy_file(emb->font->sfnt->f, output, context));
      }
    }
    else if (emb->outtype == _CF_FONTEMBED_EMB_FMT_CFF)
    {
      if (emb->plan & _CF_FONTEMBED_EMB_A_OTF_TO_CFF)
      {
        DEBUG_assert(emb->font->sfnt);
        if (emb->plan & _CF_FONTEMBED_EMB_A_SUBSET)
	{
          // TODO
        }
	else
	{
          return (_cfFontEmbedOTFCFFExtract(emb->font->sfnt, output, context));
        }
      }
      else
      {
        // TODO
      }
    }
  }

  fprintf(stderr, "NOT IMPLEMENTED\n");
  DEBUG_assert(0);
  return -1;
}
// }}}


void
_cfFontEmbedEmbClose(_cf_fontembed_emb_params_t *emb) // {{{
{
  if (emb)
  {
    free(emb->subset);
    if (emb->plan & _CF_FONTEMBED_EMB_A_CLOSE_FONTFILE)
      _cfFontEmbedFontFileClose(emb->font);
    free(emb);
  }
}
// }}}

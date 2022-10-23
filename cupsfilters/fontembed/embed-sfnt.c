//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/fontembed-private.h>
#include <cupsfilters/debug-internal.h>
#include "embed-pdf-private.h"
#include "embed-sfnt-private.h"
#include "sfnt-private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


_cf_fontembed_emb_right_t
__cfFontEmbedEmbOTFGetRights(_cf_fontembed_otf_file_t *otf) // {{{
{
  _cf_fontembed_emb_right_t ret = _CF_FONTEMBED_EMB_RIGHT_FULL;

  int len;
  char *os2 =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('O', 'S', '/', '2'),
			    &len);
  if (os2)
  {
    const unsigned short os2_version = __cfFontEmbedGetUShort(os2);
    // check len
    DEBUG_assert((os2_version != 0x0000) || (len == 78));
    DEBUG_assert((os2_version != 0x0001) || (len == 86));
    DEBUG_assert((os2_version < 0x0002) || (os2_version > 0x0004) ||
		 (len == 96));
    if (os2_version <= 0x0004)
    {
      // get rights
      unsigned short fsType = __cfFontEmbedGetUShort(os2 + 8);
      // from Adobe's Fontpolicies_v9.pdf, pg 13:
      if (fsType == 0x0002)
        ret = _CF_FONTEMBED_EMB_RIGHT_NONE;
      else
      {
        ret = fsType & 0x0300; // _CF_FONTEMBED_EMB_RIGHT_BITMAPONLY,
	                       // _CF_FONTEMBED_EMB_RIGHT_NO_SUBSET
        if ((fsType & 0x000c) == 0x0004)
          ret |= _CF_FONTEMBED_EMB_RIGHT_READONLY;
      }
    }
    free(os2);
  }
  return (ret);
}
// }}}


// NOTE: statically allocated buffer

const char *
__cfFontEmbedEmbOTFGetFontName(_cf_fontembed_otf_file_t *otf) // {{{
{
  static char fontname[64];

  int len;
  const char *fname = _cfFontEmbedOTFGetName(otf, 3, 1, 0x409, 6, &len);
                                                                // Microsoft
  if (fname)
  {
    int iA, iB = 0;
    for (iA=0; (iA < 63) && (iA * 2 < len); iA ++)
    {
      if ((fname[2 * iA] == 0) &&
	  (fname[2 * iA + 1] >= 33) && (fname[2 * iA + 1] <= 126) &&
	  (!strchr("[](){}<>/%", fname[iA * 2 + 1])))
        fontname[iB ++] = fname[iA * 2 + 1];
    }
    fontname[iB] = 0;
  }
  else if ((fname = _cfFontEmbedOTFGetName(otf, 1, 0, 0, 6, &len))) // Mac
  {
    int iA, iB = 0;
    for (iA = 0; (iA < 63) && (iA < len); iA ++)
    {
      if ((fname[iA] >= 33) && (fname[iA] <= 126) &&
	  (!strchr("[](){}<>/%", fname[iA])))
        fontname[iB ++] = fname[iA];
    }
    fontname[iB] = 0;
  }
  else
    fontname[0] = 0;

  if (!*fontname)
    // TODO construct a fontname, eg from */*/*/4
    fprintf(stderr, "WARNING: no fontName\n");

  return (fontname);
}
// }}}


// TODO? monospaced by actual glyph width?
// TODO? use PCLT table? (esp. CFF, as table dircouraged for glyf fonts)

void
__cfFontEmbedEmbOTFGetPDFFontDescr(_cf_fontembed_otf_file_t *otf,
				   _cf_fontembed_emb_pdf_font_descr_t *ret)
                                                                        // {{{
{
  int len;

//  TODO
//  ... fill in struct
  char *head =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'),
			    &len);
  DEBUG_assert(head); // version is 1.0 from _cfFontEmbedOTFLoad
  ret->bbxmin = __cfFontEmbedGetShort(head + 36) * 1000 / otf->unitsPerEm;
  ret->bbymin = __cfFontEmbedGetShort(head + 38) * 1000 / otf->unitsPerEm;
  ret->bbxmax = __cfFontEmbedGetShort(head + 40) * 1000 / otf->unitsPerEm;
  ret->bbymax = __cfFontEmbedGetShort(head + 42) * 1000 / otf->unitsPerEm;
  const int macStyle = __cfFontEmbedGetUShort(head + 44);
  free(head);

  char *post =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('p', 'o', 's', 't'),
			    &len);
  DEBUG_assert(post);
  const unsigned int post_version = __cfFontEmbedGetULong(post);
  // check length
  DEBUG_assert((post_version != 0x00010000) || (len == 32));
  DEBUG_assert((post_version != 0x00020000) ||
	       (len >= 34 + 2 * otf->numGlyphs));
  DEBUG_assert((post_version != 0x00025000) || (len == 35 + otf->numGlyphs));
  DEBUG_assert((post_version != 0x00030000) || (len == 32));
  DEBUG_assert((post_version != 0x00020000) ||
	       (__cfFontEmbedGetUShort(post + 32) == otf->numGlyphs)); // v4?
  // DEBUG_assert((post_version == 0x00030000) ==
  //              (!!(otf->flags&_CF_FONTEMBED_OTF_F_FMT_CFF)));
  //                                     // Ghostscript embedding does this..
  // TODO: v4 (apple) :  uint16 reencoding[numGlyphs]
  if ((post_version == 0x00010000) ||
      (post_version == 0x00020000) ||
      (post_version == 0x00025000) ||
      (post_version == 0x00030000) ||
      (post_version == 0x00040000))
  {
    ret->italicAngle = __cfFontEmbedGetLong(post + 4) >> 16;
    if (__cfFontEmbedGetULong(post + 12) > 0) // monospaced
      ret->flags |= 1;
  }
  else
    fprintf(stderr, "WARNING: no italicAngle, no monospaced flag\n");
  free(post);

  char *os2 =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('O', 'S', '/', '2'),
			    &len);
  if (os2)
  {
    const unsigned short os2_version = __cfFontEmbedGetUShort(os2);
    // check len
    DEBUG_assert((os2_version != 0x0000) || (len == 78));
    DEBUG_assert((os2_version != 0x0001) || (len == 86));
    DEBUG_assert((os2_version < 0x0002) || (os2_version > 0x0004) ||
		 (len == 96));
    if (os2_version <= 0x0004)
    {
      // from PDF14Deltas.pdf, pg 113
      const int weightClass = __cfFontEmbedGetUShort(os2 + 4);
      ret->stemV = 50 + weightClass * weightClass / (65 * 65);
                                                     // TODO, really bad
      //printf("a %d\n", weightClass);
      if (ret->supplement >= 0)
      {
	// cid
        ret->panose = ret->data;
        memcpy(ret->panose, os2 + 30, 12); // sFamilyClass + panose
      }
      const unsigned short fsSelection = __cfFontEmbedGetUShort(os2 + 62);
      if (fsSelection & 0x01)
	// italic
        ret->flags |= 0x0040;
      if ((fsSelection & 0x10) && (weightClass > 600))
	// force bold
        ret->flags |= 0x0400;
      const unsigned char family_class = __cfFontEmbedGetUShort(os2 + 30) >> 8;
      if (family_class == 10)
	// script
        ret->flags |= 0x0008;
      if (family_class != 8)
	// not sans-serif
        ret->flags |= 0x0002;

      ret->avgWidth = __cfFontEmbedGetShort(os2 + 2) * 1000 / otf->unitsPerEm;
      ret->ascent = __cfFontEmbedGetShort(os2 + 68) * 1000 / otf->unitsPerEm;
      ret->descent = __cfFontEmbedGetShort(os2 + 70) * 1000 / otf->unitsPerEm;
      if (os2_version >= 0x0002)
      {
        ret->xHeight = __cfFontEmbedGetShort(os2 + 86) * 1000 / otf->unitsPerEm;
        ret->capHeight = __cfFontEmbedGetShort(os2 + 88) * 1000 /
	  otf->unitsPerEm;
      } // else capHeight fixed later
    }
    else
    {
      free(os2);
      os2 = NULL;
    }
  }
  else
  {
    fprintf(stderr,"WARNING: no OS/2 table\n");
    // e.g. Subsetted font from Ghostscript // e.g. CFF
  }
  if (os2)
    free(os2);
  else
  {
    // TODO (if(CFF))
    fprintf(stderr, "WARNING: no ascent/descent, capHeight, stemV, flags\n");
    if (macStyle & 0x01) // force bold - just do it on bold
      ret->flags |= 0x0400;
    if (macStyle & 0x02) // italic
      ret->flags |= 0x0004;
    //  ... flags TODO? (Serif, Script, Italic, AllCap,SmallCap, ForceBold)
  }

  // ? maybe get ascent, descent, capHeight, xHeight, stemV directly from cff
  // Fallbacks
  if ((!ret->ascent) || (!ret->descent))
  {
    char *hhea =
      _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'h', 'e', 'a'),
			      &len);
    if (hhea)
    {
      ret->ascent = __cfFontEmbedGetShort(hhea + 4) * 1000 / otf->unitsPerEm;
      ret->descent = __cfFontEmbedGetShort(hhea + 6) * 1000 / otf->unitsPerEm;
    }
    free(hhea);
  }
  if (!ret->stemV)
  {
    // TODO? use name
    const unsigned short d_gid = _cfFontEmbedOTFFromUnicode(otf, '.');
    if (d_gid)
    {
      // stemV=bbox['.'].width;
      len = _cfFontEmbedOTFGetGlyph(otf, d_gid);
      DEBUG_assert(len >= 10);
      ret->stemV = (__cfFontEmbedGetShort(otf->gly + 6) -
		    __cfFontEmbedGetShort(otf->gly + 2)) * 1000 /
	otf->unitsPerEm;
    }
    else
    {
      if (macStyle & 1)
	// bold
        ret->stemV = 165;
      else
        ret->stemV = 109; // TODO... unserious values...
    }
  }
  if (!ret->capHeight)
    // TODO? only reqd. for fonts with latin...
    ret->capHeight = ret->ascent;
    // TODO: OTF spec says:  use metrics of 'H' (0 if not available)
  if (0) // TODO? uses only adobe latin standard? ?? e.g. Type1
    ret->flags |= 0x0020;
  else
    ret->flags |= 0x0004;
  // TODO SmallCap by font name(?)

// TODO ;   ? cid ?
}
// }}}


// TODO: split generic part and otf part
// TODO: FIXME: gid vs. char   ... NOTE: not called in multi_byte mode...
// Adobe does: char --MacRoman/WinAnsi--> name --AGL-->
//    unicode --cmap(3, 1) --> gid   only avoidable by setting
//    'symbol' + custom(1, 0) / (3, 0)
// HINT: caller sets len == otf->numGlyphs   (only when not using encoding...)

_cf_fontembed_emb_pdf_font_widths_t *
__cfFontEmbedEmbOTFGetPDFWidths(_cf_fontembed_otf_file_t *otf,
				const unsigned short *encoding,
				int len,
				const _cf_fontembed_bit_set_t glyphs)
                                            // {{{ glyphs == NULL ->
                                            //        all from 0 to len
{
  DEBUG_assert(otf);

  int first = len, last = 0;
  int iA;

  if (glyphs)
  {
    for (iA = 0; iA < len; iA ++) // iA is a "gid" when in multi_byte mode...
    {
      const int gid = (encoding) ?
	encoding[iA] : _cfFontEmbedOTFFromUnicode(otf, iA); // TODO
      if (_cfFontEmbedBitCheck(glyphs, gid))
      {
        if (first > iA)
	  // first is a character index
          first = iA;
        if (last < iA)
          last = iA;
      }
    }
  }
  else
  {
    first = 0;
    last = len;
  }
  if (last < first)
  {
    // empty
    fprintf(stderr, "WARNING: empty embedding range\n");
    return (NULL);
  }

  // ensure hmtx is there
  if (!otf -> hmtx)
  {
    if (__cfFontEmbedOTFLoadMore(otf) != 0)
    {
      fprintf(stderr, "Unsupported OTF font / cmap table \n");
      return (NULL);
    }
  }

  // now create the array
  _cf_fontembed_emb_pdf_font_widths_t *ret =
    __cfFontEmbedEmbPDFFWNew(last - first + 1);
  if (!ret)
    return (NULL);
  ret->first = first;
  ret->last = last;
  ret->widths = ret->data;
  for (iA = 0; first <= last; iA ++, first ++)
  {
    const int gid = (encoding) ?
      encoding[first] : _cfFontEmbedOTFFromUnicode(otf, first); // TODO
    if (gid >= otf->numGlyphs)
    {
      fprintf(stderr, "Bad glyphid\n");
      DEBUG_assert(0);
      free(ret);
      return (NULL);
    }
    if ((!glyphs) || (_cfFontEmbedBitCheck(glyphs, gid)))
      ret->widths[iA] =
	__cfFontEmbedGetWidthFast(otf, gid) * 1000 / otf->unitsPerEm;
      // else 0 from calloc
  }

  return (ret);
}
// }}}


// otf->hmtx must be there

static int
emb_otf_pdf_glyphwidth(void *context, int gid) // {{{
{
  _cf_fontembed_otf_file_t *otf = (_cf_fontembed_otf_file_t *)context;
  return (__cfFontEmbedGetWidthFast(otf, gid) * 1000 / otf->unitsPerEm);
}
// }}}


_cf_fontembed_emb_pdf_font_widths_t *
__cfFontEmbedEmbOTFGetPDFCIDWidths(_cf_fontembed_otf_file_t *otf,
				   const _cf_fontembed_bit_set_t glyphs) // {{{
                                               // glyphs == NULL -> output all
{
  DEBUG_assert(otf);

  // ensure hmtx is there
  if (!otf->hmtx)
  {
    if (__cfFontEmbedOTFLoadMore(otf) != 0)
    {
      fprintf(stderr, "Unsupported OTF font / cmap table \n");
      return NULL;
    }
  }
  // int dw = emb_otf_pdf_glyphwidth(otf, 0); // e.g.
  int dw = -1; // let them estimate

  return (__cfFontEmbedEmbPDFFWCIDWidths(glyphs, otf->numGlyphs, dw,
					 emb_otf_pdf_glyphwidth, otf));
}
// }}}


/*** PS stuff ***/

#include "dynstring-private.h"

const char *aglfn13(unsigned short uni); // aglfn13.c
#include "macroman-private.h"


// TODO? optimize pascal string skipping? (create index)
// NOTE: might return a statically allocated string

static const char *
emb_otf_get_post_name(const char *post,
		      unsigned short gid) // {{{
{
  if (!post)
    return (NULL);
  const unsigned int post_version = __cfFontEmbedGetULong(post);
  if (post_version == 0x00010000) // font has only 258 chars...
                                  // font cannot be used on windows
  {
    if (gid < sizeof(__cf_fontembed_mac_roman) /
	sizeof(__cf_fontembed_mac_roman[0]))
      return (__cf_fontembed_mac_roman[gid]);
  }
  else if (post_version == 0x00020000)
  {
    const unsigned short num_glyphs = __cfFontEmbedGetUShort(post + 32);
    // DEBUG_assert(num_glyphs == otf->numGlyphs);
    if (gid < num_glyphs)
    {
      unsigned short idx = __cfFontEmbedGetUShort(post + 34 + 2 * gid);
      if (idx < 258)
      {
        if (idx < sizeof(__cf_fontembed_mac_roman) /
	    sizeof(__cf_fontembed_mac_roman[0]))
          return (__cf_fontembed_mac_roman[idx]);
      }
      else if (idx < 32768)
      {
        const unsigned char *pos = (unsigned char *)post + 34 + 2 * num_glyphs;
        for (idx -= 258; idx > 0; idx --) // this sucks...
          pos += *pos + 1; // skip this string
        // convert pascal string to asciiz
        static char ret[256];
        const unsigned char len = *pos;
        memcpy(ret, (const char *)pos + 1, len);
        ret[len] = 0;
        return (ret);
      }
    }
  }
  else if (post_version == 0x00025000)
  {
    // similiar to 0x00010000, deprecated
    const unsigned short num_glyphs = __cfFontEmbedGetUShort(post + 32);
    if (gid < num_glyphs)
    {
      const unsigned short idx = post[34 + gid] + gid; // post is signed char *
      if (idx < sizeof(__cf_fontembed_mac_roman) /
	  sizeof(__cf_fontembed_mac_roman[0]))
        return (__cf_fontembed_mac_roman[idx]);
    }
  }
  else if (post_version == 0x00030000)
  {
    // no glyph names, sorry
    //  } else if (post_version == 0x00040000) { // apple AAT ?!
  }
  return (NULL);
}
// }}}


// TODO!? to_unicode should be able to represent more than one unicode
//        character?
// NOTE: statically allocated string

static const char *
get_glyphname(const char *post,
	      unsigned short *to_unicode,
	      int charcode,
	      unsigned short gid) // {{{ if charcode==0 -> force gid to be used
{
  if (gid == 0)
    return (".notdef");

  const char *postName = emb_otf_get_post_name(post, gid);
  if (postName)
    return (postName);

  static char ret[255];
  if (charcode)
  {
    if (to_unicode) // i.e. encoding was there
      charcode = to_unicode[charcode];
      // TODO!? to_unicode should be able to represent more than one unicode
      // character?
      // TODO for additional credit: for ligatures, etc  create
      // /f_f /uni12341234  or the like
    const char *aglname = aglfn13(charcode); // TODO? special case ZapfDingbats?
    if (aglname)
      return (aglname);
    snprintf(ret, 250, "uni%04X", charcode); // allows extraction
  }
  else
    snprintf(ret, 250, "c%d", gid);  // last resort: only by gid
  return (ret);
}
// }}}


struct OUTFILTER_PS
{
  _cf_fontembed_output_fn_t out;
  void *ctx;
  int len;
};


// TODO: for maximum compatiblity (PS<2013 interpreter)  split only on table
// or glyph boundary (needs lookup in loca table!)
// Note: table boundaries are at each call!

static void
outfilter_ascii_ps(const char *buf,
		   int len,
		   void *context)  // {{{
{
  struct OUTFILTER_PS *of = context;
  _cf_fontembed_output_fn_t out = of->out;
  int iA;

  (*out)("<", 1, of->ctx);
  of->len ++;

  const char *last = buf;
  char tmp[256];
  while (len > 0)
  {
    for (iA = 0; (iA < 76) && (len > 0); iA += 2, len --)
    {
      const unsigned char ch = buf[iA >> 1];
      tmp[iA] = "0123456789abcdef"[ch >> 4];
      tmp[iA + 1] = "0123456789abcdef"[ch & 0x0f];
    }
    buf += iA >> 1;
    if (buf < last + 64000)
    {
      if (len > 0)
        tmp[iA++] = '\n';
      (*out)(tmp, iA, of->ctx);
    }
    else
    {
      last = buf;
      strcpy(tmp + iA, "00>\n<");
      iA += 5;
      (*out)(tmp, iA, of->ctx);
    }
    of->len += iA;
  }

  (*out)("00>\n",4,of->ctx);
  of->len+=4;
}
// }}}


static void
outfilter_binary_ps(const char *buf,
		    int len,
		    void *context)  // {{{
{
  struct OUTFILTER_PS *of = context;
  _cf_fontembed_output_fn_t out = of->out;

  char tmp[100];
  while (len > 0)
  {
    const int maxlen = (len > 64000) ? 64000 : len;
    const int l = sprintf(tmp, "%d RD ", maxlen);
    (*out)(tmp, l, of->ctx);
    of->len += l;

    (*out)(buf, maxlen, of->ctx);
    (*out)("\n", 1, of->ctx);
    of->len += maxlen + 1;
    len -= maxlen;
    buf += maxlen;
  }
}
// }}}


//
//  encoding:  character-code ->
//             glyph id  ["required", NULL: identity, i.e. from_unicode()]
//                                                // TODO: respect subsetting
//  to_unicode:  character-code ->
//               unicode  [NULL: no char names]
//                                // kind-of "reverse" of encoding (to_unicode
//                                // does not make sense without >encoding)
//
// Status:
//  - we need a 0..255 encoding to be used in the PS file
//  - we want to allow the use of encoding[];  this should map from your
//    desired PS-stream output character (0..255) directly to the gid
//  - if encoding[] is not used, MacRoman/WinAnsi/latin1 is expected
//    (easiest: latin1, as it is a subset of unicode)
//    i.e. your want to output latin1 to the PS-stream
//  - len is the length of >encoding, or the "last used latin1 character"
//  - oh. in multibyte-mode no >encoding probably should mean
//    identity(gid->gid) not (latin1->gid)
//  - non-multibyte PDF -> only 255 chars  ... not recommended (we can't
//    just map to gids, but only to names, which acro will then cmap(3, 1)
//    to gids)
//
//  => problem with subsetting _cf_fontembed_bit_set_t (keyed by gid); we want
//     _cf_fontembed_bit_set_t keyed by 0..255 (via encoding)
//
//   TODO: a) multi font encoding
//   TODO: b) cid/big font encoding (PS >= 2015) [/CIDFontType 2] : CMap does
//            Charcode->CID, /CIDMap does CID->GID [e.g. Identity/delta value]
//            (also needed [or a)] for loca>64000 if split, etc)
//            e.g. /CIDMap 0  [requires PS >= 3011?]
//            [Danger: do not split composites]
//   TODO? incremental download [/GlyphDirectory array or dict] :
//         /GlyphDirectory does GID-><glyf entry> mapping
//         need 'fake' gdir table (size,offset=0) in sfnt; loca, glyf can be
//         ommited; hmtx can be omitted for PS >= 3011 [/MetricsCount 2]
//         idea is to fill initial null entries in the array/dict
//         [Beware of save/restore!]
//   NOTE: even when subsetting the font has to come first in the PS file
//
// ... special information: when multi-byte PDF encoding is used <gid> is
//     output.
//     therefore /Encoding /Identity-H + /CIDSystemInfo Adobe-Identity-0 will
//     yield 1-1 mapping for font.
//     problem is that text is not selectable. therefore there is the
//     /ToUnicode CMap option
//

int
__cfFontEmbedEmbOTFPS(_cf_fontembed_otf_file_t *otf,
		      unsigned short *encoding,
		      int len,
		      unsigned short *to_unicode,
		      _cf_fontembed_output_fn_t output,
		      void *context) // {{{
{
  const int binary = 0; // binary format? // TODO
  if (len > 256)
  {
    fprintf(stderr, "Encoding too big(%d) for Type42\n", len);
    return (-1);
  }
  if (len < 1)
  {
    fprintf(stderr, "At least .notdef required in Type42\n");
    return (-1);
  }
  if (!encoding)
    to_unicode = NULL; // does not make sense

  int iA, ret=0;

  __cf_fontembed_dyn_string_t ds;
  if (__cfFontEmbedDynInit(&ds, 1024) == -1)
    return (-1);

  int rlen = 0;
  char *head =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'),
			    &rlen);
  if (!head)
  {
    free(ds.buf);
    return (-1);
  }
  __cfFontEmbedDynPrintF(&ds, "%%!PS-TrueTypeFont-%d-%d\n",
	     otf->version, __cfFontEmbedGetULong(head + 4));
  const int bbxmin = __cfFontEmbedGetShort(head + 36) * 1000 / otf->unitsPerEm,
            bbymin = __cfFontEmbedGetShort(head + 38) * 1000 / otf->unitsPerEm,
            bbxmax = __cfFontEmbedGetShort(head + 40) * 1000 / otf->unitsPerEm,
            bbymax = __cfFontEmbedGetShort(head + 42) * 1000 / otf->unitsPerEm;
  free(head);

  char *post =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('p', 'o', 's', 't'),
			    &rlen);
  if ((!post) && (rlen != -1)) // other error than "not found"
  {
    free(ds.buf);
    return (-1);
  }
  if (post)
  {
    const unsigned int minMem = __cfFontEmbedGetULong(post + 16),
                       maxMem = __cfFontEmbedGetULong(post + 20);
    if (minMem)
      __cfFontEmbedDynPrintF(&ds, "%%VMusage: %d %d\n", minMem, maxMem);
  }

  // don't forget the coordinate scaling...
  __cfFontEmbedDynPrintF(&ds, "11 dict begin\n"
			 "/FontName /%s def\n"
			 "/FontType 42 def\n"
			 "/FontMatrix [1 0 0 1 0 0] def\n"
			 "/FontBBox [%f %f %f %f] def\n"
			 "/PaintType 0 def\n",
			 //"/XUID [42 16#%X 16#%X 16#%X 16#%X] def\n"
			 //   // TODO?!? (md5 of font data)  (16# means base16)
			 __cfFontEmbedEmbOTFGetFontName(otf),
			 bbxmin / 1000.0, bbymin / 1000.0,
			 bbxmax / 1000.0, bbymax / 1000.0);
  if (post)
    __cfFontEmbedDynPrintF(&ds,"/FontInfo 4 dict dup begin\n"
                           // TODO? [even non-post]:
	                   // /version | /Notice | /Copyright | /FullName |
			   // /FamilyName| /Weight
			   //   () readonly def\n   from name table: 5 7 0 4 1 2
			   // using:
			   // _cfFontEmbedOTFGetName(otf, 3, 1, 0x409, ?,&len) /
			   // _cfFontEmbedOTFGetName(otf, 1, 0, 0, ?, &len) +
			   //   encoding
			   "  /ItalicAngle %d def\n"
			   "  /isFixedPitch %s def\n"
			   "  /UnderlinePosition %f def\n"
			   "  /UnderlineThickness %f def\n"
			   "end readonly def\n",
			   __cfFontEmbedGetLong(post + 4) >> 16,
			   (__cfFontEmbedGetULong(post + 12) ?
			    "true" : "false"),
			   (__cfFontEmbedGetShort(post + 8) -
			    __cfFontEmbedGetShort(post + 10) / 2) /
			   (float)otf->unitsPerEm,
			   __cfFontEmbedGetShort(post + 10) /
			   (float)otf->unitsPerEm);
  __cfFontEmbedDynPrintF(&ds, "/Encoding 256 array\n"
			 "0 1 255 { 1 index exch /.notdef put } for\n");
  for (iA = 0; iA < len; iA ++) // encoding data: 0...255 -> /glyphname
  {
    const int gid =
      (encoding) ? encoding[iA] : _cfFontEmbedOTFFromUnicode(otf, iA);
    if (gid != 0)
      __cfFontEmbedDynPrintF(&ds, "dup %d /%s put\n",
			     iA, get_glyphname(post, to_unicode, iA, gid));
  }
  __cfFontEmbedDynPrintF(&ds, "readonly def\n");

  if (binary)
    __cfFontEmbedDynPrintF(&ds,
	  "/RD { string currentfile exch readstring pop } executeonly def\n");
  __cfFontEmbedDynPrintF(&ds, "/sfnts[\n");

  if (ds.len < 0)
  {
    free(post);
    free(ds.buf);
    return (-1);
  }
  (*output)(ds.buf, ds.len, context);
  ret += ds.len;
  ds.len = 0;

  // TODO: only tables as in _cfFontEmbedOTFSubSet
  // TODO:  somehow communicate table boundaries:
  //   __cfFontEmbedOTFActionCopy  does exactly one output call (per table)
  //   only __cfFontEmbedOTFActionReplace might do two (padding)
  // {{{ copy tables verbatim (does not affect ds .len)

  struct __cf_fontembed_otf_write_s *otfree = NULL;
#if 0
  struct __cf_fontembed_otf_write_s *otw;
  otwfree = otw =
    malloc(sizeof(struct __cf_fontembed_otf_write_s) * otf->numTables);
  if (!otw)
  {
    fprintf(stderr, "Bad alloc: %m\n");
    free(post);
    free(ds.buf);
    return (-1);
  }
  // just copy everything
  for (iA = 0; iA < otf->numTables; iA ++)
  {
    otw[iA].tag = otf->tables[iA].tag;
    otw[iA].action = __cfFontEmbedOTFActionCopy;
    otw[iA].param = otf;
    otw[iA].length = iA;
  }
  int numTables = otf->numTables;
#else
  struct __cf_fontembed_otf_write_s otw[] = // sorted
  {
    {_CF_FONTEMBED_OTF_TAG('c', 'm', 'a', 'p'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('c', 'v', 't', ' '),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('f', 'p', 'g', 'm'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('g', 'l', 'y', 'f'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('h', 'h', 'e', 'a'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('h', 'm', 't', 'x'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('l', 'o', 'c', 'a'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('m', 'a', 'x', 'p'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('n', 'a', 'm', 'e'),
     __cfFontEmbedOTFActionCopy, otf,},
    {_CF_FONTEMBED_OTF_TAG('p', 'r', 'e', 'p'),
     __cfFontEmbedOTFActionCopy, otf,},
    // vhea vmtx (never used in PDF, but possible in PS >= 3011)
    {0,0,0,0}};
  int numTables = __cfFontEmbedOTFIntersectTables(otf, otw);
#endif

  struct OUTFILTER_PS of;
  of.out = output;
  of.ctx = context;
  of.len = 0;
  if (binary)
    iA = __cfFontEmbedOTFWriteSFNT(otw, otf->version, numTables,
				   outfilter_binary_ps, &of);
  else
    iA = __cfFontEmbedOTFWriteSFNT(otw, otf->version, numTables,
				   outfilter_ascii_ps, &of);
  free(otfree);
  if (iA == -1)
  {
    free(post);
    free(ds.buf);
    return (-1);
  }
  ret += of.len;
  // }}} done copying

  __cfFontEmbedDynPrintF(&ds, "] def\n");

  __cfFontEmbedDynPrintF(&ds, "/CharStrings %d dict dup begin\n"
			 "/.notdef 0 def\n", len);
  for (iA = 0; iA < len; iA ++) // charstrings data: /glyphname -> gid
  {
    const int gid =
      (encoding) ? encoding[iA] : _cfFontEmbedOTFFromUnicode(otf, iA);
    if (gid)
      __cfFontEmbedDynPrintF(&ds, "/%s %d def\n",
			     get_glyphname(post, to_unicode, iA, gid), gid);
    // (respecting subsetting...)
  }
  __cfFontEmbedDynPrintF(&ds, "end readonly def\n");
  __cfFontEmbedDynPrintF(&ds, "FontName currentdict end definefont pop\n");
  free(post);

  if (ds.len < 0)
  {
    free(ds.buf);
    return (-1);
  }
  (*output)(ds.buf, ds.len, context);
  ret += ds.len;
  ds.len = 0;

  free(ds.buf);
  return (ret);
}
// }}}

//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/fontembed-private.h>
#include <cupsfilters/debug-internal.h>
#include "sfnt-private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


int
_cfFontEmbedOTFTTCExtract(_cf_fontembed_otf_file_t *otf,
			  _cf_fontembed_output_fn_t output,
			  void *context) // {{{
{
  DEBUG_assert(otf);
  DEBUG_assert(output);
  DEBUG_assert(otf->numTTC);

  int iA;

  struct __cf_fontembed_otf_write_s *otw;
  otw = malloc(sizeof(struct __cf_fontembed_otf_write_s) * otf->numTables);
  if (!otw)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
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
  iA = __cfFontEmbedOTFWriteSFNT(otw, otf->version, otf->numTables, output,
				 context);
  free(otw);

  return (iA);
}
// }}}


// otw {0, }-terminated, will be modified; returns numTables for
// __cfFontEmbedOTFWriteSFNT
int
__cfFontEmbedOTFIntersectTables(_cf_fontembed_otf_file_t *otf,
				struct __cf_fontembed_otf_write_s *otw) // {{{
{
  int iA, iB, numTables = 0;

  for (iA = 0, iB = 0; (iA < otf->numTables) && (otw[iB].tag);)
  {
    if (otf->tables[iA].tag == otw[iB].tag)
    {
      if (otw[iB].action == __cfFontEmbedOTFActionCopy)
        otw[iB].length = iA; // original table location found.
      if (iB != numTables) // >, actually
        memmove(otw + numTables, otw + iB,
		sizeof(struct __cf_fontembed_otf_write_s));
      iA ++;
      iB ++;
      numTables ++;
    }
    else if (otf->tables[iA].tag < otw[iB].tag)
      iA ++;
    else // not in otf->tables
    {
      if (otw[iB].action != __cfFontEmbedOTFActionCopy) // keep
      {
        if (iB != numTables) // >, actually
          memmove(otw + numTables, otw + iB,
		  sizeof(struct __cf_fontembed_otf_write_s));
        numTables ++;
      }
      // else delete
      iB ++;
    }
  }
  return (numTables);
}
// }}}


// include components (set bit in >glyphs) of currently loaded compound glyph
//   (with >curgid)
// returns additional space requirements (when bits below >donegid are touched)

static int
otf_subset_glyf(_cf_fontembed_otf_file_t *otf,
		int curgid,
		int donegid,
		_cf_fontembed_bit_set_t glyphs) // {{{
{
  int ret = 0;
  if (__cfFontEmbedGetShort(otf->gly) >= 0) // not composite
    return (ret); // done

  char *cur = otf->gly + 10;

  unsigned short flags;
  do
  {
    flags = __cfFontEmbedGetUShort(cur);
    const unsigned short sub_gid = __cfFontEmbedGetUShort(cur + 2);
    DEBUG_assert(sub_gid < otf->numGlyphs);
    if (!_cfFontEmbedBitCheck(glyphs, sub_gid))
    {
      // bad: temporarily load sub glyph
      const int len = _cfFontEmbedOTFGetGlyph(otf, sub_gid);
      DEBUG_assert(len > 0);
      _cfFontEmbedBitSet(glyphs, sub_gid);
      if (sub_gid < donegid)
      {
        ret += len;
        ret += otf_subset_glyf(otf, sub_gid, donegid, glyphs);
	                        // composite of composites?, e.g. in DejaVu
      }
#ifdef DEBUG
      const int res =
#endif
	_cfFontEmbedOTFGetGlyph(otf, curgid); // reload current glyph
      DEBUG_assert(res);
    }

    // skip parameters
    cur += 6;
    if (flags & 0x01)
      cur += 2;
    if (flags & 0x08)
      cur += 2;
    else if (flags & 0x40)
      cur += 4;
    else if (flags & 0x80)
      cur += 8;
  }
  while (flags & 0x20); // more components

  return (ret);
}
// }}}


// TODO: cmap only required in non-CID context

int
_cfFontEmbedOTFSubSet(_cf_fontembed_otf_file_t *otf,
		      _cf_fontembed_bit_set_t glyphs,
		      _cf_fontembed_output_fn_t output,
		      void *context) // {{{ - returns number of bytes written
{
  DEBUG_assert(otf);
  DEBUG_assert(glyphs);
  DEBUG_assert(output);

  int iA, b, c;

  // first pass: include all required glyphs
  _cfFontEmbedBitSet(glyphs, 0); // .notdef always required
  int glyfSize = 0;
  for (iA = 0, b = 0, c = 1; iA < otf->numGlyphs; iA ++, c <<= 1)
  {
    if (!c)
    {
      b ++;
      c = 1;
    }
    if (glyphs[b] & c)
    {
      int len = _cfFontEmbedOTFGetGlyph(otf, iA);
      if (len < 0)
      {
        DEBUG_assert(0);
        return (-1);
      }
      else if (len > 0)
      {
        glyfSize += len;
        len = otf_subset_glyf(otf, iA, iA, glyphs);
        if (len < 0)
	{
          DEBUG_assert(0);
          return (-1);
        }
        glyfSize += len;
      }
    }
  }

  // second pass: calculate new glyf and loca
  int locaSize = (otf->numGlyphs + 1) * (otf->indexToLocFormat + 1) * 2;

  char *new_loca = malloc(locaSize);
  char *new_glyf = malloc(glyfSize);
  if ((!new_loca) || (!new_glyf))
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    DEBUG_assert(0);
    free(new_loca);
    free(new_glyf);
    return (-1);
  }

  int offset = 0;
  for (iA = 0, b = 0, c = 1; iA < otf->numGlyphs; iA ++, c <<= 1)
  {
    if (!c)
    {
      b ++;
      c = 1;
    }

    DEBUG_assert(offset % 2 == 0);
    // TODO? change format? if glyfSize < 0x20000
    if (otf->indexToLocFormat == 0)
      __cfFontEmbedSetUShort(new_loca + iA * 2, offset / 2);
    else // ==1
      __cfFontEmbedSetULong(new_loca + iA * 4, offset);

    if (glyphs[b] & c)
    {
      const int len = _cfFontEmbedOTFGetGlyph(otf, iA);
      DEBUG_assert(len >= 0);
      memcpy(new_glyf + offset, otf->gly, len);
      offset += len;
    }
  }
  // last entry
  if (otf->indexToLocFormat == 0)
    __cfFontEmbedSetUShort(new_loca + otf->numGlyphs * 2, offset / 2);
  else // ==1
    __cfFontEmbedSetULong(new_loca + otf->numGlyphs * 4, offset);
  DEBUG_assert(offset == glyfSize);

  // determine new tables.
  struct __cf_fontembed_otf_write_s otw[] = // sorted
  {
    // TODO: cmap only required in non-CID context   or always in CFF
    {_CF_FONTEMBED_OTF_TAG('c', 'm', 'a', 'p'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('c', 'v', 't', ' '),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('f', 'p', 'g', 'm'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('g', 'l', 'y', 'f'),
     __cfFontEmbedOTFActionReplace, new_glyf, glyfSize},
    {_CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'),
     __cfFontEmbedOTFActionCopy, otf, }, // _copy_head
    {_CF_FONTEMBED_OTF_TAG('h', 'h', 'e', 'a'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('h', 'm', 't', 'x'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('l', 'o', 'c', 'a'),
     __cfFontEmbedOTFActionReplace, new_loca, locaSize},
    {_CF_FONTEMBED_OTF_TAG('m', 'a', 'x', 'p'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('n', 'a', 'm', 'e'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('p', 'r', 'e', 'p'),
     __cfFontEmbedOTFActionCopy, otf, },
    // vhea vmtx (never used in PDF, but possible in PS>=3011)
    {0, 0, 0, 0}
  };

  // and write them
  int numTables = __cfFontEmbedOTFIntersectTables(otf, otw);
  int ret = __cfFontEmbedOTFWriteSFNT(otw, otf->version, numTables, output,
				      context);

  free(new_loca);
  free(new_glyf);
  return (ret);

  //TODO ? reduce cmap [to (1,0) ;-)]
  //TODO (cmap for non-cid)
}
// }}}


// TODO no subsetting actually done (for now)

int
_cfFontEmbedOTFSubSetCFF(_cf_fontembed_otf_file_t *otf,
			 _cf_fontembed_bit_set_t glyphs,
			 _cf_fontembed_output_fn_t output,
			 void *context) // {{{ - returns number of bytes written
{
  DEBUG_assert(otf);
  DEBUG_assert(output);

  // TODO char *new_cff = cff_subset(...);

  // determine new tables.
  struct __cf_fontembed_otf_write_s otw[] =
  {
    {_CF_FONTEMBED_OTF_TAG('C', 'F', 'F', ' '),
     __cfFontEmbedOTFActionCopy, otf, },
//  {_CF_FONTEMBED_OTF_TAG('C', 'F', 'F', ' '),
//   __cfFontEmbedOTFActionReplace, new_glyf, glyfSize},
    {_CF_FONTEMBED_OTF_TAG('c', 'm', 'a', 'p'),
     __cfFontEmbedOTFActionCopy, otf, },
#if 0 // not actually needed!
    {_CF_FONTEMBED_OTF_TAG('c', 'v', 't', ' '),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('f', 'p', 'g', 'm'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'),
     __cfFontEmbedOTFActionCopy, otf, }, // _copy_head
    {_CF_FONTEMBED_OTF_TAG('h', 'h', 'e', 'a'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('h', 'm', 't', 'x'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('m', 'a', 'x', 'p'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('n', 'a', 'm', 'e'),
     __cfFontEmbedOTFActionCopy, otf, },
    {_CF_FONTEMBED_OTF_TAG('p', 'r', 'e', 'p'),
     __cfFontEmbedOTFActionCopy, otf, },
#endif // 0
    {0, 0, 0, 0}
  };

  // and write them
  int numTables = __cfFontEmbedOTFIntersectTables(otf, otw);
  int ret = __cfFontEmbedOTFWriteSFNT(otw, otf->version, numTables, output,
				      context);

//  free(new_cff);
  return (ret);
}
// }}}


//int copy_block(FILE *f, long pos, int length,
//               _cf_fontembed_output_fn_t output,
//               void *context); // copied bytes or -1 (also on premature EOF)

static int
copy_block(FILE *f,
	   long pos,
	   int length,
	   _cf_fontembed_output_fn_t output,
	   void *context) // {{{
{
  DEBUG_assert(f);
  DEBUG_assert(output);

  char buf[4096];
  int iA, ret;

  ret = fseek(f, pos, SEEK_SET);
  if (ret == -1)
  {
    fprintf(stderr, "Seek failed: %s\n", strerror(errno));
    return (-1);
  }
  ret = 0;
  while (length > 4096)
  {
    iA = fread(buf, 1, 4096, f);
    if (iA < 4096)
      return (-1);
    (*output)(buf, iA, context);
    ret += iA;
    length -= iA;
  }
  iA = fread(buf, 1, length, f);
  if (iA < length)
    return (-1);
  (*output)(buf, iA, context);
  ret += iA;

  return (ret);
}
// }}}


int
_cfFontEmbedOTFCFFExtract(_cf_fontembed_otf_file_t *otf,
			  _cf_fontembed_output_fn_t output,
			  void *context) // {{{ - returns number of bytes
                                         //       written
{
  DEBUG_assert(otf);
  DEBUG_assert(output);

  int idx =
    __cfFontEmbedOTFFindTable(otf, _CF_FONTEMBED_OTF_TAG('C', 'F', 'F', ' '));
  if (idx == -1)
    return (-1);

  const _cf_fontembed_otf_dir_ent_t *table = otf->tables + idx;

  return (copy_block(otf->f, table->offset, table->length, output, context));
}
// }}}


// CFF *otf_get_cff(); // not load, but create by "substream"-in ctor

#if 0 // TODO elsewhere : char *cff_subset(...);
  // first pass: include all required glyphs
  _cfFontEmbedBitSet(glyphs, 0); // .notdef always required
  int glyfSize = 0;
  for (iA = 0, b = 0, c = 1; iA < otf->numGlyphs; iA ++, c <<= 1)
  {
    if (!c)
    {
      b ++;
      c = 1;
    }
    if (glyphs[b] & c)
    {
      // TODO: cff_glyph
    }
  }

  // second pass: calculate new glyf and loca
  char *new_cff = malloc(cffSize);
  if (!new_cff)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    DEBUG_assert(0);
    return (-1);
  }

  int offset = 0;
  for (iA = 0, b = 0, c = 1; iA < otf->numGlyphs; iA ++, c <<= 1)
  {
    if (!c)
    {
      b ++;
      c = 1;
    }
    if (glyphs[b] & c)
    {
      //...
    }
  }
  return (new_cff);
#endif // 0

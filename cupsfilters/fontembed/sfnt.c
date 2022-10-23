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


// TODO?
// __cfFontEmbedGetShort(head + 48) // fontDirectionHint
// reqd. Tables: cmap, head, hhea, hmtx, maxp, name, OS/2, post
// OTF: glyf, loca [cvt, fpgm, prep]
//

static void
otf_bsearch_params(int num, // {{{
		   int recordSize,
		   int *searchRange,
		   int *entrySelector,
		   int *rangeShift)
{
  DEBUG_assert(num > 0);
  DEBUG_assert(searchRange);
  DEBUG_assert(entrySelector);
  DEBUG_assert(rangeShift);

  int iA, iB;
  for (iA = 1, iB = 0; iA <= num; iA <<= 1, iB ++);

  *searchRange = iA * recordSize / 2;
  *entrySelector = iB - 1;
  *rangeShift = num * recordSize - *searchRange;
}
// }}}


static char *
otf_bsearch(char *table, // {{{
	    const char *target,
	    int len,
	    int searchRange,
	    int entrySelector,
	    int rangeShift,
	    int lower_bound) // return lower_bound, if !=0
{
  char *ret = table + rangeShift;
  if (memcmp(target, ret, len) < 0)
    ret = table;

  for (; entrySelector > 0; entrySelector --)
  {
    searchRange >>= 1;
    ret += searchRange;
    if (memcmp(target, ret, len) < 0)
      ret -= searchRange;
  }
  const int result = memcmp(target, ret, len);
  if (result == 0)
    return (ret);
  else if (lower_bound)
  {
    if (result > 0)
      return (ret + searchRange);
    return (ret);
  }
  return (NULL); // not found;
}
// }}}


static _cf_fontembed_otf_file_t *
otf_new(FILE *f) // {{{
{
  DEBUG_assert(f);

  _cf_fontembed_otf_file_t *ret;
  ret = calloc(1, sizeof(_cf_fontembed_otf_file_t));
  if (ret)
  {
    ret->f = f;
    ret->version = 0x00010000;
  }

  return (ret);
}
// }}}


// will alloc, if >buf == NULL, returns >buf, or NULL on error
// NOTE: you probably want _cfFontEmbedOTFGetTable()

static char *
otf_read(_cf_fontembed_otf_file_t *otf,
	 char *buf,
	 long pos,
	 int length) // {{{
{
  char *ours = NULL;

  if (length == 0)
    return (buf);
  else if (length < 0)
  {
    DEBUG_assert(0);
    return (NULL);
  }

  int res = fseek(otf->f, pos, SEEK_SET);
  if (res == -1)
  {
    fprintf(stderr, "Seek failed: %s\n", strerror(errno));
    return (NULL);
  }

  // (+3) & ~3 for checksum...
  const int pad_len = (length + 3) & ~3;
  if (!buf)
  {
    ours = buf = malloc(sizeof(char) * pad_len);
    if (!buf)
    {
      fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
      return (NULL);
    }
  }

  res = fread(buf, 1, pad_len, otf->f);
  if (res != pad_len)
  {
    if (res == length) // file size not multiple of 4, pad with zero
      memset(buf + res, 0, pad_len - length);
    else {
      fprintf(stderr, "Short read\n");
      free(ours);
      return (NULL);
    }
  }

  return (buf);
}
// }}}


static int
otf_get_ttc_start(_cf_fontembed_otf_file_t *otf,
		  int use_ttc) // {{{
{
  char buf[4];

  if (!otf->numTTC) // > 0 if TTC...
    return (0);

  int pos = 0;
  if ((use_ttc < 0) || (use_ttc >= otf->numTTC) ||
      (!otf_read(otf, buf, pos + 12 + 4 * use_ttc, 4)))
  {
    fprintf(stderr, "Bad TTC subfont number\n");
    return (-1);
  }
  return (__cfFontEmbedGetULong(buf));
}
// }}}


static _cf_fontembed_otf_file_t *
otf_do_load(_cf_fontembed_otf_file_t *otf,
	    int pos) // {{{
{
  int iA;
  char buf[16];

  // {{{ read offset table
  if (otf_read(otf, buf, pos, 12))
  {
    otf->version = __cfFontEmbedGetULong(buf);
    if (otf->version == 0x00010000) // 1.0 truetype
    {
    }
    else if (otf->version ==
	     _CF_FONTEMBED_OTF_TAG('O', 'T', 'T', 'O')) // OTF(CFF)
      otf->flags |= _CF_FONTEMBED_OTF_F_FMT_CFF;
    else if (otf->version ==
	     _CF_FONTEMBED_OTF_TAG('t', 'r', 'u', 'e')) // (old mac)
    {
    }
    else if (otf->version ==
	     _CF_FONTEMBED_OTF_TAG('t', 'y', 'p', '1')) // sfnt wrapped type1
    {
      // TODO: unsupported
    }
    else
    {
      _cfFontEmbedOTFClose(otf);
      otf = NULL;
    }
    pos += 12;
  }
  else
  {
    _cfFontEmbedOTFClose(otf);
    otf = NULL;
  }
  if (!otf)
  {
    fprintf(stderr, "Not a ttf font\n");
    return (NULL);
  }
  otf->numTables = __cfFontEmbedGetUShort(buf + 4);
  // }}}

  // {{{ read directory
  otf->tables = malloc(sizeof(_cf_fontembed_otf_dir_ent_t) * otf->numTables);
  if (!otf->tables)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    _cfFontEmbedOTFClose(otf);
    return (NULL);
  }
  for (iA = 0; iA < otf->numTables; iA ++)
  {
    if (!otf_read(otf, buf, pos, 16))
    {
      _cfFontEmbedOTFClose(otf);
      return (NULL);
    }
    otf->tables[iA].tag = __cfFontEmbedGetULong(buf);
    otf->tables[iA].checkSum = __cfFontEmbedGetULong(buf + 4);
    otf->tables[iA].offset = __cfFontEmbedGetULong(buf + 8);
    otf->tables[iA].length = __cfFontEmbedGetULong(buf + 12);
    if ((otf->tables[iA].tag == _CF_FONTEMBED_OTF_TAG('C', 'F', 'F', ' ')) &&
	((otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF) == 0))
    {
      fprintf(stderr, "Wrong magic\n");
      _cfFontEmbedOTFClose(otf);
      return (NULL);
    }
    else if ((otf->tables[iA].tag == _CF_FONTEMBED_OTF_TAG('g', 'l', 'y', 'p')) &&
	     (otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF))
    {
      fprintf(stderr, "Wrong magic\n");
      _cfFontEmbedOTFClose(otf);
      return (NULL);
    }
    pos += 16;
  }
  // }}}

  //otf->flags |= _CF_FONTEMBED_OTF_F_DO_CHECKSUM;
  // {{{ check head table
  int len = 0;
  char *head =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'),
			    &len);
  if ((!head) ||
      (__cfFontEmbedGetULong(head + 0) != 0x00010000) ||  // version
      (len != 54) ||
      (__cfFontEmbedGetULong(head + 12) != 0x5F0F3CF5) || // magic
      (__cfFontEmbedGetShort(head + 52) != 0x0000))   // glyphDataFormat
  {
    fprintf(stderr, "Unsupported OTF font / head table \n");
    free(head);
    _cfFontEmbedOTFClose(otf);
    return (NULL);
  }
  // }}}
  otf->unitsPerEm = __cfFontEmbedGetUShort(head + 18);
  otf->indexToLocFormat = __cfFontEmbedGetShort(head + 50);

  // {{{ checksum whole file
  if (otf->flags & _CF_FONTEMBED_OTF_F_DO_CHECKSUM)
  {
    unsigned int csum = 0;
    char tmp[1024];
    rewind(otf->f);
    while (!feof(otf->f))
    {
      len = fread(tmp, 1, 1024, otf->f);
      if (len & 3) // zero padding reqd.
        memset(tmp + len, 0, 4 - (len & 3));
      csum += __cfFontEmbedOTFCheckSum(tmp, len);
    }
    if (csum != 0xb1b0afba)
    {
      fprintf(stderr, "Wrong global checksum\n");
      free(head);
      _cfFontEmbedOTFClose(otf);
      return (NULL);
    }
  }
  // }}}
  free(head);

  // {{{ read maxp table / numGlyphs
  char *maxp =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('m', 'a', 'x', 'p'),
			    &len);
  if (maxp)
  {
    const unsigned int maxp_version = __cfFontEmbedGetULong(maxp);
    if ((maxp_version == 0x00005000) && (len == 6)) // version 0.5
    {
      otf->numGlyphs = __cfFontEmbedGetUShort(maxp + 4);
      if ((otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF) == 0) // only CFF
      {
        free(maxp);
        maxp = NULL;
      }
    }
    else if ((maxp_version == 0x00010000) && (len == 32)) // version 1.0
    {
      otf->numGlyphs = __cfFontEmbedGetUShort(maxp + 4);
      if (otf->flags&_CF_FONTEMBED_OTF_F_FMT_CFF) // only TTF
      {
        free(maxp);
        maxp = NULL;
      }
    }
    else
    {
      free(maxp);
      maxp = NULL;
    }
  }
  if (!maxp)
  {
    fprintf(stderr, "Unsupported OTF font / maxp table \n");
    free(maxp);
    _cfFontEmbedOTFClose(otf);
    return (NULL);
  }
  free(maxp);
  // }}}

  return (otf);
}
// }}}


_cf_fontembed_otf_file_t *
_cfFontEmbedOTFLoad(const char *file) // {{{
{
  FILE *f;
  _cf_fontembed_otf_file_t *otf;

  int use_ttc =- 1;
  if ((f = fopen(file, "rb")) == NULL)
  {
    // check for TTC
    char *tmp = strrchr(file, '/'), *end;
    if (tmp)
    {
      use_ttc = strtoul(tmp + 1, &end, 10);
      if (!*end)
      {
        end = malloc((tmp - file + 1) * sizeof(char));
        if (!end)
	{
          fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
          return (NULL);
        }
        strncpy(end, file, tmp - file);
        end[tmp - file] = 0;
        f = fopen(end, "rb");
        free(end);
      }
    }
    if (!f)
    {
      fprintf(stderr, "Could not open \"%s\": %s\n", file, strerror(errno));
      return (NULL);
    }
  }
  otf = otf_new(f);
  if (!otf)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    fclose(f);
    return (NULL);
  }

  char buf[12];
  int pos = 0;
  // {{{ check for TTC
  if (otf_read(otf, buf, pos, 12))
  {
    const unsigned int version = __cfFontEmbedGetULong(buf);
    if (version == _CF_FONTEMBED_OTF_TAG('t', 't', 'c', 'f'))
    {
      const unsigned int ttc_version = __cfFontEmbedGetULong(buf + 4);
      if ((ttc_version != 0x00010000) && (ttc_version != 0x00020000))
      {
        fprintf(stderr, "Unsupported TTC version\n");
        _cfFontEmbedOTFClose(otf);
        return (NULL);
      }
      otf->numTTC = __cfFontEmbedGetULong(buf + 8);
      otf->useTTC = use_ttc;
      pos = otf_get_ttc_start(otf, use_ttc);
      if (pos == -1)
      {
        _cfFontEmbedOTFClose(otf);
        return (NULL);
      }
    }
  }
  else
  {
    fprintf(stderr, "Not a ttf font\n");
    _cfFontEmbedOTFClose(otf);
    return (NULL);
  }
  // }}}

  return (otf_do_load(otf, pos));
}
// }}}


void
_cfFontEmbedOTFClose(_cf_fontembed_otf_file_t *otf) // {{{
{
  DEBUG_assert(otf);
  if (otf)
  {
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


static int
otf_dirent_compare(const void *a, const void *b) // {{{
{
  const unsigned int aa = ((const _cf_fontembed_otf_dir_ent_t *)a)->tag;
  const unsigned int bb = ((const _cf_fontembed_otf_dir_ent_t *)b)->tag;
  if (aa < bb)
    return (-1);
  else if (aa > bb)
    return (1);
  return (0);
}
// }}}


int
__cfFontEmbedOTFFindTable(_cf_fontembed_otf_file_t *otf,
			  unsigned int tag) // {{{ - table_index  or -1 on error
{
#if 0
  // binary search would require raw table
  int pos = 0;
  char buf[12];
  if (!otf_read(otf, buf, pos, 12))
    return (-1);
  pos = 12;
  const unsigned int numTables = __cfFontEmbedGetUShort(buf + 4);
  char *tables = malloc(16 * numTables);
  if (!tables)
    return (-1);
  if (!otf_read(otf, tables, pos, 16 * numTables))
  {
    free(tables);
    return (-1);
  }
  char target[] = {(tag >> 24), (tag >> 16), (tag >> 8), tag};
  //  DEBUG_assert(__cfFontEmbedGetUShort(buf + 6) +
  //               __cfFontEmbedGetUShort(buf + 10) == 16 * numTables);
  char *result = otf_bsearch(tables, target, 4,
			     __cfFontEmbedGetUShort(buf + 6),
			     __cfFontEmbedGetUShort(buf + 8),
			     __cfFontEmbedGetUShort(buf + 10), 0);
  free(tables);
  if (result)
    return (result - tables) / 16;
#elif 1
  _cf_fontembed_otf_dir_ent_t key = {.tag = tag}, *res;
  res = bsearch(&key, otf->tables, otf->numTables, sizeof(otf->tables[0]),
		otf_dirent_compare);
  if (res)
    return (res-otf->tables);
#else
  int iA;
  for (iA = 0; iA < otf->numTables; iA ++)
  {
    if (otf->tables[iA].tag == tag)
      return (iA);
  }
#endif // 1
  return (-1);
}
// }}}


char *
_cfFontEmbedOTFGetTable(_cf_fontembed_otf_file_t *otf,
			unsigned int tag,
			int *ret_len) // {{{
{
  DEBUG_assert(otf);
  DEBUG_assert(ret_len);

  const int idx = __cfFontEmbedOTFFindTable(otf, tag);
  if (idx == -1)
  {
    *ret_len =- 1;
    return (NULL);
  }
  const _cf_fontembed_otf_dir_ent_t *table = otf->tables + idx;

  char *ret = otf_read(otf, NULL, table->offset, table->length);
  if (!ret)
    return (NULL);
  if (otf->flags & _CF_FONTEMBED_OTF_F_DO_CHECKSUM)
  {
    unsigned int csum = __cfFontEmbedOTFCheckSum(ret, table->length);
    if (tag==_CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd')) // special case
      csum -= __cfFontEmbedGetULong(ret + 8);
    if (csum != table->checkSum)
    {
      fprintf(stderr, "Wrong checksum for %c%c%c%c\n",
	      _CF_FONTEMBED_OTF_UNTAG(tag));
      free(ret);
      return (NULL);
    }
  }
  *ret_len = table->length;
  return (ret);
}
// }}}


int
__cfFontEmbedOTFLoadGlyf(_cf_fontembed_otf_file_t *otf) // {{{  - 0 on success
{
  DEBUG_assert((otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF) == 0); // not for CFF
  int iA, len;
  // {{{ find glyf table
  iA =
    __cfFontEmbedOTFFindTable(otf, _CF_FONTEMBED_OTF_TAG('g', 'l', 'y', 'f'));
  if (iA == -1)
  {
    fprintf(stderr, "Unsupported OTF font / glyf table \n");
    return (-1);
  }
  otf->glyfTable = otf->tables + iA;
  // }}}

  // {{{ read loca table
  char *loca =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('l', 'o', 'c', 'a'),
			    &len);
  if ((!loca) ||
      (otf->indexToLocFormat >= 2) ||
      (((len + 3) & ~3) != ((((otf->numGlyphs + 1) *
			      (otf->indexToLocFormat + 1) * 2) +3 ) & ~3)))
  {
    fprintf(stderr, "Unsupported OTF font / loca table \n");
    return (-1);
  }
  if (otf->glyphOffsets)
  {
    free(otf->glyphOffsets);
    DEBUG_assert(0);
  }
  otf->glyphOffsets = malloc((otf->numGlyphs + 1) * sizeof(unsigned int));
  if (!otf->glyphOffsets)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    return (-1);
  }
  if (otf->indexToLocFormat == 0)
  {
    for (iA = 0; iA <= otf->numGlyphs; iA ++)
      otf->glyphOffsets[iA] = __cfFontEmbedGetUShort(loca + iA * 2) * 2;
  }
  else // indexToLocFormat == 1
  {
    for (iA = 0; iA <= otf->numGlyphs; iA ++)
      otf->glyphOffsets[iA] = __cfFontEmbedGetULong(loca + iA * 4);
  }
  free(loca);
  if (otf->glyphOffsets[otf->numGlyphs] > otf->glyfTable->length)
  {
    fprintf(stderr, "Bad loca table \n");
    return (-1);
  }
  // }}}

  // {{{ allocate otf->gly slot
  int maxGlyfLen = 0;  // no single glyf takes more space
  for (iA = 1; iA <= otf->numGlyphs; iA ++)
  {
    const int glyfLen = otf->glyphOffsets[iA] - otf->glyphOffsets[iA - 1];
    if (glyfLen < 0)
    {
      fprintf(stderr, "Bad loca table: glyph len %d\n", glyfLen);
      return (-1);
    }
    if (maxGlyfLen < glyfLen)
      maxGlyfLen = glyfLen;
  }
  if (otf->gly)
  {
    free(otf->gly);
    DEBUG_assert(0);
  }
  otf->gly=malloc(maxGlyfLen * sizeof(char));
  if (!otf->gly)
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    return (-1);
  }
  // }}}

  return (0);
}
// }}}

int
__cfFontEmbedOTFLoadMore(_cf_fontembed_otf_file_t *otf)
                           // {{{  - 0 on success => hhea, hmtx, name, [glyf]
{
  int iA;
  int len;

  if ((otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF) == 0) // not for CFF
  {
    if (__cfFontEmbedOTFLoadGlyf(otf) == -1)
      return (-1);
  }

  // {{{ read hhea table
  char *hhea =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'h', 'e', 'a'),
			    &len);
  if ((!hhea) ||
      (__cfFontEmbedGetULong(hhea) != 0x00010000) || // version
      (len != 36) ||
      (__cfFontEmbedGetShort(hhea + 32) != 0)) // metric format
  {
    fprintf(stderr, "Unsupported OTF font / hhea table \n");
    return (-1);
  }
  otf->numberOfHMetrics = __cfFontEmbedGetUShort(hhea + 34);
  free(hhea);
  // }}}

  // {{{ read hmtx table
  char *hmtx =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'm', 't', 'x'),
			    &len);
  if ((!hmtx) ||
      (len != otf->numberOfHMetrics * 2 + otf->numGlyphs * 2))
  {
    fprintf(stderr, "Unsupported OTF font / hmtx table\n");
    return (-1);
  }
  if (otf->hmtx)
  {
    free(otf->hmtx);
    DEBUG_assert(0);
  }
  otf->hmtx = hmtx;
  // }}}

  // {{{ read name table
  char *name =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('n', 'a', 'm', 'e'),
			    &len);
  if ((!name) ||
      (__cfFontEmbedGetUShort(name) != 0x0000) || // version
      (len < __cfFontEmbedGetUShort(name + 2) * 12 + 6) ||
      (len <= __cfFontEmbedGetUShort(name + 4)))
  {
    fprintf(stderr, "Unsupported OTF font / name table\n");
    return (-1);
  }

  // check bounds
  int name_count = __cfFontEmbedGetUShort(name + 2);
  const char *nstore = name + __cfFontEmbedGetUShort(name + 4);
  for (iA = 0; iA < name_count; iA ++)
  {
    const char *nrec = name + 6 + 12 * iA;
    if (nstore-name + __cfFontEmbedGetUShort(nrec + 10) +
	__cfFontEmbedGetUShort(nrec + 8) > len)
    {
      fprintf(stderr, "Bad name table\n");
      free(name);
      return (-1);
    }
  }
  if (otf->name)
  {
    free(otf->name);
    DEBUG_assert(0);
  }
  otf->name = name;
  // }}}

  return (0);
}
// }}}


static int
otf_load_cmap(_cf_fontembed_otf_file_t *otf) // {{{  - 0 on success
{
  int iA;
  int len;

  char *cmap =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('c', 'm', 'a', 'p'),
			    &len);
  if ((!cmap) ||
      (__cfFontEmbedGetUShort(cmap) != 0x0000) || // version
      (len < __cfFontEmbedGetUShort(cmap + 2) * 8 + 4))
  {
    fprintf(stderr, "Unsupported OTF font / cmap table\n");
    DEBUG_assert(0);
    return (-1);
  }
  // check bounds, find (3, 0) or (3, 1) [TODO?]
  const int numTables = __cfFontEmbedGetUShort(cmap + 2);
  for (iA = 0; iA < numTables; iA ++)
  {
    const char *nrec = cmap + 4 + 8 * iA;
    const unsigned int offset = __cfFontEmbedGetULong(nrec + 4);
    const char *ndata = cmap + offset;
    if ((ndata < cmap + 4 + 8 * numTables) ||
	(offset >= len) ||
	(offset + __cfFontEmbedGetUShort(ndata + 2) > len))
    {
      fprintf(stderr, "Bad cmap table\n");
      free(cmap);
      DEBUG_assert(0);
      return (-1);
    }
    if ((__cfFontEmbedGetUShort(nrec) == 3) &&
	(__cfFontEmbedGetUShort(nrec + 2) <= 1) &&
	(__cfFontEmbedGetUShort(ndata) == 4) &&
	(__cfFontEmbedGetUShort(ndata + 4) == 0))
      otf->unimap = ndata;
  }
  if (otf->cmap)
  {
    free(otf->cmap);
    DEBUG_assert(0);
  }
  otf->cmap = cmap;

  return (0);
}
// }}}


int
_cfFontEmbedOTFGetWidth(_cf_fontembed_otf_file_t *otf,
			unsigned short gid) // {{{  -1 on error
{
  DEBUG_assert(otf);

  if (gid >= otf->numGlyphs)
    return (-1);

  // ensure hmtx is there
  if (!otf->hmtx)
  {
    if (__cfFontEmbedOTFLoadMore(otf) != 0)
    {
      fprintf(stderr, "Unsupported OTF font / cmap table\n");
      return (-1);
    }
  }

  return (__cfFontEmbedGetWidthFast(otf, gid));
#if 0
  if (gid >= otf->numberOfHMetrics)
  {
    return (__cfFontEmbedGetUShort(otf->hmtx +
				   (otf->numberOfHMetrics - 1) * 2));
    // TODO? lsb = __cfFontEmbedGetShort(otf->hmtx +
    //                                   otf->numberOfHMetrics * 2 + gid * 2);
    //                lsb: left_side_bearing (also in table)
  }
  return (__cfFontEmbedGetUShort(otf->hmtx + gid * 4));
  // TODO? lsb = __cfFontEmbedGetShort(otf->hmtx + gid * 4 + 2);
#endif
}
// }}}


static int
otf_name_compare(const void *a,
		 const void *b) // {{{
{
  return (memcmp(a, b, 8));
}
// }}}


const char *
_cfFontEmbedOTFGetName(_cf_fontembed_otf_file_t *otf,
		       int platformID,
		       int encodingID,
		       int languageID,
		       int nameID,
		       int *ret_len) // {{{
{
  DEBUG_assert(otf);
  DEBUG_assert(ret_len);

  // ensure name is there
  if (!otf->name)
  {
    if (__cfFontEmbedOTFLoadMore(otf) != 0)
    {
      *ret_len = -1;
      DEBUG_assert(0);
      return (NULL);
    }
  }

  char key[8];
  __cfFontEmbedSetUShort(key, platformID);
  __cfFontEmbedSetUShort(key + 2, encodingID);
  __cfFontEmbedSetUShort(key + 4, languageID);
  __cfFontEmbedSetUShort(key + 6, nameID);

  char *res = bsearch(key, otf->name + 6,
		      __cfFontEmbedGetUShort(otf->name + 2), 12,
		      otf_name_compare);
  if (res)
  {
    *ret_len = __cfFontEmbedGetUShort(res + 8);
    int npos = __cfFontEmbedGetUShort(res + 10);
    const char *nstore = otf->name + __cfFontEmbedGetUShort(otf->name + 4);
    return (nstore + npos);
  }
  *ret_len = 0;
  return (NULL);
}
// }}}


int
_cfFontEmbedOTFGetGlyph(_cf_fontembed_otf_file_t *otf,
			unsigned short gid) // {{{ result in >otf->gly,
                                            //     returns length, -1 on error
{
  DEBUG_assert(otf);
  DEBUG_assert((otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF) == 0); // not for CFF

  if (gid >= otf->numGlyphs)
    return (-1);

  // ensure >glyphOffsets and >gly is there
  if ((!otf->gly) || (!otf->glyphOffsets))
  {
    if (__cfFontEmbedOTFLoadMore(otf) != 0)
    {
      DEBUG_assert(0);
      return (-1);
    }
  }

  const int len = otf->glyphOffsets[gid + 1] - otf->glyphOffsets[gid];
  if (len == 0)
    return (0);

  DEBUG_assert(otf->glyfTable->length >= otf->glyphOffsets[gid + 1]);
  if (!otf_read(otf, otf->gly,
                otf->glyfTable->offset + otf->glyphOffsets[gid], len))
    return (-1);

  return (len);
}
// }}}


unsigned short
_cfFontEmbedOTFFromUnicode(_cf_fontembed_otf_file_t *otf,
			   int unicode) // {{{ 0 = missing
{
  DEBUG_assert(otf);
  DEBUG_assert((unicode >= 0) && (unicode < 65536));
  //DEBUG_assert((otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF) == 0);
                                           // not for CFF, other method!

  // ensure >cmap and >unimap is there
  if (!otf->cmap)
  {
    if (otf_load_cmap(otf) != 0)
    {
      DEBUG_assert(0);
      return (0); // TODO?
    }
  }
  if (!otf->unimap)
  {
    fprintf(stderr, "Unicode (3, 1) cmap in format 4 not found\n");
    return (0);
  }

#if 0
  // linear search is cache friendly and should be quite fast
#else
  const unsigned short segCountX2 = __cfFontEmbedGetUShort(otf->unimap + 6);
  char target[] = {unicode >> 8, unicode};
                                   // __cfFontEmbedSetUShort(target, unicode);
  char *result = otf_bsearch((char *)otf->unimap + 14, target, 2,
			     __cfFontEmbedGetUShort(otf->unimap + 8),
			     __cfFontEmbedGetUShort(otf->unimap + 10),
			     __cfFontEmbedGetUShort(otf->unimap + 12), 1);
  if (result >= otf->unimap + 14 + segCountX2) // outside of endCode[segCount]
  {
    DEBUG_assert(0); // bad font, no 0xffff sentinel
    return (0);
  }

  result += 2 + segCountX2; // jump over padding into startCode[segCount]
  const unsigned short startCode = __cfFontEmbedGetUShort(result);
  if (startCode > unicode)
    return (0);
  result += 2 * segCountX2;
  const unsigned short rangeOffset = __cfFontEmbedGetUShort(result);
  if (rangeOffset)
  {
    return (__cfFontEmbedGetUShort(result + rangeOffset +
				   2 * (unicode-startCode)));
                 // the so called "obscure indexing trick" into glyphIdArray[]
    // NOTE: this is according to apple spec; microsoft says we must add
    // delta (probably incorrect; fonts probably have delta == 0)
  }
  else
  {
    const short delta = __cfFontEmbedGetShort(result - segCountX2);
    return (delta + unicode) & 0xffff;
  }
#endif // 0
}
// }}}


/** output stuff **/
int
__cfFontEmbedOTFActionCopy(void *param,
			   int table_no,
			   _cf_fontembed_output_fn_t output,
			   void *context) // {{{
{
  _cf_fontembed_otf_file_t *otf = param;
  const _cf_fontembed_otf_dir_ent_t *table = otf->tables + table_no;

  if (!output) // get checksum and unpadded length
  {
    *(unsigned int *)context = table->checkSum;
    return (table->length);
  }

  // TODO? copy_block(otf->f, table->offset, (table->length + 3) & ~3, output,
  //                  context);
  // problem: PS currently depends on single-output. Also checksum not possible
  char *data = otf_read(otf, NULL, table->offset, table->length);
  if (!data)
    return (-1);
  int ret = (table->length + 3) & ~3;
  (*output)(data, ret, context);
  free(data);
  return (ret); // padded length
}
// }}}


// TODO? >modified time-stamp?
// Note: don't use this directly. __cfFontEmbedOTFWriteSFNT will internally
//       replace
// __cfFontEmbedOTFActionCopy for head with this

int
__cfFontEmbedOTFActionCopyHead(void *param,
			       int csum,
			       _cf_fontembed_output_fn_t output,
			       void *context) // {{{
{
  _cf_fontembed_otf_file_t *otf = param;
  const int table_no =
    __cfFontEmbedOTFFindTable(otf, _CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'));
                          // we can't have csum AND table_no ... never mind!
  DEBUG_assert(table_no != -1);
  const _cf_fontembed_otf_dir_ent_t *table = otf->tables + table_no;

  if (!output) // get checksum and unpadded length
  {
    *(unsigned int *)context = table->checkSum;
    return (table->length);
  }

  char *data = otf_read(otf, NULL, table->offset, table->length);
  if (!data)
    return (-1);
  __cfFontEmbedSetULong(data + 8, 0xb1b0afba - csum);
                                                // head. fix global checksum
  int ret = (table->length + 3) & ~3;
  (*output)(data, ret, context);
  free(data);
  return (ret); // padded length
}
// }}}


int
__cfFontEmbedOTFActionReplace(void *param,
			      int length,
			      _cf_fontembed_output_fn_t output,
			      void *context) // {{{
{
  char *data = param;
  char pad[4] = {0, 0, 0, 0};

  int ret = (length + 3) & ~3;
  if (!output) // get checksum and unpadded length
  {
    if (ret != length)
    {
      unsigned int csum = __cfFontEmbedOTFCheckSum(data, ret - 4);
      memcpy(pad, data + ret - 4, ret - length);
      csum += __cfFontEmbedGetULong(pad);
      *(unsigned int *)context = csum;
    }
    else
      *(unsigned int *)context = __cfFontEmbedOTFCheckSum(data, length);
    return (length);
  }

  (*output)(data, length, context);
  if (ret != length)
    (*output)(pad, ret - length, context);

  return (ret); // padded length
}
// }}}

//
// windows "works best" with the following ordering:
//   head, hhea, maxp, OS/2, hmtx, LTSH, VDMX, hdmx, cmap, fpgm, prep, cvt,
//   loca, glyf, kern, name, post, gasp, PCLT, DSIG
// or for CFF:
//   head, hhea, maxp, OS/2, name, cmap, post, CFF, (other tables, as
//   convenient)
//

#define NUM_PRIO 20
static const struct
{
  int prio;
  unsigned int tag;
} otf_tagorder_win[] =
{ // {{{
  {19, _CF_FONTEMBED_OTF_TAG('D', 'S', 'I', 'G')},
  { 5, _CF_FONTEMBED_OTF_TAG('L', 'T', 'S', 'H')},
  { 3, _CF_FONTEMBED_OTF_TAG('O', 'S', '/', '2')},
  {18, _CF_FONTEMBED_OTF_TAG('P', 'C', 'L', 'T')},
  { 6, _CF_FONTEMBED_OTF_TAG('V', 'D', 'M', 'X')},
  { 8, _CF_FONTEMBED_OTF_TAG('c', 'm', 'a', 'p')},
  {11, _CF_FONTEMBED_OTF_TAG('c', 'v', 't', ' ')},
  { 9, _CF_FONTEMBED_OTF_TAG('f', 'p', 'g', 'm')},
  {17, _CF_FONTEMBED_OTF_TAG('g', 'a', 's', 'p')},
  {13, _CF_FONTEMBED_OTF_TAG('g', 'l', 'y', 'f')},
  { 7, _CF_FONTEMBED_OTF_TAG('h', 'd', 'm', 'x')},
  { 0, _CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd')},
  { 1, _CF_FONTEMBED_OTF_TAG('h', 'h', 'e', 'a')},
  { 4, _CF_FONTEMBED_OTF_TAG('h', 'm', 't', 'x')},
  {14, _CF_FONTEMBED_OTF_TAG('k', 'e', 'r', 'n')},
  {12, _CF_FONTEMBED_OTF_TAG('l', 'o', 'c', 'a')},
  { 2, _CF_FONTEMBED_OTF_TAG('m', 'a', 'x', 'p')},
  {15, _CF_FONTEMBED_OTF_TAG('n', 'a', 'm', 'e')},
  {16, _CF_FONTEMBED_OTF_TAG('p', 'o', 's', 't')},
  {10, _CF_FONTEMBED_OTF_TAG('p', 'r', 'e', 'p')}
};
// }}}


int
__cfFontEmbedOTFWriteSFNT(struct __cf_fontembed_otf_write_s *otw,
			  unsigned int version,
			  int numTables,
			  _cf_fontembed_output_fn_t output,
			  void *context) // {{{
{
  int iA;
  int ret;

  int *order = malloc(sizeof(int) * numTables); // temporary
  char *start = malloc(12 + 16 * numTables);
  if ((!order) || (!start))
  {
    fprintf(stderr, "Bad alloc: %s\n", strerror(errno));
    free(order);
    free(start);
    return (-1);
  }

  if (1) // sort tables
  {
    int priolist[NUM_PRIO] = {0, };

    // reverse intersection of both sorted arrays
    int iA = numTables - 1,
        iB = sizeof(otf_tagorder_win) / sizeof(otf_tagorder_win[0]) - 1;
    int ret = numTables - 1;
    while ((iA >= 0) && (iB >= 0))
    {
      if (otw[iA].tag == otf_tagorder_win[iB].tag)
        priolist[otf_tagorder_win[iB--].prio] = 1 + iA--;
      else if (otw[iA].tag > otf_tagorder_win[iB].tag)
	// no order known: put unchanged at end of result
        order[ret--] = iA--;
      else // <
        iB --;
    }
    for (iA = NUM_PRIO - 1; iA >= 0; iA --)
    {
      // pick the matched tables up in sorted order (bucketsort principle)
      if (priolist[iA])
        order[ret--] = priolist[iA] - 1;
    }
  }
  else
  {
    for (iA = 0; iA < numTables; iA ++)
      order[iA] = iA;
  }

  // the header
  __cfFontEmbedSetULong(start, version);
  __cfFontEmbedSetUShort(start + 4, numTables);
  int a, b, c;
  otf_bsearch_params(numTables, 16, &a, &b, &c);
  __cfFontEmbedSetUShort(start + 6, a);
  __cfFontEmbedSetUShort(start + 8, b);
  __cfFontEmbedSetUShort(start + 10, c);

  // first pass: calculate table directory / offsets and checksums
  unsigned int globalSum = 0, csum;
  int offset = 12 + 16 * numTables;
  int headAt = -1;
  for (iA = 0; iA < numTables; iA ++)
  {
    char *entry = start + 12 + 16 * order[iA];
    const int res = (*otw[order[iA]].action)(otw[order[iA]].param,
					     otw[order[iA]].length, NULL,
					     &csum);
    DEBUG_assert(res >= 0);
    if (otw[order[iA]].tag == _CF_FONTEMBED_OTF_TAG('h', 'e', 'a', 'd'))
      headAt = order[iA];
    __cfFontEmbedSetULong(entry, otw[order[iA]].tag);
    __cfFontEmbedSetULong(entry + 4, csum);
    __cfFontEmbedSetULong(entry + 8, offset);
    __cfFontEmbedSetULong(entry + 12, res);
    offset += (res + 3) & ~3; // padding
    globalSum += csum;
  }

  // second pass: write actual data
  // write header + directory
  ret= 12 + 16 * numTables;
  (*output)(start, ret, context);
  globalSum += __cfFontEmbedOTFCheckSum(start, ret);

  // change head
  if ((headAt != -1) && (otw[headAt].action ==
			 __cfFontEmbedOTFActionCopy)) // more needed?
  {
    otw[headAt].action = __cfFontEmbedOTFActionCopyHead;
    otw[headAt].length = globalSum;
  }

  // write tables
  for (iA = 0; iA < numTables; iA ++)
  {
    const int res = (*otw[order[iA]].action)(otw[order[iA]].param,
					     otw[order[iA]].length,
					     output, context);
    if (res < 0)
    {
      free(order);
      free(start);
      return (-1);
    }
    DEBUG_assert(((res + 3) & ~3) == res);
             // correctly padded? (i.e. next line is just ret += res;)
    ret += (res + 3) & ~3;
  }
  DEBUG_assert(offset == ret);
  free(order);
  free(start);

  return (ret);
}
// }}}

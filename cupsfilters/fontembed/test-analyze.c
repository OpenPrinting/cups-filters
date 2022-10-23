//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/fontembed-private.h>
#include <cupsfilters/debug-internal.h>
#include "embed-sfnt-private.h"
#include "sfnt-private.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>


enum {
  WEIGHT_THIN = 100,
  WEIGHT_EXTRALIGHT = 200,
  WEIGHT_ULTRALIGHT = 200,
  WEIGHT_LIGHT = 300,
  WEIGHT_NORMAL = 400,
  WEIGHT_REGULAR = 400,
  WEIGHT_MEDIUM = 500,
  WEIGHT_SEMIBOLD = 600, // DEMI
  WEIGHT_BOLD = 700,
  WEIGHT_EXTRABOLD = 800,
  WEIGHT_ULTRABOLD = 800,
  WEIGHT_BLACK = 900,
  WEIGHT_HEAVY=900
};


void
show_post(_cf_fontembed_otf_file_t *otf) // {{{
{
  DEBUG_assert(otf);
  int len = 0;
  char *buf;

  buf =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('p', 'o', 's', 't'),
			    &len);
  if (!buf)
  {
    DEBUG_assert(len == -1);
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
         "  vmType1: %d %d\n",
	 len,
         __cfFontEmbedGetULong(buf),
         __cfFontEmbedGetLong(buf + 4) >> 16,
	 __cfFontEmbedGetULong(buf + 4) & 0xffff,
         __cfFontEmbedGetShort(buf + 8),
         __cfFontEmbedGetShort(buf + 10),
         __cfFontEmbedGetULong(buf + 12),
         __cfFontEmbedGetULong(buf + 16),
	 __cfFontEmbedGetULong(buf + 20),
         __cfFontEmbedGetULong(buf + 24),
	 __cfFontEmbedGetULong(buf + 38));
  free(buf);
}
// }}}


void
show_name(_cf_fontembed_otf_file_t *otf) // {{{
{
  DEBUG_assert(otf);
  int iA, len = 0;
  char *buf;

  buf =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('n', 'a', 'm', 'e'),
			    &len);
  if (!buf)
  {
    DEBUG_assert(len == -1);
    printf("No name table\n");
    return;
  }
  printf("NAME:\n");
  int name_count = __cfFontEmbedGetUShort(buf + 2);
  const char *nstore = buf + __cfFontEmbedGetUShort(buf + 4);
  for (iA = 0; iA < name_count; iA ++)
  {
    const char *nrec = buf + 6 + 12 * iA;
    printf("  { platformID/encodingID/languageID/nameID: %d/%d/%d/%d\n"
           "    length: %d, offset: %d, data                       :",
           __cfFontEmbedGetUShort(nrec),
           __cfFontEmbedGetUShort(nrec + 2),
           __cfFontEmbedGetUShort(nrec + 4),
           __cfFontEmbedGetUShort(nrec + 6),
           __cfFontEmbedGetUShort(nrec + 8),
           __cfFontEmbedGetUShort(nrec + 10));
    if ((__cfFontEmbedGetUShort(nrec) == 0) ||
	(__cfFontEmbedGetUShort(nrec) == 3)) // WCHAR
    {
      int nlen = __cfFontEmbedGetUShort(nrec + 8);
      int npos = __cfFontEmbedGetUShort(nrec + 10);
      for (; nlen > 0; nlen -= 2, npos += 2)
      {
        if (nstore[npos] != 0x00)
          printf("?");
        else
          printf("%c", nstore[npos + 1]);
      }
      printf(" }\n");
    }
    else
      printf("%.*s }\n",
             __cfFontEmbedGetUShort(nrec + 8),
	     nstore + __cfFontEmbedGetUShort(nrec + 10));
  }
  free(buf);
}
// }}}


void
show_cmap(_cf_fontembed_otf_file_t *otf) // {{{
{
  DEBUG_assert(otf);
  int iA, len = 0;

  char *cmap =
    _cfFontEmbedOTFGetTable(otf, _CF_FONTEMBED_OTF_TAG('c', 'm', 'a', 'p'),
			    &len);
  if (!cmap)
  {
    DEBUG_assert(len == -1);
    printf("No cmap table\n");
    return;
  }
  printf("cmap:\n");
  DEBUG_assert(__cfFontEmbedGetUShort(cmap) == 0x0000); // version
  const int numTables = __cfFontEmbedGetUShort(cmap + 2);
  printf("  numTables: %d\n", numTables);
  for (iA = 0; iA < numTables; iA ++)
  {
    const char *nrec = cmap + 4 + 8 * iA;
    const char *ndata = cmap + __cfFontEmbedGetULong(nrec + 4);
    DEBUG_assert(ndata >= cmap + 4 + 8 * numTables);
    printf("  platformID/encodingID: %d/%d\n"
           "  offset: %d  data (format: %d, length: %d, language: %d);\n",
           __cfFontEmbedGetUShort(nrec), __cfFontEmbedGetUShort(nrec + 2),
           __cfFontEmbedGetULong(nrec + 4),
           __cfFontEmbedGetUShort(ndata), __cfFontEmbedGetUShort(ndata + 2),
	   __cfFontEmbedGetUShort(ndata + 4));
  }
  free(cmap);
}
// }}}


void
show_glyf(_cf_fontembed_otf_file_t *otf,
	  int full) // {{{
{
  DEBUG_assert(otf);

  // ensure >glyphOffsets and >gly is there
  if ((!otf->gly) || (!otf->glyphOffsets))
  {
    if (__cfFontEmbedOTFLoadGlyf(otf) != 0)
    {
      DEBUG_assert(0);
      return;
    }
  }

  int iA;
  int compGlyf = 0, zeroGlyf = 0;

  // {{{ glyf
  DEBUG_assert(otf->gly);
  for (iA = 0; iA < otf->numGlyphs; iA ++)
  {
    int len = _cfFontEmbedOTFGetGlyph(otf, iA);
    if (len == 0)
      zeroGlyf ++;
    else if (__cfFontEmbedGetShort(otf->gly) == -1)
      compGlyf ++;
    if (full)
      printf("%d(%d) ", __cfFontEmbedGetShort(otf->gly), len);
  }
  if (full)
    printf("\n");
  printf("numGlyf(nonnull): %d(%d), composites: %d\n", otf->numGlyphs,
	 otf->numGlyphs - zeroGlyf, compGlyf);
  // }}}
}
// }}}


void
show_hmtx(_cf_fontembed_otf_file_t *otf) // {{{
{
  DEBUG_assert(otf);
  int iA;

  _cfFontEmbedOTFGetWidth(otf, 0); // load table.
  if (!otf->hmtx)
  {
    printf("NOTE: no hmtx table!\n");
    return;
  }
  printf("hmtx (%d):\n", otf->numberOfHMetrics);
  for (iA = 0; iA < otf->numberOfHMetrics; iA ++)
  {
    printf("(%d,%d) ",
           __cfFontEmbedGetUShort(otf->hmtx + iA * 4),
           __cfFontEmbedGetShort(otf->hmtx + iA * 4 + 2));
  }
  printf(" (last is repeated for the remaining %d glyphs)\n",
	 otf->numGlyphs - otf->numberOfHMetrics);
}
// }}}

int
main(int argc,
     char **argv)
{
  const char *fn = TESTFONT;
  _cf_fontembed_otf_file_t *otf = NULL;
  if (argc == 2)
    fn = argv[1];
  otf = _cfFontEmbedOTFLoad(fn);
  if (!otf)
  {
    printf("Font %s was not loaded, exiting.\n", TESTFONT);
    return (1);
  }

  DEBUG_assert(otf);
  if (otf->numTTC)
    printf("TTC has %d fonts, using %d\n", otf->numTTC, otf->useTTC);
  if (otf->version == 0x00010000)
    printf("Got TTF 1.0\n");
  else if (otf->version == _CF_FONTEMBED_OTF_TAG('O','T','T','O'))
    printf("Got OTF(CFF)\n");
  else if (otf->version == _CF_FONTEMBED_OTF_TAG('t','r','u','e'))
    printf("Got TTF (true)\n");
  else if (otf->version == _CF_FONTEMBED_OTF_TAG('t','y','p','1'))
    printf("Got SFNT(Type1)\n");

  printf("Has %d tables\n", otf->numTables);

  int iA;
  for (iA=0; iA < otf->numTables; iA ++)
  {
    printf("%c%c%c%c %d @%d\n", _CF_FONTEMBED_OTF_UNTAG(otf->tables[iA].tag),
	   otf->tables[iA].length, otf->tables[iA].offset);
  }
  printf("unitsPerEm: %d, indexToLocFormat: %d\n",
         otf->unitsPerEm, otf->indexToLocFormat);
  printf("num glyphs: %d\n", otf->numGlyphs);
  _cfFontEmbedOTFGetWidth(otf, 0); // load table.
  printf("numberOfHMetrics: %d\n", otf->numberOfHMetrics);

  printf("Embedding rights: %x\n", __cfFontEmbedEmbOTFGetRights(otf));

  show_post(otf);

  show_name(otf);

  show_cmap(otf);
  // printf("%d %d\n", _cfFontEmbedOTFFromUnicode(otf, 'A'), 0);

  if (!(otf->flags & _CF_FONTEMBED_OTF_F_FMT_CFF))
    show_glyf(otf, 1);

  show_hmtx(otf);

  _cfFontEmbedOTFClose(otf);

  return (0);
}

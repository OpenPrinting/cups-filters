//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/fontembed-private.h>
#include <cupsfilters/debug-internal.h>
#include <string.h>


//_cf_fontembed_fontfile_t *_cfFontEmbedFontFileOpen(const char *filename);

#if 0
_cf_fontembed_fontfile_t *
_cfFontEmbedFontFileOpen(const char *filename)
{
  // TODO? check magic
  if (...)
  {
  }
}
#endif // 0


_cf_fontembed_fontfile_t *
_cfFontEmbedFontFileOpenSFNT(_cf_fontembed_otf_file_t *otf) // {{{
{
  if (!otf)
  {
    DEBUG_assert(0);
    return (NULL);
  }
  _cf_fontembed_fontfile_t *ret = calloc(1, sizeof(_cf_fontembed_fontfile_t));

  ret->sfnt = otf;

  return (ret);
}
// }}}


_cf_fontembed_fontfile_t *
_cfFontEmbedFontFileOpenStd(const char *name) // {{{
{
  DEBUG_assert(name);
  _cf_fontembed_fontfile_t *ret = calloc(1, sizeof(_cf_fontembed_fontfile_t));

  ret->stdname = strdup(name);

  return (ret);
}
// }}}


void _cfFontEmbedFontFileClose(_cf_fontembed_fontfile_t *ff) // {{{
{
  if (ff)
  {
    _cfFontEmbedOTFClose(ff->sfnt);
    // ??? cff_close(ff->cff);
    free(ff->stdname);
    free(ff);
  }
}
// }}}

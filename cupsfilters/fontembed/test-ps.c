//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cupsfilters/fontembed-private.h>
#include <cupsfilters/debug-internal.h>
#include "config.h"
#include <stdio.h>
#include <stdlib.h>

const char *__cfFontEmbedEmbOTFGetFontName(_cf_fontembed_otf_file_t *otf);
                                                                    // TODO


static void
example_outfn(const char *buf,
	      int len,
	      void *context) // {{{
{
  FILE *f = (FILE *)context;
  if (fwrite(buf, 1, len, f) != len)
  {
    fprintf(stderr, "Short write: %m\n");
    DEBUG_assert(0);
    return;
  }
}
// }}}


static inline void
write_string(FILE *f,
	     _cf_fontembed_emb_params_t *emb,
	     const char *str) // {{{
{
  DEBUG_assert(f);
  DEBUG_assert(emb);
  int iA;

  if (emb->plan & _CF_FONTEMBED_EMB_A_MULTIBYTE)
  {
    putc('<', f);
    for (iA=0; str[iA] ;iA ++)
    {
      const unsigned short gid =
	_cfFontEmbedEmbGet(emb, (unsigned char)str[iA]);
      fprintf(f, "%04x", gid);
    }
    putc('>', f);
  }
  else
  {
    putc('(', f);
    for (iA = 0; str[iA]; iA ++)
      _cfFontEmbedEmbGet(emb, (unsigned char)str[iA]);
    fprintf(f, "%s", str); // TODO
    putc(')', f);
  }
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
  _cf_fontembed_fontfile_t *ff = _cfFontEmbedFontFileOpenSFNT(otf);
  _cf_fontembed_emb_params_t *emb =
    _cfFontEmbedEmbNew(ff,
		       _CF_FONTEMBED_EMB_DEST_PS,
  //                   _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE| // not yet...
		       _CF_FONTEMBED_EMB_C_TAKE_FONTFILE);

  FILE *f = fopen("test.ps", "w");
  DEBUG_assert(f);

  fprintf(f, "%%!PS-Adobe-2.0\n");

  char *str = "Hallo";

  _cfFontEmbedEmbGet(emb, 'a');

  int iA;
  for (iA = 0; str[iA]; iA ++)
    _cfFontEmbedEmbGet(emb, (unsigned char)str[iA]);

  _cfFontEmbedEmbEmbed(emb, example_outfn, f);

  // content
  fprintf(f, "  100 100 moveto\n" // content
             "  /%s findfont 10 scalefont setfont\n",
	  __cfFontEmbedEmbOTFGetFontName(emb->font->sfnt));
  write_string(f, emb, "Hallo");
  // Note that write_string sets subset bits, but it's too late
  fprintf(f, " show\n"
             "showpage\n");

  fprintf(f, "%%%%EOF\n");
  fclose(f);

  _cfFontEmbedEmbClose(emb);

  return (0);
}

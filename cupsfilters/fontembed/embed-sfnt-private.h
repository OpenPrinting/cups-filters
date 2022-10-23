//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _FONTEMBED_EMBED_SFNT_INT_H_
#define _FONTEMBED_EMBED_SFNT_INT_H_

#include <cupsfilters/fontembed-private.h>


_cf_fontembed_emb_right_t
  __cfFontEmbedEmbOTFGetRights(_cf_fontembed_otf_file_t *otf);

// NOTE: statically allocated buffer
const char *__cfFontEmbedEmbOTFGetFontName(_cf_fontembed_otf_file_t *otf);

void
  __cfFontEmbedEmbOTFGetPDFFontDescr(_cf_fontembed_otf_file_t *otf,
				     _cf_fontembed_emb_pdf_font_descr_t *ret);
_cf_fontembed_emb_pdf_font_widths_t
  *__cfFontEmbedEmbOTFGetPDFWidths(_cf_fontembed_otf_file_t *otf,
				   const unsigned short *encoding,
				   int len,
				   const _cf_fontembed_bit_set_t glyphs);
_cf_fontembed_emb_pdf_font_widths_t
  *__cfFontEmbedEmbOTFGetPDFCIDWidths(_cf_fontembed_otf_file_t *otf,
				      const _cf_fontembed_bit_set_t glyph);

int __cfFontEmbedEmbOTFPS(_cf_fontembed_otf_file_t *otf,
			  unsigned short *encoding, int len,
			  unsigned short *to_unicode,
			  _cf_fontembed_output_fn_t output, void *context);

#endif // !_FONTEMBED_EMBED_SFNT_INT_H_

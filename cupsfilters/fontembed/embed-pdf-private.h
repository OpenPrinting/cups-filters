//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _FONTEMBED_EMBED_PDF_INT_H_
#define _FONTEMBED_EMBED_PDF_INT_H_

_cf_fontembed_emb_pdf_font_widths_t *__cfFontEmbedEmbPDFFWNew(int datasize);

// if default_width == -1: default_width will be estimated
// glyphs == NULL -> output all
_cf_fontembed_emb_pdf_font_widths_t
  *__cfFontEmbedEmbPDFFWCIDWidths(const _cf_fontembed_bit_set_t glyphs,
				  int len, int default_width,
				  int (*getGlyphWidth)(void *context, int gid),
				  void *context);

#endif // !_FONTEMBED_EMBED_PDF_INT_H_

#ifndef _FONTEMBED_EMBED_SFNT_INT_H_
#define _FONTEMBED_EMBED_SFNT_INT_H_

#include "sfnt.h"
#include "embed-pdf.h"


EMB_RIGHT_TYPE emb_otf_get_rights(OTF_FILE *otf);

// NOTE: statically allocated buffer
const char *emb_otf_get_fontname(OTF_FILE *otf);

void emb_otf_get_pdf_fontdescr(OTF_FILE *otf, EMB_PDF_FONTDESCR *ret);
EMB_PDF_FONTWIDTHS *emb_otf_get_pdf_widths(OTF_FILE *otf,
					   const unsigned short *encoding,
					   int len, const BITSET glyphs);
EMB_PDF_FONTWIDTHS *emb_otf_get_pdf_cidwidths(OTF_FILE *otf,
					      const BITSET glyph);

int emb_otf_ps(OTF_FILE *otf, unsigned short *encoding, int len,
	       unsigned short *to_unicode, OUTPUT_FN output, void *context);

#endif // !_FONTEMBED_EMBED_SFNT_INT_H_

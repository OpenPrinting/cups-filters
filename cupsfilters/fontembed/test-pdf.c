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


#define OBJ \
    xref[xrefpos++] = ftell(f); \
    fprintf(f, "%d 0 obj\n", xrefpos);

#define ENDOBJ \
    fprintf(f, "endobj\n");

#define STREAMDICT \
    OBJ; \
    fprintf(f, "<<\n" \
	       "  /Length %d 0 R\n", xrefpos + 1);

#define STREAMDATA \
    fprintf(f, ">>\n" \
               "stream\n"); \
    stream_len = -ftell(f);

#define STREAM \
  STREAMDICT \
  STREAMDATA

#define ENDSTREAM \
  stream_len += ftell(f); \
  fprintf(f, "endstream\n" \
             "endobj\n"); \
  OBJ; \
  fprintf(f, "%d\n", stream_len); \
  ENDOBJ;


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
    for (iA = 0; str[iA]; iA ++)
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
		       _CF_FONTEMBED_EMB_DEST_PDF16,
		       _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE|
		       _CF_FONTEMBED_EMB_C_TAKE_FONTFILE);

  FILE *f = fopen("test.pdf", "w");
  DEBUG_assert(f);
  int xref[100], xrefpos = 3;
  int stream_len;

  fprintf(f, "%%PDF-1.3\n");
  // content
  STREAM;
  fprintf(f, "BT\n" // content
             "  100 100 Td\n"
             "  /F1 10 Tf\n");
  write_string(f, emb, "Hallo");
  fprintf(f, " Tj\n"
             "ET\n");
  ENDSTREAM;

  _cfFontEmbedEmbGet(emb, 'a');

  // {{{ do font
  _cf_fontembed_emb_pdf_font_descr_t *fdes = _cfFontEmbedEmbPDFFontDescr(emb);
  DEBUG_assert(fdes);
  _cf_fontembed_emb_pdf_font_widths_t *fwid = _cfFontEmbedEmbPDFFontWidths(emb);
  DEBUG_assert(fwid);

  STREAMDICT;
  int ff_ref = xrefpos;
  if (_cfFontEmbedEmbPDFGetFontFileSubType(emb))
  {
    fprintf(f,"  /Subtype /%s\n",
	    _cfFontEmbedEmbPDFGetFontFileSubType(emb));
  }
  if (emb->outtype == _CF_FONTEMBED_EMB_FMT_T1)
    fprintf(f, "  /Length1 ?\n"
               "  /Length2 ?\n"
               "  /Length3 ?\n");
  else if (emb->outtype == _CF_FONTEMBED_EMB_FMT_TTF)
    fprintf(f, "  /Length1 %d 0 R\n", xrefpos + 2);
  STREAMDATA;
  const int outlen = _cfFontEmbedEmbEmbed(emb, example_outfn, f);
  ENDSTREAM;
  if (emb->outtype == _CF_FONTEMBED_EMB_FMT_TTF)
  {
    OBJ;
    fprintf(f, "%d\n", outlen);
    ENDOBJ;
  }

  OBJ;
  const int fd_ref = xrefpos;
  char *res = _cfFontEmbedEmbPDFSimpleFontDescr(emb, fdes, ff_ref);
  DEBUG_assert(res);
  fputs(res, f);
  free(res);
  ENDOBJ;

  OBJ;
  int f_ref = xrefpos;
  res = _cfFontEmbedEmbPDFSimpleFont(emb, fdes, fwid, fd_ref);
  DEBUG_assert(res);
  fputs(res, f);
  free(res);
  ENDOBJ;

  if (emb->plan&_CF_FONTEMBED_EMB_A_MULTIBYTE)
  {
    OBJ;
    res = _cfFontEmbedEmbPDFSimpleCIDFont(emb, fdes->fontname, f_ref);
    f_ref = xrefpos;
    DEBUG_assert(res);
    fputs(res, f);
    free(res);
    ENDOBJ;
  }

  free(fdes);
  free(fwid);
  // }}}

  int iA;

  xref[2] = ftell(f);
  fprintf(f, "3 0 obj\n"
             "<</Type/Page\n"
             "  /Parent 2 0 R\n"
             "  /MediaBox [0 0 595 842]\n"
             "  /Contents 4 0 R\n"
             "  /Resources <<\n"
             "    /Font <<\n"
             "      /F1 %d 0 R\n"
             "    >>\n"
             "  >>\n"
             ">>\n"
             "endobj\n",
             f_ref);
  xref[1] = ftell(f);
  fprintf(f, "2 0 obj\n"
             "<</Type/Pages\n"
             "  /Count 1\n"
             "  /Kids [3 0 R]"
             ">>\n"
             "endobj\n");
  xref[0] = ftell(f);
  fprintf(f, "1 0 obj\n"
             "<</Type/Catalog\n"
             "  /Pages 2 0 R\n"
             ">>\n"
             "endobj\n");
  // {{{ pdf trailer
  int xref_start = ftell(f);
  fprintf(f, "xref\n"
             "0 %d\n"
             "%010d 65535 f \n",
             xrefpos + 1,0);
  for (iA = 0; iA < xrefpos; iA ++)
    fprintf(f, "%010d 00000 n \n", xref[iA]);
  fprintf(f, "trailer\n"
             "<<\n"
             "  /Size %d\n"
             "  /Root 1 0 R\n"
             ">>\n"
             "startxref\n"
             "%d\n"
             "%%%%EOF\n",
             xrefpos + 1, xref_start);
  // }}}
  fclose(f);

  _cfFontEmbedEmbClose(emb);

  return (0);
}

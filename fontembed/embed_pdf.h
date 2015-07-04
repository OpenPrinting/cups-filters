#ifndef EMBED_PDF_H
#define EMBED_PDF_H

// all the necessary information for pdf font embedding
typedef struct {
  char *fontname;
  unsigned int flags;

  // for the following: 0=not set/invalid
  int bbxmin,bbymin,bbxmax,bbymax;
  int italicAngle;    // >=90: not set/invalid
  int ascent;
  int descent;
  int capHeight;
  int stemV;
  // optional, default=0:
  int xHeight;
  int avgWidth;

  // CID-additions:
  char *panose; // 12 bytes
  char *registry,*ordering;
  int supplement;

  char data[1]; // used for storing e.g. >fontname
} EMB_PDF_FONTDESCR;

typedef struct {
  // normal font
  int first,last;
  int *widths;

  // multibyte font
  int default_width;
  int *warray; // format: (len c w ... w)*   if (len<0) { c1 (c2=c1+(-len)) w } else { c w[len] }, terminated by len==0

  int data[1];
} EMB_PDF_FONTWIDTHS;

const char *emb_pdf_get_font_subtype(EMB_PARAMS *emb);
const char *emb_pdf_get_fontfile_key(EMB_PARAMS *emb);
const char *emb_pdf_get_fontfile_subtype(EMB_PARAMS *emb);

EMB_PDF_FONTDESCR *emb_pdf_fontdescr(EMB_PARAMS *emb);
EMB_PDF_FONTWIDTHS *emb_pdf_fontwidths(EMB_PARAMS *emb);

/** TODO elsewhere **/
char *emb_pdf_simple_fontdescr(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,int fontfile_obj_ref);
char *emb_pdf_simple_font(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,EMB_PDF_FONTWIDTHS *fwid,int fontdescr_obj_ref);
char *emb_pdf_simple_cidfont(EMB_PARAMS *emb,const char *fontname,int descendant_obj_ref);
char *emb_pdf_simple_stdfont(EMB_PARAMS *emb);

#endif

#ifndef EMBED_H
#define EMBED_H

#include "bitset.h"
#include "fontfile.h"
#include "iofn.h"

typedef enum { EMB_INPUT_T1,     // type1-lib, with AFM/PFM,PFA/PFB
               EMB_INPUT_TTF,    // sfnt-lib, for TTF(glyf)
               EMB_INPUT_OTF,    // sfnt-lib + cff-lib, for OTF
               EMB_INPUT_CFF,    // cff-lib, for raw CFF
               EMB_INPUT_STDFONT // don't embed (already present)
               } EMB_INPUT_FORMAT;
typedef enum { EMB_OUTPUT_T1,    // original type1
               EMB_OUTPUT_TTF,   // ttf(glyf)
               EMB_OUTPUT_CFF,   // raw cff
               EMB_OUTPUT_SFNT   // OpenType (cff or glyf)
               } EMB_OUTPUT_FORMAT;
typedef enum { EMB_DEST_NATIVE,  // just subsetting/conversion
               EMB_DEST_PS, 
               EMB_DEST_PDF16  // TODO? PDF13
               } EMB_DESTINATION;

typedef enum { EMB_RIGHT_FULL=0, EMB_RIGHT_NONE=0x02,
               EMB_RIGHT_READONLY=0x04, 
               EMB_RIGHT_NO_SUBSET=0x0100,
               EMB_RIGHT_BITMAPONLY=0x0200 } EMB_RIGHT_TYPE;

typedef enum { EMB_A_MULTIBYTE=0x01,    // embedd as multibyte font?
               EMB_A_SUBSET=0x02,       // do subsetting?
               EMB_A_CONVERT_CFF=0x04,  // convert Type1 to CFF?
               EMB_A_WRAP_SFNT=0x08,    // wrap in sfnt? (OTF)

               EMB_A_CLOSE_FONTFILE=0x8000
               } EMB_ACTIONS;

typedef struct _EMB_PARAMS {
  EMB_INPUT_FORMAT intype;
  EMB_OUTPUT_FORMAT outtype;
  EMB_DESTINATION dest;

  EMB_ACTIONS plan;

  // font infos
  FONTFILE *font;
  EMB_RIGHT_TYPE rights;
// public:
  BITSET subset;

} EMB_PARAMS;

typedef enum { EMB_C_MUST_SUBSET=0x01,     // (fail, when not possible)
               EMB_C_EDITABLE_SUBSET=0x02, // (...)
               EMB_C_NEVER_SUBSET=0x04,    // (...)

               EMB_C_FORCE_MULTIBYTE=0x08, // always use multibyte fonts

               EMB_C_PDF_OT=0x10, // output CFF and even TTF as OpenType (for PDF)
               EMB_C_KEEP_T1=0x20, // don't convert T1 to CFF

               EMB_C_TAKE_FONTFILE=0x8000 // take ownership of fontfile
               } EMB_CONSTRAINTS;

EMB_PARAMS *emb_new(FONTFILE *font,EMB_DESTINATION dest,EMB_CONSTRAINTS mode);
int emb_embed(EMB_PARAMS *emb,OUTPUT_FN output,void *context); // returns number of bytes written
void emb_close(EMB_PARAMS *emb);

/*** PDF out stuff ***/
// all the necessary information for pdf font embedding
typedef struct {
  char *fontname;
  unsigned int flags;

  // for the following: 0=not set/invalid
  int bbxmin,bbymin,bbxmax,bbymax;
  int italicAngle;    // >=90: not set/invalid
  int ascend;
  int descend;
  int capHeight;
  int stemV;
  // optional, default=0:
  int xHeight;
  int avgWidth;

  // CID-additions:
  char *panose; // 12 bytes
  char *registry,*ordering;
  int supplement;

  char data[]; // used for storing e.g. >fontname
} EMB_PDF_FONTDESCR;

typedef struct {
  // normal font
  int first,last;
  int *widths;

  // multibyte font
  int default_width;
  int *warray; // format: len c w ... w   if (len<0) { c1 (c2=c1+(-len)) w } else { c w[len] }, terminated by len==0

  int data[];
} EMB_PDF_FONTWIDTHS;

const char *emb_pdf_get_font_subtype(EMB_PARAMS *emb);
const char *emb_pdf_get_fontfile_key(EMB_PARAMS *emb);
const char *emb_pdf_get_fontfile_subtype(EMB_PARAMS *emb);

EMB_PDF_FONTDESCR *emb_pdf_fontdescr(EMB_PARAMS *emb);
EMB_PDF_FONTWIDTHS *emb_pdf_fontwidths(EMB_PARAMS *emb);

char *emb_pdf_simple_fontdescr(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,int fontfile_obj_ref);
char *emb_pdf_simple_font(EMB_PARAMS *emb,EMB_PDF_FONTDESCR *fdes,EMB_PDF_FONTWIDTHS *fwid,int fontdescr_obj_ref);
char *emb_pdf_simple_cidfont(EMB_PARAMS *emb,const char *fontname,int descendant_obj_ref);
char *emb_pdf_simple_stdfont(EMB_PARAMS *emb);

#endif

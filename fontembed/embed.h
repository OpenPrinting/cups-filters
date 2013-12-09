#ifndef EMBED_H
#define EMBED_H

#include "bitset.h"
#include "fontfile.h"
#include "iofn.h"

typedef enum { EMB_FMT_T1,       // type1, with AFM/PFM,PFA/PFB
               EMB_FMT_TTF,      // sfnt, for TTF(glyf)
               EMB_FMT_OTF,      // sfnt+cff, for OTF(cff)
               EMB_FMT_CFF,      // cff, for raw CFF
               EMB_FMT_STDFONT   // don't embed (already present)
               } EMB_FORMAT;
typedef enum { EMB_DEST_NATIVE,  // just subsetting/conversion
               EMB_DEST_PS,
//               EMB_DEST_PS2,
//               EMB_DEST_PDF13,
               EMB_DEST_PDF16
               } EMB_DESTINATION;

typedef enum { EMB_RIGHT_FULL=0, EMB_RIGHT_NONE=0x02,
               EMB_RIGHT_READONLY=0x04,
               EMB_RIGHT_NO_SUBSET=0x0100,
               EMB_RIGHT_BITMAPONLY=0x0200 } EMB_RIGHT_TYPE;

typedef enum { EMB_A_MULTIBYTE=0x01,    // embedd as multibyte font?
               EMB_A_SUBSET=0x02,       // do subsetting?
               EMB_A_T1_TO_CFF=0x04,    // convert Type1 to CFF?
               EMB_A_CFF_TO_OTF=0x08,   // wrap CFF(from input or T1+CONVERT_CFF) in sfnt? (OTF)
               EMB_A_OTF_TO_CFF=0x10,   // unwrap CFF

               EMB_A_CLOSE_FONTFILE=0x8000
               } EMB_ACTIONS;

typedef enum { EMB_C_MUST_SUBSET=0x01,     // (fail, when not possible)
               EMB_C_EDITABLE_SUBSET=0x02, // (...)
               EMB_C_NEVER_SUBSET=0x04,    // (...)

               EMB_C_FORCE_MULTIBYTE=0x08, // always use multibyte fonts

               EMB_C_PDF_OT=0x10, // output TTF/OTF (esp. CFF to OTF)
               EMB_C_KEEP_T1=0x20, // don't convert T1 to CFF

               EMB_C_TAKE_FONTFILE=0x8000 // take ownership of fontfile
               } EMB_CONSTRAINTS;

typedef struct _EMB_PARAMS {
  EMB_FORMAT intype;
  EMB_FORMAT outtype;
  EMB_DESTINATION dest;

  EMB_ACTIONS plan;

  // font infos
  FONTFILE *font;
  EMB_RIGHT_TYPE rights;
// public:
  BITSET subset;

} EMB_PARAMS;

EMB_PARAMS *emb_new(FONTFILE *font,EMB_DESTINATION dest,EMB_CONSTRAINTS mode);
// emb_embedd does only the "binary" part
int emb_embed(EMB_PARAMS *emb,OUTPUT_FN output,void *context); // returns number of bytes written
void emb_close(EMB_PARAMS *emb);

// TODO: encoding, TODO: ToUnicode
static inline void emb_set(EMB_PARAMS *emb,int unicode,unsigned short gid) // {{{
{
  if (emb->subset) {
    if (emb->plan&EMB_A_MULTIBYTE) {
      bit_set(emb->subset,gid);
      // ToUnicode.add(gid,unicode);
    } else {
      // TODO ... encoding
    }
  }
}
// }}}

// TODO: encoding?, TODO: non-sfnt
static inline unsigned short emb_get(EMB_PARAMS *emb,int unicode) // {{{ gid
{
  const unsigned short gid=otf_from_unicode(emb->font->sfnt,unicode);
  emb_set(emb,unicode,gid);
  return gid;
}
// }}}

#include "embed_pdf.h"

#endif

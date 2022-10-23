//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPSFILTERS_FONTEMBED_H_
#define _CUPSFILTERS_FONTEMBED_H_


//
// Include necessary headers...
//

#include <stdlib.h>
#include <stdio.h>


//
// Constants and macros...
//

#define _CF_FONTEMBED_OTF_F_FMT_CFF      0x10000
#define _CF_FONTEMBED_OTF_F_DO_CHECKSUM  0x40000

#define _CF_FONTEMBED_OTF_TAG(a, b, c, d) (unsigned int)(((a) << 24) | \
							 ((b) << 16) | \
							 ((c) << 8) | (d))
#define _CF_FONTEMBED_OTF_UNTAG(a) (((unsigned int)(a) >> 24) & 0xff), \
                                   (((unsigned int)(a) >> 16) & 0xff), \
                                   (((unsigned int)(a) >> 8) & 0xff), \
                                   (((unsigned int)(a)) & 0xff)


//
// Types and structures...
//

// OpenType Font (OTF) handling

typedef struct
{
  unsigned int tag;
  unsigned int checkSum;
  unsigned int offset;
  unsigned int length;
} _cf_fontembed_otf_dir_ent_t;

typedef struct
{
  FILE *f;
  unsigned int numTTC, useTTC;
  unsigned int version;

  unsigned short numTables;
  _cf_fontembed_otf_dir_ent_t *tables;

  int flags;
  unsigned short unitsPerEm;
  unsigned short indexToLocFormat; // 0=short, 1=long
  unsigned short numGlyphs;

  // optionally loaded data
  unsigned int *glyphOffsets;
  unsigned short numberOfHMetrics;
  char *hmtx, *name, *cmap;
  const char *unimap; // ptr to (3,1) or (3,0) cmap start

  // single glyf buffer, allocated large enough by __cfFontEmbedOTFLoadMore()
  char *gly;
  _cf_fontembed_otf_dir_ent_t *glyfTable;

} _cf_fontembed_otf_file_t;

// SFNT Font files

struct _cf_fontembed_fontfile_s
{
  _cf_fontembed_otf_file_t *sfnt;
  // ??? *cff;
  char *stdname;
  union
  {
    int fobj;
    void *user;
  };
};

typedef struct _cf_fontembed_fontfile_s _cf_fontembed_fontfile_t;

// Output callback function type

typedef void (*_cf_fontembed_output_fn_t)(const char *buf, int len,
					  void *context);

// Bit manipulation

typedef int* _cf_fontembed_bit_set_t;

// General font embedding

typedef enum
{
  _CF_FONTEMBED_EMB_FMT_T1,       // type1, with AFM/PFM,PFA/PFB
  _CF_FONTEMBED_EMB_FMT_TTF,      // sfnt, for TTF(glyf)
  _CF_FONTEMBED_EMB_FMT_OTF,      // sfnt+cff, for OTF(cff)
  _CF_FONTEMBED_EMB_FMT_CFF,      // cff, for raw CFF
  _CF_FONTEMBED_EMB_FMT_STDFONT   // don't embed (already present)
} _cf_fontembed_emb_format_t;

typedef enum
{
  _CF_FONTEMBED_EMB_DEST_NATIVE,  // just subsetting/conversion
  _CF_FONTEMBED_EMB_DEST_PS,
//_CF_FONTEMBED_EMB_DEST_PS2,
//_CF_FONTEMBED_EMB_DEST_PDF13,
  _CF_FONTEMBED_EMB_DEST_PDF16
} _cf_fontembed_emb_dest_t;

typedef enum
{
  _CF_FONTEMBED_EMB_RIGHT_FULL = 0,
  _CF_FONTEMBED_EMB_RIGHT_NONE = 0x02,
  _CF_FONTEMBED_EMB_RIGHT_READONLY = 0x04,
  _CF_FONTEMBED_EMB_RIGHT_NO_SUBSET = 0x0100,
  _CF_FONTEMBED_EMB_RIGHT_BITMAPONLY = 0x0200
} _cf_fontembed_emb_right_t;

typedef enum
{
  _CF_FONTEMBED_EMB_A_MULTIBYTE = 0x01,    // embedd as multibyte font?
  _CF_FONTEMBED_EMB_A_SUBSET = 0x02,       // do subsetting?
  _CF_FONTEMBED_EMB_A_T1_TO_CFF = 0x04,    // convert Type1 to CFF?
  _CF_FONTEMBED_EMB_A_CFF_TO_OTF = 0x08,   // wrap CFF(from input or
                                           // T1+CONVERT_CFF) in sfnt? (OTF)
  _CF_FONTEMBED_EMB_A_OTF_TO_CFF = 0x10,   // unwrap CFF

  _CF_FONTEMBED_EMB_A_CLOSE_FONTFILE = 0x8000
} _cf_fontembed_emb_action_t;

typedef enum
{
  _CF_FONTEMBED_EMB_C_MUST_SUBSET = 0x01,     // (fail, when not possible)
  _CF_FONTEMBED_EMB_C_EDITABLE_SUBSET = 0x02, // (...)
  _CF_FONTEMBED_EMB_C_NEVER_SUBSET = 0x04,    // (...)

  _CF_FONTEMBED_EMB_C_FORCE_MULTIBYTE = 0x08, // always use multibyte fonts

  _CF_FONTEMBED_EMB_C_PDF_OT = 0x10,          // output TTF/OTF (esp. CFF to
                                              // OTF)
  _CF_FONTEMBED_EMB_C_KEEP_T1 = 0x20,         // don't convert T1 to CFF

  _CF_FONTEMBED_EMB_C_TAKE_FONTFILE = 0x8000  // take ownership of fontfile
} _cf_fontembed_emb_constraint_t;

typedef struct _cf_fontembed_emb_params_s
{
  _cf_fontembed_emb_format_t intype;
  _cf_fontembed_emb_format_t outtype;
  _cf_fontembed_emb_dest_t dest;

  _cf_fontembed_emb_action_t plan;

  // font infos
  _cf_fontembed_fontfile_t *font;
  _cf_fontembed_emb_right_t rights;
// public:
  _cf_fontembed_bit_set_t subset;
} _cf_fontembed_emb_params_t;

// PDF file font embedding
typedef struct
{
  char *fontname;
  unsigned int flags;

  // for the following: 0 = not set/invalid
  int bbxmin, bbymin, bbxmax, bbymax;
  int italicAngle;    // >= 90: not set/invalid
  int ascent;
  int descent;
  int capHeight;
  int stemV;
  // optional, default = 0:
  int xHeight;
  int avgWidth;

  // CID-additions:
  char *panose; // 12 bytes
  char *registry, *ordering;
  int supplement;

  char data[1]; // used for storing e.g. > fontname
} _cf_fontembed_emb_pdf_font_descr_t;

typedef struct
{
  // normal font
  int first, last;
  int *widths;

  // multibyte font
  int default_width;
  int *warray; // format: (len c w ... w)*
               // if (len < 0) { c1 (c2 = c1 + (-len)) w } else { c w[len] },
               // terminated by len == 0

  int data[1];
} _cf_fontembed_emb_pdf_font_widths_t;


//
// Prototypes...
//

// OpenType Font (OTF) handling

// To load TTC collections: append e.g. "/3" for the third font in the file.
_cf_fontembed_otf_file_t *_cfFontEmbedOTFLoad(const char *file);
void _cfFontEmbedOTFClose(_cf_fontembed_otf_file_t *otf);

char *_cfFontEmbedOTFGetTable(_cf_fontembed_otf_file_t *otf, unsigned int tag,
			      int *ret_len);

int _cfFontEmbedOTFGetWidth(_cf_fontembed_otf_file_t *otf, unsigned short gid);
const char *_cfFontEmbedOTFGetName(_cf_fontembed_otf_file_t *otf,
				   int platformID, int encodingID,
				   int languageID, int nameID, int *ret_len);
int _cfFontEmbedOTFGetGlyph(_cf_fontembed_otf_file_t *otf, unsigned short gid);
unsigned short _cfFontEmbedOTFFromUnicode(_cf_fontembed_otf_file_t *otf,
					  int unicode);

// TODO?! allow glyphs==NULL for non-subsetting table reduction?
int _cfFontEmbedOTFSubSet(_cf_fontembed_otf_file_t *otf,
			  _cf_fontembed_bit_set_t glyphs,
			  _cf_fontembed_output_fn_t output, void *context);
int _cfFontEmbedOTFTTCExtract(_cf_fontembed_otf_file_t *otf,
			      _cf_fontembed_output_fn_t output, void *context);
int _cfFontEmbedOTFSubSetCFF(_cf_fontembed_otf_file_t *otf,
			     _cf_fontembed_bit_set_t glyphs,
			     _cf_fontembed_output_fn_t output, void *context);
int _cfFontEmbedOTFCFFExtract(_cf_fontembed_otf_file_t *otf,
			      _cf_fontembed_output_fn_t output, void *context);

// SFNT Font files

_cf_fontembed_fontfile_t
              *_cfFontEmbedFontFileOpenSFNT(_cf_fontembed_otf_file_t *otf);
_cf_fontembed_fontfile_t *_cfFontEmbedFontFileOpenStd(const char *name);
void _cfFontEmbedFontFileClose(_cf_fontembed_fontfile_t *ff);

// General font embedding

_cf_fontembed_emb_params_t
              *_cfFontEmbedEmbNew(_cf_fontembed_fontfile_t *font,
				  _cf_fontembed_emb_dest_t dest,
				  _cf_fontembed_emb_constraint_t mode);
// _cfFontEmbedEmbEmbed does only the "binary" part
int _cfFontEmbedEmbEmbed(_cf_fontembed_emb_params_t *emb,
			 _cf_fontembed_output_fn_t output, void *context);
                                            // returns number of bytes written
void _cfFontEmbedEmbClose(_cf_fontembed_emb_params_t *emb);

// PDF file font embedding

const char *_cfFontEmbedEmbPDFGetFontSubType(_cf_fontembed_emb_params_t *emb);
const char *_cfFontEmbedEmbPDFGetFontFileKey(_cf_fontembed_emb_params_t *emb);
const char
       *_cfFontEmbedEmbPDFGetFontFileSubType(_cf_fontembed_emb_params_t *emb);

_cf_fontembed_emb_pdf_font_descr_t
       *_cfFontEmbedEmbPDFFontDescr(_cf_fontembed_emb_params_t *emb);
_cf_fontembed_emb_pdf_font_widths_t
       *_cfFontEmbedEmbPDFFontWidths(_cf_fontembed_emb_params_t *emb);

/** TODO elsewhere **/
char *_cfFontEmbedEmbPDFSimpleFontDescr(_cf_fontembed_emb_params_t *emb,
				       _cf_fontembed_emb_pdf_font_descr_t *fdes,
				       int fontfile_obj_ref);
char *_cfFontEmbedEmbPDFSimpleFont(_cf_fontembed_emb_params_t *emb,
				   _cf_fontembed_emb_pdf_font_descr_t *fdes,
				   _cf_fontembed_emb_pdf_font_widths_t *fwid,
				   int fontdescr_obj_ref);
char *_cfFontEmbedEmbPDFSimpleCIDFont(_cf_fontembed_emb_params_t *emb,
				      const char *fontname,
				      int descendant_obj_ref);
char *_cfFontEmbedEmbPDFSimpleStdFont(_cf_fontembed_emb_params_t *emb);


//
// Inline functions...
//

// Bit manipulation

static inline void
_cfFontEmbedBitSet(_cf_fontembed_bit_set_t bs,
		   int num)
{
  bs[num / (8 * sizeof(int))] |= 1 << (num % (8 * sizeof(int)));
}


static inline int
_cfFontEmbedBitCheck(_cf_fontembed_bit_set_t bs,
		     int num)
{
  return bs [num / (8 * sizeof(int))] & 1 << (num % (8 * sizeof(int)));
}


// Use free() when done. returns NULL on bad_alloc
static inline _cf_fontembed_bit_set_t
_cfFontEmbedBitSetNew(int size)
{
  return (_cf_fontembed_bit_set_t)calloc(1, ((size + 8 * sizeof(int) - 1) &
					     ~(8 * sizeof(int) - 1)) / 8);
}


static inline int
_cfFontEmbedBitsUsed(_cf_fontembed_bit_set_t bits,
		     int size) // {{{  returns true if any bit is used
{
  size = (size + 8 * sizeof(int) - 1) / (8 * sizeof(int));
  while (size > 0)
  {
    if (*bits)
      return (1);
    bits ++;
    size --;
  }
  return (0);
}
// }}}

// General font embedding

// TODO: encoding, TODO: ToUnicode
static inline void
_cfFontEmbedEmbSet(_cf_fontembed_emb_params_t *emb,
		   int unicode,
		   unsigned short gid) // {{{
{
  if (emb->subset)
  {
    if (emb->plan & _CF_FONTEMBED_EMB_A_MULTIBYTE)
    {
      _cfFontEmbedBitSet(emb->subset, gid);
      // ToUnicode.add(gid, unicode);
    }
    else
    {
      // TODO ... encoding
    }
  }
}
// }}}

// TODO: encoding?, TODO: non-sfnt
static inline unsigned short
_cfFontEmbedEmbGet(_cf_fontembed_emb_params_t *emb, int unicode) // {{{ gid
{
  const unsigned short gid = _cfFontEmbedOTFFromUnicode(emb->font->sfnt,
							unicode);
  _cfFontEmbedEmbSet(emb, unicode, gid);
  return (gid);
}
// }}}


#endif // !_CUPSFILTERS_FONTEMBED_H_

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

#define OTF_F_FMT_CFF      0x10000
#define OTF_F_DO_CHECKSUM  0x40000

#define OTF_TAG(a, b, c, d) (unsigned int)(((a) << 24) | ((b) << 16) | \
					   ((c) << 8) | (d))
#define OTF_UNTAG(a) (((unsigned int)(a) >> 24) & 0xff), \
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
} OTF_DIRENT;

typedef struct
{
  FILE *f;
  unsigned int numTTC, useTTC;
  unsigned int version;

  unsigned short numTables;
  OTF_DIRENT *tables;

  int flags;
  unsigned short unitsPerEm;
  unsigned short indexToLocFormat; // 0=short, 1=long
  unsigned short numGlyphs;

  // optionally loaded data
  unsigned int *glyphOffsets;
  unsigned short numberOfHMetrics;
  char *hmtx, *name, *cmap;
  const char *unimap; // ptr to (3,1) or (3,0) cmap start

  // single glyf buffer, allocated large enough by otf_load_more()
  char *gly;
  OTF_DIRENT *glyfTable;

} OTF_FILE;

// SFNT Font files

struct _FONTFILE
{
  OTF_FILE *sfnt;
  // ??? *cff;
  char *stdname;
  union
  {
    int fobj;
    void *user;
  };
};

typedef struct _FONTFILE FONTFILE;

// Output callback function type

typedef void (*OUTPUT_FN)(const char *buf, int len, void *context);

// Bit manipulation

typedef int* BITSET;

// General font embedding

typedef enum
{
  EMB_FMT_T1,       // type1, with AFM/PFM,PFA/PFB
  EMB_FMT_TTF,      // sfnt, for TTF(glyf)
  EMB_FMT_OTF,      // sfnt+cff, for OTF(cff)
  EMB_FMT_CFF,      // cff, for raw CFF
  EMB_FMT_STDFONT   // don't embed (already present)
} EMB_FORMAT;

typedef enum
{
  EMB_DEST_NATIVE,  // just subsetting/conversion
  EMB_DEST_PS,
//EMB_DEST_PS2,
//EMB_DEST_PDF13,
  EMB_DEST_PDF16
} EMB_DESTINATION;

typedef enum
{
  EMB_RIGHT_FULL = 0,
  EMB_RIGHT_NONE = 0x02,
  EMB_RIGHT_READONLY = 0x04,
  EMB_RIGHT_NO_SUBSET = 0x0100,
  EMB_RIGHT_BITMAPONLY = 0x0200
} EMB_RIGHT_TYPE;

typedef enum
{
  EMB_A_MULTIBYTE = 0x01,    // embedd as multibyte font?
  EMB_A_SUBSET = 0x02,       // do subsetting?
  EMB_A_T1_TO_CFF = 0x04,    // convert Type1 to CFF?
  EMB_A_CFF_TO_OTF = 0x08,   // wrap CFF(from input or T1+CONVERT_CFF) in sfnt?
                             // (OTF)
  EMB_A_OTF_TO_CFF = 0x10,   // unwrap CFF

  EMB_A_CLOSE_FONTFILE = 0x8000
} EMB_ACTIONS;

typedef enum
{
  EMB_C_MUST_SUBSET = 0x01,     // (fail, when not possible)
  EMB_C_EDITABLE_SUBSET = 0x02, // (...)
  EMB_C_NEVER_SUBSET = 0x04,    // (...)

  EMB_C_FORCE_MULTIBYTE = 0x08, // always use multibyte fonts

  EMB_C_PDF_OT = 0x10,          // output TTF/OTF (esp. CFF to OTF)
  EMB_C_KEEP_T1 = 0x20,         // don't convert T1 to CFF

  EMB_C_TAKE_FONTFILE = 0x8000  // take ownership of fontfile
} EMB_CONSTRAINTS;

typedef struct _EMB_PARAMS
{
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
} EMB_PDF_FONTDESCR;

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
} EMB_PDF_FONTWIDTHS;


//
// Prototypes...
//

// OpenType Font (OTF) handling

// To load TTC collections: append e.g. "/3" for the third font in the file.
OTF_FILE *otf_load(const char *file);
void otf_close(OTF_FILE *otf);

char *otf_get_table(OTF_FILE *otf, unsigned int tag, int *ret_len);

int otf_get_width(OTF_FILE *otf, unsigned short gid);
const char *otf_get_name(OTF_FILE *otf, int platformID, int encodingID,
			 int languageID, int nameID, int *ret_len);
int otf_get_glyph(OTF_FILE *otf, unsigned short gid);
unsigned short otf_from_unicode(OTF_FILE *otf, int unicode);

// TODO?! allow glyphs==NULL for non-subsetting table reduction?
int otf_subset(OTF_FILE *otf, BITSET glyphs, OUTPUT_FN output, void *context);
int otf_ttc_extract(OTF_FILE *otf, OUTPUT_FN output, void *context);
int otf_subset_cff(OTF_FILE *otf, BITSET glyphs, OUTPUT_FN output,
		   void *context);
int otf_cff_extract(OTF_FILE *otf, OUTPUT_FN output, void *context);

// SFNT Font files

FONTFILE *fontfile_open_sfnt(OTF_FILE *otf);
FONTFILE *fontfile_open_std(const char *name);
void fontfile_close(FONTFILE *ff);

// General font embedding

EMB_PARAMS *emb_new(FONTFILE *font, EMB_DESTINATION dest, EMB_CONSTRAINTS mode);
// emb_embed does only the "binary" part
int emb_embed(EMB_PARAMS *emb, OUTPUT_FN output, void *context);
                                          // returns number of bytes written
void emb_close(EMB_PARAMS *emb);

// PDF file font embedding

const char *emb_pdf_get_font_subtype(EMB_PARAMS *emb);
const char *emb_pdf_get_fontfile_key(EMB_PARAMS *emb);
const char *emb_pdf_get_fontfile_subtype(EMB_PARAMS *emb);

EMB_PDF_FONTDESCR *emb_pdf_fontdescr(EMB_PARAMS *emb);
EMB_PDF_FONTWIDTHS *emb_pdf_fontwidths(EMB_PARAMS *emb);

/** TODO elsewhere **/
char *emb_pdf_simple_fontdescr(EMB_PARAMS *emb, EMB_PDF_FONTDESCR *fdes,
			       int fontfile_obj_ref);
char *emb_pdf_simple_font(EMB_PARAMS *emb, EMB_PDF_FONTDESCR *fdes,
			  EMB_PDF_FONTWIDTHS *fwid, int fontdescr_obj_ref);
char *emb_pdf_simple_cidfont(EMB_PARAMS *emb, const char *fontname,
			     int descendant_obj_ref);
char *emb_pdf_simple_stdfont(EMB_PARAMS *emb);


//
// Inline functions...
//

// Bit manipulation

static inline void
bit_set(BITSET bs,
	int num)
{
  bs[num / (8 * sizeof(int))] |= 1 << (num % (8 * sizeof(int)));
}


static inline int
bit_check(BITSET bs,
	  int num)
{
  return bs [num / (8 * sizeof(int))] & 1 << (num % (8 * sizeof(int)));
}


// Use free() when done. returns NULL on bad_alloc
static inline BITSET
bitset_new(int size)
{
  return (BITSET)calloc(1, ((size + 8 * sizeof(int) - 1) &
			    ~(8 * sizeof(int) - 1)) / 8);
}


static inline int
bits_used(BITSET bits,
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
emb_set(EMB_PARAMS *emb,
	int unicode,
	unsigned short gid) // {{{
{
  if (emb->subset)
  {
    if (emb->plan & EMB_A_MULTIBYTE)
    {
      bit_set(emb->subset, gid);
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
emb_get(EMB_PARAMS *emb, int unicode) // {{{ gid
{
  const unsigned short gid = otf_from_unicode(emb->font->sfnt, unicode);
  emb_set(emb, unicode, gid);
  return (gid);
}
// }}}


#endif // !_CUPSFILTERS_FONTEMBED_H_

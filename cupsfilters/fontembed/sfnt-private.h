//
// Copyright © 2008,2012 by Tobias Hoffmann.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _FONTEMBED_SFNT_INT_H_
#define _FONTEMBED_SFNT_INT_H_


//
// Types and structures...
//

struct __cf_fontembed_otf_write_s
{
  unsigned long tag;
  int (*action)(void *param, int length, _cf_fontembed_output_fn_t output,
		void *context);
         // -1 on error, num_bytes_written on success; if >output == NULL
         // return checksum in (unsigned int *)context  instead.
  void *param;
  int length;
};


//
// Inline functions...
//

static inline unsigned short
__cfFontEmbedGetUShort(const char *buf) // {{{
{
  return (((unsigned char)buf[0] << 8) | ((unsigned char)buf[1]));
}
// }}}


static inline short
__cfFontEmbedGetShort(const char *buf) // {{{
{
  return ((buf[0] << 8) | ((unsigned char)buf[1]));
}
// }}}


static inline unsigned int
__cfFontEmbedGetUInt24(const char *buf) // {{{
{
  return (((unsigned char)buf[0] << 16) |
	  ((unsigned char)buf[1] << 8 )|
	  ((unsigned char)buf[2]));
}
// }}}


static inline unsigned int
__cfFontEmbedGetULong(const char *buf) // {{{
{
  return (((unsigned char)buf[0] << 24) |
	  ((unsigned char)buf[1] << 16) |
	  ((unsigned char)buf[2] << 8) |
	  ((unsigned char)buf[3]));
}
// }}}


static inline int
__cfFontEmbedGetLong(const char *buf) // {{{
{
  return ((buf[0] << 24) |
	  ((unsigned char)buf[1] << 16) |
	  ((unsigned char)buf[2] << 8) |
	  ((unsigned char)buf[3]));
}
// }}}


static inline void
__cfFontEmbedSetUShort(char *buf,
	   unsigned short val) // {{{
{
  buf[0] = val >> 8;
  buf[1] = val & 0xff;
}


// }}}
static inline void
__cfFontEmbedSetULong(char *buf,
	  unsigned int val) // {{{
{
  buf[0] = val >> 24;
  buf[1] = (val >> 16) & 0xff;
  buf[2] = (val >> 8) & 0xff;
  buf[3] = val & 0xff;
}
// }}}


static inline unsigned int
__cfFontEmbedOTFCheckSum(const char *buf,
	     unsigned int len) // {{{
{
  unsigned int ret = 0;
  for (len = (len + 3) / 4; len > 0; len--, buf += 4)
    ret += __cfFontEmbedGetULong(buf);
  return (ret);
}
// }}}


static inline int
__cfFontEmbedGetWidthFast(_cf_fontembed_otf_file_t *otf,
	       int gid) // {{{
{
  if (gid >= otf->numberOfHMetrics)
    return (__cfFontEmbedGetUShort(otf->hmtx +
				   (otf->numberOfHMetrics - 1) * 4));
  else
    return (__cfFontEmbedGetUShort(otf->hmtx + gid * 4));
}
// }}}


//
// Prototypes...
//

int __cfFontEmbedOTFLoadGlyf(_cf_fontembed_otf_file_t *otf); //  - 0 on success
int __cfFontEmbedOTFLoadMore(_cf_fontembed_otf_file_t *otf); //  - 0 on success

int __cfFontEmbedOTFFindTable(_cf_fontembed_otf_file_t *otf,
			      unsigned int tag); // - table_index  or
                                                 //   -1 on error

int __cfFontEmbedOTFActionCopy(void *param, int csum,
			       _cf_fontembed_output_fn_t output, void *context);
int __cfFontEmbedOTFActionReplace(void *param, int csum,
				  _cf_fontembed_output_fn_t output,
				  void *context);

// Note: Don't use this directly. __cfFontEmbedOTFWriteSFNT will internally
//       replace
// __cfFontEmbedOTFActionCopy for head with this
int __cfFontEmbedOTFActionCopyHead(void *param, int csum,
				   _cf_fontembed_output_fn_t output,
				   void *context);

int __cfFontEmbedOTFWriteSFNT(struct __cf_fontembed_otf_write_s *otw,
			      unsigned int version, int numTables,
			      _cf_fontembed_output_fn_t output, void *context);

/** from sfnt_subset.c: **/

// otw {0, }-terminated, will be modified; returns numTables for
// __cfFontEmbedOTFWriteSFNT
int __cfFontEmbedOTFIntersectTables(_cf_fontembed_otf_file_t *otf,
				    struct __cf_fontembed_otf_write_s *otw);

#endif // !_FONTEMBED_SFNT_INT_H_

#ifndef _SFNT_INT_H
#define _SFNT_INT_H

static inline unsigned short get_USHORT(const char *buf) // {{{
{
  return ((unsigned char)buf[0]<<8)|((unsigned char)buf[1]);
}
// }}}
static inline short get_SHORT(const char *buf) // {{{
{
  return (buf[0]<<8)|((unsigned char)buf[1]);
}
// }}}
static inline unsigned int get_UINT24(const char *buf) // {{{
{
  return ((unsigned char)buf[0]<<16)|
         ((unsigned char)buf[1]<<8)|
         ((unsigned char)buf[2]);
}
// }}}
static inline unsigned int get_ULONG(const char *buf) // {{{
{
  return ((unsigned char)buf[0]<<24)|
         ((unsigned char)buf[1]<<16)|
         ((unsigned char)buf[2]<<8)|
         ((unsigned char)buf[3]);
}
// }}}
static inline int get_LONG(const char *buf) // {{{
{
  return (buf[0]<<24)|
         ((unsigned char)buf[1]<<16)|
         ((unsigned char)buf[2]<<8)|
         ((unsigned char)buf[3]);
}
// }}}

static inline void set_USHORT(char *buf,unsigned short val) // {{{
{
  buf[0]=val>>8;
  buf[1]=val&0xff;
}
// }}}
static inline void set_ULONG(char *buf,unsigned int val) // {{{
{
  buf[0]=val>>24;
  buf[1]=(val>>16)&0xff;
  buf[2]=(val>>8)&0xff;
  buf[3]=val&0xff;
}
// }}}

static inline unsigned int otf_checksum(const char *buf, unsigned int len) // {{{
{
  unsigned int ret=0;
  for (len=(len+3)/4;len>0;len--,buf+=4) {
    ret+=get_ULONG(buf);
  }
  return ret;
}
// }}}
static inline int get_width_fast(OTF_FILE *otf,int gid) // {{{
{
  if (gid>=otf->numberOfHMetrics) {
    return get_USHORT(otf->hmtx+(otf->numberOfHMetrics-1)*4);
  } else {
    return get_USHORT(otf->hmtx+gid*4);
  }
}
// }}}

int otf_load_glyf(OTF_FILE *otf); //  - 0 on success
int otf_load_more(OTF_FILE *otf); //  - 0 on success

int otf_find_table(OTF_FILE *otf,unsigned int tag); // - table_index  or -1 on error

int otf_action_copy(void *param,int csum,OUTPUT_FN output,void *context);
int otf_action_replace(void *param,int csum,OUTPUT_FN output,void *context);

// Note: don't use this directly. otf_write_sfnt will internally replace otf_action_copy for head with this
int otf_action_copy_head(void *param,int csum,OUTPUT_FN output,void *context);

struct _OTF_WRITE {
  unsigned long tag;
  int (*action)(void *param,int length,OUTPUT_FN output,void *context); // -1 on error, num_bytes_written on success; if >output==NULL return checksum in (unsigned int *)context  instead.
  void *param;
  int length;
};

int otf_write_sfnt(struct _OTF_WRITE *otw,unsigned int version,int numTables,OUTPUT_FN output,void *context);

/** from sfnt_subset.c: **/

// otw {0,}-terminated, will be modified; returns numTables for otf_write_sfnt
int otf_intersect_tables(OTF_FILE *otf,struct _OTF_WRITE *otw);

#endif

#ifndef _FONTFILE_H
#define _FONTFILE_H

#include "sfnt.h"

struct _FONTFILE {
  OTF_FILE *sfnt;
  // ??? *cff;
  char *stdname;
  union {
    int fobj;
    void *user;
  };
};

typedef struct _FONTFILE FONTFILE;

FONTFILE *fontfile_open_sfnt(OTF_FILE *otf);
FONTFILE *fontfile_open_std(const char *name);
void fontfile_close(FONTFILE *ff);

#endif

#include "fontfile.h"
#include <assert.h>
#include <string.h>

//FONTFILE *fontfile_open(const char *filename);

/*
FONTFILE *fontfile_open(const char *filename)
{
  // TODO? check magic
  if (...) {
  }
}
*/

FONTFILE *fontfile_open_sfnt(OTF_FILE *otf) // {{{
{
  if (!otf) {
    assert(0);
    return NULL;
  }
  FONTFILE *ret=calloc(1,sizeof(FONTFILE));

  ret->sfnt=otf;

  return ret;
}
// }}}

FONTFILE *fontfile_open_std(const char *name) // {{{
{
  assert(name);
  FONTFILE *ret=calloc(1,sizeof(FONTFILE));

  ret->stdname=strdup(name);

  return ret;
}
// }}}

void fontfile_close(FONTFILE *ff) // {{{
{
  if (ff) {
    otf_close(ff->sfnt);
    // ??? cff_close(ff->cff);
    free(ff->stdname);
    free(ff);
  }
}
// }}}

/*
 *   PPD attribute lookup routine for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   cfFindAttr() - Find a PPD attribute based on the colormodel,
 *                    media, and resolution.
 */

/*
 * Include necessary headers.
 */

#include <config.h>
#include "driver.h"
#include <string.h>
#include <ctype.h>


/*
 * 'cfFindAttr()' - Find a PPD attribute based on the colormodel,
 *                    media, and resolution.
 */

ppd_attr_t *				/* O - Matching attribute or NULL */
cfFindAttr(ppd_file_t *ppd,		/* I - PPD file */
             const char *name,		/* I - Attribute name */
             const char *colormodel,	/* I - Color model */
             const char *media,		/* I - Media type */
             const char *resolution,	/* I - Resolution */
	     char       *spec,		/* O - Final selection string */
	     int        specsize,	/* I - Size of string buffer */
	     cf_logfunc_t log,      /* I - Log function */
	     void       *ld)            /* I - Log function data */
{
  ppd_attr_t	*attr;			/* Attribute */


 /*
  * Range check input...
  */

  if (!ppd || !name || !colormodel || !media || !resolution || !spec ||
      specsize < PPD_MAX_NAME)
    return (NULL);

 /*
  * Look for the attribute with the following keywords:
  *
  *     ColorModel.MediaType.Resolution
  *     ColorModel.Resolution
  *     ColorModel
  *     MediaType.Resolution
  *     MediaType
  *     Resolution
  *     ""
  */

  snprintf(spec, specsize, "%s.%s.%s", colormodel, media, resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s.%s", colormodel, resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s", colormodel);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s.%s", media, resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s", media);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  snprintf(spec, specsize, "%s", resolution);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s %s\"...", name, spec);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  spec[0] = '\0';
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Looking for \"*%s\"...", name);
  if ((attr = ppdFindAttr(ppd, name, spec)) != NULL && attr->value != NULL)
    return (attr);

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "No instance of \"*%s\" found...", name);

  return (NULL);
}


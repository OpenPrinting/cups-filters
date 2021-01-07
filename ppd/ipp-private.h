/*
 * Private IPP definitions for libppd.
 *
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _PPD_IPP_PRIVATE_H_
#  define _PPD_IPP_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include <cups/file.h>
#  include <cups/ipp.h>


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Structures...
 */

typedef struct _ppd_ipp_option_s		/**** Attribute mapping data ****/
{
  int		multivalue;		/* Option has multiple values? */
  const char	*name;			/* Option/attribute name */
  ipp_tag_t	value_tag;		/* Value tag for this attribute */
  ipp_tag_t	group_tag;		/* Group tag for this attribute */
  ipp_tag_t	alt_group_tag;		/* Alternate group tag for this
					 * attribute */
  const ipp_op_t *operations;		/* Allowed operations for this attr */
} _ppd_ipp_option_t;

/*
 * Prototypes for private functions...
 */

/* encode.c */
extern _ppd_ipp_option_t	*_ppdIppFindOption(const char *name);


/*
 * C++ magic...
 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_PPD_IPP_PRIVATE_H_ */

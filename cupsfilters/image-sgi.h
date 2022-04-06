/*
 *   SGI image file format library definitions for CUPS Filters.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_FILTERS_SGI_H_
#  define _CUPS_FILTERS_SGI_H_

#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>

#  ifdef __cplusplus
extern "C" {
#  endif


/*
 * Constants...
 */

#  define CF_SGI_MAGIC		474	/* Magic number in image file */

#  define CF_SGI_READ		0	/* Read from an SGI image file */
#  define CF_SGI_WRITE		1	/* Write to an SGI image file */

#  define CF_SGI_COMP_NONE	0	/* No compression */
#  define CF_SGI_COMP_RLE	1	/* Run-length encoding */
#  define CF_SGI_COMP_ARLE	2	/* Agressive run-length encoding */


/*
 * Image structure...
 */

typedef struct
{
  FILE			*file;		/* Image file */
  int			mode,		/* File open mode */
			bpp,		/* Bytes per pixel/channel */
			comp;		/* Compression */
  unsigned short	xsize,		/* Width in pixels */
			ysize,		/* Height in pixels */
			zsize;		/* Number of channels */
  long			firstrow,	/* File offset for first row */
			nextrow,	/* File offset for next row */
			**table,	/* Offset table for compression */
			**length;	/* Length table for compression */
  unsigned short	*arle_row;	/* Advanced RLE compression buffer */
  long			arle_offset,	/* Advanced RLE buffer offset */
			arle_length;	/* Advanced RLE buffer length */
} cf_sgi_t;


/*
 * Prototypes...
 */

extern int	cfSGIClose(cf_sgi_t *sgip);
extern int	cfSGIGetRow(cf_sgi_t *sgip, unsigned short *row, int y, int z);
extern cf_sgi_t	*cfSGIOpen(const char *filename, int mode, int comp, int bpp,
			   int xsize, int ysize, int zsize);
extern cf_sgi_t	*cfSGIOpenFile(FILE *file, int mode, int comp, int bpp,
			       int xsize, int ysize, int zsize);
extern int	cfSGIPutRow(cf_sgi_t *sgip, unsigned short *row, int y, int z);

#  ifdef __cplusplus
}
#  endif
#endif /* !_CUPS_FILTERS_SGI_H_ */


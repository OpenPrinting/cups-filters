/*
 * Include necessary headers...
 */

#include "colormanager.h"
#include "filter.h"
#include "image.h"
#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <assert.h>
#include <zlib.h>

/*
 * Types...
 */
typedef struct {                /**** Document information ****/
  cups_file_t	*inputfp;		  /* Temporary file, if any */
  FILE		*outputfp;		  /* Temporary file, if any */
  filter_logfunc_t logfunc;               /* Logging function, NULL for no
					     logging */
  void          *logdata;                 /* User data for logging function, can
					     be NULL */
  filter_iscanceledfunc_t iscanceledfunc; /* Function returning 1 when
					     job is canceled, NULL for not
					     supporting stop on cancel */
  void *iscanceleddata;                   /* User data for is-canceled
					     function, can be NULL */
} rastertops_doc_t;


/*
 * 'write_prolog()' - Writing the PostScript prolog for the file
 */

void
writeProlog(int 	   width,  /* I - width of the image in points */
            int 	   height, /* I - height of the image in points */
            rastertops_doc_t *doc) /* I - Document information */
{
  /* Document header... */
  fprintf(doc->outputfp, "%%!PS-Adobe-3.0\n");
  fprintf(doc->outputfp, "%%%%BoundingBox: %d %d %d %d\n", 0, 0, width, height);
  fprintf(doc->outputfp, "%%%%Creator: cups-filters\n");
  fprintf(doc->outputfp, "%%%%LanguageLevel: 2\n");
  fprintf(doc->outputfp, "%%%%DocumentData: Clean7Bit\n");
  fprintf(doc->outputfp, "%%%%Pages: (atend)\n");
  fprintf(doc->outputfp, "%%%%EndComments\n");
  fprintf(doc->outputfp, "%%%%BeginProlog\n");
  fprintf(doc->outputfp, "%%%%EndProlog\n");
}

/*
 *	'writeStartPage()' - Write the basic page setup
 */

void
writeStartPage(int  page,   /* I - Page to write */
	       int  width,  /* I - Page width in points */
               int  length, /* I - Page length in points */
               rastertops_doc_t *doc) /* I - Document information */
{
  if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_CONTROL,
				 "PAGE: %d %d", page, 1);
  fprintf(doc->outputfp, "%%%%Page: %d %d\n", page, page);
  fprintf(doc->outputfp, "%%%%BeginPageSetup\n");
  fprintf(doc->outputfp, "<< /PageSize[%d %d] >> setpagedevice\n", width,
	  length);
  fprintf(doc->outputfp, "%%%%EndPageSetup\n");
}

/*
 * 'find_bits()' - Finding the number of bits per color
 */

int                           /* O - Exit status */
find_bits(cups_cspace_t mode, /* I - Color space of data */
	  int           bpc)  /* I - Original bits per color of data */
{
  if (bpc == 1 &&
      (mode == CUPS_CSPACE_RGB ||
       mode == CUPS_CSPACE_ADOBERGB ||
       mode == CUPS_CSPACE_SRGB ||
       mode == CUPS_CSPACE_CMY))
    return 8;

  if (bpc == 16)
    return 8;

  return bpc;
}

/*
 * 'writeImage()' - Write the information regarding the image
 */

void			             /* O - Exit status */
writeImage(int           pagewidth,  /* I - width of page in points */
	   int           pageheight, /* I - height of page in points */
	   int           bpc,	     /* I - bits per color */
	   int           pixwidth,   /* I - width of image in pixels */
	   int           pixheight,  /* I - height of image in pixels */
	   cups_cspace_t mode,       /* I - color model of image */
     rastertops_doc_t *doc) /* I - Document information */
{
  fprintf(doc->outputfp, "gsave\n");

  switch (mode)
  {
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_CMY:
  case CUPS_CSPACE_SRGB:
  case CUPS_CSPACE_ADOBERGB:
    fprintf(doc->outputfp, "/DeviceRGB setcolorspace\n");
    break;

  case CUPS_CSPACE_CMYK:
    fprintf(doc->outputfp, "/DeviceCMYK setcolorspace\n");
    break;

  default:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_SW:
    fprintf(doc->outputfp, "/DeviceGray setcolorspace\n");
    break;
  }

  if (bpc == 16)
    fprintf(doc->outputfp, "/Input currentfile /FlateDecode filter def\n");
  fprintf(doc->outputfp, "%d %d scale\n", pagewidth, pageheight);
  fprintf(doc->outputfp, "<< \n"
	 "/ImageType 1\n"
	 "/Width %d\n"
	 "/Height %d\n"
	 "/BitsPerComponent %d\n", pixwidth, pixheight, find_bits(mode, bpc));

  switch (mode)
  {
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_CMY:
  case CUPS_CSPACE_SRGB:
  case CUPS_CSPACE_ADOBERGB:
    fprintf(doc->outputfp, "/Decode [0 1 0 1 0 1]\n");
    break;
	
  case CUPS_CSPACE_CMYK:
    fprintf(doc->outputfp, "/Decode [0 1 0 1 0 1 0 1]\n");
    break;
  
  case CUPS_CSPACE_SW:
  	fprintf(doc->outputfp, "/Decode [0 1]\n");
  	break;

  default:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
    fprintf(doc->outputfp, "/Decode [1 0]\n");
    break;
  }     
	
  if(bpc==16)
    fprintf(doc->outputfp,
	    "/DataSource {3 string 0 1 2 {1 index exch Input read {pop}"
	   "if Input read pop put } for} bind\n");
  else
    fprintf(doc->outputfp, "/DataSource currentfile /FlateDecode filter\n");
	
  fprintf(doc->outputfp, "/ImageMatrix [%d 0 0 %d 0 %d]\n", pixwidth,
	  -1*pixheight, pixheight);
  fprintf(doc->outputfp, ">> image\n");
}

/*
 * 'convert_pixels()'- Convert 1 bpc to 8 bpc
 */

void
convert_pixels(unsigned char *pixdata,      /* I - Original pixel data */
	       unsigned char *convertedpix, /* I - Buffer for converted data */
	       int 	     width)	    /* I - Width of data */
{
  int 		j, k = 0; /* Variables for iteration */
  unsigned int  mask;	  /* Variable for per byte iteration */
  unsigned char temp;	  /* temporary character */

  for(j = 0; j < width; ++j)
  {
    temp = *(pixdata + j);
    for (mask = 0x80; mask != 0; mask >>= 1)
    {
      if (mask!=0x80 && mask != 0x08)
      {
      	if (temp & mask)
	  convertedpix[k] = 0xFF;
	else
	  convertedpix[k] = 0;
	++k;
      }
    }
  }
}

/*
 *	'write_flate()' - Write the image data in flate encoded format
 */

int                                     /* O - Error value */
write_flate(cups_raster_t *ras,	        /* I - Image data */
	    cups_page_header2_t	header,	/* I - Bytes Per Line */
      rastertops_doc_t *doc) /* I - Document information */
{
  int            ret,                              /* Return value of this
						      function */
                 flush,                            /* Check the end of image
						      data */
                 curr_line=1,                      /* Maitining the working
						      line of pixels */
                 alloc,
                 flag = 0;
  unsigned       have;                             /* Bytes available in
						      output buffer */
  z_stream       strm;                             /* Structure required
						      by deflate */
  unsigned char  *pixdata,
                 *convertedpix;
  unsigned char  in[header.cupsBytesPerLine * 6],  /* Input data buffer */
                 out[header.cupsBytesPerLine * 6]; /* Output data buffer */
		
  /* allocate deflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit(&strm, -1);
  if (ret != Z_OK)
    return ret;

  if(header.cupsBitsPerColor == 1 &&
     (header.cupsColorSpace == CUPS_CSPACE_RGB ||
      header.cupsColorSpace == CUPS_CSPACE_ADOBERGB ||
      header.cupsColorSpace == CUPS_CSPACE_SRGB))
    flag = 1;

  /* compress until end of file */
  do {
    pixdata = malloc(header.cupsBytesPerLine);
    cupsRasterReadPixels(ras, pixdata, header.cupsBytesPerLine);
    if (flag)
    {
      convertedpix = malloc(header.cupsBytesPerLine * 6);
      convert_pixels(pixdata,convertedpix, header.cupsBytesPerLine);
      alloc = header.cupsBytesPerLine * 6;
    }
    else
    {
      convertedpix = malloc(header.cupsBytesPerLine);
      memcpy(convertedpix, pixdata, header.cupsBytesPerLine);
      alloc = header.cupsBytesPerLine;
    }

    if(curr_line == header.cupsHeight)
      flush = Z_FINISH;
    else
      flush = Z_NO_FLUSH;
    curr_line++;
    memcpy(in, convertedpix, alloc);
    strm.avail_in = alloc;
    strm.next_in = in;

    /* run deflate() on input until output buffer not full, finish
     * compression if all of source has been read in */
    do {
      strm.avail_out = alloc;
      strm.next_out = out;

      /* Run the deflate algorithm on the data */
      ret = deflate(&strm, flush);

      /* check whether state is not clobbered */
      assert(ret != Z_STREAM_ERROR);
      have = alloc - strm.avail_out;
      if (fwrite(out, 1, have, doc->outputfp) != have)
      {
	(void)deflateEnd(&strm);
	if (convertedpix != NULL)
	  free(convertedpix);
	return Z_ERRNO;
      }
    } while (strm.avail_out == 0);

    /* all input will be used */
    assert(strm.avail_in == 0);

    /* done when last data in file processed */
    free(pixdata);
    free(convertedpix);
  } while (flush != Z_FINISH);

  /* stream will be complete */
  assert(ret == Z_STREAM_END);

  /* clean up and return */
  (void)deflateEnd(&strm);
  return Z_OK;
}

/*
 *  Report a zlib or i/o error
 */

void
zerr(int ret, /* I - Return status of deflate */
    rastertops_doc_t *doc) /* I - Document information */
{
  switch (ret) {
  case Z_ERRNO:
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_ERROR,
		  "rastertops: zpipe - error in source data or output file");
    break;
  case Z_STREAM_ERROR:
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_ERROR,
		  "rastertops: zpipe - invalid compression level");
    break;
  case Z_DATA_ERROR:
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_ERROR,
		  "rastertops: zpipe - invalid or incomplete deflate data");
    break;
  case Z_MEM_ERROR:
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_ERROR,
		  "rastertops: zpipe - out of memory");
    break;
  case Z_VERSION_ERROR:
    if (doc->logfunc) doc->logfunc(doc->logdata, FILTER_LOGLEVEL_ERROR,
		  "rastertops: zpipe - zlib version mismatch!");
  }
}

/*
 * 'writeEndPage()' - Show the current page.
 */

void
writeEndPage(rastertops_doc_t *doc) /* I - Document information */
{
  fprintf(doc->outputfp, "\ngrestore\n");
  fprintf(doc->outputfp, "showpage\n");
  fprintf(doc->outputfp, "%%%%PageTrailer\n");
}

/*
 * 'writeTrailer()' - Write the PostScript trailer.
 */

void
writeTrailer(int  pages, /* I - Number of pages */
            rastertops_doc_t *doc) /* I - Document information */
{
  fprintf(doc->outputfp, "%%%%Trailer\n");
  fprintf(doc->outputfp, "%%%%Pages: %d\n", pages);
  fprintf(doc->outputfp, "%%%%EOF\n");
}

/*
 * 'rastertops()' - Filter function to convert PWG raster input
 *                  to PostScript
 */

int                         /* O - Error status */
rastertops(int inputfd,         /* I - File descriptor input stream */
       int outputfd,        /* I - File descriptor output stream */
       int inputseekable,   /* I - Is input stream seekable? (unused) */
       filter_data_t *data, /* I - Job and printer data */
       void *parameters)    /* I - Filter-specific parameters (unused) */
{
  rastertops_doc_t     doc;         /* Document information */
  cups_file_t	         *inputfp;		/* Print file */
  FILE                 *outputfp;   /* Output data stream */
  cups_raster_t	       *ras;        /* Raster stream for printing */
  cups_page_header2_t  header;      /* Page header from file */
  int           empty,         /* Is the input empty? */
                Page = 0,      /* variable for counting the pages */
                ret;           /* Return value of deflate compression */
  filter_logfunc_t     log = data->logfunc;
  void                 *ld = data->logdata;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void                 *icd = data->iscanceleddata;


  (void)inputseekable;
  (void)parameters;

  /*
  * Open the input data stream specified by the inputfd...
  */

  if ((inputfp = cupsFileOpenFd(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "rastertops: Unable to open input data stream.");
    }

    return (1);
  }

  /*
  * Open the output data stream specified by the outputfd...
  */

  if ((outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "rastertops: Unable to open output data stream.");
    }

    cupsFileClose(inputfp);

    return (1);
  }

  doc.inputfp = inputfp;
  doc.outputfp = outputfp;
  /* Logging function */
  doc.logfunc = log;
  doc.logdata = ld;
  /* Job-is-canceled function */
  doc.iscanceledfunc = iscanceled;
  doc.iscanceleddata = icd;
 
  ras = cupsRasterOpen(inputfd, CUPS_RASTER_READ);

  /*
  * Process pages as needed...
  */
  Page = 0;
  empty = 1;

  while (cupsRasterReadHeader2(ras, &header))
  {
    if (iscanceled && iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
                  "rastertops: Job canceled");
      break;
    }

   /*
    * Write the prolog for PS only once
    */

    if (empty)
    {
      empty = 0;
      writeProlog(header.PageSize[0], header.PageSize[1], &doc);
    }

   /*
    * Write a status message with the page number and number of copies.
    */

    Page ++;

    if (log) log(ld, FILTER_LOGLEVEL_INFO,
     "rastertops: Starting page %d.", Page);

   /*
    *	Write the starting of the page
    */
    writeStartPage(Page, header.PageSize[0], header.PageSize[1], &doc);

   /*
    *	write the information regarding the image
    */
    writeImage(header.PageSize[0], header.PageSize[1],
	       header.cupsBitsPerColor,
	       header.cupsWidth, header.cupsHeight,
	       header.cupsColorSpace, &doc);

    /* Write the compressed image data*/
    ret = write_flate(ras, header, &doc);
    if (ret != Z_OK)
      zerr(ret, &doc);
    writeEndPage(&doc);
  }

  if (empty)
  {
     if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
      "rastertops: Input is empty, outputting empty file.");
     cupsRasterClose(ras);
     return 0;
  }

  writeTrailer(Page, &doc);

  cupsRasterClose(ras);

  cupsFileClose(inputfp);
  
  fclose(outputfp);
  close(outputfd);

  return 0;
}

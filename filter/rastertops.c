/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief Convert PWG Raster to a PostScript file
 * @file rastertops.c
 * @author Pranjal Bhor <bhor.pranjal@gmail.com> (C) 2016
 * @author Neil 'Superna' Armstrong <superna9999@gmail.com> (C) 2010
 * @author Tobias Hoffmann <smilingthax@gmail.com> (c) 2012
 * @author Till Kamppeter <till.kamppeter@gmail.com> (c) 2014
 */

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <cups/cups.h>
#include <cups/raster.h>
#include <cupsfilters/colormanager.h>
#include <cupsfilters/image.h>
#include <assert.h>
#include <zlib.h>

/*
 * 'write_prolog()' - Writing the PostScript prolog for the file
 */

void
writeProlog(int 	   width,  /* I - width of the image in points */
            int 	   height) /* I - height of the image in points */
{
  /* Document header... */
  printf("%%!PS-Adobe-3.0\n");
  printf("%%%%BoundingBox: %d %d %d %d\n", 0, 0, width, height);
  printf("%%%%Creator: cups-filters\n");
  printf("%%%%LanguageLevel: 2\n");
  printf("%%%%DocumentData: Clean7Bit\n");
  printf("%%%%Pages: (atend)\n");
  printf("%%%%EndComments\n");
  printf("%%%%BeginProlog\n");
  printf("%%%%EndProlog\n");
}

/*
 *	'writeStartPage()' - Write the basic page setup
 */

void
writeStartPage(int  page,   /* I - Page to write */
	       int  width,  /* I - Page width in points */
               int  length) /* I - Page length in points */
{
  printf("%%%%Page: %d %d\n", page, page);
  printf("%%%%BeginPageSetup\n");
  printf("<< /PageSize[%d %d] >> setpagedevice\n", width, length);
  printf("%%%%EndPageSetup\n");
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
	   cups_cspace_t mode)       /* I - color model of image */
{
  printf("gsave\n");

  switch (mode)
  {
  case CUPS_CSPACE_RGB:
  case CUPS_CSPACE_CMY:
  case CUPS_CSPACE_SRGB:
  case CUPS_CSPACE_ADOBERGB:
    printf("/DeviceRGB setcolorspace\n");
    break;

  case CUPS_CSPACE_CMYK:
    printf("/DeviceCMYK setcolorspace\n");
    break;

  default:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
  case CUPS_CSPACE_SW:
    printf("/DeviceGray setcolorspace\n");
    break;
  }

  if (bpc == 16)
    printf("/Input currentfile /FlateDecode filter def\n");
  printf("%d %d scale\n", pagewidth, pageheight);
  printf("<< \n"
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
    printf("/Decode [0 1 0 1 0 1]\n");
    break;
	
  case CUPS_CSPACE_CMYK:
    printf("/Decode [0 1 0 1 0 1 0 1]\n");
    break;
  
  case CUPS_CSPACE_SW:
  	printf("/Decode [0 1]\n");
  	break;

  default:
  case CUPS_CSPACE_K:
  case CUPS_CSPACE_W:
    printf("/Decode [1 0]\n");
    break;
  }     
	
  if(bpc==16)
    printf("/DataSource {3 string 0 1 2 {1 index exch Input read {pop}"
	   "if Input read pop put } for} bind\n");
  else
    printf("/DataSource currentfile /FlateDecode filter\n");
	
  printf("/ImageMatrix [%d 0 0 %d 0 %d]\n", pixwidth, -1*pixheight, pixheight);
  printf(">> image\n");
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
	    cups_page_header2_t	header)	/* I - Bytes Per Line */
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
      if (fwrite(out, 1, have, stdout) != have)
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
zerr(int ret) /* I - Return status of deflate */
{
  fputs("zpipe: ", stderr);
  switch (ret) {
  case Z_ERRNO:
    fputs("error in source data or output file\n", stderr);
    break;
  case Z_STREAM_ERROR:
    fputs("invalid compression level\n", stderr);
    break;
  case Z_DATA_ERROR:
    fputs("invalid or incomplete deflate data\n", stderr);
    break;
  case Z_MEM_ERROR:
    fputs("out of memory\n", stderr);
    break;
  case Z_VERSION_ERROR:
    fputs("zlib version mismatch!\n", stderr);
  }
}

/*
 * 'writeEndPage()' - Show the current page.
 */

void
writeEndPage()
{
  printf("\ngrestore\n");
  printf("showpage\n");
  printf("%%%%PageTrailer\n");
}

/*
 * 'writeTrailer()' - Write the PostScript trailer.
 */

void
writeTrailer(int  pages) /* I - Number of pages */
{
  printf("%%%%Trailer\n");
  printf("%%%%Pages: %d\n", pages);
  printf("%%%%EOF\n");
}

/*
 * 'main()' - Main entry and processing of driver.
 */

int		   /* O - Exit status */
main(int  argc,	   /* I - Number of command-line arguments */
     char *argv[]) /* I - Command-line arguments */
{
  FILE                *input = NULL; /* File pointer to raster document */
  int                 fd,            /* File descriptor for raster document */
                      num_options,   /* Number of options */
                      count,         /* count for writing the postscript */
                      Canceled = 0,  /* variable for job cancellation */
                      Page = 0,      /* variable for counting the pages */
                      ret;           /* Return value of deflate compression */
  ppd_file_t          *ppd;          /* PPD file */
  cups_raster_t	      *ras;          /* Raster stream for printing */
  cups_page_header2_t header;        /* Page header from file */
  cups_option_t	      *options;	     /* Options */

 /*
  * Make sure status messages are not buffered...
  */
  setbuf(stderr, NULL);

 /*
  * Check command-line...
  */
  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n",
	    "rastertops");
    return (1);
  }

  num_options = cupsParseOptions(argv[5], 0, &options);

 /*
  * Open the PPD file...
  */
  ppd = ppdOpenFile(getenv("PPD"));

  if (ppd)
  {
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, num_options, options);
  }
  else
  {
    ppd_status_t status;  /* PPD error */
    int          linenum; /* Line number */

    fprintf(stderr, "DEBUG: The PPD file could not be opened.\n");
    status = ppdLastError(&linenum);
    fprintf(stderr, "DEBUG: %s on line %d.\n", ppdErrorString(status), linenum);
  }

 /*
  * Open the page stream...
  */
  if (argc == 7)
  {
    input = fopen(argv[6], "rb");
    if (input == NULL) fprintf(stderr, "Unable to open PWG Raster file");
  }
  else
    input = stdin;

 /*
  * Get fd from file
  */
  fd = fileno(input);

 /*
  * Transform
  */
  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

 /*
  * Register a signal handler to eject the current page if the
  * job is cancelled.
  */
  Canceled = 0;
  
  
 /*
  * Process pages as needed...
  */
  Page = 0;
  count = 0;

  while (cupsRasterReadHeader2(ras, &header))
  {
   /*
    * Write the prolog for PS only once
    */
    if (!count)
    {
      count++;
      writeProlog(header.PageSize[0], header.PageSize[1]);
    }

   /*
    * Write a status message with the page number and number of copies.
    */
    if (Canceled)
      break;

    Page ++;

    fprintf(stderr, "INFO: Starting page %d.\n", Page);

   /*
    *	Write the starting of the page
    */
    writeStartPage(Page, header.PageSize[0], header.PageSize[1]);

   /*
    *	write the information regarding the image
    */
    writeImage(header.PageSize[0], header.PageSize[1],
	       header.cupsBitsPerColor,
	       header.cupsWidth, header.cupsHeight,
	       header.cupsColorSpace);

    /* Write the compressed image data*/
    ret = write_flate(ras, header);
    if (ret != Z_OK)
      zerr(ret);
    writeEndPage();
  }
  writeTrailer(Page);

  return 0;
}

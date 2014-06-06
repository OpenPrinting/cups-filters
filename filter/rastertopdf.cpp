/**
 * This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief Convert PWG Raster to a PDF file
 * @file rastertopdf.cpp
 * @author Neil 'Superna' Armstrong <superna9999@gmail.com> (C) 2010
 * @author Tobias Hoffmann <smilingthax@gmail.com> (c) 2012
 * @author Till Kamppeter <till.kamppeter@gmail.com> (c) 2014
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits>
#include <signal.h>
#include <cups/cups.h>
#include <cups/raster.h>

#include <arpa/inet.h>   // ntohl

#include <vector>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>

#include <qpdf/Pl_Flate.hh>
#include <qpdf/Pl_Buffer.hh>

#define DEFAULT_PDF_UNIT 72   // 1/72 inch

#define PROGRAM "rastertopdf"

#define dprintf(format, ...) fprintf(stderr, "DEBUG2: (" PROGRAM ") " format, __VA_ARGS__)

#define iprintf(format, ...) fprintf(stderr, "INFO: (" PROGRAM ") " format, __VA_ARGS__)

void die(const char * str)
{
    fprintf(stderr, "ERROR: (" PROGRAM ") %s\n", str);
    exit(1);
}

//------------- PDF ---------------

struct pdf_info
{
    pdf_info() 
      : pagecount(0),
        width(0),height(0),
        line_bytes(0),
        bpp(0), bpc(0), color_space(CUPS_CSPACE_K),
        page_width(0),page_height(0)
    {
    }

    QPDF pdf;
    QPDFObjectHandle page;
    unsigned pagecount;
    unsigned width;
    unsigned height;
    unsigned line_bytes;
    unsigned bpp;
    unsigned bpc;
    cups_cspace_t color_space;
    PointerHolder<Buffer> page_data;
    double page_width,page_height;
};

int create_pdf_file(struct pdf_info * info)
{
    try {
        info->pdf.emptyPDF();
    } catch (...) {
        return 1;
    }
    return 0;
}

QPDFObjectHandle makeBox(double x1, double y1, double x2, double y2)
{
    QPDFObjectHandle ret=QPDFObjectHandle::newArray();
    ret.appendItem(QPDFObjectHandle::newReal(x1));
    ret.appendItem(QPDFObjectHandle::newReal(y1));
    ret.appendItem(QPDFObjectHandle::newReal(x2));
    ret.appendItem(QPDFObjectHandle::newReal(y2));
    return ret;
}

#define PRE_COMPRESS

QPDFObjectHandle makeImage(QPDF &pdf, PointerHolder<Buffer> page_data, unsigned width, unsigned height, cups_cspace_t cs, unsigned bpc)
{
    QPDFObjectHandle ret = QPDFObjectHandle::newStream(&pdf);

    std::map<std::string,QPDFObjectHandle> dict;

    dict["/Type"]=QPDFObjectHandle::newName("/XObject");
    dict["/Subtype"]=QPDFObjectHandle::newName("/Image");
    dict["/Width"]=QPDFObjectHandle::newInteger(width);
    dict["/Height"]=QPDFObjectHandle::newInteger(height);
    dict["/BitsPerComponent"]=QPDFObjectHandle::newInteger(bpc);

    if (cs==CUPS_CSPACE_K) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
    } else if (cs==CUPS_CSPACE_RGB) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
    } else if (cs==CUPS_CSPACE_CMYK) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
    } else if (cs==CUPS_CSPACE_SW) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
    } else if (cs==CUPS_CSPACE_SRGB) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
    } else if (cs==CUPS_CSPACE_ADOBERGB) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
    } else {
        return QPDFObjectHandle();
    }

    ret.replaceDict(QPDFObjectHandle::newDictionary(dict));

#ifdef PRE_COMPRESS
    // we deliver already compressed content (instead of letting QPDFWriter do it), to avoid using excessive memory
    Pl_Buffer psink("psink");
    Pl_Flate pflate("pflate",&psink,Pl_Flate::a_deflate);
    
    pflate.write(page_data->getBuffer(),page_data->getSize());
    pflate.finish();

    ret.replaceStreamData(PointerHolder<Buffer>(psink.getBuffer()),
                          QPDFObjectHandle::newName("/FlateDecode"),QPDFObjectHandle::newNull());
#else
    ret.replaceStreamData(page_data,QPDFObjectHandle::newNull(),QPDFObjectHandle::newNull());
#endif

    return ret;
}

void finish_page(struct pdf_info * info)
{
    //Finish previous Page
    if(!info->page_data.getPointer())
        return;

    QPDFObjectHandle image = makeImage(info->pdf, info->page_data, info->width, info->height, info->color_space, info->bpc);
    if(!image.isInitialized()) die("Unable to load image data");

    // add it
    info->page.getKey("/Resources").getKey("/XObject").replaceKey("/I",image);

    // draw it
    std::string content;
    content.append(QUtil::double_to_string(info->page_width) + " 0 0 " + 
                   QUtil::double_to_string(info->page_height) + " 0 0 cm\n");
    content.append("/I Do\n");
    info->page.getKey("/Contents").replaceStreamData(content,QPDFObjectHandle::newNull(),QPDFObjectHandle::newNull());

    // bookkeeping
    info->page_data = PointerHolder<Buffer>();
}

int add_pdf_page(struct pdf_info * info, int pagen, unsigned width,
		 unsigned height, int bpp, int bpc, int bpl,
		 cups_cspace_t color_space, unsigned xdpi, unsigned ydpi)
{
    try {
        finish_page(info); // any active

        info->width = width;
        info->height = height;
        info->line_bytes = bpl;
        info->bpp = bpp;
        info->bpc = bpc;
	info->color_space = color_space;

        if (info->height > (std::numeric_limits<unsigned>::max() / info->line_bytes)) {
            die("Page too big");
        }
        info->page_data = PointerHolder<Buffer>(new Buffer(info->line_bytes*info->height));

        QPDFObjectHandle page = QPDFObjectHandle::parse(
            "<<"
            "  /Type /Page"
            "  /Resources <<"
            "    /XObject << >> "
            "  >>"
            "  /MediaBox null "
            "  /Contents null "
            ">>");
        page.replaceKey("/Contents",QPDFObjectHandle::newStream(&info->pdf)); // data will be provided later
    
        // Convert to pdf units
        info->page_width=((double)info->width/xdpi)*DEFAULT_PDF_UNIT;
        info->page_height=((double)info->height/ydpi)*DEFAULT_PDF_UNIT;
        page.replaceKey("/MediaBox",makeBox(0,0,info->page_width,info->page_height));
    
        info->page = info->pdf.makeIndirectObject(page); // we want to keep a reference
        info->pdf.addPage(info->page, false);
    } catch (std::bad_alloc &ex) {
        die("Unable to allocate page data");
    } catch (...) {
        return 1;
    }

    return 0;
}

int close_pdf_file(struct pdf_info * info)
{
    try {
        finish_page(info); // any active

        QPDFWriter output(info->pdf,NULL);
        output.write();
    } catch (...) {
        return 1;
    }

    return 0;
}

void pdf_set_line(struct pdf_info * info, unsigned line_n, unsigned char *line)
{
    //dprintf("pdf_set_line(%d)\n", line_n);

    if(line_n > info->height)
    {
        dprintf("Bad line %d\n", line_n);
        return;
    }
  
    memcpy((info->page_data->getBuffer()+(line_n*info->line_bytes)), line, info->line_bytes);
}

int convert_raster(cups_raster_t *ras, unsigned width, unsigned height,
		   int bpp, int bpl, struct pdf_info * info)
{
    // We should be at raster start
    int i;
    unsigned cur_line = 0;
    unsigned char *PixelBuffer, *ptr;

    PixelBuffer = (unsigned char *)malloc(bpl);

    do
    {
        // Read raster data...
        cupsRasterReadPixels(ras, PixelBuffer, bpl);

	if (info->color_space == CUPS_CSPACE_K)
	{
	  // Invert black to grayscale...
	  for (i = bpl, ptr = PixelBuffer; i > 0; i --, ptr ++)
	    *ptr = ~*ptr;
	}

#if !ARCH_IS_BIG_ENDIAN
	if (info->bpc == 16)
	{
	  // Swap byte pairs for endianess (cupsRasterReadPixels() switches
	  // from Big Endian back to the system's Endian)
	  for (i = bpl, ptr = PixelBuffer; i > 0; i -= 2, ptr += 2)
	  {
	    unsigned char swap = *ptr;
	    *ptr = *(ptr + 1);
	    *(ptr + 1) = swap;
	  }
	}
#endif /* !ARCH_IS_BIG_ENDIAN */

        // write lines
	pdf_set_line(info, cur_line, PixelBuffer);

	++cur_line;
    }
    while(cur_line < height);

    free(PixelBuffer);

    return 0;
}

int main(int argc, char **argv)
{
    int fd, Page;
    struct pdf_info pdf;
    FILE * input = NULL;
    cups_raster_t	*ras;		/* Raster stream for printing */
    cups_page_header2_t	header;		/* Page header from file */
    ppd_file_t		*ppd;		/* PPD file */
    int			num_options;	/* Number of options */
    cups_option_t	*options;	/* Options */

    // Make sure status messages are not buffered...
    setbuf(stderr, NULL);

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "Usage: %s <job> <user> <job name> <copies> <option> [file]\n", argv[0]);
        return 1;
    }

    num_options = cupsParseOptions(argv[5], 0, &options);

    // Open the PPD file...
    ppd = ppdOpenFile(getenv("PPD"));

    if (ppd)
    {
      ppdMarkDefaults(ppd);
      cupsMarkOptions(ppd, num_options, options);
    }
    else
    {
      ppd_status_t	status;		/* PPD error */
      int		linenum;	/* Line number */

      fputs("DEBUG: The PPD file could not be opened.\n", stderr);

      status = ppdLastError(&linenum);
      
      fprintf(stderr, "DEBUG: %s on line %d.\n", ppdErrorString(status), linenum);
    }

    // Open the page stream...
    if (argc == 7)
    {
        input = fopen(argv[6], "rb");
        if (input == NULL) die("Unable to open PWG Raster file");
    }
    else
        input = stdin;

    // Get fd from file
    fd = fileno(input);

    // Transform
    ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

    // Process pages as needed...
    Page = 0;

    // Create PDF file
    if (create_pdf_file(&pdf) != 0)
      die("Unable to create PDF file");

    while (cupsRasterReadHeader2(ras, &header))
    {
      // Write a status message with the page number
      Page ++;
      fprintf(stderr, "INFO: Starting page %d.\n", Page);

      // Add a new page to PDF file
      if (add_pdf_page(&pdf, Page, header.cupsWidth, header.cupsHeight,
		       header.cupsBitsPerPixel, header.cupsBitsPerColor,
		       header.cupsBytesPerLine,
		       header.cupsColorSpace, header.HWResolution[0],
		       header.HWResolution[1]) != 0)
	die("Unable to start new PDF page");

      // Write the bit map into the PDF file
      if (convert_raster(ras, header.cupsWidth, header.cupsHeight,
			 header.cupsBitsPerPixel, header.cupsBytesPerLine, 
			 &pdf) != 0)
	die("Failed to convert page bitmap");
    }

    close_pdf_file(&pdf); // will output to stdout

    cupsFreeOptions(num_options, options);

    cupsRasterClose(ras);

    if (fd != 0)
      close(fd);

    return (Page == 0);
}

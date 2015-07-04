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
 * @brief Decode URF  to a PDF file
 * @file urf_decode.cpp
 * @author Neil 'Superna' Armstrong <superna9999@gmail.com> (C) 2010
 * @author Tobias Hoffmann <smilingthax@gmail.com> (c) 2012
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
#include <errno.h>

#include <arpa/inet.h>   // ntohl

#include <vector>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>

#include <qpdf/Pl_Flate.hh>
#include <qpdf/Pl_Buffer.hh>

#include "unirast.h"

#define DEFAULT_PDF_UNIT 72   // 1/72 inch

#define PROGRAM "urftopdf"

#ifdef URF_DEBUG
#define dprintf(format, ...) fprintf(stderr, "DEBUG: (" PROGRAM ") " format, __VA_ARGS__)
#else
#define dprintf(format, ...)
#endif

#define iprintf(format, ...) fprintf(stderr, "INFO: (" PROGRAM ") " format, __VA_ARGS__)

void die(const char * str)
{
    fprintf(stderr, "CRIT: (" PROGRAM ") die(%s) [%s]\n", str, strerror(errno));
    exit(1);
}

//------------- PDF ---------------

struct pdf_info
{
    pdf_info() 
      : pagecount(0),
        width(0),height(0),
        pixel_bytes(0),line_bytes(0),
        bpp(0),
        page_width(0),page_height(0)
    {
    }

    QPDF pdf;
    QPDFObjectHandle page;
    unsigned pagecount;
    unsigned width;
    unsigned height;
    unsigned pixel_bytes;
    unsigned line_bytes;
    unsigned bpp;
    PointerHolder<Buffer> page_data;
    double page_width,page_height;
};

int create_pdf_file(struct pdf_info * info, unsigned pagecount)
{
    try {
        info->pdf.emptyPDF();
    } catch (...) {
        return 1;
    }

    info->pagecount = pagecount;

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

enum ColorSpace {
    DEVICE_GRAY,
    DEVICE_RGB,
    DEVICE_CMYK
};

#define PRE_COMPRESS
/* or temporarily store images?
    if(cupsTempFile2(tempfile_name, 255) == NULL) die("Unable to create a temporary pdf file");
    iprintf("Created temporary file '%s'\n", tempfile_name);
*/

QPDFObjectHandle makeImage(QPDF &pdf, PointerHolder<Buffer> page_data, unsigned width, unsigned height, ColorSpace cs, unsigned bpc)
{
    QPDFObjectHandle ret = QPDFObjectHandle::newStream(&pdf);

    std::map<std::string,QPDFObjectHandle> dict;

    dict["/Type"]=QPDFObjectHandle::newName("/XObject");
    dict["/Subtype"]=QPDFObjectHandle::newName("/Image");
    dict["/Width"]=QPDFObjectHandle::newInteger(width);
    dict["/Height"]=QPDFObjectHandle::newInteger(height);
    dict["/BitsPerComponent"]=QPDFObjectHandle::newInteger(bpc);

    if (cs==DEVICE_GRAY) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
    } else if (cs==DEVICE_RGB) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
    } else if (cs==DEVICE_CMYK) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
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

//    /Filter /FlateDecode
//    /DecodeParms  [<</Predictor 1 /Colors 1[3] /BitsPerComponent $bits /Columns $x>>]  ??
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

    QPDFObjectHandle image = makeImage(info->pdf, info->page_data, info->width, info->height, DEVICE_RGB, 8);
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

int add_pdf_page(struct pdf_info * info, int pagen, unsigned width, unsigned height, int bpp, unsigned dpi)
{
    try {
        finish_page(info); // any active

        info->width = width;
        info->height = height;
        info->pixel_bytes = bpp/8;
        info->line_bytes = (width*info->pixel_bytes);
        info->bpp = bpp;
    
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
        info->page_width=((double)info->width/dpi)*DEFAULT_PDF_UNIT;
        info->page_height=((double)info->height/dpi)*DEFAULT_PDF_UNIT;
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

void pdf_set_line(struct pdf_info * info, unsigned line_n, uint8_t line[])
{
    dprintf("pdf_set_line(%d)\n", line_n);

    if(line_n > info->height)
    {
        dprintf("Bad line %d\n", line_n);
        return;
    }
  
    memcpy((info->page_data->getBuffer()+(line_n*info->line_bytes)), line, info->line_bytes);
}

// Data are in network endianness
struct urf_file_header {
    char unirast[8];
    uint32_t page_count;
} __attribute__((__packed__));

struct urf_page_header {
    uint8_t bpp;
    uint8_t colorspace;
    uint8_t duplex;
    uint8_t quality;
    uint32_t unknown0;
    uint32_t unknown1;
    uint32_t width;
    uint32_t height;
    uint32_t dot_per_inch;
    uint32_t unknown2;
    uint32_t unknown3;
} __attribute__((__packed__));

int decode_raster(int fd, unsigned width, unsigned height, int bpp, struct pdf_info * info)
{
    // We should be at raster start
    int i, j;
    unsigned cur_line = 0;
    unsigned pos = 0;
    uint8_t line_repeat_byte = 0;
    unsigned line_repeat = 0;
    int8_t packbit_code = 0;
    int pixel_size = (bpp/8);
    std::vector<uint8_t> pixel_container;
    std::vector<uint8_t> line_container;

    if (width > (std::numeric_limits<unsigned>::max() / pixel_size)) {
        die("Line too big");
    }
    try {
        pixel_container.resize(pixel_size);
        line_container.resize(pixel_size*width);
    } catch (...) {
        die("Unable to allocate temporary storage");
    }

    do
    {
        if(read(fd, &line_repeat_byte, 1) < 1)
        {
            dprintf("l%06d : line_repeat EOF at %lu\n", cur_line, lseek(fd, 0, SEEK_CUR));
            return 1;
        }

        line_repeat = (unsigned)line_repeat_byte + 1;

        dprintf("l%06d : next actions for %d lines\n", cur_line, line_repeat);

        // Start of line
        pos = 0;

        do
        {
            if(read(fd, &packbit_code, 1) < 1)
            {
                dprintf("p%06dl%06d : packbit_code EOF at %lu\n", pos, cur_line, lseek(fd, 0, SEEK_CUR));
                return 1;
            }

            dprintf("p%06dl%06d: Raster code %02X='%d'.\n", pos, cur_line, (uint8_t)packbit_code, packbit_code);

            if(packbit_code == -128)
            {
                dprintf("\tp%06dl%06d : blank rest of line.\n", pos, cur_line);
                memset((&line_container[pos*pixel_size]), 0xFF, (pixel_size*(width-pos)));
                pos = width;
                break;
            }
            else if(packbit_code >= 0 && packbit_code <= 127)
            {
                int n = (packbit_code+1);

                //Read pixel
                if(read(fd, &pixel_container[0], pixel_size) < pixel_size)
                {
                    dprintf("p%06dl%06d : pixel repeat EOF at %lu\n", pos, cur_line, lseek(fd, 0, SEEK_CUR));
                    return 1;
                }

                dprintf("\tp%06dl%06d : Repeat pixel '", pos, cur_line);
                for(j = 0 ; j < pixel_size ; ++j)
                    dprintf("%02X ", pixel_container[j]);
                dprintf("' for %d times.\n", n);

                for(i = 0 ; i < n ; ++i)
                {
                    //for(j = pixel_size-1 ; j >= 0 ; --j)
                    for(j = 0 ; j < pixel_size ; ++j)
                        line_container[pixel_size*pos + j] = pixel_container[j];
                    ++pos;
                    if(pos >= width)
                        break;
                }

                if(i < n && pos >= width)
                {
                    dprintf("\tp%06dl%06d : Forced end of line for pixel repeat.\n", pos, cur_line);
                }
                
                if(pos >= width)
                    break;
            }
            else if(packbit_code > -128 && packbit_code < 0)
            {
                int n = (-(int)packbit_code)+1;

                dprintf("\tp%06dl%06d : Copy %d verbatim pixels.\n", pos, cur_line, n);

                for(i = 0 ; i < n ; ++i)
                {
                    if(read(fd, &pixel_container[0], pixel_size) < pixel_size)
                    {
                        dprintf("p%06dl%06d : literal_pixel EOF at %lu\n", pos, cur_line, lseek(fd, 0, SEEK_CUR));
                        return 1;
                    }
                    //Invert pixels, should be programmable
                    for(j = 0 ; j < pixel_size ; ++j)
                        line_container[pixel_size*pos + j] = pixel_container[j];
                    ++pos;
                    if(pos >= width)
                        break;
                }

                if(i < n && pos >= width)
                {
                    dprintf("\tp%06dl%06d : Forced end of line for pixel copy.\n", pos, cur_line);
                }
                
                if(pos >= width)
                    break;
            }
        }
        while(pos < width);

        dprintf("\tl%06d : End Of line, drawing %d times.\n", cur_line, line_repeat);

        // write lines
        for(i = 0 ; i < (int)line_repeat ; ++i)
        {
            pdf_set_line(info, cur_line, &line_container[0]);
            ++cur_line;
        }
    }
    while(cur_line < height);

    return 0;
}

int main(int argc, char **argv)
{
    int fd, page;
    struct urf_file_header head, head_orig;
    struct urf_page_header page_header, page_header_orig;
    struct pdf_info pdf;

    FILE * input = NULL;

    if(argc < 6)
    {
        fprintf(stderr, "Usage: %s <job> <user> <job name> <copies> <option> [file]\n", argv[0]);
        return 1;
    }

    if(argc > 6)
    {
        input = fopen(argv[6], "rb");
        if(input == NULL) die("Unable to open unirast file");
    }
    else
        input = stdin;

    // Get fd from file
    fd = fileno(input);

    if(read(fd, &head_orig, sizeof(head)) == -1) die("Unable to read file header");

    //Transform
    memcpy(head.unirast, head_orig.unirast, sizeof(head.unirast));
    head.page_count = ntohl(head_orig.page_count);

    if(head.unirast[7])
        head.unirast[7] = 0;

    if(strncmp(head.unirast, "UNIRAST", 7) != 0) die("Bad File Header");

    iprintf("%s file, with %d page(s).\n", head.unirast, head.page_count);

    if(create_pdf_file(&pdf, head.page_count) != 0) die("Unable to create PDF file");

    for(page = 0 ; page < (int)head.page_count ; ++page)
    {
        if(read(fd, &page_header_orig, sizeof(page_header_orig)) == -1) die("Unable to read page header");

        //Transform
        page_header.bpp = page_header_orig.bpp;
        page_header.colorspace = page_header_orig.colorspace;
        page_header.duplex = page_header_orig.duplex;
        page_header.quality = page_header_orig.quality;
        page_header.unknown0 = 0;
        page_header.unknown1 = 0;
        page_header.width = ntohl(page_header_orig.width);
        page_header.height = ntohl(page_header_orig.height);
        page_header.dot_per_inch = ntohl(page_header_orig.dot_per_inch);
        page_header.unknown2 = 0;
        page_header.unknown3 = 0;

        iprintf("Page %d :\n", page);
        iprintf("Bits Per Pixel : %d\n", page_header.bpp);
        iprintf("Colorspace : %d\n", page_header.colorspace);
        iprintf("Duplex Mode : %d\n", page_header.duplex);
        iprintf("Quality : %d\n", page_header.quality);
        iprintf("Size : %dx%d pixels\n", page_header.width, page_header.height);
        iprintf("Dots per Inches : %d\n", page_header.dot_per_inch);

        if(page_header.colorspace != UNIRAST_COLOR_SPACE_SRGB_24BIT_1)
        {
            die("Invalid ColorSpace, only RGB 24BIT type 1 is supported");
        }
        
        if(page_header.bpp != UNIRAST_BPP_24BIT)
        {
            die("Invalid Bit Per Pixel value, only 24bit is supported");
        }

        if(add_pdf_page(&pdf, page, page_header.width, page_header.height, page_header.bpp, page_header.dot_per_inch) != 0) die("Unable to create PDF file");

        if(decode_raster(fd, page_header.width, page_header.height, page_header.bpp, &pdf) != 0)
            die("Failed to decode Page");
    }

    close_pdf_file(&pdf); // will output to stdout

    return 0;
}

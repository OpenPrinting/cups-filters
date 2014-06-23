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

#include <config.h>
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
#include <cupsfilters/colord.h>
//#include <cupsfilters/image.h>

#include <arpa/inet.h>   // ntohl

#include <vector>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>

#include <qpdf/Pl_Flate.hh>
#include <qpdf/Pl_Buffer.hh>

#ifdef USE_LCMS1
#include <lcms.h>
#define cmsColorSpaceSignature icColorSpaceSignature
#define cmsSetLogErrorHandler cmsSetErrorHandler
#define cmsSigXYZData icSigXYZData
#define cmsSigLuvData icSigLuvData
#define cmsSigLabData icSigLabData
#define cmsSigYCbCrData icSigYCbCrData
#define cmsSigYxyData icSigYxyData
#define cmsSigRgbData icSigRgbData
#define cmsSigHsvData icSigHsvData
#define cmsSigHlsData icSigHlsData
#define cmsSigCmyData icSigCmyData
#define cmsSig3colorData icSig3colorData
#define cmsSigGrayData icSigGrayData
#define cmsSigCmykData icSigCmykData
#define cmsSig4colorData icSig4colorData
#define cmsSig2colorData icSig2colorData
#define cmsSig5colorData icSig5colorData
#define cmsSig6colorData icSig6colorData
#define cmsSig7colorData icSig7colorData
#define cmsSig8colorData icSig8colorData
#define cmsSig9colorData icSig9colorData
#define cmsSig10colorData icSig10colorData
#define cmsSig11colorData icSig11colorData
#define cmsSig12colorData icSig12colorData
#define cmsSig13colorData icSig13colorData
#define cmsSig14colorData icSig14colorData
#define cmsSig15colorData icSig15colorData
#else
#include <lcms2.h>
#endif

#define DEFAULT_PDF_UNIT 72   // 1/72 inch

#define PROGRAM "rastertopdf"

#define dprintf(format, ...) fprintf(stderr, "DEBUG2: (" PROGRAM ") " format, __VA_ARGS__)

#define iprintf(format, ...) fprintf(stderr, "INFO: (" PROGRAM ") " format, __VA_ARGS__)

cmsHPROFILE         inputColorProfile = NULL;
cmsHPROFILE         outputColorProfile = NULL;
cmsHTRANSFORM       colorTransform = NULL;
int                 renderingIntent = INTENT_PERCEPTUAL;
int                 device_inhibited = 0;
bool                cm_calibrate = false;

#ifdef USE_LCMS1
static int lcmsErrorHandler(int ErrorCode, const char *ErrorText)
{
  fprintf(stderr, "ERROR: %s\n",ErrorText);
  return 1;
}
#else
static void lcmsErrorHandler(cmsContext contextId, cmsUInt32Number ErrorCode,
   const char *ErrorText)
{
  fprintf(stderr, "ERROR: %s\n",ErrorText);
}
#endif

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

    /* TODO Adjust for color calibration */
    if (!device_inhibited) {
        switch (cs) {       
            case CUPS_CSPACE_CIELab:
            case CUPS_CSPACE_ICC1:
            case CUPS_CSPACE_ICC2:
            case CUPS_CSPACE_ICC3:
            case CUPS_CSPACE_ICC4:
            case CUPS_CSPACE_ICC5:
            case CUPS_CSPACE_ICC6:
            case CUPS_CSPACE_ICC7:
            case CUPS_CSPACE_ICC8:
            case CUPS_CSPACE_ICC9:
            case CUPS_CSPACE_ICCA:
            case CUPS_CSPACE_ICCB:
            case CUPS_CSPACE_ICCC:
            case CUPS_CSPACE_ICCD:
            case CUPS_CSPACE_ICCE:
            case CUPS_CSPACE_ICCF:
            case CUPS_CSPACE_CIEXYZ:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/CalRGB");
                break;
            case CUPS_CSPACE_K:
            case CUPS_CSPACE_W:
            case CUPS_CSPACE_SW:
            case CUPS_CSPACE_WHITE:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
                break;
            case CUPS_CSPACE_CMYK:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
                break;
            case CUPS_CSPACE_RGB:
            case CUPS_CSPACE_SRGB:
            case CUPS_CSPACE_ADOBERGB:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
                break;
            default: 
                return QPDFObjectHandle();
        }
    } else if (device_inhibited) {
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
    } else 
        return QPDFObjectHandle();

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
    int i, pixels;
    unsigned cur_line = 0;
    unsigned char *PixelBuffer, *ptr;
    unsigned char * TransformBuffer = NULL;

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
        if (colorTransform != NULL && !device_inhibited) {
          // If a profile was specified, we apply the transformation
          TransformBuffer = (unsigned char *)malloc(bpl);
          pixels = bpl / (info->bpp / info->bpc);

          cmsDoTransform(colorTransform, PixelBuffer, 
                         TransformBuffer, pixels); 
          pdf_set_line(info, cur_line, TransformBuffer);          
        } else 
  	  pdf_set_line(info, cur_line, PixelBuffer);

	++cur_line;
    }
    while(cur_line < height);

    free(PixelBuffer);
    if (TransformBuffer != NULL)
      free(TransformBuffer);

    return 0;
}

static unsigned int getCMSColorSpaceType(cmsColorSpaceSignature cs)
{
    switch (cs) {
    case cmsSigXYZData:
      return PT_XYZ;
      break;
    case cmsSigLabData:
      return PT_Lab;
      break;
    case cmsSigLuvData:
      return PT_YUV;
      break;
    case cmsSigYCbCrData:
      return PT_YCbCr;
      break;
    case cmsSigYxyData:
      return PT_Yxy;
      break;
    case cmsSigRgbData:
      return PT_RGB;
      break;
    case cmsSigGrayData:
      return PT_GRAY;
      break;
    case cmsSigHsvData:
      return PT_HSV;
      break;
    case cmsSigHlsData:
      return PT_HLS;
      break;
    case cmsSigCmykData:
      return PT_CMYK;
      break;
    case cmsSigCmyData:
      return PT_CMY;
      break;
    case cmsSig2colorData:
    case cmsSig3colorData:
    case cmsSig4colorData:
    case cmsSig5colorData:
    case cmsSig6colorData:
    case cmsSig7colorData:
    case cmsSig8colorData:
    case cmsSig9colorData:
    case cmsSig10colorData:
    case cmsSig11colorData:
    case cmsSig12colorData:
    case cmsSig13colorData:
    case cmsSig14colorData:
    case cmsSig15colorData:
    default:
      break;
    }
    return PT_RGB;
}

int setProfile(const char * path) 
{
    if (path != NULL) 
      inputColorProfile = cmsOpenProfileFromFile(path,"r");
 
    if (inputColorProfile != NULL)
      // test using sRGB Profile
      outputColorProfile = cmsCreate_sRGBProfile();

    if (inputColorProfile != NULL && outputColorProfile != NULL)
      return 0;
    else
      return 1;
}

/* Obtain a source profile using color qualifiers */
int setCupsColorProfile(ppd_file_t* ppd, const char* cupsRenderingIntent, 
                    const char * mediaType, cups_cspace_t cs, unsigned xdpi, unsigned ydpi)
{
    std::string MediaType;
    std::string resolution;
    std::string colorModel;
   
    std::string path;
    ppd_attr_t *attr;

    if (ppd == NULL) {
        return 1;
    }      

    if (cupsRenderingIntent != NULL) {
        if (strcasecmp(cupsRenderingIntent,"PERCEPTUAL") != 0) {
  	    renderingIntent = INTENT_PERCEPTUAL;
        } else if (strcasecmp(cupsRenderingIntent,"RELATIVE_COLORIMETRIC") != 0) {
  	  renderingIntent = INTENT_RELATIVE_COLORIMETRIC;
        } else if (strcasecmp(cupsRenderingIntent,"SATURATION") != 0) {
	  renderingIntent = INTENT_SATURATION;
        } else if (strcasecmp(cupsRenderingIntent,"ABSOLUTE_COLORIMETRIC") != 0) {
	  renderingIntent = INTENT_ABSOLUTE_COLORIMETRIC;
        }
      }

    // ColorModel
    switch (cs) {
        case CUPS_CSPACE_RGB:
            colorModel = "Rgb";
            break;
        case CUPS_CSPACE_SRGB:
            colorModel = "Srgb";
            break;
        case CUPS_CSPACE_ADOBERGB:
            colorModel = "AdobeRgb";
            break;
        case CUPS_CSPACE_K:
            colorModel = "Sgray";
            break;
        case CUPS_CSPACE_CMYK:
            colorModel = "Cmyk";
            break;
        default:
            colorModel = "";
            break;
     }

    // Resolution
    resolution = xdpi + "x" + ydpi;

    // MediaType
    if (mediaType != NULL)
      MediaType = mediaType;

    for (attr = ppdFindAttr(ppd,"cupsICCProfile",NULL);attr != NULL;
       attr = ppdFindNextAttr(ppd,"cupsICCProfile",NULL)) {
	// check color model
	char buf[PPD_MAX_NAME];
	char *p, *r;

	strncpy(buf,attr->spec,sizeof(buf));
	if ((p = strchr(buf,'.')) != NULL) {
	    *p = '\0';
	}
	if (!colorModel.empty() && buf[0] != '\0'
	    && strcasecmp(buf,colorModel.c_str()) != 0) continue;
	if (p == NULL) {
	    break;
	} else {
	    p++;
	    if ((r = strchr(p,'.')) != 0) {
		*r = '\0';
	    }
	}
	if (!resolution.empty() && p[0] != '\0'
	    && strcasecmp(p,resolution.c_str()) != 0) continue;
	if (r == NULL) {
	    break;
	} else {
	    r++;
	    if ((p = strchr(r,'.')) != 0) {
		*p = '\0';
	    }
	}
	if (!MediaType.empty() || r[0] == '\0'
	    || strcasecmp(r,MediaType.c_str()) == 0) break;
        }
    if (attr != NULL) {
	// matched
	path = "";
	if (attr->value[0] != '/') {
	    path = path + CUPS_DATADIR + "/profiles/";
	}
	path = path + CUPS_DATADIR + attr->value;
    }

    return setProfile(path.c_str());
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
    const char		*val;		/* Option value */
    char                tmpstr[1024];

    // Make sure status messages are not buffered...
    setbuf(stderr, NULL);

    cmsSetLogErrorHandler(lcmsErrorHandler);

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "Usage: %s <job> <user> <job name> <copies> <option> [file]\n", argv[0]);
        return 1;
    }

    num_options = cupsParseOptions(argv[5], 0, &options);  

    snprintf (tmpstr, sizeof(tmpstr), "cups-%s", getenv("PRINTER"));
    device_inhibited = colord_get_inhibit_for_device_id (tmpstr);

    /* support the "cm-calibration" option */ 
    if (cupsGetOption("cm-calibration", num_options, options) != NULL) {
      cm_calibrate = true;
      device_inhibited = 1;
    }

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

    fprintf(stderr, "DEBUG: Color Management: %s\n", cm_calibrate ?
           "Calibration Mode/Enabled" : "Calibration Mode/Off");

    while (cupsRasterReadHeader2(ras, &header))
    {
      // Write a status message with the page number
      Page ++;
      fprintf(stderr, "INFO: Starting page %d.\n", Page);

      // Set profile from PPD and raster header
      if (!device_inhibited) {
          // Set user profile from command-line
          if ((val = cupsGetOption("profile", num_options, options)) != NULL) {
            setProfile(val);
          } /* else if (setCupsColorProfile(ppd, header.cupsRenderingIntent, 
                              header.MediaType, header.cupsColorSpace, 
                              header.HWResolution[0], header.HWResolution[1]) != 0)  {            
          } */

          // TESTING create color transformation
          if (inputColorProfile != NULL && outputColorProfile != NULL) {
             /* TODO apply conversion by other cases (color space) */
             unsigned int bytes = header.cupsBitsPerColor/8;
             unsigned int dcst = getCMSColorSpaceType(cmsGetColorSpace(inputColorProfile));
             colorTransform = cmsCreateTransform(inputColorProfile,
                COLORSPACE_SH(dcst) |CHANNELS_SH(header.cupsNumColors) | BYTES_SH(bytes),
                outputColorProfile,
                COLORSPACE_SH(PT_RGB) |
                CHANNELS_SH(3) | BYTES_SH(1),
                renderingIntent,0);
          }
      }
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

    if (inputColorProfile != NULL) {
      cmsCloseProfile(inputColorProfile);
    }
    if (outputColorProfile != NULL && outputColorProfile != inputColorProfile) {
      cmsCloseProfile(outputColorProfile);
    }
    if (colorTransform != NULL) {
      cmsDeleteTransform(colorTransform);
    }

    cupsFreeOptions(num_options, options);

    cupsRasterClose(ras);

    if (fd != 0)
      close(fd);

    return (Page == 0);
}

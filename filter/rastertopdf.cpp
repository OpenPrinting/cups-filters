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
#include <cupsfilters/colormanager.h>
#include <cupsfilters/image.h>

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
#define cmsSaveProfileToMem _cmsSaveProfileToMem
#else
#include <lcms2.h>
#endif

#define DEFAULT_PDF_UNIT 72   // 1/72 inch

#define PROGRAM "rastertopdf"

#define dprintf(format, ...) fprintf(stderr, "DEBUG2: (" PROGRAM ") " format, __VA_ARGS__)

#define iprintf(format, ...) fprintf(stderr, "INFO: (" PROGRAM ") " format, __VA_ARGS__)


// Color conversion function
typedef unsigned char *(*convertFunction)(unsigned char *src,
  unsigned char *dst, unsigned int pixels);

// Bit conversion function
typedef unsigned char *(*bitFunction)(unsigned char *src,
  unsigned char *dst, unsigned int pixels);

// PDF color conversion function
typedef void (*pdfConvertFunction)(struct pdf_info * info);

cmsHPROFILE         colorProfile = NULL;     // ICC Profile to be applied to PDF
int                 cm_disabled = 0;         // Flag rasied if color management is disabled 
cm_calibration_t    cm_calibrate;            // Status of CUPS color management ("on" or "off")
convertFunction     conversion_function;     // Raster color conversion function
bitFunction         bit_function;            // Raster bit function


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



// Bit conversion functions

unsigned char *invertBits(unsigned char *src, unsigned char *dst, unsigned int pixels)
{ 
    unsigned int i;

    // Invert black to grayscale...
    for (i = pixels, dst = src; i > 0; i --, dst ++)
      *dst = ~*dst;

    return dst;
}	

unsigned char *noBitConversion(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    return src;
}

// Color conversion functions

unsigned char *rgbToCmyk(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    cupsImageRGBToCMYK(src,dst,pixels);
    return dst;
}
unsigned char *whiteToCmyk(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    cupsImageWhiteToCMYK(src,dst,pixels);
    return dst;
}

unsigned char *cmykToRgb(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    cupsImageCMYKToRGB(src,dst,pixels);
    return dst;
}

unsigned char *whiteToRgb(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    cupsImageWhiteToRGB(src,dst,pixels);
    return dst;
}

unsigned char *rgbToWhite(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    cupsImageRGBToWhite(src,dst,pixels);
    return dst;
}

unsigned char *cmykToWhite(unsigned char *src, unsigned char *dst, unsigned int pixels)
{
    cupsImageCMYKToWhite(src,dst,pixels);
    return dst;
}

unsigned char *noColorConversion(unsigned char *src,
  unsigned char *dst, unsigned int pixels)
{
    return src;
}




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
        bpp(0), bpc(0), render_intent(""),
        color_space(CUPS_CSPACE_K),
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
    std::string render_intent;
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





// PDF color conversion functons...

void modify_pdf_color(struct pdf_info * info, int bpp, int bpc, convertFunction fn)
{
    unsigned old_bpp = info->bpp;
    unsigned old_bpc = info->bpc;
    double old_ncolor = old_bpp/old_bpc;

    unsigned old_line_bytes = info->line_bytes;

    double new_ncolor = (bpp/bpc);

    info->line_bytes = (unsigned)old_line_bytes*(new_ncolor/old_ncolor);
    info->bpp = bpp;
    info->bpc = bpc;
    conversion_function = fn; 

    return;
}

void convertPdf_NoConversion(struct pdf_info * info)
{
    conversion_function = noColorConversion;
    bit_function = noBitConversion;
}

void convertPdf_Cmyk8ToWhite8(struct pdf_info * info)
{
    modify_pdf_color(info, 8, 8, cmykToWhite);
    bit_function = noBitConversion;
}

void convertPdf_Rgb8ToWhite8(struct pdf_info * info)
{
    modify_pdf_color(info, 8, 8, rgbToWhite);
    bit_function = noBitConversion;
}

void convertPdf_Cmyk8ToRgb8(struct pdf_info * info)
{
    modify_pdf_color(info, 24, 8, cmykToRgb);
    bit_function = noBitConversion;
}

void convertPdf_White8ToRgb8(struct pdf_info * info)
{
    modify_pdf_color(info, 24, 8, whiteToRgb);
    bit_function = invertBits;
}

void convertPdf_Rgb8ToCmyk8(struct pdf_info * info)
{
    modify_pdf_color(info, 32, 8, rgbToCmyk);
    bit_function = noBitConversion;
}

void convertPdf_White8ToCmyk8(struct pdf_info * info)
{
    modify_pdf_color(info, 32, 8, whiteToCmyk);
    bit_function = invertBits;
}

void convertPdf_InvertColors(struct pdf_info * info)
{
    conversion_function = noColorConversion;
    bit_function = invertBits;
}



#define PRE_COMPRESS

// Create an '/ICCBased' array and embed a previously 
// set ICC Profile in the PDF
QPDFObjectHandle embedIccProfile(QPDF &pdf)
{
    if (colorProfile == NULL) {
      return QPDFObjectHandle::newNull();
    }
    
    // Return handler
    QPDFObjectHandle ret;
    // ICCBased array
    QPDFObjectHandle array = QPDFObjectHandle::newArray();
    // Profile stream dictionary
    QPDFObjectHandle iccstream;

    std::map<std::string,QPDFObjectHandle> dict;
    std::map<std::string,QPDFObjectHandle> streamdict;
    std::string n_value = "";
    std::string alternate_cs = "";
    PointerHolder<Buffer>ph;

#ifdef USE_LCMS1
    size_t profile_size;
#else
    unsigned int profile_size;
#endif

    cmsColorSpaceSignature css = cmsGetColorSpace(colorProfile);

    // Write color component # for /ICCBased array in stream dictionary
    switch(css){
      case cmsSigGrayData:
        n_value = "1";
        alternate_cs = "/DeviceGray";
        break;
      case cmsSigRgbData:
        n_value = "3";
        alternate_cs = "/DeviceRGB";
        break;
      case cmsSigCmykData:
        n_value = "4";
        alternate_cs = "/DeviceCMYK";
        break;
      default:
        fputs("DEBUG: Failed to embed ICC Profile.\n", stderr);
        return QPDFObjectHandle::newNull();
    }

    streamdict["/Alternate"]=QPDFObjectHandle::newName(alternate_cs);
    streamdict["/N"]=QPDFObjectHandle::newName(n_value);

    // Read profile into memory
    cmsSaveProfileToMem(colorProfile, NULL, &profile_size);
    unsigned char buff[profile_size];
    cmsSaveProfileToMem(colorProfile, buff, &profile_size);

    // Write ICC profile buffer into PDF
    ph = new Buffer(buff, profile_size);  
    iccstream = QPDFObjectHandle::newStream(&pdf, ph);
    iccstream.replaceDict(QPDFObjectHandle::newDictionary(streamdict));

    array.appendItem(QPDFObjectHandle::newName("/ICCBased"));
    array.appendItem(iccstream);

    // Return a PDF object reference to an '/ICCBased' array
    ret = pdf.makeIndirectObject(array);

    fputs("DEBUG: ICC Profile embedded in PDF.\n", stderr); 

    return ret;
}

QPDFObjectHandle embedSrgbProfile(QPDF &pdf)
{
    QPDFObjectHandle iccbased_reference;

    // Create an sRGB profile from lcms
    colorProfile = cmsCreate_sRGBProfile();
    // Embed it into the profile
    iccbased_reference = embedIccProfile(pdf);

    return iccbased_reference;
}

/* 
Calibration function for non-Lab PDF color spaces 
Requires white point data, and if available, gamma or matrix numbers.

Output:
  [/'color_space' 
     << /Gamma ['gamma[0]'...'gamma[n]']
        /WhitePoint ['wp[0]' 'wp[1]' 'wp[2]']
        /Matrix ['matrix[0]'...'matrix[n*n]']
     >>
  ]        
*/
QPDFObjectHandle getCalibrationArray(const char * color_space, double wp[], 
                                     double gamma[], double matrix[], double bp[])
{    
    // Check for invalid input
    if ((!strcmp("/CalGray", color_space) && matrix != NULL) ||
         wp == NULL)
      return QPDFObjectHandle();

    QPDFObjectHandle ret;
    std::string csString = color_space;
    std::string colorSpaceArrayString = "";

    char gamma_str[128];
    char bp_str[256];
    char wp_str[256];
    char matrix_str[512];


    // Convert numbers into string data for /Gamma, /WhitePoint, and/or /Matrix


    // WhitePoint
    snprintf(wp_str, sizeof(wp_str), "/WhitePoint [%g %g %g]", 
                wp[0], wp[1], wp[2]); 


    // Gamma
    if (!strcmp("/CalGray", color_space) && gamma != NULL)
      snprintf(gamma_str, sizeof(gamma_str), "/Gamma %g", 
                  gamma[0]);
    else if (!strcmp("/CalRGB", color_space) && gamma != NULL) 
      snprintf(gamma_str, sizeof(gamma_str), "/Gamma [%g %g %g]", 
                  gamma[0], gamma[1], gamma[2]); 
    else
      gamma_str[0] = '\0';
    

    // BlackPoint
    if (bp != NULL)
      snprintf(bp_str, sizeof(bp_str), "/BlackPoint [%g %g %g]", 
                  bp[0], bp[1], bp[2]); 
    else
      bp_str[0] = '\0';


    // Matrix
    if (!strcmp("/CalRGB", color_space) && matrix != NULL) {
      snprintf(matrix_str, sizeof(matrix_str), "/Matrix [%g %g %g %g %g %g %g %g %g]", 
                  matrix[0], matrix[1], matrix[2],
                  matrix[3], matrix[4], matrix[5],
                  matrix[6], matrix[7], matrix[8]);
    } else
      matrix_str[0] = '\0';


    // Write array string...
    colorSpaceArrayString = "[" + csString + " <<" 
                            + gamma_str + " " + wp_str + " " + matrix_str + " " + bp_str
                            + " >>]";
                           
    ret = QPDFObjectHandle::parse(colorSpaceArrayString);

    return ret;
}

QPDFObjectHandle getCalRGBArray(double wp[3], double gamma[3], double matrix[9], double bp[3])
{
    QPDFObjectHandle ret = getCalibrationArray("/CalRGB", wp, gamma, matrix, bp);
    return ret;
}

QPDFObjectHandle getCalGrayArray(double wp[3], double gamma[1], double bp[3])
{
    QPDFObjectHandle ret = getCalibrationArray("/CalGray", wp, gamma, 0, bp);
    return ret;
}

QPDFObjectHandle makeImage(QPDF &pdf, PointerHolder<Buffer> page_data, unsigned width, 
                           unsigned height, std::string render_intent, cups_cspace_t cs, unsigned bpc)
{
    QPDFObjectHandle ret = QPDFObjectHandle::newStream(&pdf);

    QPDFObjectHandle icc_ref;

    int use_blackpoint = 0;
    std::map<std::string,QPDFObjectHandle> dict;

    dict["/Type"]=QPDFObjectHandle::newName("/XObject");
    dict["/Subtype"]=QPDFObjectHandle::newName("/Image");
    dict["/Width"]=QPDFObjectHandle::newInteger(width);
    dict["/Height"]=QPDFObjectHandle::newInteger(height);
    dict["/BitsPerComponent"]=QPDFObjectHandle::newInteger(bpc);

    if (!cm_disabled) {
      // Write rendering intent into the PDF based on raster settings
      if (render_intent == "Perceptual") {
        dict["/Intent"]=QPDFObjectHandle::newName("/Perceptual");
      } else if (render_intent == "Absolute") {
        dict["/Intent"]=QPDFObjectHandle::newName("/AbsoluteColorimetric");
      } else if (render_intent == "Relative") {
        dict["/Intent"]=QPDFObjectHandle::newName("/RelativeColorimetric");
      } else if (render_intent == "Saturation") {
        dict["/Intent"]=QPDFObjectHandle::newName("/Saturation");
      } else if (render_intent == "RelativeBpc") {
        /* Enable blackpoint compensation */
        dict["/Intent"]=QPDFObjectHandle::newName("/RelativeColorimetric");
        use_blackpoint = 1;
      }
    }

    /* Write "/ColorSpace" dictionary based on raster input */
    if (colorProfile != NULL && !cm_disabled) {
      icc_ref = embedIccProfile(pdf);

      if (!icc_ref.isNull())
        dict["/ColorSpace"]=icc_ref;
    } else if (!cm_disabled) {
        switch (cs) {
            case CUPS_CSPACE_DEVICE1:
            case CUPS_CSPACE_DEVICE2:
            case CUPS_CSPACE_DEVICE3:
            case CUPS_CSPACE_DEVICE4:
            case CUPS_CSPACE_DEVICE5:
            case CUPS_CSPACE_DEVICE6:
            case CUPS_CSPACE_DEVICE7:
            case CUPS_CSPACE_DEVICE8:
            case CUPS_CSPACE_DEVICE9:
            case CUPS_CSPACE_DEVICEA:
            case CUPS_CSPACE_DEVICEB:
            case CUPS_CSPACE_DEVICEC:
            case CUPS_CSPACE_DEVICED:
            case CUPS_CSPACE_DEVICEE:
            case CUPS_CSPACE_DEVICEF:
                // For right now, DeviceN will use /DeviceCMYK in the PDF
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
                break;
            case CUPS_CSPACE_K:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
                break;
            case CUPS_CSPACE_SW:
                if (use_blackpoint)
                  dict["/ColorSpace"]=getCalGrayArray(cmWhitePointSGray(), cmGammaSGray(), 
                                                      cmBlackPointDefault());
                else
                  dict["/ColorSpace"]=getCalGrayArray(cmWhitePointSGray(), cmGammaSGray(), 0);
                break;
            case CUPS_CSPACE_CMYK:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
                break;
            case CUPS_CSPACE_RGB:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
                break;
            case CUPS_CSPACE_SRGB:
                icc_ref = embedSrgbProfile(pdf);
                if (!icc_ref.isNull())
                  dict["/ColorSpace"]=icc_ref;
                else 
                  dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
                break;
            case CUPS_CSPACE_ADOBERGB:
                if (use_blackpoint)
                  dict["/ColorSpace"]=getCalRGBArray(cmWhitePointAdobeRgb(), cmGammaAdobeRgb(), 
                                                         cmMatrixAdobeRgb(), cmBlackPointDefault());
                else
                  dict["/ColorSpace"]=getCalRGBArray(cmWhitePointAdobeRgb(), 
                                                     cmGammaAdobeRgb(), cmMatrixAdobeRgb(), 0);
                break;
            default:
                fputs("DEBUG: Color space not supported.\n", stderr); 
                return QPDFObjectHandle();
        }
    } else if (cm_disabled) {
        switch(cs) {
          case CUPS_CSPACE_K:
          case CUPS_CSPACE_SW:
            dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
            break;
          case CUPS_CSPACE_RGB:
          case CUPS_CSPACE_SRGB:
          case CUPS_CSPACE_ADOBERGB:
            dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
            break;
          case CUPS_CSPACE_DEVICE1:
          case CUPS_CSPACE_DEVICE2:
          case CUPS_CSPACE_DEVICE3:
          case CUPS_CSPACE_DEVICE4:
          case CUPS_CSPACE_DEVICE5:
          case CUPS_CSPACE_DEVICE6:
          case CUPS_CSPACE_DEVICE7:
          case CUPS_CSPACE_DEVICE8:
          case CUPS_CSPACE_DEVICE9:
          case CUPS_CSPACE_DEVICEA:
          case CUPS_CSPACE_DEVICEB:
          case CUPS_CSPACE_DEVICEC:
          case CUPS_CSPACE_DEVICED:
          case CUPS_CSPACE_DEVICEE:
          case CUPS_CSPACE_DEVICEF:
          case CUPS_CSPACE_CMYK:
            dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
            break;
          default:
            fputs("DEBUG: Color space not supported.\n", stderr); 
            return QPDFObjectHandle();
        }
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

    QPDFObjectHandle image = makeImage(info->pdf, info->page_data, info->width, info->height, info->render_intent, info->color_space, info->bpc);
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


/* Perform modifications to PDF if color space conversions are needed */      
int prepare_pdf_page(struct pdf_info * info, int width, int height, int bpl, 
                     int bpp, int bpc, std::string render_intent, cups_cspace_t color_space)
{
#define IMAGE_CMYK_8   (bpp == 32 && bpc == 8)
#define IMAGE_CMYK_16  (bpp == 64 && bpc == 16)
#define IMAGE_RGB_8    (bpp == 24 && bpc == 8)
#define IMAGE_RGB_16   (bpp == 48 && bpc == 16)
#define IMAGE_WHITE_1  (bpp == 1 && bpc == 1)
#define IMAGE_WHITE_8  (bpp == 8 && bpc == 8)
#define IMAGE_WHITE_16 (bpp == 16 && bpc == 16)    
     
    int error = 0;
    pdfConvertFunction fn = convertPdf_NoConversion;
    cmsColorSpaceSignature css;

    /* Register available raster information into the PDF */
    info->width = width;
    info->height = height;
    info->line_bytes = bpl;
    info->bpp = bpp;
    info->bpc = bpc;
    info->render_intent = render_intent;
    info->color_space = color_space;

    /* Invert grayscale by default */
    if (color_space == CUPS_CSPACE_K)
      fn = convertPdf_InvertColors;

    if (colorProfile != NULL) {
      css = cmsGetColorSpace(colorProfile);

      // Convert image and PDF color space to an embedded ICC Profile color space
      switch(css) {
        // Convert PDF to Grayscale when using a gray profile
        case cmsSigGrayData:
          if (color_space == CUPS_CSPACE_CMYK)
            fn = convertPdf_Cmyk8ToWhite8;
          else if (color_space == CUPS_CSPACE_RGB) 
            fn = convertPdf_Rgb8ToWhite8;
          else              
            fn = convertPdf_InvertColors;
          info->color_space = CUPS_CSPACE_K;
          break;
        // Convert PDF to RGB when using an RGB profile
        case cmsSigRgbData:
          if (color_space == CUPS_CSPACE_CMYK) 
            fn = convertPdf_Cmyk8ToRgb8;
          else if (color_space == CUPS_CSPACE_K) 
            fn = convertPdf_White8ToRgb8;
          info->color_space = CUPS_CSPACE_RGB;
          break;
        // Convert PDF to CMYK when using an RGB profile
        case cmsSigCmykData:
          if (color_space == CUPS_CSPACE_RGB)
            fn = convertPdf_Rgb8ToCmyk8;
          else if (color_space == CUPS_CSPACE_K) 
            fn = convertPdf_White8ToCmyk8;
          info->color_space = CUPS_CSPACE_CMYK;
          break;
        default:
          fputs("DEBUG: Unable to convert PDF from profile.\n", stderr);
          colorProfile = NULL;
          error = 1;
      }
      // Perform conversion of an image color space 
    } else if (!cm_disabled) {       
      switch (color_space) {
         // Convert image to CMYK
         case CUPS_CSPACE_CMYK:
           if (IMAGE_RGB_8)
             fn = convertPdf_Rgb8ToCmyk8;  
           else if (IMAGE_RGB_16)
             fn = convertPdf_NoConversion;
           else if (IMAGE_WHITE_8)
             fn = convertPdf_White8ToCmyk8;  
           else if (IMAGE_WHITE_16) 
             fn = convertPdf_NoConversion;
           break;
         // Convert image to RGB
         case CUPS_CSPACE_ADOBERGB:
         case CUPS_CSPACE_RGB:
         case CUPS_CSPACE_SRGB:
           if (IMAGE_CMYK_8)
             fn = convertPdf_Cmyk8ToRgb8;
           else if (IMAGE_CMYK_16)
             fn = convertPdf_NoConversion;  
           else if (IMAGE_WHITE_8)
             fn = convertPdf_White8ToRgb8;
           else if (IMAGE_WHITE_16) 
             fn = convertPdf_NoConversion;       
           break;
         // Convert image to Grayscale
         case CUPS_CSPACE_SW:
         case CUPS_CSPACE_K:
           if (IMAGE_CMYK_8)
             fn = convertPdf_Cmyk8ToWhite8;
           else if (IMAGE_CMYK_16)
             fn = convertPdf_NoConversion;
           else if (IMAGE_RGB_8) 
             fn = convertPdf_Rgb8ToWhite8;
           else if (IMAGE_RGB_16) 
             fn = convertPdf_NoConversion;
           break;    
         case CUPS_CSPACE_DEVICE1:
         case CUPS_CSPACE_DEVICE2:
         case CUPS_CSPACE_DEVICE3:
         case CUPS_CSPACE_DEVICE4:
         case CUPS_CSPACE_DEVICE5:
         case CUPS_CSPACE_DEVICE6:
         case CUPS_CSPACE_DEVICE7:
         case CUPS_CSPACE_DEVICE8:
         case CUPS_CSPACE_DEVICE9:
         case CUPS_CSPACE_DEVICEA:
         case CUPS_CSPACE_DEVICEB:
         case CUPS_CSPACE_DEVICEC:
         case CUPS_CSPACE_DEVICED:
         case CUPS_CSPACE_DEVICEE:
         case CUPS_CSPACE_DEVICEF:
             // No conversion for right now
             fn = convertPdf_NoConversion;
             break;
         default:
           fputs("DEBUG: Color space not supported.\n", stderr);
           error = 1;
           break;
      }
   } 

   if (!error)
     fn(info);

   return error;
}

int add_pdf_page(struct pdf_info * info, int pagen, unsigned width,
		 unsigned height, int bpp, int bpc, int bpl, std::string render_intent,
		 cups_cspace_t color_space, unsigned xdpi, unsigned ydpi)
{
    try {
        finish_page(info); // any active

        prepare_pdf_page(info, width, height, bpl, bpp, 
                         bpc, render_intent, color_space);

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
//        output.setMinimumPDFVersion("1.4");
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
    unsigned char *PixelBuffer, *ptr, *buff;

    PixelBuffer = (unsigned char *)malloc(bpl);
    buff = (unsigned char *)malloc(info->line_bytes);

    do
    {
        // Read raster data...
        cupsRasterReadPixels(ras, PixelBuffer, bpl);

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

        // perform bit operations if necessary
        bit_function(PixelBuffer, ptr,  bpl);

        // write lines and color convert when necessary
 	pdf_set_line(info, cur_line, conversion_function(PixelBuffer, buff, width));
	++cur_line;
    }
    while(cur_line < height);

    free(buff);
    free(PixelBuffer);

    return 0;
}

int setProfile(const char * path) 
{
    if (path != NULL) 
      colorProfile = cmsOpenProfileFromFile(path,"r");

    if (colorProfile != NULL) {
      fputs("DEBUG: Load profile successful.\n", stderr); 
      return 0;
    }
    else {
      fputs("DEBUG: Unable to load profile.\n", stderr); 
      return 1;
    }
}

/* Obtain a source profile name using color qualifiers from raster file */
const char * getIPPColorProfileName(const char * media_type, cups_cspace_t cs, unsigned dpi)
{
    std::string mediaType = "";
    std::string resolution = "";
    std::string colorModel = "";
   
    std::string iccProfile = "";

    // ColorModel
    switch (cs) {
        case CUPS_CSPACE_RGB:
            colorModel = "rgb";
            break;
        case CUPS_CSPACE_SRGB:
            colorModel = "srgb";
            break;
        case CUPS_CSPACE_ADOBERGB:
            colorModel = "adobergb";
            break;
        case CUPS_CSPACE_K:
            colorModel = "gray";
            break;
        case CUPS_CSPACE_CMYK:
            colorModel = "cmyk";
            break;
        default:
            colorModel = "";
            break;
     }
 
    if (media_type != NULL)
      mediaType = media_type;
    if (dpi > 0)
      resolution = dpi;

    // Requires color space and media type qualifiers
    if (resolution != "" || colorModel != "")
      return 0;

    // profile-uri reference: "http://www.server.com/colorModel-Resolution-mediaType.icc
    if (mediaType != "")          
      iccProfile = colorModel + "-" + resolution + ".icc";
    else 
      iccProfile = colorModel + "-" + resolution + "-" + mediaType + ".icc";

    return iccProfile.c_str();
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
    const char*         profile_name;	/* IPP Profile Name */
    cups_option_t	*options;	/* Options */

    // Make sure status messages are not buffered...
    setbuf(stderr, NULL);

    cmsSetLogErrorHandler(lcmsErrorHandler);

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "Usage: %s <job> <user> <job name> <copies> <option> [file]\n", argv[0]);
        return 1;
    }

    num_options = cupsParseOptions(argv[5], 0, &options);  

    /* support the CUPS "cm-calibration" option */ 
    cm_calibrate = cmGetCupsColorCalibrateMode(options, num_options);

    if (cm_calibrate == CM_CALIBRATION_ENABLED)
      cm_disabled = 1;
    else
      cm_disabled = cmIsPrinterCmDisabled(getenv("PRINTER"));

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

      // Use "profile=profile_name.icc" to embed 'profile_name.icc' into the PDF
      // for testing. Forces color management to enable.
      if ((profile_name = cupsGetOption("profile", num_options, options)) != NULL) {
        setProfile(profile_name);
        cm_disabled = 0;
      }
      if (colorProfile != NULL)       
        fprintf(stderr, "DEBUG: TEST ICC Profile specified (color management forced ON): \n[%s]\n", profile_name);

      // Add a new page to PDF file
      if (add_pdf_page(&pdf, Page, header.cupsWidth, header.cupsHeight,
		       header.cupsBitsPerPixel, header.cupsBitsPerColor, 
		       header.cupsBytesPerLine, header.cupsRenderingIntent, 
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

    if (colorProfile != NULL) {
      cmsCloseProfile(colorProfile);
    }

    cupsFreeOptions(num_options, options);

    cupsRasterClose(ras);

    if (fd != 0)
      close(fd);

    return (Page == 0);
}

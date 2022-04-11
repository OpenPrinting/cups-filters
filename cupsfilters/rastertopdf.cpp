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
 * @brief Convert PWG Raster to a PDF/PCLm file
 * @file rastertopdf.cpp
 * @author Neil 'Superna' Armstrong <superna9999@gmail.com> (C) 2010
 * @author Tobias Hoffmann <smilingthax@gmail.com> (c) 2012
 * @author Till Kamppeter <till.kamppeter@gmail.com> (c) 2014
 * @author Sahil Arora <sahilarora.535@gmail.com> (c) 2017
 */

#include "filter.h"
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
#include <qpdf/Pl_RunLength.hh>
#include <qpdf/Pl_DCT.hh>

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

// Compression method for providing data to PCLm Streams.
typedef enum compression_method_e {
  DCT_DECODE = 0,
  FLATE_DECODE,
  RLE_DECODE
} compression_method_t;

// Color conversion function
typedef unsigned char *(*convert_function)(unsigned char *src,
  unsigned char *dst, unsigned int pixels);

// Bit conversion function
typedef unsigned char *(*bit_convert_function)(unsigned char *src,
  unsigned char *dst, unsigned int pixels);

typedef struct rastertopdf_doc_s               /**** Document information ****/
{
  cmsHPROFILE         colorProfile = NULL;     /* ICC Profile to be applied to
						  PDF */
  int                 cm_disabled = 0;         /* Flag raised if color
						  management is disabled */
  convert_function     conversion_function;     /* Raster color conversion
						  function */
  bit_convert_function         bit_function;            /* Raster bit function */
  FILE		      *outputfp;	       /* Temporary file, if any */
  cf_logfunc_t    logfunc;                 /* Logging function, NULL for no
						  logging */
  void                *logdata;                /* User data for logging
						  function, can be NULL */
  cf_filter_iscanceledfunc_t iscanceledfunc;      /* Function returning 1 when
                                                  job is canceled, NULL for not
                                                  supporting stop on cancel */
  void *iscanceleddata;                        /* User data for is-canceled
						  function, can be NULL */
} rastertopdf_doc_t;

// PDF color conversion function
typedef void (*pdf_convert_function)(struct pdf_info * info,
				   rastertopdf_doc_t *doc);


// Bit conversion functions
static unsigned char *invert_bits(unsigned char *src, unsigned char *dst,
				  unsigned int pixels)
{ 
    unsigned int i;

    // Invert black to grayscale...
    for (i = pixels, dst = src; i > 0; i --, dst ++)
      *dst = ~*dst;

    return dst;
}	

static unsigned char *no_bit_conversion(unsigned char *src, unsigned char *dst,
			       unsigned int pixels)
{
    return src;
}

// Color conversion functions
static unsigned char *rgb_to_cmyk(unsigned char *src, unsigned char *dst,
			 unsigned int pixels)
{
    cfImageRGBToCMYK(src,dst,pixels);
    return dst;
}

static unsigned char *white_to_cmyk(unsigned char *src, unsigned char *dst,
			   unsigned int pixels)
{
    cfImageWhiteToCMYK(src,dst,pixels);
    return dst;
}

static unsigned char *cmyk_to_rgb(unsigned char *src, unsigned char *dst,
			 unsigned int pixels)
{
    cfImageCMYKToRGB(src,dst,pixels);
    return dst;
}

static unsigned char *white_to_rgb(unsigned char *src, unsigned char *dst,
			  unsigned int pixels)
{
    cfImageWhiteToRGB(src,dst,pixels);
    return dst;
}

static unsigned char *rgb_to_white(unsigned char *src, unsigned char *dst,
			  unsigned int pixels)
{
    cfImageRGBToWhite(src,dst,pixels);
    return dst;
}

static unsigned char *cmyk_to_white(unsigned char *src, unsigned char *dst,
			   unsigned int pixels)
{
    cfImageCMYKToWhite(src,dst,pixels);
    return dst;
}

static unsigned char *no_color_conversion(unsigned char *src,
				 unsigned char *dst, unsigned int pixels)
{
    return src;
}

/**
 * 'split_strings()' - Split a string to a vector of strings given some
 *                     delimiters
 * O - std::vector of std::string after splitting
 * I - input string to be split
 * I - string containing delimiters
 */
static std::vector<std::string>
split_strings(std::string const &str, std::string delimiters = ",")
{
  std::vector<std::string> vec(0);
  std::string value = "";
  bool push_flag = false;

  for (size_t i = 0; i < str.size(); i ++)
  {
    if (push_flag && !(value.empty()))
    {
      vec.push_back(value);
      push_flag = false;
      value.clear();
    }

    if (delimiters.find(str[i]) != std::string::npos)
      push_flag = true;
    else
      value += str[i];
  }
  if (!value.empty())
    vec.push_back(value);
  return vec;
}

/**
 * 'num_digits()' - Calculates the number of digits in an integer
 * O - number of digits in the input integer
 * I - the integer whose digits needs to be calculated
 */
static int num_digits(int n)
{
  if (n == 0) return 1;
  int digits = 0;
  while (n)
  {
    ++digits;
    n /= 10;
  }
  return digits;
}

/**
 * 'int_to_fwstring()' - Convert a number to fixed width string by padding
 *                       with zeroes
 * O - converted string
 * I - the integee which needs to be converted to string
 * I - width of string required
 */
static std::string int_to_fwstring(int n, int width)
{
  int num_zeroes = width - num_digits(n);
  if (num_zeroes < 0)
    num_zeroes = 0;
  return std::string(num_zeroes, '0') + QUtil::int_to_string(n);
}


//------------- PDF ---------------

struct pdf_info
{
    pdf_info() 
      : pagecount(0),
        width(0),height(0),
        line_bytes(0),
        bpp(0), bpc(0),
        pclm_num_strips(0),
        pclm_strip_height_preferred(16),  /* default strip height */
        pclm_strip_height(0),
        pclm_strip_height_supported(1, 16),
        pclm_compression_method_preferred(0),
        pclm_source_resolution_supported(0),
        pclm_source_resolution_default(""),
        pclm_raster_back_side(""),
        pclm_strip_data(0),
        render_intent(""),
        color_space(CUPS_CSPACE_K),
        page_width(0),page_height(0),
        outformat(CF_FILTER_OUT_FORMAT_PDF)
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
    unsigned                  pclm_num_strips;
    unsigned                  pclm_strip_height_preferred;
    std::vector<unsigned>     pclm_strip_height;
    std::vector<unsigned>     pclm_strip_height_supported;
    std::vector<compression_method_t> pclm_compression_method_preferred;
    std::vector<std::string>  pclm_source_resolution_supported;
    std::string               pclm_source_resolution_default;
    std::string               pclm_raster_back_side;
    std::vector< PointerHolder<Buffer> > pclm_strip_data;
    std::string render_intent;
    cups_cspace_t color_space;
    PointerHolder<Buffer> page_data;
    double page_width,page_height;
    cf_filter_out_format_t outformat;
};

static int create_pdf_file(struct pdf_info * info,
		    const cf_filter_out_format_t &outformat)
{
    try {
        info->pdf.emptyPDF();
        info->outformat = outformat;
    } catch (...) {
        return 1;
    }
    return 0;
}

static QPDFObjectHandle make_real_box(double x1, double y1, double x2, double y2)
{
    QPDFObjectHandle ret=QPDFObjectHandle::newArray();
    ret.appendItem(QPDFObjectHandle::newReal(x1));
    ret.appendItem(QPDFObjectHandle::newReal(y1));
    ret.appendItem(QPDFObjectHandle::newReal(x2));
    ret.appendItem(QPDFObjectHandle::newReal(y2));
    return ret;
}

static QPDFObjectHandle make_integer_box(int x1, int y1, int x2, int y2)
{
    QPDFObjectHandle ret = QPDFObjectHandle::newArray();
    ret.appendItem(QPDFObjectHandle::newInteger(x1));
    ret.appendItem(QPDFObjectHandle::newInteger(y1));
    ret.appendItem(QPDFObjectHandle::newInteger(x2));
    ret.appendItem(QPDFObjectHandle::newInteger(y2));
    return ret;
}




// PDF color conversion functons...

static void modify_pdf_color(struct pdf_info * info, int bpp, int bpc,
		      convert_function fn, rastertopdf_doc_t *doc)
{
    unsigned old_bpp = info->bpp;
    unsigned old_bpc = info->bpc;
    double old_ncolor = old_bpp/old_bpc;

    unsigned old_line_bytes = info->line_bytes;

    double new_ncolor = (bpp/bpc);

    info->line_bytes = (unsigned)old_line_bytes*(new_ncolor/old_ncolor);
    info->bpp = bpp;
    info->bpc = bpc;
    doc->conversion_function = fn; 

    return;
}

static void convert_pdf_no_conversion(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    doc->conversion_function = no_color_conversion;
    doc->bit_function = no_bit_conversion;
}

static void convert_pdf_cmyk_8_to_white_8(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    modify_pdf_color(info, 8, 8, cmyk_to_white, doc);
    doc->bit_function = no_bit_conversion;
}

static void convert_pdf_rgb_8_to_white_8(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    modify_pdf_color(info, 8, 8, rgb_to_white, doc);
    doc->bit_function = no_bit_conversion;
}

static void convert_pdf_cmyk_8_to_rgb_8(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    modify_pdf_color(info, 24, 8, cmyk_to_rgb, doc);
    doc->bit_function = no_bit_conversion;
}

static void convert_pdf_white_8_to_rgb_8(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    modify_pdf_color(info, 24, 8, white_to_rgb, doc);
    doc->bit_function = invert_bits;
}

static void convert_pdf_rgb_8_to_cmyk_8(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    modify_pdf_color(info, 32, 8, rgb_to_cmyk, doc);
    doc->bit_function = no_bit_conversion;
}

static void convert_pdf_white_8_to_cmyk_8(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    modify_pdf_color(info, 32, 8, white_to_cmyk, doc);
    doc->bit_function = invert_bits;
}

static void convert_pdf_invert_colors(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    doc->conversion_function = no_color_conversion;
    doc->bit_function = invert_bits;
}


#define PRE_COMPRESS

// Create an '/ICCBased' array and embed a previously 
// set ICC Profile in the PDF
static QPDFObjectHandle embed_icc_profile(QPDF &pdf, rastertopdf_doc_t *doc)
{
    if (doc->colorProfile == NULL) {
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

    cmsColorSpaceSignature css = cmsGetColorSpace(doc->colorProfile);

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
        if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		      "cfFilterRasterToPDF: Failed to embed ICC Profile.");
        return QPDFObjectHandle::newNull();
    }

    streamdict["/Alternate"]=QPDFObjectHandle::newName(alternate_cs);
    streamdict["/N"]=QPDFObjectHandle::newName(n_value);

    // Read profile into memory
    cmsSaveProfileToMem(doc->colorProfile, NULL, &profile_size);
    unsigned char *buff =
        (unsigned char *)calloc(profile_size, sizeof(unsigned char));
    cmsSaveProfileToMem(doc->colorProfile, buff, &profile_size);

    // Write ICC profile buffer into PDF
    ph = new Buffer(buff, profile_size);  
    iccstream = QPDFObjectHandle::newStream(&pdf, ph);
    iccstream.replaceDict(QPDFObjectHandle::newDictionary(streamdict));

    array.appendItem(QPDFObjectHandle::newName("/ICCBased"));
    array.appendItem(iccstream);

    // Return a PDF object reference to an '/ICCBased' array
    ret = pdf.makeIndirectObject(array);

    free(buff);
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		      "cfFilterRasterToPDF: ICC Profile embedded in PDF.");

    return ret;
}

static QPDFObjectHandle embed_srgb_profile(QPDF &pdf, rastertopdf_doc_t *doc)
{
    QPDFObjectHandle iccbased_reference;

    // Create an sRGB profile from lcms
    doc->colorProfile = cmsCreate_sRGBProfile();
    // Embed it into the profile
    iccbased_reference = embed_icc_profile(pdf, doc);

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
static QPDFObjectHandle get_calibration_array(const char * color_space, double wp[], 
                                     double gamma[], double matrix[],
				     double bp[])
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
      snprintf(matrix_str, sizeof(matrix_str),
	       "/Matrix [%g %g %g %g %g %g %g %g %g]", 
                  matrix[0], matrix[1], matrix[2],
                  matrix[3], matrix[4], matrix[5],
                  matrix[6], matrix[7], matrix[8]);
    } else
      matrix_str[0] = '\0';


    // Write array string...
    colorSpaceArrayString = "[" + csString + " <<" + gamma_str + " " + wp_str +
                            " " + matrix_str + " " + bp_str + " >>]";
                           
    ret = QPDFObjectHandle::parse(colorSpaceArrayString);

    return ret;
}

static QPDFObjectHandle get_cal_rgb_array(double wp[3], double gamma[3],
				double matrix[9], double bp[3])
{
    QPDFObjectHandle ret = get_calibration_array("/CalRGB", wp, gamma, matrix,
					       bp);
    return ret;
}

static QPDFObjectHandle get_cal_gray_array(double wp[3], double gamma[1], double bp[3])
{
    QPDFObjectHandle ret = get_calibration_array("/CalGray", wp, gamma, 0, bp);
    return ret;
}

/**
 * 'make_pclm_strips()' - return an std::vector of QPDFObjectHandle, each
 *                      containing the stream data of the various strips
 *                      which make up a PCLm page.
 * O - std::vector of QPDFObjectHandle
 * I - QPDF object
 * I - number of strips per page
 * I - std::vector of PointerHolder<Buffer> containing data for each strip
 * I - strip width
 * I - strip height
 * I - color space
 * I - bits per component
 * I - document information
 */
static std::vector<QPDFObjectHandle>
make_pclm_strips(QPDF &pdf, unsigned num_strips,
               std::vector< PointerHolder<Buffer> > &strip_data,
               std::vector<compression_method_t> &compression_methods,
               unsigned width, std::vector<unsigned>& strip_height,
	       cups_cspace_t cs, unsigned bpc, rastertopdf_doc_t *doc)
{
    std::vector<QPDFObjectHandle> ret(num_strips);
    for (size_t i = 0; i < num_strips; i ++)
      ret[i] = QPDFObjectHandle::newStream(&pdf);

    // Strip stream dictionary
    std::map<std::string,QPDFObjectHandle> dict;

    dict["/Type"]=QPDFObjectHandle::newName("/XObject");
    dict["/Subtype"]=QPDFObjectHandle::newName("/Image");
    dict["/Width"]=QPDFObjectHandle::newInteger(width);
    dict["/BitsPerComponent"]=QPDFObjectHandle::newInteger(bpc);

    J_COLOR_SPACE color_space;
    unsigned components;
    /* Write "/ColorSpace" dictionary based on raster input */
    switch(cs) {
      case CUPS_CSPACE_K:
      case CUPS_CSPACE_SW:
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceGray");
        color_space = JCS_GRAYSCALE;
        components = 1;
        break;
      case CUPS_CSPACE_RGB:
      case CUPS_CSPACE_SRGB:
      case CUPS_CSPACE_ADOBERGB:
        dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
        color_space = JCS_RGB;
        components = 3;
        break;
      default:
        if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		      "cfFilterRasterToPDF: Color space not supported.");
        return std::vector<QPDFObjectHandle>(num_strips, QPDFObjectHandle());
    }

    // We deliver already compressed content (instead of letting QPDFWriter
    // do it) to avoid using excessive memory. For that we first get preferred
    // compression method to pre-compress content for strip streams.

    // Use the compression method with highest priority of the available methods
    // __________________
    // Priority | Method
    // ------------------
    // 0        | DCT
    // 1        | FLATE
    // 2        | RLE
    // ------------------
    compression_method_t compression = compression_methods.front();
    for (std::vector<compression_method_t>::iterator it =
	   compression_methods.begin();
         it != compression_methods.end(); ++it)
      compression = compression > *it ? compression : *it;

    // write compressed stream data
    for (size_t i = 0; i < num_strips; i ++)
    {
      dict["/Height"]=QPDFObjectHandle::newInteger(strip_height[i]);
      ret[i].replaceDict(QPDFObjectHandle::newDictionary(dict));
      Pl_Buffer psink("psink");
      if (compression == FLATE_DECODE)
      {
        Pl_Flate pflate("pflate", &psink, Pl_Flate::a_deflate);
        pflate.write(strip_data[i]->getBuffer(), strip_data[i]->getSize());
        pflate.finish();
        ret[i].replaceStreamData(PointerHolder<Buffer>(psink.getBuffer()),
				 QPDFObjectHandle::newName("/FlateDecode"),
				 QPDFObjectHandle::newNull());
      }
      else if (compression == RLE_DECODE)
      {
        Pl_RunLength prle("prle", &psink, Pl_RunLength::a_encode);
        prle.write(strip_data[i]->getBuffer(),strip_data[i]->getSize());
        prle.finish();
        ret[i].replaceStreamData(PointerHolder<Buffer>(psink.getBuffer()),
				 QPDFObjectHandle::newName("/RunLengthDecode"),
				 QPDFObjectHandle::newNull());
      }
      else if (compression == DCT_DECODE)
      {
        Pl_DCT pdct("pdct", &psink, width, strip_height[i], components, color_space);
        pdct.write(strip_data[i]->getBuffer(),strip_data[i]->getSize());
        pdct.finish();
        ret[i].replaceStreamData(PointerHolder<Buffer>(psink.getBuffer()),
				 QPDFObjectHandle::newName("/DCTDecode"),
				 QPDFObjectHandle::newNull());
      }
    }
    return ret;
}

static QPDFObjectHandle make_image(QPDF &pdf, PointerHolder<Buffer> page_data,
			   unsigned width, unsigned height,
			   std::string render_intent, cups_cspace_t cs,
			   unsigned bpc, rastertopdf_doc_t *doc)
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

    if (!doc->cm_disabled) {
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
    if (doc->colorProfile != NULL && !doc->cm_disabled) {
      icc_ref = embed_icc_profile(pdf, doc);

      if (!icc_ref.isNull())
        dict["/ColorSpace"]=icc_ref;
    } else if (!doc->cm_disabled) {
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
                  dict["/ColorSpace"]=get_cal_gray_array(cfCmWhitePointSGray(),
						      cfCmGammaSGray(), 
                                                      cfCmBlackPointDefault());
                else
                  dict["/ColorSpace"]=get_cal_gray_array(cfCmWhitePointSGray(),
						      cfCmGammaSGray(), 0);
                break;
            case CUPS_CSPACE_CMYK:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceCMYK");
                break;
            case CUPS_CSPACE_RGB:
                dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
                break;
            case CUPS_CSPACE_SRGB:
                icc_ref = embed_srgb_profile(pdf, doc);
                if (!icc_ref.isNull())
                  dict["/ColorSpace"]=icc_ref;
                else 
                  dict["/ColorSpace"]=QPDFObjectHandle::newName("/DeviceRGB");
                break;
            case CUPS_CSPACE_ADOBERGB:
                if (use_blackpoint)
                  dict["/ColorSpace"]=get_cal_rgb_array(cfCmWhitePointAdobeRGB(),
						     cfCmGammaAdobeRGB(), 
						     cfCmMatrixAdobeRGB(),
						     cfCmBlackPointDefault());
                else
                  dict["/ColorSpace"]=get_cal_rgb_array(cfCmWhitePointAdobeRGB(), 
                                                     cfCmGammaAdobeRGB(),
						     cfCmMatrixAdobeRGB(), 0);
                break;
            default:
                if (doc->logfunc)
		  doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
			       "cfFilterRasterToPDF: Color space not supported.");
                return QPDFObjectHandle();
        }
    } else if (doc->cm_disabled) {
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
            if (doc->logfunc)
	      doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
			   "cfFilterRasterToPDF: Color space not supported.");
            return QPDFObjectHandle();
        }
    } else
        return QPDFObjectHandle();

    ret.replaceDict(QPDFObjectHandle::newDictionary(dict));

#ifdef PRE_COMPRESS
    // we deliver already compressed content (instead of letting QPDFWriter
    // do it), to avoid using excessive memory
    Pl_Buffer psink("psink");
    Pl_Flate pflate("pflate",&psink,Pl_Flate::a_deflate);
    
    pflate.write(page_data->getBuffer(),page_data->getSize());
    pflate.finish();

    ret.replaceStreamData(PointerHolder<Buffer>(psink.getBuffer()),
                          QPDFObjectHandle::newName("/FlateDecode"),
			  QPDFObjectHandle::newNull());
#else
    ret.replaceStreamData(page_data,QPDFObjectHandle::newNull(),
			  QPDFObjectHandle::newNull());
#endif

    return ret;
}

static int finish_page(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    if (info->outformat == CF_FILTER_OUT_FORMAT_PDF)
    {
      // Finish previous PDF Page
      if(!info->page_data.getPointer())
          return 0;

      QPDFObjectHandle image = make_image(info->pdf, info->page_data,
					 info->width, info->height,
					 info->render_intent,
					 info->color_space, info->bpc, doc);
      if(!image.isInitialized())
      {
        if (doc->logfunc)
	  doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		       "cfFilterRasterToPDF: Unable to load image data");
        return 1;
      }

      // add it
      info->page.getKey("/Resources").getKey("/XObject").replaceKey("/I",image);
    }
    else if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
    {
      // Finish previous PCLm page
      if (info->pclm_num_strips == 0)
        return 0;

      for (size_t i = 0; i < info->pclm_strip_data.size(); i ++)
        if(!info->pclm_strip_data[i].getPointer())
          return 0;

      std::vector<QPDFObjectHandle> strips =
	make_pclm_strips(info->pdf, info->pclm_num_strips, info->pclm_strip_data,
		       info->pclm_compression_method_preferred, info->width,
		       info->pclm_strip_height, info->color_space, info->bpc,
		       doc);
      for (size_t i = 0; i < info->pclm_num_strips; i ++)
        if(!strips[i].isInitialized())
        {
          if (doc->logfunc)
	    doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
			 "cfFilterRasterToPDF: Unable to load strip data");
          return 1;
        }

      // add it
      for (size_t i = 0; i < info->pclm_num_strips; i ++)
        info->page.getKey("/Resources").getKey("/XObject")
	  .replaceKey("/Image" +
		      int_to_fwstring(i, num_digits(info->pclm_num_strips - 1)),
		      strips[i]);
    }

    // draw it
    std::string content;
    if (info->outformat == CF_FILTER_OUT_FORMAT_PDF)
    {
      content.append(QUtil::double_to_string(info->page_width) + " 0 0 " +
                     QUtil::double_to_string(info->page_height) + " 0 0 cm\n");
      content.append("/I Do\n");
    }
    else if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
    {
      std::string res = info->pclm_source_resolution_default;

      // resolution is in dpi, so remove the last three characters from
      // resolution string to get resolution integer
      unsigned resolution_integer = std::stoi(res.substr(0, res.size() - 3));
      double d = (double)DEFAULT_PDF_UNIT / resolution_integer;
      content.append(QUtil::double_to_string(d) + " 0 0 " +
		     QUtil::double_to_string(d) + " 0 0 cm\n");
      unsigned yAnchor = info->height;
      for (unsigned i = 0; i < info->pclm_num_strips; i ++)
      {
        yAnchor -= info->pclm_strip_height[i];
        content.append("/P <</MCID 0>> BDC q\n");
        content.append(QUtil::int_to_string(info->width) + " 0 0 " +
                        QUtil::int_to_string(info->pclm_strip_height[i]) +
                        " 0 " + QUtil::int_to_string(yAnchor) + " cm\n");
        content.append("/Image" +
                       int_to_fwstring(i,
				       num_digits(info->pclm_num_strips - 1)) +
                       " Do Q\n");
      }
    }

    QPDFObjectHandle page_contents = info->page.getKey("/Contents");
    if (info->outformat == CF_FILTER_OUT_FORMAT_PDF)
      page_contents.replaceStreamData(content, QPDFObjectHandle::newNull(),
				      QPDFObjectHandle::newNull());
    else if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
      page_contents.getArrayItem(0).replaceStreamData(content,
						   QPDFObjectHandle::newNull(),
						   QPDFObjectHandle::newNull());

    // bookkeeping
    info->page_data = PointerHolder<Buffer>();
    info->pclm_strip_data.clear();

    return 0;
}


/* Perform modifications to PDF if color space conversions are needed */      
static int prepare_pdf_page(struct pdf_info * info, unsigned width, unsigned height,
		     unsigned bpl, unsigned bpp, unsigned bpc,
		     std::string render_intent, cups_cspace_t color_space,
		     rastertopdf_doc_t *doc)
{
#define IMAGE_CMYK_8   (bpp == 32 && bpc == 8)
#define IMAGE_CMYK_16  (bpp == 64 && bpc == 16)
#define IMAGE_RGB_8    (bpp == 24 && bpc == 8)
#define IMAGE_RGB_16   (bpp == 48 && bpc == 16)
#define IMAGE_WHITE_1  (bpp == 1 && bpc == 1)
#define IMAGE_WHITE_8  (bpp == 8 && bpc == 8)
#define IMAGE_WHITE_16 (bpp == 16 && bpc == 16)    

    int error = 0;
    pdf_convert_function fn = convert_pdf_no_conversion;
    cmsColorSpaceSignature css;

    /* Register available raster information into the PDF */
    info->width = width;
    info->height = height;
    info->line_bytes = bpl;
    info->bpp = bpp;
    info->bpc = bpc;
    info->render_intent = render_intent;
    info->color_space = color_space;
    if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
    {
      info->pclm_num_strips =
	(height / info->pclm_strip_height_preferred) +
	(height % info->pclm_strip_height_preferred ? 1 : 0);
      info->pclm_strip_height.resize(info->pclm_num_strips);
      info->pclm_strip_data.resize(info->pclm_num_strips);
      for (size_t i = 0; i < info->pclm_num_strips; i ++)
      {
        info->pclm_strip_height[i] =
	  info->pclm_strip_height_preferred < height ?
	    info->pclm_strip_height_preferred : height;
        height -= info->pclm_strip_height[i];
      }
    }

    /* Invert grayscale by default */
    if (color_space == CUPS_CSPACE_K)
      fn = convert_pdf_invert_colors;

    if (doc->colorProfile != NULL) {
      css = cmsGetColorSpace(doc->colorProfile);

      // Convert image and PDF color space to an embedded ICC Profile color
      // space
      switch(css) {
        // Convert PDF to Grayscale when using a gray profile
        case cmsSigGrayData:
          if (color_space == CUPS_CSPACE_CMYK)
            fn = convert_pdf_cmyk_8_to_white_8;
          else if (color_space == CUPS_CSPACE_RGB) 
            fn = convert_pdf_rgb_8_to_white_8;
          else              
            fn = convert_pdf_invert_colors;
          info->color_space = CUPS_CSPACE_K;
          break;
        // Convert PDF to RGB when using an RGB profile
        case cmsSigRgbData:
          if (color_space == CUPS_CSPACE_CMYK) 
            fn = convert_pdf_cmyk_8_to_rgb_8;
          else if (color_space == CUPS_CSPACE_K) 
            fn = convert_pdf_white_8_to_rgb_8;
          info->color_space = CUPS_CSPACE_RGB;
          break;
        // Convert PDF to CMYK when using an RGB profile
        case cmsSigCmykData:
          if (color_space == CUPS_CSPACE_RGB)
            fn = convert_pdf_rgb_8_to_cmyk_8;
          else if (color_space == CUPS_CSPACE_K) 
            fn = convert_pdf_white_8_to_cmyk_8;
          info->color_space = CUPS_CSPACE_CMYK;
          break;
        default:
          if (doc->logfunc)
	    doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
			 "cfFilterRasterToPDF: Unable to convert PDF from profile.");
          doc->colorProfile = NULL;
          error = 1;
      }
      // Perform conversion of an image color space 
    } else if (!doc->cm_disabled) {       
      switch (color_space) {
         // Convert image to CMYK
         case CUPS_CSPACE_CMYK:
           if (IMAGE_RGB_8)
             fn = convert_pdf_rgb_8_to_cmyk_8;  
           else if (IMAGE_RGB_16)
             fn = convert_pdf_no_conversion;
           else if (IMAGE_WHITE_8)
             fn = convert_pdf_white_8_to_cmyk_8;  
           else if (IMAGE_WHITE_16) 
             fn = convert_pdf_no_conversion;
           break;
         // Convert image to RGB
         case CUPS_CSPACE_ADOBERGB:
         case CUPS_CSPACE_RGB:
         case CUPS_CSPACE_SRGB:
           if (IMAGE_CMYK_8)
             fn = convert_pdf_cmyk_8_to_rgb_8;
           else if (IMAGE_CMYK_16)
             fn = convert_pdf_no_conversion;  
           else if (IMAGE_WHITE_8)
             fn = convert_pdf_white_8_to_rgb_8;
           else if (IMAGE_WHITE_16) 
             fn = convert_pdf_no_conversion;       
           break;
         // Convert image to Grayscale
         case CUPS_CSPACE_SW:
         case CUPS_CSPACE_K:
           if (IMAGE_CMYK_8)
             fn = convert_pdf_cmyk_8_to_white_8;
           else if (IMAGE_CMYK_16)
             fn = convert_pdf_no_conversion;
           else if (IMAGE_RGB_8) 
             fn = convert_pdf_rgb_8_to_white_8;
           else if (IMAGE_RGB_16) 
             fn = convert_pdf_no_conversion;
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
             fn = convert_pdf_no_conversion;
             break;
         default:
           if (doc->logfunc)
	     doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
			  "cfFilterRasterToPDF: Color space not supported.");
           error = 1;
           break;
      }
   } 

   if (!error)
     fn(info, doc);

   return error;
}

static int add_pdf_page(struct pdf_info * info, int pagen, unsigned width,
		 unsigned height, int bpp, int bpc, int bpl,
		 std::string render_intent, cups_cspace_t color_space,
		 unsigned xdpi, unsigned ydpi, rastertopdf_doc_t *doc)
{
    try {
        if (finish_page(info, doc)) // any active
        return 1;

        prepare_pdf_page(info, width, height, bpl, bpp, 
                         bpc, render_intent, color_space, doc);

        if (info->height > (std::numeric_limits<unsigned>::max() /
			    info->line_bytes)) {
            if (doc->logfunc)
	      doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
			   "cfFilterRasterToPDF: Page too big");
            return 1;
        }
        if (info->outformat == CF_FILTER_OUT_FORMAT_PDF)
          info->page_data =
	    PointerHolder<Buffer>(new Buffer(info->line_bytes * info->height));
        else if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
        {
          // reserve space for PCLm strips
          for (size_t i = 0; i < info->pclm_num_strips; i ++)
            info->pclm_strip_data[i] =
	      PointerHolder<Buffer>(new Buffer(info->line_bytes *
					       info->pclm_strip_height[i]));
        }

        QPDFObjectHandle page = QPDFObjectHandle::parse(
            "<<"
            "  /Type /Page"
            "  /Resources <<"
            "    /XObject << >> "
            "  >>"
            "  /MediaBox null "
            "  /Contents null "
            ">>");

        // Convert to pdf units
        info->page_width=((double)info->width/xdpi)*DEFAULT_PDF_UNIT;
        info->page_height=((double)info->height/ydpi)*DEFAULT_PDF_UNIT;
        if (info->outformat == CF_FILTER_OUT_FORMAT_PDF)
        {
          page.replaceKey("/Contents",QPDFObjectHandle::newStream(&info->pdf));
	                                  // data will be provided later
          page.replaceKey("/MediaBox",make_real_box(0,0,info->page_width,
						  info->page_height));
        }
        else if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
        {
          page.replaceKey("/Contents",
            QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>
				       (1,QPDFObjectHandle::newStream
					(&info->pdf))));

          // box with dimensions rounded off to the nearest integer
          page.replaceKey("/MediaBox",
			  make_integer_box(0,0,info->page_width + 0.5,
					 info->page_height + 0.5));
        }
    
        info->page = info->pdf.makeIndirectObject(page); // we want to keep a
	                                                 // reference
        info->pdf.addPage(info->page, false);
    } catch (std::bad_alloc &ex) {
        if (doc->logfunc)
	  doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		       "cfFilterRasterToPDF: Unable to allocate page data");
        return 1;
    } catch (...) {
        return 1;
    }

    return 0;
}

static int close_pdf_file(struct pdf_info * info, rastertopdf_doc_t *doc)
{
    try {
        if (finish_page(info, doc)) // any active
        return 1;
        QPDFWriter output(info->pdf,NULL);
        output.setOutputFile("pdf", doc->outputfp, false);
//        output.setMinimumPDFVersion("1.4");
        if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM)
          output.setPCLm(true);
        output.write();
    } catch (...) {
        return 1;
    }

    return 0;
}

static void pdf_set_line(struct pdf_info * info, unsigned line_n,
		  unsigned char *line, rastertopdf_doc_t *doc)
{
    if(line_n > info->height)
    {
        if (doc->logfunc)
	  doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		       "cfFilterRasterToPDF: Bad line %d", line_n);
        return;
    }

    if (info->outformat == CF_FILTER_OUT_FORMAT_PCLM) {
      // copy line data into appropriate pclm strip
      size_t strip_num = line_n / info->pclm_strip_height_preferred;
      unsigned line_strip = line_n -
	strip_num * info->pclm_strip_height_preferred;
      memcpy(((info->pclm_strip_data[strip_num])->getBuffer() +
	      (line_strip*info->line_bytes)), line, info->line_bytes);
    } else {
      memcpy((info->page_data->getBuffer() + (line_n * info->line_bytes)),
	     line, info->line_bytes);
    }
}

static int convert_raster(cups_raster_t *ras, unsigned width, unsigned height,
		   int bpp, int bpl, struct pdf_info * info,
		   rastertopdf_doc_t *doc)
{
    // We should be at raster start
    int i;
    unsigned cur_line = 0;
    unsigned char *PixelBuffer, *ptr = NULL, *buff;

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
        doc->bit_function(PixelBuffer, ptr,  bpl);

        // write lines and color convert when necessary
 	pdf_set_line(info, cur_line, doc->conversion_function(PixelBuffer,
							      buff, width),
		     doc);
	++cur_line;
    }
    while(cur_line < height);

    free(buff);
    free(PixelBuffer);

    return 0;
}

static int set_profile(const char * path, rastertopdf_doc_t *doc) 
{
    if (path != NULL) 
      doc->colorProfile = cmsOpenProfileFromFile(path,"r");

    if (doc->colorProfile != NULL) {
      if (doc->logfunc)
	doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPDF: Load profile successful.");
      return 0;
    }
    else {
      if (doc->logfunc)
	doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPDF: Unable to load profile.");
      return 1;
    }
}

int                         /* O - Error status */
cfFilterRasterToPDF(int inputfd,    /* I - File descriptor input stream */
       int outputfd,        /* I - File descriptor output stream */
       int inputseekable,   /* I - Is input stream seekable? (unused) */
       cf_filter_data_t *data, /* I - Job and printer data */
       void *parameters)    /* I - Filter-specific parameters (outformat) */
{
  int i;
  rastertopdf_doc_t	doc;		/* Document information */
  FILE          *outputfp;              /* Output data stream */
  cf_filter_out_format_t outformat; /* Output format */
  int Page, empty = 1;
  cf_cm_calibration_t    cm_calibrate;   /* Status of CUPS color management
					 ("on" or "off") */
  struct pdf_info pdf;
  cups_raster_t	*ras;		/* Raster stream for printing */
  cups_page_header2_t	header;		/* Page header from file */
  ppd_attr_t    *attr;  /* PPD attribute */
  ipp_t *printer_attrs = data->printer_attrs; /* Printer attributes from printer data*/
  ipp_attribute_t *ipp_attr; /* Printer attribute*/
  const char*         profile_name = NULL;	/* IPP Profile Name */
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;
  int total_attrs;
  char buf[1024];
  const char *kw;


  (void)inputseekable;

  if (parameters)
  {
    outformat = *(cf_filter_out_format_t *)parameters;
    if (outformat != CF_FILTER_OUT_FORMAT_PCLM)
      outformat = CF_FILTER_OUT_FORMAT_PDF;
  }
  else
    outformat = CF_FILTER_OUT_FORMAT_PDF;
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterRasterToPDF: OUTFORMAT=\"%s\"",
	       outformat == CF_FILTER_OUT_FORMAT_PDF ? "PDF" : "PCLM");

 /*
  * Open the output data stream specified by the outputfd...
  */

  if ((outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: Unable to open output data stream.");
    }

    return (1);
  }

  doc.outputfp = outputfp;
  /* Logging function */
  doc.logfunc = log;
  doc.logdata = ld;
  /* Job-is-canceled function */
  doc.iscanceledfunc = iscanceled;
  doc.iscanceleddata = icd;

  /* support the CUPS "cm-calibration" option */ 
  cm_calibrate = cfCmGetCupsColorCalibrateMode(data, data->options, data->num_options);

  if (outformat == CF_FILTER_OUT_FORMAT_PCLM ||
      cm_calibrate == CF_CM_CALIBRATION_ENABLED)
    doc.cm_disabled = 1;
  else
    doc.cm_disabled = cfCmIsPrinterCmDisabled(data);

  if (outformat == CF_FILTER_OUT_FORMAT_PCLM && data->ppd == NULL
        && printer_attrs == NULL )
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
      "cfFilterRasterToPDF: PCLm output:  Neither a PPD file nor printer IPP attributes are supplied, PCLm output not possible.");
    return 1;
  }

  // Transform
  ras = cupsRasterOpen(inputfd, CUPS_RASTER_READ);

  // Process pages as needed...
  Page = 0;

  /* Get PCLm attributes from PPD */
  if (data->ppd && outformat == CF_FILTER_OUT_FORMAT_PCLM)
  {
    char *attr_name = (char *)"cupsPclmStripHeightPreferred";
    if ((attr = ppdFindAttr(data->ppd, attr_name, NULL)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, attr->value);
      pdf.pclm_strip_height_preferred = atoi(attr->value);
    }
    else
      pdf.pclm_strip_height_preferred = 16; /* default strip height */

    attr_name = (char *)"cupsPclmStripHeightSupported";
    if ((attr = ppdFindAttr(data->ppd, attr_name, NULL)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, attr->value);
      pdf.pclm_strip_height_supported.clear();  // remove default value = 16
      std::vector<std::string> vec = split_strings(attr->value, ",");
      for (size_t i = 0; i < vec.size(); i ++)
        pdf.pclm_strip_height_supported.push_back(atoi(vec[i].c_str()));
      vec.clear();
    }

    attr_name = (char *)"cupsPclmRasterBackSide";
    if ((attr = ppdFindAttr(data->ppd, attr_name, NULL)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, attr->value);
      pdf.pclm_raster_back_side = attr->value;
    }

    attr_name = (char *)"cupsPclmSourceResolutionSupported";
    if ((attr = ppdFindAttr(data->ppd, attr_name, NULL)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, attr->value);
      pdf.pclm_source_resolution_supported = split_strings(attr->value, ",");
    }

    attr_name = (char *)"cupsPclmSourceResolutionDefault";
    if ((attr = ppdFindAttr(data->ppd, attr_name, NULL)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, attr->value);
      pdf.pclm_source_resolution_default = attr->value;
    }
    else if (pdf.pclm_source_resolution_supported.size() > 0)
    {
      pdf.pclm_source_resolution_default =
	pdf.pclm_source_resolution_supported[0];
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" missing, taking first item of \"cupsPclmSourceResolutionSupported\" as default resolution",
		   attr_name);
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPDF: PCLm output: PPD file does not contain printer resolution information for PCLm.");
      return 1;
    }

    attr_name = (char *)"cupsPclmCompressionMethodPreferred";
    if ((attr = ppdFindAttr(data->ppd, attr_name, NULL)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: PPD PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, attr->value);
      std::vector<std::string> vec = split_strings(attr->value, ",");

      // get all compression methods supported by the printer
      for (std::vector<std::string>::iterator it = vec.begin();
            it != vec.end(); ++it)
      {
        std::string compression_method = *it;
        for (char& x: compression_method)
          x = tolower(x);
        if (compression_method == "flate")
          pdf.pclm_compression_method_preferred.push_back(FLATE_DECODE);
        else if (compression_method == "rle")
          pdf.pclm_compression_method_preferred.push_back(RLE_DECODE);
        else if (compression_method == "jpeg")
          pdf.pclm_compression_method_preferred.push_back(DCT_DECODE);
      }

    }
    // If the compression methods is none of the above or is erreneous
    // use FLATE as compression method and show a warning.
    if (pdf.pclm_compression_method_preferred.empty())
    {
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "(rastertopclm) Unable parse PPD attribute \"%s\". "
		   "Using FLATE for encoding image streams.", attr_name);
      pdf.pclm_compression_method_preferred.push_back(FLATE_DECODE);
    }
  }
  else if(outformat == CF_FILTER_OUT_FORMAT_PCLM && printer_attrs)
  {
    if (log)
    {
      log(ld, CF_LOGLEVEL_DEBUG, "PCLm-related printer IPP attributes:");
      total_attrs = 0;
      ipp_attr = ippFirstAttribute(printer_attrs);
      while (ipp_attr)
      {
        if (strncmp(ippGetName(ipp_attr), "pclm-", 5) == 0)
        {
          total_attrs ++;
          ippAttributeString(ipp_attr, buf, sizeof(buf));
          log(ld, CF_LOGLEVEL_DEBUG, "  Attr: %s",ippGetName(ipp_attr));
          log(ld, CF_LOGLEVEL_DEBUG, "  Value: %s", buf);
          for (i = 0; i < ippGetCount(ipp_attr); i ++)
            if ((kw = ippGetString(ipp_attr, i, NULL)) != NULL)
	          log(ld, CF_LOGLEVEL_DEBUG, "  Keyword: %s", kw);
	      }
	      ipp_attr = ippNextAttribute(printer_attrs);
      }
      log(ld, CF_LOGLEVEL_DEBUG, "  %d attributes", total_attrs);
    }

    char *attr_name = (char *)"pclm-strip-height-preferred";
    if ((ipp_attr = ippFindAttribute(printer_attrs, attr_name, IPP_TAG_ZERO)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		  "cfFilterRasterToPDF: Printer PCLm attribute \"%s\" with value %d",
		  attr_name, ippGetInteger(ipp_attr, 0));
      pdf.pclm_strip_height_preferred = ippGetInteger(ipp_attr, 0);
    }
    else
      pdf.pclm_strip_height_preferred = 16; /* default strip height */

    attr_name = (char *)"pclm-strip-height-supported";
    if ((ipp_attr = ippFindAttribute(printer_attrs, attr_name, IPP_TAG_ZERO)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
      "cfFilterRasterToPDF: Printer PCLm attribute \"%s\"",
      attr_name);
      pdf.pclm_strip_height_supported.clear();  // remove default value = 16
      for (int i = 0; i < ippGetCount(ipp_attr); i ++)
        pdf.pclm_strip_height_supported.push_back(ippGetInteger(ipp_attr, i));
    }

    attr_name = (char *)"pclm-raster-back-side";
    if ((ipp_attr = ippFindAttribute(printer_attrs, attr_name, IPP_TAG_ZERO)) != NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
      "cfFilterRasterToPDF: Printer PCLm attribute \"%s\" with value \"%s\"",
      attr_name, ippGetString(ipp_attr, 0, NULL));
      pdf.pclm_raster_back_side = ippGetString(ipp_attr, 0, NULL);
    }

    attr_name = (char *)"pclm-source-resolution-supported";
    if ((ipp_attr = ippFindAttribute(printer_attrs, attr_name, IPP_TAG_ZERO)) != NULL)
    {
      ippAttributeString(ipp_attr, buf, sizeof(buf));
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: Printer PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, buf);
      pdf.pclm_source_resolution_supported = split_strings(buf, ",");
    }

    attr_name = (char *)"pclm-source-resolution-default";
    if ((ipp_attr = ippFindAttribute(printer_attrs, attr_name, IPP_TAG_ZERO)) != NULL)
    {
      ippAttributeString(ipp_attr, buf, sizeof(buf));
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: Printer PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, buf);
      pdf.pclm_source_resolution_default = buf;
    }
    else if (pdf.pclm_source_resolution_supported.size() > 0)
    {
      pdf.pclm_source_resolution_default =
	pdf.pclm_source_resolution_supported[0];
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: Printer PCLm attribute \"%s\" missing, taking first item of \"pclm-source-resolution-supported\" as default resolution",
		   attr_name);
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPDF: PCLm output: Printer IPP attributes do not contain printer resolution information for PCLm.");
      return 1;
    }

    attr_name = (char *)"pclm-compression-method-preferred";
    if ((ipp_attr = ippFindAttribute(printer_attrs, attr_name, IPP_TAG_ZERO)) != NULL)
    {
      ippAttributeString(ipp_attr, buf, sizeof(buf));
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: Printer PCLm attribute \"%s\" with value \"%s\"",
		   attr_name, buf);
      std::vector<std::string> vec = split_strings(buf, ",");

      // get all compression methods supported by the printer
      for (std::vector<std::string>::iterator it = vec.begin();
            it != vec.end(); ++it)
      {
        std::string compression_method = *it;
        for (char& x: compression_method)
          x = tolower(x);
        if (compression_method == "flate")
          pdf.pclm_compression_method_preferred.push_back(FLATE_DECODE);
        else if (compression_method == "rle")
          pdf.pclm_compression_method_preferred.push_back(RLE_DECODE);
        else if (compression_method == "jpeg")
          pdf.pclm_compression_method_preferred.push_back(DCT_DECODE);
      }

    }
    // If the compression methods is none of the above or is erreneous
    // use FLATE as compression method and show a warning.
    if (pdf.pclm_compression_method_preferred.empty())
    {
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "(rastertopclm) Unable parse Printer attribute \"%s\". "
		   "Using FLATE for encoding image streams.", attr_name);
      pdf.pclm_compression_method_preferred.push_back(FLATE_DECODE);
      }
  }

  while (cupsRasterReadHeader2(ras, &header))
  {
    if (iscanceled && iscanceled(icd))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: Job canceled");
      break;
    }

    if (empty)
    {
      empty = 0;
      // We have a valid input page, so create PDF file
      if (create_pdf_file(&pdf, outformat) != 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterRasterToPDF: Unable to create PDF file");
	return 1;
      }
    }

    // Write a status message with the page number
    Page ++;
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterRasterToPDF: Starting page %d.", Page);

    // Use "profile=profile_name.icc" to embed 'profile_name.icc' into the PDF
    // for testing. Forces color management to enable.
    if (outformat == CF_FILTER_OUT_FORMAT_PDF &&
        (profile_name = cupsGetOption("profile", data->num_options,
				      data->options)) != NULL) {
      set_profile(profile_name, &doc);
      doc.cm_disabled = 0;
    }
    if (doc.colorProfile != NULL)       
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPDF: TEST ICC Profile specified (color "
		   "management forced ON): \n[%s]", profile_name);

    // Add a new page to PDF file
    if (add_pdf_page(&pdf, Page, header.cupsWidth, header.cupsHeight,
          header.cupsBitsPerPixel, header.cupsBitsPerColor, 
          header.cupsBytesPerLine, header.cupsRenderingIntent, 
                      header.cupsColorSpace, header.HWResolution[0],
          header.HWResolution[1], &doc) != 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		    "cfFilterRasterToPDF: Unable to start new PDF page");
      return 1;
    }

    // Write the bit map into the PDF file
    if (convert_raster(ras, header.cupsWidth, header.cupsHeight,
      header.cupsBitsPerPixel, header.cupsBytesPerLine, 
      &pdf, &doc) != 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPDF: Failed to convert page bitmap");
      return 1;
    }
  }

  if (empty)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPDF: Input is empty, outputting empty file.");
    cupsRasterClose(ras);
    return 0;
  }

  close_pdf_file(&pdf, &doc); // output to outputfp

  if (doc.colorProfile != NULL) {
    cmsCloseProfile(doc.colorProfile);
  }

  cupsRasterClose(ras);
  fclose(outputfp);

  return (Page == 0);
}

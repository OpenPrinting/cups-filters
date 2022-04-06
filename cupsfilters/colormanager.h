/*
Copyright (c) 2014, Joseph Simon

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

MIT Open Source License  -  http://www.opensource.org/

*/


#ifndef _CUPS_FILTERS_COLORMANAGER_H_
#  define _CUPS_FILTERS_COLORMANAGER_H_


/* "Color Manager" -- Color management interface for cups-filters */


#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


#include <cups/raster.h>
#include <ppd/ppd.h>
#include <cupsfilters/filter.h>


#define CF_CM_CALIBRATION_STRING "cm-calibration"  /* String for "Color
						      Calibration Mode" */


/* Enum for status of CUPS color calibration */
typedef enum cf_cm_calibration_e
{ 
  CF_CM_CALIBRATION_DISABLED = 0,                  /* "cm-calibration" option
						      not found */
  CF_CM_CALIBRATION_ENABLED = 1                    /* "cm-calibration" found */
} cf_cm_calibration_t;



/*
 * Prototypes
 */


extern 
cf_cm_calibration_t cfCmGetCupsColorCalibrateMode(cf_filter_data_t *data,
						  cups_option_t *options,
						  int num_options);

extern int cfCmGetPrinterIccProfile(cf_filter_data_t *data,
				    char **icc_profile,
				    ppd_file_t *ppd);

extern int cfCmIsPrinterCmDisabled(cf_filter_data_t *data);

extern double* cfCmGammaAdobeRGB(void);
extern double* cfCmGammaSGray(void);

extern double* cfCmWhitePointAdobeRGB(void);
extern double* cfCmWhitePointSGray(void);

extern double* cfCmMatrixAdobeRGB(void);
extern double* cfCmBlackPointDefault(void);


#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_COLORMANAGER_H_ */

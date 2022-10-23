//
// "Color Manager" - Color management interface for libcupsfilters.
//
// Copyright (c) 2014, Joseph Simon
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//


#ifndef _CUPS_FILTERS_COLORMANAGER_H_
#  define _CUPS_FILTERS_COLORMANAGER_H_


#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


#include <cups/raster.h>
#include <cupsfilters/filter.h>


#define CF_CM_CALIBRATION_STRING "cm-calibration"  // String for "Color
						   // Calibration Mode"


// Enum for status of CUPS color calibration
typedef enum cf_cm_calibration_e
{ 
  CF_CM_CALIBRATION_DISABLED = 0,                  // "cm-calibration" option
						   // not found
  CF_CM_CALIBRATION_ENABLED = 1                    // "cm-calibration" found
} cf_cm_calibration_t;


//
// Prototypes
//

extern 
cf_cm_calibration_t cfCmGetCupsColorCalibrateMode(cf_filter_data_t *data);

extern int cfCmGetPrinterIccProfile(cf_filter_data_t *data,
				    const char *color_space,
				    const char *media_type,
				    int x_res,
				    int y_res,
				    char **icc_profile);

extern int cfCmIsPrinterCmDisabled(cf_filter_data_t *data);

extern double* cfCmGammaAdobeRGB(void);
extern double* cfCmGammaSGray(void);

extern double* cfCmWhitePointAdobeRGB(void);
extern double* cfCmWhitePointSGray(void);

extern double* cfCmMatrixAdobeRGB(void);
extern double* cfCmBlackPointDefault(void);


#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_COLORMANAGER_H_

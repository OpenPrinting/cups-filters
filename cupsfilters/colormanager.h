/* Author: Joseph Simon */


#ifndef _CUPS_FILTERS_COLORMANAGER_H_
#  define _CUPS_FILTERS_COLORMANAGER_H_


/* "Color Manager" -- Color-management interface for cups-filters */


#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


#include <cups/raster.h>


#define CM_CALIBRATION_STRING "cm-calibration"        /* Option for Color Calibration Mode */


/* Enum for status of CUPS color calibration */
typedef enum cm_calibration_e
{ 
  CM_CALIBRATION_DISABLED = 0, 
  CM_CALIBRATION_ENABLED = 1
} cm_calibration_t;


/*
 * Functions to handle color management routines throughout cups-filters
 */


extern 
cm_calibration_t    cmGetCupsColorCalibrateMode       (cups_option_t* /*options*/,
                                                             int /*num_options*/);

extern int          cmGetPrinterIccProfile            (const char*  /*printer_id*/,
                                                       const char** /*icc_profile*/,
                                                       ppd_file_t*  /*ppd*/);

extern int          cmIsPrinterCmDisabled             (const char* /*printer_id*/);


#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_COLORMANAGER_H_ */

/* Author: Joseph Simon */


#ifndef _CUPS_FILTERS_COLORMANAGER_H_
#  define _CUPS_FILTERS_COLORMANAGER_H_


/* "Color Manager" -- Color management interface for cups-filters */


#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


#include <cups/raster.h>



#define CM_CALIBRATION_STRING "cm-calibration"       /* String for "Color Calibration Mode" */


/* Enum for status of CUPS color calibration */
typedef enum cm_calibration_e
{ 
  CM_CALIBRATION_DISABLED = 0,                       /* "cm-calibration" option not found */
  CM_CALIBRATION_ENABLED = 1                         /* "cm-calibration" found */
} cm_calibration_t;



/*
 * Prototypes
 */


extern 
cm_calibration_t    cmGetCupsColorCalibrateMode       (cups_option_t *options,
                                                       int num_options);

extern int          cmGetPrinterIccProfile            (const char *printer_id,
                                                       char **icc_profile,
                                                       ppd_file_t *ppd);

extern int          cmIsPrinterCmDisabled             (const char *printer_id);

extern double*      cmGammaAdobeRgb                   (void);
extern double*      cmGammaSGray                      (void);

extern double*      cmWhitePointAdobeRgb              (void);
extern double*      cmWhitePointSGray                 (void);

extern double*      cmMatrixAdobeRgb                  (void);
extern double*      cmBlackPointDefault               (void);



#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_COLORMANAGER_H_ */

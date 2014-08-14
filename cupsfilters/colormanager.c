/* Author: Joseph Simon */


#include "colormanager.h"


/* Private functions */
static int      _get_colord_printer_cm_status   (const char* printer_id);
static int      _get_colord_profile             (const char* printer_id, 
                                                 const char** profile,
                                                 ppd_file_t* ppd);
static int      _get_ppd_fallback_profile       (ppd_file_t* ppd, const char** profile);



/*
 * Public functions
 */


/* Get printer color management status from the system's color manager */
int          
cmIsPrinterCmDisabled(const char* printer_name)
{
    int is_cm_off = 0;

    is_cm_off = _get_colord_printer_cm_status(printer_name);

    return is_cm_off;
}

/* Get printer ICC profile from the system's color manager */
int 
cmGetPrinterIccProfile(const char* printer_name,
                       const char** icc_profile,
                       ppd_file_t* ppd)
{
    int profile_set = 0;

    profile_set = _get_colord_profile(printer_name, icc_profile, ppd);

    if (!profile_set)
      profile_set = _get_ppd_fallback_profile(ppd, icc_profile);

    fprintf(stdout, "DEBUG: ICC Profile: %s\n", *icc_profile ?
        *icc_profile : "None");

    return profile_set;
}

/* Find the cm-calibration CUPS option */
cm_calibration_t    
cmGetCupsColorCalibrateMode(cups_option_t * options,
                            int num_options)
{
    cm_calibration_t status;

    if (cupsGetOption(CM_CALIBRATION_STRING, num_options, options) != NULL)
      status = CM_CALIBRATION_ENABLED;
    else
      status = CM_CALIBRATION_DISABLED;

    fprintf(stdout, "DEBUG: Color Management: %s\n", status ?
           "Calibration Mode/Enabled" : "Calibration Mode/Off");

    return status;
}


/*
 * Private functions
 */


int _get_colord_printer_cm_status(const char* printer_id)
{
    if (printer_id == NULL)
      return -1;

    int is_printer_cm_disabled = 0;

    if (strcmp(printer_id, "cups-(null)") != 0)
      is_printer_cm_disabled = colord_get_inhibit_for_device_id (printer_id);

    return is_printer_cm_disabled;
}

int _get_colord_profile(const char* printer_id, const char** profile, ppd_file_t* ppd)
{
    if (printer_id == NULL || *profile == 0 || ppd == NULL)
      return -1;

    int is_profile_set = 0;
    
    const char **qualifier = NULL;
    const char *icc_profile = NULL;

    qualifier = colord_get_qualifier_for_ppd(ppd);

    if (qualifier != NULL)
      icc_profile = colord_get_profile_for_device_id (printer_id, qualifier);
    else 
      *profile = 0;

    if (icc_profile) {
      *profile = strdup(icc_profile);
      is_profile_set = 1;
    }

    return is_profile_set;
}

/* TODO Finish writing PPD profile function */
int _get_ppd_fallback_profile(ppd_file_t* ppd, const char** profile)
{
    int is_profile_set = 0;

    return is_profile_set;
}


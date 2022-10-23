//
// "Color Manager" - Color management interface for libcupsfilters.
//
// Copyright (c) 2011-2013, Richard Hughes
// Copyright (c) 2014, Joseph Simon
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//


#include "colormanager.h"
#include <cupsfilters/colord.h>
#include <cupsfilters/filter.h>
#include <cupsfilters/ipp.h>


#define CM_MAX_FILE_LENGTH 1024


//
// Commonly-used calibration numbers
//

double           adobergb_wp[3] = {0.95045471, 1.0, 1.08905029};
double              sgray_wp[3] = {0.9505, 1, 1.0890};
double        adobergb_gamma[3] = {2.2, 2.2, 2.2};
double           sgray_gamma[1] = {2.2};
double       adobergb_matrix[9] = {0.60974121, 0.20527649, 0.14918518, 
                                   0.31111145, 0.62567139, 0.06321716, 
                                   0.01947021, 0.06086731, 0.74456787};
double    blackpoint_default[3] = {0.0, 0.0, 0.0};


//
// Public functions
//

//
// Get printer color management status from the system's color manager
//

int          
cfCmIsPrinterCmDisabled(cf_filter_data_t *data)
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  int is_printer_cm_disabled = 0;   // color management status flag
  char printer_id[CM_MAX_FILE_LENGTH] = ""; // colord printer id


  // If invalid input, we leave color management alone
  if (data->printer == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Color Manager: Invalid printer name.");
    return (0);
  }

  // Create printer id string for colord
  snprintf(printer_id, CM_MAX_FILE_LENGTH, "cups-%s", data->printer);

  // Check if device is inhibited/disabled in colord 
  is_printer_cm_disabled = cfColordGetInhibitForDeviceID (data, printer_id);

  if (is_printer_cm_disabled)
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Color Manager: Color management disabled by OS.");

  return (is_printer_cm_disabled);
}


//
// Get printer ICC profile from the system's color manager
//

int 
cfCmGetPrinterIccProfile(cf_filter_data_t *data,
			 const char *color_space,
			 const char *media_type,
			 int x_res,
			 int y_res,
			 char **profile)      // ICC Profile Path
{
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  const char *val;
  int   is_profile_set = 0;        // profile-is-found flag
  char  **qualifier = NULL;        // color qualifier strings
  char  *icc_profile = NULL;       // icc profile path
  char  printer_id[CM_MAX_FILE_LENGTH] = ""; // colord printer id 

  if (data->printer == NULL || profile == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "Color Manager: Invalid input - Unable to find profile."); 
    return (-1);
  }

  // Get color qualifier triple
  qualifier = cfColordGetQualifier(data, color_space, media_type,
				   x_res, y_res);

  if (qualifier != NULL)
  {
    // Create printer id string for colord
    snprintf(printer_id, CM_MAX_FILE_LENGTH, "cups-%s", data->printer);

    // Get profile from colord using qualifiers
    icc_profile = cfColordGetProfileForDeviceID(data,
						(const char *)printer_id,
						(const char **)qualifier);
  }

  // Do we have a profile?
  if (icc_profile)
    is_profile_set = 1;
  // If not, get fallback profile from option
  else if ((val = cupsGetOption("cm-fallback-profile",
				data->num_options, data->options)) != NULL &&
	   val[0] != '\0')
  {
    is_profile_set = 1;
    icc_profile = strdup(val);
  }
  else
    icc_profile = NULL;

  // If a profile is found, we give it to the caller    
  if (is_profile_set)
    *profile = strdup(icc_profile);
  else 
    *profile = NULL;

  if (qualifier != NULL)
  {
    for (int i= 0; qualifier[i] != NULL; i++)
      free(qualifier[i]);
    free(qualifier);
  }

  if (icc_profile != NULL)
    free(icc_profile);

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Color Manager: ICC Profile: %s",
	       *profile ? *profile : "None");

  return (is_profile_set);
}


//
// Find the "cm-calibration" CUPS option
//

cf_cm_calibration_t    
cfCmGetCupsColorCalibrateMode(cf_filter_data_t *data)
{
  int 			num_options = 0;
  cups_option_t 	*options = NULL;
  cf_logfunc_t		log = data->logfunc;
  void			*ld = data->logdata;
  cf_cm_calibration_t	status;			// color management status


  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

  // Find the string in CUPS options and
  if (cupsGetOption(CF_CM_CALIBRATION_STRING, num_options, options) != NULL)
    status = CF_CM_CALIBRATION_ENABLED;
  else
    status = CF_CM_CALIBRATION_DISABLED;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Color Manager: %s", status ?
	       "Calibration Mode/Enabled" : "Calibration Mode/Off");

  cupsFreeOptions(num_options, options);
    
  return (status);
}


//
// Accessor functions to return specific calibration data
//

// Gamma values

double *cfCmGammaAdobeRGB(void)
{
  return (adobergb_gamma);
}

double *cfCmGammaSGray(void)
{
  return (sgray_gamma);
}


// Whitepoint values

double *cfCmWhitePointAdobeRGB(void)
{
  return (adobergb_wp);
}

double *cfCmWhitePointSGray(void)
{
  return (sgray_wp);
}


// Adapted primaries matrix

double *cfCmMatrixAdobeRGB(void)
{
  return (adobergb_matrix);
}


// Blackpoint value

double *cfCmBlackPointDefault(void)
{
  return (blackpoint_default);
}

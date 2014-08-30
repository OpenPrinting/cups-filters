/*
Copyright (c) 2011-2013, Richard Hughes
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


#include "colormanager.h"
#include <cupsfilters/colord.h>
//#include <cupsfilters/kmdevices.h>


#define CM_MAX_FILE_LENGTH 1024


/* Private function prototypes */
static int      _get_colord_printer_cm_status   (const char *printer_name);
static char    *_get_colord_printer_id          (const char *printer_name);
static int      _get_colord_profile             (const char *printer_name, 
                                                 char **profile,
                                                 ppd_file_t *ppd);
static char    *_get_ppd_icc_fallback           (ppd_file_t *ppd, 
                                                 char **qualifier);



/* Commonly-used calibration numbers */

double           adobergb_wp[3] = {0.95045471, 1.0, 1.08905029};
double              sgray_wp[3] = {0.9505, 1, 1.0890};
double        adobergb_gamma[3] = {2.2, 2.2, 2.2};
double           sgray_gamma[1] = {2.2};
double       adobergb_matrix[9] = {0.60974121, 0.20527649, 0.14918518, 
                                   0.31111145, 0.62567139, 0.06321716, 
                                   0.01947021, 0.06086731, 0.74456787};
double    blackpoint_default[3] = {0.0, 0.0, 0.0};




/*
 * Public functions
 */


/* Get printer color management status from the system's color manager */
int          
cmIsPrinterCmDisabled(const char *printer_name)    /* dest name */
{

    int is_cm_off = 0;          /* color management status flag */


    /* Request color management status from colord */
    is_cm_off = _get_colord_printer_cm_status(printer_name);

    if (is_cm_off)
      fprintf(stderr,"DEBUG: Color Manager: Color management disabled by OS.\n");

    return is_cm_off;      

}


/* Get printer ICC profile from the system's color manager */
int 
cmGetPrinterIccProfile(const char *printer_name,  /* Printer name (usually "dest" name) */
                       char **icc_profile,        /* ICC Profile Path */
                       ppd_file_t *ppd)           /* Optional PPD file for fallback profile */
{

    int profile_set = 0;        /* 'is profile found' flag */


    /* Request a profile from colord */
    profile_set = _get_colord_profile(printer_name, icc_profile, ppd);

    fprintf(stderr, "DEBUG: Color Manager: ICC Profile: %s\n", *icc_profile ?
        *icc_profile : "None");

    return profile_set;

}


/* Find the "cm-calibration" CUPS option */
cm_calibration_t    
cmGetCupsColorCalibrateMode(cups_option_t *options,    /* Options from CUPS */
                            int num_options)           /* Options from CUPS */
{

    cm_calibration_t status;     /* color management status */


    /* Find the string in CUPS options and */
    if (cupsGetOption(CM_CALIBRATION_STRING, num_options, options) != NULL)
      status = CM_CALIBRATION_ENABLED;
    else
      status = CM_CALIBRATION_DISABLED;

    fprintf(stderr, "DEBUG: Color Manager: %s\n", status ?
           "Calibration Mode/Enabled" : "Calibration Mode/Off");

    return status;

}


/* Functions to return specific calibration data */


/* Gamma values */

double *cmGammaAdobeRgb(void)
{
    return adobergb_gamma;
}
double *cmGammaSGray(void)
{
    return sgray_gamma;
}


/* Whitepoint values */

double *cmWhitePointAdobeRgb(void)
{
    return adobergb_wp;
}
double *cmWhitePointSGray(void)
{
    return sgray_wp;
}


/* Adapted primaries matrix */

double *cmMatrixAdobeRgb(void)
{
    return adobergb_matrix;
}


/* Blackpoint value */

double *cmBlackPointDefault(void)
{
    return blackpoint_default;
}




/*
 * Private functions
 */


char * 
_get_colord_printer_id(const char *printer_name)         /* Dest name */
{

    if (printer_name == NULL) {
      fprintf(stderr, "DEBUG: Color Manager: Invalid printer name.\n");
      return 0;
    }

    /* Create printer id string for colord */
    char* printer_id = (char*)malloc(CM_MAX_FILE_LENGTH);
    snprintf (printer_id, CM_MAX_FILE_LENGTH, "cups-%s", printer_name);

    return printer_id;    

}


int 
_get_colord_printer_cm_status(const char *printer_name)  /* Dest name */
{

    /* If invalid input, we leave color management alone */
    if (printer_name == NULL) {
      fprintf(stderr, "DEBUG: Color Manager: Invalid printer name.\n");
      return 0;
    } else if (!strcmp(printer_name, "(null)"))
      return 0;
 
    int is_printer_cm_disabled = 0;   /* color management status flag */
    char *printer_id = 0;             /* colord printer id string */


    /* Check if device is inhibited/disabled in colord  */
    printer_id = _get_colord_printer_id(printer_name);
    is_printer_cm_disabled = colord_get_inhibit_for_device_id (printer_id);

    if (printer_id != NULL)
      free(printer_id);

    return is_printer_cm_disabled;

}

int 
_get_colord_profile(const char   *printer_name,     /* Dest name */
                    char         **profile,         /* Requested icc profile path */      
                    ppd_file_t   *ppd)              /* PPD file */
{

    if (printer_name == NULL || profile == 0) {
      fprintf(stderr, "DEBUG: Color Manager: Invalid input - Unable to find profile.\n"); 
      return -1;
    }
 
    int   is_profile_set = 0;        /* profile-is-found flag */
    char  **qualifier = NULL;        /* color qualifier strings */
    char  *icc_profile = NULL;       /* icc profile path */
    char  *printer_id = NULL;        /* colord printer id */ 


    /* Get color qualifier triple */
    qualifier = colord_get_qualifier_for_ppd(ppd);

    if (qualifier != NULL) {
      printer_id = _get_colord_printer_id(printer_name);
      /* Get profile from colord using qualifiers */
      icc_profile = colord_get_profile_for_device_id (printer_id, qualifier);
    }

    if (icc_profile) 
      is_profile_set = 1; 
    else if (ppd) {
    /* Get optional fallback PPD profile */
      icc_profile = _get_ppd_icc_fallback(ppd, qualifier);
      if (icc_profile)
        is_profile_set = 1;
    }

    /* If a profile is found, we give it to the caller */    
    if (is_profile_set)
      *profile = strdup(icc_profile);
    else 
      *profile = 0;

    if (printer_id != NULL)
      free(printer_id);

    if (qualifier != NULL) {
      for (int i=0; qualifier[i] != NULL; i++)
        free(qualifier[i]);
      free(qualifier);
    }

    return is_profile_set;

}


#ifndef CUPSDATA
#define CUPSDATA "/usr/share/cups"
#endif

/* From gstoraster */
char *
_get_ppd_icc_fallback (ppd_file_t *ppd, char **qualifier)
{
  char full_path[1024];
  char *icc_profile = NULL;
  char qualifer_tmp[1024];
  const char *profile_key;
  ppd_attr_t *attr;

  /* get profile attr, falling back to CUPS */
  profile_key = "APTiogaProfile";
  attr = ppdFindAttr(ppd, profile_key, NULL);
  if (attr == NULL) {
    profile_key = "cupsICCProfile";
    attr = ppdFindAttr(ppd, profile_key, NULL);
  }

  /* create a string for a quick comparion */
  snprintf(qualifer_tmp, sizeof(qualifer_tmp),
           "%s.%s.%s",
           qualifier[0],
           qualifier[1],
           qualifier[2]);

  /* neither */
  if (attr == NULL) {
    fprintf(stderr, "INFO: Color Manager: no profiles specified in PPD\n");
    goto out;
  }

  /* try to find a profile that matches the qualifier exactly */
  for (;attr != NULL; attr = ppdFindNextAttr(ppd, profile_key, NULL)) {
    fprintf(stderr, "INFO: Color Manager: found profile %s in PPD with qualifier '%s'\n",
            attr->value, attr->spec);

    /* invalid entry */
    if (attr->spec == NULL || attr->value == NULL)
      continue;

    /* expand to a full path if not already specified */
    if (attr->value[0] != '/')
      snprintf(full_path, sizeof(full_path),
               "%s/profiles/%s", CUPSDATA, attr->value);
    else
      strncpy(full_path, attr->value, sizeof(full_path));

    /* check the file exists */
    if (access(full_path, 0)) {
      fprintf(stderr, "INFO: Color Manager: found profile %s in PPD that does not exist\n",
              full_path);
      continue;
    }

    /* matches the qualifier */
    if (strcmp(qualifer_tmp, attr->spec) == 0) {
      icc_profile = strdup(full_path);
      goto out;
    }
  }

  /* no match */
  if (attr == NULL) {
    fprintf(stderr, "INFO: Color Manager: no profiles in PPD for qualifier '%s'\n",
            qualifer_tmp);
    goto out;
  }

out:
  return icc_profile;
}


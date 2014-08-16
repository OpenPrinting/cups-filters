#include "colormanager.h"
#include <cupsfilters/colord.h>
//#include <cupsfilters/kmdevices.h>

#define CM_MAX_FILE_LENGTH 1024


/* Private functions */
static int      _get_colord_printer_cm_status   (const char* printer_name);
static char*    _get_colord_printer_id          (const char* printer_name);
static int      _get_colord_profile             (const char* printer_name, 
                                                 char** profile,
                                                 ppd_file_t* ppd);
static char*    _get_ppd_icc_fallback           (ppd_file_t *ppd, 
                                                 char **qualifier);



/* Commonly-used white point and gamma numbers */
double adobergb_wp[3] = {0.95045471, 1.0, 1.08905029};
double sgray_wp[3] = {0.9505, 1, 1.0890};
double adobergb_gamma[3] = {2.2, 2.2, 2.2};
double sgray_gamma[1] = {2.2};
double adobergb_matrix[9] = {0.60974121, 0.20527649, 0.14918518, 
                             0.31111145, 0.62567139, 0.06321716, 
                             0.01947021, 0.06086731, 0.74456787};
/*
 * Public functions
 */


/* Get printer color management status from the system's color manager */
int          
cmIsPrinterCmDisabled(const char* printer_name)
{
    int is_cm_off = 0;

    is_cm_off = _get_colord_printer_cm_status(printer_name);

    if (is_cm_off)
      fprintf(stderr,"DEBUG: Color Manager: Color management disabled by OS.\n");

    return is_cm_off;
}

/* Get printer ICC profile from the system's color manager */
int 
cmGetPrinterIccProfile(const char* printer_name,
                       char** icc_profile,
                       ppd_file_t* ppd)
{
    int profile_set = 0;

    profile_set = _get_colord_profile(printer_name, icc_profile, ppd);

    fprintf(stderr, "DEBUG: Color Manager: ICC Profile: %s\n", *icc_profile ?
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

    fprintf(stderr, "DEBUG: Color Manager: %s\n", status ?
           "Calibration Mode/Enabled" : "Calibration Mode/Off");

    return status;
}

double* cmGammaAdobeRgb(void)
{
    return adobergb_gamma;
}

double* cmGammaSGray(void)
{
    return sgray_gamma;
}

double* cmWhitePointAdobeRgb(void)
{
    return adobergb_wp;
}

double* cmWhitePointSGray(void)
{
    return sgray_wp;
}

double* cmMatrixAdobeRgb(void)
{
    return adobergb_matrix;
}

/*
 * Private functions
 */


char* _get_colord_printer_id(const char* printer_name)
{
    if (printer_name == NULL) {
      fprintf(stderr, "DEBUG: Color Manager: Invalid printer name.\n");
      return 0;
    }

    char* printer_id = (char*)malloc(CM_MAX_FILE_LENGTH);

    snprintf (printer_id, CM_MAX_FILE_LENGTH, "cups-%s", printer_name);
    return printer_id;    
}


int _get_colord_printer_cm_status(const char* printer_name)
{
    if (printer_name == NULL) {
      fprintf(stderr, "DEBUG: Color Manager: Invalid printer name.\n");
      return 0;
    }
 
    int is_printer_cm_disabled = 0;
    char* printer_id = 0;

    printer_id = _get_colord_printer_id(printer_name);

    if (strcmp(printer_id, "cups-(null)") != 0)
      is_printer_cm_disabled = colord_get_inhibit_for_device_id (printer_id);

    if (printer_id != NULL)
      free(printer_id);

    return is_printer_cm_disabled;
}

int _get_colord_profile(const char* printer_name, 
                        char** profile, 
                        ppd_file_t* ppd)
{
    if (printer_name == NULL || profile == 0) {
      fprintf(stderr, "DEBUG: Color Manager: Invalid input - Unable to find profile.\n"); 
      return -1;
    }
 
    int is_profile_set = 0;  

    char **qualifier = NULL;
    char *icc_profile = NULL;
    char *printer_id = NULL;        

    qualifier = colord_get_qualifier_for_ppd(ppd);

    /* Get profile from colord */
    if (qualifier != NULL) {
      printer_id = _get_colord_printer_id(printer_name);
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
static char *
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


#include <oyranos.h>
#include <oyranos_icc.h>
#include <oyranos_devices.h>
#include <oyProfiles_s.h>
#include <oyObject_s.h>

oyConfig_s * get_device(const char * printer_name)
{
  oyConfig_s * device = 0;
  oyOptions_s * options = 0;

  oyOptions_SetFromText( &options, "//" OY_TYPE_STD "/config/command",
                         "properties", OY_CREATE_NEW );
  oyOptions_SetFromText( &options,
                   "//"OY_TYPE_STD"/config/icc_profile.x_color_region_target",
                         "yes", OY_CREATE_NEW );

  oyDeviceGet( OY_TYPE_STD, "PRINTER", printer_name,
               options, &device );
  
  oyOptions_Release(&options);

  return device;
}

int kmIsPrinterCmOff(const char * printer_name)
{
  int state = 0;
  oyConfig_s * device = 0;
  const char* str = 0;

  // Disable CM if invalid
  if(printer_name == NULL)
    return 1;

  device = get_device(printer_name);

  if (error) 
    state = 1;
  else {
    str = oyConfig_FindString(device, "CM_State", 0);   
    if (!strcmp(str, "Disabled"))
      state = 1;
  }

  return state;
}

const char * kmGetPrinterProfile(const char* printer_name)
{
  int state = 0;
  oyConfig_s * device = 0;
  oyProfile_s * profile = 0;
  const char* profile_filepath = 0;

  if(printer_name == NULL)
    return 0;

  device = get_device(printer_name);

  if (device != NULL)
    profile_filepath = oyGetDeviceProfile( device, options, profile );

  return profile_filepath;
}

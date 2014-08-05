#include <oyranos.h>
#include <oyranos_icc.h>
#include <oyranos_devices.h>
#include <oyProfiles_s.h>
#include <oyObject_s.h>

int kmIsPrinterCMOff(const char * printer_name)
{
  int error = 0,
      state = 0;
  oyConfig_s * device = 0;
  oyOptions_s * options = 0;
  const char* str = 0;

  // Disable CM if invalid
  if(printer_name == NULL)
    return 1;

  oyOptions_SetFromText( &options, "//" OY_TYPE_STD "/config/command",
                         "properties", OY_CREATE_NEW );
  oyOptions_SetFromText( &options,
                   "//"OY_TYPE_STD"/config/icc_profile.x_color_region_target",
                         "yes", OY_CREATE_NEW );

  error = oyDeviceGet( OY_TYPE_STD, "PRINTER", printer_name,
                       options, &device );

  if (error) 
    state = 1;
  else {
    str = oyConfig_FindString(device, "CM_State", 0);   
    if (!strcmp(str, "Disabled"))
      state = 1;
  }

  return state;
}

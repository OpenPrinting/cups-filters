/*
 *   IEEE1284 Device ID support definitions for OpenPrinting CUPS Filters.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPSFILTERS_IEEE1284_H_
#  define _CUPSFILTERS_IEEE1284_H_


/*
 * Include necessary headers.
 */

#  include <config.h>
#  include <cups/cups.h>
#  include <ppd/ppd.h>
#  include <cups/backend.h>
#  include <cups/sidechannel.h>
#  include <string.h>
#  include <stdlib.h>
#  include <errno.h>
#  include <signal.h>
#  include <unistd.h>
#  include <fcntl.h>

#  ifdef __linux
#    include <sys/ioctl.h>
#    include <linux/lp.h>
#    define IOCNR_GET_DEVICE_ID		1
#    define LPIOC_GET_DEVICE_ID(len)	_IOC(_IOC_READ, 'P', IOCNR_GET_DEVICE_ID, len)
#    include <linux/parport.h>
#    include <linux/ppdev.h>
#  endif /* __linux */

#  ifdef __sun
#    ifdef __sparc
#      include <sys/ecppio.h>
#    else
#      include <sys/ioccom.h>
#      include <sys/ecppsys.h>
#    endif /* __sparc */
#  endif /* __sun */


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Prototypes...
 */

extern int	ieee1284GetDeviceID(int fd, char *device_id,
				    int device_id_size,
				    char *make_model,
				    int make_model_size,
				    const char *scheme, char *uri,
				    int uri_size);
extern int	ieee1284GetMakeModel(const char *device_id,
				     char *make_model,
				     int make_model_size);
extern int	ieee1284GetValues(const char *device_id,
				  cups_option_t **values);
extern char	*ieee1284NormalizeMakeAndModel(const char *make_and_model,
					       char *buffer, size_t bufsize);


#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_CUPSFILTERS_IEEE1284_H_ */

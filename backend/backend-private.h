/*
 *   Backend support definitions for OpenPrinting CUPS Filters.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPSFILTERS_BACKEND_PRIVATE_H_
#  define _CUPSFILTERS_BACKEND_PRIVATE_H_


/*
 * Include necessary headers.
 */

#  include <config.h>
#  include <cups/cups.h>
#  include <cups/ppd.h>
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

extern int		backendDrainOutput(int print_fd, int device_fd);
extern int		backendGetDeviceID(int fd, char *device_id,
			                   int device_id_size,
			                   char *make_model,
					   int make_model_size,
					   const char *scheme, char *uri,
					   int uri_size);
extern int		backendGetMakeModel(const char *device_id,
			                    char *make_model,
				            int make_model_size);


#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_CUPSFILTERS_BACKEND_PRIVATE_H_ */


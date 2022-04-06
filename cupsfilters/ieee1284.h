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
#  include <regex.h>

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
 * Types...
 */

/* Bit field to describe how to normalize make/model/device ID strings */
enum cf_ieee1284_normalize_modes_e
{
 CF_IEEE1284_NORMALIZE_COMPARE = 0x01,        /* Optimized for comparing,
						 replacing any sequence of
						 non-alpha-numeric characters
						 by a single separator char,
						 at any letter-number boundary
						 and any camel-case boundary
						 add a single separator char,
						 2 separator chars between
						 make/model/extra,
						 make all letters lowercase (or
						 uppercase) */ 
 CF_IEEE1284_NORMALIZE_IPP = 0x02,            /* Only chars allowed in
						 IPP keywords */
 CF_IEEE1284_NORMALIZE_ENV = 0x04,            /* Environment variable format
					         upparcaser and underscore */
 CF_IEEE1284_NORMALIZE_HUMAN = 0x08,          /* Human-readable, conserves
						 spaces and special characters
						 but does some clean-up */
 CF_IEEE1284_NORMALIZE_LOWERCASE = 0x10,      /* All letters lowercase */
 CF_IEEE1284_NORMALIZE_UPPERCASE = 0x20,      /* All letters uppercase */
 CF_IEEE1284_NORMALIZE_SEPARATOR_SPACE = 0x40,/* Separator char is ' ' */
 CF_IEEE1284_NORMALIZE_SEPARATOR_DASH = 0x80, /* Separator char is '-' */
 CF_IEEE1284_NORMALIZE_SEPARATOR_UNDERSCORE = 0x100,/* Separator char is '_' */
 CF_IEEE1284_NORMALIZE_PAD_NUMBERS = 0x200,   /* Zero-pad numbers in stings
					         to get better list sorting
					         results */
 CF_IEEE1284_NORMALIZE_SEPARATE_COMPONENTS = 0x400,/* In the output buffer put
                                                 '\0' bytes between make,
						 model, and extra, to use
						 as separate strings */
 CF_IEEE1284_NORMALIZE_NO_MAKE_MODEL = 0x800, /* No make/model/extra separation,
					         do not try to identify, add,
					         or clean up manufacturer
						 name */
};
typedef unsigned cf_ieee1284_normalize_modes_t;

/*
 * Prototypes...
 */

extern int    cfIEEE1284GetDeviceID(int fd, char *device_id,
				    int device_id_size,
				    char *make_model,
				    int make_model_size,
				    const char *scheme, char *uri,
				    int uri_size);
extern int    cfIEEE1284GetMakeModel(const char *device_id,
				     char *make_model,
				     int make_model_size);
extern int    cfIEEE1284GetValues(const char *device_id,
				  cups_option_t **values);
extern char   *cfIEEE1284NormalizeMakeModel(const char *make_and_model,
					    const char *make,
					    cf_ieee1284_normalize_modes_t mode,
					    regex_t *extra_regex,
					    char *buffer, size_t bufsize,
					    char **model, char **extra,
					    char **drvname);


#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_CUPSFILTERS_IEEE1284_H_ */

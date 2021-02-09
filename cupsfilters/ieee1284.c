/*
 *   IEEE-1284 Device ID support functions for OpenPrinting CUPS Filters.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   ieee1284GetDeviceID()           - Get the IEEE-1284 device ID string and
 *                                     corresponding URI.
 *   ieee1284GetMakeModel()          - Get the make and model string from the 
 *                                     device ID.
 *   ieee1284GetValues()             - Get 1284 device ID keys and values.
 *   ieee1284NormalizeMakeAndModel() - Normalize a product/make-and-model
 *                                     string.
 */

/*
 * Include necessary headers.
 */

#include <config.h>
#include "ieee1284.h"
#include <cups/cups.h>
#include <string.h>
#include <ctype.h>
#define DEBUG_printf(x)
#define DEBUG_puts(x)


/*
 * 'ieee1284GetDeviceID()' - Get the IEEE-1284 device ID string and
 *                           corresponding URI.
 */

int					/* O - 0 on success, -1 on failure */
ieee1284GetDeviceID(
    int        fd,			/* I - File descriptor */
    char       *device_id,		/* O - 1284 device ID */
    int        device_id_size,		/* I - Size of buffer */
    char       *make_model,		/* O - Make/model */
    int        make_model_size,		/* I - Size of buffer */
    const char *scheme,			/* I - URI scheme */
    char       *uri,			/* O - Device URI */
    int        uri_size)		/* I - Size of buffer */
{
#ifdef __APPLE__ /* This function is a no-op */
  (void)fd;
  (void)device_id;
  (void)device_id_size;
  (void)make_model;
  (void)make_model_size;
  (void)scheme;
  (void)uri;
  (void)uri_size;

  return (-1);

#else /* Get the device ID from the specified file descriptor... */
#  ifdef __linux
  int	length;				/* Length of device ID info */
  int   got_id = 0;
#  endif /* __linux */
#  if defined(__sun) && defined(ECPPIOC_GETDEVID)
  struct ecpp_device_id did;		/* Device ID buffer */
#  endif /* __sun && ECPPIOC_GETDEVID */
  char	*ptr;				/* Pointer into device ID */


  DEBUG_printf(("ieee1284GetDeviceID(fd=%d, device_id=%p, device_id_size=%d, "
                "make_model=%p, make_model_size=%d, scheme=\"%s\", "
		"uri=%p, uri_size=%d)\n", fd, device_id, device_id_size,
		make_model, make_model_size, scheme ? scheme : "(null)",
		uri, uri_size));

 /*
  * Range check input...
  */

  if (!device_id || device_id_size < 32)
  {
    DEBUG_puts("ieee1284GetDeviceID: Bad args!");
    return (-1);
  }

  if (make_model)
    *make_model = '\0';

  if (fd >= 0)
  {
   /*
    * Get the device ID string...
    */

    *device_id = '\0';

#  ifdef __linux
    if (ioctl(fd, LPIOC_GET_DEVICE_ID(device_id_size), device_id))
    {
     /*
      * Linux has to implement things differently for every device it seems.
      * Since the standard parallel port driver does not provide a simple
      * ioctl() to get the 1284 device ID, we have to open the "raw" parallel
      * device corresponding to this port and do some negotiation trickery
      * to get the current device ID.
      */

      if (uri && !strncmp(uri, "parallel:/dev/", 14))
      {
	char	devparport[16];		/* /dev/parportN */
	int	devparportfd,		/* File descriptor for raw device */
		  mode;			/* Port mode */


       /*
	* Since the Linux parallel backend only supports 4 parallel port
	* devices, just grab the trailing digit and use it to construct a
	* /dev/parportN filename...
	*/

	snprintf(devparport, sizeof(devparport), "/dev/parport%s",
		 uri + strlen(uri) - 1);

	if ((devparportfd = open(devparport, O_RDWR | O_NOCTTY)) != -1)
	{
	 /*
	  * Claim the device...
	  */

	  if (!ioctl(devparportfd, PPCLAIM))
	  {
	    fcntl(devparportfd, F_SETFL, fcntl(devparportfd, F_GETFL) | O_NONBLOCK);

	    mode = IEEE1284_MODE_COMPAT;

	    if (!ioctl(devparportfd, PPNEGOT, &mode))
	    {
	     /*
	      * Put the device into Device ID mode...
	      */

	      mode = IEEE1284_MODE_NIBBLE | IEEE1284_DEVICEID;

	      if (!ioctl(devparportfd, PPNEGOT, &mode))
	      {
	       /*
		* Read the 1284 device ID...
		*/

		if ((length = read(devparportfd, device_id,
				   device_id_size - 1)) >= 2)
		{
		  device_id[length] = '\0';
		  got_id = 1;
		}
	      }
	    }

	   /*
	    * Release the device...
	    */

	    ioctl(devparportfd, PPRELEASE);
	  }

	  close(devparportfd);
	}
      }
    }
    else
      got_id = 1;

    if (got_id)
    {
     /*
      * Extract the length of the device ID string from the first two
      * bytes.  The 1284 spec says the length is stored MSB first...
      */

      length = (((unsigned)device_id[0] & 255) << 8) +
	       ((unsigned)device_id[1] & 255);

     /*
      * Check to see if the length is larger than our buffer; first
      * assume that the vendor incorrectly implemented the 1284 spec,
      * and then limit the length to the size of our buffer...
      */

      if (length > device_id_size || length < 14)
	length = (((unsigned)device_id[1] & 255) << 8) +
		 ((unsigned)device_id[0] & 255);

      if (length > device_id_size)
	length = device_id_size;

     /*
      * The length field counts the number of bytes in the string
      * including the length field itself (2 bytes).  The minimum
      * length for a valid/usable device ID is 14 bytes:
      *
      *     <LENGTH> MFG: <MFG> ;MDL: <MDL> ;
      *        2  +   4  +  1  +  5 +  1 +  1
      */

      if (length < 14)
      {
       /*
	* Can't use this device ID, so don't try to copy it...
	*/

	device_id[0] = '\0';
	got_id       = 0;
      }
      else
      {
       /*
	* Copy the device ID text to the beginning of the buffer and
	* nul-terminate.
	*/

	length -= 2;

	memmove(device_id, device_id + 2, length);
	device_id[length] = '\0';
      }
    }
    else
    {
      DEBUG_printf(("ieee1284GetDeviceID: ioctl failed - %s\n",
                    strerror(errno)));
      *device_id = '\0';
    }
#  endif /* __linux */

#   if defined(__sun) && defined(ECPPIOC_GETDEVID)
    did.mode = ECPP_CENTRONICS;
    did.len  = device_id_size - 1;
    did.rlen = 0;
    did.addr = device_id;

    if (!ioctl(fd, ECPPIOC_GETDEVID, &did))
    {
     /*
      * Nul-terminate the device ID text.
      */

      if (did.rlen < (device_id_size - 1))
	device_id[did.rlen] = '\0';
      else
	device_id[device_id_size - 1] = '\0';
    }
#    ifdef DEBUG
    else
      DEBUG_printf(("ieee1284GetDeviceID: ioctl failed - %s\n",
                    strerror(errno)));
#    endif /* DEBUG */
#  endif /* __sun && ECPPIOC_GETDEVID */
  }

 /*
  * Check whether device ID is valid. Turn line breaks and tabs to spaces and
  * reject device IDs with non-printable characters.
  */

  for (ptr = device_id; *ptr; ptr ++)
    if (isspace(*ptr))
      *ptr = ' ';
    else if ((*ptr & 255) < ' ' || *ptr == 127)
    {
      DEBUG_printf(("ieee1284GetDeviceID: Bad device_id character %d.",
                    *ptr & 255));
      *device_id = '\0';
      break;
    }

  DEBUG_printf(("ieee1284GetDeviceID: device_id=\"%s\"\n", device_id));

  if (scheme && uri)
    *uri = '\0';

  if (!*device_id)
    return (-1);

 /*
  * Get the make and model...
  */

  if (make_model)
    ieee1284GetMakeModel(device_id, make_model, make_model_size);

 /*
  * Then generate a device URI...
  */

  if (scheme && uri && uri_size > 32)
  {
    int			num_values;	/* Number of keys and values */
    cups_option_t	*values;	/* Keys and values in device ID */
    const char		*mfg,		/* Manufacturer */
			*mdl,		/* Model */
			*sern;		/* Serial number */
    char		temp[256],	/* Temporary manufacturer string */
			*tempptr;	/* Pointer into temp string */


   /*
    * Get the make, model, and serial numbers...
    */

    num_values = ieee1284GetValues(device_id, &values);

    if ((sern = cupsGetOption("SERIALNUMBER", num_values, values)) == NULL)
      if ((sern = cupsGetOption("SERN", num_values, values)) == NULL)
        sern = cupsGetOption("SN", num_values, values);

    if ((mfg = cupsGetOption("MANUFACTURER", num_values, values)) == NULL)
      mfg = cupsGetOption("MFG", num_values, values);

    if ((mdl = cupsGetOption("MODEL", num_values, values)) == NULL)
      mdl = cupsGetOption("MDL", num_values, values);

    if (mfg)
    {
      if (!strcasecmp(mfg, "Hewlett-Packard"))
        mfg = "HP";
      else if (!strcasecmp(mfg, "Lexmark International"))
        mfg = "Lexmark";
    }
    else
    {
      strncpy(temp, make_model, sizeof(temp) - 1);
      temp[sizeof(temp) - 1] = '\0';

      if ((tempptr = strchr(temp, ' ')) != NULL)
        *tempptr = '\0';

      mfg = temp;
    }

    if (!mdl)
      mdl = "";

    if (!strncasecmp(mdl, mfg, strlen(mfg)))
    {
      mdl += strlen(mfg);

      while (isspace(*mdl & 255))
        mdl ++;
    }

   /*
    * Generate the device URI from the manufacturer, make_model, and
    * serial number strings.
    */

    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, uri_size, scheme, NULL, mfg, 0,
                     "/%s%s%s", mdl, sern ? "?serial=" : "", sern ? sern : "");

    cupsFreeOptions(num_values, values);
  }

  return (0);
#endif /* __APPLE__ */
}


/*
 * 'ieee1284GetMakeModel()' - Get the make and model string from the device ID.
 */

int					/* O - 0 on success, -1 on failure */
ieee1284GetMakeModel(
    const char *device_id,		/* O - 1284 device ID */
    char       *make_model,		/* O - Make/model */
    int        make_model_size)		/* I - Size of buffer */
{
  int		num_values;		/* Number of keys and values */
  cups_option_t	*values;		/* Keys and values */
  const char	*mfg,			/* Manufacturer string */
		*mdl,			/* Model string */
		*des;			/* Description string */


  DEBUG_printf(("ieee1284GetMakeModel(device_id=\"%s\", "
                "make_model=%p, make_model_size=%d)\n", device_id,
		make_model, make_model_size));

 /*
  * Range check input...
  */

  if (!device_id || !*device_id || !make_model || make_model_size < 32)
  {
    DEBUG_puts("ieee1284GetMakeModel: Bad args!");
    return (-1);
  }

  *make_model = '\0';

 /*
  * Look for the description field...
  */

  num_values = ieee1284GetValues(device_id, &values);

  if ((mdl = cupsGetOption("MODEL", num_values, values)) == NULL)
    mdl = cupsGetOption("MDL", num_values, values);

  if (mdl)
  {
   /*
    * Build a make-model string from the manufacturer and model attributes...
    */

    if ((mfg = cupsGetOption("MANUFACTURER", num_values, values)) == NULL)
      mfg = cupsGetOption("MFG", num_values, values);

    if (!mfg || !strncasecmp(mdl, mfg, strlen(mfg)))
    {
     /*
      * Just copy the model string, since it has the manufacturer...
      */

      ieee1284NormalizeMakeAndModel(mdl, NULL, IEEE1284_NORMALIZE_HUMAN,
				    make_model, make_model_size, NULL, NULL);
    }
    else
    {
     /*
      * Concatenate the make and model...
      */

      char	temp[1024];		/* Temporary make and model */

      snprintf(temp, sizeof(temp), "%s %s", mfg, mdl);

      ieee1284NormalizeMakeAndModel(temp, NULL, IEEE1284_NORMALIZE_HUMAN,
				    make_model, make_model_size, NULL, NULL);
    }
  }
  else if ((des = cupsGetOption("DESCRIPTION", num_values, values)) != NULL ||
           (des = cupsGetOption("DES", num_values, values)) != NULL)
  {
   /*
    * Make sure the description contains something useful, since some
    * printer manufacturers (HP) apparently don't follow the standards
    * they helped to define...
    *
    * Here we require the description to be 8 or more characters in length,
    * containing at least one space and one letter.
    */

    if (strlen(des) >= 8)
    {
      const char	*ptr;		/* Pointer into description */
      int		letters,	/* Number of letters seen */
			spaces;		/* Number of spaces seen */


      for (ptr = des, letters = 0, spaces = 0; *ptr; ptr ++)
      {
	if (isspace(*ptr & 255))
	  spaces ++;
	else if (isalpha(*ptr & 255))
	  letters ++;

	if (spaces && letters)
	  break;
      }

      if (spaces && letters)
        ieee1284NormalizeMakeAndModel(des, NULL, IEEE1284_NORMALIZE_HUMAN,
				      make_model, make_model_size, NULL, NULL);
    }
  }

  if (!make_model[0])
  {
   /*
    * Use "Unknown" as the printer make and model...
    */

    strncpy(make_model, "Unknown", make_model_size - 1);
    make_model[make_model_size - 1] = '\0';
  }

  cupsFreeOptions(num_values, values);

  return (0);
}


/*
 * 'ieee1284GetValues()' - Get 1284 device ID keys and values.
 *
 * The returned dictionary is a CUPS option array that can be queried with
 * cupsGetOption and freed with cupsFreeOptions.
 */

int					/* O - Number of key/value pairs */
ieee1284GetValues(
    const char *device_id,		/* I - IEEE-1284 device ID string */
    cups_option_t **values)		/* O - Array of key/value pairs */
{
  int		num_values;		/* Number of values */
  char		key[256],		/* Key string */
		value[256],		/* Value string */
		*ptr;			/* Pointer into key/value */


 /*
  * Range check input...
  */

  if (values)
    *values = NULL;

  if (!device_id || !values)
    return (0);

 /*
  * Parse the 1284 device ID value into keys and values.  The format is
  * repeating sequences of:
  *
  *   [whitespace]key:value[whitespace];
  */

  num_values = 0;
  while (*device_id)
  {
    while (isspace(*device_id))
      device_id ++;

    if (!*device_id)
      break;

    for (ptr = key; *device_id && *device_id != ':'; device_id ++)
      if (ptr < (key + sizeof(key) - 1))
        *ptr++ = *device_id;

    if (!*device_id)
      break;

    while (ptr > key && isspace(ptr[-1]))
      ptr --;

    *ptr = '\0';
    device_id ++;

    while (isspace(*device_id))
      device_id ++;

    if (!*device_id)
      break;

    for (ptr = value; *device_id && *device_id != ';'; device_id ++)
      if (ptr < (value + sizeof(value) - 1))
        *ptr++ = *device_id;

    if (!*device_id)
      break;

    while (ptr > value && isspace(ptr[-1]))
      ptr --;

    *ptr = '\0';
    device_id ++;

    num_values = cupsAddOption(key, value, num_values, values);
  }

  return (num_values);
}

/*
 * 'moverightpart()' - Mark a start position in a string buffer and
 *                     move all characters beginning from there by
 *                     a given amount of characters. Characters will
 *                     get lost when moving to the left, there will
 *                     be undefined character positions when moving
 *                     to the right.
 */

void
moverightpart(
    char       *buffer,	     /* I/O - String buffer */
    size_t     bufsize,	     /* I   - Size of string buffer */
    char       *start_pos,   /* I   - Start of part to be moved */
    int        num_chars)    /* I   - Move by how many characters? */
{
  int bytes_to_be_moved,
      buf_space_available;

  if (num_chars == 0)
    return;

  buf_space_available = bufsize - (start_pos - buffer);
  bytes_to_be_moved = strlen(start_pos) + 1;

  if (num_chars > 0)
  {
    if (bytes_to_be_moved + num_chars > buf_space_available)
      bytes_to_be_moved = buf_space_available - num_chars;
    memmove(start_pos + num_chars, start_pos, bytes_to_be_moved);
  }
  else
  {
    bytes_to_be_moved += num_chars;
    memmove(start_pos, start_pos - num_chars, bytes_to_be_moved);
  }
}

/*
 * 'ieee1284NormalizeMakeAndModel()' - Normalize a product/make-and-model
 *                                     string.
 *
 * This function tries to undo the mistakes made by many printer manufacturers
 * to produce a clean make-and-model string we can use.
 */

char *					/* O - Normalized make-and-model string
                                           or NULL on error */
ieee1284NormalizeMakeAndModel(
    const char *make_and_model,		/* I - Original make-and-model string
					       or device ID */
    const char *make,                   /* I - Manufacturer name as hint for
					       correct separation of
					       make_and_model or adding
					       make, or pointer into input
					       string where model name starts
					       or NULL,
					       ignored on device ID with "MFG"
					       field or for NO_MAKE_MODEL */
    ieee1284_normalize_modes_t mode,	/* I - Bit field to describe how to
					       normalize */
    char       *buffer,			/* O - String buffer */
    size_t     bufsize,			/* O - Size of string buffer */
    char       **model,                 /* O - Pointer to where model name
					       starts in buffer or NULL */
    char       **extra)                 /* O - Pointer to where extra info
					       starts in buffer (after comma,
					       semicolon, parenthese) or NULL */
{
  char	*bufptr;			/* Pointer into buffer */
  char  sepchr = ' ';                   /* Word separator character */
  int   compare = 0,                    /* Format for comparing */
        human = 0,                      /* Format for human-readable string */
        lower = 0,                      /* All letters lowercase */
        upper = 0,                      /* All letters uppercase */
        pad = 0,                        /* Zero-pad numbers to 6 digits */
        separate = 0,                   /* Separate components with '\0' */
        nomakemodel = 0;                /* No make/model/extra separation */
  char  *makeptr = NULL,                /* Manufacturer name in buffer */
        *modelptr = NULL,               /* Model name in buffer */
        *extraptr = NULL;               /* Extra info in buffer */
  int   numdigits = 0,
        rightsidemoved = 0;

  if (!make_and_model || !buffer || bufsize < 1)
  {
    if (buffer)
      *buffer = '\0';

    return (NULL);
  }

 /*
  * Check formatting mode...
  */

  if (!mode)
    mode = IEEE1284_NORMALIZE_HUMAN;

  if (mode & IEEE1284_NORMALIZE_SEPARATOR_SPACE)
    sepchr = ' ';
  else if (mode & IEEE1284_NORMALIZE_SEPARATOR_DASH)
    sepchr = '-';
  else if (mode & IEEE1284_NORMALIZE_SEPARATOR_UNDERSCORE)
    sepchr = '_';

  if (mode & IEEE1284_NORMALIZE_LOWERCASE)
    lower = 1;
  if (mode & IEEE1284_NORMALIZE_UPPERCASE)
    upper = 1;

  if (mode & IEEE1284_NORMALIZE_PAD_NUMBERS)
    pad = 1;

  if (mode & IEEE1284_NORMALIZE_SEPARATE_COMPONENTS)
    separate = 1;

  if (mode & IEEE1284_NORMALIZE_NO_MAKE_MODEL)
    nomakemodel = 1;

  if (mode & IEEE1284_NORMALIZE_IPP)
  {
    compare = 1;
    lower = 1;
    upper = 0;
    sepchr = '-';
  }
  else if (mode & IEEE1284_NORMALIZE_ENV)
  {
    compare = 1;
    lower = 0;
    upper = 1;
    sepchr = '_';
  }
  else if (mode & IEEE1284_NORMALIZE_COMPARE)
  {
    compare = 1;
    if (lower == 0 && upper == 0)
      lower = 1;
    if (lower == 1 && upper == 1)
      upper = 0;
  }
  else if (mode & IEEE1284_NORMALIZE_HUMAN)
    human = 1;

 /*
  * Skip leading whitespace...
  */

  while (isspace(*make_and_model))
    make_and_model ++;

 /*
  * Remove parentheses...
  */

  if (make_and_model[0] == '(')
  {
    strncpy(buffer, make_and_model + 1, bufsize - 1);
    buffer[bufsize - 1] = '\0';

    if ((bufptr = strrchr(buffer, ')')) != NULL)
      *bufptr = '\0';
  }

 /*
  * Determine format of input string
  */

  if ((((makeptr = strstr(make_and_model, "MFG:")) != NULL &&
	(makeptr == make_and_model || *(makeptr - 1) == ';')) ||
       ((makeptr = strstr(make_and_model, "MANUFACTURER:")) != NULL &&
	(makeptr == make_and_model || *(makeptr - 1) == ';'))) &&
      (((modelptr = strstr(make_and_model, "MDL:")) != NULL &&
	(modelptr == make_and_model || *(modelptr - 1) == ';')) ||
       ((modelptr = strstr(make_and_model, "MODEL:")) != NULL &&
	(modelptr == make_and_model || *(modelptr - 1) == ';'))))
  {
   /*
    * Input is device ID
    */

    bufptr = buffer;
    while (*makeptr != ':') makeptr ++;
    makeptr ++;
    while (*makeptr != ';' && *makeptr != '\0' &&
	   bufptr < buffer + bufsize - 1)
    {
      *bufptr = *makeptr;
      makeptr ++;
      bufptr ++;
    }
    if (bufptr < buffer + bufsize - 1)
    {
      *bufptr = ' ';
      makeptr ++;
      bufptr ++;
    }
    makeptr = bufptr;
    while (*modelptr != ':') modelptr ++;
    modelptr ++;
    while (*modelptr != ';' && *modelptr != '\0' &&
	   bufptr < buffer + bufsize - 1)
    {
      *bufptr = *modelptr;
      modelptr ++;
      bufptr ++;
    }
    *bufptr = '\0';
    if (!nomakemodel && makeptr != bufptr)
      modelptr = makeptr;
    else
      modelptr = NULL;
    extraptr = NULL;
  }
  else
  {
   /*
    * Input is string of type "MAKE MODEL", "MAKE MODEL, EXTRA", or
    * "MAKE MODEL (EXTRA)"
    */

    modelptr = NULL;
    extraptr = NULL;
    strncpy(buffer, make_and_model, bufsize - 1);
    buffer[bufsize - 1] = '\0';

    if (!nomakemodel)
    {
      if (make)
      {
	if (make >= make_and_model &&
	    make < make_and_model + strlen(make_and_model))
         /*
          * User-supplied pointer where model name starts
          */

	  modelptr = buffer + (make - make_and_model);
	else if (!strncasecmp(buffer, make, strlen(make)) &&
		 isspace(buffer[strlen(make)]))
         /*
	  * User-supplied make string matches start of input
	  */

	  modelptr = buffer + strlen(make) + 1;
	else
	{
         /*
	  * Add user-supplied make string at start of input
	  */

	  snprintf(buffer, bufsize, "%s %s", make, make_and_model);
	  modelptr = buffer + strlen(make) + 1;
	}
      }

     /*
      * Add manufacturers as needed...
      */

      if (modelptr == NULL)
      {
	if (!strncasecmp(make_and_model, "XPrint", 6))
	{
	 /*
	  * Xerox XPrint...
	  */

	  snprintf(buffer, bufsize, "Xerox %s", make_and_model);
	  modelptr = buffer + 6;
	}
	else if (!strncasecmp(make_and_model, "Eastman", 7))
        {
         /*
	  * Kodak...
	  */

	  snprintf(buffer, bufsize, "Kodak %s", make_and_model + 7);
	  modelptr = buffer + 6;
	}
	else if (!strncasecmp(make_and_model, "laserwriter", 11))
	{
         /*
	  * Apple LaserWriter...
	  */

	  snprintf(buffer, bufsize, "Apple LaserWriter%s", make_and_model + 11);
	  modelptr = buffer + 6;
	}
	else if (!strncasecmp(make_and_model, "colorpoint", 10))
        {
         /*
	  * Seiko...
	  */

	  snprintf(buffer, bufsize, "Seiko %s", make_and_model);
	  modelptr = buffer + 6;
	}
	else if (!strncasecmp(make_and_model, "fiery", 5))
        {
         /*
	  * EFI...
	  */

	  snprintf(buffer, bufsize, "EFI %s", make_and_model);
	  modelptr = buffer + 4;
        }
	else if (!strncasecmp(make_and_model, "ps ", 3) ||
		 !strncasecmp(make_and_model, "colorpass", 9))
        {
         /*
	  * Canon...
	  */

	  snprintf(buffer, bufsize, "Canon %s", make_and_model);
	  modelptr = buffer + 6;
	}
	else if (!strncasecmp(make_and_model, "primera", 7))
        {
         /*
	  * Fargo...
	  */

	  snprintf(buffer, bufsize, "Fargo %s", make_and_model);
	  modelptr = buffer + 6;
	}
	else if (!strncasecmp(make_and_model, "designjet", 9) ||
		 !strncasecmp(make_and_model, "deskjet", 7) ||
		 !strncasecmp(make_and_model, "laserjet", 8) ||
		 !strncasecmp(make_and_model, "officejet", 9))
        {
         /*
	  * HP...
	  */

	  snprintf(buffer, bufsize, "HP %s", make_and_model);
	  modelptr = buffer + 3;
	}
	else if (!strncasecmp(make_and_model, "ecosys", 6))
        {
         /*
	  * Kyocera...
	  */

	  snprintf(buffer, bufsize, "Kyocera %s", make_and_model);
	  modelptr = buffer + 8;
	}

       /*
	* Known make names with space
	*/

        else if (strncasecmp(buffer, "konica minolta", 14) &&
		 isspace(buffer[14]))
	  modelptr = buffer + 15;
	else if (strncasecmp(buffer, "fuji xerox", 10) &&
		 isspace(buffer[10]))
	  modelptr = buffer + 11;
	else if (strncasecmp(buffer, "lexmark international", 21) &&
		 isspace(buffer[21]))
	  modelptr = buffer + 22;
	else if (strncasecmp(buffer, "kyocera mita", 12) &&
		 isspace(buffer[12]))
	  modelptr = buffer + 13;

       /*
	* Consider the first space as separation between make and model
	*/

	else
	{
	  modelptr = buffer;
	  while (!isspace(*modelptr) && *modelptr != '\0')
	    modelptr ++;
	}
      }

     /*
      * Adjust modelptr to the actual start of the model name
      */

      if (modelptr)
	while (!isalnum(*modelptr) && *modelptr != '\0')
	  modelptr ++;
    }
  }

  if (!nomakemodel)
  {
   /*
    * Clean up the make...
    */

    bufptr = buffer;
    while ((bufptr = strcasestr(bufptr, "agfa")) != NULL &&
	   (bufptr == buffer || !isalnum(*(bufptr - 1))) &&
	   !isalnum(*(bufptr + 4)))
    {
     /*
      * Replace with AGFA (all uppercase)...
      */

      bufptr[0] = 'A';
      bufptr[1] = 'G';
      bufptr[2] = 'F';
      bufptr[3] = 'A';
      bufptr += 4;
    }

    bufptr = buffer;
    while ((bufptr = strcasestr(bufptr, "Hewlett-Packard")) != NULL)
    {
     /*
      * Replace with "HP"...
      */

      bufptr[0] = 'H';
      bufptr[1] = 'P';
      moverightpart(buffer, bufsize, bufptr + 2, -13);
      if (modelptr >= bufptr + 15)
	modelptr -= 13;
      bufptr += 2;
    }

    bufptr = buffer;
    while ((bufptr = strcasestr(bufptr, "Lexmark International")) != NULL)
    {
     /*
      * Strip "International"...
      */

      moverightpart(buffer, bufsize, bufptr + 7, -14);
      if (modelptr >= bufptr + 21)
	modelptr -= 14;
      bufptr += 7;
    }

    bufptr = buffer;
    while ((bufptr = strcasestr(bufptr, "herk")) != NULL &&
	   (bufptr == buffer || !isalnum(*(bufptr - 1))) &&
	   !isalnum(*(bufptr + 4)))
    {
     /*
      * Replace with LHAG...
      */

      bufptr[0] = 'L';
      bufptr[1] = 'H';
      bufptr[2] = 'A';
      bufptr[3] = 'G';
      bufptr += 4;
    }

    bufptr = buffer;
    while ((bufptr = strcasestr(bufptr, "linotype")) != NULL &&
	   (bufptr == buffer || !isalnum(*(bufptr - 1))) &&
	   !isalnum(*(bufptr + 8)))
    {
     /*
      * Replace with LHAG...
      */

      bufptr[0] = 'L';
      bufptr[1] = 'H';
      bufptr[2] = 'A';
      bufptr[3] = 'G';
      moverightpart(buffer, bufsize, bufptr + 4, -4);
      if (modelptr >= bufptr + 8)
	modelptr -= 4;
      bufptr += 4;
    }

    bufptr = buffer;
    while ((bufptr = strcasestr(bufptr, "TOSHIBA TEC Corp.")) != NULL)
    {
     /*
      * Strip "TEC Corp."...
      */

      moverightpart(buffer, bufsize, bufptr + 7, -10);
      if (modelptr >= bufptr + 17)
	modelptr -= 10;
      bufptr += 7;
    }

   /*
    * Remove repeated manufacturer names...
    */

    while (strncasecmp(buffer, modelptr, modelptr - buffer) == 0)
      moverightpart(buffer, bufsize, modelptr, buffer - modelptr);

   /*
    * Find extra info...
    */

    /* We consider comma, semicolon, or parenthese as the end of the
       model name and the rest of the string as extra info. So we set
       a pointer to this extra info if we find such a character */
    if ((extraptr = strchr(buffer, ',')) == NULL)
      if ((extraptr = strchr(buffer, ';')) == NULL)
	extraptr = strchr(buffer, '(');
    if (extraptr)
    {
      if (!human && *extraptr == '(' &&
	  (bufptr = strchr(extraptr, ')')) != NULL)
	*bufptr = '\0';
      while(!isalnum(*extraptr) && *extraptr != '\0')
	extraptr ++;
    }
  }

 /*
  * Remove trailing whitespace...
  */

  for (bufptr = buffer + strlen(buffer) - 1;
       bufptr >= buffer && isspace(*bufptr);
       bufptr --);

  bufptr[1] = '\0';

 /*
  * Convert string into desired format
  */

  /* Word and component separation, number padding */
  bufptr = buffer;
  while (*bufptr)
  {
    rightsidemoved = 0;
    if (compare) /* Comparison-optimized format */
    {
      if (bufptr > buffer &&
	  ((isdigit(*bufptr) && isalpha(*(bufptr - 1))) || /* a0 boundary */
	   (isalpha(*bufptr) && isdigit(*(bufptr - 1))) || /* 0a boundary */
	   (!separate && modelptr && bufptr == modelptr &&
	    bufptr >= buffer + 2 && /* 2 separator char between make/model */
	    isalnum(*(bufptr - 2)) && !isalnum(*(bufptr - 1))) || 
	   (!separate && extraptr && bufptr == extraptr &&
	    bufptr >= buffer + 2 && /* 2 separator char between model/extra */
	    isalnum(*(bufptr - 2)) && !isalnum(*(bufptr - 1)))))
      {
	/* Insert single separator */
	moverightpart(buffer, bufsize, bufptr, 1);
	*bufptr = sepchr;
	rightsidemoved += 1;
      }
      else if (*bufptr == '+') /* Model names sometimes differ only by a '+' */
      {
	/* Replace with the word "plus" */
	moverightpart(buffer, bufsize, bufptr, 3);
	*bufptr = 'p';
	*(bufptr + 1) = 'l';
	*(bufptr + 2) = 'u';
	*(bufptr + 3) = 's';
	rightsidemoved += 3;
      }
      else if (!isalnum(*bufptr)) /* Space or punctuation character */
      {
	if (bufptr == buffer || !isalnum(*(bufptr - 1)))
	{
	  /* The previous is already a separator, remove this one */
	  moverightpart(buffer, bufsize, bufptr, -1);
	  rightsidemoved -= 1;
	}
	else
	  /* Turn to standard separator character */
	  *bufptr = sepchr;
      }
      if (pad)
      {
	if (isdigit(*bufptr))
	  numdigits ++;
	else if (numdigits &&
		 (!(isdigit(*bufptr)) ||
		  (modelptr && modelptr == bufptr) ||
		  (extraptr && extraptr == bufptr)))
	{
	  if (numdigits < 6)
	  {
	    moverightpart(buffer, bufsize,
			  bufptr - numdigits, 6 - numdigits);
	    memset(bufptr - numdigits, '0', 6 - numdigits);
	    rightsidemoved += 6 - numdigits;
	  }
	  numdigits = 0;
	}
      }
    }
    else if (human) /* Human-readable format */
    {
      if (isspace(*bufptr)) /* White space */
      {
	if (bufptr == buffer || isspace(*(bufptr - 1)))
	{
	  /* The previous is already white space, remove this one */
	  moverightpart(buffer, bufsize, bufptr, -1);
	  rightsidemoved -= 1;
	}
	else
	  /* Turn to standard separator character */
	  *bufptr = sepchr;
      }
    }
    /* Separate component strings with '\0' if requested */
    if (separate && bufptr > buffer)
    {
      if (modelptr && bufptr == modelptr)
	*(bufptr - 1) = '\0';
      if (extraptr && bufptr == extraptr)
	*(bufptr - 1) = '\0';
    }
    /* Correct component start pointers */
    if (modelptr && modelptr >= bufptr)
      modelptr += rightsidemoved;
    if (extraptr && extraptr >= bufptr)
      extraptr += rightsidemoved;
    /* Advance to next character */
    bufptr += (rightsidemoved > 0 ? rightsidemoved :
	       (rightsidemoved < 0 ? 0 : 1));
  }
  /* Remove separator at the end of the string */
  if (bufptr > buffer && *(bufptr - 1) == sepchr)
    *(bufptr - 1) = '\0'; 

  /* Adjustment of upper/lowercase */
  if (lower == 1 || upper == 1)
  {
    bufptr = buffer;
    while (*bufptr)
    {
      if (upper && islower(*bufptr)) *bufptr = toupper(*bufptr);
      if (lower && isupper(*bufptr)) *bufptr = tolower(*bufptr);
      bufptr ++;
    }
  }

 /*
  * Return resulting string and pointers
  */

  if (model) *model = modelptr;
  if (extra) *extra = extraptr;
  return (buffer[0] ? buffer : NULL);
}

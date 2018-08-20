/*
 *   Parallel port backend for OpenPrinting CUPS Filters.
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
 *   main()         - Send a file to the specified parallel port.
 *   drain_output() - Drain pending print data to the device.
 *   list_devices() - List all parallel devices.
 *   run_loop()     - Read and write print and back-channel data.
 *   side_cb()      - Handle side-channel requests...
 */

/*
 * Include necessary headers.
 */

#include "backend-private.h"
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>


/*
 * Local functions...
 */

static int	drain_output(int print_fd, int device_fd);
static void	list_devices(void);
static ssize_t	run_loop(int print_fd, int device_fd, int use_bc,
		         int update_state);
static int	side_cb(int print_fd, int device_fd, int use_bc);


/*
 * 'main()' - Send a file to the specified parallel port.
 *
 * Usage:
 *
 *    printer-uri job-id user title copies options [file]
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments (6 or 7) */
     char *argv[])			/* I - Command-line arguments */
{
  char		method[255],		/* Method in URI */
		hostname[1024],		/* Hostname */
		username[255],		/* Username info (not used) */
		resource[1024],		/* Resource info (device and options) */
		*options;		/* Pointer to options */
  int		port;			/* Port number (not used) */
  int		print_fd,		/* Print file */
		device_fd,		/* Parallel device */
		use_bc;			/* Read back-channel data? */
  int		copies;			/* Number of copies to print */
  ssize_t	tbytes;			/* Total number of bytes written */
  struct termios opts;			/* Parallel port options */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore SIGPIPE signals...
  */

#ifdef HAVE_SIGSET
  sigset(SIGPIPE, SIG_IGN);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, NULL);
#else
  signal(SIGPIPE, SIG_IGN);
#endif /* HAVE_SIGSET */

 /*
  * Check command-line...
  */

  if (argc == 1)
  {
    list_devices();
    return (CUPS_BACKEND_OK);
  }
  else if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]",
            argv[0]);
    return (CUPS_BACKEND_FAILED);
  }

 /*
  * If we have 7 arguments, print the file named on the command-line.
  * Otherwise, send stdin instead...
  */

  if (argc == 6)
  {
    print_fd = 0;
    copies   = 1;
  }
  else
  {
   /*
    * Try to open the print file...
    */

    if ((print_fd = open(argv[6], O_RDONLY)) < 0)
    {
      perror("ERROR: Unable to open print file");
      return (CUPS_BACKEND_FAILED);
    }

    copies = atoi(argv[4]);
  }

 /*
  * Extract the device name and options from the URI...
  */

  httpSeparateURI(HTTP_URI_CODING_ALL, cupsBackendDeviceURI(argv),
                  method, sizeof(method), username, sizeof(username),
		  hostname, sizeof(hostname), &port,
		  resource, sizeof(resource));

 /*
  * See if there are any options...
  */

  if ((options = strchr(resource, '?')) != NULL)
  {
   /*
    * Yup, terminate the device name string and move to the first
    * character of the options...
    */

    *options++ = '\0';
  }

 /*
  * Open the parallel port device...
  */

  fputs("STATE: +connecting-to-device\n", stderr);

  do
  {
#if defined(__linux) || defined(__FreeBSD__)
   /*
    * The Linux and FreeBSD parallel port drivers currently are broken WRT
    * select() and bidirection I/O...
    */

    device_fd = open(resource, O_WRONLY | O_EXCL);
    use_bc    = 0;

#else
    if ((device_fd = open(resource, O_RDWR | O_EXCL)) < 0)
    {
      device_fd = open(resource, O_WRONLY | O_EXCL);
      use_bc    = 0;
    }
    else
      use_bc = 1;
#endif /* __linux || __FreeBSD__ */

    if (device_fd == -1)
    {
      if (getenv("CLASS") != NULL)
      {
       /*
        * If the CLASS environment variable is set, the job was submitted
	* to a class and not to a specific queue.  In this case, we want
	* to abort immediately so that the job can be requeued on the next
	* available printer in the class.
	*/

        fputs("INFO: Unable to contact printer, queuing on next printer in "
              "class.\n", stderr);

       /*
        * Sleep 5 seconds to keep the job from requeuing too rapidly...
	*/

	sleep(5);

        return (CUPS_BACKEND_FAILED);
      }

      if (errno == EBUSY)
      {
        fputs("INFO: Printer busy; will retry in 30 seconds.\n", stderr);
	sleep(30);
      }
      else if (errno == ENXIO || errno == EIO || errno == ENOENT)
      {
        fputs("INFO: Printer not connected; will retry in 30 seconds.\n",
              stderr);
	sleep(30);
      }
      else
      {
	perror("ERROR: Unable to open parallel port");
	return (CUPS_BACKEND_FAILED);
      }
    }
  }
  while (device_fd < 0);

  fputs("STATE: -connecting-to-device\n", stderr);

 /*
  * Set any options provided...
  */

  tcgetattr(device_fd, &opts);

  opts.c_lflag &= ~(ICANON | ECHO | ISIG);	/* Raw mode */

  /**** No options supported yet ****/

  tcsetattr(device_fd, TCSANOW, &opts);

 /*
  * Finally, send the print file...
  */

  tbytes = 0;

  while (copies > 0 && tbytes >= 0)
  {
    copies --;

    if (print_fd != 0)
    {
      fputs("PAGE: 1 1\n", stderr);
      lseek(print_fd, 0, SEEK_SET);
    }

    tbytes = run_loop(print_fd, device_fd, use_bc, 1);

    if (print_fd != 0 && tbytes >= 0)
      fputs("INFO: Print file sent.\n", stderr);
  }

 /*
  * Close the socket connection and input file and return...
  */

  close(device_fd);

  if (print_fd != 0)
    close(print_fd);

  return (CUPS_BACKEND_OK);
}


/*
 * 'drain_output()' - Drain pending print data to the device.
 */

static int				/* O - 0 on success, -1 on error */
drain_output(int print_fd,		/* I - Print file descriptor */
             int device_fd)		/* I - Device file descriptor */
{
  int		nfds;			/* Maximum file descriptor value + 1 */
  fd_set	input;			/* Input set for reading */
  ssize_t	print_bytes,		/* Print bytes read */
		bytes;			/* Bytes written */
  char		print_buffer[8192],	/* Print data buffer */
		*print_ptr;		/* Pointer into print data buffer */
  struct timeval timeout;		/* Timeout for read... */


 /*
  * Figure out the maximum file descriptor value to use with select()...
  */

  nfds = (print_fd > device_fd ? print_fd : device_fd) + 1;

 /*
  * Now loop until we are out of data from print_fd...
  */

  for (;;)
  {
   /*
    * Use select() to determine whether we have data to copy around...
    */

    FD_ZERO(&input);
    FD_SET(print_fd, &input);

    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    if (select(nfds, &input, NULL, NULL, &timeout) < 0)
      return (-1);

    if (!FD_ISSET(print_fd, &input))
      return (0);

    if ((print_bytes = read(print_fd, print_buffer,
			    sizeof(print_buffer))) < 0)
    {
     /*
      * Read error - bail if we don't see EAGAIN or EINTR...
      */

      if (errno != EAGAIN && errno != EINTR)
      {
        perror("ERROR: Unable to read print data");
	return (-1);
      }

      print_bytes = 0;
    }
    else if (print_bytes == 0)
    {
     /*
      * End of file, return...
      */

      return (0);
    }

    fprintf(stderr, "DEBUG: Read %d bytes of print data.\n",
	    (int)print_bytes);

    for (print_ptr = print_buffer; print_bytes > 0;)
    {
      if ((bytes = write(device_fd, print_ptr, print_bytes)) < 0)
      {
       /*
        * Write error - bail if we don't see an error we can retry...
	*/

        if (errno != ENOSPC && errno != ENXIO && errno != EAGAIN &&
	    errno != EINTR && errno != ENOTTY)
	{
	  perror("ERROR: Unable to write print data");
	  return (-1);
	}
      }
      else
      {
        fprintf(stderr, "DEBUG: Wrote %d bytes of print data.\n", (int)bytes);

        print_bytes -= bytes;
	print_ptr   += bytes;
      }
    }
  }
}


/*
 * 'list_devices()' - List all parallel devices.
 */

static void
list_devices(void)
{
#ifdef __sun
  static char	*funky_hex = "0123456789abcdefghijklmnopqrstuvwxyz";
				/* Funky hex numbering used for some devices */
#endif /* __sun */

#ifdef __linux
  int	i;			/* Looping var */
  int	fd;			/* File descriptor */
  char	device[512],		/* Device filename */
	basedevice[255],	/* Base device filename for ports */
	device_id[1024],	/* Device ID string */
	make_model[1024],	/* Make and model */
	info[2048],		/* Info string */
	uri[1024];		/* Device URI */


  if (!access("/dev/parallel/", 0))
    strcpy(basedevice, "/dev/parallel/");
  else if (!access("/dev/printers/", 0))
    strcpy(basedevice, "/dev/printers/");
  else
    strcpy(basedevice, "/dev/lp");

  for (i = 0; i < 4; i ++)
  {
   /*
    * Open the port, if available...
    */

    sprintf(device, "%s%d", basedevice, i);
    if ((fd = open(device, O_RDWR | O_EXCL)) < 0)
      fd = open(device, O_WRONLY);

    if (fd >= 0)
    {
     /*
      * Now grab the IEEE 1284 device ID string...
      */

      snprintf(uri, sizeof(uri), "parallel:%s", device);

      if (!backendGetDeviceID(fd, device_id, sizeof(device_id),
                              make_model, sizeof(make_model),
			      NULL, uri, sizeof(uri)))
      {
        snprintf(info, sizeof(info), "%s LPT #%d", make_model, i + 1);
	cupsBackendReport("direct", uri, make_model, info, device_id, NULL);
      }
      else
      {
        snprintf(info, sizeof(info), "LPT #%d", i + 1);
	cupsBackendReport("direct", uri, NULL, info, NULL, NULL);
      }

      close(fd);
    }
  }
#elif defined(__sun)
  int		i, j, n;	/* Looping vars */
  char		device[255];	/* Device filename */


 /*
  * Standard parallel ports...
  */

  for (i = 0; i < 10; i ++)
  {
    sprintf(device, "/dev/ecpp%d", i);
    if (access(device, 0) == 0)
      printf("direct parallel:%s \"Unknown\" \"Sun IEEE-1284 Parallel Port #%d\"\n",
             device, i + 1);
  }

  for (i = 0; i < 10; i ++)
  {
    sprintf(device, "/dev/bpp%d", i);
    if (access(device, 0) == 0)
      printf("direct parallel:%s \"Unknown\" \"Sun Standard Parallel Port #%d\"\n",
             device, i + 1);
  }

  for (i = 0; i < 3; i ++)
  {
    sprintf(device, "/dev/lp%d", i);

    if (access(device, 0) == 0)
      printf("direct parallel:%s \"Unknown\" \"PC Parallel Port #%d\"\n",
             device, i + 1);
  }

 /*
  * MAGMA parallel ports...
  */

  for (i = 0; i < 40; i ++)
  {
    sprintf(device, "/dev/pm%02d", i);
    if (access(device, 0) == 0)
      printf("direct parallel:%s \"Unknown\" \"MAGMA Parallel Board #%d Port #%d\"\n",
             device, (i / 10) + 1, (i % 10) + 1);
  }

 /*
  * Central Data parallel ports...
  */

  for (i = 0; i < 9; i ++)
    for (j = 0; j < 8; j ++)
      for (n = 0; n < 32; n ++)
      {
        if (i == 8)	/* EtherLite */
          sprintf(device, "/dev/sts/lpN%d%c", j, funky_hex[n]);
        else
          sprintf(device, "/dev/sts/lp%c%d%c", i + 'C', j,
                  funky_hex[n]);

	if (access(device, 0) == 0)
	{
	  if (i == 8)
	    printf("direct parallel:%s \"Unknown\" \"Central Data EtherLite Parallel Port, ID %d, port %d\"\n",
	           device, j, n);
  	  else
	    printf("direct parallel:%s \"Unknown\" \"Central Data SCSI Parallel Port, logical bus %d, ID %d, port %d\"\n",
	           device, i, j, n);
	}
      }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__FreeBSD_kernel__)
  int	i;			/* Looping var */
  int	fd;			/* File descriptor */
  char	device[255];		/* Device filename */


  for (i = 0; i < 3; i ++)
  {
    sprintf(device, "/dev/lpt%d", i);
    if ((fd = open(device, O_WRONLY)) >= 0)
    {
      close(fd);
      printf("direct parallel:%s \"Unknown\" \"Parallel Port #%d (interrupt-driven)\"\n", device, i + 1);
    }

    sprintf(device, "/dev/lpa%d", i);
    if ((fd = open(device, O_WRONLY)) >= 0)
    {
      close(fd);
      printf("direct parallel:%s \"Unknown\" \"Parallel Port #%d (polled)\"\n", device, i + 1);
    }
  }
#endif
}


/*
 * 'run_loop()' - Read and write print and back-channel data.
 */

static ssize_t				/* O - Total bytes on success, -1 on error */
run_loop(int print_fd,			/* I - Print file descriptor */
	int device_fd,			/* I - Device file descriptor */
	int use_bc,			/* I - Use back-channel? */
	int update_state)		/* I - Update printer-state-reasons? */
{
  int		nfds;			/* Maximum file descriptor value + 1 */
  fd_set	input,			/* Input set for reading */
		output;			/* Output set for writing */
  ssize_t	print_bytes,		/* Print bytes read */
		bc_bytes,		/* Backchannel bytes read */
		total_bytes,		/* Total bytes written */
		bytes;			/* Bytes written */
  int		paperout;		/* "Paper out" status */
  int		offline;		/* "Off-line" status */
  char		print_buffer[8192],	/* Print data buffer */
		*print_ptr,		/* Pointer into print data buffer */
		bc_buffer[1024];	/* Back-channel data buffer */
  struct timeval timeout;		/* Timeout for select() */
  int           sc_ok;                  /* Flag a side channel error and
					   stop using the side channel
					   in such a case. */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * If we are printing data from a print driver on stdin, ignore SIGTERM
  * so that the driver can finish out any page data, e.g. to eject the
  * current page.  We only do this for stdin printing as otherwise there
  * is no way to cancel a raw print job...
  */

  if (!print_fd)
  {
#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
    sigset(SIGTERM, SIG_IGN);
#elif defined(HAVE_SIGACTION)
    memset(&action, 0, sizeof(action));

    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_IGN;
    sigaction(SIGTERM, &action, NULL);
#else
    signal(SIGTERM, SIG_IGN);
#endif /* HAVE_SIGSET */
  }
  else if (print_fd < 0)
  {
   /*
    * Copy print data from stdin, but don't mess with the signal handlers...
    */

    print_fd = 0;
  }

 /*
  * Figure out the maximum file descriptor value to use with select()...
  */

  nfds = (print_fd > device_fd ? print_fd : device_fd) + 1;


 /*
  * Side channel is OK...
  */

  sc_ok = 1;

 /*
  * Now loop until we are out of data from print_fd...
  */

  for (print_bytes = 0, print_ptr = print_buffer, offline = -1,
           paperout = -1, total_bytes = 0;;)
  {
   /*
    * Use select() to determine whether we have data to copy around...
    */

    FD_ZERO(&input);
    if (!print_bytes)
      FD_SET(print_fd, &input);
    if (use_bc)
      FD_SET(device_fd, &input);
    if (!print_bytes && sc_ok)
      FD_SET(CUPS_SC_FD, &input);

    FD_ZERO(&output);
    if (print_bytes)
      FD_SET(device_fd, &output);

    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;

    if (select(nfds, &input, &output, NULL, &timeout) < 0)
    {
     /*
      * Pause printing to clear any pending errors...
      */

      if (errno == ENXIO && offline != 1 && update_state)
      {
	fputs("STATE: +offline-report\n", stderr);
	offline = 1;
      }
      else if (errno == EINTR && total_bytes == 0)
      {
	fputs("DEBUG: Received an interrupt before any bytes were "
	      "written, aborting.\n", stderr);
	return (0);
      }

      sleep(1);
      continue;
    }

   /*
    * Check if we have a side-channel request ready...
    */

    if (sc_ok && FD_ISSET(CUPS_SC_FD, &input))
    {
     /*
      * Do the side-channel request, then start back over in the select
      * loop since it may have read from print_fd...
      *
      * If the side channel processing errors, go straight on to avoid
      * blocking of the backend by side channel problems, deactivate the side
      * channel.
      */

      if (side_cb(print_fd, device_fd, use_bc))
	sc_ok = 0;
      continue;
    }

   /*
    * Check if we have back-channel data ready...
    */

    if (FD_ISSET(device_fd, &input))
    {
      if ((bc_bytes = read(device_fd, bc_buffer, sizeof(bc_buffer))) > 0)
      {
	fprintf(stderr, "DEBUG: Received %d bytes of back-channel data.\n",
	        (int)bc_bytes);
        cupsBackChannelWrite(bc_buffer, bc_bytes, 1.0);
      }
      else if (bc_bytes < 0 && errno != EAGAIN && errno != EINTR)
      {
        perror("DEBUG: Error reading back-channel data");
	use_bc = 0;
      }
      else if (bc_bytes == 0)
        use_bc = 0;
    }

   /*
    * Check if we have print data ready...
    */

    if (FD_ISSET(print_fd, &input))
    {
      if ((print_bytes = read(print_fd, print_buffer,
                              sizeof(print_buffer))) < 0)
      {
       /*
        * Read error - bail if we don't see EAGAIN or EINTR...
	*/

	if (errno != EAGAIN && errno != EINTR)
	{
	  perror("ERROR: Unable to read print data");
	  return (-1);
	}

        print_bytes = 0;
      }
      else if (print_bytes == 0)
      {
       /*
        * End of file, break out of the loop...
	*/

        break;
      }

      print_ptr = print_buffer;

      fprintf(stderr, "DEBUG: Read %d bytes of print data.\n",
              (int)print_bytes);
    }

   /*
    * Check if the device is ready to receive data and we have data to
    * send...
    */

    if (print_bytes && FD_ISSET(device_fd, &output))
    {
      if ((bytes = write(device_fd, print_ptr, print_bytes)) < 0)
      {
       /*
        * Write error - bail if we don't see an error we can retry...
	*/

        if (errno == ENOSPC)
	{
	  if (paperout != 1 && update_state)
	  {
	    fputs("STATE: +media-empty-warning\n", stderr);
	    paperout = 1;
	  }
        }
	else if (errno == ENXIO)
	{
	  if (offline != 1 && update_state)
	  {
	    fputs("STATE: +offline-report\n", stderr);
	    offline = 1;
	  }
	}
	else if (errno != EAGAIN && errno != EINTR && errno != ENOTTY)
	{
	  perror("ERROR: Unable to write print data");
	  return (-1);
	}
      }
      else
      {
        if (paperout && update_state)
	{
	  fputs("STATE: -media-empty-warning\n", stderr);
	  paperout = 0;
	}

	if (offline && update_state)
	{
	  fputs("STATE: -offline-report\n", stderr);
	  offline = 0;
	}

        fprintf(stderr, "DEBUG: Wrote %d bytes of print data...\n", (int)bytes);

        print_bytes -= bytes;
	print_ptr   += bytes;
	total_bytes += bytes;
      }
    }
  }

 /*
  * Return with success...
  */

  return (total_bytes);
}


/*
 * 'side_cb()' - Handle side-channel requests...
 */

static int				/* O - 0 on success, -1 on error */
side_cb(int         print_fd,		/* I - Print file */
        int         device_fd,		/* I - Device file */
	int         use_bc)		/* I - Using back-channel? */
{
  cups_sc_command_t	command;	/* Request command */
  cups_sc_status_t	status;		/* Request/response status */
  char			data[2048];	/* Request/response data */
  int			datalen;	/* Request/response data size */


  datalen = sizeof(data);

  if (cupsSideChannelRead(&command, &status, data, &datalen, 1.0))
    return (-1);

  switch (command)
  {
    case CUPS_SC_CMD_DRAIN_OUTPUT :
        if (drain_output(print_fd, device_fd))
	  status = CUPS_SC_STATUS_IO_ERROR;
	else if (tcdrain(device_fd))
	  status = CUPS_SC_STATUS_IO_ERROR;
	else
	  status = CUPS_SC_STATUS_OK;

	datalen = 0;
        break;

    case CUPS_SC_CMD_GET_BIDI :
	status  = CUPS_SC_STATUS_OK;
        data[0] = use_bc;
        datalen = 1;
        break;

    case CUPS_SC_CMD_GET_DEVICE_ID :
        memset(data, 0, sizeof(data));

        if (backendGetDeviceID(device_fd, data, sizeof(data) - 1,
	                       NULL, 0, NULL, NULL, 0))
        {
	  status  = CUPS_SC_STATUS_NOT_IMPLEMENTED;
	  datalen = 0;
	}
	else
        {
	  status  = CUPS_SC_STATUS_OK;
	  datalen = strlen(data);
	}
        break;

    default :
        status  = CUPS_SC_STATUS_NOT_IMPLEMENTED;
	datalen = 0;
	break;
  }

  return (cupsSideChannelWrite(command, status, data, datalen, 1.0));
}

//
// ESC/POS raster driver for CUPS.
//
// "$Id: rastertozj.c,v 1.1 2001/08/30 19:37:33 mike Exp $"
//
//   Zjiang printer filter for the Common UNIX
//   Printing System (CUPS).
//
//   Copyright 2007-2011 by Zjiang .
//
// Contents:
//
//   Setup()        - Prepare the printer for printing.
//   StartPage()    - Start a page of graphics.
//   EndPage()      - Finish a page of graphics.
//   Shutdown()     - Shutdown the printer.
//   CancelJob()    - Cancel the current job...
//   main()         - Main entry and processing of driver.
//
    
//
// Include necessary headers...
//

#include <cupsfilters/driver.h>
#include <ppd/ppd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>

#define HEIGHT_PIXEL 24 // process this much lines at a time

// some cutting options
#define CUT_AT_PAGE_END	1
#define CUT_AT_DOC_END 2

#define CMD_LEN	8 // length of GS v 0 command

//
// Globals...
//

int Page; // Current page number
int CuttingSetting;
bool Canceled;
ppd_file_t *Ppd; // info about the ppd file

//
// Prototypes...
//

static void	Setup(void);
static void StartPage(const cups_page_header2_t *header);
static void	EndPage(void);
static void	Shutdown(void);
static void CutPaper(void);
static void	CancelJob(int sig);

//
// 'Setup()' - Prepare the printer for printing.
//

static void
Setup()
{
  //
  // Register a signal handler to eject the current page if the
  // job is cancelled.
  //

#ifdef HAVE_SIGSET // Use System V signals over POSIX to avoid bugs
  sigset(SIGTERM, CancelJob);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = CancelJob;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, CancelJob);
#endif // HAVE_SIGSET

  //
  // Look for a cutting option.
  //

  ppd_choice_t *choice = ppdFindMarkedChoice(Ppd, "Cutting");
  if (choice != NULL)
    CuttingSetting = atoi(choice->choice);

  //
  // Send a reset sequence.
  //
  putchar(0x1b);
  putchar(0x40);
}

//
// 'Shutdown()' - Shutdown the printer.
//
static void
Shutdown(void)
{
	if (CuttingSetting == CUT_AT_DOC_END)
		CutPaper();

	//
	// Send a reset sequence.
	//

	putchar(0x1b);
	putchar(0x40);
}


//
// 'EndPage()' - Finish a page of graphics.
//

static void
EndPage(void)
{
  fflush(stdout);

  if (CuttingSetting == CUT_AT_PAGE_END)
    CutPaper();
}

//
// 'StartPage()' - Start a page of graphics.
//

static void
StartPage(const cups_page_header2_t *header)
{
  Page++;

  fprintf(stderr, "DEBUG: Page %d of %d\n", Page, header->NumCopies);
  fprintf(stderr, "DEBUG: cupsWidth = %d\n", header->cupsWidth);
  fprintf(stderr, "DEBUG: cupsHeight = %d\n", header->cupsHeight);
  fprintf(stderr, "DEBUG: cupsMediaType = %d\n", header->cupsMediaType);
  fprintf(stderr, "DEBUG: cupsBitsPerColor = %d\n", header->cupsBitsPerColor);
  fprintf(stderr, "DEBUG: cupsBitsPerPixel = %d\n", header->cupsBitsPerPixel);
  fprintf(stderr, "DEBUG: cupsBytesPerLine = %d\n", header->cupsBytesPerLine);
  fprintf(stderr, "DEBUG: HWResolution = %d\n", header->HWResolution[0]);
  fprintf(stderr, "DEBUG: PageSize = [ %d %d ]\n", header->PageSize[0],
          header->PageSize[1]);
}

//
// 'CancelJob()' - Cancel the current job...
//

static void
CancelJob(int sig)			// I - Signal
{
  (void)sig;

  //
  // Send out lots of NUL bytes to clear out any pending raster data...
  //

  int i; // Looping var
  for (i = 0; i < 600; i ++)
    putchar(0);

  Canceled = true;
}

static void CutPaper()
{
  unsigned char buf[3];

  buf[0] = 0x1d;
  buf[1] = 'V';
  buf[2] = 0x01;
  fwrite(buf, 3, 1, stdout);
}

//
// 'main()' - Main entry and processing of driver.
//
int 			// O - Exit status
main(int  argc,		// I - Number of command-line arguments
     char *argv[])	// I - Command-line arguments
{
  int fd; // file descriptor for input raster stream
  cups_raster_t	*ras; // raster stream for printing
  cups_option_t *options; // options provided in argv[5]
  int num_options; // # of options provided in argv[5]

  //
  // Make sure status messages are not buffered...
  //

  setbuf(stderr, NULL);

  //
  // Check command-line...
  //

  if (argc < 6 || argc > 7)
  {
    //
    // We don't have the correct number of arguments; write an error message
    // and return.
    //
    fputs("ERROR: rastertopcl job-id user title copies options [file]\n", stderr);
    return (1);
  }

  num_options = cupsParseOptions(argv[5], 0, &options);

  //
  // Open the PPD file...
  //

  Ppd = ppdOpenFile(getenv("PPD"));
  if (!Ppd)
  {
    ppd_status_t    status;     // PPD error
    int         linenum;    // Line number

    fputs("ERROR: The PPD file could not be opened.\n", stderr);

    status = ppdLastError(&linenum);
    fprintf(stderr, "DEBUG: %s on line %d.\n", ppdErrorString(status), linenum);

    return (1);
  }

  ppdMarkDefaults(Ppd);
  ppdMarkOptions(Ppd, num_options, options);

  //
  // Open the page stream...
  //

  if (argc == 7)
  {
    if ((fd = open(argv[6], O_RDONLY)) == -1)
    {
      perror("ERROR: Unable to open raster file");
      return (1);
    }
  }
  else
  {
    fd = 0;
  }

  if ( (ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL)
  {
      perror("ERROR: Unable to read raster file");
      return (1);
  }

  //
  // Initialize the print device...
  //

  Setup();

  //
  // Process pages as needed...
  //

  cups_page_header2_t header; // page header from file
  while (cupsRasterReadHeader2(ras, &header))
  {
    if (Canceled)
      goto out;

    StartPage(&header);

    //
    // Loop for each line on the page...
    //

    int line_height = header.cupsHeight; // number of lines to print
    int line_width_bytes = header.cupsBytesPerLine; // line width length in bytes
    unsigned char *dest = calloc(CMD_LEN+line_width_bytes*line_height, 1); // space to place commands and raster data
    if (!dest)
    {
      perror("ERROR: calloc failed - ");
      return (1);
    }
    int y = 0; // current line
    while (y < line_height)
    {
      //
      // Let the user know how far we have progressed...
      //

      if ((y & 127) == 0)
      {
        fprintf(stderr, "INFO: Printing page %d, %d%% complete...\n", Page,
                100 * y / line_height);
      }

      // GS v 0 command: print raster bit image
      dest[0] = 0x1d;
      dest[1] = 0x76;
      dest[2] = 0x30;
      dest[3] = 0x00;
      dest[4] = line_width_bytes%256;
      dest[5] = line_width_bytes/256;

      int h;
      if (line_height - y > HEIGHT_PIXEL)
      {
        // image has been clipped, so we always print the fixed height.
        h = HEIGHT_PIXEL;
      }
      else
      {
        h = line_height - y;
      }

      y += h;

      dest[6] = h%256;
      dest[7] = h/256;

      //
      // Read h line of graphics...
      //

      int i;
      for (i = 0; i < h ; i++)
      {
        int bytes;
        if (!(bytes = cupsRasterReadPixels(ras, dest + CMD_LEN + i * line_width_bytes, line_width_bytes)))
        {
          perror("ERROR: Unable to read from raster file - ");
          goto out;
        }
      }

      //
      // output h lines of graphics...
      //

      fwrite(dest, h * line_width_bytes + CMD_LEN , 1, stdout);
    }
    EndPage();
    free(dest);
  }

out:

  //
  // Shutdown the printer...
  //

  Shutdown();

  cupsFreeOptions(num_options, options);

  ppdClose(Ppd);

  //
  // Close the raster stream...
  //

  cupsRasterClose(ras);
  if (fd != 0)
    close(fd);

  //
  // If no pages were printed, send an error message...
  //

  if (Page == 0)
    fputs("ERROR: No pages found!\n", stderr);

  return (Page == 0);
}

//
// End of "$Id: rastertozj.c,v 1.1 2011/01/15 19:37:33 mike Exp $".
//

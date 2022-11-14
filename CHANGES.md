# CHANGES - OpenPrinting CUPS Filters v2.0b1 - 2022-11-14

## CHANGES IN V2.0b1 (TBA)

### libcupsfilters

- Introduced the filter functions concept converting filter
  executables into library functions with common call scheme, moving
  their core functionality into libcupsfilters and allowing easier use
  by Printer Applications. Common data about the printer and the job
  are supplied via a data structure, which is the same for each filter
  in a chain of filters. The data structure can be extended via named
  extensions.

- Converted nearly all filters to filter functions, only exceptions
  are rastertoescpx, rastertopclx, commandtoescpx, commandtopclx, and
  foomatic-rip. The latter is deeply involved with Foomatic PPDs and
  the others are legacy printer drivers. Filter functions which
  output PostScript are implemented in libppd.

- Converted CUPS' rastertopwg filter into the cfFilterRasterToPWG()
  filter function.

- Created new cfFilterPWGToRaster() filter function primarily to print
  raster input (PWG Raster, Apple Raster, images) to CUPS Raster
  drivers in driver-retro-fitting Printer Applications.

- Converted all filter functions to completely work without PPD files,
  using only printer and job IPP attributes and an option list for
  options not mappable to IPP attributes. For some filter functions
  there are also wrappers for a more comprehensive PPD file support in
  libppd.

- Added concept of callback functions for logging, for the filter
  functions not spitting their log to stderr.

- Added concept of callback functions for telling that a job is
  cancelled so that filter functions can return early. This change is
  to get more flexibility and especially to support the
  papplJobIsCanceled() of PAPPL.

- Added new streaming mode triggered by the boolean
  "filter-streaming-mode" option. In this mode a filter (function) is
  supposed to avoid everything which prevents the job data from
  streaming, as loading the whole job (or good part of it) into a
  temporary file or into memory, interpreting PDF, pre-checking input
  file type or zero-page jobs, ... This is mainly to be used by
  Printer Applications when they do raster printing in streaming mode,
  to run with lowest resources possible. Currently
  cfFilterGhostscript() and cfFilterPDFToPDF() got a streaming
  mode. In streaming mode PostScript (not PDF) is assumed as input and
  no zero-page-job check is done, and all QPDF processing (page
  management, page size adjustment, ...) is skipped and only JCL
  according to the PPD added.

- Added raster-only PDF and PCLm output support to the ghostscript()
  filter function. Note that PCLm does not support back side
  orientation for duplex.

- cfFilterPDFToPDF(): Introduced new "input-page-ranges" attribute
  (Issue #365, Pull request #444, #445).

- Added cfFilterChain() filter function to run several filter
  functions in a chain.

- Added filterPOpen() and filterPClose() functions which similar to
  popen() and pclose() create a file descriptor which either takes
  data to feed into a filter function or provides data coming out of a
  filter function.

- Added new cfFilterExternal() filter function which calls an external
  CUPS filter (or backend) executable specified in the parameters, for
  example a legacy or proprietary printer driver which cannot be
  converted into a filter function. Backends can be run both for job
  execution or in their discovery mode. The environment in which the
  external executable is running is resembling CUPS as best as
  possible.

- Added support for the back and side channels which CUPS uses for
  additional communication between the filters and the backend into
  the filter function infrastructure. Now filter functions can use
  these channels and also CUPS filters or backends called via the
  cfFilterExternal() function. Printer Applications can easily create
  the needed pipes via the new function cfFilterOpenBackAndSidePipes()
  and close them via cfFilterCloseBackAndSidePipes() and filter
  functions used as classic CUPS filters get the appropriate file
  descriptors supplied by ihe ppdFilterCUPSWrapper() function of
  libppd.

- Added the cfFilterUniversal() filter function which allows a single
  CUPS filter executable which auto-creates chains of filter function
  calls to convert any input data format into any other output data
  format. So CUPS can call a single filter for any conversion, taking
  up less resources. Thanks to Pranshu Kharkwal for this excellent
  GSoC project (Pull request #421).

- Added functions to read out IPP attributes from the job and check
  them against the IPP attributes (capabilities) of the printer:
  cfIPPAttrEnumValForPrinter(), cfIPPAttrIntValForPrinter(),
  cfIPPAttrResolutionForPrinter()

- Added functions cfGenerateSizes() and cfGetPageDimensions() to match
  input page sizes with the page sizes available on the printer
  according to printer IPP attributes.

- Moved IEEE1284-device-ID-related functions into the public API of
  libcupsfilters, also made the internal functions public and renamed
  them all to cfIEEE1284...(), moved test1284 to cupsfilters/.

- Extended cfIEEE1284NormalizeMakeAndModel() to a universal function
  to clean up and normalize make/model strings (also device IDs) to
  get human-readable, machine-readable, or easily sortable make/model
  strings. Also allow supplying a regular expression to extract driver
  information when the input string is a PPD's *NickName.

- When calling filters without having printer attributes, improved
  understanding of color mode options/attributes. Options
  "output-mode", "OutputMode", "print-color-mode", and choices "auto",
  "color", "auto-monochrome", "process-monochrome", and "bi-level" are
  supported now and default color mode is RGB 8-bit color and not
  1-bit monochrome.

- When parsing IPP attributes/options map the color spaces the same
  way as in the PPD generator (Issue #326, Pull request #327).

- Added new oneBitToGrayLine() API function which converts a line of
  1-bit monochrome pixels into 8-bit grayscale format
  (cupsfilters/bitmap.[ch]).

- Removed support for asymmetric image resolutions ("ppi=XXXxYYY") in
  cfFilterImageToPDF() and cfFilterImageToRaster() as CUPS does not
  support this (Issue #347, Pull request #361, OpenPrinting CUPS issue
  #115).

- Removed now obsolete apply_filters() API function to call a chain of
  external CUPS filter executables, as we have filter functions now
  and even can call one or another filter executable (or even backend)
  via cfFilterExternal().

- Build system, README: Require CUPS 2.2.2+ and QPDF 10.3.2+.  Removed
  now unneeded ./configure switches for PCLm support in QPDF and for
  use of the urftopdf filter for old CUPS versions.

- Renamed function/data type/constant names to get a consistent API:
  Functions start with "cf" and the name is in camel-case, data types
  start with "cf_" and are all-lowercase with "_" separators,
  constants start with "CF_" and are all-uppercase, also with "_"
  separators.

- Bumped soname to 2, as we have a new API now.

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Older versions of libcups (< 2.3.1) had the enum name for
  fold-accordion finishings mistyped.  Added a workaround.

- Added support for Sharp-proprietary "ARDuplex" PPD option name for
  double-sided printing.

- Build system: Add files in gitignore that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).

- Fixed possible crash bug in oneBitLine() function.

- In ghostscript() pass on LD_LIBRARY_PATH to Ghostscript


### libppd

- Added the new libppd library overtaking all the PPD handling
  functions from libcups, CUPS' ppdc utility (PPD compiler using *.drv
  files), and the PPD file collection handling functionality from
  cups-driverd, as they are deprecated there and will probably get
  removed with the next CUPS version. This form of conservation is
  mainly intended for converting classic printer drivers which use
  PPDs into Printer Applications without completely rewriting them.

- Added functions ppdLoadAttributes(), ppdFilterLoadPPD(),
  ppdFilterLoadPPDFile(), ppdFilterFreePPD(), and
  ppdFilterFreePPDFile() to convert all PPD file options and settings
  relevant for the filter functions in libcupsfilters into printer IPP
  attributes and options. Also create a named ("libppd") extension in
  the filter data structure, for filter functions explicitly
  supporting PPD files.

- Added ppdFilterCUPSWrapper() convenience function to easily create a
  classic CUPS filter from a filter function.

- Converted the ...tops CUPS filters into the filter functions
  ppdFilterPDFToPS(), and ppdFilterRasterToPS().  As PostScript as
  print output format is as deprecated as PPD gfiles and all
  PostScript printers use PPD files, we move PostScript output
  generation to libppd, too.

- Converted CUPS' pstops filter into the ppdFilterPSToPS() filter
  functions.

- Introduced ppdFilterImageToPS() filter function for Printer
  Applications to print on PostScript printers. It is used to print
  images (IPP standard requires at least JPEG to be accepted) without
  need of a PDF renderer (cfFilterImageToPDF -> cfFilterPDFToPS) and
  without need to convert to Raster (cfFilterImageToRaster ->
  cfFilterRasterToPS).

- Created wrappers to add PPD-defined JCL code to jobs for native PDF
  printers, ppdFilterImageToPDF() and ppdFilterPDFToPDF() filter
  functions with the underlying ppdFilterEmitJCL() function.

- Created ppdFilterUniversal() wrapper function for
  cfFilterUniversal() to add PPD file support, especially for PPD
  files which define driver filters.

- ppdFilterExternalCUPS() wrapper function for cfFilterExternal() to
  add PPD file support.

- Auto-pre-fill the presets with most suitable options from the PPD
  files with new ppdCacheAssignPresets() function (Only if not filled
  via "APPrinterPreset" in the PPD). This way we can use the IPP
  options print-color-mode, print-quality, and print-content-optimize
  in a PPD/CUPS-driver-retro-fitting Printer Application with most
  PPDs.

- In ppdCacheCreateWithPPD() also support presets for
  print-ontent-optimize, not only for print-color-mode and
  print-quality

- In the ppdCacheGetPageSize() function do not only check the fit of
  the margins with the page size matching the job's page size (like
  "A4") but also the margins of the variants (like "A4.Borderless"),
  even if the variant's size dimensions differ from the base size (and
  the physical paper size), for example for overspray in borderless
  printing mode (HPLIP does this). Also select a borderless variant
  only if the job requests borderless (all margins zero).

- Move the functions ppdPwgUnppdizeName(), ppdPwgPpdizeName(), and
  ppdPwgPpdizeResolution() into the public API.

- In the ppdPwgUnppdizeName() allow to supply NULL as the list of
  characters to replace by dashes. Then all non-alpha-numeric
  characters get replaced, to result in IPP-conforming keyword
  strings.

- In the ppdPwgUnppdizeName() function support negative numbers

- ppdFilterPSToPS(): Introduced new "input-page-ranges" attribute
  (Issue #365, Pull request #444, #445).

- Changed "ColorModel" option in the PPDs from the PPD generator to
  mirror the print-color-mode IPP attribute instead of providing all
  color space/depth combos for manual selection. Color space and depth
  are now auto-selected by the urf-supported and
  pwg-raster-document-type-supported printer IPP attributes and the
  settings of print-color-mode and print-quality.  This is now
  implemented in the ghostscript() filter function both for use of the
  auto-generated PPD file for driverless iPP printers and use without
  PPD, based on IPP attributes.  For this the new library functions
  cupsRasterPrepareHeader() to create a header for Raster output and
  cupsRasterSetColorSpace() to auto-select color space and depth were
  created.

- Clean-up of human-readable string handling in the PPD generator.

- Removed support for asymmetric image resolutions ("ppi=XXXxYYY") in
  cfFilterImageToPS() as CUPS does not support this (Issue #347, Pull
  request #361, OpenPrinting CUPS issue #115).

- Removed versioning.h and the macros defined in this file (Issue
  #334).

- Removed ppdCreateFromIPPCUPS(), we have a better one in
  libcupsfilters. Also removed corresponding test in testppd.

- Build system, README: Require CUPS 2.2.2+ and QPDF 10.3.2+.  Removed
  now unneeded ./configure switches for PCLm support in QPDF and for
  use of the urftopdf filter for old CUPS versions.

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Older versions of libcups (< 2.3.1) had the enum name for
  fold-accordion finishings mistyped.  Added a workaround.

- Added support for Sharp-proprietary "ARDuplex" PPD option name for
  double-sided printing.

- Build system: Add files in gitignore that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).

- Fixed PPD memory leak caused by "emulators" field not freed
  (OpenPrinting CUPS issue #124).  - Make "True" in boolean options
  case-insensitive (OpenPrinting CUPS pull request #106).


### cups-filters

- Converted nearly all filters to filter functions, only exceptions
  are rastertoescpx, rastertopclx, commandtoescpx, commandtopclx, and
  foomatic-rip. The latter is deeply involved with Foomatic PPDs and
  the others are legacy printer drivers.

- Replaced all the filters converted to filter functions by a simple
  wrapper executable using ppdFilterCUPSWrapper() for backward
  compatibility with CUPS 2.x.

- Added new streaming mode triggered by the boolean
  "filter-streaming-mode" option. In this mode a filter (function) is
  supposed to avoid everything which prevents the job data from
  streaming, as loading the whole job (or good part of it) into a
  temporary file or into memory, interpreting PDF, pre-checking input
  file type or zero-page jobs, ... This is mainly to be used by
  Printer Applications when they do raster printing in streaming mode,
  to run with lowest resources possible. Currently foomatic-rip,
  ghostscript, and pdftopdf got a streaming mode. For the former two
  PostScript (not PDF) is assumed as input and no zero-page-job check
  is done, in the latter all QPDF processing (page management, page
  size adjustment, ...) is skipped and only JCL according to the PPD
  added.

- The CUPS filter imagetops uses the cfFilterImageToPS() filter
  function now.

- driverless, driverless-fax, Added IPP Fax Out support. Now printer
  setup tools list an additional fax "driver".  A fax queue is created
  by selecting this driver. Jobs have to be sent with "-o phone=12345"
  to supply the destination phone number (Pull request #280, #293,
  #296, #302, #304, #305, #306, #309, Issue #298, #308).

- sys5ippprinter, cups-browsed: Removed sys5ippprinter, as CUPS does
  not support System V interface scripts any more. This first approach
  of PPD-less printing was also not actually made use of.

- urftopdf: Removed as we require CUPS 2.2.2+ now which supports Apple
  Raster by itself.

- Build system, README: Require CUPS 2.2.2+ and QPDF 10.3.2+.  Removed
  now unneeded ./configure switches for PCLm support in QPDF and for
  use of the urftopdf filter for old CUPS versions.

- Sample PPDs: Renamed source directory from "ppd/" to "ppdfiles/"

- Build system: Added missing library dependencies to the filters to
  make parallel builds work (Issue #319).

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Build system: Add files in gitignore that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).


### braille-printer-app

- textbrftoindex: Fix control character filtering (Pull
  request #409)

- Build system: Remove '-D_PPD_DEPRECATED=""' from the
  compiling command lines of the source files which use
  libcups. The flag is not supported any more for longer times
  already and all the PPD-related functions deprecated by CUPS
  have moved into libppd now.

- Build system: Add files in gitignore that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).


### cups-browsed

- Added multi-threaded operation, the Avahi resolver callback (which
  examines the remote printer, registers it, checks whether we want a
  local queue for it, adds it to a cluster, ...) and the
  creation/modification of a local CUPS queue is now done in separate
  threads, so that these processes can get executed in parallel to
  keep the local queues up-to-date more timely and to not overload the
  system's resources.  Thanks a lot to Mohit Mohan who did this work
  as Google Summer of Code 2020 project
  (https://github.com/mohitmo/GSoC-2020-Documentation).

- Let the implicitclass backend use filter functions instead of
  calling filter executables.

- Build system, README.md: Require CUPS 2.2.2+ and QPDF 10.3.2+.
  Removed now unneeded ./configure switches for PCLm support in QPDF
  and for use of the urftopdf filter for old CUPS versions.

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Build system: Add files in gitignore that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).

- implicitclass: Added "#include <signal.h>" (Issue #335).

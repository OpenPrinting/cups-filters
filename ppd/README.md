# libppd

One important point for the CUPS Snap (and also CUPS from version 3.x on) is that it does not support classic printer drivers, consisting of PPD files and filters. So for using it as default CUPS implementation in a Linux distribution, all existing printer drivers need to get turned into Printer Applications, and this with a minimum effort of coding.

Most of them (probably all except Gutenprint) are difficult to get converted by their original authors, as they do not maintain the drivers any more, supported printers are old and no one wants to code for that any more, driver is simply only a big bunch of PPD files, no code, driver is proprietary, closed source, ...

So we need a way to retro-fit these drivers by wrapping them into Printer Applications with lowest coding effort possible and if needed also without needing to modify the original driver executables.

This works best if we use the driver's PPD files inside the Printer Application and so we need to handle PPD files, also after the PPD handling support got removed from CUPS and especially libcups.

To avoid that we have to invent the wheel again, writing a lot of handling code for a totally obsolete file format, I have grabbed all the PPD handling functions from libcups and from ppdc/ (current GitHub state, CUPS 2.3.3) and put them into the new libppd which I have added to cups-filters.

It has the following properties:
- All PPD-handling-related functions from libcups (except loading the PPD from a CUPS queue or polling a PPD repository on a CUPS server) are overtaken
- Also the CUPS-private functions related to PPDs are overtaken and added to libppd's public API.
- Other private or internal functions are overtaken from libcups as they are needed for the PPD-related functions to work. They are not added to the API.
- Some functions of tools and utilities like ippeveprinter and ippeveps are overtaken.
- The PPD compiler code (ppdc/ directory) is also overtaken into libppd and the ppdc utilities are overtaken to cups-filters. This allows retro-fitting printer drivers with driver information files (*.drv) instead of ready-made PPDs, both by pre-building the PPDs or by letting them get generated on-the-fly.
- All API functions have names starting with "ppd" (or "ppdc") and written in camel-case, some needed to get renamed for that.
- libppd is separate from libcupsfilters, so it does not need to get included in a Printer Application which uses functionality of cups-filters but does not use PPD files

NOTE: This is NOT to encourage printer driver developers to continue to create new PPD files and *.drv files for new printers. It is ONLY for retro-fitting existing classic CUPS drivers and PostScript PPD files. There will be no further development on the library's code, especially no new PPD or drv format extensions.

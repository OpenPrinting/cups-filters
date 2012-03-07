README - OpenPrinting CUPS Filters v1.0.4 - 2012-03-07
------------------------------------------------------

Looking for compile instructions?  Read the file "INSTALL.txt"
instead...


INTRODUCTION

    CUPS is a standards-based, open source printing system developed by Apple
    Inc. for Mac OS® X and other UNIX®-like operating systems.  CUPS uses the
    Internet Printing Protocol ("IPP") and provides System V and Berkeley
    command-line interfaces, a web interface, and a C API to manage printers and
    print jobs.

    This distribution contains backends, filters, and other software that was
    once part of the core CUPS distribution but is no longer maintained by
    Apple Inc. In addition it contains additional filters developed
    independently of Apple, especially filters for the PDF-centric printing
    workflow introduced by OpenPrinting.

    From CUPS 1.6.0 on, this package will be required for using printer drivers
    with CUPS under Linux. With CUPS 1.5.x and earlier this package can be used
    optionally to switch over to PDF-based printing. In that case some filters
    are provided by both CUPS and this package. Then the filters of this package
    should be used.

    For compiling and using this package CUPS, Poppler, libjpeg, libpng,
    libtiff, libijs, freetype, fontconfig, and liblcms (liblcms2 recommended)
    are needed. It is highly recommended, especially if non-PostScript printers
    are used, to have Ghostscript, foomatic-filters, and foomatic-db installed.

    CUPS, this package, and Ghostscript contain some rudimentary printer
    drivers, see http://www.openprinting.org/drivers/ for a more
    comprehensive set of printer drivers for Linux.

    See

    http://www.linuxfoundation.org/collaborate/workgroups/openprinting/pdf_as_standard_print_job_format

    for information about the PDF-based printing workflow.

    Report bugs to

    https://bugs.linuxfoundation.org/

    Choose "OpenPrinting" as the product and "cups-filters" as the component.

    See the "LICENSE.txt" files for legal information.


CUPS FILTERS FOR PDF AS STANDARD PRINT JOB FORMAT

    Here is documentation from the former CUPS add-on tarball with the filters
    for the PDF-based printing workflow: imagetopdf, texttopdf,
    pdftopdf, pdftoraster, pdftoopvp, and pdftoijs

    The original filters are from http://sourceforge.jp/projects/opfc/

    NOTE: the texttops and imagetops filters shipping with this package
    are simple wrapper scripts for backward compatibility with third-party
    PPD files and custom configurations. There are not referred to in the
    cupsfilters.convs file and therefore not used by the default
    configuration. Direct conversion of text or images to PostScript is
    deprecated in the PDF-based printing workflow. So do not use these
    filters when creating new PPDs or custom configurations. The parameters
    for these filters are the same as for texttopdf and imagetopdf (see
    below) as the ...tops filter calls the ....topdf filter plus
    Ghostscript's pdf2ps.

IMAGETOPDF

1. INTRODUCTION

This program is "imagetopdf". "imagetopdf" is a CUPS filter which reads
a single image file, converts it into a PDF file and outputs it to stdout.

This program accepts the following image file format;

  gif, png, jpeg, tiff, photocd, portable-anymap, portable-bitmap,
  portable-graymap, portable-pixmap, sgi-rgb, sun-raster, xbitmap,
  xpixmap, xwindowdump

xbitmap, xpixmap and xwindowdump images are converted into png images by
the "convert" command. Other kinds of image file format can be supported
if the "convert" command support them.

Output PDF file format conforms to PDF version 1.3 specification, and
input image is converted and contained in the output PDF file as a binary
format non-compression image.

"imagetopdf" may outputs multiple pages if the input image exceeds page
printable area.

2. LICENSE

"imagetopdf.c" is under the CUPS license. See the "LICENSE.txt" file.
For other files, see the copyright notice and license of each file.

3. COMMAND LINE

"imagetopdf" is a CUPS filter, and the command line arguments, environment
variables and configuration files are in accordance with the CUPS filter
interface.

imagetopdf <job> <user> <title> <num-copies> <options> [<filename>]

"imagetopdf" ignores <job> and <user>.
<title> is appended into the PDF dictionary as /Title.
<num-copies> specifies the number of document copies.
<options> is a CUPS option list.
<filename> is an input image file name.

When omit the <filename>, "imagetopdf" reads an image file from stdin.

4. ENVIRONMENT VARIABLES

This program refers the following environment variable;

   PPD:  PPD file name of the printer.

5. COMMAND OPTIONS

"imagetopdf" accepts the following CUPS standard options;

fitplot
mirror
PageSize
page-left, page-right, page-bottom, page-top
OutputOrder
Collate
sides
cupsEvenDuplex
position
scaling
ppi
natural-scaling
landscape
orientation-requested

See the CUPS documents for details of these options.

6. KOWN PROBLEMS

Problem:
  PBM and SUN raster images can not be printed.
Solution:
  Due to the CUPS libcupsimage library's bug. Update the CUPS on your system.
  
7. INFORMATION FOR DEVELOPERS

Following information is for developers, not for driver users.

7.1 Options handled by a printer or "imagetopdf"

Following options are handled by a printer or "imagetopdf".
  Collate, Copies, Duplex, OutputOrder

Which handles these options depends on following options and attributes.
  Collate, Copies, Duplex, OutputOrder, cupsEvenDuplex, cupsManualCopies

"imagetopdf" judges whether a printer can handle these options according to
the followings option settings in a PPD file.

Collate:
  If Collate is defined, "imagetopdf" judges the printer supports Collate.
Copies:
  If cupsManualCopies is defined as False, "imagetopdf" judges the printer
  does not support Copies feature.
　
Duplex:
  If Duplex is defined, "imagetopdf" judges the printer supports Duplex.
  If cupsEvenDuplex is True, Number of pages must be even.
OutputOrder:
  If OutputOrder is defined, "imagetopdf" judges the printer supports
  OutputOrder.

If the printer cannot handle these options, "imagetopdf" handles it.

Following pseudo program describes how "imagetopdf" judges to handle
these options.

Variables

Copies : specified Copies
Duplex : specified Duplex 
Collate : specified Collate
OutputOrder : specified OutputOrder
EvenDuplex : specified cupsEvenDuplex
pages : number of pages
number_up : specified number-up

device_copies : Copies passed to the printer
device_duplex : Duplex passed to the printer
device_collate : Collate passed to the printer
device_outputorder : OutputOrder passed to the printer

soft_copies : copies by imagetopdf


device_copies = 1;
device_duplex = False;
device_collate = False;
device_outputorder = False;

if (Copies == 1) {
  /* Collate is not needed. */
  Collate = False;
}

if (!Duplex) {
  /* EvenDuplex is not needed */
  EvenDuplex = False;
}


if (Copies > 1 && the printer can handle Copies) device_copies = Copies;
if (Duplex && the printer can handle Duplex) {
       device_duplex = True;
} else {
   /* imagetopdf cannot handle Duplex */
}
if (Collate && the printer can handle Collate) device_collate = True;
if (OutputOrder == Reverse && the printer can handle OutputOrder)
             device_outputorder = True;

if (Collate && !device_collate) {
   /* The printer cannot handle Collate.
      So imagetopdf handle Copies */
              device_copies = 1;
}

if (device_copies != Copies /* imagetopdf handle Copies */ && Duplex)
    /* Make imagetopdf handle Collate, otherwise both paper side may have
       same page */
              Collate = True;
              device_collate = False;
}

if (Duplex && Collate && !device_collate) {
   /* Handle EvenDuplex, otherwise the last page has
      the next copy's first page in the other side of the paper. */
   EvenDuplex = True;
}

if (Duplex && OutputOrder == Reverse && !device_outputorder) {
   /* Handle EvenDuplex, otherwise the first page's other side of paper
      is empty. */
   EvenDuplex = True;
}

soft_copies = device_copies > 1 ? 1 : Copies;

7.2 JCL

When you print PDF files to a PostScript(PS) printer, you can specify
device options in PS. In this case, you can write PS commands in a PPD file
like as follows.

*OpenUI *Resolution/Resolution : PickOne
*DefaultResolution: 600
*Resolution 300/300 dpi: "<</HWResolution[300 300]>>setpagedevice"
*Resolution 600/600 dpi: "<</HWResolution[600 600]>>setpagedevice"
*CloseUI: *Resolution

However, if options cannot be described in PS file, you can write JCLs
as follows;

*JCLOpenUI *JCLFrameBufferSize/Frame Buffer Size: PickOne
*DefaultJCLFrameBufferSize: Letter
*OrderDependency: 20 JCLSetup *JCLFrameBufferSize
*JCLFrameBufferSize Off: '@PJL SET PAGEPROTECT = OFF<0A>'
*JCLFrameBufferSize Letter: '@PJL SET PAGEPROTECT = LTR<0A>'
*JCLFrameBufferSize Legal: '@PJL SET PAGEPROTECT = LGL<0A>'
*JCLCloseUI: *JCLFrameBufferSize

Because PDF cannot specify device options in a PDF file, you have to define
all the device options as JCLs.

When a printer does not support PS nor PDF, you can use Ghostscript (GS).
In this case, you can specify device options like a PS printer.
If you want to use the same printer and same PPD file for both PDF and PS
printing, when you print a PS file, you can specify that GS handles it,
and when you print a PDF file, you can also specify that PDF filters handle
it in the same PPD file. However in this case, previous methods is not
appropriate to specify device options.

So, "imagetopdf" handles this case as follows;
(In following pseudo program, JCL option is an option specified with JCLOpenUI)

if (Both JCLBegin and JCLToPSInterpreter are specified in the PPD file) {
    output JCLs that marked JCL options.
}

if (pdftopdfJCLBegin attribute is specified in the PPD file) {
    output it's value
}

if (Copies option is specified in the PPD file) {
    mark Number of copies specified
} else if (pdftopdfJCLCopies is specified in the PPD file) {
    output JCL specified with JCLCopies
}

for (each marked options) {
    if (pdftopdfJCL<marked option's name> is specified in the PPD file) {
	output it's value as a JCL
    } else if (pdftopdfJCLBegin attributes is specified in the PPD file) {
	output "<option's name>=<marked choice>;" as a JCL
    }
}
output NEWLINE

Thus, if you want to use both PDF filters and GS by single PPD file,
what you should do is to add the following line in the PPD file;

*pdftopdfJCLBegin: "pdftoopvp jobInfo:"

Note:
  If you specify JCLBegin, you have to specify JCLToPSInterpreter as well.

Note:
  When you need to specify the value which is different from the choosen
  value based on the PPD into the jobInfo, you have to specify the values
  with the key started by "pdftopdfJCL" string.

  For example, if the page size is defined in a PPD file as following;

  *OpenUI *PageSize/Page Size: PickOne
  *DefaultPageSize: A4
  *PageSize A4/A4:
  *PageSize Letter/US Letter:
  *CloseUI: *PageSize

  if you choose the page size "Letter", the string "PageSize=Letter;" is
  added to jobInfo. On the other hand, if the driver requires the different
  value for the "Letter" size, for instance driver requires "PS=LT;"
  instead of "PageSize=Letter;" as the jobInfo value, the PPD file has to
  be defined as following;

  *OpenUI *PageSize/Page Size: PickOne
  *DefaultPageSize: A4
  *PageSize A4/A4:
  *pdftopdfJCLPageSize A4/A4: "PS=A4;"
  *PageSize Letter/US Letter:
  *pdftopdfJCLPageSize Letter/US Letter: "PS=LT;"
  *CloseUI: *PageSize

7.3 Temporally files location

"imagetopdf" creates temporally files if needed. Temporary files are created
in the location specified by TMPDIR environment variable. Default location
is "/tmp".


PDFTOPDF

1. INTRODUCTION

There are two executable programs, "pdftopdf" and "pdf2pdf", in this package.
"pdftopdf" is a filter for CUPS. It reads PDF files, changes page layout
and output a new PDF file. "pdf2pdf" is a user command version of "pdftopdf".
This document describes about only "pdftopdf". See man/pdf2pdf.1 for "pdf2pdf".

When a input PDF file which is given to "pdftopdf" does not include some
required fonts, and if you set the font embedding option, "pdftopdf" embed
fonts in the new PDF file 

"pdftopdf" finds fonts to embed by fontconfig library.
"pdftopdf" now embed only TrueType format fonts.
PDF Standard fonts are not embedded .

Note: Do not embed fonts that the font licenses inhibit embedding.

"pdftopdf" does not support functions that are not related to printing
features, including interactive features and document interchange features.
Many of these operators and sections are just ignored.
Some of these may be output, but those functions are not assured.
Encryption feature is not supported.

2. LICENSE

Almost source files are under MIT like license. However, "pdftopdf" links
some "poppler" libraries, and these files are under GNU public license.
See copyright notice of each file for details.

3. COMMAND LINE

"pdftopdf" is a CUPS filter, and the command line arguments, environment
variables and configuration files are in accordance with the CUPS filter
interface.

pdftopdf <job> <user> <title> <num-copies> <options> [<filename>]

"pdftopdf" ignores <job> and <user>.
<title> is appended into the PDF dictionary as /Title.
<num-copies> specifies the number of document copies.
<options> is a CUPS option list.
<filename> is an input PDF file name.

When omit the <filename>, "pdftopdf" reads a PDF file from stdin,
and save it as a temporary file.


CUPS options defined in <options> are delimited by space. Boolean
type CUPS option is defined only by the option key, and other type
CUPS option are defined by pairs of key and value, <key>=<value>.

4. COMMAND OPTIONS

"pdftopdf" accepts the following CUPS standard options;

fiplot
mirror
PageSize
page-left, page-right, page-bottom, page-top
number-up
number-up-layout
page-border
OutputOrder
page-set
page-ranges
Collate
sides
cupsEvenDuplex

See the CUPS documents for details of these options.

Margin given by the page-left, page-right, page-bottom and page-top is
valid when fiplot or number-up option is set.

"pdftopdf" accepts the following original options;

pdftopdfFontEmbedding
  Boolean option.
  Force "pdftopdf" to embed fonts. "pdftopdf" does not embed fonts by default.

pdftopdfFontEmbeddingWhole
  Boolean option.
  Force "pdftopdf" to embed whole fonts.
  "pdftopdf" does not embed whole fonts by default.
  If this option is false, "pdftopdf" embed subset of the fonts.
  This option is valid only when the pdftopdfFontEmbedding option is true.

pdftopdfFontEmbeddingPreLoad
  Boolean option.
  Force "pdftopdf" to embed fonts specified as pre-loaded fonts in a PPD file.
  "pdftopdf" does not embed pre-loaded fonts by default.
  If this option is false, pdftopdf does not embed pre-loaded fonts.
  This option is valid only when the pdftopdfFontEmbedding option is true.

pdftopdfFontCompress
  Boolean option.
  Force "pdftopdf" to compress embed fonts.
  "pdftopdf" does not compress embed fonts by default.
  This option is valid only when the pdftopdfFontEmbedding option is true.

pdftopdfContentsCompress
  Boolean option.
  Force "pdftopdf" to compress page contents.
  "pdftopdf" does not compress page contents by default.

pdftopdfJCLBegin
  Boolean option.
  Force "pdftopdf" to create JCL info for the following PDF filter
  "pdftoopvp".

5. INFORMATION FOR DEVELOPERS

Following information is for developers, not for driver users.

5.1 Options handled by a printer or "pdftopdf"

Following options are handled by a printer or "pdftopdf".
  Collate, Copies, Duplex, OutputOrder

Which handles these options depends on following options and attributes.
  Collate, Copies, Duplex, OutputOrder, cupsEvenDuplex, cupsManualCopies

"pdftopdf" judges whether a printer can handle these options according to
the followings option settings in a PPD file.

Collate:
  If Collate is defined, "pdftopdf" judges the printer supports Collate.
Copies:
  If cupsManualCopies is defined as False, "pdftopdf" judges the printer
  does not support Copies feature.
Duplex:
  If Duplex is defined, "pdftopdf" judges the printer supports Duplex.
  If cupsEvenDuplex is True, Number of pages must be even.
OutputOrder:
  If OutputOrder is defined, "pdftopdf" judges the printer supports
  OutputOrder.

If the printer cannot handle these options, "pdftopdf" handles it.

Following pseudo program describes how "pdftopdf" judges to handle
these options.

Variables

Copies : specified Copies
Duplex : specified Duplex 
Collate : specified Collate
OutputOrder : specified OutputOrder
EvenDuplex : specified cupsEvenDuplex
pages : number of pages
number_up : specified number-up

device_copies : Copies passed to the printer
device_duplex : Duplex passed to the printer
device_collate : Collate passed to the printer
device_outputorder : OutputOrder passed to the printer

soft_copies : copies by pdftopdf


device_copies = 1;
device_duplex = False;
device_collate = False;
device_outputorder = False;

if (Copies == 1) {
  /* Collate is not needed. */
  Collate = False;
}

if (!Duplex) {
  /* EvenDuplex is not needed */
  EvenDuplex = False;
}


if (Copies > 1 && the printer can handle Copies) device_copies = Copies;
if (Duplex && the printer can handle Duplex) {
       device_duplex = True;
} else {
   /* pdftopdf cannot handle Duplex */
}
if (Collate && the printer can handle Collate) device_collate = True;
if (OutputOrder == Reverse && the printer can handle OutputOrder)
             device_outputorder = True;

if (Collate && !device_collate) {
   /* The printer cannot handle Collate.
      So pdftopdf handle Copies */
              device_copies = 1;
}

if (device_copies != Copies /* pdftopdf handle Copies */ && Duplex)
    /* Make pdftopdf handle Collate, otherwise both paper side may have
       same page */
              Collate = True;
              device_collate = False;
}

if (Duplex && Collate && !device_collate) {
   /* Handle EvenDuplex, otherwise the last page has
      the next copy's first page in the other side of the paper. */
   EvenDuplex = True;
}

if (Duplex && OutputOrder == Reverse && !device_outputorder) {
   /* Handle EvenDuplex, otherwise the first page's other side of paper
      is empty. */
   EvenDuplex = True;
}

soft_copies = device_copies > 1 ? 1 : Copies;

5.2 JCL

When you print PDF files to a PostScript(PS) printer, you can specify
device options in PS. In this case, you can write PS commands in a PPD file
like as follows.

*OpenUI *Resolution/Resolution : PickOne
*DefaultResolution: 600
*Resolution 300/300 dpi: "<</HWResolution[300 300]>>setpagedevice"
*Resolution 600/600 dpi: "<</HWResolution[600 600]>>setpagedevice"
*CloseUI: *Resolution

However, if options cannot be described in PS file, you can write JCLs
as follows;

*JCLOpenUI *JCLFrameBufferSize/Frame Buffer Size: PickOne
*DefaultJCLFrameBufferSize: Letter
*OrderDependency: 20 JCLSetup *JCLFrameBufferSize
*JCLFrameBufferSize Off: '@PJL SET PAGEPROTECT = OFF<0A>'
*JCLFrameBufferSize Letter: '@PJL SET PAGEPROTECT = LTR<0A>'
*JCLFrameBufferSize Legal: '@PJL SET PAGEPROTECT = LGL<0A>'
*JCLCloseUI: *JCLFrameBufferSize

Because PDF cannot specify device options in a PDF file, you have to define
all the device options as JCLs.

When a printer does not support PS nor PDF, you can use Ghostscript (GS).
In this case, you can specify device options like a PS printer.
If you want to use the same printer and same PPD file for both PDF and PS
printing, when you print a PS file, you can specify that GS handles it,
and when you print a PDF file, you can also specify that PDF filters handle
it in the same PPD file. However in this case, previous methods is not
appropriate to specify device options.

So, "pdftopdf" handles this case as follows;
(In following pseudo program, JCL option is a option specified with JCLOpenUI)

if (Both JCLBegin and JCLToPSInterpreter are specified in the PPD file) {
    output JCLs that marked JCL options.
}

if (pdftopdfJCLBegin attribute is specified in the PPD file) {
    output it's value
}

if (Copies option is specified in the PPD file) {
    mark Number of copies specified
} else if (pdftopdfJCLCopies is specified in the PPD file) {
    output JCL specified with JCLCopies
}

for (each marked options) {
    if (pdftopdfJCL<marked option's name> is specified in the PPD file) {
	output it's value as a JCL
    } else if (pdftopdfJCLBegin attributes is specified in the PPD file) {
	output "<option's name>=<marked choice>;" as a JCL
    }
}
output NEWLINE

Thus, if you want to use both PDF filters and GS by single PPD file,
what you should do is to add the following line in the PPD file;

*pdftopdfJCLBegin: "pdftoopvp jobInfo:"

Note:
  If you specify JCLBegin, you have to specify JCLToPSInterpreter as well.

Note:
  When you need to specify the value which is different from the choosen
  value based on the PPD into the jobInfo, you have to specify the values
  with the key started by "pdftopdfJCL" string.

  For example, if the page size is defined in a PPD file as following;

  *OpenUI *PageSize/Page Size: PickOne
  *DefaultPageSize: A4
  *PageSize A4/A4:
  *PageSize Letter/US Letter:
  *CloseUI: *PageSize

  if you choose the page size "Letter", the string "PageSize=Letter;" is
  added to jobInfo. On the other hand, if the driver requires the different
  value for the "Letter" size, for instance driver requires "PS=LT;"
  instead of "PageSize=Letter;" as the jobInfo value, the PPD file has to
  be defined as following;

  *OpenUI *PageSize/Page Size: PickOne
  *DefaultPageSize: A4
  *PageSize A4/A4:
  *pdftopdfJCLPageSize A4/A4: "PS=A4;"
  *PageSize Letter/US Letter:
  *pdftopdfJCLPageSize Letter/US Letter: "PS=LT;"
  *CloseUI: *PageSize

5.3 Special PDF comment

"pdftopdf" outputs the following special comments from the 4th line in the
created PDF data.

%%PDFTOPDFNumCopies : <copies> --- <copies> specified Number of Copies
%%PDFTOPDFCollate : <collate> --- <collate> is true or false

5.4 Temporally files location

"pdftopdf" creates temporally files if needed.　Temporary files are created
in the location specified by TMPDIR environment　variable. Default location
is "/tmp".


TEXTTOPDF

This implements a texttopdf filter, and is derived from cups' texttops.

To configure:
-------------

- texttopdf uses a CUPS_DATADIR/charset/pdf.* (e.g. pdf.utf-8) for
  font configuration. All the fonts named here MUST also be present
  under CUPS_DATADIR/fonts/ as TrueType fonts for texttopdf to work.
  For TrueType Collections (.TTC) you'll have to append '/' and the 
  number of the font in the collection to the filename charsets/pdf.utf-8
  (resp. charsets/pdf.* ), for example to use the second font of uming.ttc 
  use the filename uming.ttc/1

- There are examples of pdf.utf-8 in the cups/data directory,
  you may use one of these (don't forget to copy / symlink the fonts).

To use:
-------

The filter is called just like any other cups filter. Have a
look at test.sh for example. 

Known Issues
------------
(Release 0.0.1)

 - text extraction does not work (at least for pdftotext from xpdf)
   for the resulting pdfs.

 - text wrapping in pretty-printing mode does not respect double-wide
   characters (CJK), and thus produce wrong results (wrap too late)
   for lines where they occur.  The fix is not trivial, since all the
   pretty-printing processing is done without knowledge of / prior to
   the font configuration (which is where single or double width
   code-ranges are specified).

 - The hebrew example in test5.pdf shows one of our limitations:
   Compose glyphs are not composed with the primary glyph but printed
   as separate glyphs.


PDFTORASTER

1. INTRODUCTION

"pdftoraster" is a filter for CUPS. It reads PDF files, convert it and
output CUPS raster.

"pdftoraster" does not support functions that are not related to printing
features, including interactive features and document interchange features.
Many of these operators and sections are just ignored.
Some of these may be output, but those functions are not assured.
Encryption feature is not supported.

2. LICENSE

Almost source files are under MIT like license. However, "pdftoraster" links
some "poppler" libraries, and these files are under GNU public license.
See copyright notice of each file for details.

3. COMMAND LINE

"pdftoraster" is a CUPS filter, and the command line arguments, environment
variables and configuration files are in accordance with the CUPS filter
interface.

pdftoraster <job> <user> <title> <num-copies> <options> [<filename>]

"pdftoraster" ignores <job> and <user>.
<title> is appended into the PDF dictionary as /Title.
<num-copies> specifies the number of document copies.
<options> is a CUPS option list.
<filename> is an input PDF file name.

When omit the <filename>, "pdftoraster" reads a PDF file from the stdin,
and save it as a temporary file.

4. ENVIRONMENT VARIABLES

This program refers the following environment variable;
   PPD:  PPD file name of the printer.

5. COMMAND OPTIONS

See CUPS documents for details.

6. INFORMATION FOR DEVELOPERS

Following information is for developers, not for driver users.

6.1 Options handled by a printer or "pdftoraster"

"pdftopdf" outputs the following special comments from the 4th line in the
created PDF data.

%%PDFTOPDFNumCopies : <copies> --- <copies> specified Number of Copies
%%PDFTOPDFCollate : <collate> --- <collate> is true or false

"pdftoraster" overrides the command line options by above two option's values.
 
6.2 Temporally files location

"pdftoraster" creates temporally files if needed. Temporary files are created
in the location specified by TMPDIR environment variable. Default location
is "/tmp".


PDFTOIJS

1. INTRODUCTION

"pdftoijs" is a filter for CUPS. It reads PDF files, converts it
and sends it to an IJS server.

2. LICENSE

Almost source files are under MIT like license. However, "pdftoijs" links
some "poppler" libraries, and these files are under GNU public license.
See copyright notice of each file for details.

3. COMMAND LINE

"pdftoijs" is a CUPS filter, and the command line arguments, environment
variables and configuration files are in accordance with the CUPS filter
interface.

pdftoijs <job> <user> <title> <num-copies> <options> [<filename>]

"pdftoijs" ignores <job> and <user>.
<title> is appended into the PDF dictionary as /Title.
<num-copies> specifies the number of document copies.
<options> is a CUPS option list.
<filename> is an input PDF file name.

When omit the <filename>, "pdftoijs" reads a PDF file from the stdin,
and save it as a temporary file.

4. ENVIRONMENT VARIABLES

This program refers the following environment variable;
   PPD:  PPD file name of the printer.

5. NEW PPD KEYWORDS

*ijsServer : the ijsserver executable
*ijsManufacturer, *ijsModel : as used by the ijs server
*ijsColorspace : the desired output colorspace, one of
                 'rgb'
                 'cmyk' (availability depending on poppler compile-options)
                 'white1', 'black1':  1-bit normal/inverted
                 'white8', 'black8':  8-bit greyscale normal/inverted
*ijsResolution [option]=[choice]: the desired output resolution e.g. "600 600"
*ijsParams [option]=[choice]: custom ijs parameters, separated by ','
                 (to escape: use \,)

6. COMMAND OPTIONS

(See CUPS documents for details.)

ijsOutputFile : the destination file, stdout otherwise

7. INFORMATION FOR DEVELOPERS

Following information is for developers, not for driver users.

7.1 Temporally files location

"pdftoijs" creates temporally files if needed. Temporary files are created
in the location specified by TMPDIR environment variable. Default location
is "/tmp".


PDFTOOPVP

1. INTRODUCTION

"pdftoopvp" is a CUPS filter which reads PDF file, renders pages and
outputs PDL to a printer driver which is compliant with the OpenPrinting
Vector Printer Driver Interface "opvp".

2. CONFIGURATION

"pdftoopvp" refers the poppler configuration file. Be aware that poppler
uses "fontconfig" for its font configuration.

3. JCL

When "pdftoopvp" reads a PDF file from stdin, "pdftoopvp" handles the data
prior to PDF header (%PDF ...) as JCL options. JCL options for "pdftoopvp"
must begin with "pdftoopvp jobInfo:". "pdftoopvp" passes the option string
just after ":" to the driver as the jobInfo option.

4. COMMAND LINE

"pdftoopvp" is a CUPS filter, and the command line arguments,
environment variables and configuration files are in accordance with
the CUPS filter interface.

pdftoopvp <job> <user> <title> <num-copies> <options> [<filename>]

"pdftoopvp" ignores <job>, <user>, <title> and <num-copies>.
<options> is a CUPS option list.

When omit the <filename>, "pdftoopvp" reads a PDF file from stdin,
and save it as a temporary file.

CUPS options defined in <options> are delimited by space. Boolean
type CUPS option is defined only by the option key, and other type
CUPS option are defined by pairs of key and value, <key>=<value>.

5. COMMAND OPTIONS

"pdftoopvp" accepts the following CUPS standard options;

Resolution=<int>
  Specifies a printer resolution in dpi.
  When this option is omitted, the resolution is treated as 300dpi.
  Horizontal and vertical resolution are treated as the same resolution.

PageSize=<string>
  Specifies a paper size by name defined in the PPD file.
  This option is ignored when no PPD file is assigned for the printer
  queue.

"pdftoopvp" accepts the following original options;

opvpDriver=<string>
  Specifies a driver library name.

opvpModel=<string>
  Specifies a printer model name.

opvpJobInfo=<string>
  Specifies "jobInfo" printing options that are passed to the driver.
  Printing options are overridden by JCL options.

opvpDocInfo=<string>
  Specifies "docInfo" document options that are passed to the driver.

opvpPageInfo=<string>
  Specifies "pageInfo" page options that are passed to the driver.

pdftoopvpClipPathNotSaved (Boolean option)
  Specifies that the driver cannot save clipping path operators in PDF.

nopdftoopvpShearImage (Boolean option)
  Specifies that the driver cannot rotate/shear images by CTM.

nopdftoopvpMiterLimit (Boolean option)
  Specifies that the driver does not support miter limit.
  If the driver does not prepare the opvpSetMiterLimit function entry,
  this option setting is ignored, and also miter limit is ignored.

pdftoopvpIgnoreMiterLimit (Boolean option)
  When nopdftoopvpMiterLimit option is set, pdftoopvp automatically
  replace paths to multiple lines or drawing images. This option
  specifies to avoid the path replacement even when nopdftoopvpMiterLimit
  option is set.

pdftoopvpMaxClipPathLength=<int>
  Specifies the maximum number of clipping path points that the driver
  supports. Default value is 2000 points.

pdftoopvpMaxFillPathLength=<int>
  Specifies the maximum number of fill path points that the driver supports.
  Default value is 4000 points.

nopdftoopvpLineStyle (Boolean option)
  Specifies that the driver ignores the line style settings in PDF.
  If the driver does not prepare the SetLineStyle , SetLineDash or
  SetLineDashOffset function entry, this option setting is ignored, and
  also line style, line dash and line dash offset are ignored.

nopdftoopvpClipPath (Boolean option)
  Specifies that the driver does not support clipping path.
  If the driver does not prepare the opvpSetClipPath function entry, this
  option is ignored, and also clip path setting is ignored.

nopdftoopvpBitmapChar (Boolean option)
  Specifies that the driver does not output characters as images.
  Default setting is that "pdftoopvp" outputs small characters as images.

pdftoopvpBitmapCharThreshold=<int>
  Specifies the threshold value that "pdftoopvp" outputs characters as
  images. Threshold value is defined as W x H where character's width
  is given by W pixels and height is given by H pixels.
  Default threshold value is 2000 points.

nopdftoopvpImageMask (Boolean option)
  Specifies that the driver does not support image mask.
  If this option is set, "pdftoopvp" treats as the nopdftoopvpBitmapChar
  option is given.

6. PPD OPTIONS

Following options can be defined in a PPD.

Resolution=<int>
PageSize=<string>
opvpDriver=<string>
opvpModel=<string>
opvpJobInfo=<string>
opvpDocInfo=<string>
opvpPageInfo=<string>
pdftoopvpClipPathNotSaved=True
pdftoopvpShearImage=False
pdftoopvpMiterLimit=False
pdftoopvpIgnoreMiterLimit=True
pdftoopvpMaxClipPathLength=<int>
pdftoopvpMaxFillPathLength=<int>
pdftoopvpLineStyle=False
pdftoopvpClipPath=False
pdftoopvpBitmapChar=False
pdftoopvpBitmapCharThreshold=<int>
pdftoopvpImageMask=False

7. OPTIONS OVERRIDING RULE

"jobInfo" printing options in a PPD is used as a initial "jobInfo" printing
options. If opvpJobInfo option is given in the command line, precedent
"jobInfo" printing options are overridden by the opvpJobInfo options.

After the "jobInfo" printing options are overridden by the opvpJobInfo
options, if JCL options are given, precedent "jobInfo" printing options are
overridden by the options given by JCL options.

8. INFORMATION FOR CUPS 1.1

To use this program under CUPS 1.1, following lines must be defined
in the CUPS's "mime.types" file.

application/vnd.cups-pdf

9. KNOWN PROBLEMS

Problem:
  When a page is rotated and a character is small, character might not be
  rotated correctly. This problem is caused by free type library.
Solution:
  Define the nopdftoopvpBitmapChar to inhibit characters output as images.


README - OpenPrinting CUPS Filters v1.0b1 - 2012-01-26
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

    For compiling and using this package CUPS, Poppler, libjpeg, libpng, and
    libtiff are needed. It is highly recommended, especially if non-PostScript
    printers are used, to have Ghostscript, foomatic-filters, and foomatic-db
    installed.

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

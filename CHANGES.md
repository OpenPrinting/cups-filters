# CHANGES - OpenPrinting CUPS Filters v2.0.0 - 2023-09-22

## CHANGES IN V2.0.0 (22th September 2023)

- `universal`: Enable `application/vnd.cups-postscript` as input
  There are filters which produce this MIME type (such as `hpps` of
  HPLIP), and if someone uses such driver on a client and the server
  has an IPP Everywhere/driverless printer, the job fails (Pull
  request #534).


## CHANGES IN V2.0rc2 (20th June 2023)

- beh backend: Use `execv()` instead of `system()` - CVE-2023-24805
  With `execv()` command line arguments are passed as separate strings
  and not the full command line in a single string. This prevents
  arbitrary command execution by escaping the quoting of the arguments
  in a job with forged job title.

- beh backend: Extra checks against odd/forged input - CVE-2023-24805

  * Do not allow `/` in the scheme of the URI (= backend executable
    name), to assure that only backends inside
    `/usr/lib/cups/backend/` are used.

  * Pre-define scheme buffer to empty string, to be defined for case
    of URI being NULL.

  * URI must have `:`, to split off scheme, otherwise error.

  * Check return value of `snprintf()` to create call path for
    backend, to error out on truncation of a too long scheme or on
    complete failure due to a completely odd scheme.

- beh backend: Further improvements - CVE-2023-24805

  * Use `strncat()` instead of `strncpy()` for getting scheme from
    URI, the latter does not require setting terminating zero byte in
    case of truncation.

  * Also exclude `.` or `..` as scheme, as directories are not valid
    CUPS backends.

  * Do not use `fprintf()` in `sigterm_handler()`, to not interfere
    with a `fprintf()` which could be running in the main process when
    `sigterm_handler()` is triggered.

  * Use `static volatile int` for global variable job_canceled.

- `parallel` backend: Added missing `#include` lines


## CHANGES IN V2.0rc1 (12th April 2023)

- foomatic-rip: Fix a SIGPIPE error when calling gs (Pull request #517)
  [Ubuntu's autopkgtest for
  foo2zjs](https://autopkgtest.ubuntu.com/packages/f/foo2zjs/lunar/ppc64el)
  shows foo2zjs's testsuite failing with cups-filters 2.0beta3 on
  ppc64el. This is cause by a timing issue in foomatic-rip which is
  fixed now.

- Coverity check done by Zdenek Dohnal for the inclusion of
  cups-filters in Fedora and Red Hat. Zdenek has fixed all the issues:
  Missing `free()`, files not closed, potential string overflows,
  ... Thanks a lot! (Pull request #510).

- Dropped all C++ references and obsolete C standards (Pull requests
  #504 and #513)
  With no C++ compiler needed, there is no need for any checks or
  setting for C++ in configure.ac.

- configure.ac: Change deprecated AC_PROG_LIBTOOL for LT_INIT (Pull
  request #508)


## CHANGES IN V2.0b3 (31st January 2023)

- texttopdf: Do not include fontconfig.h in the CUPS filter wrapper

- Build system: Do not explicitly check for libpoppler-cpp
  The cups-filters package does not contain any code using
  libpoppler-cpp, therefore we let ./configure not check for it.

- COPYING, NOTICE: Simplification for autotools-generated files
  autotools-generated files can be included under the license of the
  upstream code, and FSF copyright added to upstream copyright
  list. Simplified COPYING appropriately.

- Makefile.am: Include LICENSE in distribution tarball

- Add templates for issue reports on GitHub. This makes a selection
  screen appear when clicking "New Issue" in the web UI, to selct
  whether the issue is a regular bug, a feature request, or a security
  vulnerability.


## CHANGES IN V2.0b2 (8th January 2023)

- Corrected installation path for *.h files of *.drv files.  The ppdc
  (and underlying functions) of libppd searches for include files in
  /usr/share/ppdc and not in /usr/share/cups/ppdc any more.

- configure.ac: Remove unnecessary "AVAHI_GLIB_..." definitions.

- Makefile.am: Include NOTICE in distribution tarball

- configure.ac: Added "foreign" to to AM_INIT_AUTOMAKE() call. Makes
  automake not require a file named README.

- Cleaned up .gitignore

- Tons of fixes in the source code documentation: README.md, INSTALL,
  DEVELOPING.md, CONTRIBUTING.md, COPYING, NOTICE, ... Adapted to the
  cups-filters component, added links.


## CHANGES IN V2.0b1 (18th November 2022)

- Converted nearly all filters to filter functions, only exceptions
  are `rastertoescpx`, `rastertopclx`, `commandtoescpx`,
  `commandtopclx`, and `foomatic-rip`. The latter is deeply involved
  with Foomatic PPDs and the others are legacy printer drivers. The
  filter functions are mainly in libcupsfilters, the ones which
  generate PostScript are in libppd.

- Replaced all the filters converted to filter functions by simple
  wrapper executables using `ppdFilterCUPSWrapper()` of libppd for
  backward compatibility with CUPS 2.x.

- Added new streaming mode triggered by the boolean
  "filter-streaming-mode" option. In this mode a filter (function) is
  supposed to avoid everything which prevents the job data from
  streaming, as loading the whole job (or good part of it) into a
  temporary file or into memory, interpreting PDF, pre-checking input
  file type or zero-page jobs, ... This is mainly to be used by
  Printer Applications when they do raster printing in streaming mode,
  to run with lowest resources possible. Currently `foomatic-rip`,
  `ghostscript`, and `pdftopdf` got a streaming mode. For the former
  two PostScript (not PDF) is assumed as input and no zero-page-job
  check is done, in the latter all QPDF processing (page management,
  page size adjustment, ...) is skipped and only JCL according to the
  PPD added.

- The CUPS filter `imagetops` uses the `ppdFilterImageToPS()` filter
  function of libppd now.

- `driverless`, `driverless-fax`: Added IPP Fax Out support. Now
  printer setup tools list an additional fax "driver". A fax queue is
  created by selecting this driver. Jobs have to be sent with "-o
  phone=12345" to supply the destination phone number (Pull request
  #280, #293, #296, #302, #304, #305, #306, #309, Issue #298, #308).

- `sys5ippprinter`: Removed `sys5ippprinter`, as CUPS does not support
  System V interface scripts any more. This first approach of PPD-less
  printing was also not actually made use of.

- `urftopdf`: Removed as we require CUPS 2.2.2+ now which supports
  Apple Raster by itself.

- Build system, `README.md`: Require CUPS 2.2.2+. Removed now unneeded
  `./configure` switches for use of the `urftopdf` filter for old CUPS
  versions.

- Sample PPDs: Renamed source directory from `ppd/` to `ppdfiles/`.

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Build system: Add files in `.gitignore` that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).

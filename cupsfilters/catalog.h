//
//  This file is part of cups-filters.
//
//  This file is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as
//  published by the Free Software Foundation; either version 2.1 of the
//  License, or (at your option) any later version.
//
//  This file is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
//  Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with avahi; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
//  USA.
//

//
// Human-readable strings for printer or scanner option/attribute
// names and also the names for the possible choices for each option
// are essentially important for user-friendly print dialogs and other
// user interfaces. Also translations into the user-chosen user
// interface language should be used.
//
// To allow for human-readable names and translations/localizations we
// use the standard set of human-readable strings from the PWG:
//
// https://ftp.pwg.org/pub/pwg/ipp/examples/ipp.pot
// https://ftp.pwg.org/pub/pwg/ipp/examples/ipp.strings
//
// Translations are supposed to go here (but none are available yet):
//
// https://github.com/istopwg/ippregistry/tree/master/localizations
//
// These standard strings are also part of CUPS' translation files:
//
// https://github.com/OpenPrinting/cups/tree/master/locale
//
// Here translations take actually place as part of the translations
// of CUPS itself.
//
// We take the files from the CUPS installed into the system. It is
// not complicated to check the user language in the printer's IPP
// response via the attributes-natural-language attribute and then
// request an appropriate language version of the files if
// available. The printer-specific strings are downloaded from the
// printer following the URI in the printer-strings-uri attribute and
// are in the selected language.
//
// See also:
//
// https://lists.linuxfoundation.org/pipermail/printing-architecture/2021/003992.html
// https://lists.linuxfoundation.org/pipermail/printing-architecture/2021/003995.html
//


#ifndef _CUPS_FILTERS_CATALOG_H_
#  define _CUPS_FILTERS_CATALOG_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus

//
// Include necessary headers...
//

#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#if defined(WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif // WIN32 || __EMX__

#include <cups/cups.h>
#include <cups/backend.h>
#include <cups/raster.h>

// Data structure for IPP choice name and human-readable string
typedef struct catalog_choice_strings_s {
  char *name,
       *human_readable;
} catalog_choice_strings_t;

// Data structure for IPP option name, human-readable string, and choice list
typedef struct catalog_opt_strings_s {
  char *name,
       *human_readable;
  cups_array_t *choices;
} catalog_opt_strings_t;


//
// Prototypes...
//

int             cfGetURI(const char *url, char *name, size_t namesize);
const char      *cfCatalogSearchDir(const char *dirname);
const char      *cfCatalogFind(const char *preferreddir);
void            cfCatalogFreeChoiceStrings(void* entry, void* user_data);
void            cfCatalogFreeOptionStrings(void* entry, void* user_data);
cups_array_t    *cfCatalogOptionArrayNew();
catalog_opt_strings_t *cfCatalogFindOption(cups_array_t *options, char *name);
catalog_choice_strings_t *cfCatalogFindChoice(cups_array_t *choices,
					      char *name);
catalog_opt_strings_t *cfCatalogAddOption(char *name, char *human_readable,
					  cups_array_t *options);
catalog_choice_strings_t *cfCatalogAddChoice(char *name, char *human_readable,
					     char *opt_name,
					     cups_array_t *options);
char            *cfCatalogLookUpOption(char *name, cups_array_t *options,
				       cups_array_t *printer_options);
char            *cfCatalogLookUpChoice(char *name, char *opt_name,
				       cups_array_t *options,
				       cups_array_t *printer_options);
void            cfCatalogLoad(const char *location, cups_array_t *options);


#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_CATALOG_H_

/* spooler.c
 *
 * Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
 * Copyright (C) 2008 Lars Uebernickel <larsuebernickel@gmx.de>
 *
 * This file is part of foomatic-rip.
 *
 * Foomatic-rip is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Foomatic-rip is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "spooler.h"
#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

const char *spooler_name(int spooler)
{
    switch (spooler) {
        case SPOOLER_CUPS: return "cups";
        case SPOOLER_DIRECT: return "direct";
    };
    return "<unknown>";
}

/*  This piece of PostScript code (initial idea 2001 by Michael
    Allerhand (michael.allerhand at ed dot ac dot uk, vastly
    improved by Till Kamppeter in 2002) lets Ghostscript output
    the page accounting information which CUPS needs on standard
    error.
    Redesign by Helge Blischke (2004-11-17):
    - As the PostScript job itself may define BeginPage and/or EndPage
    procedures, or the alternate pstops filter may have inserted
    such procedures, we make sure that the accounting routine
    will safely coexist with those. To achieve this, we force
    - the accountint stuff to be inserted at the very end of the
        PostScript job's setup section,
    - the accounting stuff just using the return value of the
        existing EndPage procedure, if any (and providing a default one
        if not).
    - As PostScript jobs may contain calls to setpagedevice "between"
    pages, e.g. to change media type, do in-job stapling, etc.,
    we cannot rely on the "showpage count since last pagedevice
    activation" but instead count the physical pages by ourselves
    (in a global dictionary).
*/
const char *accounting_prolog_code =
    "[{\n"
    "%% Code for writing CUPS accounting tags on standard error\n"
    "\n"
    "/cupsPSLevel2 % Determine whether we can do PostScript level 2 or newer\n"
    "    systemdict/languagelevel 2 copy\n"
    "    known{get exec}{pop pop 1}ifelse 2 ge\n"
    "def\n"
    "\n"
    "cupsPSLevel2\n"
    "{                    % in case of level 2 or higher\n"
    "    currentglobal true setglobal    % define a dictioary foomaticDict\n"
    "    globaldict begin        % in global VM and establish a\n"
    "    /foomaticDict            % pages count key there\n"
    "    <<\n"
    "        /PhysPages 0\n"
    "    >>def\n"
    "    end\n"
    "    setglobal\n"
    "}if\n"
    "\n"
    "/cupsGetNumCopies { % Read the number of Copies requested for the current\n"
    "            % page\n"
    "    cupsPSLevel2\n"
    "    {\n"
    "    % PS Level 2+: Get number of copies from Page Device dictionary\n"
    "    currentpagedevice /NumCopies get\n"
    "    }\n"
    "    {\n"
    "    % PS Level 1: Number of copies not in Page Device dictionary\n"
    "    null\n"
    "    }\n"
    "    ifelse\n"
    "    % Check whether the number is defined, if it is \"null\" use #copies \n"
    "    % instead\n"
    "    dup null eq {\n"
    "    pop #copies\n"
    "    }\n"
    "    if\n"
    "    % Check whether the number is defined now, if it is still \"null\" use 1\n"
    "    % instead\n"
    "    dup null eq {\n"
    "    pop 1\n"
    "    } if\n"
    "} bind def\n"
    "\n"
    "/cupsWrite { % write a string onto standard error\n"
    "    (%stderr) (w) file\n"
    "    exch writestring\n"
    "} bind def\n"
    "\n"
    "/cupsFlush    % flush standard error to make it sort of unbuffered\n"
    "{\n"
    "    (%stderr)(w)file flushfile\n"
    "}bind def\n"
    "\n"
    "cupsPSLevel2\n"
    "{                % In language level 2, we try to do something reasonable\n"
    "  <<\n"
    "    /EndPage\n"
    "    [                    % start the array that becomes the procedure\n"
    "      currentpagedevice/EndPage 2 copy known\n"
    "      {get}                    % get the existing EndPage procedure\n"
    "      {pop pop {exch pop 2 ne}bind}ifelse    % there is none, define the default\n"
    "      /exec load                % make sure it will be executed, whatever it is\n"
    "      /dup load                    % duplicate the result value\n"
    "      {                    % true: a sheet gets printed, do accounting\n"
    "        currentglobal true setglobal        % switch to global VM ...\n"
    "        foomaticDict begin            % ... and access our special dictionary\n"
    "        PhysPages 1 add            % count the sheets printed (including this one)\n"
    "        dup /PhysPages exch def        % and save the value\n"
    "        end                    % leave our dict\n"
    "        exch setglobal                % return to previous VM\n"
    "        (PAGE: )cupsWrite             % assemble and print the accounting string ...\n"
    "        16 string cvs cupsWrite            % ... the sheet count ...\n"
    "        ( )cupsWrite                % ... a space ...\n"
    "        cupsGetNumCopies             % ... the number of copies ...\n"
    "        16 string cvs cupsWrite            % ...\n"
    "        (\\n)cupsWrite                % ... a newline\n"
    "        cupsFlush\n"
    "      }/if load\n"
    "                    % false: current page gets discarded; do nothing    \n"
    "    ]cvx bind                % make the array executable and apply bind\n"
    "  >>setpagedevice\n"
    "}\n"
    "{\n"
    "    % In language level 1, we do no accounting currently, as there is no global VM\n"
    "    % the contents of which are undesturbed by save and restore. \n"
    "    % If we may be sure that showpage never gets called inside a page related save / restore pair\n"
    "    % we might implement an hack with showpage similar to the one above.\n"
    "}ifelse\n"
    "\n"
    "} stopped cleartomark\n";


void init_cups(list_t *arglist, dstr_t *filelist, jobparams_t *job)
{
    char path [PATH_MAX] = "";
    char cups_jobid [128];
    char cups_user [128];
    char cups_jobtitle [128];
    char cups_copies [128];
    int cups_options_len;
    char *cups_options;
    char cups_filename [256];

    if (getenv("CUPS_FONTPATH"))
        strncpy(path, getenv("CUPS_FONTPATH"), PATH_MAX - 1);
    else if (getenv("CUPS_DATADIR")) {
       strncpy(path, getenv("CUPS_DATADIR"), PATH_MAX - 1);
       strncat(path, "/fonts", PATH_MAX - strlen(path) - 1);
    }
    if (getenv("GS_LIB")) {
        strncat(path, ":", PATH_MAX - strlen(path) - 1);
        strncat(path, getenv("GS_LIB"), PATH_MAX - strlen(path) - 1);
    }
    setenv("GS_LIB", path, 1);

    /* Get all command line parameters */
    strncpy_omit(cups_jobid, arglist_get(arglist, 0), 128, omit_shellescapes);
    strncpy_omit(cups_user, arglist_get(arglist, 1), 128, omit_shellescapes);
    strncpy_omit(cups_jobtitle, arglist_get(arglist, 2), 128, omit_shellescapes);
    strncpy_omit(cups_copies, arglist_get(arglist, 3), 128, omit_shellescapes);

    cups_options_len = strlen(arglist_get(arglist, 4));
    cups_options = malloc(cups_options_len + 1);
    strncpy_omit(cups_options, arglist_get(arglist, 4), cups_options_len + 1, omit_shellescapes);

    /* Common job parameters */
    strcpy(job->id, cups_jobid);
    strcpy(job->title, cups_jobtitle);
    strcpy(job->user, cups_user);
    strcpy(job->copies, cups_copies);
    dstrcatf(job->optstr, " %s", cups_options);

    /* Check for and handle inputfile vs stdin */
    if (list_item_count(arglist) > 4) {
        strncpy_omit(cups_filename, arglist_get(arglist, 5), 256, omit_shellescapes);
        if (cups_filename[0] != '-') {
            /* We get input from a file */
            dstrcatf(filelist, "%s ", cups_filename);
            _log("Getting input from file %s\n", cups_filename);
        }
    }

    accounting_prolog = accounting_prolog_code;

    /* On which queue are we printing?
       CUPS gives the PPD file the same name as the printer queue,
       so we can get the queue name from the name of the PPD file. */
    file_basename(job->printer, job->ppdfile, 256);

    free(cups_options);
}

/* used by init_direct to find a ppd file */
int find_ppdfile(const char *user_default_path, jobparams_t *job)
{
    /* Search also common spooler-specific locations, this way a printer
       configured under a certain spooler can also be used without spooler */

    strcpy(job->ppdfile, job->printer);
    if (access(job->ppdfile, R_OK) == 0)
        return 1;

    snprintf(job->ppdfile, 256, "%s.ppd", job->printer); /* current dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "%s/%s.ppd", user_default_path, job->printer); /* user dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "%s/direct/%s.ppd", CONFIG_PATH, job->printer); /* system dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "%s/%s.ppd", CONFIG_PATH, job->printer); /* system dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "/etc/cups/ppd/%s.ppd", job->printer); /* CUPS config dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "/usr/local/etc/cups/ppd/%s.ppd", job->printer); /* CUPS config dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;

    /* nothing found */
    job->ppdfile[0] = '\0';
    return 0;
}

/* search 'configfile' for 'key', copy value into dest, return success */
int configfile_find_option(const char *configfile, const char *key, char *dest, size_t destsize)
{
    FILE *fh;
    char line [1024];
    char *p;

    dest[0] = '\0';

    if (!(fh = fopen(configfile, "r")))
        return 0;

    while (fgets(line, 1024, fh)) {
        if (!prefixcmp(line, "default")) {
            p = strchr(line, ':');
            if (p) {
                strncpy_omit(dest, p + 1, destsize, omit_whitespace_newline);
                if (dest[0])
                    break;
            }
        }
    }
    fclose(fh);
    return dest[0] != '\0';
}

/* tries to find a default printer name in various config files and copies the
 * result into the global var 'printer'. Returns success */
int find_default_printer(const char *user_default_path, jobparams_t *job)
{
    char configfile [1024];
    char *key = "default";

    if (configfile_find_option("./.directconfig", key, job->printer, 256))
        return 1;
    if (configfile_find_option("./directconfig", key, job->printer, 256))
        return 1;
    if (configfile_find_option("./.config", key, job->printer, 256))
        return 1;
    strlcpy(configfile, user_default_path, 1024);
    strlcat(configfile, "/direct/.config", 1024);
    if (configfile_find_option(configfile, key, job->printer, 256))
        return 1;
    strlcpy(configfile, user_default_path, 1024);
    strlcat(configfile, "/direct.conf", 1024);
    if (configfile_find_option(configfile, key, job->printer, 256))
        return 1;
    if (configfile_find_option(CONFIG_PATH "/direct/.config", key, job->printer, 256))
        return 1;
    if (configfile_find_option(CONFIG_PATH "/direct.conf", key, job->printer, 256))
        return 1;

    return 0;
}

void init_direct(list_t *arglist, dstr_t *filelist, jobparams_t *job)
{
    char tmp [1024];
    listitem_t *i;
    char user_default_path [PATH_MAX];

    strlcpy(user_default_path, getenv("HOME"), 256);
    strlcat(user_default_path, "/.foomatic/", 256);

    /* Which files do we want to print? */
    for (i = arglist->first; i; i = i->next) {
        strncpy_omit(tmp, (char*)i->data, 1024, omit_shellescapes);
        dstrcatf(filelist, "%s ", tmp);
    }

    if (job->ppdfile[0] == '\0') {
        if (job->printer[0] == '\0') {
            /* No printer definition file selected, check whether we have a
               default printer defined */
            find_default_printer(user_default_path, job);
        }

        /* Neither in a config file nor on the command line a printer was selected */
        if (!job->printer[0]) {
            _log("No printer definition (option \"-P <name>\") specified!\n");
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }

        /* Search for the PPD file */
        if (!find_ppdfile(user_default_path, job)) {
            _log("There is no readable PPD file for the printer %s, is it configured?\n", job->printer);
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }
    }
}


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

void init_cups(list_t *arglist, dstr_t *filelist, jobparams_t *job)
{
    char path [PATH_MAX] = "";
    char cups_jobid [128];
    char cups_user [128];
    char cups_jobtitle [2048];
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
    strncpy_omit(cups_jobtitle, arglist_get(arglist, 2), 2048,
		 omit_shellescapes);
    strncpy_omit(cups_copies, arglist_get(arglist, 3), 128, omit_shellescapes);

    cups_options_len = strlen(arglist_get(arglist, 4));
    cups_options = malloc(cups_options_len + 1);
    strncpy_omit(cups_options, arglist_get(arglist, 4), cups_options_len + 1,
		 omit_shellescapes);

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

    /* On which queue are we printing?
       CUPS puts the print queue name into the PRINTER environment variable
       when calling filters. */
    strncpy(job->printer, getenv("PRINTER"), 256);

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

    snprintf(job->ppdfile, 2048, "%s.ppd", job->printer); /* current dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 2048, "%s/%s.ppd", user_default_path, job->printer); /* user dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 2048, "%s/direct/%s.ppd", CONFIG_PATH, job->printer); /* system dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 2048, "%s/%s.ppd", CONFIG_PATH, job->printer); /* system dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 2048, "/etc/cups/ppd/%s.ppd", job->printer); /* CUPS config dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 2048, "/usr/local/etc/cups/ppd/%s.ppd", job->printer); /* CUPS config dir */
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


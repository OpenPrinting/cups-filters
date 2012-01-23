/*
 * Copyright 2011 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "banner.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <libgen.h>


static int parse_line(char *line, char **key, char **value)
{
    char *p = line;

    *key = *value = NULL;

    while (isspace(*p)) p++;
    if (!*p || *p == '#')
        return 0;

    *key = p;
    while (*p && !isspace(*p))
        p++;
    if (!*p)
        return 1;

    *p++ = '\0';

    while (isspace(*p)) p++;
    if (!*p)
        return 1;

    *value = p;

    /* remove trailing space */
    while (*p)
        p++;
    while (isspace(*--p))
        *p = '\0';

    return 1;
}


static unsigned parse_show(char *s)
{
    unsigned info = 0;
    char *tok;

    for (tok = strtok(s, " \t"); tok; tok = strtok(NULL, " \t")) {
        if (!strcasecmp(tok, "imageable-area"))
             info |= INFO_IMAGEABLE_AREA;
        else if (!strcasecmp(tok, "job-billing"))
            info |= INFO_JOB_BILLING;
        else if (!strcasecmp(tok, "job-id"))
            info |= INFO_JOB_ID;
        else if (!strcasecmp(tok, "job-name"))
            info |= INFO_JOB_NAME;
        else if (!strcasecmp(tok, "job-originating-host-name"))
            info |= INFO_JOB_ORIGINATING_HOST_NAME;
        else if (!strcasecmp(tok, "job-originating-user-name"))
            info |= INFO_JOB_ORIGINATING_USER_NAME;
        else if (!strcasecmp(tok, "job-uuid"))
            info |= INFO_JOB_UUID;
        else if (!strcasecmp(tok, "options"))
            info |= INFO_OPTIONS;
        else if (!strcasecmp(tok, "paper-name"))
            info |= INFO_PAPER_NAME;
        else if (!strcasecmp(tok, "paper-size"))
            info |= INFO_PAPER_SIZE;
        else if (!strcasecmp(tok, "printer-driver-name"))
            info |= INFO_PRINTER_DRIVER_NAME;
        else if (!strcasecmp(tok, "printer-driver-version"))
            info |= INFO_PRINTER_DRIVER_VERSION;
        else if (!strcasecmp(tok, "printer-info"))
            info |= INFO_PRINTER_INFO;
        else if (!strcasecmp(tok, "printer-location"))
            info |= INFO_PRINTER_LOCATION;
        else if (!strcasecmp(tok, "printer-make-and-model"))
            info |= INFO_PRINTER_MAKE_AND_MODEL;
        else if (!strcasecmp(tok, "printer-name"))
            info |= INFO_PRINTER_NAME;
        else if (!strcasecmp(tok, "time-at-creation"))
            info |= INFO_TIME_AT_CREATION;
        else if (!strcasecmp(tok, "time-at-processing"))
            info |= INFO_TIME_AT_PROCESSING;
        else
            fprintf(stderr, "error: unknown value for 'Show': %s\n", tok);
    }
    return info;
}


static char * abs_template_path(const char *path,
                                const char *banner_file)
{
    char *result, *wd;
    size_t len;

    if (path[0] == '/')
        return strdup(path);

    wd = dirname(strdup(banner_file));
    result = malloc(strlen(wd) + strlen(path) + 2);
    sprintf(result, "%s/%s", wd, path);
    free(wd);
    return result;
}


banner_t * banner_new_from_file(const char *filename)
{
    FILE *f;
    char *line = NULL;
    ssize_t len = 0;
    int linenr = 0;
    banner_t *banner = calloc(1, sizeof *banner);

    if (!strcmp(filename, "-"))
        f = stdin;
    else if (!(f = fopen(filename, "r"))) {
        perror("Error opening banner file");
        return banner;
    }

    while (getline(&line, &len, f) != -1) {
        char *key, *value;

        linenr++;
        if (!parse_line(line, &key, &value))
            continue;

        if (!value) {
            fprintf(stderr, "error: line %d is missing a value\n", linenr);
            continue;
        }

        if (!strcasecmp(key, "template"))
            banner->template_file = abs_template_path(value, filename);
        else if (!strcasecmp(key, "header"))
            banner->header = strdup(value);
        else if (!strcasecmp(key, "footer"))
            banner->header = strdup(value);
        else if (!strcasecmp(key, "show"))
            banner->infos = parse_show(value);
        else if (!strcasecmp(key, "image") ||
                 !strcasecmp(key, "notice"))
            fprintf(stderr,
                    "note:%d: bannertopdf does not support '%s'\n",
                    linenr, key);
        else
            fprintf(stderr,
                    "error:%d: unknown keyword '%s'\n",
                    linenr, key);
    }

    free(line);
    fclose(f);
    return banner;
}


void banner_free(banner_t *banner)
{
    if (banner) {
        free(banner->template_file);
        free(banner->header);
        free(banner->footer);
        free(banner);
    }
}


/*
 * Copyright 2012 Canonical Ltd.
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

#ifndef banner_h
#define banner_h

#include <config.h>
#include <stdio.h>
#include <cups/cups.h>

enum banner_info {
    INFO_IMAGEABLE_AREA            = 1,
    INFO_JOB_BILLING               = 1 << 1,
    INFO_JOB_ID                    = 1 << 2,
    INFO_JOB_NAME                  = 1 << 3,
    INFO_JOB_ORIGINATING_HOST_NAME = 1 << 4,
    INFO_JOB_ORIGINATING_USER_NAME = 1 << 5,
    INFO_JOB_UUID                  = 1 << 6,
    INFO_OPTIONS                   = 1 << 7,
    INFO_PAPER_NAME                = 1 << 8,
    INFO_PAPER_SIZE                = 1 << 9,
    INFO_PRINTER_DRIVER_NAME       = 1 << 10,
    INFO_PRINTER_DRIVER_VERSION    = 1 << 11,
    INFO_PRINTER_INFO              = 1 << 12,
    INFO_PRINTER_LOCATION          = 1 << 13,
    INFO_PRINTER_MAKE_AND_MODEL    = 1 << 14,
    INFO_PRINTER_NAME              = 1 << 15,
    INFO_TIME_AT_CREATION          = 1 << 16,
    INFO_TIME_AT_PROCESSING        = 1 << 17
};


typedef struct {
    char *template_file;
    char *header, *footer;
    unsigned infos;
} banner_t;


banner_t * banner_new_from_file(const char *filename,
        int *num_options, cups_option_t **options);
void banner_free(banner_t *banner);

#endif


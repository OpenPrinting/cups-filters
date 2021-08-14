/*
 * Copyright 2012 Canonical Ltd.
 * Copyright 2013 ALT Linux, Andrew V. Stepanov <stanv@altlinux.com>
 * Copyright 2018 Sahil Arora <sahilarora.535@gmail.com>
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

#include <config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#ifndef HAVE_OPEN_MEMSTREAM
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#endif

#include <cups/cups.h>
#include <ppd/ppd.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif
#ifdef HAVE_CUPS_1_7
#include <cups/pwg.h>
#endif /* HAVE_CUPS_1_7 */

#include "filter.h"
#include <cupsfilters/pdf.h>

enum banner_info
{
    INFO_IMAGEABLE_AREA = 1,
    INFO_JOB_BILLING = 1 << 1,
    INFO_JOB_ID = 1 << 2,
    INFO_JOB_NAME = 1 << 3,
    INFO_JOB_ORIGINATING_HOST_NAME = 1 << 4,
    INFO_JOB_ORIGINATING_USER_NAME = 1 << 5,
    INFO_JOB_UUID = 1 << 6,
    INFO_OPTIONS = 1 << 7,
    INFO_PAPER_NAME = 1 << 8,
    INFO_PAPER_SIZE = 1 << 9,
    INFO_PRINTER_DRIVER_NAME = 1 << 10,
    INFO_PRINTER_DRIVER_VERSION = 1 << 11,
    INFO_PRINTER_INFO = 1 << 12,
    INFO_PRINTER_LOCATION = 1 << 13,
    INFO_PRINTER_MAKE_AND_MODEL = 1 << 14,
    INFO_PRINTER_NAME = 1 << 15,
    INFO_TIME_AT_CREATION = 1 << 16,
    INFO_TIME_AT_PROCESSING = 1 << 17
};

typedef struct bannertopdf_doc_s
{

} bannertopdf_doc_t;

typedef struct
{
    char *template_file;
    char *header, *footer;
    unsigned infos;
} banner_t;

void banner_free(banner_t *banner)
{
    if (banner)
    {
        free(banner->template_file);
        free(banner->header);
        free(banner->footer);
        free(banner);
    }
}

static int parse_line(char *line, char **key, char **value)
{
    char *p = line;

    *key = *value = NULL;

    while (isspace(*p))
        p++;
    if (!*p || *p == '#')
        return 0;

    *key = p;
    while (*p && !isspace(*p))
        p++;
    if (!*p)
        return 1;

    *p++ = '\0';

    while (isspace(*p))
        p++;
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
static unsigned parse_show(char *s, filter_logfunc_t log, void *ld)
{
    unsigned info = 0;
    char *tok;

    for (tok = strtok(s, " \t"); tok; tok = strtok(NULL, " \t"))
    {
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
        else if (log)
            log(ld, FILTER_LOGLEVEL_ERROR, "bannertopdf: error: unknown value for 'Show': %s\n", tok);
    }
    return info;
}

static char *template_path(const char *name)
{
    char *datadir, *result;

    if (name[0] == '/')
        return strdup(name);

    if ((datadir = getenv("CUPS_DATADIR")) == NULL)
    {
        result = malloc(strlen(BANNERTOPDF_DATADIR) + strlen(name) + 2);
        sprintf(result, "%s/%s", BANNERTOPDF_DATADIR, name);
    }
    else
    {
        result = malloc(strlen(datadir) + strlen(name) + 7);
        sprintf(result, "%s/data/%s", datadir, name);
    }

    return result;
}
banner_t *banner_new_from_file_descriptor(int inputfd,
                                          int *num_options, cups_option_t **options, filter_logfunc_t log, void *ld)
{
    FILE *f;
    char *line = NULL;
    size_t len = 0;
    int linenr = 0;
    banner_t *banner = NULL;

    if (!(f = fdopen(inputfd, "r")))
    {
        perror("Error opening banner file");
        goto out;
    }

    if (getline(&line, &len, f) == -1 ||
        strncmp(line, "#PDF-BANNER", 11) != 0)
        goto out;

    banner = calloc(1, sizeof *banner);

    while (getline(&line, &len, f) != -1)
    {
        char *key, *value;

        linenr++;
        if (!parse_line(line, &key, &value))
            continue;

        if (!value)
        {
            if (log)
                log(ld, FILTER_LOGLEVEL_ERROR, "line %d is missing a value", linenr);
            continue;
        }

        if (!strcasecmp(key, "template"))
            banner->template_file = template_path(value);
        else if (!strcasecmp(key, "header"))
            banner->header = strdup(value);
        else if (!strcasecmp(key, "footer"))
            banner->header = strdup(value);
        else if (!strcasecmp(key, "font"))
        {
            *num_options = cupsAddOption("banner-font",
                                         strdup(value), *num_options, options);
        }
        else if (!strcasecmp(key, "font-size"))
        {
            *num_options = cupsAddOption("banner-font-size",
                                         strdup(value), *num_options, options);
        }
        else if (!strcasecmp(key, "show"))
            banner->infos = parse_show(value, log, ld);
        else if (!strcasecmp(key, "image") ||
                 !strcasecmp(key, "notice"))
        {
            if (log)
                log(ld, FILTER_LOGLEVEL_ERROR,
                    "bannertopdf: note:%d: bannertopdf does not support '%s'",
                    linenr, key);
        }
        else
        {
            if (log)
                log(ld, FILTER_LOGLEVEL_ERROR,
                    "error:%d: unknown keyword '%s'",
                    linenr, key);
        }
    }

    /* load default template if none was specified */
    if (!banner->template_file)
        banner->template_file = template_path("default.pdf");

out:
    free(line);
    if (f)
        fclose(f);
    return banner;
}

static float get_float_option(const char *name,
                              int noptions,
                              cups_option_t *options,
                              float def)
{
    const char *value = cupsGetOption(name, noptions, options);
    return value ? atof(value) : def;
}

static int get_int_option(const char *name,
                          int noptions,
                          cups_option_t *options,
                          int def)
{
    const char *value = cupsGetOption(name, noptions, options);
    return value ? atoi(value) : def;
}

static void get_pagesize(ppd_file_t *ppd,
                         int noptions,
                         cups_option_t *options,
                         float *width,
                         float *length,
                         float media_limits[4])
{
    static const ppd_size_t defaultsize = {
        0,     /* marked */
        "",    /* name */
        612.0, /* width */
        792.0, /* length */
        18.0,  /* left */
        36.0,  /* bottom */
        594.0, /* right */
        756.0, /* top */
    };
    const ppd_size_t *pagesize;
#ifdef HAVE_CUPS_1_7
    pwg_media_t *size_found; /* page size found for given name */
    const char *val;         /* Pointer into value */
    char *ptr1, *ptr2,       /* Pointer into string */
        s[255];              /* Temporary string */
#endif                       /* HAVE_CUPS_1_7 */

    if (!ppd || !(pagesize = ppdPageSize(ppd, NULL)))
        pagesize = &defaultsize;

    *width = pagesize->width;
    *length = pagesize->length;

    media_limits[0] = get_float_option("page-left",
                                       noptions, options,
                                       pagesize->left);
    media_limits[1] = get_float_option("page-bottom",
                                       noptions, options,
                                       pagesize->bottom);
    media_limits[2] = get_float_option("page-right",
                                       noptions, options,
                                       fabs(pagesize->right));
    media_limits[3] = get_float_option("page-top",
                                       noptions, options,
                                       fabs(pagesize->top));

#ifdef HAVE_CUPS_1_7
    if (!ppd)
    {
        if ((val = cupsGetOption("media-size", noptions, options)) != NULL ||
            (val = cupsGetOption("MediaSize", noptions, options)) != NULL ||
            (val = cupsGetOption("page-size", noptions, options)) != NULL ||
            (val = cupsGetOption("PageSize", noptions, options)) != NULL ||
            (val = cupsGetOption("media", noptions, options)) != NULL)
        {
            for (ptr1 = (char *)val; *ptr1;)
            {
                for (ptr2 = s; *ptr1 && *ptr1 != ',' && (ptr2 - s) < (sizeof(s) - 1);)
                    *ptr2++ = *ptr1++;
                *ptr2++ = '\0';
                if (*ptr1 == ',')
                    ptr1++;
                size_found = NULL;
                if ((size_found = pwgMediaForPWG(s)) == NULL)
                    if ((size_found = pwgMediaForPPD(s)) == NULL)
                        size_found = pwgMediaForLegacy(s);
                if (size_found != NULL)
                {
                    *width = size_found->width * 72.0 / 2540.0;
                    *length = size_found->length * 72.0 / 2540.0;
                    media_limits[2] += (*width - 612.0);
                    media_limits[3] += (*length - 792.0);
                }
            }
        }
        if ((val = cupsGetOption("media-left-margin", noptions, options)) != NULL)
            media_limits[0] = atol(val) * 72.0 / 2540.0;
        if ((val = cupsGetOption("media-bottom-margin", noptions, options)) != NULL)
            media_limits[1] = atol(val) * 72.0 / 2540.0;
        if ((val = cupsGetOption("media-right-margin", noptions, options)) != NULL)
            media_limits[2] = *width - atol(val) * 72.0 / 2540.0;
        if ((val = cupsGetOption("media-top-margin", noptions, options)) != NULL)
            media_limits[3] = *length - atol(val) * 72.0 / 2540.0;
    }
#endif /* HAVE_CUPS_1_7 */
}

static int duplex_marked(ppd_file_t *ppd,
                         int noptions,
                         cups_option_t *options)
{
    const char *val; /* Pointer into value */
    return (ppd &&
            (ppdIsMarked(ppd, "Duplex", "DuplexNoTumble") ||
             ppdIsMarked(ppd, "Duplex", "DuplexTumble") ||
             ppdIsMarked(ppd, "JCLDuplex", "DuplexNoTumble") ||
             ppdIsMarked(ppd, "JCLDuplex", "DuplexTumble") ||
             ppdIsMarked(ppd, "EFDuplex", "DuplexNoTumble") ||
             ppdIsMarked(ppd, "EFDuplex", "DuplexTumble") ||
             ppdIsMarked(ppd, "EFDuplexing", "DuplexNoTumble") ||
             ppdIsMarked(ppd, "EFDuplexing", "DuplexTumble") ||
             ppdIsMarked(ppd, "ARDuplex", "DuplexNoTumble") ||
             ppdIsMarked(ppd, "ARDuplex", "DuplexTumble") ||
             ppdIsMarked(ppd, "KD03Duplex", "DuplexNoTumble") ||
             ppdIsMarked(ppd, "KD03Duplex", "DuplexTumble"))) ||
           ((val = cupsGetOption("Duplex", noptions, options)) != NULL &&
            (!strcasecmp(val, "DuplexNoTumble") ||
             !strcasecmp(val, "DuplexTumble"))) ||
           ((val = cupsGetOption("sides", noptions, options)) != NULL &&
            (!strcasecmp(val, "two-sided-long-edge") ||
             !strcasecmp(val, "two-sided-short-edge")));
}

static void info_linef(FILE *s,
                       const char *key,
                       const char *valuefmt, ...)
{
    va_list ap;

    va_start(ap, valuefmt);
    fprintf(s, "(%s: ", key);
    vfprintf(s, valuefmt, ap);
    fprintf(s, ") Tj T*\n");
    va_end(ap);
}

static void info_line(FILE *s,
                      const char *key,
                      const char *value)
{
    info_linef(s, key, "%s", value);
}

static void info_line_time(FILE *s,
                           const char *key,
                           const char *timestamp)
{
    char buf[40];
    time_t time;

    if (timestamp)
    {
        time = (time_t)atoll(timestamp);
        strftime(buf, sizeof buf, "%c", localtime(&time));
        info_line(s, key, buf);
    }
    else
        info_line(s, key, "unknown");
}

static const char *human_time(const char *timestamp)
{
    time_t time;
    int size = sizeof(char) * 40;
    char *buf = malloc(size);
    strcpy(buf, "unknown");

    if (timestamp)
    {
        time = (time_t)atoll(timestamp);
        strftime(buf, size, "%c", localtime(&time));
    }

    return buf;
}

/*
 * Add new key & value.
 */
static opt_t *add_opt(opt_t *in_opt, const char *key, const char *val)
{
    if (!key || !val)
    {
        return in_opt;
    }

    if (!strlen(key) || !strlen(val))
    {
        return in_opt;
    }

    opt_t *entry = malloc(sizeof(opt_t));
    if (!entry)
    {
        return in_opt;
    }

    entry->key = key;
    entry->val = val;
    entry->next = in_opt;

    return entry;
}

/*
 * Collect all known info about current task.
 * Bond PDF form field name with collected info.
 *
 * Create PDF form's field names according above.
 */
opt_t *get_known_opts(
    ppd_file_t *ppd,
    const char *jobid,
    const char *user,
    const char *jobtitle,
    int noptions,
    cups_option_t *options)
{

    ppd_attr_t *attr;
    opt_t *opt = NULL;

    /* Job ID */
    opt = add_opt(opt, "job-id", jobid);

    /* Job title */
    opt = add_opt(opt, "job-title", jobtitle);

    /* Printer by */
    opt = add_opt(opt, "user", user);

    /* Printer name */
    opt = add_opt(opt, "printer-name", getenv("PRINTER"));

    /* Printer info */
    opt = add_opt(opt, "printer-info", getenv("PRINTER_INFO"));

    /* Time at creation */
    opt = add_opt(opt, "time-at-creation",
                  human_time(cupsGetOption("time-at-creation", noptions, options)));

    /* Processing time */
    opt = add_opt(opt, "time-at-processing",
                  human_time(cupsGetOption("time-at-processing", noptions, options)));

    /* Billing information */
    opt = add_opt(opt, "job-billing",
                  cupsGetOption("job-billing", noptions, options));

    /* Source hostname */
    opt = add_opt(opt, "job-originating-host-name",
                  cupsGetOption("job-originating-host-name", noptions, options));

    /* Banner font */
    opt = add_opt(opt, "banner-font",
                  cupsGetOption("banner-font", noptions, options));

    /* Banner font size */
    opt = add_opt(opt, "banner-font-size",
                  cupsGetOption("banner-font-size", noptions, options));

    /* Job UUID */
    opt = add_opt(opt, "job-uuid",
                  cupsGetOption("job-uuid", noptions, options));

    /* Security context */
    opt = add_opt(opt, "security-context",
                  cupsGetOption("security-context", noptions, options));

    /* Security context range part */
    opt = add_opt(opt, "security-context-range",
                  cupsGetOption("security-context-range", noptions, options));

    /* Security context current range part */
    const char *full_range = cupsGetOption("security-context-range", noptions, options);
    if (full_range)
    {
        size_t cur_size = strcspn(full_range, "-");
        char *cur_range = strndup(full_range, cur_size);
        opt = add_opt(opt, "security-context-range-cur", cur_range);
    }

    /* Security context type part */
    opt = add_opt(opt, "security-context-type",
                  cupsGetOption("security-context-type", noptions, options));

    /* Security context role part */
    opt = add_opt(opt, "security-context-role",
                  cupsGetOption("security-context-role", noptions, options));

    /* Security context user part */
    opt = add_opt(opt, "security-context-user",
                  cupsGetOption("security-context-user", noptions, options));

    if (ppd)
    {
        /* Driver */
        opt = add_opt(opt, "driver", ppd->pcfilename);

        /* Driver version */
        opt = add_opt(opt, "driver-version",
                      (attr = ppdFindAttr(ppd, "FileVersion", NULL)) ? attr->value : "");

        /* Make and model */
        opt = add_opt(opt, "make-and-model", ppd->nickname);
    }

    return opt;
}

static int generate_banner_pdf(banner_t *banner,
                               ppd_file_t *ppd,
                               const char *jobid,
                               const char *user,
                               const char *jobtitle,
                               int noptions,
                               cups_option_t *options,
                               filter_logfunc_t log,
                               void *ld,
                               FILE *outputfp)
{
    char *buf;
    size_t len;
    FILE *s;
    pdf_t *doc;
    float page_width, page_length;
    float media_limits[4];
    float page_scale;
    ppd_attr_t *attr;
    unsigned copies;
#ifndef HAVE_OPEN_MEMSTREAM
    struct stat st;
#endif

    if (!(doc = pdf_load_template(banner->template_file)))
        return 1;

    get_pagesize(ppd, noptions, options,
                 &page_width, &page_length, media_limits);

    pdf_resize_page(doc, 1, page_width, page_length, &page_scale);

    pdf_add_type1_font(doc, 1, "Courier");

#ifdef HAVE_OPEN_MEMSTREAM
    s = open_memstream(&buf, &len);
#else
    if ((s = tmpfile()) == NULL)
    {
        if (log)
            log(ld, FILTER_LOGLEVEL_ERROR, "bannertopdf: cannot create temp file: %s\n", strerror(errno));
        return 1;
    }
#endif

    if (banner->infos & INFO_IMAGEABLE_AREA)
    {
        fprintf(s, "q\n");
        fprintf(s, "0 0 0 RG\n");
        fprintf(s, "%f %f %f %f re S\n", media_limits[0] + 1.0,
                media_limits[1] + 1.0,
                media_limits[2] - media_limits[0] - 2.0,
                media_limits[3] - media_limits[1] - 2.0);
        fprintf(s, "Q\n");
    }

    fprintf(s, "%f 0 0 %f 0 0 cm\n", page_scale, page_scale);

    fprintf(s, "0 0 0 rg\n");
    fprintf(s, "BT\n");
    fprintf(s, "/bannertopdf-font 14 Tf\n");
    fprintf(s, "83.662 335.0 Td\n");
    fprintf(s, "17 TL\n");

    if (banner->infos & INFO_IMAGEABLE_AREA)
        info_linef(s, "Media Limits", "%.2f x %.2f to %.2f x %.2f inches",
                   media_limits[0] / 72.0,
                   media_limits[1] / 72.0,
                   media_limits[2] / 72.0,
                   media_limits[3] / 72.0);

    if (banner->infos & INFO_JOB_BILLING)
        info_line(s, "Billing Information\n",
                  cupsGetOption("job-billing", noptions, options));

    if (banner->infos & INFO_JOB_ID)
        info_linef(s, "Job ID", "%s-%s", getenv("PRINTER"), jobid);

    if (banner->infos & INFO_JOB_NAME)
        info_line(s, "Job Title", jobtitle);

    if (banner->infos & INFO_JOB_ORIGINATING_HOST_NAME)
        info_line(s, "Printed from",
                  cupsGetOption("job-originating-host-name",
                                noptions, options));

    if (banner->infos & INFO_JOB_ORIGINATING_USER_NAME)
        info_line(s, "Printed by", user);

    if (banner->infos & INFO_JOB_UUID)
        info_line(s, "Job UUID",
                  cupsGetOption("job-uuid", noptions, options));

    if (ppd && banner->infos & INFO_PRINTER_DRIVER_NAME)
        info_line(s, "Driver", ppd->pcfilename);

    if (ppd && banner->infos & INFO_PRINTER_DRIVER_VERSION)
        info_line(s, "Driver Version",
                  (attr = ppdFindAttr(ppd, "FileVersion", NULL)) ? attr->value : "");

    if (banner->infos & INFO_PRINTER_INFO)
        info_line(s, "Description", getenv("PRINTER_INFO"));

    if (banner->infos & INFO_PRINTER_LOCATION)
        info_line(s, "Printer Location", getenv("PRINTER_LOCATION"));

    if (ppd && banner->infos & INFO_PRINTER_MAKE_AND_MODEL)
        info_line(s, "Make and Model", ppd->nickname);

    if (banner->infos & INFO_PRINTER_NAME)
        info_line(s, "Printer", getenv("PRINTER"));

    if (banner->infos & INFO_TIME_AT_CREATION)
        info_line_time(s, "Created at",
                       cupsGetOption("time-at-creation", noptions, options));

    if (banner->infos & INFO_TIME_AT_PROCESSING)
        info_line_time(s, "Printed at",
                       cupsGetOption("time-at-processing", noptions, options));

    fprintf(s, "ET\n");
#ifndef HAVE_OPEN_MEMSTREAM
    fflush(s);
    if (fstat(fileno(s), &st) < 0)
    {
        if (log)
            log(ld, FILTER_LOGLEVEL_ERROR, "bannertopdf: cannot fstat(): %s\n", , strerror(errno));
        return 1;
    }
    fseek(s, 0L, SEEK_SET);
    if ((buf = malloc(st.st_size + 1)) == NULL)
    {
        if (log)
            log(ld, FILTER_LOGLEVEL_ERROR, "bannertopdf: cannot malloc(): %s\n", , strerror(errno));
        return 1;
    }
    size_t nbytes = fread(buf, 1, st.st_size, s);
    buf[st.st_size] = '\0';
    len = strlen(buf);
#endif /* !HAVE_OPEN_MEMSTREAM */
    fclose(s);

    opt_t *known_opts = get_known_opts(ppd,
                                       jobid,
                                       user,
                                       jobtitle,
                                       noptions,
                                       options);

    /*
     * Try to find a PDF form in PDF template and fill it.
     */
    int ret = pdf_fill_form(doc, known_opts);

    /*
     * Could we fill a PDF form? If no, just add PDF stream.
     */
    if (!ret)
    {
        pdf_prepend_stream(doc, 1, buf, len);
    }

    copies = get_int_option("number-up", noptions, options, 1);

    if (duplex_marked(ppd, noptions, options))
        copies *= 2;

    if (copies > 1)
        pdf_duplicate_page(doc, 1, copies - 1);

    pdf_write(doc, outputfp);

    opt_t *opt_current = known_opts;
    opt_t *opt_next = NULL;
    while (opt_current != NULL)
    {
        opt_next = opt_current->next;
        free(opt_current);
        opt_current = opt_next;
    }

    free(buf);
    pdf_free(doc);
    return 0;
}

int bannertopdf(int inputfd,         /* I - File descriptor input stream */
                int outputfd,        /* I - File descriptor output stream */
                int inputseekable,   /* I - Is input stream seekable? (unused) */
                filter_data_t *data, /* I - Job and printer data */
                void *parameters)    /* I - Filter-specific parameters (unused) */
{
    banner_t *banner;
    int noptions;
    ppd_file_t *ppd = NULL;
    int ret;
    cups_file_t *inputfp;
    FILE *outputfp;
    cups_option_t *options = NULL;

    filter_logfunc_t log = data->logfunc;
    void *ld = data->logdata;
    filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
    void *icd = data->iscanceleddata;
    char jobid[50];

    options = (cups_option_t *)malloc(sizeof(cups_option_t));

    options->name = (char*)malloc(sizeof(char) * 1000);
    options->value = (char*)malloc(sizeof(char) * 1000);

    memcpy(options->name, data->options->name, strlen(data->options->name));
    memcpy(options->value, data->options->value, strlen(data->options->value));

    if ((inputfp = cupsFileOpenFd(inputfd, "rb")) == NULL)
    {
        if (!iscanceled || !iscanceled(icd))
        {
            if (log)
                log(ld, FILTER_LOGLEVEL_DEBUG,
                    "bannertopdf: Unable to open input data stream.");
        }
        return (1);
    }
    /*
  * Open the output data stream specified by the outputfd...
  */

    if ((outputfp = fdopen(outputfd, "w")) == NULL)
    {
        if (!iscanceled || !iscanceled(icd))
        {
            if (log)
                log(ld, FILTER_LOGLEVEL_DEBUG,
                    "bannertopdf: Unable to open output data stream.");
        }

        cupsFileClose(inputfp);
        return (1);
    }

    if (data->ppd)
        ppd = data->ppd;
    else if (data->ppdfile)
        ppd = ppdOpenFile(data->ppdfile);

    noptions = data->num_options;

    if (!ppd)
    {
        if (log)
            log(ld, FILTER_LOGLEVEL_DEBUG, "bannertopdf: Could not open PPD file '%s'", ppd);
    }
   
    banner = banner_new_from_file_descriptor(inputfd, &noptions, &options, log, ld);

    if (!banner)
    {
        if (log)
            log(ld, FILTER_LOGLEVEL_ERROR, "bannertopdf: Could not read banner file");
        return 1;
    }

    sprintf(jobid, "%d", data->job_id);

    ret = generate_banner_pdf(banner,
                              ppd,
                              jobid,
                              data->job_user,
                              data->job_title,
                              noptions,
                              options,
                              log,
                              ld,
                              outputfp);
    banner_free(banner);

    if(options) free(options);
    
    return ret;
}

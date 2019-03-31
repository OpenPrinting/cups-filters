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
#include <cups/ppd.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif
#ifdef HAVE_CUPS_1_7
#include <cups/pwg.h>
#endif /* HAVE_CUPS_1_7 */

#include "banner.h"
#include "pdf.h"


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
        0,          /* marked */
        "",         /* name */
        612.0,      /* width */
        792.0,      /* length */
        18.0,       /* left */
        36.0,       /* bottom */
        594.0,      /* right */
        756.0,      /* top */
    };
    const ppd_size_t *pagesize;
#ifdef HAVE_CUPS_1_7
    pwg_media_t      *size_found;          /* page size found for given name */
    const char       *val;                 /* Pointer into value */
    char             *ptr1, *ptr2,         /* Pointer into string */
                     s[255];               /* Temporary string */
#endif /* HAVE_CUPS_1_7 */

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
    if (!ppd) {
      if ((val = cupsGetOption("media-size", noptions, options)) != NULL ||
	  (val = cupsGetOption("MediaSize", noptions, options)) != NULL ||
	  (val = cupsGetOption("page-size", noptions, options)) != NULL ||
	  (val = cupsGetOption("PageSize", noptions, options)) != NULL ||
	  (val = cupsGetOption("media", noptions, options)) != NULL) {
	for (ptr1 = (char *)val; *ptr1;) {
	  for (ptr2 = s; *ptr1 && *ptr1 != ',' && (ptr2 - s) < (sizeof(s) - 1);)
	    *ptr2++ = *ptr1++;
	  *ptr2++ = '\0';
	  if (*ptr1 == ',')
	    ptr1 ++;
	  size_found = NULL;
	  if ((size_found = pwgMediaForPWG(s)) == NULL)
	    if ((size_found = pwgMediaForPPD(s)) == NULL)
	      size_found = pwgMediaForLegacy(s);
	  if (size_found != NULL) {
	    *width = size_found->width * 72.0 / 2540.0;
	    *length = size_found->length * 72.0 / 2540.0;
	    media_limits[2] += (*width - 612.0);
	    media_limits[3] += (*length - 792.0);
	  }
	}
      }
      if ((val = cupsGetOption("media-left-margin", noptions, options))
	  != NULL)
	media_limits[0] = atol(val) * 72.0 / 2540.0; 
      if ((val = cupsGetOption("media-bottom-margin", noptions, options))
	  != NULL)
	media_limits[1] = atol(val) * 72.0 / 2540.0; 
      if ((val = cupsGetOption("media-right-margin", noptions, options))
	  != NULL)
	media_limits[2] = *width - atol(val) * 72.0 / 2540.0; 
      if ((val = cupsGetOption("media-top-margin", noptions, options))
	  != NULL)
	media_limits[3] = *length - atol(val) * 72.0 / 2540.0; 
    }
#endif /* HAVE_CUPS_1_7 */
}


static int duplex_marked(ppd_file_t *ppd,
                         int noptions,
                         cups_option_t *options)
{
    const char       *val;                 /* Pointer into value */
    return
      (ppd &&
       (ppdIsMarked(ppd, "Duplex", "DuplexNoTumble") ||
        ppdIsMarked(ppd, "Duplex", "DuplexTumble") ||
        ppdIsMarked(ppd, "JCLDuplex", "DuplexNoTumble") ||
        ppdIsMarked(ppd, "JCLDuplex", "DuplexTumble") ||
        ppdIsMarked(ppd, "EFDuplex", "DuplexNoTumble") ||
        ppdIsMarked(ppd, "EFDuplex", "DuplexTumble") ||
        ppdIsMarked(ppd, "KD03Duplex", "DuplexNoTumble") ||
        ppdIsMarked(ppd, "KD03Duplex", "DuplexTumble"))) ||
      ((val = cupsGetOption("Duplex", noptions, options))
       != NULL &&
       (!strcasecmp(val, "DuplexNoTumble") ||
	!strcasecmp(val, "DuplexTumble"))) ||
      ((val = cupsGetOption("sides", noptions, options))
       != NULL &&
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

    if (timestamp) {
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

    if (timestamp) {
        time = (time_t)atoll(timestamp);
        strftime(buf, size, "%c", localtime(&time));
    }

    return buf;
}

/*
 * Add new key & value.
 */
static opt_t* add_opt(opt_t *in_opt, const char *key, const char *val) {
    if ( ! key || ! val ) {
        return in_opt;
    }

    if ( !strlen(key) || !strlen(val) ) {
        return in_opt;
    }

    opt_t *entry = malloc(sizeof(opt_t));
    if ( ! entry ) {
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
        cups_option_t *options) {

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
    const char * full_range = cupsGetOption("security-context-range", noptions, options);
    if ( full_range ) {
        size_t cur_size = strcspn(full_range, "-");
        char * cur_range = strndup(full_range, cur_size);
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

    if (ppd) {
      /* Driver */
      opt = add_opt(opt, "driver", ppd->pcfilename);

      /* Driver version */
      opt = add_opt(opt, "driver-version", 
		    (attr = ppdFindAttr(ppd, "FileVersion", NULL)) ? 
		    attr->value : "");

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
                               cups_option_t *options)
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

    pdf_resize_page (doc, 1, page_width, page_length, &page_scale);

    pdf_add_type1_font(doc, 1, "Courier");

#ifdef HAVE_OPEN_MEMSTREAM
    s = open_memstream(&buf, &len);
#else
    if ((s = tmpfile()) == NULL) {
        fprintf (stderr, "ERROR: bannertopdf: cannot create temp file: %s\n",
                 strerror (errno));
        return 1;
    }
#endif

    if (banner->infos & INFO_IMAGEABLE_AREA) {
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
    fflush (s);
    if (fstat (fileno (s), &st) < 0) {
        fprintf (stderr, "ERROR: bannertopdf: cannot fstat(): %s\n", strerror(errno));
        return 1 ;
    }
    fseek (s, 0L, SEEK_SET);
    if ((buf = malloc(st.st_size + 1)) == NULL) {
        fprintf (stderr, "ERROR: bannertopdf: cannot malloc(): %s\n", strerror(errno));
        return 1 ;
    }
    size_t nbytes = fread (buf, 1, st.st_size, s);
    buf[st.st_size] = '\0';
    len = strlen (buf);
#endif /* !HAVE_OPEN_MEMSTREAM */
    fclose(s);

    opt_t * known_opts = get_known_opts(ppd,
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
    if ( ! ret ) {
        pdf_prepend_stream(doc, 1, buf, len);
    }

    copies = get_int_option("number-up", noptions, options, 1);
    if (duplex_marked(ppd, noptions, options))
        copies *= 2;

    if (copies > 1)
        pdf_duplicate_page(doc, 1, copies - 1);

    pdf_write(doc, stdout);

    opt_t * opt_current = known_opts;
    opt_t * opt_next = NULL;
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


int main(int argc, char *argv[])
{
    banner_t *banner;
    int noptions;
    cups_option_t *options;
    ppd_file_t *ppd;
    int ret;

    if (argc < 6) {
        fprintf(stderr,
                "Usage: %s job-id user job-title nr-copies options [file]\n",
                argv[0]);
        return 1;
    }

    ppd = ppdOpenFile(getenv("PPD"));
    if (!ppd)
      fprintf(stderr, "DEBUG: Could not open PPD file '%s'\n", getenv("PPD"));

    noptions = cupsParseOptions(argv[5], 0, &options);
    if (ppd) {
      ppdMarkDefaults(ppd);
      cupsMarkOptions(ppd, noptions, options);
    }

    banner = banner_new_from_file(argc == 7 ? argv[6] : "-", &noptions, &options);
    if (!banner) {
        fprintf(stderr, "Error: could not read banner file\n");
        return 1;
    }

    ret = generate_banner_pdf(banner,
                              ppd,
                              argv[1],
                              argv[2],
                              argv[3],
                              noptions,
                              options);

    banner_free(banner);
    cupsFreeOptions(noptions, options);
    return ret;
}

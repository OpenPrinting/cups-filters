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
#include <errno.h>
#include <cupsfilters/ppdgenerator.h>
#include "raster.h"

#ifndef HAVE_OPEN_MEMSTREAM
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
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
#include "pdf.h"

typedef enum banner_info_e
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
} banner_info_t;

typedef struct banner_s
{
    char *template_file;
    char *header, *footer;
    unsigned infos;
} banner_t;

static void banner_free(banner_t *banner)
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

static unsigned parse_show(char *s, cf_logfunc_t log, void *ld)
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
            log(ld, CF_LOGLEVEL_ERROR, "cfFilterBannerToPDF: error: unknown value for 'Show': %s\n", tok);
    }
    return info;
}

static char *template_path(const char *name, const char *datadir)
{
    char *result;

    if (name[0] == '/')
        return strdup(name);

    result = malloc(strlen(datadir) + strlen(name) + 2);
    sprintf(result, "%s/%s", datadir, name);

    return result;
}

static banner_t *banner_new_from_file(const char *filename, int *num_options,
			       cups_option_t **options, const char *datadir,
			       cf_logfunc_t log, void *ld)
{
    FILE *f;
    char *line = NULL;
    size_t len = 0, bytes_read;
    int linenr = 0;
    int ispdf = 0;
    int gotinfos = 0;
    banner_t *banner = NULL;

    if (!(f = fopen(filename, "r")))
    {
      if (log)
	log(ld, CF_LOGLEVEL_ERROR,
	    "cfFilterBannerToPDF: Error opening temporary file with input stream");
      goto out;
    }

    while ((bytes_read = getline(&line, &len, f)) != -1)
    {
      char *start = line;

      linenr ++;

      if (bytes_read == -1)
      {
	if (log)
	  log(ld, CF_LOGLEVEL_ERROR,
	      "cfFilterBannerToPDF: No banner instructions found in input stream");
	goto out;
      }

      if (strncmp(line, "%PDF-", 5) == 0)
	ispdf = 1;
      while(start < line + len &&
	    ((ispdf && *start == '%') || isspace(*start)))
	start ++;
      if (strncasecmp(start, "#PDF-BANNER", 11) == 0 ||
	  strncasecmp(start, "PDF-BANNER", 10) == 0)
	break;
    }


    banner = calloc(1, sizeof *banner);

    while (getline(&line, &len, f) != -1)
    {
        char *key, *value;

        linenr++;
        if (!parse_line(line, &key, &value))
            continue;

        if (!value)
        {
	    if (ispdf)
	      break;
            if (log)
                log(ld, CF_LOGLEVEL_ERROR,
		    "cfFilterBannerToPDF: Line %d is missing a value", linenr);
            continue;
        }

	if (ispdf)
	  while (*key == '%')
	    key ++;

        if (!strcasecmp(key, "template"))
            banner->template_file = template_path(value, datadir);
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
	{
            banner->infos = parse_show(value, log, ld);
	    gotinfos = 1;
	}
        else if (!strcasecmp(key, "image") ||
                 !strcasecmp(key, "notice"))
        {
            if (log)
                log(ld, CF_LOGLEVEL_ERROR,
                    "cfFilterBannerToPDF: Note: %d: bannertopdf does not support '%s'",
                    linenr, key);
        }
        else
        {
	    if (ispdf)
	      break;
            if (log)
                log(ld, CF_LOGLEVEL_ERROR,
                    "cfFilterBannerToPDF: Error: %d: unknown keyword '%s'",
                    linenr, key);
        }
    }

    /* load default template if none was specified */
    if (!banner->template_file)
    {
      if (ispdf)
	banner->template_file = strdup(filename);
      else
        banner->template_file = template_path("default.pdf", datadir);
    }
    if (!gotinfos)
    {
      char *value = strdup("printer-name printer-info printer-location printer-make-and-model printer-driver-name printer-driver-version paper-size imageable-area job-id options time-at-creation time-at-processing");
      banner->infos = parse_show(value, log, ld);
      free(value);
    }

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

static void get_pagesize(cf_filter_data_t *data,
                         int noptions,
                         cups_option_t *options,
                         float *width,
                         float *length,
                         float media_limits[4])
{
    ppd_file_t *ppd = data->ppd;
    ipp_t *printer_attrs = data->printer_attrs;
    static ppd_size_t defaultsize = {
        0,     /* marked */
        "",    /* name */
        612.0, /* width */
        792.0, /* length */
        18.0,  /* left */
        36.0,  /* bottom */
        594.0, /* right */
        756.0, /* top */
    };
    ppd_size_t *pagesize = &defaultsize;
#ifdef HAVE_CUPS_1_7
    pwg_media_t *size_found; /* page size found for given name */
    const char *val;         /* Pointer into value */
    char *ptr1, *ptr2,       /* Pointer into string */
        s[255];              /* Temporary string */
#endif                       /* HAVE_CUPS_1_7 */

    if (!ppd || !(pagesize = ppdPageSize(ppd, NULL))) {
	pagesize = &defaultsize;
	if(printer_attrs!=NULL){
	    int left,
	    right,
	    top,
	    bottom,
	    min_length = 2147483647,
	    min_width = 2147483647,
	    max_length = 0,
	    max_width = 0;
	    char defsize[41];
	    ipp_attribute_t *defattr;
	    cups_array_t *printer_sizes;
	    printer_sizes = cfGenerateSizes(printer_attrs, &defattr, &min_length, &min_width,
					    &max_length, &max_width, 
					    &bottom, &left, &right, &top, defsize);
	    for(cups_size_t *size = (cups_size_t*)cupsArrayFirst(printer_sizes); size;
		size = (cups_size_t*)cupsArrayNext(printer_sizes)){
		    if(!strcmp(size->media, defsize)){
			ppd_size_t temp;
			snprintf(temp.name, sizeof(temp.name),"%s", defsize);
			temp.top = size->top *72.0 / 2540.0;
			temp.bottom = size->bottom *72.0 / 2540.0;
			temp.left = size->left *72.0 / 2540.0;
			temp.right = size->right *72.0 / 2540.0;
			temp.width = size->width *72.0 / 2540.0;
			temp.length = size->length *72.0 / 2540.0;
			pagesize = &temp;
			break;
		    }
		}
	}
    }

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
static cf_opt_t *add_opt(cf_opt_t *in_opt, const char *key, const char *val)
{
    if (!key || !val)
    {
        return in_opt;
    }

    if (!strlen(key) || !strlen(val))
    {
        return in_opt;
    }

    cf_opt_t *entry = malloc(sizeof(cf_opt_t));
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
static cf_opt_t *get_known_opts(
    cf_filter_data_t *data,
    const char *jobid,
    const char *user,
    const char *jobtitle,
    int noptions,
    
    cups_option_t *options)
{

    ppd_file_t *ppd = data->ppd;
    ppd_attr_t *attr;
    cf_opt_t *opt = NULL;
    ipp_t *printer_attrs = data->printer_attrs;
    ipp_attribute_t *ipp_attr;
    char buf[1024];
    const char *value = NULL;

    /* Job ID */
    opt = add_opt(opt, "job-id", jobid);

    /* Job title */
    opt = add_opt(opt, "job-title", jobtitle);

    /* Printed by */
    opt = add_opt(opt, "user", user);

    /* Printer name */
    opt = add_opt(opt, "printer-name", data->printer);

    /* Printer info */
    if ((value = cupsGetOption("printer-info",
				noptions, options)) == NULL || !value[0])
      value =  getenv("PRINTER_INFO");
    opt = add_opt(opt, "printer-info", value);

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
    else
    {
	/* Driver */
	opt = add_opt(opt, "driver", "drvless.ppd");

	/* Driver version */
        opt = add_opt(opt, "driver-version", VERSION);

	/* Make and model */
	int is_fax = 0;
	char make[256];
	char *model;
	if ((ipp_attr = ippFindAttribute(printer_attrs, "ipp-features-supported",
				IPP_TAG_ZERO)) != NULL &&
		ippContainsString(ipp_attr, "faxout"))
	{
	    ipp_attr = ippFindAttribute(printer_attrs, "printer-uri-supported",
		IPP_TAG_ZERO);
	    if (ipp_attr)
	    {
	    	ippAttributeString(ipp_attr, buf, sizeof(buf));
	    	if (strcasestr(buf, "faxout"))
		    is_fax = 1;
	    }
	}

	if ((ipp_attr = ippFindAttribute(printer_attrs, "printer-make-and-model",
			    	IPP_TAG_ZERO)) != NULL)
	    snprintf(make, sizeof(make), "%s", ippGetString(ipp_attr, 0, NULL));
	else
	    snprintf(make, sizeof(make), "%s", "Unknown Printer");
	if (!strncasecmp(make, "Hewlett Packard ", 16) ||
	    !strncasecmp(make, "Hewlett-Packard ", 16)) {
		model = make + 16;
		snprintf(make, sizeof(make), "%s", "HP");
	}
	else if ((model = strchr(make, ' ')) != NULL)
	    *model++ = '\0';
	else
	    model = make;
	snprintf(buf, sizeof(buf), "%s %s, %sdriverless, cups-filters %s",
		make, model, (is_fax ? "Fax, " : ""), VERSION);
	char *nickname = buf;
	opt = add_opt(opt, "make-and-model", nickname);
    }


    return opt;
}

static int generate_banner_pdf(banner_t *banner,
                               cf_filter_data_t *data,
                               const char *jobid,
                               const char *user,
                               const char *jobtitle,
                               int noptions,
                               cups_option_t *options,
                               cf_logfunc_t log,
                               void *ld,
                               FILE *outputfp)
{
    char *buf;
    size_t len;
    FILE *s;
    cf_pdf_t *doc;
    float page_width, page_length;
    float media_limits[4];
    float page_scale;
    ppd_attr_t *attr;
    unsigned copies;
    ipp_t *printer_attrs = data->printer_attrs;
    ipp_attribute_t *ipp_attr;
    ppd_file_t *ppd = data->ppd;
    char buf2[1024];
    const char *value;
#ifndef HAVE_OPEN_MEMSTREAM
    struct stat st;
#endif

    if (!(doc = cfPDFLoadTemplate(banner->template_file)))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "PDF template must exist and contain exactly 1 page: %s",
		   banner->template_file);
      return (1);
    }

    get_pagesize(data, noptions, options,
                 &page_width, &page_length, media_limits);

    if (cfPDFResizePage(doc, 1, page_width, page_length, &page_scale) != 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Unable to resize requested PDF page");
      cfPDFFree(doc);
      return (1);
    }

    if (cfPDFAddType1Font(doc, 1, "Courier") != 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Unable to add type1 font to requested PDF page");
      cfPDFFree(doc);
      return (1);
    }

#ifdef HAVE_OPEN_MEMSTREAM
    s = open_memstream(&buf, &len);
#else
    if ((s = tmpfile()) == NULL)
    {
        if (log)
            log(ld, CF_LOGLEVEL_ERROR, "cfFilterBannerToPDF: Cannot create temp file: %s\n", strerror(errno));
	cfPDFFree(doc);
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

    if ((banner->infos & INFO_PRINTER_NAME) &&
	data->printer && data->printer[0])
        info_line(s, "Printer", data->printer);

    if ((banner->infos & INFO_PRINTER_INFO) &&
	(((value = cupsGetOption("printer-info",
				 noptions, options)) != NULL && value[0]) ||
	 ((value = getenv("PRINTER_INFO")) != NULL && value[0])))
        info_line(s, "Description", value);

    if ((banner->infos & INFO_PRINTER_LOCATION) &&
	(((value = cupsGetOption("printer-location",
				 noptions, options)) != NULL && value[0]) ||
	 ((value = getenv("PRINTER_LOCATION")) != NULL && value[0])))
        info_line(s, "Location", value);

    if ((banner->infos & INFO_JOB_ID) &&
	data->printer && data->printer[0] && jobid && jobid[0])
        info_linef(s, "Job ID", "%s-%s", data->printer, jobid);

    if ((banner->infos & INFO_JOB_NAME) && jobtitle && jobtitle[0])
        info_line(s, "Job Title", jobtitle);

    if ((banner->infos & INFO_JOB_ORIGINATING_HOST_NAME) &&
	(value = cupsGetOption("job-originating-host-name",
			      noptions, options)) != NULL && value[0])
        info_line(s, "Printed from", value);

    if ((banner->infos & INFO_JOB_ORIGINATING_USER_NAME) && user && user[0])
        info_line(s, "Printed by", user);

    if ((banner->infos & INFO_TIME_AT_CREATION) &&
	(value =
	 cupsGetOption("time-at-creation", noptions, options)) != NULL &&
	value[0])
        info_line_time(s, "Created at", value);

    if ((banner->infos & INFO_TIME_AT_PROCESSING) &&
	(value =
	 cupsGetOption("time-at-processing", noptions, options)) != NULL &&
	value[0])
        info_line_time(s, "Printed at", value);

    if ((banner->infos & INFO_JOB_BILLING) &&
	(value = cupsGetOption("job-billing", noptions, options)) != NULL &&
	value[0])
        info_line(s, "Billing Information\n", value);

    if ((banner->infos & INFO_JOB_UUID) &&
	(value = cupsGetOption("job-uuid", noptions, options)) != NULL &&
	value[0])
        info_line(s, "Job UUID", value);

    if (ppd)
    {
      if ((banner->infos & INFO_PRINTER_DRIVER_NAME) ||
	  (banner->infos & INFO_PRINTER_MAKE_AND_MODEL))
            info_line(s, "Driver/Model", ppd->nickname);

	if ((banner->infos & INFO_PRINTER_DRIVER_VERSION) &&
	    (attr = ppdFindAttr(ppd, "FileVersion", NULL)) != NULL &&
	    attr->value && attr->value[0])
	  info_line(s, "Driver Version", attr->value);
    }
    else if ((banner->infos & INFO_PRINTER_DRIVER_NAME) ||
	     (banner->infos & INFO_PRINTER_MAKE_AND_MODEL))

    {
	int is_fax = 0;
	char make[256];
	char *model;
	if ((ipp_attr = ippFindAttribute(printer_attrs,
					 "ipp-features-supported",
					 IPP_TAG_ZERO)) != NULL &&
	    ippContainsString(ipp_attr, "faxout"))
	{
	    ipp_attr = ippFindAttribute(printer_attrs, "printer-uri-supported",
		IPP_TAG_ZERO);
	    if (ipp_attr)
	    {
	    	ippAttributeString(ipp_attr, buf2, sizeof(buf2));
	    	if (strcasestr(buf2, "faxout"))
		    is_fax = 1;
	    }
	}

	if ((ipp_attr = ippFindAttribute(printer_attrs,
					 "printer-make-and-model",
					 IPP_TAG_ZERO)) != NULL)
	{
	    snprintf(make, sizeof(make), "%s", ippGetString(ipp_attr, 0, NULL));

	    if (!strncasecmp(make, "Hewlett Packard ", 16) ||
		!strncasecmp(make, "Hewlett-Packard ", 16))
	    {
		model = make + 16;
		snprintf(make, sizeof(make), "%s", "HP");
	    }
	    else if ((model = strchr(make, ' ')) != NULL)
	      *model++ = '\0';
	    else
	      model = make;
	    snprintf(buf2, sizeof(buf2), "%s %s%s",
		     make, model, (is_fax ? " (Fax)" : ""));
	    char *nickname = buf2;
	    info_line(s, "Make and Model", nickname);
	}
    }

    if (banner->infos & INFO_IMAGEABLE_AREA)
    {
        info_linef(s, "Media Limits", "%.2f x %.2f to %.2f x %.2f inches",
                   media_limits[0] / 72.0,
                   media_limits[1] / 72.0,
                   media_limits[2] / 72.0,
                   media_limits[3] / 72.0);
        info_linef(s, "Media Limits", "%.2f x %.2f to %.2f x %.2f cm",
                   media_limits[0] / 72.0 * 2.54,
                   media_limits[1] / 72.0 * 2.54,
                   media_limits[2] / 72.0 * 2.54,
                   media_limits[3] / 72.0 * 2.54);
    }

    fprintf(s, "ET\n");
#ifndef HAVE_OPEN_MEMSTREAM
    fflush(s);
    if (fstat(fileno(s), &st) < 0)
    {
        if (log)
            log(ld, CF_LOGLEVEL_ERROR, "cfFilterBannerToPDF: Cannot fstat(): %s\n", , strerror(errno));
        return 1;
    }
    fseek(s, 0L, SEEK_SET);
    if ((buf = malloc(st.st_size + 1)) == NULL)
    {
        if (log)
            log(ld, CF_LOGLEVEL_ERROR, "cfFilterBannerToPDF: Cannot malloc(): %s\n", , strerror(errno));
        return 1;
    }
    size_t nbytes = fread(buf, 1, st.st_size, s);
    buf[st.st_size] = '\0';
    len = strlen(buf);
#endif /* !HAVE_OPEN_MEMSTREAM */
    fclose(s);

    cf_opt_t *known_opts = get_known_opts(data,
                                       jobid,
                                       user,
                                       jobtitle,
                                       noptions,
                                       options);

   /*
    * Try to find a PDF form in PDF template and fill it.
    */

    if (cfPDFFillForm(doc, known_opts) != 0)
    {
     /*
      * Could we fill a PDF form? If no, just add PDF stream.
      */

      if (cfPDFPrependStream(doc, 1, buf, len) != 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "Unable to prepend stream to requested PDF page");
      }
    }

    copies = get_int_option("number-up", noptions, options, 1);

    if (duplex_marked(ppd, noptions, options))
        copies *= 2;

    if (copies > 1)
    {
      if (cfPDFDuplicatePage(doc, 1, copies - 1) != 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "Unable to duplicate requested PDF page");
      }
    }

    cfPDFWrite(doc, outputfp);

    cf_opt_t *opt_current = known_opts;
    cf_opt_t *opt_next = NULL;
    while (opt_current != NULL)
    {
        opt_next = opt_current->next;
        free(opt_current);
        opt_current = opt_next;
    }

    free(buf);
    cfPDFFree(doc);
    return 0;
}

int cfFilterBannerToPDF(int inputfd,         /* I - File descriptor input stream */
                int outputfd,        /* I - File descriptor output stream */
                int inputseekable,   /* I - Is input stream seekable? (unused)*/
                cf_filter_data_t *data, /* I - Job and printer data */
                void *parameters)    /* I - Filter-specific parameters -
				            Template/Banner data directory */
{
    banner_t *banner;
    int num_options = 0;
    int ret;
    FILE *inputfp;
    FILE *outputfp;
    int tempfd;
    cups_option_t *options = NULL;
    char tempfile[1024], buffer[1024];
    size_t bytes;
    const char *datadir = (const char *)parameters;

    cf_logfunc_t log = data->logfunc;
    void *ld = data->logdata;
    cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
    void *icd = data->iscanceleddata;
    char jobid[50];

    num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

   /*
    * Open the input data stream specified by the inputfd...
    */

    if ((inputfp = fdopen(inputfd, "rb")) == NULL)
    {
        if (!iscanceled || !iscanceled(icd))
        {
            if (log)
                log(ld, CF_LOGLEVEL_DEBUG,
                    "cfFilterBannerToPDF: Unable to open input data stream.");
        }
        return (1);
    }

   /*
    * Copy the input data stream into a temporary file...
    */

    if ((tempfd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterBannerToPDF: Unable to copy input file: %s",
		   strerror(errno));
      return (1);
    }

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterBannerToPDF: Copying input to temp file \"%s\"",
		 tempfile);

    while ((bytes = fread(buffer, 1, sizeof(buffer), inputfp)) > 0)
      bytes = write(tempfd, buffer, bytes);

    if (inputfd)
    {
      fclose(inputfp);
      close(inputfd);
    }
    close(tempfd);

   /*
    * Open the output data stream specified by the outputfd...
    */

    if ((outputfp = fdopen(outputfd, "w")) == NULL)
    {
        if (!iscanceled || !iscanceled(icd))
        {
            if (log)
                log(ld, CF_LOGLEVEL_DEBUG,
                    "cfFilterBannerToPDF: Unable to open output data stream.");
        }

        fclose(inputfp);
        return (1);
    }

   /*
    * Parse the instructions...
    */
    banner = banner_new_from_file(tempfile, &num_options, &options, datadir,
				  log, ld);

    if (!banner)
    {
        if (log)
            log(ld, CF_LOGLEVEL_ERROR, "cfFilterBannerToPDF: Could not read banner file");
        return 1;
    }

    sprintf(jobid, "%d", data->job_id);

   /*
    * Create the banner/test page
    */
    ret = generate_banner_pdf(banner,
                              data,
                              jobid,
                              data->job_user,
                              data->job_title,
                              num_options,
                              options,
                              log,
                              ld,
                              outputfp);

   /*
    * Clean up...
    */
    banner_free(banner);
    if (options) cupsFreeOptions(num_options, options);
    unlink(tempfile);
    
    return ret;
}

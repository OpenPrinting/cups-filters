/*
 * Option encoding routines for libppd.
 *
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "ipp-private.h"
#include "debug-internal.h"


/*
 * Local list of option names, the value tags they should use, and the list of
 * supported operations...
 *
 * **** THIS LIST MUST BE SORTED BY ATTRIBUTE NAME ****
 */

static const ipp_op_t ipp_job_creation[] =
{
  IPP_OP_PRINT_JOB,
  IPP_OP_PRINT_URI,
  IPP_OP_VALIDATE_JOB,
  IPP_OP_CREATE_JOB,
  IPP_OP_HOLD_JOB,
  IPP_OP_SET_JOB_ATTRIBUTES,
  IPP_OP_CUPS_NONE
};

static const ipp_op_t ipp_doc_creation[] =
{
  IPP_OP_PRINT_JOB,
  IPP_OP_PRINT_URI,
  IPP_OP_SEND_DOCUMENT,
  IPP_OP_SEND_URI,
  IPP_OP_SET_JOB_ATTRIBUTES,
  IPP_OP_SET_DOCUMENT_ATTRIBUTES,
  IPP_OP_CUPS_NONE
};

static const ipp_op_t ipp_all_print[] =
{
  IPP_OP_PRINT_JOB,
  IPP_OP_PRINT_URI,
  IPP_OP_VALIDATE_JOB,
  IPP_OP_CREATE_JOB,
  IPP_OP_SEND_DOCUMENT,
  IPP_OP_SEND_URI,
  IPP_OP_CUPS_NONE
};

static const ipp_op_t cups_schemes[] =
{
  IPP_OP_CUPS_GET_DEVICES,
  IPP_OP_CUPS_GET_PPDS,
  IPP_OP_CUPS_NONE
};

static const ipp_op_t cups_get_ppds[] =
{
  IPP_OP_CUPS_GET_PPDS,
  IPP_OP_CUPS_NONE
};

static const ipp_op_t cups_ppd_name[] =
{
  IPP_OP_CUPS_ADD_MODIFY_PRINTER,
  IPP_OP_CUPS_GET_PPD,
  IPP_OP_CUPS_NONE
};

static const _ppd_ipp_option_t ipp_options[] =
{
  { 1, "auth-info",		IPP_TAG_TEXT,		IPP_TAG_JOB },
  { 1, "auth-info-default",	IPP_TAG_TEXT,		IPP_TAG_PRINTER },
  { 1, "auth-info-required",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "blackplot",		IPP_TAG_BOOLEAN,	IPP_TAG_JOB },
  { 0, "blackplot-default",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "brightness",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "brightness-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "columns",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "columns-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "compression",		IPP_TAG_KEYWORD,	IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							ipp_doc_creation },
  { 0, "copies",		IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "copies-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "date-time-at-completed",IPP_TAG_DATE,		IPP_TAG_ZERO }, /* never send as option */
  { 0, "date-time-at-creation",	IPP_TAG_DATE,		IPP_TAG_ZERO }, /* never send as option */
  { 0, "date-time-at-processing",IPP_TAG_DATE,		IPP_TAG_ZERO }, /* never send as option */
  { 0, "device-uri",		IPP_TAG_URI,		IPP_TAG_PRINTER },
  { 1, "document-copies",	IPP_TAG_RANGE,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT,
							ipp_doc_creation },
  { 0, "document-format",	IPP_TAG_MIMETYPE,	IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							ipp_doc_creation },
  { 0, "document-format-default", IPP_TAG_MIMETYPE,	IPP_TAG_PRINTER },
  { 1, "document-numbers",	IPP_TAG_RANGE,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT,
							ipp_all_print },
  { 1, "exclude-schemes",	IPP_TAG_NAME,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_schemes },
  { 1, "finishings",		IPP_TAG_ENUM,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 1, "finishings-col",	IPP_TAG_BEGIN_COLLECTION, IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 1, "finishings-col-default", IPP_TAG_BEGIN_COLLECTION, IPP_TAG_PRINTER },
  { 1, "finishings-default",	IPP_TAG_ENUM,		IPP_TAG_PRINTER },
  { 0, "fit-to-page",		IPP_TAG_BOOLEAN,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "fit-to-page-default",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "fitplot",		IPP_TAG_BOOLEAN,	IPP_TAG_JOB },
  { 0, "fitplot-default",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "gamma",			IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "gamma-default",		IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "hue",			IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "hue-default",		IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "include-schemes",	IPP_TAG_NAME,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_schemes },
  { 0, "ipp-attribute-fidelity", IPP_TAG_BOOLEAN,	IPP_TAG_OPERATION },
  { 0, "job-account-id",        IPP_TAG_NAME,           IPP_TAG_JOB },
  { 0, "job-account-id-default",IPP_TAG_NAME,           IPP_TAG_PRINTER },
  { 0, "job-accounting-user-id", IPP_TAG_NAME,          IPP_TAG_JOB },
  { 0, "job-accounting-user-id-default", IPP_TAG_NAME,  IPP_TAG_PRINTER },
  { 0, "job-authorization-uri",	IPP_TAG_URI,		IPP_TAG_OPERATION },
  { 0, "job-cancel-after",	IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "job-cancel-after-default", IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "job-hold-until",	IPP_TAG_KEYWORD,	IPP_TAG_JOB },
  { 0, "job-hold-until-default", IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "job-id",		IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-impressions",	IPP_TAG_INTEGER,	IPP_TAG_OPERATION },
  { 0, "job-impressions-completed", IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-k-limit",		IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "job-k-octets",		IPP_TAG_INTEGER,	IPP_TAG_OPERATION },
  { 0, "job-k-octets-completed",IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-media-sheets",	IPP_TAG_INTEGER,	IPP_TAG_OPERATION },
  { 0, "job-media-sheets-completed", IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-name",		IPP_TAG_NAME,		IPP_TAG_OPERATION,
							IPP_TAG_JOB },
  { 0, "job-page-limit",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "job-pages",		IPP_TAG_INTEGER,	IPP_TAG_OPERATION },
  { 0, "job-pages-completed",	IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-password",          IPP_TAG_STRING,         IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							ipp_job_creation },
  { 0, "job-password-encryption", IPP_TAG_KEYWORD,      IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							ipp_job_creation },
  { 0, "job-priority",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "job-priority-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "job-quota-period",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "job-sheets",		IPP_TAG_NAME,		IPP_TAG_JOB },
  { 1, "job-sheets-default",	IPP_TAG_NAME,		IPP_TAG_PRINTER },
  { 0, "job-state",		IPP_TAG_ENUM,		IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-state-message",	IPP_TAG_TEXT,		IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-state-reasons",	IPP_TAG_KEYWORD,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "job-uuid",		IPP_TAG_URI,		IPP_TAG_JOB },
  { 0, "landscape",		IPP_TAG_BOOLEAN,	IPP_TAG_JOB },
  { 1, "marker-change-time",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "marker-colors",		IPP_TAG_NAME,		IPP_TAG_PRINTER },
  { 1, "marker-high-levels",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "marker-levels",		IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "marker-low-levels",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "marker-message",	IPP_TAG_TEXT,		IPP_TAG_PRINTER },
  { 1, "marker-names",		IPP_TAG_NAME,		IPP_TAG_PRINTER },
  { 1, "marker-types",		IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 1, "media",			IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-bottom-margin",	IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-col",		IPP_TAG_BEGIN_COLLECTION, IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-col-default",	IPP_TAG_BEGIN_COLLECTION, IPP_TAG_PRINTER },
  { 0, "media-color",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 1, "media-default",		IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "media-key",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-left-margin",	IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-right-margin",	IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-size",		IPP_TAG_BEGIN_COLLECTION, IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-size-name",	IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-source",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-top-margin",	IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "media-type",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "mirror",		IPP_TAG_BOOLEAN,	IPP_TAG_JOB },
  { 0, "mirror-default",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "multiple-document-handling", IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "multiple-document-handling-default", IPP_TAG_KEYWORD, IPP_TAG_PRINTER },
  { 0, "natural-scaling",	IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "natural-scaling-default", IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "notify-charset",	IPP_TAG_CHARSET,	IPP_TAG_SUBSCRIPTION },
  { 1, "notify-events",		IPP_TAG_KEYWORD,	IPP_TAG_SUBSCRIPTION },
  { 1, "notify-events-default",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "notify-lease-duration",	IPP_TAG_INTEGER,	IPP_TAG_SUBSCRIPTION },
  { 0, "notify-lease-duration-default", IPP_TAG_INTEGER, IPP_TAG_PRINTER },
  { 0, "notify-natural-language", IPP_TAG_LANGUAGE,	IPP_TAG_SUBSCRIPTION },
  { 0, "notify-pull-method",	IPP_TAG_KEYWORD,	IPP_TAG_SUBSCRIPTION },
  { 0, "notify-recipient-uri",	IPP_TAG_URI,		IPP_TAG_SUBSCRIPTION },
  { 0, "notify-time-interval",	IPP_TAG_INTEGER,	IPP_TAG_SUBSCRIPTION },
  { 0, "notify-user-data",	IPP_TAG_STRING,		IPP_TAG_SUBSCRIPTION },
  { 0, "number-up",		IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "number-up-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "number-up-layout",	IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "number-up-layout-default", IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "orientation-requested",	IPP_TAG_ENUM,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "orientation-requested-default", IPP_TAG_ENUM,	IPP_TAG_PRINTER },
  { 0, "output-bin",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "output-bin-default",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 1, "overrides",		IPP_TAG_BEGIN_COLLECTION, IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "page-bottom",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "page-bottom-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "page-delivery",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "page-delivery-default",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "page-left",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "page-left-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "page-ranges",		IPP_TAG_RANGE,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "page-right",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "page-right-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "page-top",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "page-top-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "pages",			IPP_TAG_RANGE,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "penwidth",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "penwidth-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "port-monitor",		IPP_TAG_NAME,		IPP_TAG_PRINTER },
  { 0, "ppd-device-id",		IPP_TAG_TEXT,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-make",		IPP_TAG_TEXT,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-make-and-model",	IPP_TAG_TEXT,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-model-number",	IPP_TAG_INTEGER,	IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-name",		IPP_TAG_NAME,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_ppd_name },
  { 0, "ppd-natural-language",	IPP_TAG_LANGUAGE,	IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-product",		IPP_TAG_TEXT,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-psversion",		IPP_TAG_TEXT,		IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppd-type",		IPP_TAG_KEYWORD,	IPP_TAG_OPERATION,
							IPP_TAG_ZERO,
							cups_get_ppds },
  { 0, "ppi",			IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "ppi-default",		IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "prettyprint",		IPP_TAG_BOOLEAN,	IPP_TAG_JOB },
  { 0, "prettyprint-default",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "print-color-mode",	IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "print-color-mode-default", IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "print-content-optimize", IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "print-content-optimize-default", IPP_TAG_KEYWORD, IPP_TAG_PRINTER },
  { 0, "print-quality",		IPP_TAG_ENUM,		IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "print-quality-default",	IPP_TAG_ENUM,		IPP_TAG_PRINTER },
  { 0, "print-rendering-intent", IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "print-rendering-intent-default", IPP_TAG_KEYWORD, IPP_TAG_PRINTER },
  { 0, "print-scaling",		IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "print-scaling-default",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 1, "printer-alert",		IPP_TAG_STRING,		IPP_TAG_PRINTER },
  { 1, "printer-alert-description", IPP_TAG_TEXT,	IPP_TAG_PRINTER },
  { 1, "printer-commands",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "printer-error-policy",	IPP_TAG_NAME,		IPP_TAG_PRINTER },
  { 1, "printer-finisher",	IPP_TAG_STRING,		IPP_TAG_PRINTER },
  { 1, "printer-finisher-description", IPP_TAG_TEXT,	IPP_TAG_PRINTER },
  { 1, "printer-finisher-supplies", IPP_TAG_STRING,	IPP_TAG_PRINTER },
  { 1, "printer-finisher-supplies-description", IPP_TAG_TEXT, IPP_TAG_PRINTER },
  { 0, "printer-geo-location",	IPP_TAG_URI,		IPP_TAG_PRINTER },
  { 0, "printer-info",		IPP_TAG_TEXT,		IPP_TAG_PRINTER },
  { 1, "printer-input-tray",	IPP_TAG_STRING,		IPP_TAG_PRINTER },
  { 0, "printer-is-accepting-jobs", IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "printer-is-shared",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "printer-is-temporary",	IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "printer-location",	IPP_TAG_TEXT,		IPP_TAG_PRINTER },
  { 0, "printer-make-and-model", IPP_TAG_TEXT,		IPP_TAG_PRINTER },
  { 0, "printer-more-info",	IPP_TAG_URI,		IPP_TAG_PRINTER },
  { 0, "printer-op-policy",	IPP_TAG_NAME,		IPP_TAG_PRINTER },
  { 1, "printer-output-tray",	IPP_TAG_STRING,		IPP_TAG_PRINTER },
  { 0, "printer-resolution",	IPP_TAG_RESOLUTION,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "printer-resolution-default", IPP_TAG_RESOLUTION, IPP_TAG_PRINTER },
  { 0, "printer-state",		IPP_TAG_ENUM,		IPP_TAG_PRINTER },
  { 0, "printer-state-change-time", IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 1, "printer-state-reasons",	IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 1, "printer-supply",	IPP_TAG_STRING,		IPP_TAG_PRINTER },
  { 1, "printer-supply-description", IPP_TAG_TEXT,	IPP_TAG_PRINTER },
  { 0, "printer-type",		IPP_TAG_ENUM,		IPP_TAG_PRINTER },
  { 0, "printer-uri",		IPP_TAG_URI,		IPP_TAG_OPERATION },
  { 1, "printer-uri-supported",	IPP_TAG_URI,		IPP_TAG_PRINTER },
  { 0, "queued-job-count",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "raw",			IPP_TAG_MIMETYPE,	IPP_TAG_OPERATION },
  { 1, "requested-attributes",	IPP_TAG_NAME,		IPP_TAG_OPERATION },
  { 1, "requesting-user-name-allowed", IPP_TAG_NAME,	IPP_TAG_PRINTER },
  { 1, "requesting-user-name-denied", IPP_TAG_NAME,	IPP_TAG_PRINTER },
  { 0, "resolution",		IPP_TAG_RESOLUTION,	IPP_TAG_JOB },
  { 0, "resolution-default",	IPP_TAG_RESOLUTION,	IPP_TAG_PRINTER },
  { 0, "saturation",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "saturation-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "scaling",		IPP_TAG_INTEGER,	IPP_TAG_JOB },
  { 0, "scaling-default",	IPP_TAG_INTEGER,	IPP_TAG_PRINTER },
  { 0, "sides",			IPP_TAG_KEYWORD,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "sides-default",		IPP_TAG_KEYWORD,	IPP_TAG_PRINTER },
  { 0, "time-at-completed",	IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "time-at-creation",	IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "time-at-processing",	IPP_TAG_INTEGER,	IPP_TAG_ZERO }, /* never send as option */
  { 0, "wrap",			IPP_TAG_BOOLEAN,	IPP_TAG_JOB },
  { 0, "wrap-default",		IPP_TAG_BOOLEAN,	IPP_TAG_PRINTER },
  { 0, "x-dimension",		IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT },
  { 0, "y-dimension",		IPP_TAG_INTEGER,	IPP_TAG_JOB,
							IPP_TAG_DOCUMENT }
};


/*
 * Local functions...
 */

static int	ppd_compare_ipp_options(_ppd_ipp_option_t *a,
					_ppd_ipp_option_t *b);


/*
 * '_ppdIppFindOption()' - Find the attribute information for an option.
 */

_ppd_ipp_option_t *				/* O - Attribute information */
_ppdIppFindOption(const char *name)	/* I - Option/attribute name */
{
  _ppd_ipp_option_t	key;			/* Search key */


 /*
  * Lookup the proper value and group tags for this option...
  */

  key.name = name;

  return ((_ppd_ipp_option_t *)bsearch(&key, ipp_options,
                                   sizeof(ipp_options) / sizeof(ipp_options[0]),
				   sizeof(ipp_options[0]),
				   (int (*)(const void *, const void *))
				       ppd_compare_ipp_options));
}


/*
 * 'ppd_compare_ipp_options()' - Compare two IPP options.
 */

static int					/* O - Result of comparison */
ppd_compare_ipp_options(_ppd_ipp_option_t *a,	/* I - First option */
			_ppd_ipp_option_t *b)	/* I - Second option */
{
  return (strcmp(a->name, b->name));
}

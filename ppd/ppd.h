/*
 * PostScript Printer Description definitions for libppd.
 *
 * PPD FILES ARE DEPRECATED. libppd IS ONLY INTENDED FOR LEGACY IMPORT OF
 * CLASSIC CUPS DRIVERS AND POSTSCRIPT PRINTER PPD FILES.
 *
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 *
 * PostScript is a trademark of Adobe Systems, Inc.
 */

#ifndef _PPD_PPD_H_
#  define _PPD_PPD_H_

/*
 * Include necessary headers...
 */

/* We do not depend on libcupsfilters here, we only share the call scheme
   for log functions, so that the same log functions can be used with all
   libraries of the cups-filters project */
#  include <cupsfilters/log.h>

#  include <stdio.h>
#  include <cups/raster.h>


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/*
 * Constants...
 */

#  define PPD_CACHE_VERSION	9	/* Version number in cache file */


/*
 * PPD version...
 */

#  define PPD_VERSION	4.3		/* Kept in sync with Adobe version
					   number */


/*
 * PPD size limits (defined in Adobe spec)
 */

#  define PPD_MAX_NAME	41		/* Maximum size of name + 1 for nul */
#  define PPD_MAX_TEXT	81		/* Maximum size of text + 1 for nul */
#  define PPD_MAX_LINE	256		/* Maximum size of line + 1 for nul */


/**** New in cups-filters 2.0.0: Ovetaken from cups-driverd ****/
/*
 * PPD collection entry data
 */

#  define PPD_SYNC	0x50504441	/* Sync word for ppds.dat (PPDA) */
#  define PPD_MAX_LANG	32		/* Maximum languages */
#  define PPD_MAX_PROD	32		/* Maximum products */
#  define PPD_MAX_VERS	32		/* Maximum versions */

#  define PPD_TYPE_POSTSCRIPT	0	/* PostScript PPD */
#  define PPD_TYPE_PDF		1	/* PDF PPD */
#  define PPD_TYPE_RASTER	2	/* CUPS raster PPD */
#  define PPD_TYPE_FAX		3	/* Facsimile/MFD PPD */
#  define PPD_TYPE_UNKNOWN	4	/* Other/hybrid PPD */
#  define PPD_TYPE_DRV		5	/* Driver info file */
#  define PPD_TYPE_ARCHIVE	6	/* Archive file */


/*
 * Types and structures...
 */

typedef int (*cups_interpret_cb_t)(cups_page_header2_t *header, int preferred_bits);
					/**** cupsRasterInterpretPPD callback
					      function
					 *
					 * This function is called by
					 * @link cupsRasterInterpretPPD@ to
					 * validate (and update, as needed)
					 * the page header attributes. The
					 * "preferred_bits" argument provides
					 * the value of the
					 * @code cupsPreferredBitsPerColor@
					 * key from the PostScript page device
					 * dictionary and is 0 if undefined.
					 ****/

typedef enum ppd_ui_e			/**** UI Types ****/
{
  PPD_UI_BOOLEAN,			/* True or False option */
  PPD_UI_PICKONE,			/* Pick one from a list */
  PPD_UI_PICKMANY			/* Pick zero or more from a list */
} ppd_ui_t;

typedef enum ppd_section_e		/**** Order dependency sections ****/
{
  PPD_ORDER_ANY,			/* Option code can be anywhere in the
					   file */
  PPD_ORDER_DOCUMENT,			/* ... must be in the DocumentSetup
					   section */
  PPD_ORDER_EXIT,			/* ... must be sent prior to the
					   document */
  PPD_ORDER_JCL,			/* ... must be sent as a JCL command */
  PPD_ORDER_PAGE,			/* ... must be in the PageSetup
					   section */
  PPD_ORDER_PROLOG			/* ... must be in the Prolog section */
} ppd_section_t;

typedef enum ppd_cs_e			/**** Colorspaces ****/
{
  PPD_CS_CMYK = -4,			/* CMYK colorspace */
  PPD_CS_CMY,				/* CMY colorspace */
  PPD_CS_GRAY = 1,			/* Grayscale colorspace */
  PPD_CS_RGB = 3,			/* RGB colorspace */
  PPD_CS_RGBK,				/* RGBK (K = gray) colorspace */
  PPD_CS_N				/* DeviceN colorspace */
} ppd_cs_t;

typedef enum ppd_status_e		/**** Status Codes ****/
{
  PPD_OK = 0,				/* OK */
  PPD_FILE_OPEN_ERROR,			/* Unable to open PPD file */
  PPD_NULL_FILE,			/* NULL PPD file pointer */
  PPD_ALLOC_ERROR,			/* Memory allocation error */
  PPD_MISSING_PPDADOBE4,		/* Missing PPD-Adobe-4.x header */
  PPD_MISSING_VALUE,			/* Missing value string */
  PPD_INTERNAL_ERROR,			/* Internal error */
  PPD_BAD_OPEN_GROUP,			/* Bad OpenGroup */
  PPD_NESTED_OPEN_GROUP,		/* OpenGroup without a CloseGroup
					   first */
  PPD_BAD_OPEN_UI,			/* Bad OpenUI/JCLOpenUI */
  PPD_NESTED_OPEN_UI,			/* OpenUI/JCLOpenUI without a
					   CloseUI/JCLCloseUI first */
  PPD_BAD_ORDER_DEPENDENCY,		/* Bad OrderDependency */
  PPD_BAD_UI_CONSTRAINTS,		/* Bad UIConstraints */
  PPD_MISSING_ASTERISK,			/* Missing asterisk in column 0 */
  PPD_LINE_TOO_LONG,			/* Line longer than 255 chars */
  PPD_ILLEGAL_CHARACTER,		/* Illegal control character */
  PPD_ILLEGAL_MAIN_KEYWORD,		/* Illegal main keyword string */
  PPD_ILLEGAL_OPTION_KEYWORD,		/* Illegal option keyword string */
  PPD_ILLEGAL_TRANSLATION,		/* Illegal translation string */
  PPD_ILLEGAL_WHITESPACE,		/* Illegal whitespace character */
  PPD_BAD_CUSTOM_PARAM,			/* Bad custom parameter */
  PPD_MISSING_OPTION_KEYWORD,		/* Missing option keyword */
  PPD_BAD_VALUE,			/* Bad value string */
  PPD_MISSING_CLOSE_GROUP,		/* Missing CloseGroup */
  PPD_BAD_CLOSE_UI,			/* Bad CloseUI/JCLCloseUI */
  PPD_MISSING_CLOSE_UI,			/* Missing CloseUI/JCLCloseUI */
  PPD_MAX_STATUS			/* @private@ */
} ppd_status_t;

enum ppd_conform_e			/**** Conformance Levels ****/
{
  PPD_CONFORM_RELAXED,			/* Relax whitespace and control char */
  PPD_CONFORM_STRICT			/* Require strict conformance */
};

typedef enum ppd_conform_e ppd_conform_t;
					/**** Conformance Levels ****/

typedef struct ppd_attr_s		/**** PPD Attribute Structure ****/
{
  char		name[PPD_MAX_NAME];	/* Name of attribute (cupsXYZ) */
  char		spec[PPD_MAX_NAME];	/* Specifier string, if any */
  char		text[PPD_MAX_TEXT];	/* Human-readable text, if any */
  char		*value;			/* Value string */
} ppd_attr_t;

typedef struct ppd_option_s ppd_option_t;
					/**** Options ****/

typedef struct ppd_choice_s		/**** Option choices ****/
{
  char		marked;			/* 0 if not selected, 1 otherwise */
  char		choice[PPD_MAX_NAME];	/* Computer-readable option name */
  char		text[PPD_MAX_TEXT];	/* Human-readable option name */
  char		*code;			/* Code to send for this option */
  ppd_option_t	*option;		/* Pointer to parent option structure */
} ppd_choice_t;

struct ppd_option_s			/**** Options ****/
{
  char		conflicted;		/* 0 if no conflicts exist, 1
					   otherwise */
  char		keyword[PPD_MAX_NAME];	/* Option keyword name ("PageSize",
					   etc.) */
  char		defchoice[PPD_MAX_NAME];/* Default option choice */
  char		text[PPD_MAX_TEXT];	/* Human-readable text */
  ppd_ui_t	ui;			/* Type of UI option */
  ppd_section_t	section;		/* Section for command */
  float		order;			/* Order number */
  int		num_choices;		/* Number of option choices */
  ppd_choice_t	*choices;		/* Option choices */
};

typedef struct ppd_group_s		/**** Groups ****/
{
  /**** Group text strings are limited to 39 chars + nul in order to
   **** preserve binary compatibility and allow applications to get
   **** the group's keyword name.
   ****/
  char		text[PPD_MAX_TEXT - PPD_MAX_NAME];
  					/* Human-readable group name */
  char		name[PPD_MAX_NAME];	/* Group name @since CUPS 1.1.18/macOS
					   10.3@ */
  int		num_options;		/* Number of options */
  ppd_option_t	*options;		/* Options */
  int		num_subgroups;		/* Number of sub-groups */
  struct ppd_group_s *subgroups;	/* Sub-groups (max depth = 1) */
} ppd_group_t;

typedef struct ppd_const_s		/**** Constraints ****/
{
  char		option1[PPD_MAX_NAME];	/* First keyword */
  char		choice1[PPD_MAX_NAME];	/* First option/choice (blank for all) */
  char		option2[PPD_MAX_NAME];	/* Second keyword */
  char		choice2[PPD_MAX_NAME];	/* Second option/choice (blank for all) */
} ppd_const_t;

typedef struct ppd_size_s		/**** Page Sizes ****/
{
  int		marked;			/* Page size selected? */
  char		name[PPD_MAX_NAME];	/* Media size option */
  float		width;			/* Width of media in points */
  float		length;			/* Length of media in points */
  float		left;			/* Left printable margin in points */
  float		bottom;			/* Bottom printable margin in points */
  float		right;			/* Right printable margin in points */
  float		top;			/* Top printable margin in points */
} ppd_size_t;

typedef struct ppd_emul_s		/**** Emulators ****/
{
  char		name[PPD_MAX_NAME];	/* Emulator name */
  char		*start;			/* Code to switch to this emulation */
  char		*stop;			/* Code to stop this emulation */
} ppd_emul_t;

typedef struct ppd_profile_s		/**** sRGB Color Profiles ****/
{
  char		resolution[PPD_MAX_NAME];
  					/* Resolution or "-" */
  char		media_type[PPD_MAX_NAME];
					/* Media type or "-" */
  float		density;		/* Ink density to use */
  float		gamma;			/* Gamma correction to use */
  float		matrix[3][3];		/* Transform matrix */
} ppd_profile_t;

/**** New in CUPS 1.2/macOS 10.5 ****/
typedef enum ppd_cptype_e		/**** Custom Parameter Type ****/
{
  PPD_CUSTOM_UNKNOWN = -1,		/* Unknown type (error) */
  PPD_CUSTOM_CURVE,			/* Curve value for f(x) = x^value */
  PPD_CUSTOM_INT,			/* Integer number value */
  PPD_CUSTOM_INVCURVE,			/* Curve value for f(x) = x^(1/value) */
  PPD_CUSTOM_PASSCODE,			/* String of (hidden) numbers */
  PPD_CUSTOM_PASSWORD,			/* String of (hidden) characters */
  PPD_CUSTOM_POINTS,			/* Measurement value in points */
  PPD_CUSTOM_REAL,			/* Real number value */
  PPD_CUSTOM_STRING			/* String of characters */
} ppd_cptype_t;

typedef union ppd_cplimit_u		/**** Custom Parameter Limit ****/
{
  float		custom_curve;		/* Gamma value */
  int		custom_int;		/* Integer value */
  float		custom_invcurve;	/* Gamma value */
  int		custom_passcode;	/* Passcode length */
  int		custom_password;	/* Password length */
  float		custom_points;		/* Measurement value */
  float		custom_real;		/* Real value */
  int		custom_string;		/* String length */
} ppd_cplimit_t;

typedef union ppd_cpvalue_u		/**** Custom Parameter Value ****/
{
  float		custom_curve;		/* Gamma value */
  int		custom_int;		/* Integer value */
  float		custom_invcurve;	/* Gamma value */
  char		*custom_passcode;	/* Passcode value */
  char		*custom_password;	/* Password value */
  float		custom_points;		/* Measurement value */
  float		custom_real;		/* Real value */
  char		*custom_string;		/* String value */
} ppd_cpvalue_t;

typedef struct ppd_cparam_s		/**** Custom Parameter ****/
{
  char		name[PPD_MAX_NAME];	/* Parameter name */
  char		text[PPD_MAX_TEXT];	/* Human-readable text */
  int		order;			/* Order (0 to N) */
  ppd_cptype_t	type;			/* Parameter type */
  ppd_cplimit_t	minimum,		/* Minimum value */
		maximum;		/* Maximum value */
  ppd_cpvalue_t	current;		/* Current value */
} ppd_cparam_t;

typedef struct ppd_coption_s		/**** Custom Option ****/
{
  char		keyword[PPD_MAX_NAME];	/* Name of option that is being
					   extended... */
  ppd_option_t	*option;		/* Option that is being extended... */
  int		marked;			/* Extended option is marked */
  cups_array_t	*params;		/* Parameters */
} ppd_coption_t;

typedef struct ppd_globals_s		/**** CUPS PPD global state data ****/
{
  /* ppd.c */
  ppd_status_t		ppd_status;	/* Status of last ppdOpen*() */
  int			ppd_line;	/* Current line number */
  ppd_conform_t		ppd_conform;	/* Level of conformance required */

  /* ppd-util.c */
  char			ppd_filename[HTTP_MAX_URI];
					/* PPD filename */
} ppd_globals_t;

typedef enum ppd_localization_e	/**** Selector for ppdOpenWithLocalization ****/
{
  PPD_LOCALIZATION_DEFAULT,		/* Load only the default localization */
  PPD_LOCALIZATION_ICC_PROFILES,	/* Load only the color profile
					   localization */
  PPD_LOCALIZATION_NONE,		/* Load no localizations */
  PPD_LOCALIZATION_ALL			/* Load all localizations */
} ppd_localization_t;

typedef enum ppd_parse_e		/**** Selector for ppdParseOptions ****/
{
  PPD_PARSE_OPTIONS,			/* Parse only the options */
  PPD_PARSE_PROPERTIES,			/* Parse only the properties */
  PPD_PARSE_ALL				/* Parse everything */
} ppd_parse_t;

typedef struct ppd_cups_uiconst_s	/**** Constraint from cupsUIConstraints ****/
{
  ppd_option_t	*option;		/* Constrained option */
  ppd_choice_t	*choice;		/* Constrained choice or @code NULL@ */
  int		installable;		/* Installable option? */
} ppd_cups_uiconst_t;

typedef struct ppd_cups_uiconsts_s	/**** cupsUIConstraints ****/
{
  char		resolver[PPD_MAX_NAME];	/* Resolver name */
  int		installable,		/* Constrained against any installable
					   options? */
		num_constraints;	/* Number of constraints */
  ppd_cups_uiconst_t *constraints;	/* Constraints */
} ppd_cups_uiconsts_t;

typedef enum ppd_pwg_print_color_mode_e	/**** PWG print-color-mode indices ****/
{
  PPD_PWG_PRINT_COLOR_MODE_MONOCHROME = 0,	/* print-color-mode=monochrome*/
  PPD_PWG_PRINT_COLOR_MODE_COLOR,		/* print-color-mode=color */
  /* Other values are not supported by CUPS yet. */
  PPD_PWG_PRINT_COLOR_MODE_MAX
} ppd_pwg_print_color_mode_t;

typedef enum ppd_pwg_print_quality_e	/**** PWG print-quality values ****/
{
  PPD_PWG_PRINT_QUALITY_DRAFT = 0,	/* print-quality=3 */
  PPD_PWG_PRINT_QUALITY_NORMAL,		/* print-quality=4 */
  PPD_PWG_PRINT_QUALITY_HIGH,		/* print-quality=5 */
  PPD_PWG_PRINT_QUALITY_MAX
} ppd_pwg_print_quality_t;

typedef struct ppd_pwg_finishings_s	/**** PWG finishings mapping data ****/
{
  ipp_finishings_t	value;		/* finishings value */
  int			num_options;	/* Number of options to apply */
  cups_option_t		*options;	/* Options to apply */
} ppd_pwg_finishings_t;

struct ppd_cache_s		   /**** PPD cache and PWG conversion data ****/
{
  int		num_bins;		/* Number of output bins */
  pwg_map_t	*bins;			/* Output bins */
  int		num_sizes;		/* Number of media sizes */
  pwg_size_t	*sizes;			/* Media sizes */
  int		custom_max_width,	/* Maximum custom width in 2540ths */
		custom_max_length,	/* Maximum custom length in 2540ths */
		custom_min_width,	/* Minimum custom width in 2540ths */
		custom_min_length;	/* Minimum custom length in 2540ths */
  char		*custom_max_keyword,	/* Maximum custom size PWG keyword */
		*custom_min_keyword,	/* Minimum custom size PWG keyword */
		custom_ppd_size[41];	/* Custom PPD size name */
  pwg_size_t	custom_size;		/* Custom size record */
  char		*source_option;		/* PPD option for media source */
  int		num_sources;		/* Number of media sources */
  pwg_map_t	*sources;		/* Media sources */
  int		num_types;		/* Number of media types */
  pwg_map_t	*types;			/* Media types */
  int		num_presets[PPD_PWG_PRINT_COLOR_MODE_MAX][PPD_PWG_PRINT_QUALITY_MAX];
					/* Number of print-color-mode/print-quality options */
  cups_option_t	*presets[PPD_PWG_PRINT_COLOR_MODE_MAX][PPD_PWG_PRINT_QUALITY_MAX];
					/* print-color-mode/print-quality options */
  char		*sides_option,		/* PPD option for sides */
		*sides_1sided,		/* Choice for one-sided */
		*sides_2sided_long,	/* Choice for two-sided-long-edge */
		*sides_2sided_short;	/* Choice for two-sided-short-edge */
  char		*product;		/* Product value */
  cups_array_t	*filters,		/* cupsFilter/cupsFilter2 values */
		*prefilters;		/* cupsPreFilter values */
  int		single_file;		/* cupsSingleFile value */
  cups_array_t	*finishings;		/* cupsIPPFinishings values */
  cups_array_t	*templates;		/* cupsFinishingTemplate values */
  int		max_copies,		/* cupsMaxCopies value */
		account_id,		/* cupsJobAccountId value */
		accounting_user_id;	/* cupsJobAccountingUserId value */
  char		*password;		/* cupsJobPassword value */
  cups_array_t	*mandatory;		/* cupsMandatory value */
  char		*charge_info_uri;	/* cupsChargeInfoURI value */
  cups_array_t	*strings;		/* Localization strings */
  cups_array_t	*support_files;		/* Support files - ICC profiles, etc. */
};
typedef struct ppd_cache_s ppd_cache_t;
					/**** PPD cache and mapping data ****/

typedef struct ppd_file_s		/**** PPD File ****/
{
  int		language_level;		/* Language level of device */
  int		color_device;		/* 1 = color device, 0 = grayscale */
  int		variable_sizes;		/* 1 = supports variable sizes,
					   0 = doesn't */
  int		accurate_screens;	/* 1 = supports accurate screens,
					   0 = not */
  int		contone_only;		/* 1 = continuous tone only, 0 = not */
  int		landscape;		/* -90 or 90 */
  int		model_number;		/* Device-specific model number */
  int		manual_copies;		/* 1 = Copies done manually,
					   0 = hardware */
  int		throughput;		/* Pages per minute */
  ppd_cs_t	colorspace;		/* Default colorspace */
  char		*patches;		/* Patch commands to be sent to
					   printer */
  int		num_emulations;		/* Number of emulations supported
					   (no longer supported) @private@ */
  ppd_emul_t	*emulations;		/* Emulations and the code to invoke
					   them (no longer supported)
					   @private@ */
  char		*jcl_begin;		/* Start JCL commands */
  char		*jcl_ps;		/* Enter PostScript interpreter */
  char		*jcl_end;		/* End JCL commands */
  char		*lang_encoding;		/* Language encoding */
  char		*lang_version;		/* Language version (English, Spanish,
					   etc.) */
  char		*modelname;		/* Model name (general) */
  char		*ttrasterizer;		/* Truetype rasterizer */
  char		*manufacturer;		/* Manufacturer name */
  char		*product;		/* Product name (from PS
					   RIP/interpreter) */
  char		*nickname;		/* Nickname (specific) */
  char		*shortnickname;		/* Short version of nickname */
  int		num_groups;		/* Number of UI groups */
  ppd_group_t	*groups;		/* UI groups */
  int		num_sizes;		/* Number of page sizes */
  ppd_size_t	*sizes;			/* Page sizes */
  float		custom_min[2];		/* Minimum variable page size */
  float		custom_max[2];		/* Maximum variable page size */
  float		custom_margins[4];	/* Margins around page */
  int		num_consts;		/* Number of UI/Non-UI constraints */
  ppd_const_t	*consts;		/* UI/Non-UI constraints */
  int		num_fonts;		/* Number of pre-loaded fonts */
  char		**fonts;		/* Pre-loaded fonts */
  int		num_profiles;		/* Number of sRGB color profiles */
  ppd_profile_t	*profiles;		/* sRGB color profiles */
  int		num_filters;		/* Number of filters */
  char		**filters;		/* Filter strings... */

  /**** New in CUPS 1.1 ****/
  int		flip_duplex;		/* 1 = Flip page for back sides */

  /**** New in CUPS 1.1.19 ****/
  char		*protocols;		/* Protocols (BCP, TBCP) string
					   @since CUPS 1.1.19/macOS 10.3@ */
  char		*pcfilename;		/* PCFileName string @since
					   CUPS 1.1.19/macOS 10.3@ */
  int		num_attrs;		/* Number of attributes @since
					   CUPS 1.1.19/macOS 10.3@ @private@ */
  int		cur_attr;		/* Current attribute @since
					   CUPS 1.1.19/macOS 10.3@ @private@ */
  ppd_attr_t	**attrs;		/* Attributes @since CUPS 1.1.19/macOS
					   10.3@ @private@ */

  /**** New in CUPS 1.2/macOS 10.5 ****/
  cups_array_t	*sorted_attrs;		/* Attribute lookup array @since
					   CUPS 1.2/macOS 10.5@ @private@ */
  cups_array_t	*options;		/* Option lookup array @since
					   CUPS 1.2/macOS 10.5@ @private@ */
  cups_array_t	*coptions;		/* Custom options array @since
					   CUPS 1.2/macOS 10.5@ @private@ */

  /**** New in CUPS 1.3/macOS 10.5 ****/
  cups_array_t	*marked;		/* Marked choices
					   @since CUPS 1.3/macOS 10.5@
					   @private@ */

  /**** New in CUPS 1.4/macOS 10.6 ****/
  cups_array_t	*cups_uiconstraints;	/* cupsUIConstraints @since
					   CUPS 1.4/macOS 10.6@ @private@ */

  /**** New in CUPS 1.5 ****/
  ppd_cache_t	*cache;			/* PPD cache and mapping data @since
					   CUPS 1.5/macOS 10.7@ @private@ */
} ppd_file_t;

/**** New in cups-filters 2.0.0: Ovetaken from cups-driverd ****/
typedef struct				/**** PPD record ****/
{
  time_t	mtime;			/* Modification time */
  off_t		size;			/* Size in bytes */
  int		model_number;		/* cupsModelNumber */
  int		type;			/* ppd-type */
  char		filename[512],		/* Filename */
		name[256],		/* PPD name */
		languages[PPD_MAX_LANG][6],
					/* LanguageVersion/cupsLanguages */
		products[PPD_MAX_PROD][128],
					/* Product strings */
		psversions[PPD_MAX_VERS][32],
					/* PSVersion strings */
		make[128],		/* Manufacturer */
		make_and_model[128],	/* NickName/ModelName */
		device_id[256],		/* IEEE 1284 Device ID */
		scheme[128];		/* PPD scheme */
} ppd_rec_t;

typedef struct				/**** In-memory record ****/
{
  int		found;			/* 1 if PPD is found */
  int		matches;		/* Match count */
  ppd_rec_t	record;			/* PPDs.dat record */
} ppd_info_t;

typedef struct
{
  char		*name;			/* Name for PPD collection */
  char		*path;			/* Directory where PPD collection is
					   located */
} ppd_collection_t;



/*
 * Prototypes...
 */

/* cupsMarkOptions replaced by ppdMarkOptions in libppd */
/* extern int		cupsMarkOptions(ppd_file_t *ppd, int num_options,
                                        cups_option_t *options); */
/* Definition for backward compatibility, will be removed soon */
#define cupsMarkOptions cupsMarkOptions_USE_ppdMarkOptions_INSTEAD

extern void		ppdClose(ppd_file_t *ppd);
extern int		ppdCollect(ppd_file_t *ppd, ppd_section_t section,
			           ppd_choice_t  ***choices);
extern int		ppdConflicts(ppd_file_t *ppd);
extern int		ppdEmit(ppd_file_t *ppd, FILE *fp,
			        ppd_section_t section);
extern int		ppdEmitFd(ppd_file_t *ppd, int fd,
			          ppd_section_t section);
extern int		ppdEmitJCL(ppd_file_t *ppd, FILE *fp, int job_id,
			           const char *user, const char *title);
extern ppd_choice_t	*ppdFindChoice(ppd_option_t *o, const char *option);
extern ppd_choice_t	*ppdFindMarkedChoice(ppd_file_t *ppd,
			                     const char *keyword);
extern ppd_option_t	*ppdFindOption(ppd_file_t *ppd, const char *keyword);
extern int		ppdIsMarked(ppd_file_t *ppd, const char *keyword,
			            const char *option);
extern void		ppdMarkDefaults(ppd_file_t *ppd);
extern int		ppdMarkOption(ppd_file_t *ppd, const char *keyword,
			              const char *option);
extern ppd_file_t	*ppdOpen(FILE *fp);
extern ppd_file_t	*ppdOpenFd(int fd);
extern ppd_file_t	*ppdOpenFile(const char *filename);
extern float		ppdPageLength(ppd_file_t *ppd, const char *name);
extern ppd_size_t	*ppdPageSize(ppd_file_t *ppd, const char *name);
extern float		ppdPageWidth(ppd_file_t *ppd, const char *name);

/**** New in CUPS 1.1.19 ****/
extern const char	*ppdErrorString(ppd_status_t status);
extern ppd_attr_t	*ppdFindAttr(ppd_file_t *ppd, const char *name,
			             const char *spec);
extern ppd_attr_t	*ppdFindNextAttr(ppd_file_t *ppd, const char *name,
			                 const char *spec);
extern ppd_status_t	ppdLastError(int *line);

/**** New in CUPS 1.1.20 ****/
extern void		ppdSetConformance(ppd_conform_t c);

/**** New in CUPS 1.2 ****/
/* cupsRasterInterpretPPD replaced by ppdRasterInterpretPPD in libppd */
/* extern int		cupsRasterInterpretPPD(cups_page_header2_t *h,
			                       ppd_file_t *ppd,
					       int num_options,
					       cups_option_t *options,
					       cups_interpret_cb_t func); */
/* Definition for backward compatibility, will be removed soon */
#define cupsRasterInterpretPPD cupsRasterInterpretPPD_USE_ppdRasterInterpretPPD_INSTEAD

extern int		ppdCollect2(ppd_file_t *ppd, ppd_section_t section,
			            float min_order, ppd_choice_t  ***choices);
extern int		ppdEmitAfterOrder(ppd_file_t *ppd, FILE *fp,
			                  ppd_section_t section, int limit,
					  float min_order);
extern int		ppdEmitJCLEnd(ppd_file_t *ppd, FILE *fp);
extern char		*ppdEmitString(ppd_file_t *ppd, ppd_section_t section,
			               float min_order);
extern ppd_coption_t	*ppdFindCustomOption(ppd_file_t *ppd,
			                     const char *keyword);
extern ppd_cparam_t	*ppdFindCustomParam(ppd_coption_t *opt,
			                    const char *name);
extern ppd_cparam_t	*ppdFirstCustomParam(ppd_coption_t *opt);
extern ppd_option_t	*ppdFirstOption(ppd_file_t *ppd);
extern ppd_cparam_t	*ppdNextCustomParam(ppd_coption_t *opt);
extern ppd_option_t	*ppdNextOption(ppd_file_t *ppd);
extern int		ppdLocalize(ppd_file_t *ppd);
extern ppd_file_t	*ppdOpen2(cups_file_t *fp);

/**** New in CUPS 1.3/macOS 10.5 ****/
extern const char	*ppdLocalizeIPPReason(ppd_file_t *ppd,
			                      const char *reason,
					      const char *scheme,
					      char *buffer,
					      size_t bufsize);

/**** New in CUPS 1.4/macOS 10.6 ****/
/* cupsGetConflicts replaced by ppdGetConflicts in libppd */
/* extern int		cupsGetConflicts(ppd_file_t *ppd, const char *option,
					 const char *choice,
					 cups_option_t **options); */
/* Definition for backward compatibility, will be removed soon */
#define cupsGetConflicts cupsGetConflicts_USE_ppdGetConflicts_INSTEAD

/* cupsResolveConflicts replaced by ppdResolveConflicts in libppd */
/* extern int		cupsResolveConflicts(ppd_file_t *ppd,
			                     const char *option,
			                     const char *choice,
					     int *num_options,
					     cups_option_t **options); */
/* Definition for backward compatibility, will be removed soon */
#define cupsResolveConflicts cupsResolveConflicts_USE_ppdResolveConflicts_INSTEAD

extern int		ppdInstallableConflict(ppd_file_t *ppd,
			                       const char *option,
					       const char *choice);
extern ppd_attr_t	*ppdLocalizeAttr(ppd_file_t *ppd, const char *keyword,
			                 const char *spec);
extern const char	*ppdLocalizeMarkerName(ppd_file_t *ppd,
			                       const char *name);
extern int		ppdPageSizeLimits(ppd_file_t *ppd,
			                  ppd_size_t *minimum,
					  ppd_size_t *maximum);

/**** New in cups-filters 2.0.0: Renamed functions from original CUPS API ****/
extern int		ppdMarkOptions(ppd_file_t *ppd,
				       int num_options,
				       cups_option_t *options);
extern int		ppdRasterInterpretPPD(cups_page_header2_t *h,
					      ppd_file_t *ppd,
					      int num_options,
					      cups_option_t *options,
					      cups_interpret_cb_t func);
extern int		ppdGetConflicts(ppd_file_t *ppd, const char *option,
					const char *choice,
					cups_option_t **options);
extern int		ppdResolveConflicts(ppd_file_t *ppd,
					    const char *option,
					    const char *choice,
					    int *num_options,
					    cups_option_t **options);

/**** New in cups-filters 2.0.0: Formerly CUPS-private functions ****/
extern int		ppdConvertOptions(ipp_t *request,
					  ppd_file_t *ppd,
					  ppd_cache_t *pc,
					  ipp_attribute_t *media_col_sup,
					  ipp_attribute_t *doc_handling_sup,
					  ipp_attribute_t *print_color_mode_sup,
					  const char *user,
					  const char *format,
					  int copies,
					  int num_options,
					  cups_option_t *options);
extern int		ppdRasterExecPS(cups_page_header2_t *h,
					int *preferred_bits,
					const char *code);
extern int		ppdRasterInterpretPPD(cups_page_header2_t *h,
					      ppd_file_t *ppd,
					      int num_options,
					      cups_option_t *options,
					      cups_interpret_cb_t func);
extern ppd_cache_t	*ppdCacheCreateWithFile(const char *filename,
						ipp_t **attrs);
extern ppd_cache_t	*ppdCacheCreateWithPPD(ppd_file_t *ppd);
extern void		ppdCacheDestroy(ppd_cache_t *pc);
extern const char	*ppdCacheGetBin(ppd_cache_t *pc,
					const char *output_bin);
extern int		ppdCacheGetFinishingOptions(ppd_cache_t *pc,
						    ipp_t *job,
						    ipp_finishings_t value,
						    int num_options,
						    cups_option_t **options);
extern int		ppdCacheGetFinishingValues(ppd_file_t *ppd,
						   ppd_cache_t *pc,
						   int max_values,
						   int *values);
extern const char	*ppdCacheGetInputSlot(ppd_cache_t *pc, ipp_t *job,
					      const char *keyword);
extern const char	*ppdCacheGetMediaType(ppd_cache_t *pc, ipp_t *job,
					      const char *keyword);
extern const char	*ppdCacheGetOutputBin(ppd_cache_t *pc,
					      const char *keyword);
extern const char	*ppdCacheGetPageSize(ppd_cache_t *pc, ipp_t *job,
					     const char *keyword, int *exact);
extern pwg_size_t	*ppdCacheGetSize(ppd_cache_t *pc,
					 const char *page_size);
extern const char	*ppdCacheGetSource(ppd_cache_t *pc,
					   const char *input_slot);
extern const char	*ppdCacheGetType(ppd_cache_t *pc,
					 const char *media_type);
extern int		ppdCacheWriteFile(ppd_cache_t *pc,
					  const char *filename, ipp_t *attrs);
extern void		ppdFreeLanguages(cups_array_t *languages);
extern cups_encoding_t	ppdGetEncoding(const char *name);
extern cups_array_t	*ppdGetLanguages(ppd_file_t *ppd);
extern ppd_globals_t	*ppdGlobals(void);
extern unsigned		ppdHashName(const char *name);
extern ppd_attr_t	*ppdLocalizedAttr(ppd_file_t *ppd, const char *keyword,
					  const char *spec, const char *ll_CC);
extern char		*ppdNormalizeMakeAndModel(const char *make_and_model,
						  char *buffer,
						  size_t bufsize);
extern ppd_file_t	*ppdOpenWithLocalization(cups_file_t *fp,
				  ppd_localization_t localization);
extern ppd_file_t	*ppdOpenFileWithLocalization(const char *filename,
				      ppd_localization_t localization);
extern int		ppdParseOptions(const char *s, int num_options,
					cups_option_t **options,
					ppd_parse_t which);
extern const char	*ppdPwgInputSlotForSource(const char *media_source,
						  char *name, size_t namesize);
extern const char	*ppdPwgMediaTypeForType(const char *media_type,
						char *name, size_t namesize);
extern const char	*ppdPwgPageSizeForMedia(pwg_media_t *media,
						char *name, size_t namesize);
extern void		ppdPwgPpdizeName(const char *ipp, char *name,
					 size_t namesize);
extern void		ppdPwgPpdizeResolution(ipp_attribute_t *attr,
					       int element, int *xres,
					       int *yres, char *name,
					       size_t namesize);
extern void		ppdPwgUnppdizeName(const char *ppd, char *name,
					   size_t namesize,
					   const char *dashchars);

/**** New in cups-filters 2.0.0: Overtaken from ippeveprinter ****/
extern ipp_t		*ppdLoadAttributes(ppd_file_t   *ppd,
					   cups_array_t *docformats);

/**** New in cups-filters 2.0.0: Overtaken from ippeveps ****/
extern int		ppdGetOptions(cups_option_t **options,
				      ipp_t *printer_attrs,
				      ipp_t *job_attrs,
				      ppd_file_t *ppd);

/**** New in cups-filters 2.0.0: Added for pclmtoraster filter ****/
extern int		ppdRasterMatchPPDSize(cups_page_header2_t *header,
					      ppd_file_t *ppd,
					      double margins[4],
					      double dimensions[4],
					      int *image_fit,
					      int *landscape);

/**** New in cups-filters 2.0.0: Ovetaken from cups-driverd ****/
extern cups_array_t	*ppdCollectionListPPDs(cups_array_t *ppd_collections,
					       int limit,
					       int num_options,
					       cups_option_t *options,
					       filter_logfunc_t log,
					       void *ld);
extern cups_file_t	*ppdCollectionGetPPD(const char *name,
					     cups_array_t *ppd_collections,
					     filter_logfunc_t log,
					     void *ld);
extern int		ppdCollectionDumpCache(const char *filename,
					       filter_logfunc_t log,
					       void *ld);


/*
 * C++ magic...
 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_PPD_PPD_H_ */

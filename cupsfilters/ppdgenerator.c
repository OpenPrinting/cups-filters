/*
 *   PWG Raster/Apple Raster/PCLm/PDF/IPP legacy PPD generator
 *
 *   Copyright 2016-2019 by Till Kamppeter.
 *   Copyright 2017-2019 by Sahil Arora.
 *   Copyright 2018-2019 by Deepak Patankar.
 *
 *   The PPD generator is based on the PPD generator for the CUPS
 *   "lpadmin -m everywhere" functionality in the cups/ppd-cache.c
 *   file. The copyright of this file is:
 *
 *   Copyright 2010-2016 by Apple Inc.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file. 
 */

#include <config.h>
#include <limits.h>
#include <cups/cups.h>
#include <cups/dir.h>
#include <ppd/ppd.h>
#include <cupsfilters/ppdgenerator.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif


/*
 * Include necessary headers.
 */

#include <errno.h>
#include "driver.h"
#include <string.h>
#include <ctype.h>
#ifdef HAVE_CUPS_1_7
#include <cups/pwg.h>
#endif /* HAVE_CUPS_1_7 */


/*
 * Macros to work around typos in older libcups version
 */

#if (CUPS_VERSION_MAJOR < 2) || ((CUPS_VERSION_MAJOR == 2) && ((CUPS_VERSION_MINOR < 3) || ((CUPS_VERSION_MINOR == 3) && (CUPS_VERSION_PATCH < 1))))
#define IPP_FINISHINGS_CUPS_FOLD_ACCORDION IPP_FINISHINGS_CUPS_FOLD_ACCORDIAN
#define IPP_FINISHINGS_FOLD_ACCORDION IPP_FINISHINGS_FOLD_ACCORDIAN
#endif


#ifdef HAVE_CUPS_1_6
/*
 * Local types...
 */

/* Data structure for IPP choice name and human-readable string */
typedef struct ipp_choice_strings_s {
  char *name, *human_readable;
} ipp_choice_strings_t;

/* Data structure for IPP option name, human-readable string, and choice list */
typedef struct ipp_opt_strings_s {
  char *name, *human_readable;
  cups_array_t *choices;
} ipp_opt_strings_t;

/* The following code uses a lot of CUPS >= 1.6 specific stuff.
   It needed for create_local_queue() in cups-browsed
   to set up local queues for non-CUPS printer broadcasts
   that is disabled in create_local_queue() for older CUPS <= 1.5.4.
   Accordingly the following code is also disabled here for CUPS < 1.6. */

/*
 * The code below is borrowed from the CUPS 2.2.x upstream repository
 * (via patches attached to https://www.cups.org/str.php?L4258). This
 * allows for automatic PPD generation already with CUPS versions older
 * than CUPS 2.2.x. We have also an additional test and development
 * platform for this code. Taken from cups/ppd-cache.c,
 * cups/string-private.h, cups/string.c.
 * 
 * The advantage of PPD generation instead of working with System V
 * interface scripts is that the print dialogs of the clients do not
 * need to ask the printer for its options via IPP. So we have access
 * to the options with the current PPD-based dialogs and can even share
 * the automatically created print queue to other CUPS-based machines
 * without problems.
 */


cups_array_t *opt_strings_catalog = NULL;
char ppdgenerator_msg[1024];

typedef struct _pwg_finishings_s	/**** PWG finishings mapping data ****/
{
  ipp_finishings_t	value;		/* finishings value */
  int			num_options;	/* Number of options to apply */
  cups_option_t		*options;	/* Options to apply */
} _pwg_finishings_t;

#define _PWG_EQUIVALENT(x, y)	(abs((x)-(y)) < 2)

#ifndef HAVE_STRLCPY
/*
 * 'strlcpy()' - Safely copy two strings.
 */

size_t					/* O - Length of string */
strlcpy(char       *dst,		/* O - Destination string */
	const char *src,		/* I - Source string */
	size_t      size)		/* I - Size of destination string buffer */
{
  size_t	srclen;			/* Length of source string */


 /*
  * Figure out how much room is needed...
  */

  size --;

  srclen = strlen(src);

 /*
  * Copy the appropriate amount...
  */

  if (srclen > size)
    srclen = size;

  memmove(dst, src, srclen);
  dst[srclen] = '\0';

  return (srclen);
}
#endif /* !HAVE_STRLCPY */

/*
 * 'str_formatd()' - Format a floating-point number.
 */

char *					/* O - Pointer to end of string */
str_formatd(char         *buf,	/* I - String */
                char         *bufend,	/* I - End of string buffer */
		double       number,	/* I - Number to format */
                struct lconv *loc)	/* I - Locale data */
{
  char		*bufptr,		/* Pointer into buffer */
		temp[1024],		/* Temporary string */
		*tempdec,		/* Pointer to decimal point */
		*tempptr;		/* Pointer into temporary string */
  const char	*dec;			/* Decimal point */
  int		declen;			/* Length of decimal point */


 /*
  * Format the number using the "%.12f" format and then eliminate
  * unnecessary trailing 0's.
  */

  snprintf(temp, sizeof(temp), "%.12f", number);
  for (tempptr = temp + strlen(temp) - 1;
       tempptr > temp && *tempptr == '0';
       *tempptr-- = '\0');

 /*
  * Next, find the decimal point...
  */

  if (loc && loc->decimal_point) {
    dec    = loc->decimal_point;
    declen = (int)strlen(dec);
  } else {
    dec    = ".";
    declen = 1;
  }

  if (declen == 1)
    tempdec = strchr(temp, *dec);
  else
    tempdec = strstr(temp, dec);

 /*
  * Copy everything up to the decimal point...
  */

  if (tempdec) {
    for (tempptr = temp, bufptr = buf;
         tempptr < tempdec && bufptr < bufend;
	 *bufptr++ = *tempptr++);

    tempptr += declen;

    if (*tempptr && bufptr < bufend) {
      *bufptr++ = '.';

      while (*tempptr && bufptr < bufend)
        *bufptr++ = *tempptr++;
    }

    *bufptr = '\0';
  } else {
    strlcpy(buf, temp, (size_t)(bufend - buf + 1));
    bufptr = buf + strlen(buf);
  }

  return (bufptr);
}


/*
 * 'pwg_compare_sizes()' - Compare two media sizes...
 */

static int				/* O - Result of comparison */
pwg_compare_sizes(cups_size_t *a,	/* I - First media size */
                  cups_size_t *b)	/* I - Second media size */
{
  return (strcmp(a->media, b->media));
}


/*
 * 'pwg_copy_size()' - Copy a media size.
 */

static cups_size_t *			/* O - New media size */
pwg_copy_size(cups_size_t *size)	/* I - Media size to copy */
{
  cups_size_t	*newsize = (cups_size_t *)calloc(1, sizeof(cups_size_t));
					/* New media size */

  if (newsize)
    memcpy(newsize, size, sizeof(cups_size_t));

  return (newsize);
}

static int				/* O  - 1 on success, 0 on failure */
get_url(const char *url,		/* I  - URL to get */
	char       *name,		/* I  - Temporary filename */
	size_t     namesize)		/* I  - Size of temporary filename
					        buffer */
{
  http_t		*http = NULL;
  char			scheme[32],	/* URL scheme */
			userpass[256],	/* URL username:password */
			host[256],	/* URL host */
			resource[256];	/* URL resource */
  int			port;		/* URL port */
  http_encryption_t	encryption;	/* Type of encryption to use */
  http_status_t		status;		/* Status of GET request */
  int			fd;		/* Temporary file */


  if (httpSeparateURI(HTTP_URI_CODING_ALL, url, scheme, sizeof(scheme),
		      userpass, sizeof(userpass), host, sizeof(host), &port,
		      resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
    return (0);

  if (port == 443 || !strcmp(scheme, "https"))
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  http = httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 5000, NULL);

  if (!http)
    return (0);

  if ((fd = cupsTempFd(name, (int)namesize)) < 0)
    return (0);

  status = cupsGetFd(http, resource, fd);

  close(fd);
  httpClose(http);

  if (status != HTTP_STATUS_OK) {
    unlink(name);
    *name = '\0';
    return (0);
  }

  return (1);
}



/*
 * 'find_cups_message_catalog()' - Find a CUPS message catalog file
 *                                 containing human-readable standard
 *                                 option and choice names for IPP
 *                                 printers
 */

static const char *
search_dir_for_catalog(const char *dirname)
{
  const char *catalog = NULL, *c1, *c2;
  cups_dir_t *dir = NULL, *subdir;
  cups_dentry_t *subdirentry, *catalogentry;
  char subdirpath[1024], catalogpath[2048], lang[8];
  int i;

  if (dirname == NULL)
    return NULL;

  /* Check first whether we have an English file and prefer this */
  snprintf(catalogpath, sizeof(catalogpath), "%s/en/cups_en.po", dirname);
  if (access(catalogpath, R_OK) == 0) {
    /* Found */
    catalog = strdup(catalogpath);
    return catalog;
  }

  if ((dir = cupsDirOpen(dirname)) == NULL)
    return NULL;

  while ((subdirentry = cupsDirRead(dir)) != NULL) {
    /* Do we actually have a subdir? */
    if (!S_ISDIR(subdirentry->fileinfo.st_mode))
      continue;
    /* Check format of subdir name */
    c1 = subdirentry->filename;
    if (c1[0] < 'a' || c1[0] > 'z' || c1[1] < 'a' || c1[1] > 'z')
      continue;
    if (c1[2] >= 'a' && c1[2] <= 'z')
      i = 3;
    else
      i = 2;
    if (c1[i] == '_') {
      i ++;
      if (c1[i] < 'A' || c1[i] > 'Z' || c1[i+1] < 'A' || c1[i+1] > 'Z')
	continue;
      i += 2;
      if (c1[i] >= 'A' && c1[i] <= 'Z')
	i ++;
    }
    if (c1[i] != '\0' && c1[i] != '@')
      continue;
    strncpy(lang, c1, i);
    lang[i] = '\0';
    snprintf(subdirpath, sizeof(subdirpath), "%s/%s", dirname, c1);
    if ((subdir = cupsDirOpen(subdirpath)) != NULL) {
      while ((catalogentry = cupsDirRead(subdir)) != NULL) {
	/* Do we actually have a regular file? */
	if (!S_ISREG(catalogentry->fileinfo.st_mode))
	  continue;
	/* Check format of catalog name */
	c2 = catalogentry->filename;
	if (strlen(c2) < 10 || strncmp(c2, "cups_", 5) != 0 ||
	    strncmp(c2 + 5, lang, i) != 0 ||
	    strcmp(c2 + strlen(c2) - 3, ".po"))
	  continue;
	/* Is catalog readable ? */
	snprintf(catalogpath, sizeof(catalogpath), "%s/%s", subdirpath, c2);
	if (access(catalogpath, R_OK) != 0)
	  continue;
	/* Found */
	catalog = strdup(catalogpath);
	break;
      }
      cupsDirClose(subdir);
      subdir = NULL;
      if (catalog != NULL)
	break;
    }
  }

  cupsDirClose(dir);
  return catalog;
}

static const char *
find_cups_message_catalog(const char *preferreddir)
{
  const char *catalog = NULL, *c;
  char buf[1024];

  /* Directory supplied by calling program, from config file,
     environment variable, ... */
  if ((catalog = search_dir_for_catalog(preferreddir)) != NULL)
    goto found;

  /* Directory supplied by environment variable CUPS_LOCALEDIR */
  if ((catalog = search_dir_for_catalog(getenv("CUPS_LOCALEDIR"))) != NULL)
    goto found;

  /* Determine CUPS datadir (usually /usr/share/cups) */
  if ((c = getenv("CUPS_DATADIR")) == NULL)
    c = CUPS_DATADIR;

  /* Search /usr/share/cups/locale/ (location which
     Debian/Ubuntu package of CUPS is using) */
  snprintf(buf, sizeof(buf), "%s/locale", c);
  if ((catalog = search_dir_for_catalog(buf)) != NULL)
    goto found;

  /* Search /usr/(local/)share/locale/ (standard location
     which CUPS is using on Linux) */
  snprintf(buf, sizeof(buf), "%s/../locale", c);
  if ((catalog = search_dir_for_catalog(buf)) != NULL)
    goto found;

  /* Search /usr/(local/)lib/locale/ (standard location
     which CUPS is using on many non-Linux systems) */
  snprintf(buf, sizeof(buf), "%s/../../lib/locale", c);
  if ((catalog = search_dir_for_catalog(buf)) != NULL)
    goto found;

 found:
  return catalog;
}

static int
compare_choices(void *a,
		void *b,
		void *user_data)
{
  return strcasecmp(((ipp_choice_strings_t *)a)->name,
		    ((ipp_choice_strings_t *)b)->name);
}

static int
compare_options(void *a,
		void *b,
		void *user_data)
{
  return strcasecmp(((ipp_opt_strings_t *)a)->name,
		    ((ipp_opt_strings_t *)b)->name);
}

static void
free_choice_strings(void* entry, void* user_data)
{
  ipp_choice_strings_t *entry_rec = (ipp_choice_strings_t *)entry;

  if (entry_rec) {
    if (entry_rec->name) free(entry_rec->name);
    if (entry_rec->human_readable) free(entry_rec->human_readable);
    free(entry_rec);
  }
}

static void
free_opt_strings(void* entry,
		 void* user_data)
{
  ipp_opt_strings_t *entry_rec = (ipp_opt_strings_t *)entry;

  if (entry_rec) {
    if (entry_rec->name) free(entry_rec->name);
    if (entry_rec->human_readable) free(entry_rec->human_readable);
    if (entry_rec->choices) cupsArrayDelete(entry_rec->choices);
    free(entry_rec);
  }
}

static cups_array_t *
opt_array_new()
{
  return cupsArrayNew3(compare_options, NULL, NULL, 0,
		       NULL, free_opt_strings);
}

static ipp_opt_strings_t *
find_opt_in_array(cups_array_t *options,
		  char *name)
{
  ipp_opt_strings_t opt;

  if (!name || !options)
    return NULL;

  opt.name = name;
  return cupsArrayFind(options, &opt);
}

static ipp_choice_strings_t *
find_choice_in_array(cups_array_t *choices,
		     char *name)
{
  ipp_choice_strings_t choice;

  if (!name || !choices)
    return NULL;

  choice.name = name;
  return cupsArrayFind(choices, &choice);
}

static ipp_opt_strings_t *
add_opt_to_array(char *name,
		 char *human_readable,
		 cups_array_t *options)
{
  ipp_opt_strings_t *opt = NULL;

  if (!name || !options)
    return NULL;

  if ((opt = find_opt_in_array(options, name)) == NULL) {
    opt = calloc(1, sizeof(ipp_opt_strings_t));
    if (!opt) return NULL;
    opt->human_readable = NULL;
    opt->choices = cupsArrayNew3(compare_choices, NULL, NULL, 0,
				 NULL, free_choice_strings);
    if (!opt->choices) {
      free(opt);
      return NULL;
    }
    opt->name = strdup(name);
    if (!cupsArrayAdd(options, opt)) {
      free_opt_strings(opt, NULL);
      return NULL;
    }
  }

  if (human_readable)
    opt->human_readable = strdup(human_readable);

  return opt;
}

static ipp_choice_strings_t *
add_choice_to_array(char *name,
		    char *human_readable,
		    char *opt_name,
		    cups_array_t *options)
{
  ipp_choice_strings_t *choice = NULL;
  ipp_opt_strings_t *opt;

  if (!name || !human_readable || !opt_name || !options)
    return NULL;

  opt = add_opt_to_array(opt_name, NULL, options);
  if (!opt) return NULL;

  if ((choice = find_choice_in_array(opt->choices, name)) == NULL) {
    choice = calloc(1, sizeof(ipp_choice_strings_t));
    if (!choice) return NULL;
    choice->human_readable = NULL;
    choice->name = strdup(name);
    if (!cupsArrayAdd(opt->choices, choice)) {
      free_choice_strings(choice, NULL);
      return NULL;
    }
  }

  if (human_readable)
    choice->human_readable = strdup(human_readable);

  return choice;

}

static char *
lookup_option(char *name,
	      cups_array_t *options,
	      cups_array_t *printer_options)
{
  ipp_opt_strings_t *opt = NULL;

  if (!name || !options)
    return NULL;

  if (printer_options &&
      (opt = find_opt_in_array(printer_options, name)) != NULL)
    return opt->human_readable;
  if ((opt = find_opt_in_array(options, name)) != NULL)
    return opt->human_readable;
  else
    return NULL;
}

static char *
lookup_choice(char *name,
	      char *opt_name,
	      cups_array_t *options,
	      cups_array_t *printer_options)
{
  ipp_opt_strings_t *opt = NULL;
  ipp_choice_strings_t *choice = NULL;

  if (!name || !opt_name || !options)
    return NULL;

  if (printer_options &&
      (opt = find_opt_in_array(printer_options, opt_name)) != NULL &&
      (choice = find_choice_in_array(opt->choices, name)) != NULL)
    return choice->human_readable;
  else if ((opt = find_opt_in_array(options, opt_name)) != NULL &&
	   (choice = find_choice_in_array(opt->choices, name)) != NULL)
    return choice->human_readable;
  else
    return NULL;
}


static void
load_opt_strings_catalog(const char *location,
			 cups_array_t *options)
{
  char tmpfile[1024];
  const char *filename = NULL;
  struct stat statbuf;
  cups_file_t *fp;
  char line[65536];
  char *ptr, *start, *start2, *end, *end2, *sep;
  char *opt_name = NULL, *choice_name = NULL,
       *human_readable = NULL;
  int part = -1; /* -1: before first "msgid" or invalid
		        line
		     0: "msgid"
		     1: "msgstr"
		     2: "..." = "..."
		    10: EOF, save last entry */
  int digit;
  int found_in_catalog = 0;

  if (location == NULL || (strncasecmp(location, "http:", 5) &&
			   strncasecmp(location, "https:", 6))) {
    if (location == NULL ||
	(stat(location, &statbuf) == 0 &&
	 S_ISDIR(statbuf.st_mode))) /* directory? */
    {
      filename = find_cups_message_catalog(location);
      if (filename)
        found_in_catalog = 1;
    }
    else
      filename = location;
  } else {
    if (get_url(location, tmpfile, sizeof(tmpfile)))
      filename = tmpfile;
  }
  if (!filename)
    return;

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    if (filename == tmpfile)
      unlink(filename);
    return;
  }

  while (cupsFileGets(fp, line, sizeof(line)) || (part = 10)) {
    /* Find a pair of quotes delimiting a string in each line
       and optional "msgid" or "msgstr" keywords, or a
       "..." = "..." pair. Skip comments ('#') and empty lines. */
    if (part < 10) {
      ptr = line;
      while (isspace(*ptr)) ptr ++;
      if (*ptr == '#' || *ptr == '\0') continue;
      if ((start = strchr(ptr, '\"')) == NULL) continue;
      if ((end = strrchr(ptr, '\"')) == start) continue;
      if (*(end - 1) == '\\') continue;
      start2 = NULL;
      end2 = NULL;
      if (start > ptr) {
	if (*(start - 1) == '\\') continue;
	if (strncasecmp(ptr, "msgid", 5) == 0) part = 0;
	if (strncasecmp(ptr, "msgstr", 6) == 0) part = 1;
      } else {
	start2 = ptr;
	while ((start2 = strchr(start2 + 1, '\"')) < end &&
	       *(start2 - 1) == '\\');
	if (start2 < end) {
	  /* Line with "..." = "..." of text/strings format */
	  end2 = end;
	  end = start2;
	  start2 ++;
	  while (isspace(*start2)) start2 ++;
	  if (*start2 != '=') continue;
	  start2 ++;
	  while (isspace(*start2)) start2 ++;
	  if (*start2 != '\"') continue;
	  start2 ++;
	  *end2 = '\0';
	  part = 2;
	} else
	  /* Continuation line in message catalog file */
	  start2 = NULL;
      }
      start ++;
      *end = '\0';
    }
    /* Read out the strings between the quotes and save entries */
    if (part == 0 || part == 2 || part == 10) {
      /* Save previous attribute */
      if (human_readable) {
	if (opt_name) {
	  if (choice_name) {
	    add_choice_to_array(choice_name, human_readable,
				opt_name, options);
	    free(choice_name);
	  } else
	    add_opt_to_array(opt_name, human_readable, options);
	  free(opt_name);
	}
	free(human_readable);
	opt_name = NULL;
	choice_name = NULL;
	human_readable = NULL;
      }
      /* Stop the loop after saving the last entry */
      if (part == 10)
	break;
      /* IPP attribute has to be defined with a single msgid line,
	 no continuation lines */
      if (opt_name) {
	free (opt_name);
	opt_name = NULL;
	if (choice_name) {
	  free (choice_name);
	  choice_name = NULL;
	}
	part = -1;
	continue;
      }
      /* No continuation line in text/strings format */
      if (part == 2 && (start2 == NULL || end2 == NULL)) {
	part = -1;
	continue;
      }
      /* Check line if it is a valid IPP attribute:
	 No spaces, only lowercase letters, digits, '-', '_',
	 "option" or "option.choice" */
      for (ptr = start, sep = NULL; ptr < end; ptr ++)
	if (*ptr == '.') { /* Separator between option and choice */
	  if (!sep) { /* Only the first '.' counts */
	    sep = ptr + 1;
	    *ptr = '\0';
	  }
	} else if (!((*ptr >= 'a' && *ptr <= 'z') ||
		     (*ptr >= '0' && *ptr <= '9') ||
		     *ptr == '-' || *ptr == '_'))
	  break;
      if (ptr < end) { /* Illegal character found */
	part = -1;
	continue;
      }
      if (strlen(start) > 0) /* Option name found */
	opt_name = strdup(start);
      else { /* Empty option name */
	part = -1;
	continue;
      }
      if (sep && strlen(sep) > 0) /* Choice name found */
	choice_name = strdup(sep);
      else /* Empty choice name */
	choice_name = NULL;
      if (part == 2) { /* Human-readable string in the same line */
	start = start2;
	end = end2;
      }
    }
    if (part == 1 || part == 2) {
      /* msgid was not for an IPP attribute, ignore this msgstr */
      if (!opt_name) continue;
      /* Empty string */
      if (start == end) continue;
      /* Unquote string */
      ptr = start;
      end = start;
      while (*ptr) {
	if (*ptr == '\\') {
	  ptr ++;
	  if (isdigit(*ptr)) {
	    digit = 0;
	    *end = 0;
	    while (isdigit(*ptr) && digit < 3) {
	      *end = *end * 8 + *ptr - '0';
	      digit ++;
	      ptr ++;
	    }
	    end ++;
	  } else {
	    if (*ptr == 'n')
	      *end ++ = '\n';
	    else if (*ptr == 'r')
	      *end ++ = '\r';
	    else if (*ptr == 't')
	      *end ++ = '\t';
	    else
	      *end ++ = *ptr;
	    ptr ++;
	  }
	} else
	  *end ++ = *ptr ++;
      }
      *end = '\0';
      /* Did the unquoting make the string empty? */
      if (strlen(start) == 0) continue;
      /* Add the string to our human-readable string */
      if (human_readable) { /* Continuation line */
	human_readable = realloc(human_readable,
				 sizeof(char) *
				 (strlen(human_readable) +
				  strlen(start) + 2));
	ptr = human_readable + strlen(human_readable);
	*ptr = ' ';
	strlcpy(ptr + 1, start, strlen(start) + 1);
      } else { /* First line */
	human_readable = malloc(sizeof(char) *
				(strlen(start) + 1));
	strlcpy(human_readable, start, strlen(start) + 1);
      }
    }
  }
  cupsFileClose(fp);
  if (choice_name != NULL)
    free(choice_name);
  if (opt_name != NULL)
    free(opt_name);
  if (filename == tmpfile)
    unlink(filename);
  if (found_in_catalog)
    free((char *)filename);
}


int
cfCompareResolutions(void *resolution_a,
		     void *resolution_b,
		     void *user_data)
{
  cf_res_t *res_a = (cf_res_t *)resolution_a;
  cf_res_t *res_b = (cf_res_t *)resolution_b;
  int i, a, b;

  /* Compare the pixels per square inch */
  a = res_a->x * res_a->y;
  b = res_b->x * res_b->y;
  i = (a > b) - (a < b);
  if (i) return i;

  /* Compare how much the pixel shape deviates from a square, the
     more, the worse */
  a = 100 * res_a->y / res_a->x;
  if (a > 100) a = 10000 / a; 
  b = 100 * res_b->y / res_b->x;
  if (b > 100) b = 10000 / b; 
  return (a > b) - (a < b);
}

void *
cfCopyResolution(void *resolution,
		void *user_data)
{
  cf_res_t *res = (cf_res_t *)resolution;
  cf_res_t *copy;

  copy = (cf_res_t *)calloc(1, sizeof(cf_res_t));
  if (copy) {
    copy->x = res->x;
    copy->y = res->y;
  }

  return copy;
}

void
cfFreeResolution(void *resolution,
		void *user_data)
{
  cf_res_t *res = (cf_res_t *)resolution;

  if (res) free(res);
}

cups_array_t *
cfNewResolutionArray()
{
  return cupsArrayNew3(cfCompareResolutions, NULL, NULL, 0,
		       cfCopyResolution, cfFreeResolution);
}

cf_res_t *
cfNewResolution(int x,
		int y)
{
  cf_res_t *res = (cf_res_t *)calloc(1, sizeof(cf_res_t));
  if (res) {
    res->x = x;
    res->y = y;
  }
  return res;
}

/* Read a single resolution from an IPP attribute, take care of
   obviously wrong entries (printer firmware bugs), ignoring
   resolutions of less than 60 dpi in at least one dimension and
   fixing Brother's "600x2dpi" resolutions. */
cf_res_t *
cfIPPResToResolution(ipp_attribute_t *attr,
		     int index)
{
  cf_res_t *res = NULL;
  int x = 0, y = 0;

  if (attr) {
    ipp_tag_t tag = ippGetValueTag(attr);
    int count = ippGetCount(attr);

    if (tag == IPP_TAG_RESOLUTION && index < count) {
      ppdPwgPpdizeResolution(attr, index, &x, &y, NULL, 0);
      if (y == 2) y = x; /* Brother quirk ("600x2dpi") */
      if (x >= 60 && y >= 60)
	res = cfNewResolution(x, y);
    }
  }

  return res;
}

cups_array_t *
cfIPPAttrToResolutionArray(ipp_attribute_t *attr)
{
  cups_array_t *res_array = NULL;
  cf_res_t *res;
  int i;

  if (attr) {
    ipp_tag_t tag = ippGetValueTag(attr);
    int count = ippGetCount(attr);

    if (tag == IPP_TAG_RESOLUTION && count > 0) {
      res_array = cfNewResolutionArray();
      if (res_array) {
	for (i = 0; i < count; i ++)
	  if ((res = cfIPPResToResolution(attr, i)) != NULL) {
	    if (cupsArrayFind(res_array, res) == NULL)
	      cupsArrayAdd(res_array, res);
	    cfFreeResolution(res, NULL);
	  }
      }
      if (cupsArrayCount(res_array) == 0) {
	cupsArrayDelete(res_array);
	res_array = NULL;
      }
    }
  }

  return res_array;
}

/* Build up an array of common resolutions and most desirable default
   resolution from multiple arrays of resolutions with an optional
   default resolution.
   Call this function with each resolution array you find as "new", and
   in "current" an array of the common resolutions will be built up.
   You do not need to create an empty array for "current" before
   starting. Initialize it with NULL.
   "current_default" holds the default resolution of the array "current".
   It will get replaced by "new_default" if "current_default" is either
   NULL or a resolution which is not in "current" any more.
   "new" and "new_default" will be deleted/freed and set to NULL after
   each, successful or unsuccssful operation.
   Note that when calling this function the addresses of the pointers
   to the resolution arrays and default resolutions have to be given
   (call by reference) as all will get modified by the function. */

int /* 1 on success, 0 on failure */
cfJoinResolutionArrays(cups_array_t **current,
		       cups_array_t **new_arr,
		       cf_res_t **current_default,
		       cf_res_t **new_default)
{
  cf_res_t *res;
  int retval;

  if (current == NULL || new_arr == NULL || *new_arr == NULL ||
      cupsArrayCount(*new_arr) == 0) {
    retval = 0;
    goto finish;
  }

  if (*current == NULL) {
    /* We are adding the very first resolution array, simply make it
       our common resolutions array */
    *current = *new_arr;
    if (current_default) {
      if (*current_default)
	free(*current_default);
      *current_default = (new_default ? *new_default : NULL);
    }
    return 1;
  } else if (cupsArrayCount(*current) == 0) {
    retval = 1;
    goto finish;
  }

  /* Dry run: Check whether the two arrays have at least one resolution
     in common, if not, do not touch the original array */
  for (res = cupsArrayFirst(*current);
       res; res = cupsArrayNext(*current))
    if (cupsArrayFind(*new_arr, res))
      break;

  if (res) {
    /* Reduce the original array to the resolutions which are in both
       the original and the new array, at least one resolution will
       remain. */
    for (res = cupsArrayFirst(*current);
	 res; res = cupsArrayNext(*current))
      if (!cupsArrayFind(*new_arr, res))
	cupsArrayRemove(*current, res);
    if (current_default) {
      /* Replace the current default by the new one if the current default
	 is not in the array any more or if it is NULL. If the new default
	 is not in the list or NULL in such a case, set the current default
	 to NULL */
      if (*current_default && !cupsArrayFind(*current, *current_default)) {
	free(*current_default);
	*current_default = NULL;
      }
      if (*current_default == NULL && new_default && *new_default &&
	  cupsArrayFind(*current, *new_default))
	*current_default = cfCopyResolution(*new_default, NULL);
    }
    retval = 1;
  } else
    retval = 0;

 finish:
  if (new_arr && *new_arr) {
    cupsArrayDelete(*new_arr);
    *new_arr = NULL;
  }
  if (new_default && *new_default) {
    free(*new_default);
    *new_default = NULL;
  }
  return retval;
}

cups_array_t*
cfGenerateSizes(ipp_t *response,
		ipp_attribute_t **defattr,
		int* min_length,
		int* min_width,
		int* max_length,
		int* max_width,
		int* bottom,
		int* left,
		int* right,
		int* top,
		char* ppdname)
{
  cups_array_t             *sizes;               /* Media sizes we've added */
  ipp_attribute_t          *attr,                /* xxx-supported */
                           *x_dim, *y_dim;       /* Media dimensions */
  ipp_t                    *media_col,           /* Media collection */
                           *media_size;          /* Media size collection */
  int                      i, count = 0;
  pwg_media_t              *pwg;                 /* PWG media size */
  int                      left_def, right_def, bottom_def, top_def;
  ipp_attribute_t          *margin;  /* media-xxx-margin attribute */
  const char               *psname;

  if ((attr = ippFindAttribute(response, "media-bottom-margin-supported",
			       IPP_TAG_INTEGER)) != NULL) {
    for (i = 1, *bottom = ippGetInteger(attr, 0), count = ippGetCount(attr);
	 i < count; i ++)
      if (i == 1 || ippGetInteger(attr, i) < *bottom)
        *bottom = ippGetInteger(attr, i);
  } else
    *bottom = 1270;

  if ((attr = ippFindAttribute(response, "media-left-margin-supported",
			       IPP_TAG_INTEGER)) != NULL) {
    for (i = 1, *left = ippGetInteger(attr, 0), count = ippGetCount(attr);
	 i < count; i ++)
      if (i == 1 || ippGetInteger(attr, i) < *left)
        *left = ippGetInteger(attr, i);
  } else
    *left = 635;

  if ((attr = ippFindAttribute(response, "media-right-margin-supported",
			       IPP_TAG_INTEGER)) != NULL) {
    for (i = 1, *right = ippGetInteger(attr, 0), count = ippGetCount(attr);
	 i < count; i ++)
      if (i == 1 || ippGetInteger(attr, i) < *right)
        *right = ippGetInteger(attr, i);
  } else
    *right = 635;

  if ((attr = ippFindAttribute(response, "media-top-margin-supported",
			       IPP_TAG_INTEGER)) != NULL) {
    for (i = 1, *top = ippGetInteger(attr, 0), count = ippGetCount(attr);
	 i < count; i ++)
      if (i == 1 || ippGetInteger(attr, i) < *top)
        *top = ippGetInteger(attr, i);
  } else
    *top = 1270;

  if ((*defattr = ippFindAttribute(response, "media-col-default",
				   IPP_TAG_BEGIN_COLLECTION)) != NULL) {
    if ((attr = ippFindAttribute(ippGetCollection(*defattr, 0), "media-size",
				 IPP_TAG_BEGIN_COLLECTION)) != NULL) {
      media_size = ippGetCollection(attr, 0);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);
  
      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-bottom-margin", IPP_TAG_INTEGER))
	  != NULL)
	bottom_def = ippGetInteger(margin, 0);
      else
	bottom_def = *bottom;

      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-left-margin", IPP_TAG_INTEGER))
	  != NULL)
	left_def = ippGetInteger(margin, 0);
      else
	left_def = *left;

      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-right-margin", IPP_TAG_INTEGER))
	  != NULL)
	right_def = ippGetInteger(margin, 0);
      else
	right_def = *right;

      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-top-margin", IPP_TAG_INTEGER))
	  != NULL)
	top_def = ippGetInteger(margin, 0);
      else
	top_def = *top;

      if (x_dim && y_dim &&
	  (pwg = pwgMediaForSize(ippGetInteger(x_dim, 0),
				 ippGetInteger(y_dim, 0))) != NULL) {
	psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
        if (bottom_def == 0 && left_def == 0 && right_def == 0 && top_def == 0)
          snprintf(ppdname, PPD_MAX_NAME, "%s.Borderless", psname);
        else
          strlcpy(ppdname, psname, PPD_MAX_NAME);
      } else
	strlcpy(ppdname, "Unknown", PPD_MAX_NAME);
    } else
      strlcpy(ppdname, "Unknown", PPD_MAX_NAME);
  } else if ((pwg =
	      pwgMediaForPWG(ippGetString(ippFindAttribute(response,
							   "media-default",
							   IPP_TAG_ZERO), 0,
					  NULL))) != NULL) {
    psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
    strlcpy(ppdname, psname, PPD_MAX_NAME);
  } else
    strlcpy(ppdname, "Unknown", PPD_MAX_NAME);

  sizes = cupsArrayNew3((cups_array_func_t)pwg_compare_sizes, NULL, NULL, 0,
			(cups_acopy_func_t)pwg_copy_size,
			(cups_afree_func_t)free);

  if ((attr = ippFindAttribute(response, "media-col-database",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL) {
    for (i = 0, count = ippGetCount(attr); i < count; i ++) {
      cups_size_t temp;   /* Current size */

      media_col   = ippGetCollection(attr, i);
      media_size  =
	ippGetCollection(ippFindAttribute(media_col, "media-size",
					  IPP_TAG_BEGIN_COLLECTION), 0);
      x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
      y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);
      pwg         = pwgMediaForSize(ippGetInteger(x_dim, 0),
				    ippGetInteger(y_dim, 0));

      if (pwg) {
	temp.width  = pwg->width;
	temp.length = pwg->length;

	if ((margin = ippFindAttribute(media_col, "media-bottom-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.bottom = ippGetInteger(margin, 0);
	else
	  temp.bottom = *bottom;

	if ((margin = ippFindAttribute(media_col, "media-left-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.left = ippGetInteger(margin, 0);
	else
	  temp.left = *left;

	if ((margin = ippFindAttribute(media_col, "media-right-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.right = ippGetInteger(margin, 0);
	else
	  temp.right = *right;

	if ((margin = ippFindAttribute(media_col, "media-top-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.top = ippGetInteger(margin, 0);
	else
	  temp.top = *top;

	psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
	if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 &&
	    temp.top == 0)
	  snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	else
	  strlcpy(temp.media, psname, sizeof(temp.media));

	if (!cupsArrayFind(sizes, &temp))
	  cupsArrayAdd(sizes, &temp);
      } else if (ippGetValueTag(x_dim) == IPP_TAG_RANGE ||
		 ippGetValueTag(y_dim) == IPP_TAG_RANGE) {
	/*
	 * Custom size - record the min/max values...
	 */

	int lower, upper;   /* Range values */

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(x_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(x_dim, 0);

	if (lower < *min_width)
	  *min_width = lower;
	if (upper > *max_width)
	  *max_width = upper;

	if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(y_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(y_dim, 0);

	if (lower < *min_length)
	  *min_length = lower;
	if (upper > *max_length)
	  *max_length = upper;
      }
    }
  }
  if ((attr = ippFindAttribute(response, "media-size-supported",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL) {
    for (i = 0, count = ippGetCount(attr); i < count; i ++) {
      cups_size_t temp;   /* Current size */

      media_size  = ippGetCollection(attr, i);
      x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
      y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);
      pwg         = pwgMediaForSize(ippGetInteger(x_dim, 0),
				    ippGetInteger(y_dim, 0));

      if (pwg) {
	temp.width  = pwg->width;
	temp.length = pwg->length;
	temp.bottom = *bottom;
	temp.left   = *left;
	temp.right  = *right;
	temp.top    = *top;

	psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
	if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 &&
	    temp.top == 0)
	  snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	else
	  strlcpy(temp.media, psname, sizeof(temp.media));

	if (!cupsArrayFind(sizes, &temp))
	  cupsArrayAdd(sizes, &temp);
      } else if (ippGetValueTag(x_dim) == IPP_TAG_RANGE ||
		 ippGetValueTag(y_dim) == IPP_TAG_RANGE) {
	/*
	 * Custom size - record the min/max values...
	 */

	int lower, upper;   /* Range values */

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(x_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(x_dim, 0);

	if (lower < *min_width)
	  *min_width = lower;
	if (upper > *max_width)
	  *max_width = upper;

	if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(y_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(y_dim, 0);

	if (lower < *min_length)
	  *min_length = lower;
	if (upper > *max_length)
	  *max_length = upper;
      }
    }
  }
  if ((attr = ippFindAttribute(response, "media-supported", IPP_TAG_ZERO))
      != NULL) {
    for (i = 0, count = ippGetCount(attr); i < count; i ++) {
      const char  *pwg_size = ippGetString(attr, i, NULL);
      /* PWG size name */
      cups_size_t temp, *temp2; /* Current size, found size */

      if ((pwg = pwgMediaForPWG(pwg_size)) != NULL) {
        if (strstr(pwg_size, "_max_") || strstr(pwg_size, "_max.")) {
          if (pwg->width > *max_width)
            *max_width = pwg->width;
          if (pwg->length > *max_length)
            *max_length = pwg->length;
        } else if (strstr(pwg_size, "_min_") || strstr(pwg_size, "_min.")) {
          if (pwg->width < *min_width)
            *min_width = pwg->width;
          if (pwg->length < *min_length)
            *min_length = pwg->length;
        } else {
	  temp.width  = pwg->width;
	  temp.length = pwg->length;
	  temp.bottom = *bottom;
	  temp.left   = *left;
	  temp.right  = *right;
	  temp.top    = *top;

	  psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
	  if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 &&
	      temp.top == 0)
	    snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	  else
	    strlcpy(temp.media, psname, sizeof(temp.media));

	  /* Add the printer's original IPP name to an already found size */
	  if ((temp2 = cupsArrayFind(sizes, &temp)) != NULL) {
	    snprintf(temp2->media + strlen(temp2->media),
		     sizeof(temp2->media) - strlen(temp2->media),
		     " %s", pwg_size);
	    /* Check if we have also a borderless version of the size and add
	       the original IPP name also there */
	    snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	    if ((temp2 = cupsArrayFind(sizes, &temp)) != NULL)
	      snprintf(temp2->media + strlen(temp2->media),
		       sizeof(temp2->media) - strlen(temp2->media),
		       " %s", pwg_size);
	  } else
	    cupsArrayAdd(sizes, &temp);
	}
      }
    }
  }
  return sizes;
}


/*
 * Human-readable strings in the PPDs generated by the PPD generator for
 * using driverless IPP printers with CUPS
 *
 * To allow for translated/localized PPDs we use the standard set
 * of human-readable strings from the PWG:
 *
 * https://ftp.pwg.org/pub/pwg/ipp/examples/ipp.pot
 * https://ftp.pwg.org/pub/pwg/ipp/examples/ipp.strings
 *
 * Creating those with translated human-readable strings would allow us to
 * easily produce PPDs in other languges.
 *
 * Translations are supposed to go here (but none are available yet):
 *
 * https://github.com/istopwg/ippregistry/tree/master/localizations
 *
 * These standard strings are also part of CUPS' translation files:
 *
 * https://github.com/OpenPrinting/cups/tree/master/locale
 *
 * Here translations take actually place as part of the translations
 * of CUPS itself.
 *
 * We take the files from the CUPS installed into the system (as the PPD
 * generator actually does not make sense without CUPS) but currently
 * only use the English version. It is not complicated to check the user
 * language in the printer's IPP response via the
 * attributes-natural-language attribute and then request an appropriate
 * language version of the files if available. The printer-specific
 * strings are downloaded from the printer following the URI in the
 * printer-strings-uri attribute and are in the slected language.
 *
 * There are no human-readable strings in these files for the non-IPP
 * choices of the print color mode, for 16 bit color depth, Adobe RGB,
 * and "device" modes as advertised for Apple Raster (urf-supported)
 * and PWG Raster (pwg-raster-document-type-supported). These color
 * spaces are supposed to not be selected manually by the user but the
 * filters should instead detect the color space and depth of the input
 * file and automatically select the best color space and depth for the
 * output to the printer. As the filters do not currently do that we
 * we are allowing the manual selection as a workaround.
 *
 * It is not clear whether these issues (translated PPDs and color space
 * selection) will get fixed in the PPD generator as the need of PPDs
 * in CUPS can go away soon.
 *
 * See also:
 *
 * https://lists.linuxfoundation.org/pipermail/printing-architecture/2021/003992.html
 * https://lists.linuxfoundation.org/pipermail/printing-architecture/2021/003995.html
 */

/*
 * 'cfCreatePPDFromIPP()' - Create a PPD file describing the capabilities
 *                          of an IPP printer, using info from DNS-SD record
 *                          as fallback (for poor IPP responses, especially
 *                          IPP 1.x legacy)
 */

char *                                           /* O - PPD filename or NULL on
						        error */
cfCreatePPDFromIPP (char         *buffer,          /* I - Filename buffer */
		    size_t       bufsize,          /* I - Size of filename
						          buffer */
		    ipp_t        *response,        /* I - Get-Printer-Attributes
						          response */
		    const char   *make_model,      /* I - Make and model from
						          DNS-SD */
		    const char   *pdl,             /* I - List of PDLs from
						          DNS-SD */
		    int          color,            /* I - Color printer? (from
						          DNS-SD) */
		    int          duplex,           /* I - Duplex printer? (from
						          DNS-SD) */
		    char         *status_msg,      /* I - Status message buffer,
						          NULL to ignore
							  message */
		    size_t       status_msg_size)  /* I - Size of status message
						          buffer */
{
  return cfCreatePPDFromIPP2(buffer, bufsize, response, make_model, pdl,
			     color, duplex, NULL, NULL, NULL, NULL,
			     status_msg, status_msg_size);
}

/*
 * 'cfCreatePPDFromIPP2()' - Create a PPD file describing the capabilities
 *                           of an IPP printer, with extra parameters for
 *                           PPDs from a merged IPP record for printer
 *                           clusters
 */

char *                                           /* O - PPD filename or NULL on
						        error */
cfCreatePPDFromIPP2(char         *buffer,          /* I - Filename buffer */
		    size_t       bufsize,          /* I - Size of filename
						          buffer */
		    ipp_t        *response,        /* I - Get-Printer-Attributes
						          response */
		    const char   *make_model,      /* I - Make and model from
						          DNS-SD */
		    const char   *pdl,             /* I - List of PDLs from
						          DNS-SD */
		    int          color,            /* I - Color printer? (from
						          DNS-SD) */
		    int          duplex,           /* I - Duplex printer? (from
						          DNS-SD) */
		    cups_array_t *conflicts,       /* I - Array of constraints*/
		    cups_array_t *sizes,           /* I - Media sizes we've
						          added */ 
		    char*        default_pagesize, /* I - Default page size*/
		    const char   *default_cluster_color, /* I - cluster def
							  color (if cluster's
							  attributes are
							  returned) */
		    char         *status_msg,      /* I - Status message buffer,
						          NULL to ignore
							  message */
		    size_t       status_msg_size)  /* I - Size of status message
						          buffer */
{
  cups_file_t		*fp;		/* PPD file */
  cups_array_t		*printer_sizes;	/* Media sizes we've added */
  cups_size_t		*size;		/* Current media size */
  ipp_attribute_t	*attr,		/* xxx-supported */
                        *attr2,
			*defattr,	/* xxx-default */
                        *quality,	/* print-quality-supported */
			*x_dim, *y_dim;	/* Media dimensions */
  ipp_t			*media_col,	/* Media collection */
			*media_size;	/* Media size collection */
  char			make[256],	/* Make and model */
			*model,		/* Model name */
			ppdname[PPD_MAX_NAME];
		    			/* PPD keyword */
  int			i, j,		/* Looping vars */
			count = 0,	/* Number of values */
			bottom,		/* Largest bottom margin */
			left,		/* Largest left margin */
			right,		/* Largest right margin */
			top,		/* Largest top margin */
			max_length = 0,	/* Maximum custom size */
			max_width = 0,
			min_length = INT_MAX,
					/* Minimum custom size */
			min_width = INT_MAX,
			is_apple = 0,	/* Does the printer support Apple
					   Raster? */
                        is_pwg = 0,	/* Does the printer support PWG
					   Raster? */
                        is_pclm = 0,    /* Does the printer support PCLm? */
                        is_pdf = 0;     /* Does the printer support PDF? */
  pwg_media_t		*pwg;		/* PWG media size */
  int			xres, yres;	/* Resolution values */
  cups_array_t          *common_res,    /* Common resolutions of all PDLs */
                        *current_res,   /* Resolutions of current PDL */
                        *pdl_list;      /* List of PDLs */
  cf_res_t              *common_def,    /* Common default resolution */
                        *current_def,   /* Default resolution of current PDL */
                        *min_res,       /* Minimum common resolution */
                        *max_res;       /* Maximum common resolution */
  struct lconv		*loc = localeconv();
					/* Locale data */
  cups_array_t          *opt_strings_catalog = NULL;
                                        /* Standard option UI strings */
  cups_array_t          *printer_opt_strings_catalog = NULL;
                                        /* Printer-specific option UI strings */
  char                  *human_readable,
                        *human_readable2;
  const char		*keyword;	/* Keyword value */
  cups_array_t		*fin_options = NULL;
					/* Finishing options */
  char			buf[256],
                        filter_path[1024];
                                        /* Path to filter executable */
  const char		*cups_serverbin;/* CUPS_SERVERBIN environment
					   variable */
  char			*defaultoutbin = NULL;
  const char		*outbin;
  char			outbin_properties[1024];
  int			octet_str_len;
  void			*outbin_properties_octet;
  int			outputorderinfofound = 0,
			faceupdown = 1,
			firsttolast = 1;
  int			manual_copies = -1,
			is_fax = 0;

 /*
  * Range check input...
  */

  if (buffer)
    *buffer = '\0';

  if (!buffer || bufsize < 1) {
    if (status_msg && status_msg_size)
      snprintf(status_msg, status_msg_size, "%s", strerror(EINVAL));
    return (NULL);
  }

  if (!response) {
    if (status_msg && status_msg_size)
      snprintf(status_msg, status_msg_size, "No IPP attributes.");
    return (NULL);
  }

 /*
  * Open a temporary file for the PPD...
  */

  if ((fp = cupsTempFile2(buffer, (int)bufsize)) == NULL) {
    if (status_msg && status_msg_size)
      snprintf(status_msg, status_msg_size, "%s", strerror(errno));
    return (NULL);
  }

 /*
  * Standard stuff for PPD file...
  */

  cupsFilePuts(fp, "*PPD-Adobe: \"4.3\"\n");
  cupsFilePuts(fp, "*FormatVersion: \"4.3\"\n");
  cupsFilePrintf(fp, "*FileVersion: \"%s\"\n", VERSION);
  cupsFilePuts(fp, "*LanguageVersion: English\n");
  cupsFilePuts(fp, "*LanguageEncoding: ISOLatin1\n");
  cupsFilePuts(fp, "*PSVersion: \"(3010.000) 0\"\n");
  cupsFilePuts(fp, "*LanguageLevel: \"3\"\n");
  cupsFilePuts(fp, "*FileSystem: False\n");
  cupsFilePuts(fp, "*PCFileName: \"drvless.ppd\"\n");

  if ((attr = ippFindAttribute(response, "ipp-features-supported",
			       IPP_TAG_KEYWORD)) != NULL &&
      ippContainsString(attr, "faxout"))
  {
    attr = ippFindAttribute(response, "printer-uri-supported",
			    IPP_TAG_URI);
    if (attr)
    {
      ippAttributeString(attr, buf, sizeof(buf));
      if (strcasestr(buf, "faxout"))
	is_fax = 1;
    }
  }

  if ((attr = ippFindAttribute(response, "printer-make-and-model",
			       IPP_TAG_TEXT)) != NULL)
    strlcpy(make, ippGetString(attr, 0, NULL), sizeof(make));
  else if (make_model && make_model[0] != '\0')
    strlcpy(make, make_model, sizeof(make));
  else
    strlcpy(make, "Unknown Printer", sizeof(make));

  if (!strncasecmp(make, "Hewlett Packard ", 16) ||
      !strncasecmp(make, "Hewlett-Packard ", 16)) {
    model = make + 16;
    strlcpy(make, "HP", sizeof(make));
  }
  else if ((model = strchr(make, ' ')) != NULL)
    *model++ = '\0';
  else
    model = make;

  cupsFilePrintf(fp, "*Manufacturer: \"%s\"\n", make);
  cupsFilePrintf(fp, "*ModelName: \"%s %s\"\n", make, model);
  cupsFilePrintf(fp, "*Product: \"(%s %s)\"\n", make, model);
  cupsFilePrintf(fp, "*NickName: \"%s %s, %sdriverless, cups-filters %s\"\n",
		 make, model, (is_fax ? "Fax, " : ""), VERSION);
  cupsFilePrintf(fp, "*ShortNickName: \"%s %s\"\n", make, model);

  /* Which is the default output bin? */
  if ((attr = ippFindAttribute(response, "output-bin-default", IPP_TAG_ZERO))
      != NULL)
    defaultoutbin = strdup(ippGetString(attr, 0, NULL));
  /* Find out on which position of the list of output bins the default one is,
     if there is no default bin, take the first of this list */
  i = 0;
  if ((attr = ippFindAttribute(response, "output-bin-supported",
			       IPP_TAG_ZERO)) != NULL) {
    count = ippGetCount(attr);
    for (i = 0; i < count; i ++) {
      outbin = ippGetString(attr, i, NULL);
      if (outbin == NULL)
	continue;
      if (defaultoutbin == NULL) {
	defaultoutbin = strdup(outbin);
	break;
      } else if (strcasecmp(outbin, defaultoutbin) == 0)
	break;
    }
  }
  if ((attr = ippFindAttribute(response, "printer-output-tray",
			       IPP_TAG_STRING)) != NULL &&
      i < ippGetCount(attr)) {
    outbin_properties_octet = ippGetOctetString(attr, i, &octet_str_len);
    memset(outbin_properties, 0, sizeof(outbin_properties));
    memcpy(outbin_properties, outbin_properties_octet,
	   ((size_t)octet_str_len < sizeof(outbin_properties) - 1 ?
	    (size_t)octet_str_len : sizeof(outbin_properties) - 1));
    if (strcasestr(outbin_properties, "pagedelivery=faceUp")) {
      outputorderinfofound = 1;
      faceupdown = -1;
    }
    if (strcasestr(outbin_properties, "stackingorder=lastToFirst"))
      firsttolast = -1;
  }
  if (outputorderinfofound == 0 && defaultoutbin &&
      strcasestr(defaultoutbin, "face-up"))
    faceupdown = -1;
  if (defaultoutbin)
    free (defaultoutbin);
  if (firsttolast * faceupdown < 0)
    cupsFilePuts(fp, "*DefaultOutputOrder: Reverse\n");
  else
    cupsFilePuts(fp, "*DefaultOutputOrder: Normal\n");

  /* Do we have a color printer? */
  if (((attr = ippFindAttribute(response,
			       "color-supported", IPP_TAG_BOOLEAN)) != NULL &&
       ippGetBoolean(attr, 0)) ||
      color)
    cupsFilePuts(fp, "*ColorDevice: True\n");
  else
    cupsFilePuts(fp, "*ColorDevice: False\n");

  cupsFilePrintf(fp, "*cupsVersion: %d.%d\n", CUPS_VERSION_MAJOR,
		 CUPS_VERSION_MINOR);
  cupsFilePuts(fp, "*cupsSNMPSupplies: False\n");
  cupsFilePuts(fp, "*cupsLanguages: \"en\"\n");

  if ((attr = ippFindAttribute(response, "printer-more-info", IPP_TAG_URI)) !=
      NULL)
    cupsFilePrintf(fp, "*APSupplies: \"%s\"\n", ippGetString(attr, 0, NULL));

  if ((attr = ippFindAttribute(response, "printer-charge-info-uri",
			       IPP_TAG_URI)) != NULL)
    cupsFilePrintf(fp, "*cupsChargeInfoURI: \"%s\"\n", ippGetString(attr, 0,
								    NULL));

  /* Message catalogs for UI strings */
  if (opt_strings_catalog == NULL) {
    opt_strings_catalog = opt_array_new();
    load_opt_strings_catalog(NULL, opt_strings_catalog);
  }
  if ((attr = ippFindAttribute(response, "printer-strings-uri",
			       IPP_TAG_URI)) != NULL) {
    printer_opt_strings_catalog = opt_array_new();
    load_opt_strings_catalog(ippGetString(attr, 0, NULL),
			     printer_opt_strings_catalog);
    if (cupsArrayCount(printer_opt_strings_catalog) > 0)
      cupsFilePrintf(fp, "*cupsStringsURI: \"%s\"\n", ippGetString(attr, 0,
								   NULL));
  }

 /*
  * Accounting...
  */

  if (ippGetBoolean(ippFindAttribute(response, "job-account-id-supported",
				     IPP_TAG_BOOLEAN), 0))
    cupsFilePuts(fp, "*cupsJobAccountId: True\n");

  if (ippGetBoolean(ippFindAttribute(response,
				     "job-accounting-user-id-supported",
				     IPP_TAG_BOOLEAN), 0))
    cupsFilePuts(fp, "*cupsJobAccountingUserId: True\n");

  if ((attr = ippFindAttribute(response, "printer-privacy-policy-uri", IPP_TAG_URI)) != NULL)
    cupsFilePrintf(fp, "*cupsPrivacyURI: \"%s\"\n", ippGetString(attr, 0, NULL));

  if ((attr = ippFindAttribute(response, "printer-mandatory-job-attributes", IPP_TAG_KEYWORD)) != NULL)
  {
    char	prefix = '\"';		// Prefix for string

    cupsFilePuts(fp, "*cupsMandatory: \"");
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      if (strcmp(keyword, "attributes-charset") && strcmp(keyword, "attributes-natural-language") && strcmp(keyword, "printer-uri"))
      {
        cupsFilePrintf(fp, "%c%s", prefix, keyword);
        prefix = ',';
      }
    }
    cupsFilePuts(fp, "\"\n");
  }

  if ((attr = ippFindAttribute(response, "printer-requested-job-attributes", IPP_TAG_KEYWORD)) != NULL)
  {
    char	prefix = '\"';		// Prefix for string

    cupsFilePuts(fp, "*cupsRequested: \"");
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      if (strcmp(keyword, "attributes-charset") && strcmp(keyword, "attributes-natural-language") && strcmp(keyword, "printer-uri"))
      {
        cupsFilePrintf(fp, "%c%s", prefix, keyword);
        prefix = ',';
      }
    }
    cupsFilePuts(fp, "\"\n");
  }

 /*
  * Password/PIN printing...
  */

  if ((attr = ippFindAttribute(response, "job-password-supported",
			       IPP_TAG_INTEGER)) != NULL) {
    char	pattern[33];		/* Password pattern */
    int		maxlen = ippGetInteger(attr, 0);
					/* Maximum length */
    const char	*repertoire =
      ippGetString(ippFindAttribute(response,
				    "job-password-repertoire-configured",
				    IPP_TAG_KEYWORD), 0, NULL);
					/* Type of password */

    if (maxlen > (int)(sizeof(pattern) - 1))
      maxlen = (int)sizeof(pattern) - 1;

    if (!repertoire || !strcmp(repertoire, "iana_us-ascii_digits"))
      memset(pattern, '1', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_us-ascii_letters"))
      memset(pattern, 'A', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_us-ascii_complex"))
      memset(pattern, 'C', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_us-ascii_any"))
      memset(pattern, '.', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_utf-8_digits"))
      memset(pattern, 'N', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_utf-8_letters"))
      memset(pattern, 'U', (size_t)maxlen);
    else
      memset(pattern, '*', (size_t)maxlen);

    pattern[maxlen] = '\0';

    cupsFilePrintf(fp, "*cupsJobPassword: \"%s\"\n", pattern);
  }

  

 /*
  * PDLs and common resolutions ...
  */

  common_res = NULL;
  current_res = NULL;
  common_def = NULL;
  current_def = NULL;
  min_res = NULL;
  max_res = NULL;
  /* Put all available PDls into a simple case-insensitevely searchable
     sorted string list */
  if ((pdl_list = cupsArrayNew3((cups_array_func_t)strcasecmp, NULL, NULL, 0,
				(cups_acopy_func_t)strdup,
				(cups_afree_func_t)free)) == NULL)
    goto bad_ppd;
  int formatfound = 0;

  if (((attr = ippFindAttribute(response, "document-format-supported",
				IPP_TAG_MIMETYPE)) != NULL) ||
      (pdl && pdl[0] != '\0')) {
    const char *format = pdl;
    i = 0;
    count = ippGetCount(attr);
    while ((attr && i < count) || /* Go through formats in attribute */
	   (!attr && pdl && pdl[0] != '\0' && format[0] != '\0')) {
      /* Go through formats in pdl string (from DNS-SD record) */

      /* Pick next format from attribute */
      if (attr) format = ippGetString(attr, i, NULL);
      /* Add format to list of supported PDLs, skip duplicates */
      if (!cupsArrayFind(pdl_list, (void *)format))
	cupsArrayAdd(pdl_list, (void *)format);
      if (attr)
	/* Next format in attribute */
	i ++;
      else {
	/* Find the next format in the string pdl, if there is none left,
	   go to the terminating zero */
	while (!isspace(*format) && *format != ',' && *format != '\0')
	  format ++;
	while ((isspace(*format) || *format == ',') && *format != '\0')
	  format ++;
      }
    }
  }

  /*
   * Fax 
   */

  if (is_fax)
    cupsFilePuts(fp, "*cupsIPPFaxOut: True\n");

  /* Check for each CUPS/cups-filters-supported PDL, starting with the
     most desirable going to the least desirable. If a PDL requires a
     certain set of resolutions (the raster-based PDLs), find the
     resolutions and find out which are the common resolutions of all
     supported PDLs. Choose the default resolution from the most
     desirable of all resolution-requiring PDLs if it is common in all
     of them. Skip a resolution-requiring PDL if its resolution list
     attribute is missing or contains only broken entries. Use the
     general resolution list and default resolution of the printer
     only if it does not support any resolution-requiring PDL. Use 300
     dpi if there is no resolution info at all in the attributes.
     In case of PDF as PDL check whether also the
     "application/vnd.cups-pdf" MIME type is accepted. In this case
     our printer is a remote CUPS queue which already runs the
     pdftopdf filter on the server, so let the PPD take
     "application/pdf" as input format so that pdftopdf does not also
     get executed on the client, applying option settings twice. See
     https://github.com/apple/cups/issues/5361 */
  if (cupsArrayFind(pdl_list, "application/vnd.cups-pdf")) {
    cupsFilePuts(fp, "*cupsFilter2: \"application/pdf application/pdf 0 -\"\n");
    manual_copies = 0;
    formatfound = 1;
    is_pdf = 1;
  } else if (cupsArrayFind(pdl_list, "application/pdf")) {
    cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-pdf application/pdf 200 -\"\n");
    manual_copies = 0;
    formatfound = 1;
    is_pdf = 1;
  }
#ifdef CUPS_RASTER_HAVE_APPLERASTER
  if (cupsArrayFind(pdl_list, "image/urf")) {
    if ((attr = ippFindAttribute(response, "urf-supported",
				 IPP_TAG_KEYWORD)) != NULL) {
      int lowdpi = 0, hidpi = 0; /* Lower and higher resolution */
      for (i = 0, count = ippGetCount(attr); i < count; i ++) {
	const char *rs = ippGetString(attr, i, NULL); /* RS value */
	if (strncasecmp(rs, "RS", 2))
	  continue;
	lowdpi = atoi(rs + 2);
	if ((rs = strrchr(rs, '-')) != NULL)
	  hidpi = atoi(rs + 1);
	else
	  hidpi = lowdpi;
	break;
      }
      if (lowdpi == 0) {
	/* Invalid "urf-supported" value... */
	goto bad_ppd;
      } else {
	if ((current_res = cfNewResolutionArray()) != NULL) {
	  if ((current_def = cfNewResolution(lowdpi, lowdpi)) != NULL)
          {
	    cupsArrayAdd(current_res, current_def);
            cfFreeResolution(current_def, NULL);
          }
	  if (hidpi != lowdpi &&
	      (current_def = cfNewResolution(hidpi, hidpi)) != NULL)
          {
	    cupsArrayAdd(current_res, current_def);
            cfFreeResolution(current_def, NULL);
          }
	  current_def = NULL;
	  if (cupsArrayCount(current_res) > 0 &&
	      cfJoinResolutionArrays(&common_res, &current_res, &common_def,
				     &current_def)) {
	    cupsFilePuts(fp, "*cupsFilter2: \"image/urf image/urf 0 -\"\n");
	    if (formatfound == 0) manual_copies = 1;
	    formatfound = 1;
	    is_apple = 1;
	  }
	}
      }
    }
  }
#endif
  if (cupsArrayFind(pdl_list, "image/pwg-raster")) {
    if ((attr = ippFindAttribute(response,
				 "pwg-raster-document-resolution-supported",
				 IPP_TAG_RESOLUTION)) != NULL) {
      current_def = NULL;
      if ((current_res = cfIPPAttrToResolutionArray(attr)) != NULL &&
	  cfJoinResolutionArrays(&common_res, &current_res, &common_def,
				 &current_def)) {
	cupsFilePuts(fp, "*cupsFilter2: \"image/pwg-raster image/pwg-raster 300 -\"\n");
	if (formatfound == 0) manual_copies = 1;
	formatfound = 1;
	is_pwg = 1;
      }
    }
  }
#ifdef QPDF_HAVE_PCLM
  if (cupsArrayFind(pdl_list, "application/PCLm")) {
    if ((attr = ippFindAttribute(response, "pclm-source-resolution-supported",
				 IPP_TAG_RESOLUTION)) != NULL) {
      if ((defattr = ippFindAttribute(response,
				      "pclm-source-resolution-default",
				      IPP_TAG_RESOLUTION)) != NULL)
	current_def = cfIPPResToResolution(defattr, 0);
      else
	current_def = NULL;
      if ((current_res = cfIPPAttrToResolutionArray(attr)) != NULL &&
	  cfJoinResolutionArrays(&common_res, &current_res, &common_def,
				 &current_def)) {
	cupsFilePuts(fp, "*cupsFilter2: \"application/PCLm application/PCLm 400 -\"\n");
	if (formatfound == 0) manual_copies = 1;
	formatfound = 1;
	is_pclm = 1;
      }
    }
  }
#endif
  /* Legacy formats only if we have no driverless format */
  if (!is_pdf && !is_apple && !is_pwg && !is_pclm) {
    if (cupsArrayFind(pdl_list, "application/vnd.hp-pclxl")) {
      /* Check whether the gstopxl filter is installed,
	 otherwise ignore the PCL-XL support of the printer */
      if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
	cups_serverbin = CUPS_SERVERBIN;
      snprintf(filter_path, sizeof(filter_path), "%s/filter/gstopxl",
	       cups_serverbin);
      if (access(filter_path, X_OK) == 0) {
	cupsFilePrintf(fp, "*cupsFilter2: \"application/vnd.cups-pdf application/vnd.hp-pclxl 100 gstopxl\"\n");
	if (formatfound == 0) manual_copies = 1;
	formatfound = 1;
      }
    }
    if (cupsArrayFind(pdl_list, "application/postscript")) {
      /* Higher cost value as PostScript interpreters are often buggy */
      cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-postscript application/postscript 200 -\"\n");
      if (formatfound == 0) manual_copies = 0;
      formatfound = 1;
    }
    if (cupsArrayFind(pdl_list, "application/vnd.hp-pcl")) {
      cupsFilePrintf(fp, "*cupsFilter2: \"application/vnd.cups-raster application/vnd.hp-pcl 300 rastertopclx\"\n");
      if (formatfound == 0) manual_copies = 1;
      formatfound = 1;
    }
  }
  if (cupsArrayFind(pdl_list, "image/jpeg"))
    cupsFilePuts(fp, "*cupsFilter2: \"image/jpeg image/jpeg 0 -\"\n");
  if (cupsArrayFind(pdl_list, "image/png"))
    cupsFilePuts(fp, "*cupsFilter2: \"image/png image/png 0 -\"\n");
  cupsArrayDelete(pdl_list);
  if (manual_copies < 0) manual_copies = 1;
  if (formatfound == 0)
    goto bad_ppd;

  /* For the case that we will print in a raster format and not in a high-level
     format, we need to create multiple copies on the client. We add a line to
     the PPD which tells the pdftopdf filter to generate the copies */
  if (manual_copies == 1)
    cupsFilePuts(fp, "*cupsManualCopies: True\n");

  /* No resolution requirements by any of the supported PDLs? 
     Use "printer-resolution-supported" attribute */
  if (common_res == NULL) {
    if ((attr = ippFindAttribute(response, "printer-resolution-supported",
				 IPP_TAG_RESOLUTION)) != NULL) {
      if ((defattr = ippFindAttribute(response, "printer-resolution-default",
				      IPP_TAG_RESOLUTION)) != NULL)
	current_def = cfIPPResToResolution(defattr, 0);
      else
	current_def = NULL;
      if ((current_res = cfIPPAttrToResolutionArray(attr)) != NULL)
	cfJoinResolutionArrays(&common_res, &current_res, &common_def,
			       &current_def);
    }
  }
  /* Still no resolution found? Default to 300 dpi */
  if (common_res == NULL) {
    if ((common_res = cfNewResolutionArray()) != NULL) {
      if ((current_def = cfNewResolution(300, 300)) != NULL)
      {
	cupsArrayAdd(common_res, current_def);
        cfFreeResolution(current_def, NULL);
      }
      current_def = NULL;
    } else
      goto bad_ppd;
  }
  /* No default resolution determined yet */
  if (common_def == NULL) {
    if ((defattr = ippFindAttribute(response, "printer-resolution-default",
				    IPP_TAG_RESOLUTION)) != NULL) {
      common_def = cfIPPResToResolution(defattr, 0);
      if (!cupsArrayFind(common_res, common_def)) {
	free(common_def);
	common_def = NULL;
      }
    }
    if (common_def == NULL) {
      count = cupsArrayCount(common_res);
      common_def = cfCopyResolution(cupsArrayIndex(common_res, count / 2), NULL);
    }
  }
  /* Get minimum and maximum resolution */
  min_res = cfCopyResolution(cupsArrayFirst(common_res), NULL);
  max_res = cfCopyResolution(cupsArrayLast(common_res), NULL);
  cupsArrayDelete(common_res);

 /*
  * Generically check for Raster-format-related attributes in IPP
  * response and ppdize them one by one
  */

  attr = ippFirstAttribute(response); /* first attribute */
  while (attr) {                      /* loop through all the attributes */
    if ((is_apple && strncasecmp(ippGetName(attr), "urf-", 4) == 0) ||
	(is_pwg && strncasecmp(ippGetName(attr), "pwg-raster-", 11) == 0)
#ifdef QPDF_HAVE_PCLM
	|| (is_pclm && strncasecmp(ippGetName(attr), "pclm-", 5) == 0)
#endif
	) {
      ppdPwgPpdizeName(ippGetName(attr), ppdname, sizeof(ppdname));
      cupsFilePrintf(fp, "*cups%s: ", ppdname);
      ipp_tag_t tag = ippGetValueTag(attr);
      count = ippGetCount(attr);

      if (tag == IPP_TAG_RESOLUTION) { /* ppdize values of type resolution */
	if ((current_res = cfIPPAttrToResolutionArray(attr)) != NULL) {
	  count = cupsArrayCount(current_res);
	  if (count > 1)
	    cupsFilePuts(fp, "\"");
	  for (i = 0, current_def = cupsArrayFirst(current_res);
	       current_def;
	       i ++, current_def = cupsArrayNext(current_res)) {
	    int x = current_def->x;
	    int y = current_def->y;
	    if (x == y)
	      cupsFilePrintf(fp, "%ddpi", x);
	    else
	      cupsFilePrintf(fp, "%dx%ddpi", x, y);
	    if (i < count - 1)
	      cupsFilePuts(fp, ",");
	  }
	  if (count > 1)
	    cupsFilePuts(fp, "\"");
	  cupsFilePuts(fp, "\n");
	} else
	  cupsFilePuts(fp, "\"\"\n");
	cupsArrayDelete(current_res);
      } else {
	ippAttributeString(attr, buf, sizeof(buf));
	if (count > 1 || /* quotes around multi-valued and string
			    attributes */
	    tag == IPP_TAG_STRING ||
	    tag == IPP_TAG_TEXT ||
	    tag == IPP_TAG_TEXTLANG)
	  cupsFilePrintf(fp, "\"%s\"\n", buf);
	else
	  cupsFilePrintf(fp, "%s\n", buf);
      }
    }
    attr = ippNextAttribute(response);
  }

 /*
  * PageSize/PageRegion/ImageableArea/PaperDimension
  */
  printer_sizes = cfGenerateSizes(response, &defattr, &min_length, &min_width,
				  &max_length, &max_width,
				  &bottom, &left, &right, &top, ppdname);
  if (sizes==NULL) {
    sizes = printer_sizes;
  } else
    strcpy(ppdname, default_pagesize);

  if (cupsArrayCount(sizes) > 0) {
   /*
    * List all of the standard sizes...
    */

    char	tleft[256],		/* Left string */
		tbottom[256],		/* Bottom string */
		tright[256],		/* Right string */
		ttop[256],		/* Top string */
		twidth[256],		/* Width string */
		tlength[256],		/* Length string */
		ppdsizename[128];
    char        *ippsizename,
                *suffix;
    int         all_borderless = 1;

    /* Find a page size without ".Borderless" suffix */
    /* (if all are ".Borderless" we drop the suffix in the PPD) */
    for (size = (cups_size_t *)cupsArrayFirst(sizes); size;
	 size = (cups_size_t *)cupsArrayNext(sizes))
      if (strcasestr(size->media, ".Borderless") == NULL)
	break;
    if (size)
      all_borderless = 0;

    if (all_borderless) {
      suffix = strcasestr(ppdname, ".Borderless");
      if (suffix)
	*suffix = '\0';
    }

    cupsFilePrintf(fp, "*OpenUI *PageSize/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *PageSize\n"
		   "*DefaultPageSize: %s\n", "Media Size", ppdname);
    for (size = (cups_size_t *)cupsArrayFirst(sizes); size;
	 size = (cups_size_t *)cupsArrayNext(sizes)) {
      str_formatd(twidth, twidth + sizeof(twidth),
		      size->width * 72.0 / 2540.0, loc);
      str_formatd(tlength, tlength + sizeof(tlength),
		      size->length * 72.0 / 2540.0, loc);
      strlcpy(ppdsizename, size->media, sizeof(ppdsizename));
      if ((ippsizename = strchr(ppdsizename, ' ')) != NULL) {
	*ippsizename = '\0';
	ippsizename ++;
      }

      if (ippsizename)
	human_readable = lookup_choice(ippsizename, "media",
				       opt_strings_catalog,
				       printer_opt_strings_catalog);
      else
	human_readable = NULL;
      if (!human_readable) {
	pwg = pwgMediaForSize(size->width, size->length);
	if (pwg)
	  human_readable = lookup_choice((char *)pwg->pwg, "media",
					 opt_strings_catalog,
					 printer_opt_strings_catalog);
      }

      if (all_borderless) {
	suffix = strcasestr(ppdsizename, ".Borderless");
	if (suffix)
	  *suffix = '\0';
      }

      cupsFilePrintf(fp, "*PageSize %s%s%s%s: \"<</PageSize[%s %s]>>setpagedevice\"\n",
		     ppdsizename,
		     (human_readable ? "/" : ""),
		     (human_readable ? human_readable : ""),
		     (human_readable && strstr(ppdsizename, ".Borderless") ?
		      " (Borderless)" : ""),
		     twidth, tlength);
    }
    cupsFilePuts(fp, "*CloseUI: *PageSize\n");

    cupsFilePrintf(fp, "*OpenUI *PageRegion/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *PageRegion\n"
		   "*DefaultPageRegion: %s\n", "Media Size", ppdname);
    for (size = (cups_size_t *)cupsArrayFirst(sizes); size;
	 size = (cups_size_t *)cupsArrayNext(sizes)) {
      str_formatd(twidth, twidth + sizeof(twidth),
		      size->width * 72.0 / 2540.0, loc);
      str_formatd(tlength, tlength + sizeof(tlength),
		      size->length * 72.0 / 2540.0, loc);
      strlcpy(ppdsizename, size->media, sizeof(ppdsizename));
      if ((ippsizename = strchr(ppdsizename, ' ')) != NULL) {
	*ippsizename = '\0';
	ippsizename ++;
      }

      if (ippsizename)
	human_readable = lookup_choice(ippsizename, "media",
				       opt_strings_catalog,
				       printer_opt_strings_catalog);
      else
	human_readable = NULL;
      if (!human_readable) {
	pwg = pwgMediaForSize(size->width, size->length);
	if (pwg)
	  human_readable = lookup_choice((char *)pwg->pwg, "media",
					 opt_strings_catalog,
					 printer_opt_strings_catalog);
      }

      if (all_borderless) {
	suffix = strcasestr(ppdsizename, ".Borderless");
	if (suffix)
	  *suffix = '\0';
      }

      cupsFilePrintf(fp, "*PageRegion %s%s%s%s: \"<</PageSize[%s %s]>>setpagedevice\"\n",
		     ppdsizename,
		     (human_readable ? "/" : ""),
		     (human_readable ? human_readable : ""),
		     (human_readable && strstr(ppdsizename, ".Borderless") ?
		      " (Borderless)" : ""),
		     twidth, tlength);
    }
    cupsFilePuts(fp, "*CloseUI: *PageRegion\n");

    cupsFilePrintf(fp, "*DefaultImageableArea: %s\n"
		   "*DefaultPaperDimension: %s\n", ppdname, ppdname);

    for (size = (cups_size_t *)cupsArrayFirst(sizes); size;
	 size = (cups_size_t *)cupsArrayNext(sizes)) {
      str_formatd(tleft, tleft + sizeof(tleft),
		      size->left * 72.0 / 2540.0, loc);
      str_formatd(tbottom, tbottom + sizeof(tbottom),
		      size->bottom * 72.0 / 2540.0, loc);
      str_formatd(tright, tright + sizeof(tright),
		      (size->width - size->right) * 72.0 / 2540.0, loc);
      str_formatd(ttop, ttop + sizeof(ttop),
		      (size->length - size->top) * 72.0 / 2540.0, loc);
      str_formatd(twidth, twidth + sizeof(twidth),
		      size->width * 72.0 / 2540.0, loc);
      str_formatd(tlength, tlength + sizeof(tlength),
		      size->length * 72.0 / 2540.0, loc);
      strlcpy(ppdsizename, size->media, sizeof(ppdsizename));
      if ((ippsizename = strchr(ppdsizename, ' ')) != NULL)
	*ippsizename = '\0';

      if (all_borderless) {
	suffix = strcasestr(ppdsizename, ".Borderless");
	if (suffix)
	  *suffix = '\0';
      }

      cupsFilePrintf(fp, "*ImageableArea %s: \"%s %s %s %s\"\n", ppdsizename,
		     tleft, tbottom, tright, ttop);
      cupsFilePrintf(fp, "*PaperDimension %s: \"%s %s\"\n", ppdsizename,
		     twidth, tlength);
    }

   /*
    * Custom size support...
    */

    if (max_width > 0 && min_width < INT_MAX && max_length > 0 &&
	min_length < INT_MAX) {
      char	tmax[256], tmin[256];	/* Min/max values */

      str_formatd(tleft, tleft + sizeof(tleft), left * 72.0 / 2540.0, loc);
      str_formatd(tbottom, tbottom + sizeof(tbottom),
		      bottom * 72.0 / 2540.0, loc);
      str_formatd(tright, tright + sizeof(tright), right * 72.0 / 2540.0,
		      loc);
      str_formatd(ttop, ttop + sizeof(ttop), top * 72.0 / 2540.0, loc);

      cupsFilePrintf(fp, "*HWMargins: \"%s %s %s %s\"\n", tleft, tbottom,
		     tright, ttop);

      str_formatd(tmax, tmax + sizeof(tmax), max_width * 72.0 / 2540.0,
		      loc);
      str_formatd(tmin, tmin + sizeof(tmin), min_width * 72.0 / 2540.0,
		      loc);
      cupsFilePrintf(fp, "*ParamCustomPageSize Width: 1 points %s %s\n", tmin,
		     tmax);

      str_formatd(tmax, tmax + sizeof(tmax), max_length * 72.0 / 2540.0,
		      loc);
      str_formatd(tmin, tmin + sizeof(tmin), min_length * 72.0 / 2540.0,
		      loc);
      cupsFilePrintf(fp, "*ParamCustomPageSize Height: 2 points %s %s\n", tmin,
		     tmax);

      cupsFilePuts(fp, "*ParamCustomPageSize WidthOffset: 3 points 0 0\n");
      cupsFilePuts(fp, "*ParamCustomPageSize HeightOffset: 4 points 0 0\n");
      cupsFilePuts(fp, "*ParamCustomPageSize Orientation: 5 int 0 3\n");
      cupsFilePuts(fp, "*CustomPageSize True: \"pop pop pop <</PageSize[5 -2 roll]/ImagingBBox null>>setpagedevice\"\n");
    }
  } else {
    cupsFilePrintf(fp,
		   "*%% Printer did not supply page size info via IPP, using defaults\n"
		   "*OpenUI *PageSize/Media Size: PickOne\n"
		   "*OrderDependency: 10 AnySetup *PageSize\n"
		   "*DefaultPageSize: Letter\n"
		   "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
		   "*PageSize Legal/US Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
		   "*PageSize Executive/Executive: \"<</PageSize[522 756]>>setpagedevice\"\n"
		   "*PageSize Tabloid/Tabloid: \"<</PageSize[792 1224]>>setpagedevice\"\n"
		   "*PageSize A3/A3: \"<</PageSize[842 1191]>>setpagedevice\"\n"
		   "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
		   "*PageSize A5/A5: \"<</PageSize[420 595]>>setpagedevice\"\n"
		   "*PageSize B5/JIS B5: \"<</PageSize[516 729]>>setpagedevice\"\n"
		   "*PageSize EnvISOB5/Envelope B5: \"<</PageSize[499 709]>>setpagedevice\"\n"
		   "*PageSize Env10/Envelope #10 : \"<</PageSize[297 684]>>setpagedevice\"\n"
		   "*PageSize EnvC5/Envelope C5: \"<</PageSize[459 649]>>setpagedevice\"\n"
		   "*PageSize EnvDL/Envelope DL: \"<</PageSize[312 624]>>setpagedevice\"\n"
		   "*PageSize EnvMonarch/Envelope Monarch: \"<</PageSize[279 540]>>setpagedevice\"\n"
		   "*CloseUI: *PageSize\n"
		   "*OpenUI *PageRegion/Media Size: PickOne\n"
		   "*OrderDependency: 10 AnySetup *PageRegion\n"
		   "*DefaultPageRegion: Letter\n"
		   "*PageRegion Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
		   "*PageRegion Legal/US Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
		   "*PageRegion Executive/Executive: \"<</PageSize[522 756]>>setpagedevice\"\n"
		   "*PageRegion Tabloid/Tabloid: \"<</PageSize[792 1224]>>setpagedevice\"\n"
		   "*PageRegion A3/A3: \"<</PageSize[842 1191]>>setpagedevice\"\n"
		   "*PageRegion A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
		   "*PageRegion A5/A5: \"<</PageSize[420 595]>>setpagedevice\"\n"
		   "*PageRegion B5/JIS B5: \"<</PageSize[516 729]>>setpagedevice\"\n"
		   "*PageRegion EnvISOB5/Envelope B5: \"<</PageSize[499 709]>>setpagedevice\"\n"
		   "*PageRegion Env10/Envelope #10 : \"<</PageSize[297 684]>>setpagedevice\"\n"
		   "*PageRegion EnvC5/Envelope C5: \"<</PageSize[459 649]>>setpagedevice\"\n"
		   "*PageRegion EnvDL/Envelope DL: \"<</PageSize[312 624]>>setpagedevice\"\n"
		   "*PageRegion EnvMonarch/Envelope Monarch: \"<</PageSize[279 540]>>setpagedevice\"\n"
		   "*CloseUI: *PageSize\n"
		   "*DefaultImageableArea: Letter\n"
		   "*ImageableArea Letter/US Letter: \"18 12 594 780\"\n"
		   "*ImageableArea Legal/US Legal: \"18 12 594 996\"\n"
		   "*ImageableArea Executive/Executive: \"18 12 504 744\"\n"
		   "*ImageableArea Tabloid/Tabloid: \"18 12 774 1212\"\n"
		   "*ImageableArea A3/A3: \"18 12 824 1179\"\n"
		   "*ImageableArea A4/A4: \"18 12 577 830\"\n"
		   "*ImageableArea A5/A5: \"18 12 402 583\"\n"
		   "*ImageableArea B5/JIS B5: \"18 12 498 717\"\n"
		   "*ImageableArea EnvISOB5/Envelope B5: \"18 12 481 697\"\n"
		   "*ImageableArea Env10/Envelope #10 : \"18 12 279 672\"\n"
		   "*ImageableArea EnvC5/Envelope C5: \"18 12 441 637\"\n"
		   "*ImageableArea EnvDL/Envelope DL: \"18 12 294 612\"\n"
		   "*ImageableArea EnvMonarch/Envelope Monarch: \"18 12 261 528\"\n"
		   "*DefaultPaperDimension: Letter\n"
		   "*PaperDimension Letter/US Letter: \"612 792\"\n"
		   "*PaperDimension Legal/US Legal: \"612 1008\"\n"
		   "*PaperDimension Executive/Executive: \"522 756\"\n"
		   "*PaperDimension Tabloid/Tabloid: \"792 1224\"\n"
		   "*PaperDimension A3/A3: \"842 1191\"\n"
		   "*PaperDimension A4/A4: \"595 842\"\n"
		   "*PaperDimension A5/A5: \"420 595\"\n"
		   "*PaperDimension B5/JIS B5: \"516 729\"\n"
		   "*PaperDimension EnvISOB5/Envelope B5: \"499 709\"\n"
		   "*PaperDimension Env10/Envelope #10 : \"297 684\"\n"
		   "*PaperDimension EnvC5/Envelope C5: \"459 649\"\n"
		   "*PaperDimension EnvDL/Envelope DL: \"312 624\"\n"
		   "*PaperDimension EnvMonarch/Envelope Monarch: \"279 540\"\n");
  }

  cupsArrayDelete(printer_sizes);

 /*
  * InputSlot...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-source",
			       IPP_TAG_KEYWORD)) != NULL)
    ppdPwgPpdizeName(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    ppdname[0] = '\0';

  if ((attr = ippFindAttribute(response, "media-source-supported",
			       IPP_TAG_KEYWORD)) != NULL &&
      (count = ippGetCount(attr)) > 1) {
    int have_default = ppdname[0] != '\0';
					/* Do we have a default InputSlot? */
    static const char * const sources[] =
    {					/* Standard "media-source" strings */
      "auto",
      "main",
      "alternate",
      "large-capacity",
      "manual",
      "envelope",
      "disc",
      "photo",
      "hagaki",
      "main-roll",
      "alternate-roll",
      "top",
      "middle",
      "bottom",
      "side",
      "left",
      "right",
      "center",
      "rear",
      "by-pass-tray",
      "tray-1",
      "tray-2",
      "tray-3",
      "tray-4",
      "tray-5",
      "tray-6",
      "tray-7",
      "tray-8",
      "tray-9",
      "tray-10",
      "tray-11",
      "tray-12",
      "tray-13",
      "tray-14",
      "tray-15",
      "tray-16",
      "tray-17",
      "tray-18",
      "tray-19",
      "tray-20",
      "roll-1",
      "roll-2",
      "roll-3",
      "roll-4",
      "roll-5",
      "roll-6",
      "roll-7",
      "roll-8",
      "roll-9",
      "roll-10"
    };

    human_readable = lookup_option("media-source", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*OpenUI *InputSlot/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *InputSlot\n",
		   (human_readable ? human_readable : "Media Source"));
    if (have_default)
      cupsFilePrintf(fp, "*DefaultInputSlot: %s\n", ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++) {
      keyword = ippGetString(attr, i, NULL);

      ppdPwgPpdizeName(keyword, ppdname, sizeof(ppdname));

      if (i == 0 && !have_default)
	cupsFilePrintf(fp, "*DefaultInputSlot: %s\n", ppdname);

      human_readable = lookup_choice((char *)keyword, "media-source",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      for (j = (int)(sizeof(sources) / sizeof(sources[0])) - 1; j >= 0; j --)
        if (!strcmp(sources[j], keyword))
	  break;
      if (j >= 0)
	cupsFilePrintf(fp, "*InputSlot %s%s%s: \"<</MediaPosition %d>>setpagedevice\"\n",
		       ppdname,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""), j);
      else
	cupsFilePrintf(fp, "*InputSlot %s%s%s: \"\"\n",
		       ppdname,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
    }
    cupsFilePuts(fp, "*CloseUI: *InputSlot\n");
  }

 /*
  * MediaType...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-type",
			       IPP_TAG_ZERO)) != NULL)
    ppdPwgPpdizeName(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "media-type-supported",
			       IPP_TAG_ZERO)) != NULL &&
      (count = ippGetCount(attr)) > 1) {
    human_readable = lookup_option("media-type", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*OpenUI *MediaType/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *MediaType\n"
		   "*DefaultMediaType: %s\n",
		   (human_readable ? human_readable : "Media Type"),
		   ppdname);
    for (i = 0; i < count; i ++) {
      keyword = ippGetString(attr, i, NULL);

      ppdPwgPpdizeName(keyword, ppdname, sizeof(ppdname));

      human_readable = lookup_choice((char *)keyword, "media-type",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*MediaType %s%s%s: \"<</MediaType(%s)>>setpagedevice\"\n",
		     ppdname,
		     (human_readable ? "/" : ""),
		     (human_readable ? human_readable : ""),
		     ppdname);
    }
    cupsFilePuts(fp, "*CloseUI: *MediaType\n");
  }

 /*
  * ColorModel...
  */

  if ((attr = ippFindAttribute(response, "print-color-mode-supported",
			       IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(response, "output-mode-supported",
			    IPP_TAG_KEYWORD);

  human_readable = lookup_option("print-color-mode", opt_strings_catalog,
				 printer_opt_strings_catalog);
  if (attr && ippGetCount(attr) > 0) {
    const char *default_color = NULL;	/* Default */
    int first_choice = 1;

    cupsFilePrintf(fp, "*%% ColorModel from %s\n", ippGetName(attr));

    for (i = 0, count = ippGetCount(attr); i < count; i ++) {
      keyword = ippGetString(attr, i, NULL); /* Keyword for color/bit depth */

      if (!strcmp(keyword, "auto")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("auto", "print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel Auto/%s: \"\"\n",
		       (human_readable2 ? human_readable2 : "Automatic"));

	default_color = "Auto";
      } else if (!strcmp(keyword, "bi-level")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("bi-level", "print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel FastGray/%s: \"\"\n",
		       (human_readable2 ? human_readable2 : "Text"));

        if (!default_color)
	  default_color = "FastGray";
      } else if (!strcmp(keyword, "process-bi-level")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("process-bi-level", "print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel ProcessFastGray/%s: \"\"\n",
		       (human_readable2 ? human_readable2 : "Process Text"));
      } else if (!strcmp(keyword, "auto-monochrome")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("auto-monochrome", "print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel AutoGray/%s: \"\"\n",
		       (human_readable2 ? human_readable2 : "Auto Monochrome"));
      } else if (!strcmp(keyword, "monochrome")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("monochrome", "print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel Gray/%s: \"\"\n",
		       (human_readable2 ? human_readable2 : "Monochrome"));

        if (!default_color || !strcmp(default_color, "FastGray"))
	  default_color = "Gray";
      } else if (!strcmp(keyword, "process-monochrome")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("process-monochrome",
					"print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel ProcessGray/%s: \"\"\n",
		       (human_readable2 ? human_readable2 :
			"Process Monochrome"));
      } else if (!strcmp(keyword, "color")) {
        if (first_choice) {
	  first_choice = 0;
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *ColorModel\n",
			 (human_readable ? human_readable : "Color Mode"));
	}

	human_readable2 = lookup_choice("color", "print-color-mode",
					opt_strings_catalog,
					printer_opt_strings_catalog);
        cupsFilePrintf(fp, "*ColorModel RGB/%s: \"\"\n",
		       (human_readable2 ? human_readable2 : "Color"));

        if (!default_color || strcmp(default_color, "Auto"))
	  default_color = "RGB";

	/* Apparently some printers only advertise color support, so make sure
           we also do grayscale for these printers... */
	if (!ippContainsString(attr, "monochrome") &&
	    !ippContainsString(attr, "auto-monochrome") &&
	    !ippContainsString(attr, "process-monochrome") &&
	    !ippContainsString(attr, "bi-level") &&
	    !ippContainsString(attr, "process-bi-level")) {
	  human_readable2 = lookup_choice("monochrome", "print-color-mode",
					  opt_strings_catalog,
					  printer_opt_strings_catalog);
	  cupsFilePrintf(fp, "*ColorModel Gray/%s: \"\"\n",
			 (human_readable2 ? human_readable2 : "Grayscale"));
	}
      }
    }

    if (default_pagesize != NULL) {
      /* Here we are dealing with a cluster, if the default cluster color
         is not supplied we set it Gray*/
      if (default_cluster_color != NULL) {
	default_color = default_cluster_color;
      } else
	default_color = "Gray";
    }

    if (default_color)
      cupsFilePrintf(fp, "*DefaultColorModel: %s\n", default_color);
    if (!first_choice)
      cupsFilePuts(fp, "*CloseUI: *ColorModel\n");
  } else {
    cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *ColorModel\n",
		   (human_readable ? human_readable : "Color Mode"));
    cupsFilePrintf(fp, "*DefaultColorModel: Gray\n");
    cupsFilePuts(fp, "*ColorModel FastGray/Fast Grayscale: \"\"\n");
    cupsFilePuts(fp, "*ColorModel Gray/Grayscale: \"\"\n");
    if (color) {
      /* Color printer according to DNS-SD (or unknown) */
      cupsFilePuts(fp, "*ColorModel RGB/Color: \"\"\n");
    }
    cupsFilePuts(fp, "*CloseUI: *ColorModel\n");
  }

 /*
  * Duplex...
  */

  if (((attr = ippFindAttribute(response, "sides-supported",
				IPP_TAG_KEYWORD)) != NULL &&
       ippContainsString(attr, "two-sided-long-edge")) ||
      (attr == NULL && duplex)) {
    human_readable = lookup_option("sides", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*OpenUI *Duplex/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *Duplex\n"
		   "*DefaultDuplex: None\n",
		   (human_readable ? human_readable : "2-Sided Printing"));
    human_readable = lookup_choice("one-sided", "sides", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*Duplex None/%s: \"<</Duplex false>>setpagedevice\"\n",
		   (human_readable ? human_readable : "Off"));
    human_readable = lookup_choice("two-sided-long-edge", "sides",
				   opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*Duplex DuplexNoTumble/%s: \"<</Duplex true/Tumble false>>setpagedevice\"\n",
		   (human_readable ? human_readable : "On (Portrait)"));
    human_readable = lookup_choice("two-sided-short-edge", "sides",
				   opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*Duplex DuplexTumble/%s: \"<</Duplex true/Tumble true>>setpagedevice\"\n",
		   (human_readable ? human_readable : "On (Landscape)"));
    cupsFilePrintf(fp, "*CloseUI: *Duplex\n");

    if ((attr = ippFindAttribute(response, "urf-supported",
				 IPP_TAG_KEYWORD)) != NULL) {
      for (i = 0, count = ippGetCount(attr); i < count; i ++) {
	const char *dm = ippGetString(attr, i, NULL); /* DM value */

	if (!strcasecmp(dm, "DM1")) {
	  cupsFilePuts(fp, "*cupsBackSide: Normal\n");
	  break;
	} else if (!strcasecmp(dm, "DM2")) {
	  cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
	  break;
	} else if (!strcasecmp(dm, "DM3")) {
	  cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
	  break;
	} else if (!strcasecmp(dm, "DM4")) {
	  cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
	  break;
	}
      }
    } else if ((attr = ippFindAttribute(response,
					"pwg-raster-document-sheet-back",
					IPP_TAG_KEYWORD)) != NULL) {
      keyword = ippGetString(attr, 0, NULL); /* Keyword value */

      if (!strcmp(keyword, "flipped"))
        cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
      else if (!strcmp(keyword, "manual-tumble"))
        cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
      else if (!strcmp(keyword, "normal"))
        cupsFilePuts(fp, "*cupsBackSide: Normal\n");
      else
        cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
    }
  }

 /*
  * Output bin...
  */

  if ((attr = ippFindAttribute(response, "output-bin-default",
			       IPP_TAG_ZERO)) != NULL)
    ppdPwgPpdizeName(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "output-bin-supported",
			       IPP_TAG_ZERO)) != NULL &&
      (count = ippGetCount(attr)) > 0) {
    human_readable = lookup_option("output-bin", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*OpenUI *OutputBin/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *OutputBin\n"
		   "*DefaultOutputBin: %s\n",
		   (human_readable ? human_readable : "Output Bin"),
		   ppdname);
    attr2 = ippFindAttribute(response, "printer-output-tray", IPP_TAG_STRING);
    for (i = 0; i < count; i ++) {
      keyword = ippGetString(attr, i, NULL);

      ppdPwgPpdizeName(keyword, ppdname, sizeof(ppdname));

      human_readable = lookup_choice((char *)keyword, "output-bin",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OutputBin %s%s%s: \"\"\n",
		     ppdname,
		     (human_readable ? "/" : ""),
		     (human_readable ? human_readable : ""));
      outputorderinfofound = 0;
      faceupdown = 1;
      firsttolast = 1;
      if (attr2 && i < ippGetCount(attr2)) {
	outbin_properties_octet = ippGetOctetString(attr2, i, &octet_str_len);
	memset(outbin_properties, 0, sizeof(outbin_properties));
	memcpy(outbin_properties, outbin_properties_octet,
	       ((size_t)octet_str_len < sizeof(outbin_properties) - 1 ?
		(size_t)octet_str_len : sizeof(outbin_properties) - 1));
	if (strcasestr(outbin_properties, "pagedelivery=faceUp")) {
	  outputorderinfofound = 1;
	  faceupdown = -1;
	} else if (strcasestr(outbin_properties, "pagedelivery=faceDown")) {
	  outputorderinfofound = 1;
	  faceupdown = 1;
	}
	if (strcasestr(outbin_properties, "stackingorder=lastToFirst")) {
	  outputorderinfofound = 1;
	  firsttolast = -1;
	} else if (strcasestr(outbin_properties, "stackingorder=firstToLast")) {
	  outputorderinfofound = 1;
	  firsttolast = 1;
	}
      }
      if (outputorderinfofound == 0) {
	if (strcasestr(keyword, "face-up")) {
	  outputorderinfofound = 1;
	  faceupdown = -1;
	}
	if (strcasestr(keyword, "face-down")) {
	  outputorderinfofound = 1;
	  faceupdown = 1;
	}
      }
      if (outputorderinfofound)
	cupsFilePrintf(fp, "*PageStackOrder %s: %s\n",
		       ppdname,
		       (firsttolast * faceupdown < 0 ? "Reverse" : "Normal"));
    }
    cupsFilePuts(fp, "*CloseUI: *OutputBin\n");
  }

 /*
  * Finishing options...
  */

  if ((attr = ippFindAttribute(response, "finishings-supported",
			       IPP_TAG_ENUM)) != NULL) {
    int			value;		/* Enum value */
    const char		*ppd_keyword;	/* PPD keyword for enum */
    cups_array_t	*names;		/* Names we've added */
    static const char * const base_keywords[] =
    {					/* Base STD 92 keywords */
      NULL,				/* none */
      "SingleAuto",			/* staple */
      "SingleAuto",			/* punch */
      NULL,				/* cover */
      "BindAuto",			/* bind */
      "SaddleStitch",			/* saddle-stitch */
      "EdgeStitchAuto",			/* edge-stitch */
      "Auto",				/* fold */
      NULL,				/* trim */
      NULL,				/* bale */
      NULL,				/* booklet-maker */
      NULL,				/* jog-offset */
      NULL,				/* coat */
      NULL				/* laminate */
    };

    count = ippGetCount(attr);
    names = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0,
			  (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
    fin_options = cupsArrayNew((cups_array_func_t)strcmp, NULL);

   /*
    * Staple/Bind/Stitch
    */

    for (i = 0; i < count; i ++) {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strncmp(keyword, "staple-", 7) ||
	  !strncmp(keyword, "bind-", 5) ||
	  !strncmp(keyword, "edge-stitch-", 12) ||
	  !strcmp(keyword, "saddle-stitch"))
        break;
    }

    if (i < count) {
      static const char * const staple_keywords[] =
      {					/* StapleLocation keywords */
	"SinglePortrait",
	"SingleRevLandscape",
	"SingleLandscape",
	"SingleRevPortrait",
	"EdgeStitchPortrait",
	"EdgeStitchLandscape",
	"EdgeStitchRevPortrait",
	"EdgeStitchRevLandscape",
	"DualPortrait",
	"DualLandscape",
	"DualRevPortrait",
	"DualRevLandscape",
	"TriplePortrait",
	"TripleLandscape",
	"TripleRevPortrait",
	"TripleRevLandscape"
      };
      static const char * const bind_keywords[] =
      {					/* StapleLocation binding keywords */
	"BindPortrait",
	"BindLandscape",
	"BindRevPortrait",
	"BindRevLandscape"
      };

      cupsArrayAdd(fin_options, "*StapleLocation");

      human_readable = lookup_choice("staple", "finishing-template",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *StapleLocation/%s: PickOne\n",
		     (human_readable ? human_readable : "Staple"));
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *StapleLocation\n");
      cupsFilePuts(fp, "*DefaultStapleLocation: None\n");
      human_readable = lookup_choice("3", "finishings",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*StapleLocation None/%s: \"\"\n",
		     (human_readable ? human_readable : "None"));

      for (; i < count; i ++) {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

        if (strncmp(keyword, "staple-", 7) &&
	    strncmp(keyword, "bind-", 5) &&
	    strncmp(keyword, "edge-stitch-", 12) &&
	    strcmp(keyword, "saddle-stitch"))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue; /* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

        if (value >= IPP_FINISHINGS_NONE && value <= IPP_FINISHINGS_LAMINATE)
          ppd_keyword = base_keywords[value - IPP_FINISHINGS_NONE];
        else if (value >= IPP_FINISHINGS_STAPLE_TOP_LEFT &&
		 value <= IPP_FINISHINGS_STAPLE_TRIPLE_BOTTOM)
          ppd_keyword = staple_keywords[value - IPP_FINISHINGS_STAPLE_TOP_LEFT];
        else if (value >= IPP_FINISHINGS_BIND_LEFT &&
		 value <= IPP_FINISHINGS_BIND_BOTTOM)
          ppd_keyword = bind_keywords[value - IPP_FINISHINGS_BIND_LEFT];
        else
          ppd_keyword = NULL;

        if (!ppd_keyword)
          continue;

	snprintf(buf, sizeof(buf), "%d", value);
	human_readable = lookup_choice(buf, "finishings", opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*StapleLocation %s%s%s: \"\"\n", ppd_keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*StapleLocation %s\"\n",
		       value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *StapleLocation\n");
    }

   /*
    * Fold
    */

    for (i = 0; i < count; i ++) {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strncmp(keyword, "cups-fold-", 10) ||
	  !strcmp(keyword, "fold") ||
	  !strncmp(keyword, "fold-", 5))
        break;
    }

    if (i < count) {
      static const char * const fold_keywords[] =
      {					/* FoldType keywords */
	"Accordion",
	"DoubleGate",
	"Gate",
	"Half",
	"HalfZ",
	"LeftGate",
	"Letter",
	"Parallel",
	"XFold",
	"RightGate",
	"ZFold",
	"EngineeringZ"
      };

      cupsArrayAdd(fin_options, "*FoldType");

      human_readable = lookup_choice("fold", "finishing-template",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *FoldType/%s: PickOne\n",
		     (human_readable ? human_readable : "Fold"));
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *FoldType\n");
      cupsFilePuts(fp, "*DefaultFoldType: None\n");
      human_readable = lookup_choice("3", "finishings",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*FoldType None/%s: \"\"\n",
		     (human_readable ? human_readable : "None"));

      for (; i < count; i ++) {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

        if (!strncmp(keyword, "cups-fold-", 10))
          keyword += 5;
        else if (strcmp(keyword, "fold") && strncmp(keyword, "fold-", 5))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue; /* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

        if (value >= IPP_FINISHINGS_NONE && value <= IPP_FINISHINGS_LAMINATE)
          ppd_keyword = base_keywords[value - IPP_FINISHINGS_NONE];
        else if (value >= IPP_FINISHINGS_FOLD_ACCORDION &&
		 value <= IPP_FINISHINGS_FOLD_ENGINEERING_Z)
          ppd_keyword = fold_keywords[value - IPP_FINISHINGS_FOLD_ACCORDION];
        else if (value >= IPP_FINISHINGS_CUPS_FOLD_ACCORDION &&
		 value <= IPP_FINISHINGS_CUPS_FOLD_Z)
          ppd_keyword = fold_keywords[value -
				      IPP_FINISHINGS_CUPS_FOLD_ACCORDION];
        else
          ppd_keyword = NULL;

        if (!ppd_keyword)
          continue;

	snprintf(buf, sizeof(buf), "%d", value);
	human_readable = lookup_choice(buf, "finishings", opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*FoldType %s%s%s: \"\"\n", ppd_keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*FoldType %s\"\n",
		       value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *FoldType\n");
    }

   /*
    * Punch
    */

    for (i = 0; i < count; i ++) {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strncmp(keyword, "cups-punch-", 11) ||
	  !strncmp(keyword, "punch-", 6))
        break;
    }

    if (i < count) {
      static const char * const punch_keywords[] =
      {					/* PunchMedia keywords */
	"SinglePortrait",
	"SingleRevLandscape",
	"SingleLandscape",
	"SingleRevPortrait",
	"DualPortrait",
	"DualLandscape",
	"DualRevPortrait",
	"DualRevLandscape",
	"TriplePortrait",
	"TripleLandscape",
	"TripleRevPortrait",
	"TripleRevLandscape",
	"QuadPortrait",
	"QuadLandscape",
	"QuadRevPortrait",
	"QuadRevLandscape",
	"MultiplePortrait",
	"MultipleLandscape",
	"MultipleRevPortrait",
	"MultipleRevLandscape"
      };

      cupsArrayAdd(fin_options, "*PunchMedia");

      human_readable = lookup_choice("punch", "finishing-template",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *PunchMedia/%s: PickOne\n",
		     (human_readable ? human_readable : "Punch"));
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *PunchMedia\n");
      cupsFilePuts(fp, "*DefaultPunchMedia: None\n");
      human_readable = lookup_choice("3", "finishings",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*PunchMedia None/%s: \"\"\n",
		     (human_readable ? human_readable : "None"));

      for (i = 0; i < count; i ++) {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

        if (!strncmp(keyword, "cups-punch-", 11))
          keyword += 5;
        else if (strncmp(keyword, "punch-", 6))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue; /* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

        if (value >= IPP_FINISHINGS_NONE && value <= IPP_FINISHINGS_LAMINATE)
          ppd_keyword = base_keywords[value - IPP_FINISHINGS_NONE];
        else if (value >= IPP_FINISHINGS_PUNCH_TOP_LEFT &&
		 value <= IPP_FINISHINGS_PUNCH_MULTIPLE_BOTTOM)
          ppd_keyword = punch_keywords[value - IPP_FINISHINGS_PUNCH_TOP_LEFT];
        else if (value >= IPP_FINISHINGS_CUPS_PUNCH_TOP_LEFT &&
		 value <= IPP_FINISHINGS_CUPS_PUNCH_QUAD_BOTTOM)
          ppd_keyword = punch_keywords[value -
				       IPP_FINISHINGS_CUPS_PUNCH_TOP_LEFT];
        else
          ppd_keyword = NULL;

        if (!ppd_keyword)
          continue;

	snprintf(buf, sizeof(buf), "%d", value);
	human_readable = lookup_choice(buf, "finishings", opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*PunchMedia %s%s%s: \"\"\n", ppd_keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*PunchMedia %s\"\n",
		       value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *PunchMedia\n");
    }

   /*
    * Booklet
    */

    if (ippContainsInteger(attr, IPP_FINISHINGS_BOOKLET_MAKER)) {
      cupsArrayAdd(fin_options, "*Booklet");

      human_readable = lookup_choice("booklet-maker", "finishing-template",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *Booklet/%s: Boolean\n",
		     (human_readable ? human_readable : "Booklet"));
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *Booklet\n");
      cupsFilePuts(fp, "*DefaultBooklet: False\n");
      cupsFilePuts(fp, "*Booklet False: \"\"\n");
      cupsFilePuts(fp, "*Booklet True: \"\"\n");
      cupsFilePrintf(fp, "*cupsIPPFinishings %d/booklet-maker: \"*Booklet True\"\n",
		     IPP_FINISHINGS_BOOKLET_MAKER);
      cupsFilePuts(fp, "*CloseUI: *Booklet\n");
    }

   /*
    * CutMedia
    */

    for (i = 0; i < count; i ++) {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strcmp(keyword, "trim") || !strncmp(keyword, "trim-", 5))
        break;
    }

    if (i < count) {
      static const char * const trim_keywords[] =
      {				/* CutMedia keywords */
        "EndOfPage",
        "EndOfDoc",
        "EndOfSet",
        "EndOfJob"
      };

      cupsArrayAdd(fin_options, "*CutMedia");

      human_readable = lookup_choice("trim", "finishing-template",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *CutMedia/%s: PickOne\n",
		     (human_readable ? human_readable : "Cut"));
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *CutMedia\n");
      cupsFilePuts(fp, "*DefaultCutMedia: None\n");
      human_readable = lookup_choice("3", "finishings",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*CutMedia None/%s: \"\"\n",
		     (human_readable ? human_readable : "None"));

      for (i = 0; i < count; i ++) {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

	if (strcmp(keyword, "trim") && strncmp(keyword, "trim-", 5))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue; /* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

        if (value == IPP_FINISHINGS_TRIM)
          ppd_keyword = "Auto";
	else
	  ppd_keyword = trim_keywords[value - IPP_FINISHINGS_TRIM_AFTER_PAGES];

	snprintf(buf, sizeof(buf), "%d", value);
	human_readable = lookup_choice(buf, "finishings", opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*CutMedia %s%s%s: \"\"\n", ppd_keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*CutMedia %s\"\n",
		       value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *CutMedia\n");
    }

    cupsArrayDelete(names);
  }

  if ((attr = ippFindAttribute(response, "finishings-col-database",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL) {
    ipp_t	*finishing_col;		/* Current finishing collection */
    ipp_attribute_t *finishing_attr;	/* Current finishing member attribute */
    cups_array_t *templates;		/* Finishing templates */

    human_readable = lookup_option("finishing-template", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*OpenUI *cupsFinishingTemplate/%s: PickOne\n",
		   (human_readable ? human_readable : "Finishing Template"));
    cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *cupsFinishingTemplate\n");
    cupsFilePuts(fp, "*DefaultcupsFinishingTemplate: none\n");
    human_readable = lookup_choice("3", "finishings",
				   opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*cupsFinishingTemplate None/%s: \"\"\n",
		   (human_readable ? human_readable : "None"));

    templates = cupsArrayNew((cups_array_func_t)strcmp, NULL);
    count     = ippGetCount(attr);

    for (i = 0; i < count; i ++) {
      finishing_col = ippGetCollection(attr, i);
      keyword = ippGetString(ippFindAttribute(finishing_col,
					      "finishing-template",
					      IPP_TAG_ZERO), 0, NULL);

      if (!keyword || cupsArrayFind(templates, (void *)keyword))
        continue;

      if (!strcmp(keyword, "none"))
        continue;

      cupsArrayAdd(templates, (void *)keyword);

      human_readable = lookup_choice((char *)keyword, "finishing-template",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      if (human_readable == NULL)
	human_readable = (char *)keyword;
      cupsFilePrintf(fp, "*cupsFinishingTemplate %s/%s: \"\n", keyword,
		     human_readable);
      for (finishing_attr = ippFirstAttribute(finishing_col); finishing_attr;
	   finishing_attr = ippNextAttribute(finishing_col)) {
        if (ippGetValueTag(finishing_attr) == IPP_TAG_BEGIN_COLLECTION) {
	  const char *name = ippGetName(finishing_attr);
					/* Member attribute name */

          if (strcmp(name, "media-size"))
            cupsFilePrintf(fp, "%% %s\n", name);
	}
      }
      cupsFilePuts(fp, "\"\n");
      cupsFilePuts(fp, "*End\n");
    }

    cupsFilePuts(fp, "*CloseUI: *cupsFinishingTemplate\n");

    if (cupsArrayCount(fin_options)) {
      const char	*fin_option;	/* Current finishing option */

      cupsFilePuts(fp, "*cupsUIConstraint finishing-template: \"*cupsFinishingTemplate");
      for (fin_option = (const char *)cupsArrayFirst(fin_options); fin_option;
	   fin_option = (const char *)cupsArrayNext(fin_options))
        cupsFilePrintf(fp, " %s", fin_option);
      cupsFilePuts(fp, "\"\n");

      cupsFilePuts(fp, "*cupsUIResolver finishing-template: \"*cupsFinishingTemplate None");
      for (fin_option = (const char *)cupsArrayFirst(fin_options); fin_option;
	   fin_option = (const char *)cupsArrayNext(fin_options))
        cupsFilePrintf(fp, " %s None", fin_option);
      cupsFilePuts(fp, "\"\n");
    }

    cupsArrayDelete(templates);
  }

  cupsArrayDelete(fin_options);

 /*
  * DefaultResolution...
  */

  xres = common_def->x;
  yres = common_def->y;
  if (xres == yres)
    cupsFilePrintf(fp, "*DefaultResolution: %ddpi\n", xres);
  else
    cupsFilePrintf(fp, "*DefaultResolution: %dx%ddpi\n", xres, yres);

 /*
  * cupsPrintQuality...
  */

  if ((quality =
       ippFindAttribute(response, "print-quality-supported",
			IPP_TAG_ENUM)) != NULL) {
    human_readable = lookup_option("print-quality", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*OpenUI *cupsPrintQuality/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
		   "*DefaultcupsPrintQuality: Normal\n",
		   (human_readable ? human_readable : "Print Quality"));
    if (ippContainsInteger(quality, IPP_QUALITY_DRAFT)) {
      human_readable = lookup_choice("3", "print-quality", opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*cupsPrintQuality Draft/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n",
		     (human_readable ? human_readable : "Draft"),
		     min_res->x, min_res->y);
    }
    human_readable = lookup_choice("4", "print-quality", opt_strings_catalog,
				   printer_opt_strings_catalog);
    cupsFilePrintf(fp, "*cupsPrintQuality Normal/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n",
		   (human_readable ? human_readable : "Normal"),
		   common_def->x, common_def->y);
    if (ippContainsInteger(quality, IPP_QUALITY_HIGH)) {
      human_readable = lookup_choice("5", "print-quality", opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*cupsPrintQuality High/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n",
		     (human_readable ? human_readable : "High"),
		     max_res->x, max_res->y);
    }
    cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
  }

  /* Only add these options if jobs get sent to the printer as PDF,
     PWG Raster, or Apple Raster, as only then arbitrary IPP
     attributes get passed through from the filter command line
     to the printer by the "ipp" CUPS backend. */
  if (is_pdf || is_pwg || is_apple) {
    /*
     * Print Optimization ...
     */

    if ((attr = ippFindAttribute(response, "print-content-optimize-default",
				 IPP_TAG_ZERO)) != NULL)
      strlcpy(ppdname, ippGetString(attr, 0, NULL), sizeof(ppdname));
    else
      strlcpy(ppdname, "auto", sizeof(ppdname));

    if ((attr = ippFindAttribute(response, "print-content-optimize-supported",
				 IPP_TAG_ZERO)) != NULL &&
	(count = ippGetCount(attr)) > 1) {
      human_readable = lookup_option("print-content-optimize",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *print-content-optimize/%s: PickOne\n"
		     "*OrderDependency: 10 AnySetup *print-content-optimize\n"
		     "*Defaultprint-content-optimize: %s\n",
		     (human_readable ? human_readable : "Print Optimization"),
		     ppdname);
      for (i = 0; i < count; i ++) {
	keyword = ippGetString(attr, i, NULL);

	human_readable = lookup_choice((char *)keyword,
				       "print-content-optimize",
				       opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*print-content-optimize %s%s%s: \"\"\n",
		       keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
      }
      cupsFilePuts(fp, "*CloseUI: *print-content-optimize\n");
    }

    /*
     * Print Rendering Intent ...
     */

    if ((attr = ippFindAttribute(response, "print-rendering-intent-default",
				 IPP_TAG_ZERO)) != NULL)
      strlcpy(ppdname, ippGetString(attr, 0, NULL), sizeof(ppdname));
    else
      strlcpy(ppdname, "auto", sizeof(ppdname));

    if ((attr = ippFindAttribute(response, "print-rendering-intent-supported",
				 IPP_TAG_ZERO)) != NULL &&
	(count = ippGetCount(attr)) > 1) {
      human_readable = lookup_option("print-rendering-intent",
				     opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *print-rendering-intent/%s: PickOne\n"
		     "*OrderDependency: 10 AnySetup *print-rendering-intent\n"
		     "*Defaultprint-rendering-intent: %s\n",
		     (human_readable ? human_readable :
		      "Print Rendering Intent"),
		     ppdname);
      for (i = 0; i < count; i ++) {
	keyword = ippGetString(attr, i, NULL);

	human_readable = lookup_choice((char *)keyword,
				       "print-rendering-intent",
				       opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*print-rendering-intent %s%s%s: \"\"\n",
		       keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
      }
      cupsFilePuts(fp, "*CloseUI: *print-rendering-intent\n");
    }

    /*
     * Print Scaling ...
     */

    if ((attr = ippFindAttribute(response, "print-scaling-default",
				 IPP_TAG_ZERO)) != NULL)
      strlcpy(ppdname, ippGetString(attr, 0, NULL), sizeof(ppdname));
    else
      strlcpy(ppdname, "auto", sizeof(ppdname));

    if ((attr = ippFindAttribute(response, "print-scaling-supported",
				 IPP_TAG_ZERO)) != NULL &&
	(count = ippGetCount(attr)) > 1) {
      human_readable = lookup_option("print-scaling", opt_strings_catalog,
				     printer_opt_strings_catalog);
      cupsFilePrintf(fp, "*OpenUI *print-scaling/%s: PickOne\n"
		     "*OrderDependency: 10 AnySetup *print-scaling\n"
		     "*Defaultprint-scaling: %s\n",
		     (human_readable ? human_readable : "Print Scaling"),
		     ppdname);
      for (i = 0; i < count; i ++) {
	keyword = ippGetString(attr, i, NULL);

	human_readable = lookup_choice((char *)keyword, "print-scaling",
				       opt_strings_catalog,
				       printer_opt_strings_catalog);
	cupsFilePrintf(fp, "*print-scaling %s%s%s: \"\"\n",
		       keyword,
		       (human_readable ? "/" : ""),
		       (human_readable ? human_readable : ""));
      }
      cupsFilePuts(fp, "*CloseUI: *print-scaling\n");
    }
  }

 /*
  * Phone Options for Fax..
  */

  if (is_fax) {
    human_readable = lookup_option("Phone", opt_strings_catalog,
				   printer_opt_strings_catalog);

    cupsFilePrintf(fp, "*OpenUI *phone/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *phone\n"
		   "*Defaultphone: None\n"
		   "*phone None: \"\"\n"
		   "*CloseUI: *phone\n",
		   (human_readable ? human_readable : "Phone Number"));
    cupsFilePrintf(fp,"*Customphone True: \"\"\n"
		   "*ParamCustomphone Text: 1 string 0 64\n");

    human_readable = lookup_option("faxPrefix", opt_strings_catalog,
				   printer_opt_strings_catalog);

    cupsFilePrintf(fp, "*OpenUI *faxPrefix/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *faxPrefix\n"
		   "*DefaultfaxPrefix: None\n"
		   "*faxPrefix None: \"\"\n"
		   "*CloseUI: *faxPrefix\n",
		   (human_readable ? human_readable : "Pre-Dial Number"));
    cupsFilePrintf(fp,"*CustomfaxPrefix True: \"\"\n"
		   "*ParamCustomfaxPrefix Text: 1 string 0 64\n");
  }

 /*
  * Presets...
  */

  if ((attr = ippFindAttribute(response, "job-presets-supported",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL) {
    for (i = 0, count = ippGetCount(attr); i < count; i ++) {
      ipp_t	*preset = ippGetCollection(attr, i); /* Preset collection */
      const char *preset_name =         /* Preset name */
	ippGetString(ippFindAttribute(preset,
				      "preset-name", IPP_TAG_ZERO), 0, NULL),
		 *localized_name;	/* Localized preset name */
      ipp_attribute_t *member;		/* Member attribute in preset */
      const char *member_name;		/* Member attribute name */
      char       member_value[256];	/* Member attribute value */

      if (!preset || !preset_name)
        continue;

      if ((localized_name = lookup_option((char *)preset_name,
					  opt_strings_catalog,
					  printer_opt_strings_catalog)) == NULL)
        cupsFilePrintf(fp, "*APPrinterPreset %s: \"\n", preset_name);
      else
        cupsFilePrintf(fp, "*APPrinterPreset %s/%s: \"\n", preset_name,
		       localized_name);

      for (member = ippFirstAttribute(preset); member;
	   member = ippNextAttribute(preset)) {
        member_name = ippGetName(member);

        if (!member_name || !strcmp(member_name, "preset-name"))
          continue;

        if (!strcmp(member_name, "finishings")) {
	  for (i = 0, count = ippGetCount(member); i < count; i ++) {
	    const char *option = NULL;	/* PPD option name */

	    keyword = ippEnumString("finishings", ippGetInteger(member, i));

	    if (!strcmp(keyword, "booklet-maker")) {
	      option  = "Booklet";
	      keyword = "True";
	    } else if (!strncmp(keyword, "fold-", 5))
	      option = "FoldType";
	    else if (!strncmp(keyword, "punch-", 6))
	      option = "PunchMedia";
	    else if (!strncmp(keyword, "bind-", 5) ||
		     !strncmp(keyword, "edge-stitch-", 12) ||
		     !strcmp(keyword, "saddle-stitch") ||
		     !strncmp(keyword, "staple-", 7))
	      option = "StapleLocation";

	    if (option && keyword)
	      cupsFilePrintf(fp, "*%s %s\n", option, keyword);
	  }
        } else if (!strcmp(member_name, "finishings-col")) {
          ipp_t *fin_col;		/* finishings-col value */

          for (i = 0, count = ippGetCount(member); i < count; i ++) {
            fin_col = ippGetCollection(member, i);

            if ((keyword =
		 ippGetString(ippFindAttribute(fin_col,
					       "finishing-template",
					       IPP_TAG_ZERO), 0, NULL)) != NULL)
              cupsFilePrintf(fp, "*cupsFinishingTemplate %s\n", keyword);
          }
        } else if (!strcmp(member_name, "media")) {
         /*
          * Map media to PageSize...
          */

          if ((pwg = pwgMediaForPWG(ippGetString(member, 0, NULL))) != NULL &&
	      pwg->ppd)
            cupsFilePrintf(fp, "*PageSize %s\n", pwg->ppd);
        } else if (!strcmp(member_name, "media-col")) {
          media_col = ippGetCollection(member, 0);

          if ((media_size =
	       ippGetCollection(ippFindAttribute(media_col,
						 "media-size",
						 IPP_TAG_BEGIN_COLLECTION),
				0)) != NULL) {
            x_dim = ippFindAttribute(media_size, "x-dimension",
				     IPP_TAG_INTEGER);
            y_dim = ippFindAttribute(media_size, "y-dimension",
				     IPP_TAG_INTEGER);
            if ((pwg = pwgMediaForSize(ippGetInteger(x_dim, 0),
				       ippGetInteger(y_dim, 0))) != NULL &&
		pwg->ppd)
	      cupsFilePrintf(fp, "*PageSize %s\n", pwg->ppd);
          }

          if ((keyword = ippGetString(ippFindAttribute(media_col,
						       "media-source",
						       IPP_TAG_ZERO), 0,
				      NULL)) != NULL) {
            ppdPwgPpdizeName(keyword, ppdname, sizeof(ppdname));
            cupsFilePrintf(fp, "*InputSlot %s\n", keyword);
	  }

          if ((keyword = ippGetString(ippFindAttribute(media_col, "media-type",
						       IPP_TAG_ZERO), 0,
				      NULL)) != NULL) {
            ppdPwgPpdizeName(keyword, ppdname, sizeof(ppdname));
            cupsFilePrintf(fp, "*MediaType %s\n", keyword);
	  }
        } else if (!strcmp(member_name, "print-quality")) {
	 /*
	  * Map print-quality to cupsPrintQuality...
	  */

          int qval = ippGetInteger(member, 0);
					/* print-quality value */
	  static const char * const qualities[] = { "Draft", "Normal", "High" };
					/* cupsPrintQuality values */

          if (qval >= IPP_QUALITY_DRAFT && qval <= IPP_QUALITY_HIGH)
            cupsFilePrintf(fp, "*cupsPrintQuality %s\n",
			   qualities[qval - IPP_QUALITY_DRAFT]);
        } else if (!strcmp(member_name, "output-bin")) {
          ppdPwgPpdizeName(ippGetString(member, 0, NULL), ppdname,
			   sizeof(ppdname));
          cupsFilePrintf(fp, "*OutputBin %s\n", ppdname);
        } else if (!strcmp(member_name, "sides")) {
          keyword = ippGetString(member, 0, NULL);
          if (keyword && !strcmp(keyword, "one-sided"))
            cupsFilePuts(fp, "*Duplex None\n");
	  else if (keyword && !strcmp(keyword, "two-sided-long-edge"))
	    cupsFilePuts(fp, "*Duplex DuplexNoTumble\n");
	  else if (keyword && !strcmp(keyword, "two-sided-short-edge"))
	    cupsFilePuts(fp, "*Duplex DuplexTumble\n");
        } else {
         /*
          * Add attribute name and value as-is...
          */

          ippAttributeString(member, member_value, sizeof(member_value));
          cupsFilePrintf(fp, "*%s %s\n", member_name, member_value);
	}
      }

      cupsFilePuts(fp, "\"\n*End\n");
    }
  }

 /*
  * constraints
  */
  if (conflicts != NULL) {
    char* constraint;
    for (constraint = (char *)cupsArrayFirst(conflicts); constraint;
         constraint = (char *)cupsArrayNext(conflicts)) {
      cupsFilePrintf(fp, "%s", constraint);
    }
  }

 /*
  * Close up and return...
  */

  free(common_def);
  free(min_res);
  free(max_res);

  if (status_msg && status_msg_size)
    snprintf(status_msg, status_msg_size,
	     "%s %sPPD generated.",
	     (is_apple ? "Apple Raster" :
	      (is_pwg ? "PWG Raster" :
	       (is_pdf ? "PDF" :
		(is_pclm ? "PCLm" :
		 "Legacy IPP printer")))),
	     (is_fax ? "Fax " : ""));

  cupsFileClose(fp);
  if (printer_opt_strings_catalog)
    cupsArrayDelete(printer_opt_strings_catalog);

  return (buffer);

 /*
  * If we get here then there was a problem creating the PPD...
  */

 bad_ppd:

  if (common_res) cupsArrayDelete(common_res);
  if (common_def) free(common_def);
  if (min_res) free(min_res);
  if (max_res) free(max_res);

  cupsFileClose(fp);
  if (printer_opt_strings_catalog)
    cupsArrayDelete(printer_opt_strings_catalog);
  unlink(buffer);
  *buffer = '\0';

  if (status_msg && status_msg_size)
    snprintf(status_msg, status_msg_size,
	     "Printer does not support required IPP attributes or document formats.");

  return (NULL);
}

#endif /* HAVE_CUPS_1_6 */

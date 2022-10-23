//
// IPP attribute/option string catalog manager for libcupsfilters.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers.
//

#include <config.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cups/cups.h>
#include <cups/backend.h>
#include <cups/dir.h>
#include <cups/pwg.h>
#include <cupsfilters/catalog.h>


int					// O  - 1 on success, 0 on failure
cfGetURI(const char *url,		// I  - URL to get
	 char       *name,		// I  - Temporary filename
	 size_t     namesize)		// I  - Size of temporary filename
					//      buffer
{
  http_t		*http = NULL;
  char			scheme[32],	// URL scheme
			userpass[256],	// URL username:password
			host[256],	// URL host
			resource[256];	// URL resource
  int			port;		// URL port
  http_encryption_t	encryption;	// Type of encryption to use
  http_status_t		status;		// Status of GET request
  int			fd;		// Temporary file


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

  if (status != HTTP_STATUS_OK)
  {
    unlink(name);
    *name = '\0';
    return (0);
  }

  return (1);
}


//
// 'cfCatalogFind()' - Find a CUPS message catalog file containing
//                     human-readable standard option and choice names
//                     for IPP printers
//

const char *
cfCatalogSearchDir(const char *dirname)
{
  const char *catalog = NULL, *c1, *c2;
  cups_dir_t *dir = NULL, *subdir;
  cups_dentry_t *subdirentry, *catalogentry;
  char subdirpath[1024], catalogpath[2048], lang[8];
  int i;

  if (dirname == NULL)
    return (NULL);

  // Check first whether we have an English file and prefer this
  snprintf(catalogpath, sizeof(catalogpath), "%s/en/cups_en.po", dirname);
  if (access(catalogpath, R_OK) == 0)
  {
    // Found
    catalog = strdup(catalogpath);
    return (catalog);
  }

  if ((dir = cupsDirOpen(dirname)) == NULL)
    return (NULL);

  while ((subdirentry = cupsDirRead(dir)) != NULL)
  {
    // Do we actually have a subdir?
    if (!S_ISDIR(subdirentry->fileinfo.st_mode))
      continue;
    // Check format of subdir name
    c1 = subdirentry->filename;
    if (c1[0] < 'a' || c1[0] > 'z' || c1[1] < 'a' || c1[1] > 'z')
      continue;
    if (c1[2] >= 'a' && c1[2] <= 'z')
      i = 3;
    else
      i = 2;
    if (c1[i] == '_')
    {
      i ++;
      if (c1[i] < 'A' || c1[i] > 'Z' || c1[i + 1] < 'A' || c1[i + 1] > 'Z')
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
    if ((subdir = cupsDirOpen(subdirpath)) != NULL)
    {
      while ((catalogentry = cupsDirRead(subdir)) != NULL)
      {
	// Do we actually have a regular file?
	if (!S_ISREG(catalogentry->fileinfo.st_mode))
	  continue;
	// Check format of catalog name
	c2 = catalogentry->filename;
	if (strlen(c2) < 10 || strncmp(c2, "cups_", 5) != 0 ||
	    strncmp(c2 + 5, lang, i) != 0 ||
	    strcmp(c2 + strlen(c2) - 3, ".po"))
	  continue;
	// Is catalog readable ?
	snprintf(catalogpath, sizeof(catalogpath), "%s/%s", subdirpath, c2);
	if (access(catalogpath, R_OK) != 0)
	  continue;
	// Found
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
  return (catalog);
}


const char *
cfCatalogFind(const char *preferreddir)
{
  const char *catalog = NULL, *c;
  char buf[1024];

  // Directory supplied by calling program, from config file,
  // environment variable, ...
  if ((catalog = cfCatalogSearchDir(preferreddir)) != NULL)
    goto found;

  // Directory supplied by environment variable CUPS_LOCALEDIR
  if ((catalog = cfCatalogSearchDir(getenv("CUPS_LOCALEDIR"))) != NULL)
    goto found;

  // Determine CUPS datadir (usually /usr/share/cups)
  if ((c = getenv("CUPS_DATADIR")) == NULL)
    c = CUPS_DATADIR;

  // Search /usr/share/cups/locale/ (location which
  // Debian/Ubuntu package of CUPS is using)
  snprintf(buf, sizeof(buf), "%s/locale", c);
  if ((catalog = cfCatalogSearchDir(buf)) != NULL)
    goto found;

  // Search /usr/(local/)share/locale/ (standard location
  // which CUPS is using on Linux)
  snprintf(buf, sizeof(buf), "%s/../locale", c);
  if ((catalog = cfCatalogSearchDir(buf)) != NULL)
    goto found;

  // Search /usr/(local/)lib/locale/ (standard location
  // which CUPS is using on many non-Linux systems)
  snprintf(buf, sizeof(buf), "%s/../../lib/locale", c);
  if ((catalog = cfCatalogSearchDir(buf)) != NULL)
    goto found;

 found:
  return (catalog);
}


static int
compare_choices(void *a,
		void *b,
		void *user_data)
{
  return (strcasecmp(((catalog_choice_strings_t *)a)->name,
		     ((catalog_choice_strings_t *)b)->name));
}


static int
compare_options(void *a,
		void *b,
		void *user_data)
{
  return (strcasecmp(((catalog_opt_strings_t *)a)->name,
		     ((catalog_opt_strings_t *)b)->name));
}


void
cfCatalogFreeChoiceStrings(void* entry,
			   void* user_data)
{
  catalog_choice_strings_t *entry_rec = (catalog_choice_strings_t *)entry;

  if (entry_rec)
  {
    if (entry_rec->name) free(entry_rec->name);
    if (entry_rec->human_readable) free(entry_rec->human_readable);
    free(entry_rec);
  }
}


void
cfCatalogFreeOptionStrings(void* entry,
			   void* user_data)
{
  catalog_opt_strings_t *entry_rec = (catalog_opt_strings_t *)entry;

  if (entry_rec)
  {
    if (entry_rec->name) free(entry_rec->name);
    if (entry_rec->human_readable) free(entry_rec->human_readable);
    if (entry_rec->choices) cupsArrayDelete(entry_rec->choices);
    free(entry_rec);
  }
}


cups_array_t *
cfCatalogOptionArrayNew()
{
  return (cupsArrayNew3(compare_options, NULL, NULL, 0,
			NULL, cfCatalogFreeOptionStrings));
}


catalog_opt_strings_t *
cfCatalogFindOption(cups_array_t *options,
		    char *name)
{
  catalog_opt_strings_t opt;

  if (!name || !options)
    return (NULL);

  opt.name = name;
  return (cupsArrayFind(options, &opt));
}


catalog_choice_strings_t *
cfCatalogFindChoice(cups_array_t *choices,
		    char *name)
{
  catalog_choice_strings_t choice;

  if (!name || !choices)
    return (NULL);

  choice.name = name;
  return (cupsArrayFind(choices, &choice));
}


catalog_opt_strings_t *
cfCatalogAddOption(char *name,
		   char *human_readable,
		   cups_array_t *options)
{
  catalog_opt_strings_t *opt = NULL;

  if (!name || !options)
    return (NULL);

  if ((opt = cfCatalogFindOption(options, name)) == NULL)
  {
    opt = calloc(1, sizeof(catalog_opt_strings_t));
    if (!opt)
      return (NULL);
    opt->human_readable = NULL;
    opt->choices = cupsArrayNew3(compare_choices, NULL, NULL, 0,
				 NULL, cfCatalogFreeChoiceStrings);
    if (!opt->choices)
    {
      free(opt);
      return (NULL);
    }
    opt->name = strdup(name);
    if (!cupsArrayAdd(options, opt))
    {
      cfCatalogFreeOptionStrings(opt, NULL);
      return (NULL);
    }
  }

  if (human_readable)
    opt->human_readable = strdup(human_readable);

  return (opt);
}


catalog_choice_strings_t *
cfCatalogAddChoice(char *name,
		    char *human_readable,
		    char *opt_name,
		    cups_array_t *options)
{
  catalog_choice_strings_t *choice = NULL;
  catalog_opt_strings_t *opt;

  if (!name || !human_readable || !opt_name || !options)
    return (NULL);

  opt = cfCatalogAddOption(opt_name, NULL, options);
  if (!opt)
    return (NULL);

  if ((choice = cfCatalogFindChoice(opt->choices, name)) == NULL)
  {
    choice = calloc(1, sizeof(catalog_choice_strings_t));
    if (!choice)
      return (NULL);
    choice->human_readable = NULL;
    choice->name = strdup(name);
    if (!cupsArrayAdd(opt->choices, choice))
    {
      cfCatalogFreeChoiceStrings(choice, NULL);
      return (NULL);
    }
  }

  if (human_readable)
    choice->human_readable = strdup(human_readable);

  return (choice);

}


char *
cfCatalogLookUpOption(char *name,
		      cups_array_t *options,
		      cups_array_t *printer_options)
{
  catalog_opt_strings_t *opt = NULL;

  if (!name || !options)
    return (NULL);

  if (printer_options &&
      (opt = cfCatalogFindOption(printer_options, name)) != NULL)
    return (opt->human_readable);
  if ((opt = cfCatalogFindOption(options, name)) != NULL)
    return (opt->human_readable);
  else
    return (NULL);
}


char *
cfCatalogLookUpChoice(char *name,
		      char *opt_name,
		      cups_array_t *options,
		      cups_array_t *printer_options)
{
  catalog_opt_strings_t *opt = NULL;
  catalog_choice_strings_t *choice = NULL;

  if (!name || !opt_name || !options)
    return (NULL);

  if (printer_options &&
      (opt = cfCatalogFindOption(printer_options, opt_name)) != NULL &&
      (choice = cfCatalogFindChoice(opt->choices, name)) != NULL)
    return (choice->human_readable);
  else if ((opt = cfCatalogFindOption(options, opt_name)) != NULL &&
	   (choice = cfCatalogFindChoice(opt->choices, name)) != NULL)
    return (choice->human_readable);
  else
    return (NULL);
}


void
cfCatalogLoad(const char *location,
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
  int part = -1; // -1: before first "msgid" or invalid
		 //     line
		 //  0: "msgid"
		 //  1: "msgstr"
		 //  2: "..." = "..."
		 // 10: EOF, save last entry
  int digit;
  int found_in_catalog = 0;

  if (location == NULL || (strncasecmp(location, "http:", 5) &&
			   strncasecmp(location, "https:", 6)))
  {
    if (location == NULL ||
	(stat(location, &statbuf) == 0 &&
	 S_ISDIR(statbuf.st_mode))) // directory?
    {
      filename = cfCatalogFind(location);
      if (filename)
        found_in_catalog = 1;
    }
    else
      filename = location;
  }
  else
  {
    if (cfGetURI(location, tmpfile, sizeof(tmpfile)))
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

  while (cupsFileGets(fp, line, sizeof(line)) || (part = 10))
  {
    // Find a pair of quotes delimiting a string in each line
    // and optional "msgid" or "msgstr" keywords, or a
    // "..." = "..." pair. Skip comments ('#') and empty lines.
    if (part < 10)
    {
      ptr = line;
      while (isspace(*ptr)) ptr ++;
      if (*ptr == '#' || *ptr == '\0') continue;
      if ((start = strchr(ptr, '\"')) == NULL) continue;
      if ((end = strrchr(ptr, '\"')) == start) continue;
      if (*(end - 1) == '\\') continue;
      start2 = NULL;
      end2 = NULL;
      if (start > ptr)
      {
	if (*(start - 1) == '\\') continue;
	if (strncasecmp(ptr, "msgid", 5) == 0) part = 0;
	if (strncasecmp(ptr, "msgstr", 6) == 0) part = 1;
      }
      else
      {
	start2 = ptr;
	while ((start2 = strchr(start2 + 1, '\"')) < end &&
	       *(start2 - 1) == '\\');
	if (start2 < end)
	{
	  // Line with "..." = "..." of text/strings format
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
	}
	else
	  // Continuation line in message catalog file
	  start2 = NULL;
      }
      start ++;
      *end = '\0';
    }
    // Read out the strings between the quotes and save entries
    if (part == 0 || part == 2 || part == 10)
    {
      // Save previous attribute
      if (human_readable)
      {
	if (opt_name)
	{
	  if (choice_name)
	  {
	    cfCatalogAddChoice(choice_name, human_readable,
			       opt_name, options);
	    free(choice_name);
	  }
	  else
	    cfCatalogAddOption(opt_name, human_readable, options);
	  free(opt_name);
	}
	free(human_readable);
	opt_name = NULL;
	choice_name = NULL;
	human_readable = NULL;
      }
      // Stop the loop after saving the last entry
      if (part == 10)
	break;
      // IPP attribute has to be defined with a single msgid line,
      // no continuation lines
      if (opt_name)
      {
	free (opt_name);
	opt_name = NULL;
	if (choice_name)
	{
	  free (choice_name);
	  choice_name = NULL;
	}
	part = -1;
	continue;
      }
      // No continuation line in text/strings format
      if (part == 2 && (start2 == NULL || end2 == NULL))
      {
	part = -1;
	continue;
      }
      // Check line if it is a valid IPP attribute:
      // No spaces, only lowercase letters, digits, '-', '_',
      // "option" or "option.choice"
      for (ptr = start, sep = NULL; ptr < end; ptr ++)
	if (*ptr == '.') // Separator between option and choice
	{
	  if (!sep) // Only the first '.' counts
	  {
	    sep = ptr + 1;
	    *ptr = '\0';
	  }
	}
	else if (!((*ptr >= 'a' && *ptr <= 'z') ||
		   (*ptr >= '0' && *ptr <= '9') ||
		   *ptr == '-' || *ptr == '_'))
	  break;
      if (ptr < end) // Illegal character found
      {
	part = -1;
	continue;
      }
      if (strlen(start) > 0) // Option name found
	opt_name = strdup(start);
      else // Empty option name
      {
	part = -1;
	continue;
      }
      if (sep && strlen(sep) > 0) // Choice name found
	choice_name = strdup(sep);
      else // Empty choice name
	choice_name = NULL;
      if (part == 2) // Human-readable string in the same line
      {
	start = start2;
	end = end2;
      }
    }
    if (part == 1 || part == 2)
    {
      // msgid was not for an IPP attribute, ignore this msgstr
      if (!opt_name) continue;
      // Empty string
      if (start == end) continue;
      // Unquote string
      ptr = start;
      end = start;
      while (*ptr)
      {
	if (*ptr == '\\')
	{
	  ptr ++;
	  if (isdigit(*ptr))
	  {
	    digit = 0;
	    *end = 0;
	    while (isdigit(*ptr) && digit < 3)
	    {
	      *end = *end * 8 + *ptr - '0';
	      digit ++;
	      ptr ++;
	    }
	    end ++;
	  }
	  else
	  {
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
	}
	else
	  *end ++ = *ptr ++;
      }
      *end = '\0';
      // Did the unquoting make the string empty?
      if (strlen(start) == 0) continue;
      // Add the string to our human-readable string
      if (human_readable) // Continuation line
      {
	human_readable = realloc(human_readable,
				 sizeof(char) *
				 (strlen(human_readable) +
				  strlen(start) + 2));
	ptr = human_readable + strlen(human_readable);
	*ptr = ' ';
	strncpy(ptr + 1, start,
		sizeof(human_readable) - (ptr - human_readable) - 1);
      }
      else // First line
	human_readable = strdup(start);
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

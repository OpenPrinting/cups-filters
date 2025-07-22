//
// foomatic-hash.c
//
// Copyright (C) 2024-2025 Zdenek Dohnal <zdohnal@redhat.com>
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file implements the tool foomatic-hash, which scans presented drivers
// for FoomaticRIP* option values which are used during composing shell command, prints
// them into a file for review, hashes the found values and puts them into a separate file.
// The options in question:
// - FoomaticRIPCommandLine,
// - FoomaticRIPCommandLinePDF,
// - FoomaticRIPOptionSetting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "util.h"
#include <ctype.h>
#include <cups/array.h>
#include <cups/cups.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(HAVE_LIBPPD)
#include <ppd/ppd.h>
#endif // HAVE_LIBPPD


void write_array(cups_array_t *ar, char *filename);


//
// `write_array()` - Writes the CUPS array content into file, line by line...
//

void
write_array(cups_array_t *ar,       // I - CUPS array with contents to write
	    char	 *filename) // I - Path to file where to put data in
{
  cups_file_t *f = NULL;	   // CUPS file pointer

  if (cupsArrayCount(ar) == 0)
    return;

  if ((f = cupsFileOpen(filename, "w")) == NULL)
  {
    fprintf(stderr, "Cannot open file \"%s\" for write.\n", filename);
    return;
  }

  for (char *s = (char*)cupsArrayGetFirst(ar); s; s = (char*)cupsArrayGetNext(ar))
    cupsFilePrintf(f, "%s\n", s);

  cupsFileClose(f);
}


//
// 'generate_hash_file()' - Generate file with unique hashes.
//

int					 // O - 0 - success/ 1 - error
generate_hash_file(cups_array_t *values, // I - File with values to hash
		   char         *output) // I - File where to save new file
{
  cups_array_t *syshashes = NULL,	 // Already existing hashes on system
	       *hashes = NULL;		 // Hashed values from input
  char	       *data = NULL,		 // Pointer for storing string from array of values
	       comment[16],		 // Array for storing comment
	       hash_string[65];		 // Array for hexadecimal representation of hashed value


  //
  // Load existing hashes from system...
  //

  if (load_system_hashes(&syshashes))
    return (1);

  //
  // Load hashes from previous runs if any...
  //

  if (load_array(&hashes, output))
    return (1);

  //
  // Now do the hashing, save the hexadecimal string if it is
  // unique - if the hash is not on system or in the loaded hash
  // file from previous runs...
  //

  for (data = (char*)cupsArrayGetFirst(values); data; data = (char*)cupsArrayGetNext(values))
  {
    if (hash_data((unsigned char*)data, strlen(data), hash_string, sizeof(hash_string)))
      return (1);

    if (!cupsArrayFind(syshashes, hash_string) && !cupsArrayFind(hashes, hash_string))
      cupsArrayAdd(hashes, hash_string);
  }

  if (cupsArrayCount(hashes))
  {
    //
    // Add comment mentioning the used hash algorithm
    //

    snprintf(comment, sizeof(comment), "# %s", hash_alg);

    if (!cupsArrayFind(hashes, comment))
      cupsArrayAdd(hashes, comment);

    //
    // Create a new hash file...
    //

    write_array(hashes, output);
  }

  cupsArrayDelete(syshashes);
  cupsArrayDelete(hashes);

  return (0);
}


//
// `find_foomaticrip_keywords()` - reads PPD file, find FoomaticRIPCommandLine,
// FoomaticRIPCommandLinePDF and FoomaticRIPOptionSetting, save their values
// into CUPS array.
//

void
find_foomaticrip_keywords(cups_array_t *data, // O - Array with values of FoomaticRIP* PPD keywords
			  cups_file_t  *file) // I - File descriptor opened via CUPS API
{
  char *p;				      // Helper pointer
  char key[128],			      // PPD keyword
       line[256],			      // PPD line length is max 255 (excl. \0)
       name[64],			      // PPD option name
       text[64];			      // PPD option human-readable text

  //
  // Allocate struct for saving value data dynamically,
  // it can span over multiplelines...
  //

  dstr_t *value = create_dstr();

  dstrassure(value, 256);

  //
  // Going through the PPD file...
  //

  while (cupsFileGets(file, line, 256) != NULL)
  {
    //
    // Ignore commmented lines and whatever not starting with '*'
    // to get the closest keyword
    //

    if (line[0] != '*' || startswith(line, "*%"))
      continue;

    //
    // Get the PPD keyword
    // Structure of PPD line:
    //  *keyword [option_name/option_text]: value1 [value2 value3...] 
    //

    key[0] = name[0] = text[0] = '\0';

    if ((p = strchr(line, ':')) == NULL)
      continue;

    *p = '\0';

    sscanf(line, "*%127s%*[ \t]%63[^ \t/=)]%*1[/=]%63[^\n]", key, name, text);

    //
    // Get the value...
    //

    dstrclear(value);
    sscanf(p + 1, " %255[^\r\n]", value->data);
    value->len = strlen(value->data);

    //
    // If the value is multiline (the current line ends with && or does not end with \"),
    // continue saving it, and handle quotes if the value is quoted...
    //

    while (1)
    {
      if (dstrendswith(value, "&&"))
      {
	//
	// "&&" is the continue-on-next-line marker
	//

	value->len -= 2;
	value->data[value->len] = '\0';
      }
      else if (value->data[0] == '\"' && !strchr(value->data +1, '\"'))
      {
	//
	// Quoted but quotes are not yet closed - typically value blocks
	// ended by keyword *End - append LF for the next line...
	//

	dstrcat(value, "\n"); // keep newlines in quoted string
      }
      // Quotes already closed, we have the whole value...
      else
	break;

      //
      // We read the next line if the value was not complete...
      //

      if (cupsFileGets(file, line, 256) == NULL)
	break;

      dstrcat(value, line);
      dstrremovenewline(value);

      //
      // 2047 characters to read for value sounds reasonable,
      // break if we have more and crop the string...
      //

      if (strlen(value->data) > 2047)
      {
	value->data[2047] = '\0';
	value->len = 2047;
	break;
      }
    }

    //
    // Skip if the key is not what we look for...
    //

    if (strcmp(key, "FoomaticRIPCommandLine") && strcmp(key, "FoomaticRIPCommandLinePDF") && strcmp(key, "FoomaticRIPOptionSetting"))
      continue;

    //
    // Remove quotes...
    //

    if (value->data[0] == '\"')
    {
      memmove(value->data, value->data +1, value->len +1);
      p = strrchr(value->data, '\"');
      if (!p)
      {
	fprintf(stderr, "Invalid line: \"%s: ...\"\n", key);
	continue;
      }
      *p = '\0';
    }

    //
    // Remove last newline and last whitespace...
    //

    dstrremovenewline(value);

    dstrtrim_right(value);

    //
    // Skip empty values if there are any...
    //

    if (!value->data || !value->data[0])
      continue;

    //
    // Save data value
    //

    if (!cupsArrayFind(data, value->data))
      cupsArrayAdd(data, value->data);
  }

  free_dstr(value);
}


//
// `get_values_from_ppd()` - Open the PPD file and get values of
//  desired FoomaticRIP PPD keywords...
//

int					    // O - Return value, 0 - success, 1 - error
get_values_from_ppd(cups_array_t *data,     // O - Array of found FoomaticRIP* values
			char     *filename) // I - Path to the file
{
  cups_file_t *file = NULL;		    // File descriptor
  int	      ret = 0;			    // Return value

  if (!is_valid_path(filename, IS_FILE))
    return (1);

  if ((file = cupsFileOpen(filename, "r")) == NULL)
  {
    fprintf(stderr, "Cannot open \"%s\" for reading.\n", filename);
    return (1);
  }

  find_foomaticrip_keywords(data, file);

  cupsFileClose(file);

  return (ret);
}


#if defined(HAVE_LIBPPD)
//
// `copy_col()` - Allocation function for collection struct.
//

ppd_collection_t *     // O - Dynamically allocated PPD collection struct
copy_col(char *path)   // I - Directory with drivers
{
  ppd_collection_t *col = NULL;

  if ((col = (ppd_collection_t*)calloc(1, sizeof(ppd_collection_t))) == NULL)
  {
    fprintf(stderr, "Cannot allocate memory for PPD collection.\n");
    return (NULL);
  }

  if ((col->path = (char*)calloc(strlen(path) + 1, sizeof(char))) == NULL)
  {
    fprintf(stderr, "Cannot allocate memory for PPD path.\n");
    free(col);
    return (NULL);
  }

  snprintf(col->path, strlen(path) + 1, "%s", path);

  return (col);
}


//
// `free_col()` - Free function for PPD collection.
//

void
free_col(ppd_collection_t *col) // I - PPD collection
{
  free(col->path);
  free(col);
}


//
// `compare_col()` - Comparing function for PPD collection.
//

int				 // O - Result of comparison, 0 - the same, 1 - differs
compare_col(char             *a, // I - PPD collection
            ppd_collection_t *b) // I - PPD collection
{
  if(!strcmp(a, b->path))
    return (0);

  return (1);
}
#endif // HAVE_LIBPPD


//
// `get_values_from_ppdpaths()` - Goes via sent list of directories, gets
// PPDs and gets value strings for FoomaticRIP related PPD keywords.
//

int						 // O - Return value, 0 - success, 1 - error
get_values_from_ppdpaths(cups_array_t *data,     // O - Array of found values
			 char	      *ppdpaths) // I - List of directories with drivers, comma separated
{
#if defined(HAVE_LIBPPD)
  char		   *path = NULL,		 // Directory path
		   *start = NULL,		 // Helper pointer to start of string
		   *end = NULL;			 // Helper pointer to end of string
  cups_array_t     *ppd_collections = NULL,	 // Directories with drivers
		   *ppds = NULL;		 // PPD URIs
  cups_file_t      *ppdfile = NULL;		 // PPD file descriptor
  int		   ret = 0;			 // Return value
  ppd_info_t       *ppd = NULL;			 // In-memory record of PPD


  if ((ppd_collections = cupsArrayNew3((cups_array_func_t)compare_col, NULL, NULL, 0, (cups_acopy_func_t)copy_col, (cups_afree_func_t)free_col)) == NULL)
  {
    fprintf(stderr, "Could not allocate PPD collection array.\n");
    return (1);
  }

  //
  // Go through input directory list, validate each record,
  // and add them into array...
  //

  if ((path = strchr(ppdpaths, ',')) == NULL)
  {
    if (is_valid_path(ppdpaths, IS_DIR))
      cupsArrayAdd(ppd_collections, ppdpaths);
  }
  else
  {
    for (start = end = ppdpaths; *end; start = end)
    {
      if ((end = strchr(start, ',')) != NULL)
	*end++ = '\0';
      else
	end = start + strlen(start);

      if (is_valid_path(start, IS_DIR) && !cupsArrayFind(ppd_collections, start))
	cupsArrayAdd(ppd_collections, start);
    }
  }

  //
  // Get array of in-memory PPD records, later used for generating the PPDs themselves...
  //

  if ((ppds = ppdCollectionListPPDs(ppd_collections, 0, 0, NULL, NULL, NULL)) == NULL)
    goto end;

  //
  // Go through in-memory PPD records, generate a PPD and search for FoomaticRIP* keywords...
  //

  for (ppd = (ppd_info_t*)cupsArrayGetFirst(ppds); ppd; ppd = (ppd_info_t*)cupsArrayGetNext(ppds))
  {
    if ((ppdfile = ppdCollectionGetPPD(ppd->record.name, ppd_collections, NULL, NULL)) == NULL)
      continue;

    find_foomaticrip_keywords(data, ppdfile);

    cupsFileClose(ppdfile);
  }


end:
  for (ppd = (ppd_info_t*)cupsArrayGetFirst(ppds); ppd; ppd = (ppd_info_t*)cupsArrayGetNext(ppds))
    free(ppd);

  cupsArrayDelete(ppds);

  cupsArrayDelete(ppd_collections);

  return (ret);
#else
  fprintf(stdout, "foomatic-hash is not compiled with LIBPPD support.\n");

  return (0);
#endif // HAVE_LIBPPD
}


void
help()
{
  printf("Usage:\n"
	 "foomatic-hash --ppd <ppdfile> <scanoutput> <hashes_file>\n"
	 "foomatic-hash --ppd-paths <path1,path2...pathN> <scanoutput> <hashes_file>\n"
	 "\n"
	 "Finds values of FoomaticRIPCommandLine, FoomaticRIPPDFCommandLine\n"
	 "and FoomaticRIPOptionSetting from the specified PPDs, appends them\n"
	 "into the specified scan output for review, and hashes the found values.\n"
	 "\n"
	 "--ppd <ppdfile>                   - PPD file to read\n"
	 "--ppd-paths <path1,path2...pathN> - Paths to look for PPDs, available only with libppd\n"
	 "<scanoutput>    - Found required values from drivers\n"
	 "<hashes_file>   - Output file with hashes\n");
}


int
main(int argc,
     char** argv)
{
  cups_array_t *data = NULL; // Found FoomaticRIP* PPD keyword values
  int	       ret = 1;


  if (argc != 5)
  {
    help();
    return (0);
  }

  //
  // End up early if we can't write into paths provided as arguments
  //

  if (!is_valid_path(argv[3], IS_FILE) ||
      ((data = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL) ||
      !is_valid_path(argv[4], IS_FILE))
    return (1);

  //
  // We scan single PPD file, or from directory (if libppd support is present)
  //

  if (!strcmp(argv[1], "--ppd"))
  {
    if (get_values_from_ppd(data, argv[2]))
      return (1);
  }
  else if (!strcmp(argv[1], "--ppd-paths"))
  {
    if (get_values_from_ppdpaths(data, argv[2]))
      return (1);
  }
  else
  {
    fprintf(stderr, "Unsupported argument.\n");
    return (1);
  }

  //
  // Write found values of FoomaticRIPCommandLine, FoomaticRIPPDFCommandLine and FoomaticRIPOptionSetting
  // PPD keywords...
  //

  write_array(data, argv[3]);

  //
  // Hash the found values..
  //

  ret = generate_hash_file(data, argv[4]);

  cupsArrayDelete(data);

 
  return (ret);
}

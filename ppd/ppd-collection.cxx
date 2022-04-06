/*
 * PPD collection support for cups-filters
 *
 * This program handles listing and installing static PPD files, PPD files
 * created from driver information files, and dynamically generated PPD files
 * using driver helper programs.
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

#include <cups/dir.h>
#include <cups/transcode.h>
#include "ppd.h"
#include "ppdc.h"
#include "file-private.h"
#include "array-private.h"
#include <regex.h>
#include <sys/wait.h>


/*
 * Constants...
 */

#define TAR_BLOCK	512		/* Number of bytes in a block */
#define TAR_BLOCKS	10		/* Blocking factor */

#define TAR_MAGIC	"ustar"		/* 5 chars and a null */
#define TAR_VERSION	"00"		/* POSIX tar version */
#define TAR_OLDGNU_MAGIC "ustar  "

#define TAR_OLDNORMAL	'\0'		/* Normal disk file, Unix compat */
#define TAR_NORMAL	'0'		/* Normal disk file */
#define TAR_LINK	'1'		/* Link to previously dumped file */
#define TAR_SYMLINK	'2'		/* Symbolic link */
#define TAR_CHR		'3'		/* Character special file */
#define TAR_BLK		'4'		/* Block special file */
#define TAR_DIR		'5'		/* Directory */
#define TAR_FIFO	'6'		/* FIFO special file */
#define TAR_CONTIG	'7'		/* Contiguous file */


/*
 * PPD information structures...
 */

typedef union				/**** TAR record format ****/
{
  unsigned char	all[TAR_BLOCK];		/* Raw data block */
  struct
  {
    char	pathname[100],		/* Destination path */
		mode[8],		/* Octal file permissions */
		uid[8],			/* Octal user ID */
		gid[8],			/* Octal group ID */
		size[12],		/* Octal size in bytes */
		mtime[12],		/* Octal modification time */
		chksum[8],		/* Octal checksum value */
		linkflag,		/* File type */
		linkname[100],		/* Source path for link */
		magic[6],		/* Magic string */
		version[2],		/* Format version */
		uname[32],		/* User name */
		gname[32],		/* Group name */
		devmajor[8],		/* Octal device major number */
		devminor[8],		/* Octal device minor number */
		prefix[155];		/* Prefix for long filenames */
  }	header;
} tar_rec_t;

typedef struct
{
  cups_array_t	*Inodes;	/* Inodes of directories we've visited*/
  cups_array_t	*PPDsByName,
				/* PPD files sorted by filename and name */
		*PPDsByMakeModel;
				/* PPD files sorted by make and model */
  int		ChangedPPD;	/* Did we change the PPD database? */
} ppd_list_t;

//typedef int (*cupsd_compare_func_t)(const void *, const void *);


/*
 * Globals...
 */

static const char * const PPDTypes[] =	/* ppd-type values */
			{
			  "postscript",
			  "pdf",
			  "raster",
			  "fax",
			  "object",
			  "object-direct",
			  "object-storage",
			  "unknown",
			  "drv",
			  "archive"
			};


/*
 * Local functions...
 */

static ppd_info_t	*add_ppd(const char *filename, const char *name,
			         const char *language, const char *make,
				 const char *make_and_model,
				 const char *device_id, const char *product,
				 const char *psversion, time_t mtime,
				 size_t size, int model_number, int type,
				 const char *scheme, ppd_list_t *ppdlist,
				 cf_logfunc_t log, void *ld);
static cups_file_t	*cat_drv(const char *name, char *ppdname,
				 cf_logfunc_t log, void *ld);
static cups_file_t	*cat_static(const char *name,
				    cf_logfunc_t log, void *ld);
static cups_file_t	*cat_tar(const char *name, char *ppdname,
				 cf_logfunc_t log, void *ld);
static int		compare_inodes(struct stat *a, struct stat *b);
static int		compare_matches(const ppd_info_t *p0,
			                const ppd_info_t *p1);
static int		compare_names(const ppd_info_t *p0,
			              const ppd_info_t *p1);
static int		compare_ppds(const ppd_info_t *p0,
			             const ppd_info_t *p1);
static void		free_array(cups_array_t *a);
static void		free_ppdlist(ppd_list_t *ppdlist);
static int		load_driver(const char *filename,
				    const char *name,
				    ppd_list_t *ppdlist,
				    cf_logfunc_t log, void *ld);
static int		load_drv(const char *filename, const char *name,
			         cups_file_t *fp, time_t mtime, off_t size,
				 ppd_list_t *ppdlist,
				 cf_logfunc_t log, void *ld);
static void		load_ppd(const char *filename, const char *name,
			         const char *scheme, struct stat *fileinfo,
			         ppd_info_t *ppd, cups_file_t *fp, off_t end,
				 ppd_list_t *ppdlist,
				 cf_logfunc_t log, void *ld);
static int		load_ppds(const char *d, const char *p, int descend,
				  ppd_list_t *ppdlist,
				  cf_logfunc_t log, void *ld);
static int		load_ppds_dat(const char *filename, int verbose,
				      ppd_list_t *ppdlist,
				      cf_logfunc_t log, void *ld);
static int		load_tar(const char *filename, const char *name,
			         cups_file_t *fp, time_t mtime, off_t size,
				 ppd_list_t *ppdlist,
				 cf_logfunc_t log, void *ld);
static int		read_tar(cups_file_t *fp, char *name, size_t namesize,
			         struct stat *info,
				 cf_logfunc_t log, void *ld);
static regex_t		*regex_device_id(const char *device_id,
					 cf_logfunc_t log, void *ld);
static regex_t		*regex_string(const char *s,
				      cf_logfunc_t log, void *ld);
static int		CompareNames(const char *s, const char *t);
static cups_array_t	*CreateStringsArray(const char *s);
static int		ExecCommand(const char *command, char **argv);
static cups_file_t	*PipeCommand(int *cpid, int *epid, const char *command,
				     char **argv, uid_t user,
				     cf_logfunc_t log, void *ld);
static int		ClosePipeCommand(cups_file_t *fp, int cpid, int epid,
					 cf_logfunc_t log, void *ld);


/*
 * 'ppdCollectionListPPDs()' - List PPD files.
 */

cups_array_t *				/* O - List of PPD files */
ppdCollectionListPPDs(
	cups_array_t  *ppd_collections, /* I - Directories to search for PPDs
					       in */
	int           limit,		/* I - Limit */
	int           num_options,	/* I - Number of options */
	cups_option_t *options,		/* I - Options */
	cf_logfunc_t log,		/* I - Log function */
	void *ld)			/* I - Aux. data for log function */
{
  int		i;			/* Looping var */
  ppd_collection_t *col;		/* Pointer to PPD collection */
  int		count;			/* Number of PPDs to list */
  ppd_info_t	*ppd,			/* Current PPD file */
		*newppd;		/* Copy of current PPD */
  cups_file_t	*fp;			/* ppds.dat file */
  cups_array_t	*include,		/* PPD schemes to include */
		*exclude;		/* PPD schemes to exclude */
  const char    *cachename,		/* Cache file name */
		*only_makes,		/* Do we only want a list of makes? */
		*device_id,		/* ppd-device-id option */
		*language,		/* ppd-natural-language option */
		*make,			/* ppd-make option */
		*make_and_model,	/* ppd-make-and-model option */
		*model_number_str,	/* ppd-model-number option */
		*product,		/* ppd-product option */
		*psversion,		/* ppd-psversion option */
		*type_str;		/* ppd-type option */
  int		model_number,		/* ppd-model-number value */
		type;			/* ppd-type value */
  size_t	make_and_model_len,	/* Length of ppd-make-and-model */
		product_len;		/* Length of ppd-product */
  regex_t	*device_id_re,		/* Regular expression for matching
					   device ID */
		*make_and_model_re;	/* Regular expression for matching make
					   and model */
  regmatch_t	re_matches[6];		/* Regular expression matches */
  cups_array_t	*matches,		/* Matching PPDs */
		*result;		/* Resulting PPD list */
  ppd_list_t	ppdlist;		/* Lists of all available PPDs */
  int		matches_array_created = 0;


 /*
  * Initialize PPD list...
  */

  ppdlist.PPDsByName      = cupsArrayNew((cups_array_func_t)compare_names,
					  NULL);
  ppdlist.PPDsByMakeModel = cupsArrayNew((cups_array_func_t)compare_ppds,
					  NULL);
  ppdlist.ChangedPPD      = 0;


 /*
  * See if we have a PPD database file...
  */

  cachename = cupsGetOption("ppd-cache", num_options, options);
  if (cachename && cachename[0] &&
      load_ppds_dat(cachename, 1, &ppdlist, log, ld)) {
    free_ppdlist(&ppdlist);
    return(NULL);
  }

 /*
  * Load all PPDs in the specified directories and below...
  */

  ppdlist.Inodes = cupsArrayNew((cups_array_func_t)compare_inodes, NULL);

  for (col = (ppd_collection_t *)cupsArrayFirst(ppd_collections);
       col;
       col = (ppd_collection_t *)cupsArrayNext(ppd_collections))
    load_ppds(col->path, col->name ? col->name : col->path, 1, &ppdlist,
	      log, ld);

  if (cachename && cachename[0])
  {
   /*
    * Cull PPD files that are no longer present...
    */

    for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist.PPDsByName), i = 0;
	 ppd;
	 ppd = (ppd_info_t *)cupsArrayNext(ppdlist.PPDsByName), i ++)
      if (!ppd->found)
      {
       /*
	* Remove this PPD file from the list...
	*/

	cupsArrayRemove(ppdlist.PPDsByName, ppd);
	cupsArrayRemove(ppdlist.PPDsByMakeModel, ppd);
	free(ppd);

	ppdlist.ChangedPPD = 1;
      }

   /*
    * Write the new ppds.dat file...
    */

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "libppd: [PPD Collections] ChangedPPD=%d",
		 ppdlist.ChangedPPD);

    if (ppdlist.ChangedPPD)
    {
      char	newname[1024];		/* New filename */

      snprintf(newname, sizeof(newname), "%s.%d", cachename, (int)getpid());

      if ((fp = cupsFileOpen(newname, "w")) != NULL)
      {
	unsigned ppdsync = PPD_SYNC;	/* Sync word */

	cupsFileWrite(fp, (char *)&ppdsync, sizeof(ppdsync));

	for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist.PPDsByName);
	     ppd;
	     ppd = (ppd_info_t *)cupsArrayNext(ppdlist.PPDsByName))
	  cupsFileWrite(fp, (char *)&(ppd->record), sizeof(ppd_rec_t));

	cupsFileClose(fp);

	if (rename(newname, cachename))
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "libppd: [PPD Collections] Unable to rename \"%s\" - %s",
		       newname, strerror(errno));
	}
	else
	  if (log) log(ld, CF_LOGLEVEL_INFO,
		       "libppd: [PPD Collections] Wrote \"%s\", %d PPDs...",
		       cachename, cupsArrayCount(ppdlist.PPDsByName));
      }
      else
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] Unable to write \"%s\" - %s",
		     cachename, strerror(errno));
    }
    else
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "libppd: [PPD Collections] No new or changed PPDs...");
  }

 /*
  * Lists for inclusion and exclusion of certain PPD sources...
  */

  exclude     = CreateStringsArray(cupsGetOption("exclude-schemes",
						 num_options, options));
  include     = CreateStringsArray(cupsGetOption("include-schemes",
						 num_options, options));

 /*
  * Read further options...
  */

  only_makes       = cupsGetOption("only-makes", num_options, options);
  device_id        = cupsGetOption("device-id", num_options, options);
  language         = cupsGetOption("natural-language", num_options, options);
  make             = cupsGetOption("make", num_options, options);
  make_and_model   = cupsGetOption("make-and-model", num_options, options);
  model_number_str = cupsGetOption("model-number", num_options, options);
  product          = cupsGetOption("product", num_options, options);
  psversion        = cupsGetOption("psversion", num_options, options);
  type_str         = cupsGetOption("type", num_options, options);

  if (make_and_model)
    make_and_model_len = strlen(make_and_model);
  else
    make_and_model_len = 0;

  if (product)
    product_len = strlen(product);
  else
    product_len = 0;

  if (model_number_str)
    model_number = atoi(model_number_str);
  else
    model_number = 0;

  if (type_str)
  {
    for (type = 0;
         type < (int)(sizeof(PPDTypes) / sizeof(PPDTypes[0]));
	 type ++)
      if (!strcmp(type_str, PPDTypes[type]))
        break;

    if (type >= (int)(sizeof(PPDTypes) / sizeof(PPDTypes[0])))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Bad ppd-type=\"%s\" ignored!",
		   type_str);
      type_str = NULL;
    }
  }
  else
    type = 0;

  for (i = 0; i < num_options; i ++)
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "libppd: [PPD Collections] %s=\"%s\"", options[i].name,
		 options[i].value);

  if (limit <= 0 || limit > cupsArrayCount(ppdlist.PPDsByMakeModel))
    count = cupsArrayCount(ppdlist.PPDsByMakeModel);
  else
    count = limit;

  if (device_id || language || make || make_and_model || model_number_str ||
      product)
  {
    matches = cupsArrayNew((cups_array_func_t)compare_matches, NULL);
    matches_array_created = 1;

    if (device_id)
      device_id_re = regex_device_id(device_id, log, ld);
    else
      device_id_re = NULL;

    if (make_and_model)
      make_and_model_re = regex_string(make_and_model, log, ld);
    else
      make_and_model_re = NULL;

    for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist.PPDsByMakeModel);
	 ppd;
	 ppd = (ppd_info_t *)cupsArrayNext(ppdlist.PPDsByMakeModel))
    {
     /*
      * Filter PPDs based on make, model, product, language, model number,
      * and/or device ID using the "matches" score value.  An exact match
      * for product, make-and-model, or device-id adds 3 to the score.
      * Partial matches for make-and-model yield 1 or 2 points, and matches
      * for the make and language add a single point.  Results are then sorted
      * by score, highest score first.
      */

      if (ppd->record.type < PPD_TYPE_POSTSCRIPT ||
	  ppd->record.type >= PPD_TYPE_DRV)
	continue;

      if (cupsArrayFind(exclude, ppd->record.scheme) ||
          (include && !cupsArrayFind(include, ppd->record.scheme)))
        continue;

      ppd->matches = 0;

      if (device_id_re &&
	  !regexec(device_id_re, ppd->record.device_id,
                   (size_t)(sizeof(re_matches) / sizeof(re_matches[0])),
		   re_matches, 0))
      {
       /*
        * Add the number of matching values from the device ID - it will be
	* at least 2 (manufacturer and model), and as much as 3 (command set).
	*/

        for (i = 1; i < (int)(sizeof(re_matches) / sizeof(re_matches[0])); i ++)
	  if (re_matches[i].rm_so >= 0)
	    ppd->matches ++;
      }

      if (language)
      {
	for (i = 0; i < PPD_MAX_LANG; i ++)
	  if (!ppd->record.languages[i][0])
	    break;
	  else if (!strcmp(ppd->record.languages[i], language))
	  {
	    ppd->matches ++;
	    break;
	  }
      }

      if (make && !_ppd_strcasecmp(ppd->record.make, make))
        ppd->matches ++;

      if (make_and_model_re &&
          !regexec(make_and_model_re, ppd->record.make_and_model,
	           (size_t)(sizeof(re_matches) / sizeof(re_matches[0])),
		   re_matches, 0))
      {
	// See how much of the make-and-model string we matched...
	if (re_matches[0].rm_so == 0)
	{
	  if ((size_t)re_matches[0].rm_eo == make_and_model_len)
	    ppd->matches += 3;		// Exact match
	  else
	    ppd->matches += 2;		// Prefix match
	}
	else
	  ppd->matches ++;		// Infix match
      }

      if (model_number_str && ppd->record.model_number == model_number)
        ppd->matches ++;

      if (product)
      {
	for (i = 0; i < PPD_MAX_PROD; i ++)
	  if (!ppd->record.products[i][0])
	    break;
	  else if (!_ppd_strcasecmp(ppd->record.products[i], product))
	  {
	    ppd->matches += 3;
	    break;
	  }
	  else if (!_ppd_strncasecmp(ppd->record.products[i], product,
	                              product_len))
	  {
	    ppd->matches += 2;
	    break;
	  }
      }

      if (psversion)
      {
	for (i = 0; i < PPD_MAX_VERS; i ++)
	  if (!ppd->record.psversions[i][0])
	    break;
	  else if (!_ppd_strcasecmp(ppd->record.psversions[i], psversion))
	  {
	    ppd->matches ++;
	    break;
	  }
      }

      if (type_str && ppd->record.type == type)
        ppd->matches ++;

      if (ppd->matches)
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "libppd: [PPD Collections] %s matches with score %d!",
		     ppd->record.name, ppd->matches);
        cupsArrayAdd(matches, ppd);
      }
    }
    if (device_id_re)
      free(device_id_re);
    if (make_and_model_re)
      free(make_and_model_re);
  }
  else if (include || exclude)
  {
    matches = cupsArrayNew((cups_array_func_t)compare_ppds, NULL);
    matches_array_created = 1;

    for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist.PPDsByMakeModel);
	 ppd;
	 ppd = (ppd_info_t *)cupsArrayNext(ppdlist.PPDsByMakeModel))
    {
     /*
      * Filter PPDs based on the include/exclude lists.
      */

      if (ppd->record.type < PPD_TYPE_POSTSCRIPT ||
	  ppd->record.type >= PPD_TYPE_DRV)
	continue;

      if (cupsArrayFind(exclude, ppd->record.scheme) ||
          (include && !cupsArrayFind(include, ppd->record.scheme)))
        continue;

      cupsArrayAdd(matches, ppd);
    }
  }
  else
    matches = ppdlist.PPDsByMakeModel;

  result = cupsArrayNew(NULL, NULL);

  for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist.PPDsByMakeModel), i = 0;
       count > 0 && ppd;
       ppd = (ppd_info_t *)cupsArrayNext(ppdlist.PPDsByMakeModel), i ++)
  {
   /*
    * Skip invalid PPDs...
    */

    if (ppd->record.type < PPD_TYPE_POSTSCRIPT ||
        ppd->record.type >= PPD_TYPE_DRV)
      continue;

   /*
    * Output this PPD...
    */

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "libppd: [PPD Collections] Sending %s (%s)...",
		 ppd->record.name, ppd->record.make_and_model);

    count --;

    if (only_makes)
      cupsArrayAdd(result, strdup(ppd->record.make));
    else
    {
      newppd = (ppd_info_t *)malloc(sizeof(ppd_info_t));
      memcpy(newppd, ppd, sizeof(ppd_info_t));
      cupsArrayAdd(result, newppd);
    }

   /*
    * If we have only requested the make, then skip
    * the remaining PPDs with this make...
    */

    if (only_makes)
    {
      const char	*this_make;	/* This ppd-make */


      for (this_make = ppd->record.make,
               ppd = (ppd_info_t *)cupsArrayNext(matches);
	   ppd;
	   ppd = (ppd_info_t *)cupsArrayNext(matches))
	if (_ppd_strcasecmp(this_make, ppd->record.make))
	  break;

      cupsArrayPrev(matches);
    }
  }

  free_ppdlist(&ppdlist);
  if (matches_array_created)
    cupsArrayDelete(matches);

  return(result);
}


/*
 * 'ppdCollectionGetPPD()' - Copy a PPD file to stdout.
 */

cups_file_t *
ppdCollectionGetPPD(
	const char *name,		/* I - PPD URI of the desired PPD */
	cups_array_t *ppd_collections,	/* I - Directories to search for PPDs
					       in */
	cf_logfunc_t log,		/* I - Log function */
	void *ld)			/* I - Aux. data for log function */
{
  ppd_collection_t *col;		/* Pointer to PPD collection */
  cups_file_t	*fp;			/* Archive file pointer */
  int		fd;
  int		cpid;			/* Process ID for driver program */
  int		epid;			/* Process ID for logging process */
  int		bytes;
  int           is_archive = 0;
  int           is_drv = 0;
  char		realname[1024],		/* Scheme from PPD name */
		buffer[8192],		/* Copy buffer */
		tempname[1024],		/* Temp file name */
		*ptr,			/* Pointer into string */
		*ppdname,
		ppduri[1024];		/* PPD URI */


 /*
  * Figure out if this is a static or dynamic PPD file...
  */

  if (strstr(name, "../"))
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Invalid PPD name.");
    return(NULL);
  }

  if (strstr(name, ".tar:") || strstr(name, ".tar.gz:"))
    is_archive = 1;
  else if (strstr(name, ".drv:"))
    is_drv = 1;

  if (ppd_collections)
  {
    for (col = (ppd_collection_t *)cupsArrayFirst(ppd_collections);
	 col;
	 col = (ppd_collection_t *)cupsArrayNext(ppd_collections))
    {
      if (col->name)
      {
	if (col->name[0])
	{
	  if (!strncmp(name, col->name, strlen(col->name)))
	    snprintf(realname, sizeof(realname), "%s/%s", col->path,
		     name + strlen(col->name));
	  else
	    continue;
	}
	else
	  snprintf(realname, sizeof(realname), "%s/%s", col->path,
		   name);
      } else
	strlcpy(realname, name, sizeof(realname));
      if ((ptr = strchr(realname, ':')) == NULL)
	ppdname = NULL;
      else
      {
	if (!is_archive && !is_drv)
	  strlcpy(ppduri, realname, sizeof(ppduri));
	*ptr = '\0';
	ppdname = ptr + 1;
      }
      if (access(realname, R_OK) == 0)
	break;
    }
    if (col == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Requested PPD %s is in none of "
		   "the collections",
		   name);      
      return(NULL);
    }
  }
  else
  {
    strlcpy(realname, name, sizeof(realname));
    if ((ptr = strchr(realname, ':')) == NULL)
      ppdname = NULL;
    else
    {
      if (!is_archive && !is_drv)
	strlcpy(ppduri, realname, sizeof(ppduri));
      *ptr = '\0';
      ppdname = ptr + 1;
    }
    if (access(realname, R_OK))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Cannot access file %s - %s",
		   realname, strerror(errno));      
      return(NULL);
    }
  }

  if (is_archive)
    return(cat_tar(realname, ppdname, log, ld));
  else if (is_drv)
    return(cat_drv(realname, ppdname, log, ld));
  else if (ppdname == NULL)
    return(cat_static(realname, log, ld));
  else
  {
   /*
    * Dynamic PPD, see if we have a driver program to support it...
    */

    char	*argv[4];		/* Arguments for program */

    ptr = strrchr(realname, '/');
    if (ptr == NULL)
      ptr = realname;
    else
      ptr ++;
    ptr = ppduri + (ptr - realname);
    if (access(realname, X_OK))
    {
     /*
      * File does not exist or is not executable...
      */

      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Unable to access \"%s\" - %s",
		   realname, strerror(errno));

      return(NULL);
    }

   /*
    * Yes, let it cat the PPD file...
    */
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "libppd: [PPD Collections] Grabbing PPD via command: \"%s cat %s\"",
		 realname, ptr);

    argv[0] = realname;
    argv[1] = (char *)"cat";
    argv[2] = (char *)ptr;
    argv[3] = NULL;

    if ((fp = PipeCommand(&cpid, &epid, realname, argv, 0, log, ld)) != NULL)
    {
      if ((fd = cupsTempFd(tempname, sizeof(tempname))) < 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] Unable to copy PPD to temp "
		     "file: %s",
		     strerror(errno));
	return (NULL);
      }
      while ((bytes = cupsFileRead(fp, buffer, sizeof(buffer))) > 0)
	bytes = write(fd, buffer, bytes);
      ClosePipeCommand(fp, cpid, epid, log, ld);
      close(fd);
      fp = cupsFileOpen(tempname, "r");
      unlink(tempname);
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "libppd: [PPD Collections] Unable to execute \"%s\": %s",
		   tempname, strerror(errno));
      return(NULL);
    }
  }

 /*
  * Exit with no errors...
  */

  return(fp);
}


/*
 * 'ppdCollectionDumpCache()' - Dump the contents of the ppds.dat file.
 */

int
ppdCollectionDumpCache(const char *filename,	/* I - Filename */
		       cf_logfunc_t log,	/* I - Log function */
		       void *ld)		/* I - Aux. data for log
						       function */
{
  ppd_info_t	*ppd;			/* Current PPD */
  ppd_list_t	ppdlist;		/* Lists of all available PPDs */


 /*
  * Initialize PPD list...
  */

  ppdlist.Inodes = NULL;
  ppdlist.PPDsByName      = cupsArrayNew((cups_array_func_t)compare_names,
					  NULL);
  ppdlist.PPDsByMakeModel = cupsArrayNew((cups_array_func_t)compare_ppds,
					  NULL);
  ppdlist.ChangedPPD      = 0;


 /*
  * See if we a PPD database file...
  */

  if (load_ppds_dat(filename, 0, &ppdlist, log, ld)) {
    free_ppdlist(&ppdlist);
    return(1);
  }

  puts("mtime,size,model_number,type,filename,name,languages0,products0,"
       "psversions0,make,make_and_model,device_id,scheme");
  for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist.PPDsByName);
       ppd;
       ppd = (ppd_info_t *)cupsArrayNext(ppdlist.PPDsByName))
    printf("%d,%ld,%d,%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\","
           "\"%s\",\"%s\"\n",
           (int)ppd->record.mtime, (long)ppd->record.size,
	   ppd->record.model_number, ppd->record.type, ppd->record.filename,
	   ppd->record.name, ppd->record.languages[0], ppd->record.products[0],
	   ppd->record.psversions[0], ppd->record.make,
	   ppd->record.make_and_model, ppd->record.device_id,
	   ppd->record.scheme);

  free_ppdlist(&ppdlist);
  return(0);
}


/*
 * 'add_ppd()' - Add a PPD file.
 */

static ppd_info_t *			/* O - PPD */
add_ppd(const char *filename,		/* I - PPD filename */
        const char *name,		/* I - PPD name */
        const char *language,		/* I - LanguageVersion */
        const char *make,		/* I - Manufacturer */
	const char *make_and_model,	/* I - NickName/ModelName */
	const char *device_id,		/* I - 1284DeviceID */
	const char *product,		/* I - Product */
	const char *psversion,		/* I - PSVersion */
        time_t     mtime,		/* I - Modification time */
	size_t     size,		/* I - File size */
	int        model_number,	/* I - Model number */
	int        type,		/* I - Driver type */
	const char *scheme,		/* I - PPD scheme */
	ppd_list_t *ppdlist,
	cf_logfunc_t log,		/* I - Log function */
	void *ld)			/* I - Aux. data for log function */
{
  ppd_info_t	*ppd;			/* PPD */


 /*
  * Add a new PPD file...
  */

  if ((ppd = (ppd_info_t *)calloc(1, sizeof(ppd_info_t))) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Ran out of memory for %d PPD "
		 "files!",
		 cupsArrayCount(ppdlist->PPDsByName));
    return (NULL);
  }

 /*
  * Zero-out the PPD data and copy the values over...
  */

  ppd->found               = 1;
  ppd->record.mtime        = mtime;
  ppd->record.size         = (off_t)size;
  ppd->record.model_number = model_number;
  ppd->record.type         = type;

  strlcpy(ppd->record.filename, filename, sizeof(ppd->record.filename));
  strlcpy(ppd->record.name, name, sizeof(ppd->record.name));
  strlcpy(ppd->record.languages[0], language,
          sizeof(ppd->record.languages[0]));
  strlcpy(ppd->record.products[0], product, sizeof(ppd->record.products[0]));
  strlcpy(ppd->record.psversions[0], psversion,
          sizeof(ppd->record.psversions[0]));
  strlcpy(ppd->record.make, make, sizeof(ppd->record.make));
  strlcpy(ppd->record.make_and_model, make_and_model,
          sizeof(ppd->record.make_and_model));
  strlcpy(ppd->record.device_id, device_id, sizeof(ppd->record.device_id));
  strlcpy(ppd->record.scheme, scheme, sizeof(ppd->record.scheme));

 /*
  * Add the PPD to the PPD arrays...
  */

  cupsArrayAdd(ppdlist->PPDsByName, ppd);
  cupsArrayAdd(ppdlist->PPDsByMakeModel, ppd);

 /*
  * Return the new PPD pointer...
  */

  return (ppd);
}


/*
 * 'cat_drv()' - Generate a PPD from a driver info file.
 */

static cups_file_t *			/* O - Pointer to PPD file */
cat_drv(const char *filename,		/* I - *.drv file name */
	char *ppdname,			/* I - PPD name in the *.drv file */
	cf_logfunc_t log,		/* I - Log function */
	void *ld)			/* I - Aux. data for log function */
{
  cups_file_t	*fp;			// File pointer
  int		fd;
  char		tempname[1024];		// Name for the temporary file
  ppdcSource	*src;			// PPD source file data
  ppdcDriver	*d;			// Current driver
  cups_file_t	*out = NULL;		// PPD output to temp file
  int           fd1, fd2;

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to open \"%s\" - %s\n",
		 filename, strerror(errno));

    return (NULL);
  }

  /* Eliminate any output to stderr, to get rid of the error messages of
     the *.drv file parser */
  fd1 = dup(2);
  fd2 = open("/dev/null", O_WRONLY);
  dup2(fd2, 2);
  close(fd2);

  src = new ppdcSource(filename, fp);

  for (d = (ppdcDriver *)src->drivers->first();
       d;
       d = (ppdcDriver *)src->drivers->next())
    if (!strcmp(ppdname, d->pc_file_name->value) ||
        (d->file_name && !strcmp(ppdname, d->file_name->value)))
      break;

  if (d)
  {
    ppdcArray	*locales;		// Locale names
    ppdcCatalog	*catalog;		// Message catalog in .drv file


    if ((fd = cupsTempFd(tempname, sizeof(tempname))) < 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Unable to copy PPD to temp "
		   "file: %s",
		   strerror(errno));
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "libppd: [PPD Collections] %u locales defined in \"%s\"...\n",
		   (unsigned)src->po_files->count, filename);

      locales = new ppdcArray();
      for (catalog = (ppdcCatalog *)src->po_files->first();
	   catalog;
	   catalog = (ppdcCatalog *)src->po_files->next())
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "libppd: [PPD Collections] Adding locale \"%s\"...\n",
		     catalog->locale->value);
	catalog->locale->retain();
	locales->add(catalog->locale);
      }

      out = cupsFileOpenFd(fd, "w");
      d->write_ppd_file(out, NULL, locales, src, PPDC_LFONLY);
      cupsFileClose(out);
      close(fd);
      locales->release();

      out = cupsFileOpen(tempname, "r");
      unlink(tempname);
    }
  }
  else
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] PPD \"%s\" not found.\n", ppdname);

  src->release();
  cupsFileClose(fp);

  /* Re-activate stderr output */
  dup2(fd1, 2);
  close(fd1);

  return (out);
}


/*
 * 'cat_static()' - Return pointer to static PPD file
 */

static cups_file_t *			/* O - Pointer to PPD file */
cat_static(const char *name,		/* I - PPD name */
	   cf_logfunc_t log,	/* I - Log function */
	   void *ld)			/* I - Aux. data for log function */
{
  cups_file_t	*fp;

  if ((fp = cupsFileOpen(name, "r")) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to open \"%s\" - %s",
		 name, strerror(errno));

    return (NULL);
  }

  return(fp);
}


/*
 * 'cat_tar()' - Copy an archived PPD file to temp file.
 */

static cups_file_t *			/* O - Pointer to PPD file */
cat_tar(const char *filename,		/* I - Archive name */
	char *ppdname,			/* I - PPD name in the archive */
	cf_logfunc_t log,		/* I - Log function */
	void *ld)			/* I - Aux. data for log function */
{
  cups_file_t	*fp;			/* Archive file pointer */
  int		fd;
  char		tempname[1024],		/* Name for the temporary file */
		curname[256],		/* Current name in archive */
		buffer[8192];		/* Copy buffer */
  struct stat	curinfo;		/* Current file info in archive */
  off_t		total,			/* Total bytes copied */
		next;			/* Offset for next record in archive */
  ssize_t	bytes;			/* Bytes read */


 /*
  * Open the archive file...
  */

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to open \"%s\" - %s",
		 filename, strerror(errno));

    return (NULL);
  }

 /*
  * Scan the archive for the PPD...
  */

  while (read_tar(fp, curname, sizeof(curname), &curinfo, log, ld))
  {
    next = cupsFileTell(fp) + ((curinfo.st_size + TAR_BLOCK - 1) &
                               ~(TAR_BLOCK - 1));

    if (!strcmp(ppdname, curname))
    {
      if ((fd = cupsTempFd(tempname, sizeof(tempname))) < 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] Unable to copy PPD to temp "
		     "file: %s",
		     strerror(errno));
	return (NULL);
      }
      for (total = 0; total < curinfo.st_size; total += bytes)
      {
        if ((size_t)(bytes = (curinfo.st_size - total)) > sizeof(buffer))
          bytes = sizeof(buffer);

        if ((bytes = cupsFileRead(fp, buffer, (size_t)bytes)) < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
          {
            bytes = 0;
          }
          else
          {
	    if (log) log(ld, CF_LOGLEVEL_ERROR,
			 "libppd: [PPD Collections] Read error - %s",
			 strerror(errno));
	    cupsFileClose(fp);
	    close(fd);
	    unlink(tempname);
	    return(NULL);
          }
        }
        else if (bytes > 0 && write(fd, buffer, bytes) != bytes)
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "libppd: [PPD Collections] Write error - %s",
		       strerror(errno));
	  cupsFileClose(fp);
	  close(fd);
	  unlink(tempname);
	  return(NULL);
	}
      }

      cupsFileClose(fp);
      close(fd);
      fp = cupsFileOpen(tempname, "r");
      unlink(tempname);
      
      return(fp);
    }

    if (cupsFileTell(fp) != next)
      cupsFileSeek(fp, next);
  }

  cupsFileClose(fp);

  if (log) log(ld, CF_LOGLEVEL_ERROR,
	       "libppd: [PPD Collections] PPD \"%s\" not found.", ppdname);

  return (NULL);
}


/*
 * 'compare_inodes()' - Compare two inodes.
 */

static int				/* O - Result of comparison */
compare_inodes(struct stat *a,		/* I - First inode */
               struct stat *b)		/* I - Second inode */
{
  if (a->st_dev != b->st_dev)
    return (a->st_dev - b->st_dev);
  else
    return (a->st_ino - b->st_ino);
}


/*
 * 'compare_matches()' - Compare PPD match scores for sorting.
 */

static int
compare_matches(const ppd_info_t *p0,	/* I - First PPD */
                const ppd_info_t *p1)	/* I - Second PPD */
{
  if (p1->matches != p0->matches)
    return (p1->matches - p0->matches);
  else
    return (CompareNames(p0->record.make_and_model,
			      p1->record.make_and_model));
}


/*
 * 'compare_names()' - Compare PPD filenames for sorting.
 */

static int				/* O - Result of comparison */
compare_names(const ppd_info_t *p0,	/* I - First PPD file */
              const ppd_info_t *p1)	/* I - Second PPD file */
{
  int	diff;				/* Difference between strings */


  if ((diff = strcmp(p0->record.filename, p1->record.filename)) != 0)
    return (diff);
  else
    return (strcmp(p0->record.name, p1->record.name));
}


/*
 * 'compare_ppds()' - Compare PPD file make and model names for sorting.
 */

static int				/* O - Result of comparison */
compare_ppds(const ppd_info_t *p0,	/* I - First PPD file */
             const ppd_info_t *p1)	/* I - Second PPD file */
{
  int	diff;				/* Difference between strings */


 /*
  * First compare manufacturers...
  */

  if ((diff = _ppd_strcasecmp(p0->record.make, p1->record.make)) != 0)
    return (diff);
  else if ((diff = CompareNames(p0->record.make_and_model,
                                     p1->record.make_and_model)) != 0)
    return (diff);
  else if ((diff = strcmp(p0->record.languages[0],
                          p1->record.languages[0])) != 0)
    return (diff);
  else
    return (compare_names(p0, p1));
}


/*
 * 'free_array()' - Free an array of strings.
 */

static void
free_array(cups_array_t *a)		/* I - Array to free */
{
  char	*ptr;				/* Pointer to string */


  for (ptr = (char *)cupsArrayFirst(a);
       ptr;
       ptr = (char *)cupsArrayNext(a))
    free(ptr);

  cupsArrayDelete(a);
}


/*
 * 'free_ppdlist()' - Free the PPD list arrays.
 */

static void
free_ppdlist(ppd_list_t *ppdlist)	/* I - PPD list to free */
{
  struct stat	*dinfoptr;		/* Pointer to Inode info */
  ppd_info_t	*ppd;			/* Pointer to PPD info */


  for (dinfoptr = (struct stat *)cupsArrayFirst(ppdlist->Inodes);
       dinfoptr;
       dinfoptr = (struct stat *)cupsArrayNext(ppdlist->Inodes))
    free(dinfoptr);
  cupsArrayDelete(ppdlist->Inodes);

  for (ppd = (ppd_info_t *)cupsArrayFirst(ppdlist->PPDsByName);
       ppd;
       ppd = (ppd_info_t *)cupsArrayNext(ppdlist->PPDsByName))
    free(ppd);
  cupsArrayDelete(ppdlist->PPDsByName);
  cupsArrayDelete(ppdlist->PPDsByMakeModel);
}


/*
 * 'load_driver()' - Load driver-generated PPD files.
 */

static int				/* O - 1 on success, 0 on failure */
load_driver(const char *filename,	/* I - Driver excutable file name */
	    const char *name,		/* I - Name to the rest of the world */
	    ppd_list_t *ppdlist,
	    cf_logfunc_t log,	/* I - Log function */
	    void *ld)			/* I - Aux. data for log function */
{
  int		i;			/* Looping var */
  char		*start,			/* Start of value */
		*ptr;			/* Pointer into string */
  const char	*scheme = NULL;		/* Scheme for this driver */
  int		cpid;			/* Process ID for driver program */
  int		epid;			/* Process ID for logging process */
  cups_file_t	*fp;			/* Pipe to driver program */
  char		*argv[3],		/* Arguments for command */
		line[2048],		/* Line from driver */
		ppd_name[256],		/* ppd-name */
		make[128],		/* ppd-make */
		make_and_model[128],	/* ppd-make-and-model */
		device_id[256],		/* ppd-device-id */
		languages[128],		/* ppd-natural-language */
		product[128],		/* ppd-product */
		psversion[128],		/* ppd-psversion */
		type_str[128];		/* ppd-type */
  int		type;			/* PPD type */
  ppd_info_t	*ppd;			/* Newly added PPD */


 /*
  * Run the driver with the "list" argument and collect the output...
  */

  argv[0] = (char *)filename;
  argv[1] = (char *)"list";
  argv[2] = NULL;

  if ((fp = PipeCommand(&cpid, &epid, filename, argv, 0, log, ld)) != NULL)
  {
    while (cupsFileGets(fp, line, sizeof(line)))
    {
     /*
      * Each line is of the form:
      *
      *   "ppd-name" ppd-natural-language "ppd-make" "ppd-make-and-model" \
      *       "ppd-device-id" "ppd-product" "ppd-psversion"
      */

      device_id[0] = '\0';
      product[0]   = '\0';
      psversion[0] = '\0';
      strlcpy(type_str, "postscript", sizeof(type_str));

      if (sscanf(line, "\"%255[^\"]\"%127s%*[ \t]\"%127[^\"]\""
		 "%*[ \t]\"%127[^\"]\"%*[ \t]\"%255[^\"]\""
		 "%*[ \t]\"%127[^\"]\"%*[ \t]\"%127[^\"]\""
		 "%*[ \t]\"%127[^\"]\"",
		 ppd_name, languages, make, make_and_model,
		 device_id, product, psversion, type_str) < 4)
      {
       /*
	* Bad format; strip trailing newline and write an error message.
	*/

	if (line[strlen(line) - 1] == '\n')
	  line[strlen(line) - 1] = '\0';

	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] Bad line from \"%s\": %s",
		     filename, line);
	break;
      }
      else
      {
       /*
	* Add the device to the array of available devices...
	*/

	if ((start = strchr(languages, ',')) != NULL)
	  *start++ = '\0';

	for (type = 0;
	     type < (int)(sizeof(PPDTypes) / sizeof(PPDTypes[0]));
	     type ++)
	  if (!strcmp(type_str, PPDTypes[type]))
	    break;

	if (type >= (int)(sizeof(PPDTypes) / sizeof(PPDTypes[0])))
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "libppd: [PPD Collections] Bad ppd-type \"%s\" ignored!",
		       type_str);
	  type = PPD_TYPE_UNKNOWN;
	}

	if ((scheme = strrchr(name, '/')) != NULL &&
	    !strncmp(scheme + 1, ppd_name, strlen(scheme + 1)) &&
	    (scheme - name) + strlen(ppd_name) + 1 < sizeof(ppd_name) &&
	    *(ptr = ppd_name + strlen(scheme + 1)) == ':')
	{
	  scheme ++;
	  memmove(ppd_name + strlen(name), ptr, strlen(ptr) + 1);
	  memmove(ppd_name, name, strlen(name));
	} else if (strncmp(name, ppd_name, strlen(name)) ||
		   *(ppd_name + strlen(name)) != ':')
	{
	  cupsFileClose(fp);
	  return (0);
	}

	if (scheme == 0)
	  scheme = name;

	ppd = add_ppd(filename, ppd_name, languages, make, make_and_model,
		      device_id, product, psversion, 0, 0, 0, type, scheme,
		      ppdlist, log, ld);

	if (!ppd)
	{
	  cupsFileClose(fp);
	  return (0);
	}

	if (start && *start)
	{
	  for (i = 1; i < PPD_MAX_LANG && *start; i ++)
	  {
	    if ((ptr = strchr(start, ',')) != NULL)
	      *ptr++ = '\0';
	    else
	      ptr = start + strlen(start);

	    strlcpy(ppd->record.languages[i], start,
		    sizeof(ppd->record.languages[0]));

	    start = ptr;
	  }
	}

	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "libppd: [PPD Collections] Adding PPD \"%s\"...",
		     ppd_name);
      }
    }

    ClosePipeCommand(fp, cpid, epid, log, ld);
  }
  else
    if (log) log(ld, CF_LOGLEVEL_WARN,
		 "libppd: [PPD Collections] Unable to execute \"%s\": %s",
		 filename, strerror(errno));

  return (1);
}


/*
 * 'load_drv()' - Load the PPDs from a driver information file.
 */

static int				/* O - 1 on success, 0 on failure */
load_drv(const char  *filename,		/* I - Actual filename */
         const char  *name,		/* I - Name to the rest of the world */
         cups_file_t *fp,		/* I - File to read from */
	 time_t      mtime,		/* I - Mod time of driver info file */
	 off_t       size,		/* I - Size of driver info file */
	 ppd_list_t  *ppdlist,
	 cf_logfunc_t log,		/* I - Log function */
	 void *ld)			/* I - Aux. data for log function */
{
  ppdcSource	*src;			// Driver information file
  ppdcDriver	*d;			// Current driver
  ppdcAttr	*device_id,		// 1284DeviceID attribute
		*product,		// Current product value
		*ps_version,		// PSVersion attribute
		*cups_fax,		// cupsFax attribute
		*nick_name;		// NickName attribute
  ppdcFilter	*filter;		// Current filter
  ppd_info_t	*ppd;			// Current PPD
  int		products_found;		// Number of products found
  char		uri[2048],		// Driver URI
		make_model[1024];	// Make and model
  int		type;			// Driver type
  int           fd1, fd2;


 /*
  * Eliminate any output to stderr, to get rid of the error messages of
  * the *.drv file parser
  */

  fd1 = dup(2);
  fd2 = open("/dev/null", O_WRONLY);
  dup2(fd2, 2);
  close(fd2);

 /*
  * Load the driver info file...
  */

  src = new ppdcSource(filename, fp);

  if (src->drivers->count == 0)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Bad driver information file \"%s\"!\n",
		 filename);
    src->release();
    /* Re-activate stderr output */
    dup2(fd1, 2);
    close(fd1);
    return (0);
  }

 /*
  * Add a dummy entry for the file...
  */

  add_ppd(filename, name, "", "", "", "", "", "", mtime, (size_t)size, 0,
	  PPD_TYPE_DRV, "drv", ppdlist, log, ld);

 /*
  * Then the drivers in the file...
  */

  for (d = (ppdcDriver *)src->drivers->first();
       d;
       d = (ppdcDriver *)src->drivers->next())
  {
    snprintf(uri, sizeof(uri), "%s:%s", name,
	     d->file_name ? d->file_name->value :
	     d->pc_file_name->value);

    device_id  = d->find_attr("1284DeviceID", NULL);
    ps_version = d->find_attr("PSVersion", NULL);
    nick_name  = d->find_attr("NickName", NULL);

    if (nick_name)
      strncpy(make_model, nick_name->value->value, sizeof(make_model) - 1);
    else if (strncasecmp(d->model_name->value, d->manufacturer->value,
                         strlen(d->manufacturer->value)))
      snprintf(make_model, sizeof(make_model), "%s %s, %s",
               d->manufacturer->value, d->model_name->value,
	       d->version->value);
    else
      snprintf(make_model, sizeof(make_model), "%s, %s", d->model_name->value,
               d->version->value);

    if ((cups_fax = d->find_attr("cupsFax", NULL)) != NULL &&
        !strcasecmp(cups_fax->value->value, "true"))
      type = PPD_TYPE_FAX;
    else if (d->type == PPDC_DRIVER_PS)
      type = PPD_TYPE_POSTSCRIPT;
    else if (d->type != PPDC_DRIVER_CUSTOM)
      type = PPD_TYPE_RASTER;
    else
    {
      for (filter = (ppdcFilter *)d->filters->first(),
               type = PPD_TYPE_POSTSCRIPT;
	   filter;
	   filter = (ppdcFilter *)d->filters->next())
        if (strcasecmp(filter->mime_type->value, "application/vnd.cups-raster"))
	  type = PPD_TYPE_RASTER;
        else if (strcasecmp(filter->mime_type->value,
	                    "application/vnd.cups-pdf"))
	  type = PPD_TYPE_PDF;
    }

    for (product = (ppdcAttr *)d->attrs->first(), products_found = 0,
             ppd = NULL;
         product;
	 product = (ppdcAttr *)d->attrs->next())
      if (!strcmp(product->name->value, "Product"))
      {
        if (!products_found)
	  ppd = add_ppd(filename, uri, "en", d->manufacturer->value,
			make_model, device_id ? device_id->value->value : "",
			product->value->value,
			ps_version ? ps_version->value->value : "(3010) 0",
			mtime, (size_t)size, d->model_number, type, "drv",
			ppdlist, log, ld);
	else if (products_found < PPD_MAX_PROD)
	  strncpy(ppd->record.products[products_found], product->value->value,
		  sizeof(ppd->record.products[0]));
	else
	  break;

	products_found ++;
      }

    if (!products_found)
      add_ppd(filename, uri, "en", d->manufacturer->value, make_model,
	      device_id ? device_id->value->value : "", d->model_name->value,
	      ps_version ? ps_version->value->value : "(3010) 0", mtime,
	      (size_t)size, d->model_number, type, "drv", ppdlist, log, ld);
  }

  src->release();

 /*
  * Re-activate stderr output
  */

  dup2(fd1, 2);
  close(fd1);

  return (1);
}


/*
 * 'load_ppd()' - Load a PPD file.
 */

static void
load_ppd(const char  *filename,		/* I - Real filename */
         const char  *name,		/* I - Virtual filename */
         const char  *scheme,		/* I - PPD scheme */
         struct stat *fileinfo,		/* I - File information */
         ppd_info_t  *ppd,		/* I - Existing PPD file or NULL */
         cups_file_t *fp,		/* I - File to read from */
         off_t       end,		/* I - End of file position or 0 */
	 ppd_list_t  *ppdlist,
	 cf_logfunc_t log,		/* I - Log function */
	 void *ld)			/* I - Aux. data for log function */
{
  int		i;			/* Looping var */
  char		line[256],		/* Line from file */
		*ptr,			/* Pointer into line */
		lang_version[64],	/* PPD LanguageVersion */
		lang_encoding[64],	/* PPD LanguageEncoding */
		country[64],		/* Country code */
		manufacturer[256],	/* Manufacturer */
		make_model[256],	/* Make and Model */
		model_name[256],	/* ModelName */
		nick_name[256],		/* NickName */
		device_id[256],		/* 1284DeviceID */
		product[256],		/* Product */
		psversion[256],		/* PSVersion */
		temp[512];		/* Temporary make and model */
  int		install_group,		/* In the installable options group? */
		model_number,		/* cupsModelNumber */
		type;			/* ppd-type */
  cups_array_t	*products,		/* Product array */
		*psversions,		/* PSVersion array */
		*cups_languages;	/* cupsLanguages array */
  int		new_ppd;		/* Is this a new PPD? */
  struct				/* LanguageVersion translation table */
  {
    const char	*version,		/* LanguageVersion string */
		*language;		/* Language code */
  }		languages[] =
  {
    { "chinese",		"zh" },
    { "czech",			"cs" },
    { "danish",			"da" },
    { "dutch",			"nl" },
    { "english",		"en" },
    { "finnish",		"fi" },
    { "french",			"fr" },
    { "german",			"de" },
    { "greek",			"el" },
    { "hungarian",		"hu" },
    { "italian",		"it" },
    { "japanese",		"ja" },
    { "korean",			"ko" },
    { "norwegian",		"no" },
    { "polish",			"pl" },
    { "portuguese",		"pt" },
    { "russian",		"ru" },
    { "simplified chinese",	"zh_CN" },
    { "slovak",			"sk" },
    { "spanish",		"es" },
    { "swedish",		"sv" },
    { "traditional chinese",	"zh_TW" },
    { "turkish",		"tr" }
  };


 /*
  * Now read until we get the required fields...
  */

  cups_languages = cupsArrayNew(NULL, NULL);
  products       = cupsArrayNew(NULL, NULL);
  psversions     = cupsArrayNew(NULL, NULL);

  model_name[0]    = '\0';
  nick_name[0]     = '\0';
  manufacturer[0]  = '\0';
  device_id[0]     = '\0';
  lang_encoding[0] = '\0';
  strlcpy(lang_version, "en", sizeof(lang_version));
  model_number     = 0;
  install_group    = 0;
  type             = PPD_TYPE_POSTSCRIPT;

  while ((end == 0 || cupsFileTell(fp) < end) &&
	 cupsFileGets(fp, line, sizeof(line)))
  {
    if (!strncmp(line, "*Manufacturer:", 14))
      sscanf(line, "%*[^\"]\"%255[^\"]", manufacturer);
    else if (!strncmp(line, "*ModelName:", 11))
      sscanf(line, "%*[^\"]\"%127[^\"]", model_name);
    else if (!strncmp(line, "*LanguageEncoding:", 18))
      sscanf(line, "%*[^:]:%63s", lang_encoding);
    else if (!strncmp(line, "*LanguageVersion:", 17))
      sscanf(line, "%*[^:]:%63s", lang_version);
    else if (!strncmp(line, "*NickName:", 10))
      sscanf(line, "%*[^\"]\"%255[^\"]", nick_name);
    else if (!_ppd_strncasecmp(line, "*1284DeviceID:", 14))
    {
      sscanf(line, "%*[^\"]\"%255[^\"]", device_id);

      // Make sure device ID ends with a semicolon...
      if (device_id[0] && device_id[strlen(device_id) - 1] != ';')
	strlcat(device_id, ";", sizeof(device_id));
    }
    else if (!strncmp(line, "*Product:", 9))
    {
      if (sscanf(line, "%*[^\"]\"(%255[^\"]", product) == 1)
      {
       /*
	* Make sure the value ends with a right parenthesis - can't stop at
	* the first right paren since the product name may contain escaped
	* parenthesis...
	*/

	ptr = product + strlen(product) - 1;
	if (ptr > product && *ptr == ')')
	{
	 /*
	  * Yes, ends with a parenthesis, so remove it from the end and
	  * add the product to the list...
	  */

	  *ptr = '\0';
	  cupsArrayAdd(products, strdup(product));
	}
      }
    }
    else if (!strncmp(line, "*PSVersion:", 11))
    {
      sscanf(line, "%*[^\"]\"%255[^\"]", psversion);
      cupsArrayAdd(psversions, strdup(psversion));
    }
    else if (!strncmp(line, "*cupsLanguages:", 15))
    {
      char	*start;			/* Start of language */


      for (start = line + 15; *start && isspace(*start & 255); start ++);

      if (*start++ == '\"')
      {
	while (*start)
	{
	  for (ptr = start + 1;
	       *ptr && *ptr != '\"' && !isspace(*ptr & 255);
	       ptr ++);

	  if (*ptr)
	  {
	    *ptr++ = '\0';

	    while (isspace(*ptr & 255))
	      *ptr++ = '\0';
	  }

	  cupsArrayAdd(cups_languages, strdup(start));
	  start = ptr;
	}
      }
    }
    else if (!strncmp(line, "*cupsFax:", 9))
    {
      for (ptr = line + 9; isspace(*ptr & 255); ptr ++);

      if (!_ppd_strncasecmp(ptr, "true", 4))
	type = PPD_TYPE_FAX;
    }
    else if ((!strncmp(line, "*cupsFilter:", 12) ||
	      !strncmp(line, "*cupsFilter2:", 13)) &&
	     type == PPD_TYPE_POSTSCRIPT)
    {
      if (strstr(line + 12, "application/vnd.cups-raster"))
	type = PPD_TYPE_RASTER;
      else if (strstr(line + 12, "application/vnd.cups-pdf"))
	type = PPD_TYPE_PDF;
    }
    else if (!strncmp(line, "*cupsModelNumber:", 17))
      sscanf(line, "*cupsModelNumber:%d", &model_number);
    else if (!strncmp(line, "*OpenGroup: Installable", 23))
      install_group = 1;
    else if (!strncmp(line, "*CloseGroup:", 12))
      install_group = 0;
    else if (!strncmp(line, "*OpenUI", 7))
    {
     /*
      * Stop early if we have a NickName or ModelName attributes
      * before the first non-installable OpenUI...
      */

      if (!install_group && (model_name[0] || nick_name[0]) &&
	  cupsArrayCount(products) > 0 && cupsArrayCount(psversions) > 0)
	break;
    }
  }

 /*
  * See if we got all of the required info...
  */

  if (nick_name[0])
    cupsCharsetToUTF8((cups_utf8_t *)make_model, nick_name,
		      sizeof(make_model), ppdGetEncoding(lang_encoding));
  else
    strlcpy(make_model, model_name, sizeof(make_model));

  while (isspace(make_model[0] & 255))
    _ppd_strcpy(make_model, make_model + 1);

  if (!make_model[0] || cupsArrayCount(products) == 0 ||
      cupsArrayCount(psversions) == 0)
  {
   /*
    * We don't have all the info needed, so skip this file...
    */

    if (!make_model[0])
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "libppd: [PPD Collections] Missing NickName and ModelName "
		   "in %s!",
		   filename);

    if (cupsArrayCount(products) == 0)
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "libppd: [PPD Collections] Missing Product in %s!",
		   filename);

    if (cupsArrayCount(psversions) == 0)
      if (log) log(ld, CF_LOGLEVEL_WARN,
		   "libppd: [PPD Collections] Missing PSVersion in %s!",
		   filename);

    free_array(products);
    free_array(psversions);
    free_array(cups_languages);

    return;
  }

  if (model_name[0])
    cupsArrayAdd(products, strdup(model_name));

 /*
  * Normalize the make and model string...
  */

  while (isspace(manufacturer[0] & 255))
    _ppd_strcpy(manufacturer, manufacturer + 1);

  if (!_ppd_strncasecmp(make_model, manufacturer, strlen(manufacturer)))
    strlcpy(temp, make_model, sizeof(temp));
  else
    snprintf(temp, sizeof(temp), "%s %s", manufacturer, make_model);

  ppdNormalizeMakeAndModel(temp, make_model, sizeof(make_model));

 /*
  * See if we got a manufacturer...
  */

  if (!manufacturer[0] || !strcmp(manufacturer, "ESP"))
  {
   /*
    * Nope, copy the first part of the make and model then...
    */

    strlcpy(manufacturer, make_model, sizeof(manufacturer));

   /*
    * Truncate at the first space, dash, or slash, or make the
    * manufacturer "Other"...
    */

    for (ptr = manufacturer; *ptr; ptr ++)
      if (*ptr == ' ' || *ptr == '-' || *ptr == '/')
	break;

    if (*ptr && ptr > manufacturer)
      *ptr = '\0';
    else
      strlcpy(manufacturer, "Other", sizeof(manufacturer));
  }
  else if (!_ppd_strncasecmp(manufacturer, "LHAG", 4) ||
	   !_ppd_strncasecmp(manufacturer, "linotype", 8))
    strlcpy(manufacturer, "LHAG", sizeof(manufacturer));
  else if (!_ppd_strncasecmp(manufacturer, "Hewlett", 7))
    strlcpy(manufacturer, "HP", sizeof(manufacturer));

 /*
  * Fix the lang_version as needed...
  */

  if ((ptr = strchr(lang_version, '-')) != NULL)
    *ptr++ = '\0';
  else if ((ptr = strchr(lang_version, '_')) != NULL)
    *ptr++ = '\0';

  if (ptr)
  {
   /*
    * Setup the country suffix...
    */

    country[0] = '_';
    _ppd_strcpy(country + 1, ptr);
  }
  else
  {
   /*
    * No country suffix...
    */

    country[0] = '\0';
  }

  for (i = 0; i < (int)(sizeof(languages) / sizeof(languages[0])); i ++)
    if (!_ppd_strcasecmp(languages[i].version, lang_version))
      break;

  if (i < (int)(sizeof(languages) / sizeof(languages[0])))
  {
   /*
    * Found a known language...
    */

    snprintf(lang_version, sizeof(lang_version), "%s%s",
	     languages[i].language, country);
  }
  else
  {
   /*
    * Unknown language; use "xx"...
    */

    strlcpy(lang_version, "xx", sizeof(lang_version));
  }

 /*
  * Record the PPD file...
  */

  new_ppd = !ppd;

  if (new_ppd)
  {
   /*
    * Add new PPD file...
    */

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "libppd: [PPD Collections] Adding PPD \"%s\"...", name);

    ppd = add_ppd(filename, name, lang_version, manufacturer, make_model,
		  device_id, (char *)cupsArrayFirst(products),
		  (char *)cupsArrayFirst(psversions), fileinfo->st_mtime,
		  (size_t)fileinfo->st_size, model_number, type, scheme,
		  ppdlist, log, ld);

    if (!ppd)
      return;
  }
  else
  {
   /*
    * Update existing record...
    */

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "libppd: [PPD Collections] Updating ppd \"%s\"...", name);

    memset(ppd, 0, sizeof(ppd_info_t));

    ppd->found               = 1;
    ppd->record.mtime        = fileinfo->st_mtime;
    ppd->record.size         = fileinfo->st_size;
    ppd->record.model_number = model_number;
    ppd->record.type         = type;

    strlcpy(ppd->record.filename, filename, sizeof(ppd->record.filename));
    strlcpy(ppd->record.name, name, sizeof(ppd->record.name));
    strlcpy(ppd->record.languages[0], lang_version,
	    sizeof(ppd->record.languages[0]));
    strlcpy(ppd->record.products[0], (char *)cupsArrayFirst(products),
	    sizeof(ppd->record.products[0]));
    strlcpy(ppd->record.psversions[0], (char *)cupsArrayFirst(psversions),
	    sizeof(ppd->record.psversions[0]));
    strlcpy(ppd->record.make, manufacturer, sizeof(ppd->record.make));
    strlcpy(ppd->record.make_and_model, make_model,
	    sizeof(ppd->record.make_and_model));
    strlcpy(ppd->record.device_id, device_id, sizeof(ppd->record.device_id));
    strlcpy(ppd->record.scheme, scheme, sizeof(ppd->record.scheme));
  }

 /*
  * Add remaining products, versions, and languages...
  */

  for (i = 1;
       i < PPD_MAX_PROD && (ptr = (char *)cupsArrayNext(products)) != NULL;
       i ++)
    strlcpy(ppd->record.products[i], ptr,
	    sizeof(ppd->record.products[0]));

  for (i = 1;
       i < PPD_MAX_VERS && (ptr = (char *)cupsArrayNext(psversions)) != NULL;
       i ++)
    strlcpy(ppd->record.psversions[i], ptr,
	    sizeof(ppd->record.psversions[0]));

  for (i = 1, ptr = (char *)cupsArrayFirst(cups_languages);
       i < PPD_MAX_LANG && ptr;
       i ++, ptr = (char *)cupsArrayNext(cups_languages))
    strlcpy(ppd->record.languages[i], ptr,
	    sizeof(ppd->record.languages[0]));

 /*
  * Free products, versions, and languages...
  */

  free_array(cups_languages);
  free_array(products);
  free_array(psversions);

  ppdlist->ChangedPPD = 1;
}


/*
 * 'load_ppds()' - Load PPD files recursively.
 */

static int				/* O - 1 on success, 0 on failure */
load_ppds(const char *d,		/* I - Actual directory */
          const char *p,		/* I - Virtual path in name */
	  int        descend,		/* I - Descend into directories? */
	  ppd_list_t *ppdlist,
	  cf_logfunc_t log,		/* I - Log function */
	  void *ld)			/* I - Aux. data for log function */
{
  struct stat	dinfo,			/* Directory information */
		*dinfoptr;		/* Pointer to match */
  cups_file_t	*fp;			/* Pointer to file */
  cups_dir_t	*dir;			/* Directory pointer */
  cups_dentry_t	*dent;			/* Directory entry */
  char		filename[1024],		/* Name of PPD or directory */
		line[256],		/* Line from file */
		*ptr,			/* Pointer into name */
		name[1024];		/* Name of PPD file */
  ppd_info_t	*ppd,			/* New PPD file */
		key;			/* Search key */


 /*
  * See if we've loaded this directory before...
  */

  if (stat(d, &dinfo))
  {
    if (errno != ENOENT)
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Unable to stat \"%s\": %s", d,
		   strerror(errno));

    return (0);
  }
  else if (cupsArrayFind(ppdlist->Inodes, &dinfo))
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Skipping \"%s\": loop detected!",
		 d);
    return (1);
  }

 /*
  * Nope, add it to the Inodes array and continue...
  */

  dinfoptr = (struct stat *)malloc(sizeof(struct stat));
  memcpy(dinfoptr, &dinfo, sizeof(struct stat));
  cupsArrayAdd(ppdlist->Inodes, dinfoptr);

 /*
  * Check permissions...
  */

  //if (_ppdFileCheck(d, _PPD_FILE_CHECK_DIRECTORY, !geteuid(), log, ld))
  //  return (0);

  if ((dir = cupsDirOpen(d)) == NULL)
  {
    if (errno != ENOENT)
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "libppd: [PPD Collections] Unable to open PPD directory "
		   "\"%s\": %s\n",
		   d, strerror(errno));

    return (0);
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "libppd: [PPD Collections] Loading \"%s\"...", d);

  while ((dent = cupsDirRead(dir)) != NULL)
  {
   /*
    * Skip files/directories starting with "."...
    */

    if (dent->filename[0] == '.')
      continue;

   /*
    * See if this is a file...
    */

    if (!strcmp(d, "/"))
      snprintf(filename, sizeof(filename), "/%s", dent->filename);
    else
      snprintf(filename, sizeof(filename), "%s/%s", d, dent->filename);

    if (!strcmp(p, "/"))
      snprintf(name, sizeof(name), "/%s", dent->filename);
    else if (p[0])
      snprintf(name, sizeof(name), "%s/%s", p, dent->filename);
    else
      strlcpy(name, dent->filename, sizeof(name));

    if (S_ISDIR(dent->fileinfo.st_mode))
    {
     /*
      * Do subdirectory...
      */

      if (descend)
      {
	if (!load_ppds(filename, name, 1, ppdlist, log, ld))
	{
	  cupsDirClose(dir);
	  return (1);
	}
      }

      continue;
    }
    else if (strstr(filename, ".plist"))
    {
     /*
      * Skip plist files in the PPDs directory...
      */

      continue;
    }
    //else if (_ppdFileCheck(filename, _PPD_FILE_CHECK_FILE_ONLY, !geteuid(),
    //			   log, ld))
    //   continue;

   /*
    * See if this file has been scanned before...
    */

    strlcpy(key.record.filename, filename, sizeof(key.record.filename));
    strlcpy(key.record.name, name, sizeof(key.record.name));

    ppd = (ppd_info_t *)cupsArrayFind(ppdlist->PPDsByName, &key);

    if (ppd &&
	ppd->record.size == dent->fileinfo.st_size &&
	ppd->record.mtime == dent->fileinfo.st_mtime)
    {
     /*
      * Rewind to the first entry for this file...
      */

      while ((ppd = (ppd_info_t *)cupsArrayPrev(ppdlist->PPDsByName)) != NULL &&
	     !strcmp(ppd->record.filename, filename));

     /*
      * Then mark all of the matches for this file as found...
      */

      while ((ppd = (ppd_info_t *)cupsArrayNext(ppdlist->PPDsByName)) != NULL &&
	     !strcmp(ppd->record.filename, filename))
        ppd->found = 1;

      continue;
    }

   /*
    * No, file is new/changed, so re-scan it...
    */

    if ((fp = cupsFileOpen(filename, "r")) == NULL)
      continue;

   /*
    * Now see if this is a PPD file...
    */

    line[0] = '\0';
    cupsFileGets(fp, line, sizeof(line));

    if (!strncmp(line, "*PPD-Adobe:", 11))
    {
     /*
      * Yes, load it...
      */

      load_ppd(filename, name, "file", &dent->fileinfo, ppd, fp, 0, ppdlist,
	       log, ld);
    }
    else
    {
     /*
      * Nope, treat it as a an archive, a PPD-generating executable, or a
      * driver information file...
      */

      cupsFileRewind(fp);

      if ((ptr = strstr(filename, ".tar")) != NULL &&
          (!strcmp(ptr, ".tar") || !strcmp(ptr, ".tar.gz")))
        load_tar(filename, name, fp, dent->fileinfo.st_mtime,
                 dent->fileinfo.st_size, ppdlist, log, ld);
      else if ((ptr = strstr(filename, ".drv")) != NULL && !strcmp(ptr, ".drv"))
	load_drv(filename, name, fp, dent->fileinfo.st_mtime,
		 dent->fileinfo.st_size, ppdlist, log, ld);
      else if ((dent->fileinfo.st_mode & 0111) &&
	       S_ISREG(dent->fileinfo.st_mode))
      {
	/* File is not a PPD, not an archive, but executable, try whether
	   it generates PPDs... */
	load_driver(filename, name, ppdlist, log, ld);
      }
    }

   /*
    * Close the file...
    */

    cupsFileClose(fp);
  }

  cupsDirClose(dir);

  return (1);
}


/*
 * 'load_ppds_dat()' - Load the ppds.dat file.
 */

static int
load_ppds_dat(const char *filename,	/* I - Filename */
              int        verbose,	/* I - Be verbose? */
	      ppd_list_t *ppdlist,
	      cf_logfunc_t log,	/* I - Log function */
	      void *ld)			/* I - Aux. data for log function */
{
  ppd_info_t	*ppd;			/* Current PPD file */
  cups_file_t	*fp;			/* ppds.dat file */
  struct stat	fileinfo;		/* ppds.dat information */


  if (filename == NULL || !filename[0])
    return(0);

  if ((fp = cupsFileOpen(filename, "r")) != NULL)
  {
   /*
    * See if we have the right sync word...
    */

    unsigned ppdsync;			/* Sync word */
    int      num_ppds;			/* Number of PPDs */

    if ((size_t)cupsFileRead(fp, (char *)&ppdsync,
			     sizeof(ppdsync)) == sizeof(ppdsync) &&
        ppdsync == PPD_SYNC &&
        !stat(filename, &fileinfo) &&
	(((size_t)fileinfo.st_size - sizeof(ppdsync)) % sizeof(ppd_rec_t)) ==
	0 &&
	(num_ppds = ((size_t)fileinfo.st_size - sizeof(ppdsync)) /
	 sizeof(ppd_rec_t)) > 0)
    {
     /*
      * We have a ppds.dat file, so read it!
      */

      for (; num_ppds > 0; num_ppds --)
      {
	if ((ppd = (ppd_info_t *)calloc(1, sizeof(ppd_info_t))) == NULL)
	{
	  if (verbose)
	    if (log) log(ld, CF_LOGLEVEL_ERROR,
			 "libppd: [PPD Collections] Unable to allocate memory "
			 "for PPD!");
	  return(1);
	}

	if (cupsFileRead(fp, (char *)&(ppd->record), sizeof(ppd_rec_t)) > 0)
	{
	  cupsArrayAdd(ppdlist->PPDsByName, ppd);
	  cupsArrayAdd(ppdlist->PPDsByMakeModel, ppd);
	}
	else
	{
	  free(ppd);
	  break;
	}
      }

      if (verbose)
	if (log) log(ld, CF_LOGLEVEL_INFO,
		     "libppd: [PPD Collections] Read \"%s\", %d PPDs...",
		     filename, cupsArrayCount(ppdlist->PPDsByName));
    }

    cupsFileClose(fp);
  }

  return(0);
}


/*
 * 'load_tar()' - Load archived PPD files.
 */

static int				/* O - 1 on success, 0 on failure */
load_tar(const char  *filename,		/* I - Actual filename */
         const char  *name,		/* I - Name to the rest of the world */
         cups_file_t *fp,		/* I - File to read from */
	 time_t      mtime,		/* I - Mod time of driver info file */
	 off_t       size,		/* I - Size of driver info file */
	 ppd_list_t  *ppdlist,
	 cf_logfunc_t log,		/* I - Log function */
	 void *ld)			/* I - Aux. data for log function */
{
  char		curname[256],		/* Current archive file name */
		uri[2048];		/* Virtual file URI */
  const char	*curext;		/* Extension on file */
  struct stat	curinfo;		/* Current archive file information */
  off_t		next;			/* Position for next header */


 /*
  * Add a dummy entry for the file...
  */

  add_ppd(filename, name, "", "", "", "", "", "", mtime, (size_t)size, 0,
	  PPD_TYPE_ARCHIVE, "file", ppdlist, log, ld);
  ppdlist->ChangedPPD = 1;

 /*
  * Scan for PPDs in the archive...
  */

  while (read_tar(fp, curname, sizeof(curname), &curinfo, log, ld))
  {
    next = cupsFileTell(fp) + ((curinfo.st_size + TAR_BLOCK - 1) &
                               ~(TAR_BLOCK - 1));

    if ((curext = strrchr(curname, '.')) != NULL &&
        !_ppd_strcasecmp(curext, ".ppd"))
    {
      snprintf(uri, sizeof(uri), "%s:%s", name, curname);
      load_ppd(filename, uri, "file", &curinfo, NULL, fp, next, ppdlist,
	       log, ld);
    }

    if (cupsFileTell(fp) != next)
      cupsFileSeek(fp, next);
  }

  return (1);
}


/*
 * 'read_tar()' - Read a file header from an archive.
 *
 * This function skips all directories and special files.
 */

static int				/* O - 1 if found, 0 on EOF */
read_tar(cups_file_t *fp,		/* I - Archive to read */
         char        *name,		/* I - Filename buffer */
         size_t      namesize,		/* I - Size of filename buffer */
         struct stat *info,		/* O - File information */
	 cf_logfunc_t log,		/* I - Log function */
	 void *ld)			/* I - Aux. data for log function */
{
  tar_rec_t	record;			/* Record from file */


  while ((size_t)cupsFileRead(fp, (char *)&record,
			      sizeof(record)) == sizeof(record))
  {
   /*
    * Check for a valid tar header...
    */

    if ((memcmp(record.header.magic, TAR_MAGIC, 6) ||
	 memcmp(record.header.version, TAR_VERSION, 2)) &&
	memcmp(record.header.magic, TAR_OLDGNU_MAGIC, 8))
    {
      if (record.header.magic[0] ||
          memcmp(record.header.magic, record.header.magic + 1, 5))
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] Bad tar magic/version.");
      break;
    }

   /*
    * Ignore non-files...
    */

    if (record.header.linkflag != TAR_OLDNORMAL &&
        record.header.linkflag != TAR_NORMAL)
      continue;

   /*
    * Grab size and name from tar header and return...
    */

    if (record.header.prefix[0])
      snprintf(name, namesize, "%s/%s", record.header.prefix,
               record.header.pathname);
    else
      strlcpy(name, record.header.pathname, namesize);

    info->st_mtime = strtol(record.header.mtime, NULL, 8);
    info->st_size  = strtoll(record.header.size, NULL, 8);

    return (1);
  }

  return (0);
}


/*
 * 'regex_device_id()' - Compile a regular expression based on the 1284 device
 *                       ID.
 */

static regex_t *			/* O - Regular expression */
regex_device_id(const char *device_id,	/* I - IEEE-1284 device ID */
		cf_logfunc_t log,	/* I - Log function */
		void *ld)		/* I - Aux. data for log function */
{
  char		res[2048],		/* Regular expression string */
		*ptr;			/* Pointer into string */
  regex_t	*re;			/* Regular expression */
  int		cmd;			/* Command set string? */


  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "libppd: [PPD Collections] regex_device_id(\"%s\")",
	       device_id);

 /*
  * Scan the device ID string and insert class, command set, manufacturer, and
  * model attributes to match.  We assume that the device ID in the PPD and the
  * device ID reported by the device itself use the same attribute names and
  * order of attributes.
  */

  ptr = res;

  while (*device_id && ptr < (res + sizeof(res) - 6))
  {
    cmd = !_ppd_strncasecmp(device_id, "COMMAND SET:", 12) ||
          !_ppd_strncasecmp(device_id, "CMD:", 4);

    if (cmd || !_ppd_strncasecmp(device_id, "MANUFACTURER:", 13) ||
        !_ppd_strncasecmp(device_id, "MFG:", 4) ||
        !_ppd_strncasecmp(device_id, "MFR:", 4) ||
        !_ppd_strncasecmp(device_id, "MODEL:", 6) ||
        !_ppd_strncasecmp(device_id, "MDL:", 4))
    {
      if (ptr > res)
      {
        *ptr++ = '.';
	*ptr++ = '*';
      }

      *ptr++ = '(';

      while (*device_id && *device_id != ';' && ptr < (res + sizeof(res) - 8))
      {
        if (strchr("[]{}().*\\|", *device_id))
	  *ptr++ = '\\';
        if (*device_id == ':')
	{
	 /*
	  * KEY:.*value
	  */

	  *ptr++ = *device_id++;
	  *ptr++ = '.';
	  *ptr++ = '*';
	}
	else
	  *ptr++ = *device_id++;
      }

      if (*device_id == ';' || !*device_id)
      {
       /*
        * KEY:.*value.*;
	*/

	*ptr++ = '.';
	*ptr++ = '*';
        *ptr++ = ';';
      }
      *ptr++ = ')';
      if (cmd)
        *ptr++ = '?';
    }
    else if ((device_id = strchr(device_id, ';')) == NULL)
      break;
    else
      device_id ++;
  }

  *ptr = '\0';

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "libppd: [PPD Collections] regex_device_id: \"%s\"", res);

 /*
  * Compile the regular expression and return...
  */

  if (res[0] && (re = (regex_t *)calloc(1, sizeof(regex_t))) != NULL)
  {
    if (!regcomp(re, res, REG_EXTENDED | REG_ICASE))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "libppd: [PPD Collections] regex_device_id: OK");
      return (re);
    }

    free(re);
  }

  return (NULL);
}


/*
 * 'regex_string()' - Construct a regular expression to compare a simple string.
 */

static regex_t *			/* O - Regular expression */
regex_string(const char *s,		/* I - String to compare */
	     cf_logfunc_t log,	/* I - Log function */
	     void *ld)			/* I - Aux. data for log function */
{
  char		res[2048],		/* Regular expression string */
		*ptr;			/* Pointer into string */
  regex_t	*re;			/* Regular expression */


  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "libppd: [PPD Collections] regex_string(\"%s\")", s);

 /*
  * Convert the string to a regular expression, escaping special characters
  * as needed.
  */

  ptr = res;

  while (*s && ptr < (res + sizeof(res) - 2))
  {
    if (strchr("[]{}().*\\", *s))
      *ptr++ = '\\';

    *ptr++ = *s++;
  }

  *ptr = '\0';

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "libppd: [PPD Collections] regex_string: \"%s\"", res);

 /*
  * Create a case-insensitive regular expression...
  */

  if (res[0] && (re = (regex_t *)calloc(1, sizeof(regex_t))) != NULL)
  {
    if (!regcomp(re, res, REG_ICASE))
    {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "libppd: [PPD Collections] regex_string: OK");
      return (re);
    }

    free(re);
  }

  return (NULL);
}


/*
 * 'CompareNames()' - Compare two names.
 *
 * This function basically does a _ppd_strcasecmp() of the two strings,
 * but is also aware of numbers so that "a2" < "a100".
 */

int					/* O - Result of comparison */
CompareNames(const char *s,		/* I - First string */
	     const char *t)		/* I - Second string */
{
  int		diff,			/* Difference between digits */
		digits;			/* Number of digits */


 /*
  * Loop through both names, returning only when a difference is
  * seen.  Also, compare whole numbers rather than just characters, too!
  */

  while (*s && *t)
  {
    if (isdigit(*s & 255) && isdigit(*t & 255))
    {
     /*
      * Got a number; start by skipping leading 0's...
      */

      while (*s == '0')
        s ++;
      while (*t == '0')
        t ++;

     /*
      * Skip equal digits...
      */

      while (isdigit(*s & 255) && *s == *t)
      {
        s ++;
	t ++;
      }

     /*
      * Bounce out if *s and *t aren't both digits...
      */

      if (isdigit(*s & 255) && !isdigit(*t & 255))
        return (1);
      else if (!isdigit(*s & 255) && isdigit(*t & 255))
        return (-1);
      else if (!isdigit(*s & 255) || !isdigit(*t & 255))
        continue;

      if (*s < *t)
        diff = -1;
      else
        diff = 1;

     /*
      * Figure out how many more digits there are...
      */

      digits = 0;
      s ++;
      t ++;

      while (isdigit(*s & 255))
      {
        digits ++;
	s ++;
      }

      while (isdigit(*t & 255))
      {
        digits --;
	t ++;
      }

     /*
      * Return if the number or value of the digits is different...
      */

      if (digits < 0)
        return (-1);
      else if (digits > 0)
        return (1);
      else if (diff)
        return (diff);
    }
    else if (tolower(*s) < tolower(*t))
      return (-1);
    else if (tolower(*s) > tolower(*t))
      return (1);
    else
    {
      s ++;
      t ++;
    }
  }

 /*
  * Return the results of the final comparison...
  */

  if (*s)
    return (1);
  else if (*t)
    return (-1);
  else
    return (0);
}


/*
 * 'CreateStringsArray()' - Create a CUPS array of strings.
 */

cups_array_t *				/* O - CUPS array */
CreateStringsArray(const char *s)	/* I - Comma-delimited strings */
{
  if (!s || !*s)
    return (NULL);
  else
    return (_ppdArrayNewStrings(s, ','));
}


/*
 * 'ExecCommand()' - Run a program with the correct environment.
 *
 * On macOS, we need to update the CFProcessPath environment variable that
 * is passed in the environment so the child can access its bundled resources.
 */

int					/* O - exec() status */
ExecCommand(const char *command,		/* I - Full path to program */
          char       **argv)		/* I - Command-line arguments */
{
#ifdef __APPLE__
  int	i, j;				/* Looping vars */
  char	*envp[500],			/* Array of environment variables */
	cfprocesspath[1024],		/* CFProcessPath environment variable */
	linkpath[1024];			/* Link path for symlinks... */
  int	linkbytes;			/* Bytes for link path */


 /*
  * Some macOS programs are bundled and need the CFProcessPath environment
  * variable defined.  If the command is a symlink, resolve the link and point
  * to the resolved location, otherwise, use the command path itself.
  */

  if ((linkbytes = readlink(command, linkpath, sizeof(linkpath) - 1)) > 0)
  {
   /*
    * Yes, this is a symlink to the actual program, nul-terminate and
    * use it...
    */

    linkpath[linkbytes] = '\0';

    if (linkpath[0] == '/')
      snprintf(cfprocesspath, sizeof(cfprocesspath), "CFProcessPath=%s",
	       linkpath);
    else
      snprintf(cfprocesspath, sizeof(cfprocesspath), "CFProcessPath=%s/%s",
	       dirname((char *)command), linkpath);
  }
  else
    snprintf(cfprocesspath, sizeof(cfprocesspath), "CFProcessPath=%s", command);

  envp[0] = cfprocesspath;

 /*
  * Copy the rest of the environment except for any CFProcessPath that may
  * already be there...
  */

  for (i = 1, j = 0;
       environ[j] && i < (int)(sizeof(envp) / sizeof(envp[0]) - 1);
       j ++)
    if (strncmp(environ[j], "CFProcessPath=", 14))
      envp[i ++] = environ[j];

  envp[i] = NULL;

 /*
  * Use execve() to run the program...
  */

  return (execve(command, argv, envp));

#else
 /*
  * On other operating systems, just call execv() to use the same environment
  * variables as the parent...
  */

  return (execv(command, argv));
#endif /* __APPLE__ */
}


/*
 * 'PipeCommand()' - Read output from a command.
 */

cups_file_t *				/* O - CUPS file or NULL on error */
PipeCommand(int        *cpid,		/* O - Process ID or 0 on error */
	    int        *epid,		/* O - Log Process ID or 0 on error */
	    const char *command,	/* I - Command to run */
	    char       **argv,		/* I - Arguments to pass to command */
	    uid_t      user,		/* I - User to run as or 0 for current*/
	    cf_logfunc_t log,
	    void       *ld)
{
  int	      fd,			/* Temporary file descriptor */
	      cfds[2],			/* Output Pipe file descriptors */
	      efds[2];			/* Error/Log Pipe file descriptors */
  cups_file_t *outfp, *logfp;
  char        buf[BUFSIZ];
  cf_loglevel_t log_level;
  char        *msg;


  *cpid = *epid = 0;
  cfds[0] = cfds[1] = efds[0] = efds[1] = -1;

 /*
  * First create the pipes...
  */

  if (pipe(cfds))
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to establish output pipe for %s call: %s",
		 argv[0], strerror(errno));
    return (NULL);
  }
  if (log)
    if (pipe(efds))
    {
      log(ld, CF_LOGLEVEL_ERROR,
	  "libppd: [PPD Collections] Unable to establish error/logging pipe for %s call: %s",
	  argv[0], strerror(errno));
      return (NULL);
    }

 /*
  * Set the "close on exec" flag on each end of the pipes...
  */

  if (fcntl(cfds[0], F_SETFD, fcntl(cfds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(cfds[0]);
    close(cfds[1]);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to set \"close on exec\" flag on read end of the output pipe for %s call: %s",
		 argv[0], strerror(errno));
    return (NULL);
  }

  if (fcntl(cfds[1], F_SETFD, fcntl(cfds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(cfds[0]);
    close(cfds[1]);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to set \"close on exec\" flag on write end of the output pipe for %s call: %s",
		 argv[0], strerror(errno));
    return (NULL);
  }

  if (log)
  {
    if (fcntl(efds[0], F_SETFD, fcntl(efds[0], F_GETFD) | FD_CLOEXEC))
    {
      close(cfds[0]);
      close(cfds[1]);
      close(efds[0]);
      close(efds[1]);
      log(ld, CF_LOGLEVEL_ERROR,
	  "libppd: [PPD Collections] Unable to set \"close on exec\" flag on read end of the error/logging pipe for %s call: %s",
	  argv[0], strerror(errno));
      return (NULL);
    }

    if (fcntl(efds[1], F_SETFD, fcntl(efds[1], F_GETFD) | FD_CLOEXEC))
    {
      close(cfds[0]);
      close(cfds[1]);
      close(efds[0]);
      close(efds[1]);
      log(ld, CF_LOGLEVEL_ERROR,
	  "libppd: [PPD Collections] Unable to set \"close on exec\" flag on write end of the error/logging pipe for %s call: %s",
	  argv[0], strerror(errno));
      return (NULL);
    }
  }

 /*
  * Then run the command...
  */

  if ((*cpid = fork()) < 0)
  {
   /*
    * Unable to fork!
    */

    *cpid = 0;
    close(cfds[0]);
    close(cfds[1]);
    if (log)
    {
      close(efds[0]);
      close(efds[1]);
    }

    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to fork for %s: %s", argv[0],
		 strerror(errno));

    return (NULL);
  }
  else if (!*cpid)
  {
   /*
    * Child (command) comes here...
    */

    if (!getuid() && user && setuid(user) < 0)	/* Run as restricted user */
      exit(errno);

    if ((fd = open("/dev/null", O_RDONLY)) > 0)
    {
      dup2(fd, 0);			/* < /dev/null */
      close(fd);
    }

    dup2(cfds[1], 1);			/* > command pipe */
    close(cfds[1]);

    if (log)
    {
      dup2(efds[1], 2);			/* 2> error pipe */
      close(efds[1]);
    }
    else if ((fd = open("/dev/null", O_WRONLY)) > 0)
    {
      dup2(fd, 2);			/* 2> /dev/null */
      close(fd);
    }

    ExecCommand(command, argv);
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "libppd: [PPD Collections] Unable to launch %s: %s", argv[0],
		 strerror(errno));
    exit(errno);
  }
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "libppd: [PPD Collections] Started %s (PID %d)", argv[0], cpid);

 /*
  * Parent comes here ...
  */

  close(cfds[1]);

 /*
  * Open the input side of the pipe...
  */

  outfp = cupsFileOpenFd(cfds[0], "r");

 /*
  * Fork the error logging...
  */

  if (log)
  {
    if ((*epid = fork()) < 0)
    {
     /*
      * Unable to fork!
      */

      *epid = 0;
      close(efds[0]);
      close(efds[1]);

      kill(*cpid, SIGTERM);
      ClosePipeCommand(outfp, *cpid, 0, log, ld);

      log(ld, CF_LOGLEVEL_ERROR,
	  "libppd: [PPD Collections] Unable to fork for logging for %s: %s",
	  argv[0], strerror(errno));

      return (NULL);
    }
    else if (!*epid)
    {
     /*
      * Child (logging) comes here...
      */

      close(cfds[0]);
      close(efds[1]);
      logfp = cupsFileOpenFd(efds[0], "r");
      while (cupsFileGets(logfp, buf, sizeof(buf)))
	if (log) {
	  if (strncmp(buf, "DEBUG: ", 7) == 0) {
	    log_level = CF_LOGLEVEL_DEBUG;
	    msg = buf + 7;
	  } else if (strncmp(buf, "DEBUG2: ", 8) == 0) {
	    log_level = CF_LOGLEVEL_DEBUG;
	    msg = buf + 8;
	  } else if (strncmp(buf, "INFO: ", 6) == 0) {
	    log_level = CF_LOGLEVEL_INFO;
	    msg = buf + 6;
	  } else if (strncmp(buf, "WARNING: ", 9) == 0) {
	    log_level = CF_LOGLEVEL_WARN;
	    msg = buf + 9;
	  } else if (strncmp(buf, "ERROR: ", 7) == 0) {
	    log_level = CF_LOGLEVEL_ERROR;
	    msg = buf + 7;
	  } else {
	    log_level = CF_LOGLEVEL_DEBUG;
	    msg = buf;
	  }
	  log(ld, log_level, "libppd: [PPD Collections] %s: %s", argv[0], msg);
	}
      cupsFileClose(logfp);
      /* No need to close the fd errfds[0], as cupsFileClose(fp) does this
	 already */
      /* Ignore errors of the logging process */
      exit(0);
    }

    log(ld, CF_LOGLEVEL_DEBUG,
	"libppd: [PPD Collections] Started logging for %s (PID %d)",
	argv[0], cpid);

   /*
    * Parent comes here ...
    */

    close(efds[0]);
    close(efds[1]);
  }

  return outfp;
}


/*
 * 'ClosePipeCommand()' - Wait for the command called with PipeCommand() to
 *                        finish and return the status.
 */

static int
ClosePipeCommand(cups_file_t *fp,
		 int cpid,
		 int epid,
		 cf_logfunc_t log,
		 void *ld)
{
  int           pid;
  int           status = 65536;
  int           wstatus;


 /*
  * close the stream...
  */

  cupsFileClose(fp);

 /*
  * Wait for the child process to exit...
  */

  while (cpid > 0 || epid > 0) {
    if ((pid = wait(&wstatus)) < 0) {
      if (errno == EINTR)
	continue;
      else
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] Error closing sub-processes: %s - killing processes",
		     strerror(errno));
	kill(cpid, SIGTERM);
	cpid = -1;
	kill(epid, SIGTERM);
	epid = -1;
	break;
      }
    }

    /* How did the filter terminate */
    if (wstatus) {
      if (WIFEXITED(wstatus)) {
	/* Via exit() anywhere or return() in the main() function */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] %s (PID %d) stopped with status %d",
		     (pid == cpid ? "Command" : "Logging"), pid,
		     WEXITSTATUS(wstatus));
	status = WEXITSTATUS(wstatus);
      } else {
	/* Via signal */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "libppd: [PPD Collections] %s (PID %d) crashed on signal %d",
		     (pid == cpid ? "Command" : "Logging"), pid,
		     WTERMSIG(wstatus));
	status = 256 * WTERMSIG(wstatus);
      }
    } else {
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "libppd: [PPD Collections] %s (PID %d) exited with no errors.",
		   (pid == cpid ? "Command" : "Logging"), pid);
      status = 0;
    }
    if (pid == cpid)
      cpid = -1;
    else  if (pid == epid)
      epid = -1;
  }

  return status;
}

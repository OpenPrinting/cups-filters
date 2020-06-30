/*
 * Sorted array routines for libppd.
 *
 * Copyright 2007-2014 by Apple Inc.
 * Copyright 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "string-private.h"
#include "debug-internal.h"
#include "array-private.h"


/*
 * '_ppdArrayAddStrings()' - Add zero or more delimited strings to an array.
 *
 * Note: The array MUST be created using the @link _ppdArrayNewStrings@
 * function. Duplicate strings are NOT added. If the string pointer "s" is NULL
 * or the empty string, no strings are added to the array.
 */

int					/* O - 1 on success, 0 on failure */
_ppdArrayAddStrings(cups_array_t *a,	/* I - Array */
                     const char   *s,	/* I - Delimited strings or NULL */
                     char         delim)/* I - Delimiter character */
{
  char		*buffer,		/* Copy of string */
		*start,			/* Start of string */
		*end;			/* End of string */
  int		status = 1;		/* Status of add */


  DEBUG_printf(("_ppdArrayAddStrings(a=%p, s=\"%s\", delim='%c')", (void *)a, s, delim));

  if (!a || !s || !*s)
  {
    DEBUG_puts("1_ppdArrayAddStrings: Returning 0");
    return (0);
  }

  if (delim == ' ')
  {
   /*
    * Skip leading whitespace...
    */

    DEBUG_puts("1_ppdArrayAddStrings: Skipping leading whitespace.");

    while (*s && isspace(*s & 255))
      s ++;

    DEBUG_printf(("1_ppdArrayAddStrings: Remaining string \"%s\".", s));
  }

  if (!strchr(s, delim) &&
      (delim != ' ' || (!strchr(s, '\t') && !strchr(s, '\n'))))
  {
   /*
    * String doesn't contain a delimiter, so add it as a single value...
    */

    DEBUG_puts("1_ppdArrayAddStrings: No delimiter seen, adding a single "
               "value.");

    if (!cupsArrayFind(a, (void *)s))
      status = cupsArrayAdd(a, (void *)s);
  }
  else if ((buffer = strdup(s)) == NULL)
  {
    DEBUG_puts("1_ppdArrayAddStrings: Unable to duplicate string.");
    status = 0;
  }
  else
  {
    for (start = end = buffer; *end; start = end)
    {
     /*
      * Find the end of the current delimited string and see if we need to add
      * it...
      */

      if (delim == ' ')
      {
        while (*end && !isspace(*end & 255))
          end ++;
        while (*end && isspace(*end & 255))
          *end++ = '\0';
      }
      else if ((end = strchr(start, delim)) != NULL)
        *end++ = '\0';
      else
        end = start + strlen(start);

      DEBUG_printf(("1_ppdArrayAddStrings: Adding \"%s\", end=\"%s\"", start,
                    end));

      if (!cupsArrayFind(a, start))
        status &= cupsArrayAdd(a, start);
    }

    free(buffer);
  }

  DEBUG_printf(("1_ppdArrayAddStrings: Returning %d.", status));

  return (status);
}


/*
 * '_ppdArrayNewStrings()' - Create a new array of comma-delimited strings.
 *
 * Note: The array automatically manages copies of the strings passed. If the
 * string pointer "s" is NULL or the empty string, no strings are added to the
 * newly created array.
 */

cups_array_t *				/* O - Array */
_ppdArrayNewStrings(const char *s,	/* I - Delimited strings or NULL */
                     char       delim)	/* I - Delimiter character */
{
  cups_array_t	*a;			/* Array */


  if ((a = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0,
                         (cups_acopy_func_t)_ppdStrAlloc,
			 (cups_afree_func_t)_ppdStrFree)) != NULL)
    _ppdArrayAddStrings(a, s, delim);

  return (a);
}

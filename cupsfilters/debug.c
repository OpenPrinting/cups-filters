//
// Debugging functions for cupsfilters.
//
// Copyright © 2008-2018 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//


//
// Include necessary headers...
//

#include <stdio.h>
#include <stdarg.h>


//
// '_cf_debug_printf()' - Write a formatted line to the log.
//

void
_cf_debug_printf(const char *format,	// I - Printf-style format string
                 ...)			// I - Additional arguments as needed
{
  va_list		ap;		// Pointer to arguments

  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fflush(stderr);
}


//
// '_cf_debug_puts()' - Write a single line to the log.
//

void
_cf_debug_puts(const char *s)		// I - String to output
{
  fputs(s, stderr);
  fflush(stderr);
}

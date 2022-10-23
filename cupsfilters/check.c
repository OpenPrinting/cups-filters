//
// Byte checking routines for libcupsfilters.
//
// Copyright 2007 by Apple Inc.
// Copyright 1993-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Contents:
//
//   cfCheckBytes() - Check to see if all bytes are zero.
//   cfCheckValue() - Check to see if all bytes match the given value.


//
// Include necessary headers.
//

#include "driver.h"


//
// 'cfCheckBytes()' - Check to see if all bytes are zero.
//

int						// O - 1 if they match
cfCheckBytes(const unsigned char *bytes,	// I - Bytes to check
	     int                 length)	// I - Number of bytes to check
{
  while (length > 7)
  {
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);
    if (*bytes++)
      return (0);

    length -= 8;
  }

  while (length > 0)
    if (*bytes++)
      return (0);
    else
      length --;

  return (1);
}


//
// 'cfCheckValue()' - Check to see if all bytes match the given value.
//

int						// O - 1 if they match
cfCheckValue(const unsigned char *bytes,	// I - Bytes to check
	     int                 length,	// I - Number of bytes to check
	     const unsigned char value)		// I - Value to check
{
  while (length > 7)
  {
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);
    if (*bytes++ != value)
      return (0);

    length -= 8;
  }

  while (length > 0)
    if (*bytes++ != value)
      return (0);
    else
      length --;

  return (1);
}

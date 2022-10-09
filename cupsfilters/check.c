//
//   Byte checking routines for cups-filters.
//
//   Copyright 2007 by Apple Inc.
//   Copyright 1993-2005 by Easy Software Products.
//
//   These coded instructions, statements, and computer programs are the
//   property of Apple Inc. and are protected by Federal copyright
//   law.  Distribution and use rights are outlined in the file "COPYING"
//   which should have been included with this file.
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

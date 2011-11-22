//========================================================================
//
// UGooString.h
//
// Unicode string
//
// Copyright 2005 Albert Astals Cid <aacid@kde.org>
//
//========================================================================

#ifndef UGooString_H
#define UGooString_H

#include "CharTypes.h"

class GooString;

class UGooString
{
public:
  // Create an unicode string
  UGooString(Unicode *u, int l);

  // Create a unicode string from <str>.
  UGooString(GooString &str);

  // Copy the unicode string
  UGooString(const UGooString &str);

  // Create a unicode string from <str>.
  UGooString(const char *str);

  // Destructor.
  ~UGooString();

  // Get length.
  int getLength() const { return length; }

  // Compare two strings:  -1:<  0:=  +1:>
  int cmp(UGooString *str) const;

  // get the unicode
  Unicode *unicode() const { return s; }

  // get the const char*
  char *getCString() const;

private:
  void initChar(GooString &str);

  int length;
  Unicode *s;
};

#endif

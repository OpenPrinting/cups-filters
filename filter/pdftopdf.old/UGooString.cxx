//========================================================================
//
// UGooString.cc
//
// Unicode string
//
// Copyright 2005 Albert Astals Cid <aacid@kde.org>
//
//========================================================================

#include <string.h>

#include "goo/gmem.h"
#include "goo/GooString.h"
#include "PDFDocEncoding.h"
#include "UGooString.h"

UGooString::UGooString(Unicode *u, int l)
{
  s = u;
  length = l;
}

UGooString::UGooString(GooString &str)
{
  if ((str.getChar(0) & 0xff) == 0xfe && (str.getChar(1) & 0xff) == 0xff)
  {
    length = (str.getLength() - 2) / 2;
    s = (Unicode *)gmallocn(length, sizeof(Unicode));
    for (int j = 0; j < length; ++j) {
      s[j] = ((str.getChar(2 + 2*j) & 0xff) << 8) | (str.getChar(3 + 2*j) & 0xff);
    }
  } else
    initChar(str);
}

UGooString::UGooString(const UGooString &str)
{
  length = str.length;
  s = (Unicode *)gmallocn(length, sizeof(Unicode));
  memcpy(s, str.s, length * sizeof(Unicode));
}

UGooString::UGooString(const char *str)
{
  GooString aux(str);
  initChar(aux);
}

void UGooString::initChar(GooString &str)
{
  length = str.getLength();
  s = (Unicode *)gmallocn(length, sizeof(Unicode));
  bool anyNonEncoded = false;
  for (int j = 0; j < length && !anyNonEncoded; ++j) {
    s[j] = pdfDocEncoding[str.getChar(j) & 0xff];
    if (!s[j]) anyNonEncoded = true;
  }
  if ( anyNonEncoded )
  {
    for (int j = 0; j < length; ++j) {
      s[j] = str.getChar(j);
    }
  }
}

UGooString::~UGooString()
{
  gfree(s);
}

int UGooString::cmp(UGooString *str) const
{
  int n1, n2, i, x;
  Unicode *p1, *p2;

  n1 = length;
  n2 = str->length;
  for (i = 0, p1 = s, p2 = str->s; i < n1 && i < n2; ++i, ++p1, ++p2) {
    x = *p1 - *p2;
    if (x != 0) {
      return x;
    }
  }
  return n1 - n2;
}

char *UGooString::getCString() const
{
  char *res = new char[length + 1];
  for (int i = 0; i < length; i++) res[i] = s[i];
  res[length] = '\0';
  return res;
}

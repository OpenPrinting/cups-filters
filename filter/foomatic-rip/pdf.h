//
// pdf.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef pdf_h
#define pdf_h

int print_pdf(FILE *s, const char *alreadyread, size_t len,
	      const char *filename, size_t startpos);
int pdf_count_pages(const char *filename);

#endif // !pdf_h

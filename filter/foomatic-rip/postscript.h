//
// postscript.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef postscript_h
#define postscript_h

int print_ps(FILE *s, const char *alreadyread, size_t len,
	     const char *filename);

#endif // !postscript_h

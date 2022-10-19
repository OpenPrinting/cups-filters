//
// spooler.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef SPOOLER_H
#define SPOOLER_H

#include "foomaticrip.h"
#include "util.h"

const char *spooler_name(int spooler);
void init_cups(list_t *arglist, dstr_t *filelist, jobparams_t *job);
void init_direct(list_t *arglist, dstr_t *filelist, jobparams_t *job);

#endif // !SPOOLER_H


//
// renderer.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef renderer_h
#define renderer_h

void massage_gs_commandline(dstr_t *cmd);
int exec_kid3(FILE *in, FILE *out, void *user_arg);

#endif // !renderer_h

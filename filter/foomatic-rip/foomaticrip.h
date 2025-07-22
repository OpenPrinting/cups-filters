//
// foomaticrip.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef foomatic_h
#define foomatic_h

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"

#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

// This is the location of the debug logfile (and also the copy of the
// processed PostScript data) in case you have enabled debugging above.
// The logfile will get the extension ".log", the PostScript data ".ps".

#ifndef LOG_FILE
#define LOG_FILE "/tmp/foomatic-rip"
#endif

// Supported spoolers are currently:
//
//   cups    - CUPS - Common Unix Printing System
//   direct  - Direct, spooler-less printing

#define SPOOLER_CUPS      1
#define SPOOLER_DIRECT    2

// The spooler from which foomatic-rip was called. set in main()
extern int spooler;

typedef struct
{
    char printer[256];
    char id[128];
    char user[128];
    char host[128];
    char title[2048];
    char ppdfile[2048];
    char copies[128];
    int rbinumcopies;
    struct dstr *optstr;
    time_t time;
} jobparams_t;


jobparams_t *get_current_job();

FILE *open_postpipe();

extern struct dstr *currentcmd;
extern struct dstr *jclappend;
extern char **jclprepend;
extern int jobhasjcl;
extern char cupsfilterpath[PATH_MAX];
extern int debug;
extern int do_docs;
extern char printer_model[];
extern int dontparse;
extern int pdfconvertedtops;
extern char gspath[PATH_MAX];
extern char echopath[PATH_MAX];

#endif


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


// Constants used by this filter
//
// Error codes, as some spoolers behave different depending on the reason why
// the RIP failed, we return an error code.

#define EXIT_PRINTED 0                          // file was printed normally
#define EXIT_PRNERR 1                           // printer error occured
#define EXIT_PRNERR_NORETRY 2                   // printer error with no hope
                                                // of retry
#define EXIT_JOBERR 3                           // job is defective
#define EXIT_SIGNAL 4                           // terminated after catching
                                                // signal
#define EXIT_ENGAGED 5                          // printer is otherwise engaged
                                                // (connection refused)
#define EXIT_STARVED 6                          // starved for system resources
#define EXIT_PRNERR_NORETRY_ACCESS_DENIED 7     // bad password? bad port
                                                // permissions?
#define EXIT_PRNERR_NOT_RESPONDING 8            // just doesn't answer at all
                                                // (turned off?)
#define EXIT_PRNERR_NORETRY_BAD_SETTINGS 9      // interface settings are
                                                // invalid
#define EXIT_PRNERR_NO_SUCH_ADDRESS 10          // address lookup failed, may
                                                // be transient
#define EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS 11  // address lookup failed, not
                                                // transient
#define EXIT_INCAPABLE 50                       // printer wants (lacks)
                                                // features or resources


// Supported spoolers are currently:
//
//   cups    - CUPS - Common Unix Printing System
//   direct  - Direct, spooler-less printing

#define SPOOLER_CUPS      1
#define SPOOLER_DIRECT    2

// The spooler from which foomatic-rip was called. set in main()
extern int spooler;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define CMDLINE_MAX 65536

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

void _log(const char* msg, ...);
int redirect_log_to_stderr();
void rip_die(int status, const char *msg, ...);

const char *get_modern_shell();
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


/* foomaticrip.c
 *
 * Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
 * Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
 *
 * This file is part of foomatic-rip.
 *
 * Foomatic-rip is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Foomatic-rip is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include "pdf.h"
#include "postscript.h"
#include "process.h"
#include "spooler.h"
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <memory.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <signal.h>
#include <pwd.h>
#include <cupsfilters/colormanager.h>

/* Logging */
FILE* logh = NULL;

void _logv(const char *msg, va_list ap)
{
    if (!logh)
        return;
    vfprintf(logh, msg, ap);
    fflush(logh);
}

void _log(const char* msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _logv(msg, ap);
    va_end(ap);
}

void close_log()
{
    if (logh && logh != stderr)
        fclose(logh);
}

int redirect_log_to_stderr()
{
    if (dup2(fileno(logh), fileno(stderr)) < 0) {
        _log("Could not dup logh to stderr\n");
        return 0;
    }
    return 1;
}

void rip_die(int status, const char *msg, ...)
{
    va_list ap;

    _log("Process is dying with \"");
    va_start(ap, msg);
    _logv(msg, ap);
    va_end(ap);
    _log("\", exit stat %d\n", status);

    _log("Cleaning up...\n");
    kill_all_processes();

    exit(status);
}


jobparams_t  *job = NULL;

jobparams_t * get_current_job()
{
    assert(job);
    return job;
}


dstr_t *postpipe;  /* command into which the output of this filter should be piped */
FILE *postpipe_fh = NULL;

FILE * open_postpipe()
{
    const char *p;

    if (postpipe_fh)
        return postpipe_fh;

    if (isempty(postpipe->data))
        return stdout;

    /* Delete possible '|' symbol in the beginning */
    p = skip_whitespace(postpipe->data);
    if (*p && *p == '|')
        p += 1;

    if (start_system_process("postpipe", p, &postpipe_fh, NULL) < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS,
                "Cannot execute postpipe %s\n", postpipe->data);

    return postpipe_fh;
}


char printer_model[256] = "";
char attrpath[256] = "";


int spooler = SPOOLER_DIRECT;
int dontparse = 0;
int jobhasjcl;
int pdfconvertedtops;


/* cm-calibration flag */
int cm_calibrate = 0;

int cm_disabled = 0;

/* These variables were in 'dat' before */
char colorprofile [128];
char cupsfilter[256];
char **jclprepend = NULL;
dstr_t *jclappend;

/* Set debug to 1 to enable the debug logfile for this filter; it will
 * appear as defined by LOG_FILE. It will contain status from this
 * filter, plus the renderer's stderr output. You can also add a line
 * "debug: 1" to your /etc/cups/foomatic-rip.conf or
 * /etc/foomatic/filter.conf to get all your Foomatic filters into
 * debug mode.  WARNING: This logfile is a security hole; do not use
 * in production. */
int debug = 0;

/* Path to the GhostScript which foomatic-rip shall use */
char gspath[PATH_MAX] = "gs";

/* What 'echo' program to use.  It needs -e and -n.  Linux's builtin
and regular echo work fine; non-GNU platforms may need to install
gnu echo and put gecho here or something. */
char echopath[PATH_MAX] = "echo";

/* CUPS raster drivers are searched here */
char cupsfilterpath[PATH_MAX] = "/usr/local/lib/cups/filter:"
                                "/usr/local/libexec/cups/filter:"
                                "/opt/cups/filter:"
                                "/usr/lib/cups/filter";

char modern_shell[] = SHELL;

void config_set_option(const char *key, const char *value)
{
    if (strcmp(key, "debug") == 0)
        debug = atoi(value);

    /* What path to use for filter programs and such
     *
     * Your printer driver must be in the path, as must be the renderer,
     * and possibly other stuff. The default path is often fine on Linux,
     * but may not be on other systems. */
    else if (strcmp(key, "execpath") == 0 && !isempty(value))
        setenv("PATH", value, 1);

    else if (strcmp(key, "cupsfilterpath") == 0)
        strlcpy(cupsfilterpath, value, PATH_MAX);
    else if (strcmp(key, "preferred_shell") == 0)
        strlcpy(modern_shell, value, 32);
    else if (strcmp(key, "gspath") == 0)
        strlcpy(gspath, value, PATH_MAX);
    else if (strcmp(key, "echo") == 0)
        strlcpy(echopath, value, PATH_MAX);
}

int config_from_file(const char *filename)
{
    FILE *fh;
    char line[256];
    char *key, *value;

    fh = fopen(filename, "r");
    if (fh == NULL)
        return 0;

    while (fgets(line, 256, fh) != NULL)
    {
        key = strtok(line, " :\t\r\n");
        if (key == NULL || key[0] == '#')
            continue;
        value = strtok(NULL, " \t\r\n#");
        config_set_option(key, value);
    }
    fclose(fh);

    return 1;
}

const char * get_modern_shell()
{
    return modern_shell;
}

/* returns position in 'str' after the option */
char * extract_next_option(char *str, char **pagerange, char **key, char **value)
{
    char *p = str;
    char quotechar;

    *pagerange = NULL;
    *key = NULL;
    *value = NULL;

    if (!str)
        return NULL;

    /* skip whitespace and commas */
    while (*p && (isspace(*p) || *p == ',')) p++;

    if (!*p)
        return NULL;

    /* read the pagerange if we have one */
    if (prefixcmp(p, "even:") == 0 || prefixcmp(p, "odd:") == 0 || isdigit(*p)) {
        *pagerange = p;
        p = strchr(p, ':');
        if (!p)
            return NULL;
        *p = '\0';
        p++;
    }

    /* read the key */
    if (*p == '\'' || *p == '\"') {
        quotechar = *p;
        *key = p +1;
        p = strchr(*key, quotechar);
        if (!p)
            return NULL;
    }
    else {
        *key = p;
        while (*p && *p != ':' && *p != '=' && *p != ' ') p++;
    }

    if (*p != ':' && *p != '=') { /* no value for this option */
        if (!*p)
            return NULL;
        else if (isspace(*p)) {
            *p = '\0';
            return p +1;
        }
        return p;
    }

    *p++ = '\0'; /* remove the separator sign */

    if (*p == '\"' || *p == '\'') {
        quotechar = *p;
        *value = p +1;
        p = strchr(*value, quotechar);
        if (!p)
            return NULL;
        *p = '\0';
        p++;
    }
    else {
        *value = p;
        while (*p && *p != ' ' && *p != ',') p++;
        if (*p == '\0')
            return NULL;
        *p = '\0';
        p++;
    }

    return *p ? p : NULL;
}

/* processes job->optstr */
void process_cmdline_options()
{
    char *p, *cmdlineopts, *nextopt, *pagerange, *key, *value;
    option_t *opt, *opt2;
    int optset;
    char tmp [256];

    _log("Printing system options:\n");
    cmdlineopts = strdup(job->optstr->data);
    for (nextopt = extract_next_option(cmdlineopts, &pagerange, &key, &value);
        key;
        nextopt = extract_next_option(nextopt, &pagerange, &key, &value))
    {
        /* Consider only options which are not in the PPD file here */
        if ((opt = find_option(key)) != NULL) continue;
        if (value)
            _log("Pondering option '%s=%s'\n", key, value);
        else
            _log("Pondering option '%s'\n", key);

        /* "profile" option to supply a color correction profile to a CUPS raster driver */
        if (!strcmp(key, "profile")) {
            strlcpy(colorprofile, value, 128);
            continue;
        }
        /* option to set color calibration mode */
        if (!strcmp(key, "cm-calibration")) {
            cm_calibrate = 1;
            continue;
        }
        /* Solaris options that have no reason to be */
        if (!strcmp(key, "nobanner") || !strcmp(key, "dest") || !strcmp(key, "protocol"))
            continue;

        if (pagerange) {
            snprintf(tmp, 256, "pages:%s", pagerange);
            optset = optionset(tmp);
        }
        else
            optset = optionset("userval");

        if (value) {
            if (strcasecmp(key, "media") == 0) {
                /*  Standard arguments?
                    media=x,y,z
                    sides=one|two-sided-long|short-edge

                    Rummage around in the media= option for known media, source,
                    etc types.
                    We ought to do something sensible to make the common manual
                    boolean option work when specified as a media= tray thing.

                    Note that this fails miserably when the option value is in
                    fact a number; they all look alike.  It's unclear how many
                    drivers do that.  We may have to standardize the verbose
                    names to make them work as selections, too. */

	        if (value[0] == '\0')
		    continue;
                p = strtok(value, ",");
                do {
                    if ((opt = find_option("PageSize")) && option_accepts_value(opt, p))
                        option_set_value(opt, optset, p);
                    else if ((opt = find_option("MediaType")) && option_has_choice(opt, p))
                        option_set_value(opt, optset, p);
                    else if ((opt = find_option("InputSlot")) && option_has_choice(opt, p))
                        option_set_value(opt, optset, p);
                    else if (!strcasecmp(p, "manualfeed")) {
                        /* Special case for our typical boolean manual
                           feeder option if we didn't match an InputSlot above */
                        if ((opt = find_option("ManualFeed")))
                            option_set_value(opt, optset, "1");
                    }
                    else
                        _log("Unknown \"media\" component: \"%s\".\n", p);

                } while ((p = strtok(NULL, ",")));
            }
            else if (!strcasecmp(key, "sides")) {
                /* Handle the standard duplex option, mostly */
                if (!prefixcasecmp(value, "two-sided")) {
                    if ((opt = find_option("Duplex"))) {
                        /* Default to long-edge binding here, for the case that
                           there is no binding setting */
                        option_set_value(opt, optset, "DuplexNoTumble");

                        /* Check the binding: "long edge" or "short edge" */
                        if (strcasestr(value, "long-edge")) {
                            if ((opt2 = find_option("Binding")))
                                option_set_value(opt2, optset, "LongEdge");
                            else
                                option_set_value(opt, optset, "DuplexNoTumble");
                        }
                        else if (strcasestr(value, "short-edge")) {
                            if ((opt2 = find_option("Binding")))
                                option_set_value(opt2, optset, "ShortEdge");
                            else
                                option_set_value(opt, optset, "DuplexNoTumble");
                        }
                    }
                }
                else if (!prefixcasecmp(value, "one-sided")) {
                    if ((opt = find_option("Duplex")))
                        option_set_value(opt, optset, "0");
                }

                /*  TODO
                    We should handle the other half of this option - the
                    BindEdge bit.  Also, are there well-known ipp/cups options
                    for Collate and StapleLocation?  These may be here...
                */
            }
	    else
	        _log("Unknown option %s=%s.\n", key, value);
        }
        /* Custom paper size */
        else if ((opt = find_option("PageSize")) && option_set_value(opt, optset, key)) {
            /* do nothing, if the value could be set, it has been set */
        }
        else
            _log("Unknown boolean option \"%s\".\n", key);
    }
    free(cmdlineopts);

    /* We 'clear' the profile if cm-calibration mode was specified */
    if (cm_calibrate) {
        colorprofile[0] = '\0';
        cm_disabled = 1;
    }

    _log("CM Color Calibration Mode in CUPS: %s\n", cm_calibrate ? 
         "Activated" : "Off");

    _log("Options from the PPD file:\n");
    cmdlineopts = strdup(job->optstr->data);
    for (nextopt = extract_next_option(cmdlineopts, &pagerange, &key, &value);
        key;
        nextopt = extract_next_option(nextopt, &pagerange, &key, &value))
    {
        /* Consider only PPD file options here */
        if ((opt = find_option(key)) == NULL) continue; 
        if (value)
            _log("Pondering option '%s=%s'\n", key, value);
        else
            _log("Pondering option '%s'\n", key);

        if (pagerange) {
            snprintf(tmp, 256, "pages:%s", pagerange);
            optset = optionset(tmp);

            if (opt && (option_get_section(opt) != SECTION_ANYSETUP &&
                        option_get_section(opt) != SECTION_PAGESETUP)) {
                _log("This option (%s) is not a \"PageSetup\" or \"AnySetup\" option, so it cannot be restricted to a page range.\n", key);
                continue;
            }
        }
        else
            optset = optionset("userval");

        if (value) {
	    /* Various non-standard printer-specific options */
	    if (!option_set_value(opt, optset, value)) {
	        _log("  invalid choice \"%s\", using \"%s\" instead\n", 
		     value, option_get_value(opt, optset));
	    }
        }
        /* Standard bool args:
           landscape; what to do here?
           duplex; we should just handle this one OK now? */
        else if (!prefixcasecmp(key, "no"))
            option_set_value(opt, optset, "0");
        else
            option_set_value(opt, optset, "1");
    }
    free(cmdlineopts);
}

/*  Functions to let foomatic-rip fork to do several tasks in parallel.

To do the filtering without loading the whole file into memory we work
on a data stream, we read the data line by line analyse it to decide what
filters to use and start the filters if we have found out which we need.
We buffer the data only as long as we didn't determing which filters to
use for this piece of data and with which options. There are no temporary
files used.

foomatic-rip splits into up to 3 parallel processes to do the whole
filtering (listed in the order of the data flow):

   MAIN: Prepare the job auto-detecting the spooler, reading the PPD,
         extracting the options from the command line, and parsing
         the job data itself. It analyses the job data to check
         whether it is PostScript or PDF, it also stuffs PostScript
         code from option settings into the PostScript data stream.
         It starts the renderer (KID3/KID4) as soon as it knows its
         command line and restarts it when page-specific option
         settings need another command line or different JCL commands.
   KID3: The rendering process. In most cases Ghostscript, "cat"
         for native PostScript printers with their manufacturer's
         PPD files.
   KID4: Put together the JCL commands and the renderer's output
         and send all that either to STDOUT or pipe it into the
         command line defined with $postpipe. */



void write_output(void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t left = len;
    FILE *postpipe = open_postpipe();

    /* Remove leading whitespace */
    while (isspace(*p++) && left-- > 0)
        ;

    fwrite_or_die((void *)p, left, 1, postpipe);
    fflush(postpipe);
}

enum FileType {
    UNKNOWN_FILE,
    PDF_FILE,
    PS_FILE
};

int guess_file_type(const char *begin, size_t len, int *startpos)
{
    const char * p, * end;
    p = begin;
    end = begin + len;

    while (p < end)
    {
        p = memchr(p, '%', end - p);
	if (!p)
	    return UNKNOWN_FILE;
	*startpos = p - begin;
	if ((end - p) > 2 && !memcmp(p, "%!", 2))
	    return PS_FILE;
	else if ((end - p) > 7 && !memcmp(p, "%PDF-1.", 7))
	    return PDF_FILE;
	++ p;
    }
    *startpos = 0;
    return UNKNOWN_FILE;
}

/*
 * Prints 'filename'. If 'convert' is true, the file will be converted if it is
 * not postscript or pdf
 */
int print_file(const char *filename, int convert)
{
    FILE *file;
    char buf[8192];
    int type;
    int startpos;
    size_t n;
    int ret;

    if (!strcasecmp(filename, "<STDIN>"))
        file = stdin;
    else {
        file = fopen(filename, "r");
        if (!file) {
            _log("Could not open \"%s\" for reading\n", filename);
            return 0;
        }
    }

    n = fread_or_die(buf, 1, sizeof(buf) - 1, file);
    buf[n] = '\0';
    type = guess_file_type(buf, n, &startpos);
    /* We do not use any JCL preceeded to the inputr data, as it is simply
       the PJL commands from the PPD file, and these commands we can also
       generate, end we even merge them with PJl from the driver */
    /*if (startpos > 0) {
        jobhasjcl = 1;
        write_output(buf, startpos);
    }*/
    if (file != stdin)
        rewind(file);

    if (convert) pdfconvertedtops = 0;

    switch (type) {
        case PDF_FILE:
            _log("Filetype: PDF\n");

            if (!ppd_supports_pdf())
            {
                char pdf2ps_cmd[CMDLINE_MAX];
                FILE *out, *in;
                int renderer_pid;
		char tmpfilename[PATH_MAX] = "";

                _log("Driver does not understand PDF input, "
                     "converting to PostScript\n");

		pdfconvertedtops = 1;

		/* If reading from stdin, write everything into a temporary file */
		if (file == stdin)
                {
		    int fd;
		    FILE *tmpfile;
		    
		    snprintf(tmpfilename, PATH_MAX, "%s/foomatic-XXXXXX", temp_dir());
		    fd = mkstemp(tmpfilename);
		    if (fd < 0) {
		        _log("Could not create temporary file: %s\n", strerror(errno));
		        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
		    }
		    tmpfile = fdopen(fd, "r+");
		    copy_file(tmpfile, stdin, buf, n);
		    fclose(tmpfile);
		    
		    filename = tmpfilename;
		}

		/* If the spooler is CUPS we use the pdftops filter of CUPS,
		   to have always the same PDF->PostScript conversion method
		   in the whole printing environment, including incompatibility
		   workarounds in the CUPS filter (so this way we also have to
		   maintain all these quirks only once).

		   The "-dNOINTERPOLATE" makes Ghostscript rendering
		   significantly faster.

		   The "-dNOMEDIAATTRS" makes Ghostscript not checking the
		   page sizes against a list of known sizes and try to
		   correct them.

		   Note that Ghostscript's "pswrite" output device turns text
		   into bitmaps and therefore produces huge PostScript files.
		   In addition, this output device is deprecated. Therefore
		   we use "ps2write".

		   We give priority to Ghostscript here and use Poppler if
		   Ghostscript is not available. */
		if (spooler == SPOOLER_CUPS)
		  snprintf(pdf2ps_cmd, CMDLINE_MAX,
			   "pdftops '%s' '%s' '%s' '%s' '%s' '%s'",
			   job->id, job->user, job->title, "1", job->optstr->data,
			   filename);
		else
		  snprintf(pdf2ps_cmd, CMDLINE_MAX,
			   "gs -q -sstdout=%%stderr -sDEVICE=ps2write -sOutputFile=- "
			   "-dBATCH -dNOPAUSE -dPARANOIDSAFER -dNOINTERPOLATE -dNOMEDIAATTRS -dShowAcroForm %s 2>/dev/null || "
			   "pdftops -level2 -origpagesizes %s - 2>/dev/null",
			   filename, filename);

                renderer_pid = start_system_process("pdf-to-ps", pdf2ps_cmd, &in, &out);

                if (dup2(fileno(out), fileno(stdin)) < 0)
                    rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS,
                            "Couldn't dup stdout of pdf-to-ps\n");

                clearerr(stdin);
                ret = print_file("<STDIN>", 0);

                wait_for_process(renderer_pid);
                if (in != NULL)
                  fclose(in);
                if (out != NULL)
                  fclose(out);

                return ret;
            }

            if (file == stdin)
                return print_pdf(stdin, buf, n, filename, startpos);
            else
                return print_pdf(file, NULL, 0, filename, startpos);

        case PS_FILE:
            _log("Filetype: PostScript\n");
            if (file == stdin)
                return print_ps(stdin, buf, n, filename);
            else
                return print_ps(file, NULL, 0, filename);

        case UNKNOWN_FILE:
	    _log("Cannot process \"%s\": Unknown filetype.\n", filename);
	    if (file != NULL)
	      fclose(file);
	    return 0;
    }

    fclose(file);
    return 1;
}

void signal_terminate(int signal)
{
    rip_die(EXIT_PRINTED, "Caught termination signal: Job canceled\n");
}

jobparams_t * create_job()
{
    jobparams_t *job = calloc(1, sizeof(jobparams_t));
    struct passwd *passwd;

    job->optstr = create_dstr();
    job->time = time(NULL);
    strcpy(job->copies, "1");
    gethostname(job->host, 128);
    passwd = getpwuid(getuid());
    if (passwd)
        strlcpy(job->user, passwd->pw_name, 128);
    snprintf(job->title, 2048, "%s@%s", job->user, job->host);

    return job;
}

void free_job(jobparams_t *job)
{
    free_dstr(job->optstr);
    free(job);
}

int main(int argc, char** argv)
{
    int i;
    int verbose = 0, quiet = 0;
    const char* str;
    char *p, *filename;
    const char *path;
    char tmp[1024], profile_arg[256], gstoraster[512];
    int havefilter, havegstoraster;
    dstr_t *filelist;
    list_t * arglist;

    arglist = list_create_from_array(argc -1, (void**)&argv[1]);

    if (argc == 2 && (arglist_find(arglist, "--version") || arglist_find(arglist, "--help") ||
                arglist_find(arglist, "-v") || arglist_find(arglist, "-h"))) {
        printf("foomatic-rip of cups-filters version "VERSION"\n");
        printf("\"man foomatic-rip\" for help.\n");
        list_free(arglist);
        return 0;
    }

    filelist = create_dstr();
    job = create_job();

    jclprepend = NULL;
    jclappend = create_dstr();
    postpipe = create_dstr();

    options_init();

    signal(SIGTERM, signal_terminate);
    signal(SIGINT, signal_terminate);
    signal(SIGPIPE, SIG_IGN);

    /* First try to find a config file in the CUS config directory, like
       /etc/cups/foomatic-rip.conf */
    i = 0;
    if ((str = getenv("CUPS_SERVERROOT")) != NULL) {
	snprintf(tmp, sizeof(tmp), "%s/foomatic-rip.conf", str);
	i = config_from_file(tmp);
    }
    /* If there is none, fall back to /etc/foomatic/filter.conf */
    if (i == 0) 
        i = config_from_file(CONFIG_PATH "/filter.conf");

    /* Command line options for verbosity */
    if (arglist_remove_flag(arglist, "-v"))
        verbose = 1;
    if (arglist_remove_flag(arglist, "-q"))
        quiet = 1;
    if (arglist_remove_flag(arglist, "--debug"))
        debug = 1;

    if (debug) {
#if defined(__UCLIBC__) || defined(__NetBSD__)
	sprintf(tmp, "%s-log-XXXXXX", LOG_FILE);
	int fd = mkstemp (tmp);
#else
	sprintf(tmp, "%s-XXXXXX.log", LOG_FILE);
	int fd = mkstemps (tmp, 4);
#endif
	if (fd != -1)
	    logh = fdopen(fd, "w");
	else
	    logh = stderr;
    } else if (quiet && !verbose)
        logh = NULL; /* Quiet mode, do not log */
    else
        logh = stderr; /* Default: log to stderr */

    /* Start debug logging */
    if (debug) {
        /* If we are not in debug mode, we do this later, as we must find out at
        first which spooler is used. When printing without spooler we
        suppress logging because foomatic-rip is called directly on the
        command line and so we avoid logging onto the console. */
        _log("foomatic-rip version "VERSION" running...\n");

        /* Print the command line only in debug mode, Mac OS X adds very many
        options so that CUPS cannot handle the output of the command line
        in its log files. If CUPS encounters a line with more than 1024
        characters sent into its log files, it aborts the job with an error. */
        if (spooler != SPOOLER_CUPS) {
            _log("called with arguments: ");
            for (i = 1; i < argc -1; i++)
                _log("\'%s\', ", argv[i]);
            _log("\'%s\'\n", argv[i]);
        }
    }

    if (getenv("PPD")) {
        strncpy(job->ppdfile, getenv("PPD"), 2048);
        if (strlen(getenv("PPD")) > 2047)
          job->ppdfile[2047] = '\0';
        spooler = SPOOLER_CUPS;
    if (getenv("CUPS_SERVERBIN")) {
        strncpy(cupsfilterpath, getenv("CUPS_SERVERBIN"), sizeof(cupsfilterpath));
        if (strlen(getenv("CUPS_SERVERBIN")) > PATH_MAX-1)
          cupsfilterpath[PATH_MAX-1] = '\0';
        }
    }

    /* Check status of printer color management from the color manager */
    cm_disabled = cmIsPrinterCmDisabled(getenv("PRINTER"));

    /* CUPS calls foomatic-rip only with 5 or 6 positional parameters,
       not with named options, like for example "-p <string>". */
    if (spooler != SPOOLER_CUPS) {

        if ((str = arglist_get_value(arglist, "-j")) || (str = arglist_get_value(arglist, "-J"))) {
            strncpy_omit(job->title, str, 2048, omit_shellescapes);
          if (!arglist_remove(arglist, "-j"))
        	arglist_remove(arglist, "-J");
	}

        /* PPD file name given via the command line
           allow duplicates, and use the last specified one */
            while ((str = arglist_get_value(arglist, "-p"))) {
                strncpy(job->ppdfile, str, 2048);
                if (strlen(str) > 2047)
                  job->ppdfile[2047] = '\0';
                arglist_remove(arglist, "-p");
            }
	    while ((str = arglist_get_value(arglist, "--ppd"))) {
	        strncpy(job->ppdfile, str, 2048);
	        if (strlen(str) > 2047)
	          job->ppdfile[2047] = '\0';
	        arglist_remove(arglist, "--ppd");
	    }

        /* Options for spooler-less printing */
        while ((str = arglist_get_value(arglist, "-o"))) {
            strncpy_omit(tmp, str, 1024, omit_shellescapes);
            dstrcatf(job->optstr, "%s ", tmp);
            /* if "-o cm-calibration" was passed, we raise a flag */
            if (!strcmp(tmp, "cm-calibration")) {
                cm_calibrate = 1;
                cm_disabled = 1;
            }
            arglist_remove(arglist, "-o");
	    /* We print without spooler */
	    spooler = SPOOLER_DIRECT;
        }

        /* Printer for spooler-less printing */
        if ((str = arglist_get_value(arglist, "-d"))) {
            strncpy_omit(job->printer, str, 256, omit_shellescapes);
            arglist_remove(arglist, "-d");
        }

        /* Printer for spooler-less printing */
        if ((str = arglist_get_value(arglist, "-P"))) {
            strncpy_omit(job->printer, str, 256, omit_shellescapes);
            arglist_remove(arglist, "-P");
        }

    }

    _log("'CM Color Calibration' Mode in SPOOLER-LESS: %s\n", cm_calibrate ? 
         "Activated" : "Off");

    /* spooler specific initialization */
    switch (spooler) {

        case SPOOLER_CUPS:
            init_cups(arglist, filelist, job);
            break;

        case SPOOLER_DIRECT:
            init_direct(arglist, filelist, job);
            break;
    }

    /* Files to be printed (can be more than one for spooler-less printing) */
    /* Empty file list -> print STDIN */
    dstrtrim(filelist);
    if (filelist->len == 0)
        dstrcpyf(filelist, "<STDIN>");

    /* Check filelist */
    p = strtok(strdup(filelist->data), " ");
    while (p) {
        if (strcmp(p, "<STDIN>") != 0) {
            if (p[0] == '-')
                rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Invalid argument: %s", p);
            else if (access(p, R_OK) != 0) {
                _log("File %s does not exist/is not readable\n", p);
            strclr(p);
            }
        }
        p = strtok(NULL, " ");
    }

    /* When we print without spooler do not log onto STDERR unless
       the "-v" ('Verbose') is set or the debug mode is used */
    if (spooler == SPOOLER_DIRECT && !verbose && !debug) {
        if (logh && logh != stderr)
            fclose(logh);
        logh = NULL;
    }

    /* If we are in debug mode, we do this earlier. */
    if (!debug) {
        _log("foomatic-rip version " VERSION " running...\n");
        /* Print the command line only in debug mode, Mac OS X adds very many
        options so that CUPS cannot handle the output of the command line
        in its log files. If CUPS encounters a line with more than 1024
        characters sent into its log files, it aborts the job with an error. */
        if (spooler != SPOOLER_CUPS) {
            _log("called with arguments: ");
            for (i = 1; i < argc -1; i++)
                _log("\'%s\', ", argv[i]);
            _log("\'%s\'\n", argv[i]);
        }
    }

    /* PPD File */
    /* Load the PPD file and build a data structure for the renderer's
       command line and the options */
    if (spooler == SPOOLER_CUPS && job->printer && strlen(job->printer) > 0) {
      str = cupsGetPPD(job->printer);
      if (str) {
        read_ppd_file(str);
        unlink(str);
      } else
        read_ppd_file(job->ppdfile);
    } else 
      read_ppd_file(job->ppdfile);

    /* We do not need to parse the PostScript job when we don't have
       any options. If we have options, we must check whether the
       default settings from the PPD file are valid and correct them
       if nexessary. */
    if (option_count() == 0) {
        /* We don't have any options, so we do not need to parse the
           PostScript data */
        dontparse = 1;
    }

    /* Is our PPD for a CUPS raster driver */
    if (!isempty(cupsfilter)) {
        /* Search the filter in cupsfilterpath
           The %Y is a placeholder for the option settings */
        havefilter = 0;
        path = cupsfilterpath;
        while ((path = strncpy_tochar(tmp, path, 1024, ":"))) {
            strlcat(tmp, "/", 1024);
            strlcat(tmp, cupsfilter, 1024);
            if (access(tmp, X_OK) == 0) {
                havefilter = 1;
                strlcpy(cupsfilter, tmp, 256);
                strlcat(cupsfilter, " 0 '' '' 0 '%Y%X'", 256);
                break;
            }
        }

        if (!havefilter) {
            /* We do not have the required filter, so we assume that
               rendering this job is supposed to be done on a remote
               server. So we do not define a renderer command line and
               embed only the option settings (as we had a PostScript
               printer). This way the settings are taken into account
               when the job is rendered on the server.*/
            _log("CUPS filter for this PPD file not found - assuming that job will "
                 "be rendered on a remote server. Only the PostScript of the options"
                 "will be inserted into the PostScript data stream.\n");
        }
        else {
            /* use gstoraster filter if available, otherwise run Ghostscript
	       directly */
            havegstoraster = 0;
            path = cupsfilterpath;
            while ((path = strncpy_tochar(tmp, path, 1024, ":"))) {
                strlcat(tmp, "/gstoraster", 1024);
                if (access(tmp, X_OK) == 0) {
                    havegstoraster = 1;
                    strlcpy(gstoraster, tmp, 256);
                    strlcat(gstoraster, " 0 '' '' 0 '%X'", 256);
                    break;
                }
            }
            if (!havegstoraster) {
                const char **qualifier = NULL;
                char *icc_profile = NULL;

                if (!cm_disabled) {
                  qualifier = get_ppd_qualifier();
                  _log("INFO: Using qualifer: '%s.%s.%s'\n",
                        qualifier[0], qualifier[1], qualifier[2]);

                  cmGetPrinterIccProfile(getenv("PRINTER"),
					 (char **)&icc_profile, 0);

                  /* fall back to PPD */
                  if (icc_profile == NULL) {
                    _log("INFO: need to look in PPD for matching qualifer\n");
                    icc_profile = get_icc_profile_for_qualifier(qualifier);
                  }
                }

                /* ICC profile is specified for Ghostscript unless
                   "cm-calibration" option was passed in foomatic-rip */
                if (icc_profile != NULL)
                  snprintf(profile_arg, sizeof(profile_arg),
                           "-sOutputICCProfile='%s'", icc_profile);
                else
                  profile_arg[0] = '\0';

                snprintf(gstoraster, sizeof(gstoraster), "gs -dQUIET -dDEBUG -dPARANOIDSAFER -dNOPAUSE -dBATCH -dNOINTERPOLATE -dNOMEDIAATTRS -sDEVICE=cups -dShowAcroForm %s -sOutputFile=- -", profile_arg);
                free(icc_profile);
            }

            /* build Ghostscript/CUPS driver command line */
            snprintf(cmd, 1024, "%s | %s", gstoraster, cupsfilter);
            _log("INFO: Using command line: %s\n", cmd);

            /* Set environment variables */
            setenv("PPD", job->ppdfile, 1);
        }
    }

    /* Was the RIP command line defined in the PPD file? If not, we assume a PostScript printer
       and do not render/translate the input data */
    if (isempty(cmd)) {
        strcpy(cmd, "cat%A%B%C%D%E%F%G%H%I%J%K%L%M%Z");
        if (dontparse) {
            /* No command line, no options, we have a raw queue, don't check
               whether the input is PostScript, simply pass the input data to
               the backend.*/
            dontparse = 2;
            strcpy(printer_model, "Raw queue");
        }
    }

    /* Summary for debugging */
    _log("\nParameter Summary\n"
         "-----------------\n\n"
         "Spooler: %s\n"
         "Printer: %s\n"
         "Shell: %s\n"
         "PPD file: %s\n"
         "ATTR file: %s\n"
         "Printer model: %s\n",
        spooler_name(spooler), job->printer, get_modern_shell(), job->ppdfile, attrpath, printer_model);
    /* Print the options string only in debug mode, Mac OS X adds very many
       options so that CUPS cannot handle the output of the option string
       in its log files. If CUPS encounters a line with more than 1024 characters
       sent into its log files, it aborts the job with an error.*/
    if (debug || spooler != SPOOLER_CUPS)
        _log("Options: %s\n", job->optstr->data);
    _log("Job title: %s\n", job->title);
    _log("File(s) to be printed:\n");
    _log("%s\n\n", filelist->data);
    if (getenv("GS_LIB"))
        _log("Ghostscript extra search path ('GS_LIB'): %s\n", getenv("GS_LIB"));

    /* Process options from command line */
    optionset_copy_values(optionset("default"), optionset("userval"));
    process_cmdline_options();

    /* no postpipe for CUPS , even if one is defined in the PPD file */
    if (spooler == SPOOLER_CUPS )
        dstrclear(postpipe);

    if (postpipe->len)
        _log("Output will be redirected to:\n%s\n", postpipe);


    filename = strtok_r(filelist->data, " ", &p);
    while (filename) {
        _log("\n================================================\n\n"
             "File: %s\n\n"
             "================================================\n\n", filename);

        /* Do we have a raw queue? */
        if (dontparse == 2) {
            /* Raw queue, simply pass the input into the postpipe (or to STDOUT
               when there is no postpipe) */
            _log("Raw printing, executing \"cat %%s\"\n\n");
            snprintf(tmp, 1024, "cat %s", postpipe->data);
            run_system_process("raw-printer", tmp);
            continue;
        }

        /* First, for arguments with a default, stick the default in as
           the initial value for the "header" option set, this option set
           consists of the PPD defaults, the options specified on the
           command line, and the options set in the header part of the
           PostScript file (all before the first page begins). */
        optionset_copy_values(optionset("userval"), optionset("header"));

        if (!print_file(filename, 1))
	    rip_die(EXIT_PRNERR_NORETRY, "Could not print file %s\n", filename);
        filename = strtok_r(NULL, " ", &p);
    }

    /* Close the last input file */
    fclose(stdin);

    /* TODO dump everything in $dat when debug is turned on (necessary?) */

    _log("\nClosing foomatic-rip.\n");


    /* Cleanup */
    free_job(job);
    free_dstr(filelist);
    options_free();
    close_log();

    argv_free(jclprepend);
    free_dstr(jclappend);

    list_free(arglist);

    return EXIT_PRINTED;
}


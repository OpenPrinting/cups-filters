/* renderer.c
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
#include <config.h>
#include <signal.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#include "foomaticrip.h"
#include "util.h"
#include "process.h"
#include "options.h"

/*
 * Check whether we have a Ghostscript version with redirection of the standard
 * output of the PostScript programs via '-sstdout=%stderr'
 */
int test_gs_output_redirection()
{
    char gstestcommand[CMDLINE_MAX];
    char output[10] = "";
    int bytes;

    snprintf(gstestcommand, CMDLINE_MAX,
	     "%s -dQUIET -dPARANOIDSAFER -dNOPAUSE "
             "-dBATCH -dNOMEDIAATTRS -sDEVICE=ps2write -sstdout=%%stderr "
             "-sOutputFile=/dev/null -c '(hello\n) print flush' 2>&1", gspath);

    FILE *pd = popen(gstestcommand, "r");
    if (!pd) {
        _log("Failed to execute ghostscript!\n");
        return 0;
    }

    bytes = fread_or_die(output, 1, 10, pd);
    pclose(pd);

    if (bytes > 0 && startswith(output, "hello"))
        return 1;

    return 0;
}

/*
 * Massage arguments to make ghostscript execute properly as a filter, with
 * output on stdout and errors on stderr etc.  (This function does what
 * foomatic-gswrapper used to do)
 */
void massage_gs_commandline(dstr_t *cmd)
{
    int gswithoutputredirection = test_gs_output_redirection();
    size_t start, end;
    dstr_t *gscmd, *cmdcopy;

    extract_command(&start, &end, cmd->data, "gs");
    if (start == end) /* cmd doesn't call ghostscript */
        return;

    gscmd = create_dstr();
    dstrncpy(gscmd, &cmd->data[start], end - start);

    /* If Ghostscript does not support redirecting the standard output
       of the PostScript program to standard error with '-sstdout=%stderr', sen
       the job output data to fd 3; errors will be on 2(stderr) and job ps
       program interpreter output on 1(stdout). */
    if (gswithoutputredirection)
        dstrreplace(gscmd, "-sOutputFile=- ", "-sOutputFile=%stdout ", 0);
    else
        dstrreplace(gscmd, "-sOutputFile=- ", "-sOutputFile=/dev/fd/3 ", 0);

    /* Use always buffered input. This works around a Ghostscript
       bug which prevents printing encrypted PDF files with Adobe Reader 8.1.1
       and Ghostscript built as shared library (Ghostscript bug #689577, Ubuntu
       bug #172264) */
    if (dstrendswith(gscmd, " -"))
        dstrcat(gscmd, "_");
    else
        dstrreplace(gscmd, " - ", " -_ ", 0);
    dstrreplace(gscmd, " /dev/fd/0", " -_ ", 0);

    /* Turn *off* -q (quiet!); now that stderr is useful! :) */
    dstrreplace(gscmd, " -q ", " ", 0);

    /* Escape any quotes, and then quote everything just to be sure...
       Escaping a single quote inside single quotes is a bit complex as the
       shell takes everything literal there. So we have to assemble it by
       concatinating different quoted strings.  Finally we get e.g.: 'x'"'"'y'
       or ''"'"'xy' or 'xy'"'"'' or ... */
    /* dstrreplace(cmd, "'", "'\"'\"'"); TODO tbd */

    dstrremove(gscmd, 0, 2);     /* Remove 'gs' */
    if (gswithoutputredirection)
    {
        dstrprepend(gscmd, " -sstdout=%stderr ");
        dstrprepend(gscmd, gspath);
    }
    else
    {
        dstrprepend(gscmd, gspath);
        dstrcat(gscmd, " 3>&1 1>&2");
    }

    /* put gscmd back into cmd, between 'start' and 'end' */
    cmdcopy = create_dstr();
    dstrcpy(cmdcopy, cmd->data);

    dstrncpy(cmd, cmdcopy->data, start);
    dstrcat(cmd, gscmd->data);
    dstrcat(cmd, &cmdcopy->data[end]);

    free_dstr(gscmd);
    free_dstr(cmdcopy);

    /* If the renderer command line contains the "echo" command, replace the
     * "echo" by the user-chosen $myecho (important for non-GNU systems where
     * GNU echo is in a special path */
    dstrreplace(cmd, "echo", echopath, 0); /* TODO search for \wecho\w */
}

char * read_line(FILE *stream, size_t *readbytes)
{
    char *line;
    size_t alloc = 64, len = 0;
    int c;

    line = malloc(alloc);

    while ((c = fgetc(stream)) != EOF) {
        if (len >= alloc -1) {
            alloc *= 2;
            line = realloc(line, alloc);
        }
        line[len] = (char)c;
        len++;
        if (c == '\n')
            break;
    }

    line[len] = '\0';
    *readbytes = len;
    return line;
}

void write_binary_data(FILE *stream, const char *data, size_t bytes)
{
    int i;
    for (i=0; i < bytes; i++)
    {
	fputc(data[i], stream);
    }
}

/*
 * Read all lines containing 'jclstr' from 'stream' (actually, one more) and
 * return them in a zero terminated array.
 */
static char ** read_jcl_lines(FILE *stream, const char *jclstr,
			      size_t *readbinarybytes)
{
    char *line;
    char **result;
    size_t alloc = 8, cnt = 0;

    result = malloc(alloc * sizeof(char *));

    /* read from the renderer output until the first non-JCL line appears */
    while ((line = read_line(stream, readbinarybytes)))
    {
        if (cnt >= alloc -1)
        {
            alloc *= 2;
            result = realloc(result, alloc * sizeof(char *));
        }
        result[cnt] = line;
        if (!strstr(line, jclstr))
            break;
        /* Remove newline from the end of a line containing JCL */
        result[cnt][*readbinarybytes - 1] = '\0';
        cnt++;
    }

    cnt++;
    result[cnt] = NULL;
    return result;
}

static int jcl_keywords_equal(const char *jclline1, const char *jclline2,
                              const char *jclstr)
{
    char *j1, *j2, *p1, *p2;

    j1 = strstr(jclline1, jclstr);
    if (!j1) return 0;
    if (!(p1 = strchr(skip_whitespace(j1), '=')))
        p1 = j1 + strlen(j1);
    p1--;
    while (p1 > j1 && isspace(*p1))
        p1--;

    j2 = strstr(jclline2, jclstr);
    if (!j2) return 0;
    if (!(p2 = strchr(skip_whitespace(j2), '=')))
        p2 = j2 + strlen(j2);
    p2--;
    while (p2 > j2 && isspace(*p2))
        p2--;

    if (p1 - j1 != p2 - j2) return 0;
    return strncmp(j1, j2, p1 - j1 + 1) == 0;
}

/*
 * Finds the keyword of line in opts
 */
static const char * jcl_options_find_keyword(char **opts, const char *line,
                                             const char *jclstr)
{
    if (!opts)
        return NULL;

    while (*opts)
    {
        if (jcl_keywords_equal(*opts, line, jclstr))
            return *opts;
        opts++;
    }
    return NULL;
}

static void argv_write(FILE *stream, char **argv, const char *sep)
{
    if (!argv)
        return;

    while (*argv)
        fprintf(stream, "%s%s", *argv++, sep);
}

/*
 * Merges 'original_opts' and 'pref_opts' and writes them to 'stream'. Header /
 * footer is taken from 'original_opts'. If both have the same options, the one
 * from 'pref_opts' is preferred
 * Returns true, if original_opts was not empty
 */
static int write_merged_jcl_options(FILE *stream,
                                    char **original_opts,
                                    char **opts,
                                    size_t readbinarybytes,
                                    const char *jclstr)
{
    char *p = strstr(original_opts[0], jclstr);
    char header[128];
    char **optsp1 = NULL, **optsp2 = NULL;

    /* No JCL options in original_opts, just prepend opts */
    if (argv_count(original_opts) == 1)
    {
        fprintf(stream, "%s", jclbegin);
        argv_write(stream, opts, "\n");
        write_binary_data(stream, original_opts[0], readbinarybytes);
        return 0;
    }

    if (argv_count(original_opts) == 2)
    {
        /* If we have only one line of JCL it is probably something like the
         * "@PJL ENTER LANGUAGE=..." line which has to be in the end, but it
         * also contains the "<esc>%-12345X" which has to be in the beginning
         * of the job */
        if (p)
            fwrite_or_die(original_opts[0], 1, p - original_opts[0], stream);
        else
            fprintf(stream, "%s\n", original_opts[0]);

        argv_write(stream, opts, "\n");

        if (p)
            fprintf(stream, "%s\n", p);

        write_binary_data(stream, original_opts[1], readbinarybytes);
        return 1;
    }

    /* Write jcl header */
    strncpy(header, original_opts[0], p - original_opts[0]);
    header[p - original_opts[0]] = '\0';
    fprintf(stream, "%s", header);

    /* Insert the JCL commands from the PPD file right before the first
       "@PJL SET ..." line from the, if there are no "@PJL SET ..." lines,
       directly before "@PJL ENTER LANGUAGE ...", otherwise after the JCL
       commands from the driver */
    for (optsp1 = original_opts; *(optsp1 + 1); optsp1++) {
        if (optsp2 == NULL &&
	    ((strstr(*optsp1, "ENTER LANGUAGE") != NULL) ||
	     (strncasecmp(*optsp1, "@PJL SET ", 9) == 0))) {
	    for (optsp2 = opts; *optsp2; optsp2++)
	        if (!jcl_options_find_keyword(original_opts, *optsp2, jclstr))
		    fprintf(stream, "%s\n", *optsp2);
	}
        if (optsp1 != original_opts) p = *optsp1;
        if (!p)
            _log("write_merged_jcl_options() dereferences NULL pointer p\n");
        if (jcl_options_find_keyword(opts, p, jclstr))
	  fprintf(stream, "%s\n", jcl_options_find_keyword(opts, p, jclstr));
	else
            fprintf(stream, "%s\n", p);
    }
    if (optsp2 == NULL)
        for (optsp2 = opts; *optsp2; optsp2++)
            if (!jcl_options_find_keyword(original_opts, *optsp2, jclstr))
	        fprintf(stream, "%s\n", *optsp2);

    write_binary_data(stream, *optsp1, readbinarybytes);

    return 1;
}

void log_jcl()
{
    char **opt;

    _log("JCL: %s", jclbegin);
    if (jclprepend)
        for (opt = jclprepend; *opt; opt++)
            _log("%s\n", *opt);

    _log("<job data> %s\n\n", jclappend->data);
}

int exec_kid4(FILE *in, FILE *out, void *user_arg)
{
    FILE *fileh = open_postpipe();
    int driverjcl = 0;
    size_t readbinarybytes;

    log_jcl();

    /* wrap the JCL around the job data, if there are any options specified...
     * Should the driver already have inserted JCL commands we merge our JCL
     * header with the one from the driver */
    if (argv_count(jclprepend) > 0)
    {
        if (!isspace(jclprepend[0][0]))
        {
            char *jclstr, **jclheader;
            size_t pos;

            pos = strcspn(jclprepend[0], " \t\n\r");
            jclstr = malloc(pos +1);
            strncpy(jclstr, jclprepend[0], pos);
            jclstr[pos] = '\0';

            jclheader = read_jcl_lines(in, jclstr, &readbinarybytes);

            driverjcl = write_merged_jcl_options(fileh,
                                                 jclheader,
                                                 jclprepend,
                                                 readbinarybytes,
                                                 jclstr);

            free(jclstr);
            argv_free(jclheader);
        }
        else
            /* No merging of JCL header possible, simply prepend it */
            argv_write(fileh, jclprepend, "\n");
    }

    /* The job data */
    copy_file(fileh, in, NULL, 0);

    /* A JCL trailer */
    if (argv_count(jclprepend) > 0 && !driverjcl)
        fwrite_or_die(jclappend->data, jclappend->len, 1, fileh);

    fclose(in);
    if (fclose(fileh) != 0)
    {
        _log("error closing postpipe\n");
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }

    return EXIT_PRINTED;
}

int exec_kid3(FILE *in, FILE *out, void *user_arg)
{
    dstr_t *commandline;
    int kid4;
    FILE *kid4in;
    int status;

    commandline = create_dstr();
    dstrcpy(commandline, (const char *)user_arg);

    kid4 = start_process("kid4", exec_kid4, NULL, &kid4in, NULL);
    if (kid4 < 0) {
        free_dstr(commandline);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }

    if (in && dup2(fileno(in), fileno(stdin)) < 0) {
        _log("kid3: Could not dup stdin\n");
        fclose(kid4in);
        free_dstr(commandline);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    if (dup2(fileno(kid4in), fileno(stdout)) < 0) {
        _log("kid3: Could not dup stdout to kid4\n");
        fclose(kid4in);
        free_dstr(commandline);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    if (debug)
    {
        if (!redirect_log_to_stderr()) {
            fclose(kid4in);
            free_dstr(commandline);
            return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
        }

        /* Save the data supposed to be fed into the renderer also into a file*/
        dstrprepend(commandline, "tee $(mktemp " LOG_FILE "-XXXXXX.ps) | ( ");
        dstrcat(commandline, ")");
    }

    /* Actually run the thing */
    status = run_system_process("renderer", commandline->data);

    if (in)
        fclose(in);
    fclose(kid4in);
    fclose(stdin);
    fclose(stdout);
    free_dstr(commandline);

    if (WIFEXITED(status)) {
        switch (WEXITSTATUS(status)) {
            case 0:  /* Success! */
                /* wait for postpipe/output child */
                wait_for_process(kid4);
                _log("kid3 finished\n");
                return EXIT_PRINTED;
            case 1:
                _log("Possible error on renderer command line or PostScript error. Check options.");
                return EXIT_JOBERR;
            case 139:
                _log("The renderer may have dumped core.");
                return EXIT_JOBERR;
            case 141:
                _log("A filter used in addition to the renderer itself may have failed.");
                return EXIT_PRNERR;
            case 243:
            case 255:  /* PostScript error? */
                return EXIT_JOBERR;
        }
    }
    else if (WIFSIGNALED(status)) {
        switch (WTERMSIG(status)) {
            case SIGUSR1:
                return EXIT_PRNERR;
            case SIGUSR2:
                return EXIT_PRNERR_NORETRY;
            case SIGTTIN:
                return EXIT_ENGAGED;
        }
    }
    return EXIT_PRNERR;
}


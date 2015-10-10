/* pdf.c
 *
 * Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
 * Copyright (C) 2008 Lars Uebernickel <larsuebernickel@gmx.de>
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
#include "process.h"
#include "renderer.h"

#include <stdlib.h>
#include <ctype.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))


static int wait_for_renderer();


static int pdf_count_pages(const char *filename)
{
    char gscommand[CMDLINE_MAX];
    char output[31] = "";
    int pagecount;
    size_t bytes;

    snprintf(gscommand, CMDLINE_MAX, "%s -dNODISPLAY -q -c "
	     "'/pdffile (%s) (r) file def pdfdict begin pdffile pdfopen begin "
	     "(PageCount: ) print pdfpagecount == flush currentdict pdfclose "
	     "end end quit'",
	     gspath, filename);

    FILE *pd = popen(gscommand, "r");
    if (!pd)
      rip_die(EXIT_STARVED, "Failed to execute ghostscript to determine number of input pages!\n");

    bytes = fread(output, 1, 31, pd);
    pclose(pd);

    if (bytes <= 0 || sscanf(output, "PageCount: %d", &pagecount) < 1)
      pagecount = -1;

    return pagecount;
}

pid_t kid3 = 0;


static int start_renderer(const char *cmd)
{
    if (kid3 != 0)
        wait_for_renderer();

    _log("Starting renderer with command: %s\n", cmd);
    kid3 = start_process("kid3", exec_kid3, (void *)cmd, NULL, NULL);
    if (kid3 < 0)
        rip_die(EXIT_STARVED, "Could not start renderer\n");

    return 1;
}

static int wait_for_renderer()
{
    int status;

    waitpid(kid3, &status, 0);

    if (!WIFEXITED(status)) {
        _log("Kid3 did not finish normally.\n");
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    _log("Kid3 exit status: %d\n", WEXITSTATUS(status));
    if (WEXITSTATUS(status) != 0)
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);

    kid3 = 0;
    return 1;
}

/*
 * Extract pages 'first' through 'last' from the pdf and write them into a
 * temporary file.
 */
static int pdf_extract_pages(char filename[PATH_MAX],
                             const char *pdffilename,
                             int first,
                             int last)
{
    char gscommand[CMDLINE_MAX];
    char filename_arg[PATH_MAX], first_arg[50], last_arg[50];
    int fd;

    _log("Extracting pages %d through %d\n", first, last);

    snprintf(filename, PATH_MAX, "%s/foomatic-XXXXXX", temp_dir());
    if ((fd = mkstemp(filename)) == -1)
        rip_die(EXIT_STARVED, "Unable to create temporary file!\n");
    close (fd);

    snprintf(filename_arg, PATH_MAX, "-sOutputFile=%s", filename);

    first_arg[0] = '\0';
    last_arg[0] = '\0';
    if (first > 1)
    {
        snprintf(first_arg, 50, "-dFirstPage=%d", first);
        if (last >= first)
            snprintf(last_arg, 50, "-dLastPage=%d", last);
    }

    snprintf(gscommand, CMDLINE_MAX, "%s -q -dNOPAUSE -dBATCH -dPARANOIDSAFER -dNOINTERPOLATE"
	     "-sDEVICE=pdfwrite %s %s %s %s",
	     gspath, filename_arg, first_arg, last_arg, pdffilename);

    FILE *pd = popen(gscommand, "r");
    if (!pd)
        rip_die(EXIT_STARVED, "Could not run ghostscript to extract the pages!\n");
    pclose(pd);

    return 1;
}

static int render_pages_with_generic_command(dstr_t *cmd,
                                             const char *filename,
                                             int firstpage,
                                             int lastpage)
{
    char tmpfile[PATH_MAX];
    int result;

    /* TODO it might be a good idea to give pdf command lines the possibility
     * to get the file on the command line rather than piped through stdin
     * (maybe introduce a &filename; ??) */

    if (lastpage < 0)  /* i.e. print the whole document */
        dstrcatf(cmd, " < %s", filename);
    else
    {
        if (!pdf_extract_pages(tmpfile, filename, firstpage, lastpage))
            rip_die(EXIT_STARVED, "Could not run ghostscript to extract the pages!\n");
        dstrcatf(cmd, " < %s", tmpfile);
    }

    result = start_renderer(cmd->data);

    if (lastpage > 0)
        unlink(tmpfile);

    return result;
}

static int render_pages_with_ghostscript(dstr_t *cmd,
                                         size_t start_gs_cmd,
                                         size_t end_gs_cmd,
                                         const char *filename,
                                         int firstpage,
                                         int lastpage)
{
    char *p;

    /* No need to create a temporary file, just give ghostscript the file and
     * first/last page on the command line */

    /* Some command lines want to read from stdin */
    for (p = &cmd->data[end_gs_cmd -1]; isspace(*p); p--)
        ;
    if (*p == '-')
        *p = ' ';

    dstrinsertf(cmd, end_gs_cmd, " %s ", filename);

    if (firstpage > 1)
    {
        if (lastpage >= firstpage)
            dstrinsertf(cmd, start_gs_cmd +2,
                        " -dFirstPage=%d -dLastPage=%d ",
                        firstpage, lastpage);
        else
            dstrinsertf(cmd, start_gs_cmd +2,
                        " -dFirstPage=%d ", firstpage);
    }

    return start_renderer(cmd->data);
}

static int render_pages(const char *filename, int firstpage, int lastpage)
{
    dstr_t *cmd = create_dstr();
    size_t start, end;
    int result;

    build_commandline(optionset("currentpage"), cmd, 1);

    extract_command(&start, &end, cmd->data, "gs");
    if (start == end)
        /* command is not Ghostscript */
        result = render_pages_with_generic_command(cmd,
                                                   filename,
                                                   firstpage,
                                                   lastpage);
    else
        /* Ghostscript command, tell it which pages we want to render */
        result = render_pages_with_ghostscript(cmd,
                                               start,
                                               end,
                                               filename,
                                               firstpage,
                                               lastpage);

    free_dstr(cmd);
    return result;
}

static int print_pdf_file(const char *filename)
{
    int page_count, i;
    int firstpage;

    page_count = pdf_count_pages(filename);

    if (page_count <= 0)
        rip_die(EXIT_JOBERR, "Unable to determine number of pages, page count: %d\n", page_count);
    _log("File contains %d pages\n", page_count);

    optionset_copy_values(optionset("header"), optionset("currentpage"));
    optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    firstpage = 1;
    for (i = 1; i <= page_count; i++)
    {
        set_options_for_page(optionset("currentpage"), i);
        if (!optionset_equal(optionset("currentpage"), optionset("previouspage"), 1))
        {
            render_pages(filename, firstpage, i);
            firstpage = i;
        }
        optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    }
    if (firstpage == 1)
        render_pages(filename, 1, -1); /* Render the whole document */
    else
        render_pages(filename, firstpage, page_count);

    wait_for_renderer();

    return 1;
}

int print_pdf(FILE *s,
              const char *alreadyread,
              size_t len,
              const char *filename,
              size_t startpos)
{
    char tmpfilename[PATH_MAX] = "";
    int result;

    /* If reading from stdin, write everything into a temporary file */
    /* TODO don't do this if there aren't any pagerange-limited options */
    if (s == stdin)
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
        copy_file(tmpfile, stdin, alreadyread, len);
        fclose(tmpfile);

        filename = tmpfilename;
    }

    result = print_pdf_file(filename);

    if (!isempty(tmpfilename))
        unlink(tmpfilename);

    return result;
}


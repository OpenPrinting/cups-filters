/* postscript.c
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
#include "renderer.h"
#include "process.h"

#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>

void get_renderer_handle(const dstr_t *prepend, FILE **fd, pid_t *pid);
int close_renderer_handle(FILE *rendererhandle, pid_t rendererpid);

#define LT_BEGIN_FEATURE 1
#define LT_FOOMATIC_RIP_OPTION_SETTING 2
int line_type(const char *line)
{
    const char *p;
    if (startswith(line, "%%BeginFeature:"))
        return LT_BEGIN_FEATURE;
    p = line;
    while (*p && isspace(*p)) p++;
    if (!startswith(p, "%%"))
        return 0;
    p += 2;
    while (*p && isspace(*p)) p++;
    if (startswith(p, "FoomaticRIPOptionSetting:"))
        return LT_FOOMATIC_RIP_OPTION_SETTING;
    return 0;
}


/*  Next, examine the PostScript job for traces of command-line and
    JCL options. PPD-aware applications and spoolers stuff option
    settings directly into the file, they do not necessarily send
    PPD options by the command line. Also stuff in PostScript code
    to apply option settings given by the command line and to set
    the defaults given in the PPD file.

    Examination strategy: read lines from STDIN until the first
    %%Page: comment appears and save them as @psheader. This is the
    page-independent header part of the PostScript file. The
    PostScript interpreter (renderer) must execute this part once
    before rendering any assortment of pages. Then pages can be
    printed in any arbitrary selection or order. All option
    settings we find here will be collected in the default option
    set for the RIP command line.

    Now the pages will be read and sent to the renderer, one after
    the other. Every page is read into memory until the
    %%EndPageSetup comment appears (or a certain amount of lines was
    read). So we can get option settings only valid for this
    page. If we have such settings we set them in the modified
    command set for this page.

    If the renderer is not running yet (first page) we start it with
    the command line built from the current modified command set and
    send the first page to it, in the end we leave the renderer
    running and keep input and output pipes open, so that it can
    accept further pages. If the renderer is still running from
    the previous page and the current modified command set is the
    same as the one for the previous page, we send the page. If
    the command set is different, we close the renderer, re-start
    it with the command line built from the new modified command
    set, send the header again, and then the page.

    After the last page the trailer (%%Trailer) is sent.

    The output pipe of this program stays open all the time so that
    the spooler does not assume that the job has finished when the
    renderer is re-started.

    Non DSC-conforming documents will be read until a certain line
    number is reached. Command line or JCL options inserted later
    will be ignored.

    If options are implemented by PostScript code supposed to be
    stuffed into the job's PostScript data we stuff the code for all
    these options into our job data, So all default settings made in
    the PPD file (the user can have edited the PPD file to change
    them) are taken care of and command line options get also
    applied. To give priority to settings made by applications we
    insert the options's code in the beginnings of their respective
    sections, so that sommething, which is already inserted, gets
    executed after our code. Missing sections are automatically
    created. In non-DSC-conforming files we insert the option code
    in the beginning of the file. This is the same policy as used by
    the "pstops" filter of CUPS.

    If CUPS is the spooler, the option settings were already
    inserted by the "pstops" filter, so we don't insert them
    again. The only thing we do is correcting settings of numerical
    options when they were set to a value not available as choice in
    the PPD file, As "pstops" does not support "real" numerical
    options, it sees these settings as an invalid choice and stays
    with the default setting. In this case we correct the setting in
    the first occurence of the option's code, as this one is the one
    added by CUPS, later occurences come from applications and
    should not be touched.

    If the input is not PostScript (if there is no "%!" after
    $maxlinestopsstart lines) we will abort the document with an error.
*/

/* PostScript sections */
#define PS_SECTION_JCLSETUP 1
#define PS_SECTION_PROLOG 2
#define PS_SECTION_SETUP 3
#define PS_SECTION_PAGESETUP 4

#define MAX_NON_DSC_LINES_IN_HEADER 1000
#define MAX_LINES_FOR_PAGE_OPTIONS 200

typedef struct {
    size_t pos;

    FILE *file;
    const char *alreadyread;
    size_t len;
} stream_t;

void _print_ps(stream_t *stream);

int stream_next_line(dstr_t *line, stream_t *s)
{
    int c;
    size_t cnt = 0;

    dstrclear(line);
    while (s->pos < s->len) {
        c = s->alreadyread[s->pos++];
        dstrputc(line, c);
        cnt++;
        if (c == '\n')
            return cnt;
    }

    while ((c = fgetc(s->file)) != EOF) {
        dstrputc(line, c);
        cnt++;
        if (c == '\n')
            return cnt;
    }
    return cnt;
}

int print_ps(FILE *file, const char *alreadyread, size_t len, const char *filename)
{
    stream_t stream;

    if (file != stdin && (dup2(fileno(file), fileno(stdin)) < 0)) {
        _log("Could not dup %s to stdin.\n", filename);
        return 0;
    }

    stream.pos = 0;
    stream.file = stdin;
    stream.alreadyread = alreadyread;
    stream.len = len;
    _print_ps(&stream);
    return 1;
}

void _print_ps(stream_t *stream)
{
    char *p;

    int maxlines = 1000;    /* Maximum number of lines to be read  when the
                               documenent is not  DSC-conforming.
                               "$maxlines = 0"  means that all will be read and
                               examined. If it is  discovered that the input
                               file  is DSC-conforming, this will  be set to 0. */

    int maxlinestopsstart = 200;    /* That many lines are allowed until the
                                      "%!" indicating PS comes. These
                                      additional lines in the
                                      beginning are usually JCL
                                      commands. The lines will be
                                      ignored by our parsing but
                                      passed through. */

    int printprevpage = 0;  /* We set this when encountering "%%Page:" and the
                               previous page is not printed yet. Then it will
                               be printed and the new page will be prepared in
                               the next run of the loop (we don't read a new
                               line and don't increase the $linect then). */

    int linect = 0;         /* how many lines have we examined */
    int nonpslines = 0;     /* lines before "%!" found yet. */
    int more_stuff = 1;     /* there is more stuff in stdin */
    int saved = 0;          /* DSC line not precessed yet */
    int isdscjob = 0;       /* is the job dsc conforming */
    int inheader = 1;       /* Are we still in the header, before first
                               "%%Page:" comment= */

    int optionsalsointoheader = 0; /* 1: We are in a "%%BeginSetup...
                                    %%EndSetup" section after the first
                                    "%%Page:..." line (OpenOffice.org
                                    does this and intends the options here
                                    apply to the whole document and not
                                    only to the current page). We have to
                                    add all lines also to the end of the
                                    @psheader now and we have to set
                                    non-PostScript options also in the
                                    "header" optionset. 0: otherwise. */

    int insertoptions = 1;  /* If we find out that a file with a DSC magic
                               string ("%!PS-Adobe-") is not really DSC-
                               conforming, we insert the options directly
                               after the line with the magic string. We use
                               this variable to store the number of the line
                               with the magic string */

    int prologfound = 0;    /* Did we find the
                               "%%BeginProlog...%%EndProlog" section? */
    int setupfound = 0;     /* Did we find the
                               %%BeginSetup...%%EndSetup" section? */
    int pagesetupfound = 0; /* special page setup handling needed */

    int inprolog = 0;       /* We are between "%%BeginProlog" and "%%EndProlog" */
    int insetup = 0;        /* We are between "%%BeginSetup" and "%%EndSetup" */
    int infeature = 0;      /* We are between "%%BeginFeature" and "%%EndFeature" */

    int optionreplaced = 0; /* Will be set to 1 when we are in an
                               option ("%%BeginFeature...
                               %%EndFeature") which we have replaced. */

    int postscriptsection = PS_SECTION_JCLSETUP; /* In which section of the PostScript file
                                                   are we currently ? */

    int nondsclines = 0;    /* Number of subsequent lines found which are at a
                               non-DSC-conforming place, between the sections
                               of the header.*/

    int nestinglevel = 0;   /* Are we in the main document (0) or in an
                               embedded document bracketed by "%%BeginDocument"
                               and "%%EndDocument" (>0) We do not parse the
                               PostScript in an embedded document. */

    int inpageheader = 0;   /* Are we in the header of a page,
                               between "%%BeginPageSetup" and
                               "%%EndPageSetup" (1) or not (0). */

    int passthru = 0;       /* 0: write data into psfifo,
                               1: pass data directly to the renderer */

    int lastpassthru = 0;   /* State of 'passthru' in previous line
                               (to allow debug output when $passthru
                               switches. */

    int ignorepageheader = 0; /* Will be set to 1 as soon as active
                                 code (not between "%%BeginPageSetup"
                                 and "%%EndPageSetup") appears after a
                                 "%%Page:" comment. In this case
                                 "%%BeginPageSetup" and
                                 "%%EndPageSetup" is not allowed any
                                 more on this page and will be ignored.
                                 Will be set to 0 when a new "%%Page:"
                                 comment appears. */

    int optset = optionset("header"); /* Where do the option settings which
                                         we have found go? */

    /* current line */
    dstr_t *line = create_dstr();

    dstr_t *onelinebefore = create_dstr();
    dstr_t *twolinesbefore = create_dstr();

    /* The header of the PostScript file, to be send after each start of the renderer */
    dstr_t *psheader = create_dstr();

    /* The input FIFO, data which we have pulled from stdin for examination,
       but not send to the renderer yet */
    dstr_t *psfifo = create_dstr();

    int ignoreline;

    int ooo110 = 0;         /* Flag to work around an application bug */

    int currentpage = 0;   /* The page which we are currently printing */

    option_t *o;
    const char *val;

    int linetype;

    dstr_t *linesafterlastbeginfeature = create_dstr(); /* All codelines after the last "%%BeginFeature" */

    char optionname [128];
    char value [128];
    int fromcomposite = 0;

    dstr_t *pdest;

    double width, height;

    pid_t rendererpid = 0;
    FILE *rendererhandle = NULL;

    int retval;

    dstr_t *tmp = create_dstr();
    jobhasjcl = 0;

    /* We do not parse the PostScript to find Foomatic options, we check
        only whether we have PostScript. */
    if (dontparse)
        maxlines = 1;

    _log("Reading PostScript input ...\n");

    do {
        ignoreline = 0;

        if (printprevpage || saved || stream_next_line(line, stream)) {
            saved = 0;
            if (linect == nonpslines) {
                /* In the beginning should be the postscript leader,
                   sometimes after some JCL commands */
                if ( !(line->data[0] == '%' && line->data[1] == '!') &&
                     !(line->data[1] == '%' && line->data[2] == '!')) /* There can be a Windows control character before "%!" */
                {
                    nonpslines++;
                    if (maxlines == nonpslines)
                        maxlines ++;
                    jobhasjcl = 1;

                    if (nonpslines > maxlinestopsstart) {
                        /* This is not a PostScript job, abort it */
                        _log("Job does not start with \"%%!\", is it Postscript?\n");
			rip_die(EXIT_JOBERR, "Unknown data format.\n");
                    }
                }
                else {
                    /* Do we have a DSC-conforming document? */
                    if ((line->data[0] == '%' && startswith(line->data, "%!PS-Adobe-")) ||
                        (line->data[1] == '%' && startswith(line->data, "%!PS-Adobe-")))
                    {
                        /* Do not stop parsing the document */
                        if (!dontparse) {
                            maxlines = 0;
                            isdscjob = 1;
                            insertoptions = linect + 1;
                            /* We have written into psfifo before, now we continue in
                               psheader and move over the data which is already in psfifo */
                            dstrcat(psheader, psfifo->data);
                            dstrclear(psfifo);
                        }
                        _log("--> This document is DSC-conforming!\n");
                    }
                    else {
                        /* Job is not DSC-conforming, stick in all PostScript
                           option settings in the beginning */
                        append_prolog_section(line, optset, 1);
                        append_setup_section(line, optset, 1);
                        append_page_setup_section(line, optset, 1);
                        prologfound = 1;
                        setupfound = 1;
                        pagesetupfound = 1;
                    }
                }
            }
            else {
                if (startswith(line->data, "%")) {
                    if (startswith(line->data, "%%BeginDocument")) {
                        /* Beginning of an embedded document
                        Note that Adobe Acrobat has a bug and so uses
                        "%%BeginDocument " instead of "%%BeginDocument:" */
                        nestinglevel++;
                        _log("Embedded document, nesting level now: %d\n", nestinglevel);
                    }
                    else if (nestinglevel > 0 && startswith(line->data, "%%EndDocument")) {
                        /* End of an embedded document */
                        nestinglevel--;
                        _log("End of embedded document, nesting level now: %d\n", nestinglevel);
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%Creator")) {
                        /* Here we set flags to treat particular bugs of the
                        PostScript produced by certain applications */
                        p = strstr(line->data, "%%Creator") + 9;
                        while (*p && (isspace(*p) || *p == ':')) p++;
                        if (!strcmp(p, "OpenOffice.org")) {
                            p += 14;
                            while (*p && isspace(*p)) p++;
                            if (sscanf(p, "1.1.%d", &ooo110) == 1) {
                                _log("Document created with OpenOffice.org 1.1.x\n");
                                ooo110 = 1;
                            }
                        } else if (!strcmp(p, "StarOffice 8")) {
                            p += 12;
			    _log("Document created with StarOffice 8\n");
			    ooo110 = 1;
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%BeginProlog")) {
                        /* Note: Below is another place where a "Prolog" section
                        start will be considered. There we assume start of the
                        "Prolog" if the job is DSC-Conformimg, but an arbitrary
                        comment starting with "%%Begin", but not a comment
                        explicitly treated here, is found. This is done because
                        many "dvips" (TeX/LaTeX) files miss the "%%BeginProlog"
                        comment.
                        Beginning of Prolog */
                        _log("\n-----------\nFound: %%%%BeginProlog\n");
                        inprolog = 1;
                        if (inheader)
                            postscriptsection = PS_SECTION_PROLOG;
                        nondsclines = 0;
                        /* Insert options for "Prolog" */
                        if (!prologfound) {
                            append_prolog_section(line, optset, 0);
                            prologfound = 1;
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%EndProlog")) {
                        /* End of Prolog */
                        _log("Found: %%%%EndProlog\n");
                        inprolog = 0;
                        insertoptions = linect +1;
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%BeginSetup")) {
                        /* Beginning of Setup */
                        _log("\n-----------\nFound: %%%%BeginSetup\n");
                        insetup = 1;
                        nondsclines = 0;
                        /* We need to distinguish with the $inheader variable
                        here whether we are in the header or on a page, as
                        OpenOffice.org inserts a "%%BeginSetup...%%EndSetup"
                        section after the first "%%Page:..." line and assumes
                        this section to be valid for all pages. */
                        if (inheader) {
                            postscriptsection = PS_SECTION_SETUP;
                            /* If there was no "Prolog" but there are
                            options for the "Prolog", push a "Prolog"
                            with these options onto the psfifo here */
                            if (!prologfound) {
                                dstrclear(tmp);
                                append_prolog_section(tmp, optset, 1);
                                dstrprepend(line, tmp->data);
                                prologfound = 1;
                            }
                            /* Insert options for "DocumentSetup" or "AnySetup" */
                            if (spooler != SPOOLER_CUPS && !setupfound) {
                                /* For non-CUPS spoolers or no spooler at all,
                                we leave everythnig as it is */
                                append_setup_section(line, optset, 0);
                                setupfound = 1;
                            }
                        }
                        else {
                            /* Found option settings must be stuffed into both
                            the header and the currrent page now. They will
                            be written into both the "header" and the
                            "currentpage" optionsets and the PostScript code
                            lines of this section will not only go into the
                            output stream, but also added to the end of the
                            @psheader, so that they get repeated (to preserve
                            the embedded PostScript option settings) on a
                            restart of the renderer due to command line
                            option changes */
                            optionsalsointoheader = 1;
                            _log("\"%%%%BeginSetup\" in page header\n");
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%EndSetup")) {
                        /* End of Setup */
                        _log("Found: %%%%EndSetup\n");
                        insetup = 0;
                        if (inheader)
                            insertoptions = linect +1;
                        else {
                            /* The "%%BeginSetup...%%EndSetup" which
                            OpenOffice.org has inserted after the first
                            "%%Page:..." line ends here, so the following
                            options go only onto the current page again */
                            optionsalsointoheader = 0;
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%Page:")) {
                        if (!lastpassthru && !inheader) {
                            /* In the last line we were not in passthru mode,
                            so the last page is not printed. Prepare to do
                            it now. */
                            printprevpage = 1;
                            passthru = 1;
                            _log("New page found but previous not printed, print it now.\n");
                        }
                        else {
                            /* the previous page is printed, so we can prepare
                            the current one */
                            _log("\n-----------\nNew page: %s", line->data);
                            printprevpage = 0;
                            currentpage++;
                            /* We consider the beginning of the page already as
                            page setup section, as some apps do not use
                            "%%PageSetup" tags. */
                            postscriptsection = PS_SECTION_PAGESETUP;

                            /* TODO can this be removed?
                            Save PostScript state before beginning the page
                            $line .= "/foomatic-saved-state save def\n"; */

                            /* Here begins a new page */
                            if (inheader) {
                                build_commandline(optset, NULL, 0);
                                /* Here we add some stuff which still
                                belongs into the header */
                                dstrclear(tmp);

                                /* If there was no "Setup" but there are
                                options for the "Setup", push a "Setup"
                                with these options onto the @psfifo here */
                                if (!setupfound) {
                                    append_setup_section(tmp, optset, 1);
                                    setupfound = 1;
                                }
                                /* If there was no "Prolog" but there are
                                options for the "Prolog", push a "Prolog"
                                with these options onto the @psfifo here */
                                if (!prologfound) {
                                    append_prolog_section(tmp, optset, 1);
                                    prologfound = 1;
                                }
                                /* Now we push this into the header */
                                dstrcat(psheader, tmp->data);

                                /* The first page starts, so header ends */
                                inheader = 0;
                                nondsclines = 0;
                                /* Option setting should go into the page
                                specific option set now */
                                optset = optionset("currentpage");
                            }
                            else {
                                /*  Restore PostScript state after completing the
                                    previous page:

                                        foomatic-saved-state restore
                                        %%Page: ...
                                        /foomatic-saved-state save def

                                    Print this directly, so that if we need to
                                    restart the renderer for this page due to
                                    a command line change this is done under the
                                    old instance of the renderer
                                    rint $rendererhandle
                                    "foomatic-saved-state restore\n"; */

                                /* Save the option settings of the previous page */
                                optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
                                optionset_delete_values(optionset("currentpage"));
                            }
                            /* Initialize the option set */
                            optionset_copy_values(optionset("header"), optionset("currentpage"));

                            /* Set the command line options which apply only
                                to given pages */
                            set_options_for_page(optionset("currentpage"), currentpage);
                            pagesetupfound = 0;
                            if (spooler == SPOOLER_CUPS) {
                                /* Remove the "notfirst" flag from all options
                                    forseen for the "PageSetup" section, because
                                    when these are numerical options for CUPS.
                                    they have to be set to the correct value
                                    for every page */
                                for (o = optionlist; o; o = o->next) {
                                    if (option_get_section(o ) == SECTION_PAGESETUP)
                                        o->notfirst = 0;
                                }
                            }
                            /* Now the page header comes, so buffer the data,
                                because we must perhaps shut down and restart
                                the renderer */
                            passthru = 0;
                            ignorepageheader = 0;
                            optionsalsointoheader = 0;
                        }
                    }
                    else if (nestinglevel == 0 && !ignorepageheader &&
                            startswith(line->data, "%%BeginPageSetup")) {
                        /* Start of the page header, up to %%EndPageSetup
                        nothing of the page will be drawn, page-specific
                        option settngs (as letter-head paper for page 1)
                        go here*/
                        _log("\nFound: %%%%BeginPageSetup\n");
                        passthru = 0;
                        inpageheader = 1;
                        postscriptsection = PS_SECTION_PAGESETUP;
                        optionsalsointoheader = (ooo110 && currentpage == 1) ? 1 : 0;
                        /* Insert PostScript option settings
                           (options for section "PageSetup") */
                        if (isdscjob) {
                            append_page_setup_section(line, optset, 0);
                            pagesetupfound = 1;
                        }
                    }
                    else if (nestinglevel == 0 && !ignorepageheader &&
                            startswith(line->data, "%%BeginPageSetup")) {
                        /* End of the page header, the page is ready to be printed */
                        _log("Found: %%%%EndPageSetup\n");
                        _log("End of page header\n");
                        /* We cannot for sure say that the page header ends here
                        OpenOffice.org puts (due to a bug) a "%%BeginSetup...
                        %%EndSetup" section after the first "%%Page:...". It
                        is possible that CUPS inserts a "%%BeginPageSetup...
                        %%EndPageSetup" before this section, which means that
                        the options in the "%%BeginSetup...%%EndSetup"
                        section are after the "%%EndPageSetup", so we
                        continue for searching options up to the buffer size
                        limit $maxlinesforpageoptions. */
                        passthru = 0;
                        inpageheader = 0;
                        optionsalsointoheader = 0;
                    }
                    else if (nestinglevel == 0 && !optionreplaced && (!passthru || !isdscjob) &&
                            ((linetype = line_type(line->data)) &&
                            (linetype == LT_BEGIN_FEATURE || linetype == LT_FOOMATIC_RIP_OPTION_SETTING))) {

                        /* parse */
                        if (linetype == LT_BEGIN_FEATURE) {
                            dstrcpy(tmp, line->data);
                            p = strtok(tmp->data, " \t"); /* %%BeginFeature: */
                            p = strtok(NULL, " \t="); /* Option */
                            if (*p == '*') p++;
                            strlcpy(optionname, p, 128);
                            p = strtok(NULL, " \t\r\n"); /* value */
                            fromcomposite = 0;
                            strlcpy(value, p, 128);
                        }
                        else { /* LT_FOOMATIC_RIP_OPTION_SETTING */
                            dstrcpy(tmp, line->data);
                            p = strstr(tmp->data, "FoomaticRIPOptionSetting:");
                            p = strtok(p, " \t");  /* FoomaticRIPOptionSetting */
                            p = strtok(NULL, " \t="); /* Option */
                            strlcpy(optionname, p, 128);
                            p = strtok(NULL, " \t\r\n"); /* value */
                            if (*p == '@') { /* fromcomposite */
                                p++;
                                fromcomposite = 1;
                            }
                            else
                                fromcomposite = 0;
                            strlcpy(value, p, 128);
                        }

                        /* Mark that we are in a "Feature" section */
                        if (linetype == LT_BEGIN_FEATURE) {
                            infeature = 1;
                            dstrclear(linesafterlastbeginfeature);
                        }

                        /* OK, we have an option.  If it's not a
                        Postscript-style option (ie, it's command-line or
                        JCL) then we should note that fact, since the
                        attribute-to-filter option passing in CUPS is kind of
                        funky, especially wrt boolean options. */
                        _log("Found: %s", line->data);
                        if ((o = find_option(optionname)) &&
			    (o->type != TYPE_NONE)) {
                            _log("   Option: %s=%s%s\n", optionname, fromcomposite ? "From" : "", value);
                            if (spooler == SPOOLER_CUPS &&
                                linetype == LT_BEGIN_FEATURE &&
                                !option_get_value(o, optionset("notfirst")) &&
				strcmp((val = option_get_value(o, optset)) ? val : "", value) != 0 &&
                                (inheader || option_get_section(o) == SECTION_PAGESETUP)) {

                                /* We have the first occurence of an option
                                setting and the spooler is CUPS, so this
                                setting is inserted by "pstops" or
                                "imagetops". The value from the command
                                line was not inserted by "pstops" or
                                "imagetops" so it seems to be not under
                                the choices in the PPD. Possible
                                reasons:

                                - "pstops" and "imagetops" ignore settings
                                of numerical or string options which are
                                not one of the choices in the PPD file,
                                and inserts the default value instead.

                                - On the command line an option was applied
                                only to selected pages:
                                "-o <page ranges>:<option>=<values>
                                This is not supported by CUPS, so not
                                taken care of by "pstops".

                                We must fix this here by replacing the
                                setting inserted by "pstops" or "imagetops"
                                with the exact setting given on the command
                                line. */

                                /* $arg->{$optionset} is already
                                range-checked, so do not check again here
                                Insert DSC comment */
                                pdest = (inheader && isdscjob) ? psheader : psfifo;
                                if (option_is_ps_command(o)) {
                                    /* PostScript option, insert the code */

                                    option_get_command(tmp, o, optset, -1);
                                    if (!(val = option_get_value(o, optset)))
                                        val = "";

                                    /* Boolean and enumerated choice options can only be set in the
                                     * PageSetup section */
                                    if ((inheader && option_is_custom_value(o, val)) || !inheader)
                                    {
                                        if (o->type == TYPE_BOOL)
                                            dstrcatf(pdest, "%%%%BeginFeature: *%s %s\n", o->name,
                                                     val && !strcmp(val, "1") ? "True" : "False");
                                        else
                                            dstrcatf(pdest, "%%%%BeginFeature: *%s %s\n", o->name, val);

                                        dstrcatf(pdest, "%s\n", tmp->data);

                                        /* We have replaced this option on the FIFO */
                                        optionreplaced = 1;
                                    }
                                }
                                else { /* Command line or JCL option */
                                    val = option_get_value(o, optset);

                                    if (!inheader || option_is_custom_value(o, val)) {
                                        dstrcatf(pdest, "%%%% FoomaticRIPOptionSetting: %s=%s\n",
                                                 o->name, val ? val : "");
                                        optionreplaced = 1;
                                    }
                                }

                                if (optionreplaced) {
                                    val = option_get_value(o, optset);
                                    _log(" --> Correcting numerical/string option to %s=%s (Command line argument)\n",
                                            o->name, val ? val : "");
                                }
                            }

                            /* Mark that we have already found this option */
                            o->notfirst = 1;
                            if (!optionreplaced) {
                                if (o->style != 'G') {
                                    /* Controlled by '<Composite>' setting of
                                    a member option of a composite option */
                                    if (fromcomposite) {
                                        dstrcpyf(tmp, "From%s", value);
                                        strlcpy(value, tmp->data, 128);
                                    }

                                    /* Non PostScript option
                                    Check whether it is valid */
                                    if (option_set_value(o, optset, value)) {
                                        _log("Setting option\n");
                                        strlcpy(value, option_get_value(o, optset), 128);
                                        if (optionsalsointoheader)
                                            option_set_value(o, optionset("header"), value);
                                        if (o->type == TYPE_ENUM &&
                                                (!strcmp(o->name, "PageSize") || !strcmp(o->name, "PageRegion")) &&
                                                startswith(value, "Custom") &&
                                                linetype == LT_FOOMATIC_RIP_OPTION_SETTING) {
                                            /* Custom Page size */
                                            width = height = 0.0;
                                            p = linesafterlastbeginfeature->data;
                                            while (*p && isspace(*p)) p++;
                                            width = strtod(p, &p);
                                            while (*p && isspace(*p)) p++;
                                            height = strtod(p, &p);
                                            if (width && height) {
                                                dstrcpyf(tmp, "%s.%fx%f", value, width, height);
                                                strlcpy(value, tmp->data, 128);
                                                option_set_value(o, optset, value);
                                                if (optionsalsointoheader)
                                                    option_set_value(o, optionset("header"), value);
                                            }
                                        }
                                        /* For a composite option insert the
                                        code from the member options with
                                        current setting "From<composite>"
                                        The code from the member options
                                        is chosen according to the setting
                                        of the composite option. */
                                        if (option_is_composite(o) && linetype == LT_FOOMATIC_RIP_OPTION_SETTING) {
                                            build_commandline(optset, NULL, 0); /* TODO can this be removed? */

                                            /* TODO merge section and ps_section */
                                            if (postscriptsection == PS_SECTION_JCLSETUP)
                                                option_get_command(tmp, o, optset, SECTION_JCLSETUP);
                                            else if (postscriptsection == PS_SECTION_PROLOG)
                                                option_get_command(tmp, o, optset, SECTION_PROLOG);
                                            else if (postscriptsection == PS_SECTION_SETUP)
                                                option_get_command(tmp, o, optset, SECTION_DOCUMENTSETUP);
                                            else if (postscriptsection == PS_SECTION_PAGESETUP)
                                                option_get_command(tmp, o, optset, SECTION_PAGESETUP);
                                            dstrcat(line, tmp->data);
                                        }
                                    }
                                    else
                                        _log(" --> Invalid option setting found in job\n");
                                }
                                else if (fromcomposite) {
                                    /* PostScript option, but we have to look up
                                    the PostScript code to be inserted from
                                    the setting of a composite option, as
                                    this option is set to "Controlled by
                                    '<Composite>'". */
                                    /* Set the option */
                                    dstrcpyf(tmp, "From%s", value);
                                    strlcpy(value, tmp->data, 128);
                                    if (option_set_value(o, optset, value)) {
                                        _log(" --> Looking up setting in composite option %s\n", value);
                                        if (optionsalsointoheader)
                                            option_set_value(o, optionset("header"), value);
                                        /* update composite options */
                                        build_commandline(optset, NULL, 0);
                                        /* Substitute PostScript comment by the real code */
                                        /* TODO what exactly is the next line doing? */
                                        /* dstrcpy(line, o->compositesubst->data); */
                                    }
                                    else
                                        _log(" --> Invalid option setting found in job\n");
                                }
                                else
                                    /* it is a PostScript style option with
                                    the code readily inserted, no option
                                    for the renderer command line/JCL to set,
                                    no lookup of a composite option needed,
                                    so nothing to do here... */
                                    _log(" --> Option will be set by PostScript interpreter\n");
                            }
                        }
                        else
                            /* This option is unknown to us, WTF? */
                            _log("Unknown option %s=%s found in the job\n", optionname, value);
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%EndFeature")) {
                        /* End of feature */
                        infeature = 0;
                        /* If the option setting was replaced, it ends here,
                        too, and the next option is not necessarily also replaced */
                        optionreplaced = 0;
                        dstrclear(linesafterlastbeginfeature);
                    }
                    else if (nestinglevel == 0 && isdscjob && !prologfound &&
                                startswith(line->data, "%%Begin")) {
                        /* In some PostScript files (especially when generated
                        by "dvips" of TeX/LaTeX) the "%%BeginProlog" is
                        missing, so assume that it was before the current
                        line (the first line starting with "%%Begin". */
                        _log("Job claims to be DSC-conforming, but \"%%%%BeginProlog\" "
                            "was missing before first line with another"
                            "\"%%%%BeginProlog\" comment (is this a TeX/LaTeX/dvips-generated"
                            " PostScript file?). Assuming start of \"Prolog\" here.\n");
                        /* Beginning of Prolog */
                        inprolog = 1;
                        nondsclines = 0;
                        /* Insert options for "Prolog" before the current line */
                        dstrcpyf(tmp, "%%%%BeginProlog\n");
                        append_prolog_section(tmp, optset, 0);
                        dstrprepend(line, tmp->data);
                        prologfound = 1;
                    }
                    else if (nestinglevel == 0 && (
                                startswith(line->data, "%RBINumCopies:") ||
                                startswith(line->data, "%%RBINumCopies:"))) {
                        p = strchr(line->data, ':') +1;
                        get_current_job()->rbinumcopies = atoi(p);
                        _log("Found %RBINumCopies: %d\n", get_current_job()->rbinumcopies);
                    }
                    else if (startswith(skip_whitespace(line->data), "%") ||
                            startswith(skip_whitespace(line->data), "$"))
                        /* This is an unknown PostScript comment or a blank
                        line, no active code */
                        ignoreline = 1;
                }
                else {
                    /* This line is active PostScript code */
                    if (infeature)
                        /* Collect coe in a "%%BeginFeature: ... %%EndFeature"
                        section, to get the values for a custom option
                        setting */
                        dstrcat(linesafterlastbeginfeature, line->data);

                    if (inheader) {
                        if (!inprolog && !insetup) {
                            /* Outside the "Prolog" and "Setup" section
                            a correct DSC-conforming document has no
                            active PostScript code, so consider the
                            file as non-DSC-conforming when there are
                            too many of such lines. */
                            nondsclines++;
                            if (nondsclines > MAX_NON_DSC_LINES_IN_HEADER) {
                                /* Consider document as not DSC-conforming */
                                _log("This job seems not to be DSC-conforming, "
                                    "DSC-comment for next section not found, "
                                    "stopping to parse the rest, passing it "
                                    "directly to the renderer.\n");
                                /* Stop scanning for further option settings */
                                maxlines = 1;
                                isdscjob = 0;
                                /* Insert defaults and command line settings in
                                the beginning of the job or after the last valid
                                section */
                                dstrclear(tmp);
                                if (prologfound)
                                    append_prolog_section(tmp, optset, 1);
                                if (setupfound)
                                    append_setup_section(tmp, optset, 1);
                                if (pagesetupfound)
                                    append_page_setup_section(tmp, optset, 1);
                                dstrinsert(psheader, line_start(psheader->data, insertoptions), tmp->data);

                                prologfound = 1;
                                setupfound = 1;
                                pagesetupfound = 1;
                            }
                        }
                    }
                    else if (!inpageheader) {
                        /* PostScript code inside a page, but not between
                        "%%BeginPageSetup" and "%%EndPageSetup", so
                        we are perhaps already drawing onto a page now */
                        if (startswith(onelinebefore->data, "%%Page"))
                            _log("No page header or page header not DSC-conforming\n");
                        /* Stop buffering lines to search for options
                        placed not DSC-conforming */
                        if (line_count(psfifo->data) >= MAX_LINES_FOR_PAGE_OPTIONS) {
                            _log("Stopping search for page header options\n");
                            passthru = 1;
                            /* If there comes a page header now, ignore it */
                            ignorepageheader = 1;
                            optionsalsointoheader = 0;
                        }
                        /* Insert PostScript option settings (options for the
                         * section "PageSetup" */
                        if (isdscjob && !pagesetupfound) {
                            append_page_setup_section(psfifo, optset, 1);
                            pagesetupfound = 1;
                        }
                    }
                }
            }

            /* Debug Info */
            if (lastpassthru != passthru) {
                if (passthru)
                    _log("Found: %s --> Output goes directly to the renderer now.\n\n", line->data);
                else
                    _log("Found: %s --> Output goes to the FIFO buffer now.\n\n", line->data);
            }

            /* We are in an option which was replaced, do not output the current line */
            if (optionreplaced)
                dstrclear(line);

            /* If we are in a "%%BeginSetup...%%EndSetup" section after
            the first "%%Page:..." and the current line belongs to
            an option setting, we have to copy the line also to the
            @psheader. */
            if (optionsalsointoheader && (infeature || startswith(line->data, "%%EndFeature")))
                dstrcat(psheader, line->data);

            /* Store or send the current line */
            if (inheader && isdscjob) {
                /* We are still in the PostScript header, collect all lines
                in @psheader */
                dstrcat(psheader, line->data);
            }
            else {
                if (passthru && isdscjob) {
                    if (!lastpassthru) {
                        /*
                         * We enter passthru mode with this line, so the
                         * command line can have changed, check it and close
                         * the renderer if needed
                         */
                        if (rendererpid && !optionset_equal(optionset("currentpage"), optionset("previouspage"), 0)) {
                            _log("Command line/JCL options changed, restarting renderer\n");
                            retval = close_renderer_handle(rendererhandle, rendererpid);
                            if (retval != EXIT_PRINTED)
                                rip_die(retval, "Error closing renderer\n");
                            rendererpid = 0;
                        }
                    }

                    /* Flush psfifo and send line directly to the renderer */
                    if (!rendererpid) {
                        /* No renderer running, start it */
                        dstrcpy(tmp, psheader->data);
                        dstrcat(tmp, psfifo->data);
                        get_renderer_handle(tmp, &rendererhandle, &rendererpid);
                        /* psfifo is sent out, flush it */
                        dstrclear(psfifo);
                    }

                    if (!isempty(psfifo->data)) {
                        /* Send psfifo to renderer */
                        fwrite(psfifo->data, psfifo->len, 1, rendererhandle);
                        /* flush psfifo */
                        dstrclear(psfifo);
                    }

                    /* Send line to renderer */
                    if (!printprevpage) {
                        fwrite(line->data, line->len, 1, rendererhandle);

                        while (stream_next_line(line, stream) > 0) {
                            if (startswith(line->data, "%%")) {
                                _log("Found: %s", line->data);
                                _log(" --> Continue DSC parsing now.\n\n");
                                saved = 1;
                                break;
                            }
                            else {
                                fwrite(line->data, line->len, 1, rendererhandle);
                                linect++;
                            }
                        }
                    }
                }
                else {
                    /* Push the line onto the stack to split up later */
                    dstrcat(psfifo, line->data);
                }
            }

            if (!printprevpage)
                linect++;
        }
        else {
            /* EOF! */
            more_stuff = 0;

            /* No PostScript header in the whole file? Then it's not
            PostScript, convert it.
            We open the file converter here when the file has less
            lines than the amount which we search for the PostScript
            header ($maxlinestopsstart). */
            if (linect <= nonpslines) {
	        /* This is not a PostScript job, abort it */
	        _log("Job does not start with \"%%!\", is it Postscript?\n");
	        rip_die(EXIT_JOBERR, "Unknown data format.\n");
            }
        }

        lastpassthru = passthru;

        if (!ignoreline && !printprevpage) {
            dstrcpy(twolinesbefore, onelinebefore->data);
            dstrcpy(onelinebefore, line->data);
        }

    } while ((maxlines == 0 || linect < maxlines) && more_stuff != 0);

    /* Some buffer still containing data? Send it out to the renderer */
    if (more_stuff || inheader || !isempty(psfifo->data)) {
        /* Flush psfifo and send the remaining data to the renderer, this
        only happens with non-DSC-conforming jobs or non-Foomatic PPDs */
        if (more_stuff)
            _log("Stopped parsing the PostScript data, "
                 "sending rest directly to the renderer.\n");
        else
            _log("Flushing FIFO.\n");

        if (inheader) {
            build_commandline(optset, NULL, 0);
            /* No page initialized yet? Copy the "header" option set into the
            "currentpage" option set, so that the renderer will find the
            options settings. */
            optionset_copy_values(optionset("header"), optionset("currentpage"));
            optset = optionset("currentpage");

            /* If not done yet, insert defaults and command line settings
            in the beginning of the job or after the last valid section */
            dstrclear(tmp);
            if (prologfound)
                append_prolog_section(tmp, optset, 1);
            if (setupfound)
                append_setup_section(tmp, optset, 1);
            if (pagesetupfound)
                append_page_setup_section(tmp, optset, 1);
            dstrinsert(psheader, line_start(psheader->data, insertoptions), tmp->data);

            prologfound = 1;
            setupfound = 1;
            pagesetupfound = 1;
        }

        if (rendererpid > 0 && !optionset_equal(optionset("currentpage"), optionset("previouspage"), 0)) {
            _log("Command line/JCL options changed, restarting renderer\n");
            retval = close_renderer_handle(rendererhandle, rendererpid);
            if (retval != EXIT_PRINTED)
                rip_die(retval, "Error closing renderer\n");
            rendererpid = 0;
        }

        if (!rendererpid) {
            dstrcpy(tmp, psheader->data);
            dstrcat(tmp, psfifo->data);
            get_renderer_handle(tmp, &rendererhandle, &rendererpid);
            /* We have sent psfifo now */
            dstrclear(psfifo);
        }

        if (psfifo->len) {
            /* Send psfifo to the renderer */
            fwrite(psfifo->data, psfifo->len, 1, rendererhandle);
            dstrclear(psfifo);
        }

        /* Print the rest of the input data */
        if (more_stuff) {
            while (stream_next_line(tmp, stream))
                fwrite(tmp->data, tmp->len, 1, rendererhandle);
        }
    }

    /*  At every "%%Page:..." comment we have saved the PostScript state
    and we have increased the page number. So if the page number is
    non-zero we had at least one "%%Page:..." comment and so we have
    to give a restore the PostScript state.
    if ($currentpage > 0) {
        print $rendererhandle "foomatic-saved-state restore\n";
    } */

    /* Close the renderer */
    if (rendererpid) {
        retval = close_renderer_handle(rendererhandle, rendererpid);
        if (retval != EXIT_PRINTED)
            rip_die(retval, "Error closing renderer\n");
        rendererpid = 0;
    }

    free_dstr(line);
    free_dstr(onelinebefore);
    free_dstr(twolinesbefore);
    free_dstr(psheader);
    free_dstr(psfifo);
    free_dstr(tmp);
}

/*
 * Run the renderer command line (and if defined also the postpipe) and returns
 * a file handle for stuffing in the PostScript data.
 */
void get_renderer_handle(const dstr_t *prepend, FILE **fd, pid_t *pid)
{
    pid_t kid3;
    FILE *kid3in;
    dstr_t *cmdline = create_dstr();

    /* Build the command line and get the JCL commands */
    build_commandline(optionset("currentpage"), cmdline, 0);
    massage_gs_commandline(cmdline);

    _log("\nStarting renderer with command: \"%s\"\n", cmdline->data);
    kid3 = start_process("kid3", exec_kid3, (void *)cmdline->data, &kid3in, NULL);
    if (kid3 < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Cannot fork for kid3\n");

    /* Feed the PostScript header and the FIFO contents */
    if (prepend)
        fwrite(prepend->data, prepend->len, 1, kid3in);

    /* We are the parent, return glob to the file handle */
    *fd = kid3in;
    *pid = kid3;

    free_dstr(cmdline);
}

/* Close the renderer process and wait until all kid processes finish */
int close_renderer_handle(FILE *rendererhandle, pid_t rendererpid)
{
    int status;

    _log("\nClosing renderer\n");
    fclose(rendererhandle);

    status = wait_for_process(rendererpid);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
}


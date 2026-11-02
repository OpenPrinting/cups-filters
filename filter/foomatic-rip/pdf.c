//
// pdf.c
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

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


//
// Ghostscript treats file names starting with '|' or '%' as pipes or
// devices, so give it relative names as "./name"
//

static const char *
gs_file_name(const char *name,
	     char *buf,
	     size_t bufsize)
{
  if (name[0] == '/')
    return (name);

  snprintf(buf, bufsize, "./%s", name);
  return (buf);
}


//
// Quote a file name for the renderer's shell command line, as
// 'name' with every ' written as '\''.  Returns a malloc()ed string.
//

static char *
shell_quote(const char *s)
{
  size_t len = 3;
  const char *p;
  char *q, *r;

  for (p = s; *p; p ++)
    len += (*p == '\'') ? 4 : 1;

  if ((q = malloc(len)) == NULL)
    rip_die(EXIT_STARVED, "Not enough memory to quote a file name\n");

  r = q;
  *r++ = '\'';
  for (p = s; *p; p ++)
  {
    if (*p == '\'')
    {
      memcpy(r, "'\\''", 4);
      r += 4;
    }
    else
      *r++ = *p;
  }
  *r++ = '\'';
  *r = '\0';

  return (q);
}


int
pdf_count_pages(const char *filename)
{
  char file_arg[PATH_MAX + 32], name[PATH_MAX + 2];
  char output[64] = "";
  int pagecount;
  size_t bytes, total_read;
  char *p;
  pid_t pid;

  // Hand the file name to Ghostscript as a string parameter, so it does not
  // need to be quoted, neither for a shell nor inside the PostScript code
  snprintf(file_arg, sizeof(file_arg), "-sFoomaticPDFFile=%s",
	   gs_file_name(filename, name, sizeof(name)));
  char *gsargv[] =
  {
    gspath, "-dNODISPLAY", "-dNOSAFER", "-dNOPAUSE", "-q", file_arg, "-c",
    "/pdffile FoomaticPDFFile (r) file runpdfbegin (PageCount: ) print "
    "pdfpagecount = quit",
    NULL
  };

  FILE *pd = open_argv_process(gsargv, 0, &pid);
  if (pd == NULL)
    rip_die(EXIT_STARVED,
	    "Failed to execute ghostscript to determine number of input pages!\n");

  // Leave room for a terminator: if ghostscript's output filled the whole
  // buffer, sscanf()/memchr() below must not run past the end looking for
  // one that was never there.
  total_read = fread_or_die(output, 1, sizeof(output) - 1, pd);
  output[total_read] = '\0';
  close_argv_process(pd, pid);

  p = output;
  bytes = total_read;
  pagecount = -1;
  while (bytes > 0)
  {
    if (sscanf(p, "PageCount: %d", &pagecount) >= 1)
      break;
    p = memchr(p, '\n', bytes);
    if (p == NULL)
      break;
    p ++;
    bytes = total_read - (p - output);
  }

  return (pagecount);
}

pid_t kid3 = 0;


static int
start_renderer(const char *cmd)
{
  if (kid3 != 0)
    wait_for_renderer();

  _log("Starting renderer with command: %s\n", cmd);
  kid3 = start_process("kid3", exec_kid3, (void *)cmd, NULL, NULL);
  if (kid3 < 0)
    rip_die(EXIT_STARVED, "Could not start renderer\n");

  return (1);
}


static int
wait_for_renderer()
{
  int status;

  if (kid3 == 0)
    return (1);

  waitpid(kid3, &status, 0);

  if (!WIFEXITED(status))
  {
    _log("Kid3 did not finish normally.\n");
    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
  }

  _log("Kid3 exit status: %d\n", WEXITSTATUS(status));
  if (WEXITSTATUS(status) != 0)
    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);

  kid3 = 0;
  return (1);
}


//
// Extract pages 'first' through 'last' from the pdf and write them into a
// temporary file.
//

static int
pdf_extract_pages(char filename[PATH_MAX],
		  const char *pdffilename,
		  int first,
		  int last)
{
  char filename_arg[2 * PATH_MAX + 16], first_arg[50], last_arg[50];
  char name[PATH_MAX + 2], inname[PATH_MAX + 2];
  const char *p;
  char *q;
  char *gsargv[16];
  int fd, n = 0;
  pid_t pid;

  _log("Extracting pages %d through %d\n", first, last);

  snprintf(filename, PATH_MAX, "%s/foomatic-XXXXXX", temp_dir());
  if ((fd = mkstemp(filename)) == -1)
    rip_die(EXIT_STARVED, "Unable to create temporary file!\n");
  close (fd);

  // Ghostscript expands '%' in output file names (e.g. "%d" for the page
  // number), so double them
  snprintf(filename_arg, sizeof(filename_arg), "-sOutputFile=");
  q = filename_arg + strlen(filename_arg);
  for (p = gs_file_name(filename, name, sizeof(name));
       *p && q < filename_arg + sizeof(filename_arg) - 2; p ++)
  {
    if (*p == '%')
      *q++ = '%';
    *q++ = *p;
  }
  *q = '\0';

  first_arg[0] = '\0';
  last_arg[0] = '\0';
  if (first > 1)
    snprintf(first_arg, 50, "-dFirstPage=%d", first);
  if (last >= first)
    snprintf(last_arg, 50, "-dLastPage=%d", last);

  gsargv[n ++] = gspath;
  gsargv[n ++] = "-q";
  gsargv[n ++] = "-dNOPAUSE";
  gsargv[n ++] = "-dBATCH";
  gsargv[n ++] = "-dSAFER";
  gsargv[n ++] = "-dNOINTERPOLATE";
  gsargv[n ++] = "-dNOMEDIAATTRS";
  gsargv[n ++] = "-sDEVICE=pdfwrite";
  gsargv[n ++] = "-dShowAcroForm";
  gsargv[n ++] = filename_arg;
  if (first_arg[0])
    gsargv[n ++] = first_arg;
  if (last_arg[0])
    gsargv[n ++] = last_arg;
  gsargv[n ++] = (char *)gs_file_name(pdffilename, inname, sizeof(inname));
  gsargv[n] = NULL;

  FILE *pd = open_argv_process(gsargv, 0, &pid);
  if (!pd)
    rip_die(EXIT_STARVED, "Could not run ghostscript to extract the pages!\n");

  if (close_argv_process(pd, pid) != 0)
    _log("Warning: ghostscript process exited with non-zero status\n");

  return (1);
}


static int
render_pages_with_generic_command(dstr_t *cmd,
				  const char *filename,
				  int firstpage,
				  int lastpage)
{
  char tmpfile[PATH_MAX];
  char *quoted;
  int result;

  // TODO it might be a good idea to give pdf command lines the possibility
  // to get the file on the command line rather than piped through stdin
  // (maybe introduce a &filename; ??)

  if (lastpage < 0)  // i.e. print the whole document
  {
    quoted = shell_quote(filename);
    dstrcatf(cmd, " < %s", quoted);
    free(quoted);
  }
  else
  {
    if (!pdf_extract_pages(tmpfile, filename, firstpage, lastpage))
      rip_die(EXIT_STARVED,
	      "Could not run ghostscript to extract the pages!\n");
    quoted = shell_quote(tmpfile);
    dstrcatf(cmd, " < %s", quoted);
    free(quoted);
  }

  result = start_renderer(cmd->data);

  // The renderer reads the extracted pages through its shell's "<", so let
  // it finish before removing the file
  if (lastpage > 0)
  {
    wait_for_renderer();
    unlink(tmpfile);
  }

  return (result);
}


static int
render_pages_with_ghostscript(dstr_t *cmd,
			      size_t start_gs_cmd,
			      size_t end_gs_cmd,
			      const char *filename,
			      int firstpage,
			      int lastpage)
{
  char *p, *quoted;
  char name[PATH_MAX + 2];

  // No need to create a temporary file, just give ghostscript the file and
  // first/last page on the command line

  // Some command lines want to read from stdin
  for (p = &cmd->data[end_gs_cmd - 1];
       p > &cmd->data[start_gs_cmd] && isspace(*p);
       p--);
  if (*p == '-')
    *p = ' ';

  quoted = shell_quote(gs_file_name(filename, name, sizeof(name)));
  dstrinsertf(cmd, end_gs_cmd, " %s ", quoted);
  free(quoted);

  dstrinsertf(cmd, start_gs_cmd + 2, " -dShowAcroForm ");

  if (lastpage >= firstpage)
    dstrinsertf(cmd, start_gs_cmd +2,
		" -dFirstPage=%d -dLastPage=%d ",
		firstpage, lastpage);
  else if (firstpage > 1)
    dstrinsertf(cmd, start_gs_cmd +2,
		" -dFirstPage=%d ", firstpage);

  return (start_renderer(cmd->data));
}


static int
render_pages(int optset,
	     const char *filename,
	     int firstpage,
	     int lastpage)
{
  dstr_t *cmd = create_dstr();
  size_t start, end;
  int result;

  build_commandline(optset, cmd, 1);

  extract_command(&start, &end, cmd->data, "gs");
  if (start == end)
    // command is not Ghostscript
    result = render_pages_with_generic_command(cmd,
					       filename,
					       firstpage,
					       lastpage);
  else
    // Ghostscript command, tell it which pages we want to render
    result = render_pages_with_ghostscript(cmd,
					   start,
					   end,
					   filename,
					   firstpage,
					   lastpage);

  free_dstr(cmd);
  return (result);
}


static int
print_pdf_file(const char *filename)
{
  int page_count, i;
  int firstpage;

  page_count = pdf_count_pages(filename);

  if (page_count < 0)
    rip_die(EXIT_JOBERR,
	    "Unable to determine number of pages, page count: %d\n",
	    page_count);
  _log("File contains %d pages\n", page_count);
  if (page_count == 0)
  {
    _log("No pages left, outputting empty file.\n");
    return (1);
  }

  optionset_copy_values(optionset("header"), optionset("currentpage"));
  optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
  firstpage = 1;
  for (i = 1; i <= page_count; i++)
  {
    // Start from the job's options, so that options given for earlier
    // pages only don't carry over
    optionset_copy_values(optionset("header"), optionset("currentpage"));
    set_options_for_page(optionset("currentpage"), i);
    if (!optionset_equal(optionset("currentpage"),
			 optionset("previouspage"), 1))
    {
      // Render the pages before this one with their own options
      if (i > 1)
	render_pages(optionset("previouspage"), filename, firstpage, i - 1);
      firstpage = i;
    }
    optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
  }
  if (firstpage == 1)
    render_pages(optionset("currentpage"), filename, 1, -1); // Whole document
  else
    render_pages(optionset("currentpage"), filename, firstpage, page_count);

  wait_for_renderer();

  return (1);
}


int
print_pdf(FILE *s,
	  const char *alreadyread,
	  size_t len,
	  const char *filename,
	  size_t startpos)
{
  char tmpfilename[PATH_MAX] = "";
  int result;

  // If reading from stdin, write everything into a temporary file
  // TODO don't do this if there aren't any pagerange-limited options
  if (s == stdin)
  {
    int fd;
    FILE *tmpfile;

    snprintf(tmpfilename, PATH_MAX, "%s/foomatic-XXXXXX", temp_dir());
    fd = mkstemp(tmpfilename);
    if (fd < 0)
    {
      _log("Could not create temporary file: %s\n", strerror(errno));
      return (EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    tmpfile = fdopen(fd, "r+");
    copy_file(tmpfile, stdin, alreadyread, len);
    fclose(tmpfile);

    filename = tmpfilename;
  }

  result = print_pdf_file(filename);

  if (!isempty(tmpfilename))
    unlink(tmpfilename);

  return (result);
}


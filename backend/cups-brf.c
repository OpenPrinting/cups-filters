/*
 * BRF (Braille-Ready Format) virtual backend
 *
 * Copyright (c) 2017 by Samuel Thibault <samuel.thibault@ens-lyon.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "backend-private.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

int main(int argc, char *argv[]) {
  char *user;
  char *dir;
  char *title;
  char *outfile;
  char *c;
  char buffer[4096];
  ssize_t sizein, sizeout, done;
  struct passwd *pw;
  int ret;
  int fd;

  if (setuid(0)) {
    /* We need to be root to be able to turn into another user.  */
    fprintf(stderr,"ERROR: cups-brf must be called as root\n");
    return CUPS_BACKEND_FAILED;
  }

  if (argc == 1) {
    /* This is just discovery.  */
    printf("file cups-brf:/ \"Virtual Braille BRF Printer\" \"CUPS-BRF\" \"MFG:Generic;MDL:CUPS-BRF Printer;DES:Generic CUPS-BRF Printer;CLS:PRINTER;CMD:BRF;\"\n");
    return CUPS_BACKEND_OK;
  }

  if (argc < 6) {
    /* Invalid number of parameters.  */
    fprintf(stderr, "ERROR: cups-brf jobid user name nb options [filename]\n");
    return CUPS_BACKEND_FAILED;
  }

  if (argc == 7) {
    /* Explicit file name, open it.  */
    char *filename = argv[6];
    fd = open(filename,  O_RDONLY);
    if (dup2(fd, STDIN_FILENO) < 0) {
      fprintf(stderr, "ERROR: opening file \"%s\"\n", filename);
      return CUPS_BACKEND_FAILED;
    }
  }

  /* Now we have everything, turn into the user */
  user = argv[2];
  pw = getpwnam(user);
  if (pw == NULL) {
    fprintf(stderr, "ERROR: getting user \"%s\" information\n", user);
    return CUPS_BACKEND_FAILED;
  }
  if (setgid(pw->pw_gid)) {
    fprintf(stderr, "ERROR: turning gid into %u\n", pw->pw_gid);
    return CUPS_BACKEND_FAILED;
  }
  if (setuid(pw->pw_uid)) {
    fprintf(stderr, "ERROR: turning uid into %u\n", pw->pw_uid);
    return CUPS_BACKEND_FAILED;
  }

  /* Now we act as user */
  umask(0077);

  /* Create BRF directory in $HOME */
  if (asprintf(&dir, "%s/BRF", pw->pw_dir) < 0) {
    fprintf(stderr, "ERROR: could not allocate memory\n");
    return CUPS_BACKEND_FAILED;
  }
  fprintf(stderr, "DEBUG: creating directory \"%s\n", dir);
  ret = mkdir(dir, 0700);
  if (ret == -1 && errno != EEXIST) {
    fprintf(stderr, "ERROR: could not create directory \"%s\": %s\n", dir, strerror(errno));
    return CUPS_BACKEND_FAILED;
  }

  /* Avoid escaping from the directory */
  title = argv[3];
  for (c = title; *c; c++) {
    if (*c == '/')
      *c = '_';
  }
  /* Avoid hiding the file */
  while (*title == '.')
    title++;

  /* Avoid empty title */
  if (!*title)
    title = "unknown";

  /* generate mask */
  if (asprintf(&outfile, "%s/%s.XXXXXX.brf", dir, title) < 0) {
    fprintf(stderr, "ERROR: could not allocate memory\n");
    return CUPS_BACKEND_FAILED;
  }

  /* Create file */
  fprintf(stderr, "DEBUG: creating file \"%s\n", outfile);
  fd = mkstemps(outfile, 4);
  if (fd < 0) {
    fprintf(stderr, "ERROR: could not create file \"%s\": %s\n", outfile, strerror(errno));
    return CUPS_BACKEND_FAILED;
  }

  /* We are all set, copy data.  */
  while (1) {
    /* Read some.  */
    sizein = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (sizein < 0) {
      fprintf(stderr, "ERROR: while reading input: %s\n", strerror(errno));
      return CUPS_BACKEND_FAILED;
    }
    if (sizein == 0)
      /* We are done! */
      break;

    /* Write it.  */
    for (done = 0; done < sizein; done += sizeout) {
      sizeout = write(fd, buffer + done, sizein - done);
      if (sizeout < 0) {
	fprintf(stderr, "ERROR: while writing to \"%s\": %s\n", outfile, strerror(errno));
	return CUPS_BACKEND_FAILED;
      }
    }
  }
  if (close(fd) < 0) {
    fprintf(stderr, "ERROR: while closing \"%s\": %s\n", outfile, strerror(errno));
    return CUPS_BACKEND_FAILED;
  }

  return CUPS_BACKEND_OK;
}

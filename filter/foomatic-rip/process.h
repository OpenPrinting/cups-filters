/* process.h
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

#ifndef process_h
#define process_h

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t start_process(const char *name, int (*proc_func)(), void *user_arg, FILE **fdin, FILE **fdout);
pid_t start_system_process(const char *name, const char *command, FILE **fdin, FILE **fdout);

/* returns command's return status (see waitpid(2)) */
int run_system_process(const char *name, const char *command);

pid_t create_pipe_process(const char *name,
                          FILE *src,
                          FILE *dest,
                          const char *alreadyread,
                          size_t alreadyread_len);

int wait_for_process(int pid);

void kill_all_processes();

#endif


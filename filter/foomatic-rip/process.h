//
// process.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef process_h
#define process_h

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>


extern char modern_shell[];

pid_t start_process(const char *name, int (*proc_func)(FILE*, FILE*, void*), void *user_arg,
		    FILE **fdin, FILE **fdout);
pid_t start_system_process(const char *name, const char *command, FILE **fdin,
			   FILE **fdout);

const char *get_modern_shell();
// returns command's return status (see waitpid(2))
int run_system_process(const char *name, const char *command);

pid_t create_pipe_process(const char *name, FILE *src, FILE *dest,
                          const char *alreadyread, size_t alreadyread_len);

int wait_for_process(int pid);

void kill_all_processes();

#endif // !process_h

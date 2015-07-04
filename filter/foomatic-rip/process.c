/* process.c
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
#include "process.h"
#include <unistd.h>
#include "util.h"
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>

int kidgeneration = 0;

struct process {
    char name[64];
    pid_t pid;
    int isgroup;
};

#define MAX_CHILDS 4
struct process procs[MAX_CHILDS] = {
    { "", -1, 0 },
    { "", -1, 0 },
    { "", -1, 0 },
    { "", -1, 0 }
};

void add_process(const char *name, int pid, int isgroup)
{
    int i;
    for (i = 0; i < MAX_CHILDS; i++) {
        if (procs[i].pid == -1) {
            strlcpy(procs[i].name, name, 64);
            procs[i].pid = pid;
            procs[i].isgroup = isgroup;
            return;
        }
    }
    rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Didn't think there would be that many child processes... Exiting.\n");
}

int find_process(int pid)
{
    int i;
    for (i = 0; i < MAX_CHILDS; i++)
        if (procs[i].pid == pid)
            return i;
    return -1;
}

void clear_proc_list()
{
    int i;
    for (i = 0; i < MAX_CHILDS; i++)
        procs[i].pid = -1;
}

void kill_all_processes()
{
    int i;

    for (i = 0; i < MAX_CHILDS; i++) {
        if (procs[i].pid == -1)
            continue;
        _log("Killing %s\n", procs[i].name);
        kill(procs[i].isgroup ? -procs[i].pid : procs[i].pid, 15);
        sleep(1 << (3 - kidgeneration));
        kill(procs[i].isgroup ? -procs[i].pid : procs[i].pid, 9);
    }
    clear_proc_list();
}

static pid_t _start_process(const char *name,
                          int (*proc_func)(FILE *, FILE *, void *),
                          void *user_arg, FILE **pipe_in, FILE **pipe_out,
                          int createprocessgroup)
{
    pid_t pid;
    int pfdin[2], pfdout[2];
    int ret;
    FILE *in, *out;

    if (pipe_in)
      if (pipe(pfdin) < 0)
	return -1;
    if (pipe_out)
      if (pipe(pfdout) < 0)
	return -1;

    _log("Starting process \"%s\" (generation %d)\n", name, kidgeneration +1);

    pid = fork();
    if (pid < 0) {
        _log("Could not fork for %s\n", name);
        if (pipe_in) {
            close(pfdin[0]);
            close(pfdin[1]);
        }
        if (pipe_out) {
            close(pfdout[0]);
            close(pfdout[1]);
        }
        return -1;
    }

    if (pid == 0) { /* Child */
        if (pipe_in) {
            close(pfdin[1]);
            in = fdopen(pfdin[0], "r");
        }
        else
            in = NULL;

        if (pipe_out) {
            close(pfdout[0]);
            out = fdopen(pfdout[1], "w");
        }
        else
            out = NULL;

        if (createprocessgroup)
            setpgid(0, 0);

        kidgeneration++;

        /* The subprocess list is only valid for the parent. Clear it. */
        clear_proc_list();

        ret = proc_func(in, out, user_arg);
        exit(ret);
    }

    /* Parent */
    if (pipe_in) {
        close(pfdin[0]);
        *pipe_in = fdopen(pfdin[1], "w");
        if (!*pipe_in)
            _log("fdopen: %s\n", strerror(errno));
    }
    if (pipe_out) {
        close(pfdout[1]);
        *pipe_out = fdopen(pfdout[0], "r");
        if (!*pipe_out)
            _log("fdopen: %s\n", strerror(errno));
    }

    /* Add the child process to the list of open processes (to be able to kill
     * them in case of a signal. */
    add_process(name, pid, createprocessgroup);

    return pid;
}

int exec_command(FILE *in, FILE *out, void *cmd)
{
    if (in && dup2(fileno(in), fileno(stdin)) < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "%s: Could not dup stdin\n", (const char *)cmd);
    if (out && dup2(fileno(out), fileno(stdout)) < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "%s: Could not dup stdout\n", (const char *)cmd);

    execl(get_modern_shell(), get_modern_shell(), "-c", (const char *)cmd, (char *)NULL);

    _log("Error: Executing \"%s -c %s\" failed (%s).\n", get_modern_shell(), (const char *)cmd, strerror(errno));
    return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
}

pid_t start_system_process(const char *name, const char *command, FILE **fdin, FILE **fdout)
{
    return _start_process(name, exec_command, (void*)command, fdin, fdout, 1);
}

pid_t start_process(const char *name, int (*proc_func)(FILE *, FILE *, void *), void *user_arg, FILE **fdin, FILE **fdout)
{
    return _start_process(name, proc_func, user_arg, fdin, fdout, 0);
}

int wait_for_process(int pid)
{
    int i;
    int status;

    i = find_process(pid);
    if (i < 0) {
        _log("No such process \"%d\"", pid);
        return -1;
    }

    waitpid(procs[i].pid, &status, 0);
    if (WIFEXITED(status))
        _log("%s exited with status %d\n", procs[i].name, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        _log("%s received signal %d\n", procs[i].name, WTERMSIG(status));

    /* remove from process list */
    procs[i].pid = -1;
    return status;
}

int run_system_process(const char *name, const char *command)
{
    int pid = start_system_process(name, command, NULL, NULL);
    return wait_for_process(pid);
}


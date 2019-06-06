/* options.h
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

#ifndef options_h
#define options_h


#include <stddef.h>
#include <regex.h>
#include "util.h"

/* Option types */
#define TYPE_NONE       0
#define TYPE_ENUM       1
#define TYPE_PICKMANY   2
#define TYPE_BOOL       3
#define TYPE_INT        4
#define TYPE_FLOAT      5
#define TYPE_STRING     6
#define TYPE_PASSWORD   7
#define TYPE_CURVE      8
#define TYPE_INVCURVE   9
#define TYPE_PASSCODE   10
#define TYPE_POINTS     11

/* Sections */
#define SECTION_ANYSETUP        1
#define SECTION_PAGESETUP       2
#define SECTION_PROLOG          3
#define SECTION_DOCUMENTSETUP   4
#define SECTION_JCLSETUP        5



typedef struct choice_s {
    char value [128];
    char text [128];
    char command[65536];
    struct choice_s *next;
} choice_t;

/* Custom option parameter */
typedef struct param_s {
    char name [128];
    char text [128];       /* formerly comment, changed to 'text' to
                              be consistent with cups */
    int order;

    int type;
    char min[20], max[20]; /* contents depend on 'type' */

    regex_t *allowedchars;
    regex_t *allowedregexp;

    struct param_s *next;
} param_t;

/* Option */
typedef struct option_s {
    char name [128];
    char text [128];
    char varname [128];         /* clean version of 'name' (no spaces etc.) */
    int type;
    int style;
    char spot;
    double order;
    int section;

    int notfirst;               /* TODO remove */

    choice_t *choicelist;

    /* Foomatic PPD extensions */
    char *proto;                /* *FoomaticRIPOptionPrototype: if this is set
                                   it will be used with only the first option
                                   in paramlist (there should be only one) */
    param_t *foomatic_param;

    /* CUPS custom options */
    char *custom_command;       /* *CustomFoo */
    param_t *paramlist;         /* for custom values, sorted by stack order */
    size_t param_count;

    struct value_s *valuelist;

    struct option_s *next;
    struct option_s *next_by_order;
} option_t;


/* A value for an option */
typedef struct value_s {
    int optionset;
    char *value;
    option_t *fromoption; /* This is set when this value is set by a composite */
    struct value_s *next;
} value_t;


extern option_t *optionlist;
extern option_t *optionlist_sorted_by_order;

extern char jclbegin[256];
extern char jcltointerpreter[256];
extern char jclend[256];
extern char jclprefix[256];

extern char cmd[4096];
extern char cmd_pdf[4096];


int option_is_composite(option_t *opt);
int option_is_ps_command(option_t *opt);
int option_is_jcl_arg(option_t *opt);
int option_is_commandline_arg(option_t *opt);


int option_get_section(option_t *opt); /* TODO deprecated */

/* handles ANYSETUP (for (PAGE|DOCUMENT)SETUP) */
int option_is_in_section(option_t *opt, int section);

void options_init();
void options_free();

size_t option_count();
option_t *find_option(const char *name);

void read_ppd_file(const char *filename);

int ppd_supports_pdf();


int option_set_value(option_t *opt, int optset, const char *value);
const char * option_get_value(option_t *opt, int optset);

/* section == -1 for all sections */
int option_get_command(dstr_t *cmd, option_t *opt, int optset, int section);

int option_accepts_value(option_t *opt, const char *value);
int option_has_choice(option_t *opt, const char *choice);
int option_is_custom_value(option_t *opt, const char *value);


const char * optionset_name(int idx);
int optionset(const char * name);

void optionset_copy_values(int src_optset, int dest_optset);
int optionset_equal(int optset1, int optset2, int exceptPS);
void optionset_delete_values(int optionset);

void append_prolog_section(dstr_t *str, int optset, int comments);
void append_setup_section(dstr_t *str, int optset, int comments);
void append_page_setup_section(dstr_t *str, int optset, int comments);
int build_commandline(int optset, dstr_t *cmdline, int pdfcmdline);

void set_options_for_page(int optset, int page);
char *get_icc_profile_for_qualifier(const char **qualifier);
const char **get_ppd_qualifier(void);

#endif


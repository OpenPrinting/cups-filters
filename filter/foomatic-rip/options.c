/* options.c
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
#include "options.h"
#include "util.h"
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <regex.h>
#include <string.h>
#include <math.h>

/* qualifier -> filename mapping entry */
typedef struct icc_mapping_entry_s {
    char *qualifier;
    char *filename;
} icc_mapping_entry_t;

/* Values from foomatic keywords in the ppd file */
char printer_model [256];
char printer_id [256];
char driver [128];
char cmd [4096];
char cmd_pdf [4096];
dstr_t *postpipe = NULL;  /* command into which the output of this
                             filter should be piped */
char cupsfilter [256];
int jobentitymaxlen = 0;
int userentitymaxlen = 0;
int hostentitymaxlen = 0;
int titleentitymaxlen = 0;
int optionsentitymaxlen = 0;

/* JCL prefix to put before the JCL options
 (Can be modified by a "*JCLBegin:" keyword in the ppd file): */
char jclbegin[256] = "\033%-12345X@PJL\n";

/* JCL command to switch the printer to the PostScript interpreter
 (Can be modified by a "*JCLToPSInterpreter:" keyword in the PPD file): */
char jcltointerpreter[256] = "";

/* JCL command to close a print job
 (Can be modified by a "*JCLEnd:" keyword in the PPD file): */
char jclend[256] = "\033%-12345X@PJL RESET\n";

/* Prefix for starting every JCL command
 (Can be modified by "*FoomaticJCLPrefix:" keyword in the PPD file): */
char jclprefix[256] = "@PJL ";
int jclprefixset = 0;

dstr_t *prologprepend;
dstr_t *setupprepend;
dstr_t *pagesetupprepend;


list_t *qualifier_data = NULL;
char **qualifier = NULL;

option_t *optionlist = NULL;
option_t *optionlist_sorted_by_order = NULL;

int optionset_alloc, optionset_count;
char **optionsets;


const char * get_icc_profile_for_qualifier(const char **qualifier)
{
    char tmp[1024];
    char *profile = NULL;
    listitem_t *i;
    icc_mapping_entry_t *entry;

    /* no data */
    if (qualifier_data == NULL)
        goto out;

    /* search list for qualifier */
    snprintf(tmp, sizeof(tmp), "%s.%s.%s",
             qualifier[0], qualifier[1], qualifier[2]);
    for (i = qualifier_data->first; i != NULL; i = i->next) {
        entry = (icc_mapping_entry_t *) i->data;
        if (strcmp(entry->qualifier, tmp) == 0) {
            profile = entry->filename;
            break;
        }
    }
out:
    return profile;
}

/* a selector is a general tri-dotted specification.
 * The 2nd and 3rd elements of the qualifier are optionally modified by
 * cupsICCQualifier2 and cupsICCQualifier3:
 *
 * [Colorspace].[{cupsICCQualifier2}].[{cupsICCQualifier3}]
 */
const char **
get_ppd_qualifier ()
{
  return (const char**) qualifier;
}

const char * type_name(int type)
{
    switch (type) {
        case TYPE_NONE:
            return "none";
        case TYPE_ENUM:
            return "enum";
        case TYPE_PICKMANY:
            return "pickmany";
        case TYPE_BOOL:
            return "bool";
        case TYPE_INT:
            return "int";
        case TYPE_FLOAT:
            return "float";
        case TYPE_STRING:
            return "string";
    };
    _log("type '%d' does not exist\n", type);
    return NULL;
}

int type_from_string(const char *typestr)
{
    int type = TYPE_NONE;

    /* Official PPD options */
    if (!strcmp(typestr, "PickOne"))
        type = TYPE_ENUM;
    else if (!strcmp(typestr, "PickMany"))
        type = TYPE_PICKMANY;
    else if (!strcmp(typestr, "Boolean"))
        type = TYPE_BOOL;

    /* FoomaticRIPOption */
    else if (strcasecmp(typestr, "enum") == 0)
        type = TYPE_ENUM;
    else if (strcasecmp(typestr, "pickmany") == 0)
        type = TYPE_PICKMANY;
    else if (strcasecmp(typestr, "bool") == 0)
        type = TYPE_BOOL;
    else if (strcasecmp(typestr, "int") == 0)
        type = TYPE_INT;
    else if (strcasecmp(typestr, "float") == 0)
        type = TYPE_FLOAT;
    else if (strcasecmp(typestr, "string") == 0)
        type = TYPE_STRING;
    else if (strcasecmp(typestr, "password") == 0)
        type = TYPE_PASSWORD;

    return type;
}

char style_from_string(const char *style)
{
    char r = '\0';
    if (strcmp(style, "PS") == 0)
        r = 'G';
    else if (strcmp(style, "CmdLine") == 0)
        r = 'C';
    else if (strcmp(style, "JCL") == 0)
        r = 'J';
    else if (strcmp(style, "Composite") == 0)
        r = 'X';
    return r;
}

int section_from_string(const char *value)
{
    if (!strcasecmp(value, "AnySetup"))
        return SECTION_ANYSETUP;
    else if (!strcasecmp(value, "PageSetup"))
        return SECTION_PAGESETUP;
    else if (!strcasecmp(value, "Prolog"))
        return SECTION_PROLOG;
    else if (!strcasecmp(value, "DocumentSetup"))
        return SECTION_DOCUMENTSETUP;
    else if (!strcasecmp(value, "JCLSetup"))
        return SECTION_JCLSETUP;

    _log("Unknown section: \"%s\"\n", value);
    return 0;
}

void options_init()
{
    optionset_alloc = 8;
    optionset_count = 0;
    optionsets = calloc(optionset_alloc, sizeof(char *));

    prologprepend = create_dstr();
    setupprepend = create_dstr();
    pagesetupprepend = create_dstr();
}

static void free_param(param_t *param)
{
    if (param->allowedchars) {
        regfree(param->allowedchars);
        free(param->allowedchars);
    }

    if (param->allowedregexp) {
        regfree(param->allowedregexp);
        free(param->allowedregexp);
    }

    free(param);
}

/*
 *  Values
 */

static void free_value(value_t *val)
{
    if (val->value)
        free(val->value);
    free(val);
}


/*
 *  Options
 */
static void free_option(option_t *opt)
{
    choice_t *choice;
    param_t *param;
    value_t *value;

    free(opt->custom_command);
    free(opt->proto);

    while (opt->valuelist) {
        value = opt->valuelist;
        opt->valuelist = opt->valuelist->next;
        free_value(value);
    }
    while (opt->choicelist) {
        choice = opt->choicelist;
        opt->choicelist = opt->choicelist->next;
        free(choice);
    }
    while (opt->paramlist) {
        param = opt->paramlist;
        opt->paramlist = opt->paramlist->next;
        free_param(param);
    }
    if (opt->foomatic_param)
        free_param(opt->foomatic_param);

    free(opt);
}

void options_free()
{
    option_t *opt;
    int i;
    listitem_t *item;
    icc_mapping_entry_t *entry;

    for (i = 0; i < optionset_count; i++)
        free(optionsets[i]);
    free(optionsets);
    optionsets = NULL;
    optionset_alloc = 0;
    optionset_count = 0;

    if (qualifier_data) {
        for (item = qualifier_data->first; item != NULL; item = item->next) {
            entry = (icc_mapping_entry_t *) item->data;
            free(entry->qualifier);
            free(entry->filename);
            free(entry);
        }
        list_free(qualifier_data);
    }

    for (i=0; i<3; i++)
      free(qualifier[i]);
    free(qualifier);

    while (optionlist) {
        opt = optionlist;
        optionlist = optionlist->next;
        free_option(opt);
    }

    if (postpipe)
        free_dstr(postpipe);

    free_dstr(prologprepend);
    free_dstr(setupprepend);
    free_dstr(pagesetupprepend);
}

size_t option_count()
{
    option_t *opt;
    size_t cnt = 0;

    for (opt = optionlist; opt; opt = opt->next)
        cnt++;
    return cnt;
}

option_t * find_option(const char *name)
{
    option_t *opt;

    /* PageRegion and PageSize are the same options, just store one of them */
    if (!strcasecmp(name, "PageRegion"))
        return find_option("PageSize");

    for (opt = optionlist; opt; opt = opt->next) {
      if ((!strcasecmp(opt->name, name)) ||
	  ((!strcasecmp(opt->name, &name[2])) &&
	   (!prefixcasecmp(name, "no"))))
            return opt;
    }
    return NULL;
}

option_t * assure_option(const char *name)
{
    option_t *opt, *last;

    if ((opt = find_option(name)))
        return opt;

    opt = calloc(1, sizeof(option_t));

    /* PageRegion and PageSize are the same options, just store one of them */
    if (!strcmp(name, "PageRegion"))
        strlcpy(opt->name, "PageSize", 128);
    else
        strlcpy(opt->name, name, 128);

    /* set varname */
    strcpy(opt->varname, opt->name);
    strrepl(opt->varname, "-/.", '_');

    /* Default execution style is 'G' (PostScript) since all arguments for
    which we don't find "*Foomatic..." keywords are usual PostScript options */
    opt->style = 'G';

    opt->type = TYPE_NONE;

    /* append opt to optionlist */
    if (optionlist) {
        for (last = optionlist; last->next; last = last->next);
        last->next = opt;
    }
    else
        optionlist = opt;

    /* prepend opt to optionlist_sorted_by_order
       (0 is always at the beginning) */
    if (optionlist_sorted_by_order) {
        opt->next_by_order = optionlist_sorted_by_order;
        optionlist_sorted_by_order = opt;
    }
    else {
        optionlist_sorted_by_order = opt;
    }

    _log("Added option %s\n", opt->name);
    return opt;
}

/* This functions checks if "opt" is named "name", or if it has any
   alternative names "name" (e.g. PageSize / PageRegion) */
int option_has_name(option_t *opt, const char *name)
{
    if (!strcmp(opt->name, name))
        return 1;

    if (!strcmp(opt->name, "PageSize") && !strcmp(name, "PageRegion"))
        return 1;

    return 0;
}

int option_is_composite(option_t *opt)
{
    return opt ? (opt->style == 'X') : 0;
}

int option_is_ps_command(option_t *opt)
{
    return opt->style == 'G';
}

int option_is_jcl_arg(option_t *opt)
{
    return opt->style == 'J';
}

int option_is_commandline_arg(option_t *opt)
{
    return opt->style == 'C';
}

int option_get_section(option_t *opt)
{
    return opt->section;
}

static value_t * option_find_value(option_t *opt, int optionset)
{
    value_t *val;

    if (!opt)
        return NULL;

    for (val = opt->valuelist; val; val = val->next) {
        if (val->optionset == optionset)
            return val;
    }
    return NULL;
}

static value_t * option_assure_value(option_t *opt, int optionset)
{
    value_t *val, *last;
    val = option_find_value(opt, optionset);
    if (!val) {
        val = calloc(1, sizeof(value_t));
        val->optionset = optionset;

        /* append to opt->valuelist */
        if (opt->valuelist) {
            for (last = opt->valuelist; last->next; last = last->next);
            last->next = val;
        }
        else
            opt->valuelist = val;
    }
    return val;
}

static param_t * option_find_param_index(option_t *opt, const char *name, int *idx)
{
    param_t *param;
    int i;
    for (param = opt->paramlist, i = 0; param; param = param->next, i += 1) {
        if (!strcasecmp(param->name, name)) {
            if (idx)
                *idx = i;
            return param;
        }
    }
    if (idx)
        *idx = -1;
    return 0;
}

static choice_t * option_find_choice(option_t *opt, const char *name)
{
    choice_t *choice;
    assert(opt && name);
    for (choice = opt->choicelist; choice; choice = choice->next) {
        if (!strcasecmp(choice->value, name))
            return choice;
    }
    return NULL;
}

void free_paramvalues(option_t *opt, char **paramvalues)
{
    int i;
    if (!paramvalues)
        return;
    for (i = 0; i < opt->param_count; i++)
        free(paramvalues[i]);
    free(paramvalues);
}

char * get_valid_param_string(option_t *opt, param_t *param, const char *str)
{
    char *result;
    int i, imin, imax;
    float f, fmin, fmax;
    size_t len;

    switch (param->type) {
        case TYPE_INT:
            i = atoi(str);
            imin = !isempty(param->min) ? atoi(param->min) : -999999;
            imax = !isempty(param->max) ? atoi(param->max) : 1000000;
            if (i < imin) {
                _log("Value \"%s\" for option \"%s\", parameter \"%s\" is smaller than the minimum value \"%d\"\n",
                     str, opt->name, param->name, imin);
                return NULL;
            }
            else if (i > imax) {
                _log("Value \"%s\" for option \"%s\", parameter \"%s\" is larger than the maximum value \"%d\"\n",
                     str, opt->name, param->name, imax);
                return NULL;
            }
            result = malloc(32);
            snprintf(result, 32, "%d", i);
            return result;

        case TYPE_FLOAT:
        case TYPE_CURVE:
        case TYPE_INVCURVE:
        case TYPE_POINTS:
            f = atof(str);
            fmin = !isempty(param->min) ? atof(param->min) : -999999.0;
            fmax = !isempty(param->max) ? atof(param->max) : 1000000.0;
            if (f < fmin) {
                _log("Value \"%s\" for option \"%s\", parameter \"%s\" is smaller than the minimum value \"%d\"\n",
                     str, opt->name, param->name, fmin);
                return NULL;
            }
            else if (f > fmax) {
                _log("Value \"%s\" for option \"%s\", parameter \"%s\" is larger than the maximum value \"%d\"\n",
                     str, opt->name, param->name, fmax);
                return NULL;
             }
            result = malloc(32);
            snprintf(result, 32, "%f", f);
            return result;

        case TYPE_STRING:
        case TYPE_PASSWORD:
        case TYPE_PASSCODE:
            if (param->allowedchars &&
                    regexec(param->allowedchars, str, 0, NULL, 0) != 0) {
                _log("Custom string \"%s\" for \"%s\", parameter \"%s\" contains illegal characters.\n",
                    str, opt->name, param->name);
                return NULL;
            }
            if (param->allowedregexp &&
                    regexec(param->allowedregexp, str, 0, NULL, 0) != 0) {
                _log("Custom string \"%s\" for \"%s\", parameter \"%s\" does not match the allowed regexp.\n",
                    str, opt->name, param->name);
                return NULL;
            }
            len = strlen(str);
            if (!isempty(param->min) && len < atoi(param->min)) {
                _log("Custom value \"%s\" is too short for option \"%s\", parameter \"%s\".\n",
                    str, opt->name, param->name);
                return NULL;
            }
            if (!isempty(param->max) && len > atoi(param->max)) {
                _log("Custom value \"%s\" is too long for option \"%s\", parameter \"%s\".\n",
                    str, opt->name, param->name);
                return NULL;
            }
            return strdup(str);
    }
    return NULL;
}

char * get_valid_param_string_int(option_t *opt, param_t *param, int value)
{
    char str[20];
    snprintf(str, 20, "%d", value);
    return get_valid_param_string(opt, param, str);
}

char * get_valid_param_string_float(option_t *opt, param_t *param, float value)
{
    char str[20];
    snprintf(str, 20, "%f", value);
    return get_valid_param_string(opt, param, str);
}

float convert_to_points(float f, const char *unit)
{
    if (!strcasecmp(unit, "pt"))
        return roundf(f);
    if (!strcasecmp(unit, "in"))
        return roundf(f * 72.0);
    if (!strcasecmp(unit, "cm"))
        return roundf(f * 72.0 / 2.54);
    if (!strcasecmp(unit, "mm"))
        return roundf(f * 72.0 / 25.4);

    _log("Unknown unit: \"%s\"\n", unit);
    return roundf(f);
}

static char ** paramvalues_from_string(option_t *opt, const char *str)
{
    char ** paramvalues;
    int n, i;
    param_t *param;
    char *copy, *cur, *p;
    float width, height;
    char unit[3];

    if (!strcmp(opt->name, "PageSize"))
    {
        if (startswith(str, "Custom."))
            str = &str[7];
        /* 'unit' is optional, if it is not given, 'pt' is assumed */
        n = sscanf(str, "%fx%f%2s", &width, &height, unit);
        if (n > 1) {
            if (n == 3) {
                width = convert_to_points(width, unit);
                height = convert_to_points(height, unit);
            }
            paramvalues = calloc(opt->param_count, sizeof(char*));
            for (param = opt->paramlist, i = 0; param; param = param->next, i++) {
                if (!strcasecmp(param->name, "width"))
                    paramvalues[i] = get_valid_param_string_int(opt, param, (int)width);
                else if (!strcasecmp(param->name, "height"))
                    paramvalues[i] = get_valid_param_string_int(opt, param, (int)height);
                else
                    paramvalues[i] = !isempty(param->min) ? param->min : "-999999";
                if (!paramvalues[i]) {
                    free_paramvalues(opt, paramvalues);
                    return NULL;
                }
            }
            return paramvalues;
        }
    }

    if (opt->param_count == 1) {
        paramvalues = malloc(sizeof(char*));
        paramvalues[0] = get_valid_param_string(opt, opt->paramlist,
            startswith(str, "Custom.") ? &str[7] : str);
        if (!paramvalues[0]) {
            free(paramvalues);
            return NULL;
        }
    }
    else {
        if (!(p = strchr(str, '{')))
            return NULL;
        paramvalues = calloc(opt->param_count, sizeof(char*));
        copy = strdup(p +1);
        for (cur = strtok(copy, " \t}"); cur; cur = strtok(NULL, " \t}")) {
            p = strchr(cur, '=');
            if (!p)
                continue;
            *p++ = '\0';
            if ((param = option_find_param_index(opt, cur, &i)))
                paramvalues[i] = get_valid_param_string(opt, param, p);
            else
                _log("Could not find param \"%s\" for option \"%s\"\n",
                    cur, opt->name);
        }
        free(copy);

        /* check if all params have been set */
        for (i = 0; i < opt->param_count; i++) {
            if (!paramvalues[i]) {
                free_paramvalues(opt, paramvalues);
                return NULL;
            }
        }
    }
    return paramvalues;
}

char * paramvalues_to_string(option_t *opt, char **paramvalues)
{
    int i;
    param_t *param;
    dstr_t *res = create_dstr();
    char *data;

    if (opt->param_count < 1) {
        free (res);
        return NULL;
    }

    if (opt->param_count == 1) {
        param = opt->paramlist;
        dstrcpyf(res, "Custom.%s", paramvalues[0]);
    }
    else {
        dstrcpyf(res, "{%s=%s", opt->paramlist->name, paramvalues[0]);
        param = opt->paramlist->next;
        i = 1;
        while (param) {
            dstrcatf(res, " %s=%s", param->name, paramvalues[i]);
            i++;
            param = param->next;
        }
        dstrcat(res, "}");
    }
    /* only free dstr struct, NOT the string data */
    data = res->data;
    free(res);
    return data;
}

char * get_valid_value_string(option_t *opt, const char *value)
{
    char *res;
    choice_t *choice;
    char **paramvalues;

    if (!value)
        return NULL;

    if (startswith(value, "From") && option_is_composite(find_option(&value[4])))
        return strdup(value);

    if (opt->type == TYPE_BOOL) {
        if (is_true_string(value))
            return strdup("1");
        else if (is_false_string(value))
            return strdup("0");
        else {
            _log("Could not interpret \"%s\" as boolean value for option \"%s\".\n", value, opt->name);
            return NULL;
        }
    }

    /* Check if "value" is a predefined choice (except for "Custom", which is
     * not really a predefined choice, but an error if used without further
     * parameters) */
    if ((strcmp(value, "Custom") != 0 || strcmp(opt->name, "PageSize") == 0) &&
	(choice = option_find_choice(opt, value)))
        return strdup(choice->value);

    if (opt->type == TYPE_ENUM) {
        if (!strcasecmp(value, "none"))
            return strdup("None");

        /*
         * CUPS assumes that options with the choices "Yes", "No", "On", "Off",
         * "True", or "False" are boolean options and maps "-o Option=On" to
         * "-o Option" and "-o Option=Off" to "-o noOption", which foomatic-rip
         * maps to "0" and "1".  So when "0" or "1" is unavailable in the
         * option, we try "Yes", "No", "On", "Off", "True", and "False". 
         */
        if (is_true_string(value)) {
            for (choice = opt->choicelist; choice; choice = choice->next) {
                if (is_true_string(choice->value))
                    return strdup(choice->value);
            }
        }
        else if (is_false_string(value)) {
            for (choice = opt->choicelist; choice; choice = choice->next) {
                if (is_false_string(choice->value))
                    return strdup(choice->value);
            }
        }
    }

    /* Custom value */
    if (opt->paramlist) {
        paramvalues = paramvalues_from_string(opt, value);
        if (paramvalues) {
            res = paramvalues_to_string(opt, paramvalues);
            free(paramvalues);
            return (startswith(res, "Custom.") ? strdup(&res[7]) : strdup(res));
        }
    }
    else if (opt->foomatic_param)
        return get_valid_param_string(opt, opt->foomatic_param,
                              startswith(value, "Custom.") ? &value[7] : value);

    /* Return the default value */
    return NULL;
}

/* Returns the current value for 'opt' in 'optionset'. */
const char * option_get_value(option_t *opt, int optionset)
{
    value_t *val = option_find_value(opt, optionset);
    return val ? val->value : NULL;
}

/* Returns non-zero if the foomatic prototype should be used for that
 * optionset, otherwise the custom_command will be used */
int option_use_foomatic_prototype(option_t *opt)
{
    /* Only PostScript and JCL options can be CUPS custom options */
    if (!option_is_ps_command(opt) && !option_is_jcl_arg(opt))
        return 1;

    /* if only one of them exists, take that one */
    if (opt->custom_command && !opt->proto)
        return 0;
    if (!opt->custom_command && opt->proto)
        return 1;
    return 0;
}

void build_foomatic_custom_command(dstr_t *cmd, option_t *opt, const char *values)
{
    if (!opt->proto && !strcmp(opt->name, "PageSize"))
    {
        choice_t *choice = option_find_choice(opt, "Custom");
        char ** paramvalues = paramvalues_from_string(opt, values);
        char width[30], height[30];
        int pos;

        assert(choice);

        /* Get rid of the trailing ".00000", it confuses ghostscript */
        snprintf(width, 20, "%d", atoi(paramvalues[0]));
        snprintf(height, 20, "%d", atoi(paramvalues[1]));

        dstrcpy(cmd, choice->command);

        if ((pos = dstrreplace(cmd, "%0", width, 0)) < 0)
            pos = dstrreplace(cmd, "0", width, 0);

        if (dstrreplace(cmd, "%1", height, pos) < 0)
            dstrreplace(cmd, "0", height, pos);

        free_paramvalues(opt, paramvalues);
    }
    else
    {
        dstrcpy(cmd, opt->proto);
        /* use replace instead of printf-style because opt->proto could contain
           other format strings */
        dstrreplace(cmd, "%s", values, 0);
    }
}

void build_cups_custom_ps_command(dstr_t *cmd, option_t *opt, const char *values)
{
    param_t *param;
    int i;
    char **paramvalues = paramvalues_from_string(opt, values);

    dstrclear(cmd);
    for (param = opt->paramlist, i = 0; param; param = param->next, i++)
        dstrcatf(cmd, "%s ", paramvalues[i]);
    dstrcat(cmd, opt->custom_command);
    free_paramvalues(opt, paramvalues);
}

void build_cups_custom_jcl_command(dstr_t *cmd, option_t *opt, const char *values)
{
    param_t *param;
    int i;
    char orderstr[8];
    char **paramvalues = paramvalues_from_string(opt, values);

    dstrcpy(cmd, opt->custom_command);
    for (param = opt->paramlist, i = 0; param; param = param->next, i++) {
        snprintf(orderstr, 8, "\\%d", param->order);
        dstrreplace(cmd, orderstr, paramvalues[i], 0);
    }
    free_paramvalues(opt, paramvalues);
}

int composite_get_command(dstr_t *cmd, option_t *opt, int optionset, int section)
{
    char *copy, *cur, *p;
    option_t *dep;
    const char * valstr;
    dstr_t *depcmd;

    dstrclear(cmd);
    if (!option_is_composite(opt))
        return 0;

    if (!(valstr = option_get_value(opt, optionset)))
        return 0;

    depcmd = create_dstr();
    copy = strdup(valstr);
    /* Dependent options have been set to the right value in composite_set_values,
       so just find out which options depend on this composite and get their commands
       for "optionset" with option_get_command() */
    for (cur = strtok(copy, " \t"); cur; cur = strtok(NULL, " \t")) {
        dstrclear(depcmd);
        if ((p = strchr(cur, '='))) {
            *p++ = '\0';
            if ((dep = find_option(cur)))
                option_get_command(depcmd, dep, optionset, section);
        }
        else if (startswith(cur, "no") || startswith(cur, "No")) {
            if ((dep = find_option(&cur[2])))
                option_get_command(depcmd, dep, optionset, section);
            }
        else {
            if ((dep = find_option(cur)))
                option_get_command(depcmd, dep, optionset, section);
        }
        if (depcmd->len)
            dstrcatf(cmd, "%s\n", depcmd->data);
    }
    free(copy);
    free_dstr(depcmd);
    return cmd->len != 0;
}

int option_is_in_section(option_t *opt, int section)
{
    if (opt->section == section)
        return 1;
    if (opt->section == SECTION_ANYSETUP && (section == SECTION_PAGESETUP || section == SECTION_DOCUMENTSETUP))
        return 1;
    return 0;
}

int option_is_custom_value(option_t *opt, const char *value)
{
    if (opt->type == TYPE_BOOL || opt->type == TYPE_ENUM)
        return 0;

    return !option_has_choice(opt, value);
}

int option_get_command(dstr_t *cmd, option_t *opt, int optionset, int section)
{
    const char *valstr;
    choice_t *choice = NULL;

    dstrclear(cmd);

    if (option_is_composite(opt))
        return composite_get_command(cmd, opt, optionset, section);

    if (section >= 0 && !option_is_in_section(opt, section))
        return 1; /* empty command for this section */

    valstr = option_get_value(opt, optionset);
    if (!valstr)
        return 0;

    /* If the value is set to a predefined choice */
    choice = option_find_choice(opt, valstr);
    if (choice && (*choice->command ||
		   ((opt->type != TYPE_INT) && (opt->type != TYPE_FLOAT)))) {
        dstrcpy(cmd, choice->command);
        return 1;
    }

    /* Consider "None" as the empty string for string and password options */
    if ((opt->type == TYPE_STRING || opt->type == TYPE_PASSWORD) &&
	!strcasecmp(valstr, "None"))
        valstr = "";

    /* Custom value */
    if (option_use_foomatic_prototype(opt))
	build_foomatic_custom_command(cmd, opt, valstr);
    else {
	dstrcpy(cmd, opt->custom_command);
	if ((option_get_section(opt) == SECTION_JCLSETUP) ||
	    (opt->style == 'J'))
	    build_cups_custom_jcl_command(cmd, opt, valstr);
	else
	  build_cups_custom_ps_command(cmd, opt, valstr);
    }

    return cmd->len != 0;
}

void composite_set_values(option_t *opt, int optionset, const char *values)
{
    char *copy, *cur, *p;
    option_t *dep;
    value_t *val;

    copy = strdup(values);
    for (cur = strtok(copy, " \t"); cur; cur = strtok(NULL, " \t")) {
        if ((p = strchr(cur, '='))) {
            *p++ = '\0';
            if ((dep = find_option(cur))) {
                val = option_assure_value(dep, optionset);
                val->fromoption = opt;
                val->value = get_valid_value_string(dep, p);
            }
            else
                _log("Could not find option \"%s\" (set from composite \"%s\")", cur, opt->name);
        }
        else if (startswith(cur, "no") || startswith(cur, "No")) {
            if ((dep = find_option(&cur[2]))) {
                val = option_assure_value(dep, optionset);
                val->fromoption = opt;
                val->value = get_valid_value_string(dep, "0");
            }
        }
        else {
            if ((dep = find_option(cur))) {
                val = option_assure_value(dep, optionset);
                val->fromoption = opt;
                val->value = get_valid_value_string(dep, "1");
            }
        }
    }
    free(copy);
}

int option_set_value(option_t *opt, int optionset, const char *value)
{
    value_t *val = option_assure_value(opt, optionset);
    char *newvalue;
    choice_t *choice;
    option_t *fromopt;

    newvalue = get_valid_value_string(opt, value);
    if (!newvalue)
        return 0;

    free(val->value);
    val->value = NULL;

    if (startswith(newvalue, "From") && (fromopt = find_option(&newvalue[4])) &&
                option_is_composite(fromopt)) {
        /* TODO only set the changed option, not all of them */
        choice = option_find_choice(fromopt, 
                                    option_get_value(fromopt, optionset));

        composite_set_values(fromopt, optionset, choice->command);
    }
    else {
        val->value = newvalue;
    }

    if (option_is_composite(opt)) {
        /* set dependent values */
        choice = option_find_choice(opt, value);
        if (choice && !isempty(choice->command))
            composite_set_values(opt, optionset, choice->command);
    }
    return 1;
}

int option_accepts_value(option_t *opt, const char *value)
{
    char *val = get_valid_value_string(opt, value);
    if (!val)
        return 0;
    free(val);
    return 1;
}

int option_has_choice(option_t *opt, const char *choice)
{
    return option_find_choice(opt, choice) != NULL;
}

const char * option_text(option_t *opt)
{
    if (isempty(opt->text))
        return opt->text;
    return opt->text;
}

int option_type(option_t *opt)
{
    return opt->type;
}

void option_set_order(option_t *opt, double order)
{
    option_t *prev;

    /* remove opt from old position */
    if (opt == optionlist_sorted_by_order)
        optionlist_sorted_by_order = opt->next_by_order;
    else {
        for (prev = optionlist_sorted_by_order;
             prev && prev->next_by_order != opt;
             prev = prev->next_by_order);
        prev->next_by_order = opt->next_by_order;
    }

    opt->order = order;

    /* insert into new position */
    if (!optionlist_sorted_by_order)
        optionlist_sorted_by_order = opt;
    else if (optionlist_sorted_by_order->order > opt->order) {
        opt->next_by_order = optionlist_sorted_by_order;
        optionlist_sorted_by_order = opt;
    }
    else {
        for (prev = optionlist_sorted_by_order;
            prev->next_by_order && prev->next_by_order->order < opt->order;
            prev = prev->next_by_order);
        opt->next_by_order = prev->next_by_order;
        prev->next_by_order = opt;
    }
}

/* Set option from *FoomaticRIPOption keyword */
void option_set_from_string(option_t *opt, const char *str)
{
    char type[32], style[32];
    double order;
    int matches;

    matches = sscanf(str, "%31s %31s %c %lf", type, style, &opt->spot, &order);
    if (matches < 3) {
        _log("Can't read the value of *FoomaticRIPOption for \"%s\"", opt->name);
        return;
    }
    opt->type = type_from_string(type);
    opt->style = style_from_string(style);

    if (matches == 4)
        option_set_order(opt, order);
}

static choice_t * option_assure_choice(option_t *opt, const char *name)
{
    choice_t *choice, *last = NULL;

    for (choice = opt->choicelist; choice; choice = choice->next) {
        if (!strcasecmp(choice->value, name))
            return choice;
        last = choice;
    }
    if (!choice) {
        choice = calloc(1, sizeof(choice_t));
        if (last)
            last->next = choice;
        else
            opt->choicelist = choice;
        strlcpy(choice->value, name, 128);
    }
    return choice;
}

static void unhtmlify(char *dest, size_t size, const char *src)
{
    jobparams_t *job = get_current_job();
    char *pdest = dest;
    const char *psrc = src, *p = NULL;
    const char *repl;
    struct tm *t = localtime(&job->time);
    char tmpstr[10];
    size_t s, l, n;

    while (*psrc && pdest - dest < size - 1) {

        if (*psrc == '&') {
            psrc++;
            repl = NULL;
            p = NULL;
            l = 0;

            /* Replace HTML/XML entities by the original characters */
            if (!prefixcmp(psrc, "apos")) {
                repl = "\'";
                p = psrc + 4;
            } else if (!prefixcmp(psrc, "quot")) {
                repl = "\"";
                p = psrc + 4;
            } else if (!prefixcmp(psrc, "gt")) {
                repl = ">";
                p = psrc + 2;
            } else if (!prefixcmp(psrc, "lt")) {
                repl = "<";
                p = psrc + 2;
            } else if (!prefixcmp(psrc, "amp")) {
                repl = "&";
                p = psrc + 3;

            /* Replace special entities by job->data */
            } else if (!prefixcmp(psrc, "job")) {
                repl = job->id;
                p = psrc + 3;
                if (jobentitymaxlen != 0)
                    l = jobentitymaxlen;
            } else if (!prefixcmp(psrc, "user")) {
                repl = job->user;
                p = psrc + 4;
                if (userentitymaxlen != 0)
                    l = userentitymaxlen;
            } else if (!prefixcmp(psrc, "host")) {
                repl = job->host;
                p = psrc + 4;
                if (hostentitymaxlen != 0)
                    l = hostentitymaxlen;
            } else if (!prefixcmp(psrc, "title")) {
                repl = job->title;
                p = psrc + 5;
                if (titleentitymaxlen != 0)
                    l = titleentitymaxlen;
            } else if (!prefixcmp(psrc, "copies")) {
                repl = job->copies;
                p = psrc + 6;
            } else if (!prefixcmp(psrc, "rbinumcopies")) {
                if (job->rbinumcopies > 0) {
                    snprintf(tmpstr, 10, "%d", job->rbinumcopies);
                    repl = tmpstr;
                }
                else
                    repl = job->copies;
                p = psrc + 12;
            }
            else if (!prefixcmp(psrc, "options")) {
                repl = job->optstr->data;
                p = psrc + 7;
                if (optionsentitymaxlen != 0)
                    l = optionsentitymaxlen;
            } else if (!prefixcmp(psrc, "year")) {
                sprintf(tmpstr, "%04d", t->tm_year + 1900);
                repl = tmpstr;
                p = psrc + 4;
            }
            else if (!prefixcmp(psrc, "month")) {
                sprintf(tmpstr, "%02d", t->tm_mon + 1);
                repl = tmpstr;
                p = psrc + 5;
            }
            else if (!prefixcmp(psrc, "date")) {
                sprintf(tmpstr, "%02d", t->tm_mday);
                repl = tmpstr;
                p = psrc + 4;
            }
            else if (!prefixcmp(psrc, "hour")) {
                sprintf(tmpstr, "%02d", t->tm_hour);
                repl = tmpstr;
                p = psrc + 4;
            }
            else if (!prefixcmp(psrc, "min")) {
                sprintf(tmpstr, "%02d", t->tm_min);
                repl = tmpstr;
                p = psrc + 3;
            }
            else if (!prefixcmp(psrc, "sec")) {
                sprintf(tmpstr, "%02d", t->tm_sec);
                repl = tmpstr;
                p = psrc + 3;
            }
            if (p) {
                n = strtol(p, (char **)(&p), 0);
                if (n != 0)
                    l = n;
                if (*p != ';')
                    repl = NULL;
            } else
                repl = NULL;
            if (repl) {
                if ((l == 0) || (l > strlen(repl)))
                    l = strlen(repl);
                s = size - (pdest - dest) - 1;
                strncpy(pdest, repl, s);
                if (s < l)
                    pdest += s;
                else
                    pdest += l;
                psrc = p + 1;
            }
            else {
                *pdest = '&';
                pdest++;
            }
        }
        else {
            *pdest = *psrc;
            pdest++;
            psrc++;
        }
    }
    *pdest = '\0';
}

/*
 * Checks whether 'code' contains active PostScript, i.e. not only comments
 */
static int contains_active_postscript(const char *code)
{
    char **line, **lines;
    int contains_ps = 0;

    if (!(lines = argv_split(code, "\n", NULL)))
        return 0;

    for (line = lines; *line && !contains_ps; line++)
        contains_ps = !isempty(*line) && 
                      !startswith(skip_whitespace(*line), "%");

    argv_free(lines);
    return contains_ps;
}

void option_set_choice(option_t *opt, const char *name, const char *text,
                       const char *code)
{
    choice_t *choice;

    if (opt->type == TYPE_BOOL) {
        if (is_true_string(name))
            choice = option_assure_choice(opt, "1");
        else
            choice = option_assure_choice(opt, "0");
    }
    else
        choice = option_assure_choice(opt, name);

    if (text)
        strlcpy(choice->text, text, 128);

    if (!code)
    {
        _log("Warning: No code for choice \"%s\" of option \"%s\"\n",
             choice->text, opt->name);
        return;
    }

    if (!startswith(code, "%% FoomaticRIPOptionSetting"))
        unhtmlify(choice->command, 65536, code);
}

/*
 *  Parameters
 */

int param_set_allowed_chars(param_t *param, const char *value)
{
    char rxstr[128], tmp[128];

    param->allowedchars = malloc(sizeof(regex_t));
    unhtmlify(tmp, 128, value);
    snprintf(rxstr, 128, "^[%s]*$", tmp);
    if (regcomp(param->allowedchars, rxstr, 0) != 0) {
        regfree(param->allowedchars);
        param->allowedchars = NULL;
        return 0;
    }
    return 1;
}

int param_set_allowed_regexp(param_t *param, const char *value)
{
    char tmp[128];

    param->allowedregexp = malloc(sizeof(regex_t));
    unhtmlify(tmp, 128, value);
    if (regcomp(param->allowedregexp, tmp, 0) != 0) {
        regfree(param->allowedregexp);
        param->allowedregexp = NULL;
        return 0;
    }
    return 1;
}

void option_set_custom_command(option_t *opt, const char *cmd)
{
    size_t len = strlen(cmd) + 50;
    free(opt->custom_command);
    opt->custom_command = malloc(len);
    unhtmlify(opt->custom_command, len, cmd);
}

param_t * option_add_custom_param_from_string(option_t *opt,
    const char *name, const char *text, const char *str)
{
    param_t *param = calloc(1, sizeof(param_t));
    param_t *p;
    char typestr[33];
    int n;

    strlcpy(param->name, name, 128);
    strlcpy(param->text, text, 128);

    n = sscanf(str, "%d%15s%19s%19s",
        &param->order, typestr, param->min, param->max);

    if (n != 4) {
        _log("Could not parse custom parameter for '%s'!\n", opt->name);
        free(param);
        return NULL;
    }

    if (!strcmp(typestr, "curve"))
        param->type = TYPE_CURVE;
    else if (!strcmp(typestr, "invcurve"))
        param->type = TYPE_INVCURVE;
    else if (!strcmp(typestr, "int"))
        param->type = TYPE_INT;
    else if (!strcmp(typestr, "real"))
        param->type = TYPE_FLOAT;
    else if (!strcmp(typestr, "passcode"))
        param->type = TYPE_PASSCODE;
    else if (!strcmp(typestr, "password"))
        param->type = TYPE_PASSWORD;
    else if (!strcmp(typestr, "points"))
        param->type = TYPE_POINTS;
    else if (!strcmp(typestr, "string"))
        param->type = TYPE_STRING;
    else {
        _log("Unknown custom parameter type for param '%s' for option '%s'\n", param->name, opt->name);
        free(param);
        return NULL;
    }

    param->next = NULL;

    /* Insert param into opt->paramlist, sorted by order */
    if (!opt->paramlist)
        opt->paramlist = param;
    else if (opt->paramlist->order > param->order) {
        param->next = opt->paramlist;
        opt->paramlist = param;
    }
    else {
        for (p = opt->paramlist;
             p->next && p->next->order < param->order;
             p = p->next);
        param->next = p->next;
        p->next = param;
    }

    opt->param_count++;
    return param;
}

param_t * option_assure_foomatic_param(option_t *opt)
{
    param_t *param;

    if (opt->foomatic_param)
        return opt->foomatic_param;

    param = calloc(1, sizeof(param_t));
    strcpy(param->name, "foomatic-param");
    param->order = 0;
    param->type = opt->type;

    opt->foomatic_param = param;
    return param;
}


/*
 *  Optionsets
 */

const char * optionset_name(int idx)
{
    if (idx < 0 || idx >= optionset_count) {
        _log("Optionset with index %d does not exist\n", idx);
        return NULL;
    }
    return optionsets[idx];
}

int optionset(const char * name)
{
    int i;

    for (i = 0; i < optionset_count; i++) {
        if (!strcmp(optionsets[i], name))
            return i;
    }

    if (optionset_count == optionset_alloc) {
        optionset_alloc *= 2;
        optionsets = realloc(optionsets, optionset_alloc * sizeof(char *));
        for (i = optionset_count; i < optionset_alloc; i++)
            optionsets[i] = NULL;
    }

    optionsets[optionset_count] = strdup(name);
    optionset_count++;
    return optionset_count -1;
}

void optionset_copy_values(int src_optset, int dest_optset)
{
    option_t *opt;
    value_t *val;

    for (opt = optionlist; opt; opt = opt->next) {
        for (val = opt->valuelist; val; val = val->next) {
            if (val->optionset == src_optset) {
                option_set_value(opt, dest_optset, val->value);
                break;
            }
        }
    }
}

void optionset_delete_values(int optionset)
{
    option_t *opt;
    value_t *val, *prev_val;

    for (opt = optionlist; opt; opt = opt->next) {
        val = opt->valuelist;
        prev_val = NULL;
        while (val) {
            if (val->optionset == optionset) {
                if (prev_val)
                    prev_val->next = val->next;
                else
                    opt->valuelist = val->next;
                free_value(val);
                val = prev_val ? prev_val->next : opt->valuelist;
                break;
            } else {
                prev_val = val;
                val = val->next;
            }
        }
    }
}

int optionset_equal(int optset1, int optset2, int exceptPS)
{
    option_t *opt;
    const char *val1, *val2;

    for (opt = optionlist; opt; opt = opt->next) {
        if (exceptPS && opt->style == 'G')
            continue;

        val1 = option_get_value(opt, optset1);
        val2 = option_get_value(opt, optset2);

        if (val1 && val2) { /* both entries exist */
            if (strcmp(val1, val2) != 0)
                return 0; /* but aren't equal */
        }
        else if (val1 || val2) /* one entry exists --> can't be equal */
            return 0;
        /* If no extry exists, the non-existing entries
         * are considered as equal */
    }
    return 1;
}

/*
 *  read_ppd_file()
 */
void read_ppd_file(const char *filename)
{
    FILE *fh;
    const char *tmp;
    char *icc_qual2 = NULL;
    char *icc_qual3 = NULL;
    char line [256];            /* PPD line length is max 255 (excl. \0) */
    char *p;
    char key[128], name[64], text[64];
    dstr_t *value = create_dstr(); /* value can span multiple lines */
    double order;
    value_t *val;
    option_t *opt, *current_opt = NULL;
    param_t *param;
    icc_mapping_entry_t *entry;

    fh = fopen(filename, "r");
    if (!fh)
	rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Unable to open PPD file %s\n", filename);
    _log("Parsing PPD file ...\n");

    dstrassure(value, 256);

    qualifier_data = list_create();
    while (!feof(fh)) {
        tmp = fgets(line, 256, fh);

        if (line[0] != '*' || startswith(line, "*%"))
            continue;

        /* get the key */
        if (!(p = strchr(line, ':')))
            continue;
        *p = '\0';

        key[0] = name[0] = text[0] = '\0';
        sscanf(line, "*%127s%*[ \t]%63[^ \t/=)]%*1[/=]%63[^\n]", key, name, text);

        /* get the value */
        dstrclear(value);
        sscanf(p +1, " %255[^\r\n]", value->data);
        value->len = strlen(value->data);
        if (!value->len)
            _log("PPD: Missing value for key \"%s\"\n", line);

        while (1) {
            /* "&&" is the continue-on-next-line marker */
            if (dstrendswith(value, "&&")) {
                value->len -= 2;
                value->data[value->len] = '\0';
            }
            /* quoted but quotes are not yet closed */
            else if (value->data[0] == '\"' && !strchr(value->data +1, '\"'))
                dstrcat(value, "\n"); /* keep newlines in quoted string*/
            /* not quoted, or quotes already closed */
            else
                break;

            tmp = fgets(line, 256, fh);
            dstrcat(value, line);
            dstrremovenewline(value);
        }

        /* remove quotes */
        if (value->data[0] == '\"') {
            memmove(value->data, value->data +1, value->len +1);
            p = strrchr(value->data, '\"');
            if (!p) {
                _log("Invalid line: \"%s: ...\"\n", key);
                continue;
            }
            *p = '\0';
        }
        /* remove last newline */
        dstrremovenewline(value);

        /* remove last whitespace */
        dstrtrim_right(value);

        /* process key/value pairs */
        if (strcmp(key, "NickName") == 0) {
            unhtmlify(printer_model, 256, value->data);
        }
        else if (strcmp(key, "FoomaticIDs") == 0) {
            /* *FoomaticIDs: <printer ID> <driver ID> */
            sscanf(value->data, "%*[ \t]%127[^ \t]%*[ \t]%127[^ \t\n]",
                printer_id, driver);
        }
        else if (strcmp(key, "FoomaticRIPPostPipe") == 0) {
            if (!postpipe)
                postpipe = create_dstr();
            dstrassure(postpipe, value->len +128);
            unhtmlify(postpipe->data, postpipe->alloc, value->data);
        }
        else if (strcmp(key, "FoomaticRIPCommandLine") == 0) {
            unhtmlify(cmd, 4096, value->data);
        }
        else if (strcmp(key, "FoomaticRIPCommandLinePDF") == 0) {
            unhtmlify(cmd_pdf, 4096, value->data);
        }
        else if (!strcmp(key, "cupsFilter")) {
            /* cupsFilter: <code> */
            /* only save the filter for "application/vnd.cups-raster" */
            if (prefixcmp(value->data, "application/vnd.cups-raster") == 0) {
                p = strrchr(value->data, ' ');
                if (p)
                    unhtmlify(cupsfilter, 256, p +1);
            }
        }
        else if (startswith(key, "Custom") && !strcasecmp(name, "true")) {
            /* Cups custom option: *CustomFoo True: "command" */
            if (startswith(&key[6], "JCL")) {
                opt = assure_option(&key[9]);
                opt->style = 'J';
            }
            else
                opt = assure_option(&key[6]);
            option_set_custom_command(opt, value->data);
            if (!strcmp(key, "CustomPageSize"))
                option_set_custom_command(assure_option("PageRegion"), value->data);
        }
        else if (startswith(key, "ParamCustom")) {
            /* Cups custom parameter:
               *ParamCustomFoo Name/Text: order type minimum maximum */
            if (startswith(&key[11], "JCL"))
                opt = assure_option(&key[14]);
            else
                opt = assure_option(&key[11]);
            option_add_custom_param_from_string(opt, name, text, value->data);
        }
        else if (!strcmp(key, "OpenUI") || !strcmp(key, "JCLOpenUI")) {
            /* "*[JCL]OpenUI *<option>[/<translation>]: <type>" */
            current_opt = assure_option(&name[1]);
            if (!isempty(text))
                strlcpy(current_opt->text, text, 128);
            if (startswith(key, "JCL"))
                current_opt->style = 'J';
            /* Set the argument type only if not defined yet,
            a definition in "*FoomaticRIPOption" has priority */
            if (current_opt->type == TYPE_NONE)
                current_opt->type = type_from_string(value->data);
        }
        else if (!strcmp(key, "CloseUI") || !strcmp(key, "JCLCloseUI")) {
            /* *[JCL]CloseUI: *<option> */
            if (!current_opt || !option_has_name(current_opt, value->data +1))
                _log("CloseUI found without corresponding OpenUI (%s).\n", value->data +1);
            current_opt = NULL;
        }
        else if (!strcmp(key, "FoomaticRIPOption")) {
            /* "*FoomaticRIPOption <option>: <type> <style> <spot> [<order>]"
               <order> only used for 1-choice enum options */
            option_set_from_string(assure_option(name), value->data);
        }
        else if (!strcmp(key, "FoomaticRIPOptionPrototype")) {
            /* "*FoomaticRIPOptionPrototype <option>: <code>"
               Used for numerical and string options only */
            opt = assure_option(name);
            opt->proto = malloc(65536);
            unhtmlify(opt->proto, 65536, value->data);
        }
        else if (!strcmp(key, "FoomaticRIPOptionRange")) {
            /* *FoomaticRIPOptionRange <option>: <min> <max>
               Used for numerical options only */
            param = option_assure_foomatic_param(assure_option(name));
            sscanf(value->data, "%19s %19s", param->min, param->max);
        }
        else if (!strcmp(key, "FoomaticRIPOptionMaxLength")) {
            /*  "*FoomaticRIPOptionMaxLength <option>: <length>"
                Used for string options only */
            param = option_assure_foomatic_param(assure_option(name));
            sscanf(value->data, "%19s", param->max);
        }
        else if (!strcmp(key, "FoomaticRIPOptionAllowedChars")) {
            /* *FoomaticRIPOptionAllowedChars <option>: <code>
                Used for string options only */
            param = option_assure_foomatic_param(assure_option(name));
            param_set_allowed_chars(param, value->data);
        }
        else if (!strcmp(key, "FoomaticRIPOptionAllowedRegExp")) {
            /* "*FoomaticRIPOptionAllowedRegExp <option>: <code>"
               Used for string options only */
            param = option_assure_foomatic_param(assure_option(name));
            param_set_allowed_regexp(param, value->data);
        }
        else if (!strcmp(key, "OrderDependency")) {
            /* OrderDependency: <order> <section> *<option> */
            /* use 'text' to read <section> */
            sscanf(value->data, "%lf %63s *%63s", &order, text, name);
            opt = assure_option(name);
            opt->section = section_from_string(text);
            option_set_order(opt, order);
        }

        /* Default options are not yet validated (not all options/choices
           have been read yet) */
        else if (!prefixcmp(key, "Default")) {
            /* Default<option>: <value> */

            opt = assure_option(&key[7]);
            val = option_assure_value(opt, optionset("default"));
            free(val->value);
            val->value = strdup(value->data);
        }
        else if (!prefixcmp(key, "FoomaticRIPDefault")) {
            /* FoomaticRIPDefault<option>: <value>
               Used for numerical options only */
            opt = assure_option(&key[18]);
            val = option_assure_value(opt, optionset("default"));
            free(val->value);
            val->value = strdup(value->data);
        }

        /* Current argument */
        else if (current_opt && !strcmp(key, current_opt->name)) {
            /* *<option> <choice>[/translation]: <code> */
            option_set_choice(current_opt, name, text, value->data);
        }
        else if (!strcmp(key, "FoomaticRIPOptionSetting")) {
            /* "*FoomaticRIPOptionSetting <option>[=<choice>]: <code>
               For boolean options <choice> is not given */
            option_set_choice(assure_option(name),
                isempty(text) ? "true" : text, NULL, value->data);
        }

        /* "*(Foomatic|)JCL(Begin|ToPSInterpreter|End|Prefix): <code>"
           The printer supports PJL/JCL when there is such a line */
        else if (!prefixcmp(key, "JCLBegin") ||
                 !prefixcmp(key, "FoomaticJCLBegin")) {
            unhexify(jclbegin, 256, value->data);
            if (!jclprefixset && strstr(jclbegin, "PJL") == NULL)
                jclprefix[0] = '\0';
        }
        else if (!prefixcmp(key, "JCLToPSInterpreter") ||
                 !prefixcmp(key, "FoomaticJCLToPSInterpreter")) {
             unhexify(jcltointerpreter, 256, value->data);
        }
        else if (!prefixcmp(key, "JCLEnd") ||
                 !prefixcmp(key, "FoomaticJCLEnd")) {
             unhexify(jclend, 256, value->data);
        }
        else if (!prefixcmp(key, "JCLPrefix") ||
                 !prefixcmp(key, "FoomaticJCLPrefix")) {
            unhexify(jclprefix, 256, value->data);
            jclprefixset = 1;
        }
        else if (!prefixcmp(key, "% COMDATA #")) {
            /* old foomtic 2.0.x PPD file */
            _log("You are using an old Foomatic 2.0 PPD file, which is no "
                 "longer supported by Foomatic >4.0. Exiting.\n");
            exit(1); /* TODO exit more gracefully */
        }
        else if (!strcmp(key, "FoomaticRIPJobEntityMaxLength")) {
            /*  "*FoomaticRIPJobEntityMaxLength: <length>" */
            sscanf(value->data, "%d", &jobentitymaxlen);
        }
        else if (!strcmp(key, "FoomaticRIPUserEntityMaxLength")) {
            /*  "*FoomaticRIPUserEntityMaxLength: <length>" */
            sscanf(value->data, "%d", &userentitymaxlen);
        }
        else if (!strcmp(key, "FoomaticRIPHostEntityMaxLength")) {
            /*  "*FoomaticRIPHostEntityMaxLength: <length>" */
            sscanf(value->data, "%d", &hostentitymaxlen);
        }
        else if (!strcmp(key, "FoomaticRIPTitleEntityMaxLength")) {
            /*  "*FoomaticRIPTitleEntityMaxLength: <length>" */
            sscanf(value->data, "%d", &titleentitymaxlen);
        }
        else if (!strcmp(key, "FoomaticRIPOptionsEntityMaxLength")) {
            /*  "*FoomaticRIPOptionsEntityMaxLength: <length>" */
            sscanf(value->data, "%d", &optionsentitymaxlen);
        }
        else if (!strcmp(key, "cupsICCProfile")) {
            /*  "*cupsICCProfile: <qualifier/Title> <filename>" */
            entry = calloc(1, sizeof(icc_mapping_entry_t));
            entry->qualifier = strdup(name);
            entry->filename = strdup(value->data);
            list_append (qualifier_data, entry);
        }
        else if (!strcmp(key, "cupsICCQualifier2")) {
            /*  "*cupsICCQualifier2: <value>" */
            icc_qual2 = strdup(value->data);
        }
        else if (!strcmp(key, "cupsICCQualifier3")) {
            /*  "*cupsICCQualifier3: <value>" */
            icc_qual3 = strdup(value->data);
        }
    }

    fclose(fh);
    free_dstr(value);

    /* Validate default options by resetting them with option_set_value() */
    for (opt = optionlist; opt; opt = opt->next) {
        val = option_find_value(opt, optionset("default"));
        if (val) {
            /* if fromopt is set, this value has already been validated */
            if (!val->fromoption)
                option_set_value(opt, optionset("default"), val->value);
        }
        else
            /* Make sure that this option has a default choice, even if none is
               defined in the PPD file */
            option_set_value(opt, optionset("default"), opt->choicelist->value);
    }

    /* create qualifier for this PPD */
    qualifier = calloc(4, sizeof(char*));

    /* get colorspace */
    tmp = option_get_value(find_option("ColorSpace"), optionset("default"));
    if (tmp == NULL)
      tmp = option_get_value(find_option("ColorModel"), optionset("default"));
    if (tmp == NULL)
      tmp = "";
    qualifier[0] = strdup(tmp);

    /* get selector2 */
    if (icc_qual2 == NULL)
        icc_qual2 = strdup("MediaType");
    tmp = option_get_value(find_option(icc_qual2), optionset("default"));
    if (tmp == NULL)
      tmp = "";
    qualifier[1] = strdup(tmp);

    /* get selectors */
    if (icc_qual3 == NULL)
        icc_qual3 = strdup("Resolution");
    tmp = option_get_value(find_option(icc_qual3), optionset("default"));
    if (tmp == NULL)
      tmp = "";
    qualifier[2] = strdup(tmp);

    free (icc_qual2);
    free (icc_qual3);
}

int ppd_supports_pdf()
{
    option_t *opt;

    /* If at least one option inserts PostScript code, we cannot support PDF */
    for (opt = optionlist; opt; opt = opt->next)
    {
        choice_t *choice;

        if (!option_is_ps_command(opt) || option_is_composite(opt) ||
	    (opt->type == TYPE_NONE))
	  continue;

        for (choice = opt->choicelist; choice; choice = choice->next)
	  if (contains_active_postscript(choice->command)) {
	    _log("  PostScript option found: %s=%s: \"%s\"\n",
		 opt->name, choice->value, choice->command);
	    return 0;
	  }
    }

    if (!isempty(cmd_pdf))
        return 1;

    /* Ghostscript also accepts PDF, use that if it is in the normal command
     * line */
    if (startswith(cmd, "gs"))
    {
        strncpy(cmd_pdf, cmd, 4096);
        return 1;
    }

    _log("  Neither PDF renderer command line nor Ghostscript-based renderer command line found\n");
    return 0;
}

/* build a renderer command line, based on the given option set */
int build_commandline(int optset, dstr_t *cmdline, int pdfcmdline)
{
    option_t *opt;
    const char *userval;
    char *s, *p;
    dstr_t *cmdvar = create_dstr();
    dstr_t *open = create_dstr();
    dstr_t *close = create_dstr();
    char letters[] = "%A %B %C %D %E %F %G %H %I %J %K %L %M %W %X %Y %Z";
    int jcl = 0;

    dstr_t *local_jclprepend = create_dstr();

    dstrclear(prologprepend);
    dstrclear(setupprepend);
    dstrclear(pagesetupprepend);

    if (cmdline)
        dstrcpy(cmdline, pdfcmdline ? cmd_pdf : cmd);

    for (opt = optionlist_sorted_by_order; opt; opt = opt->next_by_order) {
        /* composite options have no direct influence, and all their dependents
           have already been set */
        if (option_is_composite(opt))
            continue;

        userval = option_get_value(opt, optset);
        option_get_command(cmdvar, opt, optset, -1);

        /* Insert the built snippet at the correct place */
        if (option_is_ps_command(opt)) {
            /* Place this Postscript command onto the prepend queue
               for the appropriate section. */
            if (cmdvar->len) {
                dstrcpyf(open, "[{\n%%%%BeginFeature: *%s ", opt->name);
                if (opt->type == TYPE_BOOL)
                    dstrcatf(open, is_true_string(userval) ? "True\n" : "False\n");
                else
                    dstrcatf(open, "%s\n", userval);
                dstrcpyf(close, "\n%%%%EndFeature\n} stopped cleartomark\n");

                switch (option_get_section(opt)) {
                    case SECTION_PROLOG:
                        dstrcatf(prologprepend, "%s%s%s", open->data, cmdvar->data, close->data);
                        break;

                    case SECTION_ANYSETUP:
                        if (optset != optionset("currentpage"))
                            dstrcatf(setupprepend, "%s%s%s", open->data, cmdvar->data, close->data);
                        else if (strcmp(option_get_value(opt, optionset("header")), userval) != 0)
                            dstrcatf(pagesetupprepend, "%s%s%s", open->data, cmdvar->data, close->data);
                        break;

                    case SECTION_DOCUMENTSETUP:
                        dstrcatf(setupprepend, "%s%s%s", open->data, cmdvar->data, close->data);
                        break;

                    case SECTION_PAGESETUP:
                        dstrcatf(pagesetupprepend, "%s%s%s", open->data, cmdvar->data, close->data);
                        break;

                    case SECTION_JCLSETUP:          /* PCL/JCL argument */
                        s = malloc(cmdvar->len +1);
                        unhexify(s, cmdvar->len +1, cmdvar->data);
                        dstrcatf(local_jclprepend, "%s", s);
                        free(s);
                        break;

                    default:
                        dstrcatf(setupprepend, "%s%s%s", open->data, cmdvar->data, close->data);
                }
            }
        }
        else if (option_is_jcl_arg(opt)) {
            jcl = 1;
            /* Put JCL commands onto JCL stack */
            if (cmdvar->len) {
                char *s = malloc(cmdvar->len +1);
                unhexify(s, cmdvar->len +1, cmdvar->data);
                if (!startswith(cmdvar->data, jclprefix))
                    dstrcatf(local_jclprepend, "%s%s\n", jclprefix, s);
                else
                    dstrcat(local_jclprepend, s);
                free(s);
            }
        }
        else if (option_is_commandline_arg(opt) && cmdline) {
            /* Insert the processed argument in the command line
            just before every occurrence of the spot marker. */
            p = malloc(3);
            snprintf(p, 3, "%%%c", opt->spot);
            s = malloc(cmdvar->len +3);
            snprintf(s, cmdvar->len +3, "%s%%%c", cmdvar->data, opt->spot);
            dstrreplace(cmdline, p, s, 0);
            free(p);
            free(s);
        }

        /* Insert option into command line of CUPS raster driver */
        if (cmdline && strstr(cmdline->data, "%Y")) {
            if (isempty(userval))
                continue;
            s = malloc(strlen(opt->name) + strlen(userval) + 20);
            sprintf(s, "%s=%s %%Y", opt->name, userval);
            dstrreplace(cmdline, "%Y", s, 0);
            free(s);
        }
    }

    /* Tidy up after computing option statements for all of P, J, and C types: */

    /* C type finishing */
    /* Pluck out all of the %n's from the command line prototype */
    if (cmdline) {
        s = strtok(letters, " ");
        do {
            dstrreplace(cmdline, s, "", 0);
        } while ((s = strtok(NULL, " ")));
    }

    /* J type finishing */
    /* Compute the proper stuff to say around the job */
    if (jcl && !jobhasjcl) {
        /* command to switch to the interpreter */
        dstrcatf(local_jclprepend, "%s", jcltointerpreter);

        /* Arrange for JCL RESET command at the end of job */
        dstrcpy(jclappend, jclend);

        argv_free(jclprepend);
        jclprepend = argv_split(local_jclprepend->data, "\r\n", NULL);
    }

    free_dstr(cmdvar);
    free_dstr(open);
    free_dstr(close);
    free_dstr(local_jclprepend);

    return !isempty(cmd);
}

/* if "comments" is set, add "%%BeginProlog...%%EndProlog" */
void append_prolog_section(dstr_t *str, int optset, int comments)
{
    /* Start comment */
    if (comments) {
        _log("\"Prolog\" section is missing, inserting it.\n");
        dstrcat(str, "%%BeginProlog\n");
    }

    /* Generate the option code (not necessary when CUPS is spooler and
       PostScript data is not converted from PDF) */
    if ((spooler != SPOOLER_CUPS) || pdfconvertedtops) {
        _log("Inserting option code into \"Prolog\" section.\n");
        build_commandline(optset, NULL, 0);
        dstrcat(str, prologprepend->data);
    }

    /* End comment */
    if (comments)
        dstrcat(str, "%%EndProlog\n");
}

void append_setup_section(dstr_t *str, int optset, int comments)
{
    /* Start comment */
    if (comments) {
        _log("\"Setup\" section is missing, inserting it.\n");
        dstrcat(str, "%%BeginSetup\n");
    }

    /* Generate the option code (not necessary when CUPS is spooler and
       PostScript data is not converted from PDF) */
    if ((spooler != SPOOLER_CUPS) || pdfconvertedtops) {
        _log("Inserting option code into \"Setup\" section.\n");
        build_commandline(optset, NULL, 0);
        dstrcat(str, setupprepend->data);
    }

    /* End comment */
    if (comments)
        dstrcat(str, "%%EndSetup\n");
}

void append_page_setup_section(dstr_t *str, int optset, int comments)
{
    /* Start comment */
    if (comments) {
        _log("\"PageSetup\" section is missing, inserting it.\n");
        dstrcat(str, "%%BeginPageSetup\n");
    }

    /* Generate the option code (not necessary when CUPS is spooler) */
    _log("Inserting option code into \"PageSetup\" section.\n");
    build_commandline(optset, NULL, 0);
    dstrcat(str, pagesetupprepend->data);

    /* End comment */
    if (comments)
        dstrcat(str, "%%EndPageSetup\n");
}


typedef struct page_range {
    short even, odd;
    unsigned first, last;
    struct page_range *next;
} page_range_t;

static page_range_t * parse_page_ranges(const char *ranges)
{
    page_range_t *head = NULL, *tail = NULL;
    char *tokens, *tok;
    int cnt;

    tokens = strdup(ranges);
    for (tok = strtok(tokens, ","); tok; tok = strtok(NULL, ",")) {
        page_range_t *pr = calloc(1, sizeof(page_range_t));

        if (startswith(tok, "even"))
            pr->even = 1;
        else if (startswith(tok, "odd"))
            pr->odd = 1;
        else if ((cnt = sscanf(tok, "%u-%u", &pr->first, &pr->last))) {
            /* If 'last' has not been read, this could mean only one page (no
             * hyphen) or all pages to the end */
            if (cnt == 1 && !endswith(tok, "-"))
                pr->last = pr->first;
            else if (cnt == 2 && pr->first > pr->last) {
                unsigned tmp = pr->first;
                pr->first = pr->last;
                pr->last = tmp;
            }
        }
        else {
            printf("Invalid page range: %s\n", tok);
            free(pr);
            continue;
        }

        if (tail) {
            tail->next = pr;
            tail = pr;
        }
        else
            tail = head = pr;
    }

    free(tokens);
    return head;
}

static void free_page_ranges(page_range_t *ranges)
{
    page_range_t *pr;
    while (ranges) {
        pr = ranges;
        ranges = ranges->next;
        free(pr);
    }
}

/* Parse a string containing page ranges and either check whether a
   given page is in the ranges or, if the given page number is zero,
   determine the score how specific this page range string is.*/
int get_page_score(const char *pages, int page)
{
    page_range_t *ranges = parse_page_ranges(pages);
    page_range_t *pr;
    int totalscore = 0;
    int pageinside = 0;

    for (pr = ranges; pr; pr = pr->next) {
        if (pr->even) {
            totalscore += 50000;
            if (page % 2 == 0)
                pageinside = 1;
        }
        else if (pr->odd) {
            totalscore += 50000;
            if (page % 2 == 1)
                pageinside = 1;
        }
        else if (pr->first == pr->last) {   /* Single page */
            totalscore += 1;
            if (page == pr->first)
                pageinside = 1;
        }
        else if (pr->last == 0) {           /* To the end of the document */
            totalscore += 100000;
            if (page >= pr->first)
                pageinside = 1;
        }
        else {                              /* Sequence of pages */
            totalscore += pr->last - pr->first +1;
            if (page >= pr->first && page <= pr->last)
                pageinside = 1;
        }
    }

    free_page_ranges(ranges);

    if (page == 0 || pageinside)
        return totalscore;

    return 0;
}

/* Set the options for a given page */
void set_options_for_page(int optset, int page)
{
    int score, bestscore;
    option_t *opt;
    value_t *val, *bestvalue;
    const char *ranges;
    const char *optsetname;

    for (opt = optionlist; opt; opt = opt->next) {

        bestscore = 10000000;
        bestvalue = NULL;
        for (val = opt->valuelist; val; val = val->next) {

            optsetname = optionset_name(val->optionset);
            if (!startswith(optsetname, "pages:"))
                continue;

            ranges = &optsetname[6]; /* after "pages:" */
            score = get_page_score(ranges, page);
            if (score && score < bestscore) {
                bestscore = score;
                bestvalue = val;
            }
        }

        if (bestvalue)
            option_set_value(opt, optset, bestvalue->value);
    }
}


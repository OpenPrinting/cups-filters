//
// util.h
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef util_h
#define util_h

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <cups/cups.h>
#include <string.h>
#include <stdio.h>

#if CUPS_VERSION_MAJOR <= 2 && CUPS_VERSION_MINOR < 5
#  define cupsArrayGetFirst(ar) cupsArrayFirst(ar)
#  define cupsArrayGetNext(ar) cupsArrayNext(ar)
#endif

// Constants used by this filter
//
// Error codes, as some spoolers behave different depending on the reason why
// the RIP failed, we return an error code.

#define EXIT_PRINTED 0                          // file was printed normally
#define EXIT_PRNERR 1                           // printer error occured
#define EXIT_PRNERR_NORETRY 2                   // printer error with no hope
                                                // of retry
#define EXIT_JOBERR 3                           // job is defective
#define EXIT_SIGNAL 4                           // terminated after catching
                                                // signal
#define EXIT_ENGAGED 5                          // printer is otherwise engaged
                                                // (connection refused)
#define EXIT_STARVED 6                          // starved for system resources
#define EXIT_PRNERR_NORETRY_ACCESS_DENIED 7     // bad password? bad port
                                                // permissions?
#define EXIT_PRNERR_NOT_RESPONDING 8            // just doesn't answer at all
                                                // (turned off?)
#define EXIT_PRNERR_NORETRY_BAD_SETTINGS 9      // interface settings are
                                                // invalid
#define EXIT_PRNERR_NO_SUCH_ADDRESS 10          // address lookup failed, may
                                                // be transient
#define EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS 11  // address lookup failed, not
                                                // transient
#define EXIT_PRNERR_NOTALLOWED 12               // the value is not allowed on the system
#define EXIT_INCAPABLE 50                       // printer wants (lacks)
                                                // features or resources

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CMDLINE_MAX 65536

#ifndef NAME_MAX
#define NAME_MAX 255
#endif


extern const char *hash_alg;
extern const char* shellescapes;
extern FILE* logh;

// used for path validation - parameter --ppd supports files, --ppd-paths directories
enum filetype {
  IS_FILE,
  IS_DIR
};

// logging and exiting...
void _log(const char* msg, ...);
void rip_die(int status, const char *msg, ...);
int redirect_log_to_stderr();
void close_log();

int isempty(const char *string);
const char * temp_dir();
int prefixcmp(const char *str, const char *prefix);
int prefixcasecmp(const char *str, const char *prefix);

int startswith(const char *str, const char *prefix);
int endswith(const char *str, const char *postfix);

const char * skip_whitespace(const char *str);

void strlower(char *dest, size_t destlen, const char *src);

// Like strncpy, but omits characters for which omit_func returns true
// It also assures that dest is zero terminated.
// Returns a pointer to the position in 'src' right after the last byte that
// has been copied.
const char * strncpy_omit(char* dest, const char* src, size_t n, int (*omit_func)(int));

int omit_unprintables(int c);
int omit_shellescapes(int c);
int omit_specialchars(int c);
int omit_whitespace(int c);
int omit_whitespace_newline(int c);

#ifndef HAVE_STRCASESTR
// strcasestr() is not available under Solaris
char * strcasestr (const char *haystack, const char *needle);
#endif

// TODO check for platforms which already have strlcpy and strlcat

// Copy at most size - 1 characters from src to dest
// dest will always be \0 terminated (unless size == 0)
// returns strlen(src)
#ifndef HAVE_STRLCPY
size_t strlcpy(char *dest, const char *src, size_t size);
#endif // ! HAVE_STRLCPY
#ifndef HAVE_STRLCAT
size_t strlcat(char *dest, const char *src, size_t size);
#endif // ! HAVE_STRLCAT

// Replace all occurences of each of the characters in 'chars' by 'repl'
void strrepl(char *str, const char *chars, char repl);

// Replace all occurences of each of the characters in 'chars' by 'repl',
// but do not allow consecutive 'repl' chars
void strrepl_nodups(char *str, const char *chars, char repl);

// clears 'str' with \0s
void strclr(char *str);

char *strnchr(const char *str, int c, size_t n);

void escapechars(char *dest, size_t size, const char *src,
		 const char *esc_chars);

// copies characters from 'src' to 'dest', until 'src' contains a character
//from 'stopchars'
// - will not copy more than 'max' chars
// - dest will be zero terminated in either case
// - returns a pointer to the position right after the last byte that has been
//   copied
const char *strncpy_tochar(char *dest, const char *src, size_t max,
			   const char *stopchars);

// "safe" versions of standard <cstdio> fwrite and fread that will cause the
// program to exit gracefully when a write/read fails
size_t fwrite_or_die(const void* ptr, size_t size, size_t count, FILE* stream);
size_t fread_or_die(void* ptr, size_t size, size_t count, FILE* stream);

// 'paths' is a colon seperated list of paths (like $PATH)
// 'found_in' may be NULL if it is not needed
int find_in_path(const char *progname, const char *paths, char *found_in);

// extracts the base name of 'path', i.e. only the filename, without path or
// extension
void file_basename(char *dest, const char *path, size_t dest_size);

// if 'path' is relative, prepend cwd
void make_absolute_path(char *path, int len);

int is_true_string(const char *str); // "1", "Yes", "On", "True"
int is_false_string(const char *str); // "0", "No", "Off", "False", "None"

int digit(char c); // returns 0-9 if c is a digit, otherwise -1

int line_count(const char *str);

// returns the index of the beginning of the line_number'th line in str
int line_start(const char *str, int line_number);

// Replace hex notation for unprintable characters in PPD files
// by the actual characters ex: "<0A>" --> chr(hex("0A"))
void unhexify(char *dest, size_t size, const char *src);

void extract_command(size_t *start, size_t *end, const char *cmdline,
		     const char *cmd);

char **argv_split(const char *string, const char *separators, int *cntp);
size_t argv_count(char **argv);
void argv_free(char **argv);

// Returns non-zero if 'cmdline' calls 'cmd' in some way
int contains_command(const char *cmdline, const char *cmd);

int copy_file(FILE *dest, FILE *src, const char *alreadyread,
	      size_t alreadyread_len);

// File related functions with CUPS arrays
int load_array(cups_array_t **ar, char *filename);
int is_valid_path(char *path, enum filetype type);

// Hash functions
int hash_data(unsigned char* data, size_t datalen, char *hash_string, size_t string_len);
int load_system_hashes(cups_array_t **hashes);

// Dynamic string
typedef struct dstr
{
  char *data;
  size_t len;
  size_t alloc;
} dstr_t;

dstr_t * create_dstr();
void free_dstr(dstr_t *ds);
void dstrclear(dstr_t *ds);
void dstrassure(dstr_t *ds, size_t alloc);
void dstrcpy(dstr_t *ds, const char *src);
void dstrncpy(dstr_t *ds, const char *src, size_t n);
void dstrncat(dstr_t *ds, const char *src, size_t n);
void dstrcpyf(dstr_t *ds, const char *src, ...);
void dstrcat(dstr_t *ds, const char *src);
void dstrcatf(dstr_t *ds, const char *src, ...);
void dstrputc(dstr_t *ds, int c);
size_t fgetdstr(dstr_t *ds, FILE *stream); // returns number of characters read
int dstrreplace(dstr_t *ds, const char *find, const char *repl, int start);
void dstrprepend(dstr_t *ds, const char *str);
void dstrinsert(dstr_t *ds, int idx, const char *str);
void dstrinsertf(dstr_t *ds, int idx, const char *str, ...);
void dstrremove(dstr_t *ds, int idx, size_t count);
void dstrcatline(dstr_t *ds, const char *str);
                            // appends the first line from str to ds (incl. \n)

int dstrendswith(dstr_t *ds, const char *str);
void dstrfixnewlines(dstr_t *ds);
void dstrremovenewline(dstr_t *ds);
void dstrtrim(dstr_t *ds);
void dstrtrim_right(dstr_t *ds);

// Doubly linked list of void pointers
typedef struct listitem_s
{
  void *data;
  struct listitem_s *prev, *next;
} listitem_t;

typedef struct
{
  listitem_t *first, *last;
} list_t;

list_t * list_create();
list_t * list_create_from_array(int count, void ** data);
                                                // array values are NOT copied
void list_free(list_t *list);

size_t list_item_count(list_t *list);

list_t * list_copy(list_t *list);

void list_prepend(list_t *list, void *data);
void list_append(list_t *list, void *data);
void list_remove(list_t *list, listitem_t *item);

listitem_t * list_get(list_t *list, int idx);

// Argument values may be seperated from their keys in the following ways:
//  - with whitespace (i.e. it is in the next list entry)
//  - with a '='
//  - not at all

listitem_t * arglist_find(list_t *list, const char *name);
listitem_t * arglist_find_prefix(list_t *list, const char *name);

char * arglist_get_value(list_t *list, const char *name);
char * arglist_get(list_t *list, int idx);

int arglist_remove(list_t *list, const char *name);
int arglist_remove_flag(list_t *list, const char *name);

#endif // !util_h

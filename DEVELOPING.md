Developing for CUPS FILTERS
===========================

Please see the [Contributing to CUPS](CONTRIBUTING.md) file for information on
contributing to the CUPS project.


How To Contact The Developers
-----------------------------

The Linux Foundation's "printing-architecture" mailing list is the primary means
of asking questions and informally discussing issues and feature requests with
the OpenPrinting developers.  To subscribe or see the mailing list archives, go
to <https://lists.linuxfoundation.org/mailman/listinfo/printing-architecture>.


Interfaces
----------

CUPS Filters interfaces, including the C APIs and command-line arguments,
environment variables, configuration files, and output format, are stable
across patch versions and are generally backwards-compatible with interfaces
used in prior major and minor versions.

CUPS Filters C APIs starting with an underscore (`_`) are considered to be
private to the library and are not subject to the normal guarantees of
stability between CUPS releases and must never be used in source code outside
this library. Similarly, configuration and state files written by CUPS Filters
are considered private if a corresponding man page is not provided with the
CUPS Filters release.  Never rely on undocumented files or formats when
developing software for CUPS Filters.  Always use a published C API to access
data stored in a file to avoid compatibility problems in the future.


Build System
------------

The CUPS Filters build system uses GNU autoconf, automake, and libtool to
tailor the software to the local operating system.


Version Numbering
-----------------

CUPS Filters uses a three-part version number separated by periods to represent
the major, minor, and patch release numbers.  Major release numbers indicate
large design changes or backwards-incompatible changes to the CUPS Filters API.
Minor release numbers indicate new features and other smaller changes which are
backwards-compatible with previous CUPS Filters releases.  Patch numbers
indicate bug fixes to the previous feature or patch release.  This version
numbering scheme is consistent with the
[Semantic Versioning](http://semver.org) specification.

> Note:
>
> When we talk about compatibility, we are talking about binary compatibility
> for public APIs and output format compatibility for program interfaces.
> Changes to configuration file formats or the default behavior of programs
> are not generally considered incompatible as the upgrade process can
> normally address such changes gracefully.

Production releases use the plain version numbers:

    MAJOR.MINOR.PATCH
    1.0.0
    ...
    1.1.0
    ...
    1.1.23
    ...
    2.0.0
    ...
    2.1.0
    2.1.1
    2.1.2
    2.1.3

The first production release in a MAJOR.MINOR series (MAJOR.MINOR.0) is called
a feature release.  Feature releases are the only releases that may contain new
features.  Subsequent production releases in a MAJOR.MINOR series may only
contain bug fixes.

Beta-test releases are identified by appending the letter B to the major and
minor version numbers followed by the beta release number:

    MAJOR.MINORbNUMBER
    2.2b1

Release candidates are identified by appending the letters RC to the major and
minor version numbers followed by the release candidate number:

    MAJOR.MINORrcNUMBER
    2.2rc1


Coding Guidelines
-----------------

Contributed source code must follow the guidelines below.  While the examples
are for C and C++ source files, source code for other languages should conform
to the same guidelines as allowed by the language.

Source code comments provide the reference portion for auto-generation
of developer documentation, which will be generated using the
[codedoc](https://www.msweet.org/codedoc) software.


### Source Files

All source files names must consist of lowercase ASCII letters, numbers, dash
("-"), underscore ("_"), and period (".") to ensure compatibility across
different filesystems.  Source files containing functions have an extension of
".c" for C and ".cxx" for C++ source files.  All other "include" files have an
extension of ".h".  Tabs are set to 8 characters/columns.

> Note: The ".cxx" extension is used because it is the only common C++ extension
> between Linux®, macOS®, Unix®, and Windows®.

The top of each source file contains a header giving the purpose or nature of
the source file and the copyright and licensing notice:

    /*
     * Description of file contents.
     *
     * Copyright © 2021-2022 by OpenPrinting
     *
     * Licensed under Apache License v2.0.  See the file "LICENSE" for more
     * information.
     */


### Header Files

Private API header files must be named with the suffix "-private", for example
the "xxx.h" header file defines all of the public APIs while the
"xxx-private.h" header file defines all of the private APIs as well.
Typically a private API header file will include the corresponding public API
header file.


### Comments

All source code utilizes block comments within functions to describe the
operations being performed by a group of statements; avoid putting a comment
per line unless absolutely necessary, and then consider refactoring the code
so that it is not necessary.  C source files can use either the block comment
format ("/* comment */") or the C99/C++ "//" comment format, with the block
comment format being preferred for multi-line comments:

    /*
     * Clear the state array before we begin.  Make sure that every
     * element is set to `CUPS_STATE_IDLE`.
     */

     for (i = 0; i < (sizeof(array) / sizeof(sizeof(array[0])); i ++)
       array[i] = CUPS_STATE_IDLE;

     // Wait for state changes on another thread...
     do
     {
       for (i = 0; i < (sizeof(array) / sizeof(sizeof(array[0])); i ++)
         if (array[i] != CUPS_STATE_IDLE)
           break;

       if (i == (sizeof(array) / sizeof(array[0])))
         sleep(1);
     } while (i == (sizeof(array) / sizeof(array[0])));


### Indentation

All code blocks enclosed by brackets begin with the opening brace on a new
line.  The code then follows starting on a new line after the brace and is
indented 2 spaces.  The closing brace is then placed on a new line following
the code at the original indentation:

    {
      int i; // Looping var

      // Process foobar values from 0 to 999...
      for (i = 0; i < 1000; i ++)
      {
        do_this(i);
        do_that(i);
      }
    }

Single-line statements following "do", "else", "for", "if", and "while" are
indented 2 spaces as well.  Blocks of code in a "switch" block are indented 4
spaces after each "case" and "default" case:

    switch (array[i])
    {
      case CUPS_STATE_IDLE :
          do_this(i);
          do_that(i);
          break;

      default :
          do_nothing(i);
          break;
    }


### Spacing

A space follows each reserved word such as `if`, `while`, etc.  Spaces are not
inserted between a function name and the arguments in parenthesis.


### Return Values

Parenthesis surround values returned from a function:

    return (CUPS_STATE_IDLE);


### Functions

Functions with a global scope have a lowercase prefix followed by capitalized
words, e.g., `cupsDoThis`, `cupsDoThat`, `cupsDoSomethingElse`, etc.  Private
global functions begin with a leading underscore, e.g., `_cupsDoThis`,
`_cupsDoThat`, etc.

Functions with a local scope are declared static with lowercase names and
underscores between words, e.g., `do_this`, `do_that`, `do_something_else`, etc.

Each function begins with a comment header describing what the function does,
the possible input limits (if any), the possible output values (if any), and
any special information needed:

    /*
     * 'do_this()' - Compute y = this(x).
     *
     * This function computes "this(x)" and returns the result. "x" must be
     * between 0.0 and 1.1.
     *
     * Notes: none.
     */

    static float       // O - Inverse power value, 0.0 <= y <= 1.1
    do_this(float x)   // I - Power value (0.0 <= x <= 1.1)
    {
      ...
      return (y);
    }

Return/output values are indicated using an "O" prefix, input values are
indicated using the "I" prefix, and values that are both input and output use
the "IO" prefix for the corresponding in-line comment.

The [codedoc](https://www.msweet.org/codedoc) documentation generator also
understands some markdown syntax as well as the following special text in the
function description comment:

    @deprecated@         - Marks the function as deprecated: not recommended
                           for new development and scheduled for removal.
    @link name@          - Provides a hyperlink to the corresponding function
                           or type definition.
    @since CUPS version@ - Marks the function as new in the specified version
                           of CUPS.
    @private@            - Marks the function as private so it will not be
                           included in the documentation.


### Variables

Variables with a global scope are capitalized, e.g., `ThisVariable`,
`ThatVariable`, `ThisStateVariable`, etc.  Globals in CUPS libraries are either
part of the per-thread global values managed by the `_cupsGlobals` function
or are suitably protected for concurrent access.  Global variables should be
replaced by function arguments whenever possible.

Variables with a local scope are lowercase with underscores between words,
e.g., `this_variable`, `that_variable`, etc.  Any "local global" variables
shared by functions within a source file are declared static.  As for global
variables, local static variables are suitably protected for concurrent access.

Each variable is declared on a separate line and is immediately followed by a
comment describing the variable:

    int         ThisVariable;    // The current state of this
    static int  that_variable;   // The current state of that


### Types

All type names are lowercase with underscores between words and `_t` appended
to the end of the name, e.g., `cups_this_type_t`, `cups_that_type_t`, etc.
Type names start with a prefix, typically `cups` or the name of the program,
to avoid conflicts with system types.  Private type names start with an
underscore, e.g., `_cups_this_t`, `_cups_that_t`, etc.

Each type has a comment immediately after the typedef:

    typedef int cups_this_type_t;  // This type is for CUPS foobar options.


### Structures

All structure names are lowercase with underscores between words and `_s`
appended to the end of the name, e.g., `cups_this_s`, `cups_that_s`, etc.
Structure names start with a prefix, typically `cups` or the name of the
program, to avoid conflicts with system types.  Private structure names start
with an underscore, e.g., `_cups_this_s`, `_cups_that_s`, etc.

Each structure has a comment immediately after the struct and each member is
documented similar to the variable naming policy above:

    struct cups_this_struct_s  // This structure is for CUPS foobar options.
    {
      int this_member;         // Current state for this
      int that_member;         // Current state for that
    };


### Constants

All constant names are uppercase with underscores between words, e.g.,
`CUPS_THIS_CONSTANT`, `CUPS_THAT_CONSTANT`, etc.  Constants begin with an
uppercase prefix, typically `CUPS_` or the program or type name.  Private
constants start with an underscore, e.g., `_CUPS_THIS_CONSTANT`,
`_CUPS_THAT_CONSTANT`, etc.

Typed enumerations should be used whenever possible to allow for type checking
by the compiler.

Comments immediately follow each constant:

    typedef enum cups_tray_e  // Tray enumerations
    {
      CUPS_TRAY_THIS,         // This tray
      CUPS_TRAY_THAT          // That tray
    } cups_tray_t;



#!/bin/sh

# just for comparison
/usr/lib/cups/filter/texttops 1 hi_user there_title 1 "" Makefile > test1.ps || :

# for the next to work, you'll have to make a subdirectory fonts/ here, containing the fonts
# and a subdirectory charsets/ with a file pdf.utf-8
export CUPS_DATADIR=`pwd`/

./texttopdf 1 hi_user there_title 1 "" Makefile > test1.pdf
./texttopdf 1 hi_user there_title 1 "PrettyPrint=1" Makefile > test2.pdf
(export CONTENT_TYPE=application/x-csource; ./texttopdf 1 hi_user there_title 1 "PrettyPrint=1" test_pdf1.c > test3.pdf)
(export CHARSET=utf-8; ./texttopdf 1 hi_user there_title 1 "PrettyPrint=1" Makefile > test4.pdf)
(export CHARSET=utf-8; ./texttopdf 1 hi_user there_title 1 "" testin > test5.pdf)

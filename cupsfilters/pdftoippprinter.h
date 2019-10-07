#ifndef _CUPS_FILTERS_PDFTOIPPPRINTER_H
#  define _CUPS_FILTERS_PDFTOIPPPRINTER_H

int         apply_filters(int argc, char *argv[]);
void		set_option_in_str(char *buf, int buflen,
					  const char *option,
					  const char *value);

#endif

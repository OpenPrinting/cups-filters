#ifndef _CUPS_FILTERS_PDFTOPDF_PDFTOPDF_JCL_H
#define _CUPS_FILTERS_PDFTOPDF_PDFTOPDF_JCL_H

struct _cfPDFToPDFProcessingParameters;
class _cfPDFToPDFProcessor;

void _cfPDFToPDFEmitPreamble(FILE *fp, ppd_file_t *ppd,
			     const _cfPDFToPDFProcessingParameters &param);
void _cfPDFToPDFEmitPostamble(FILE *fp, ppd_file_t *ppd,
			      const _cfPDFToPDFProcessingParameters &param);

void _cfPDFToPDFEmitComment(_cfPDFToPDFProcessor &proc,
			    const _cfPDFToPDFProcessingParameters &param);

#endif

#ifndef PDFTOPDF_JCL_H
#define PDFTOPDF_JCL_H

struct ProcessingParameters;

void emitPreamble(ppd_file_t *ppd,const ProcessingParameters &param);
void emitPostamble(ppd_file_t *ppd,const ProcessingParameters &param);

#endif

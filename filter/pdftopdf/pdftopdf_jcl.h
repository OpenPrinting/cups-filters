#ifndef PDFTOPDF_JCL_H
#define PDFTOPDF_JCL_H

struct ProcessingParameters;
class PDFTOPDF_Processor;

void emitPreamble(ppd_file_t *ppd,const ProcessingParameters &param);
void emitPostamble(ppd_file_t *ppd,const ProcessingParameters &param);

void emitComment(PDFTOPDF_Processor &proc,const ProcessingParameters &param);

#endif

#include <ctype.h>
#include "pdftopdf-processor-private.h"
#include <ppd/ppd.h>

#include <string.h>

// TODO: -currently changes ppd.  (Copies)
//
static void emitJCLOptions(FILE *fp, ppd_file_t *ppd, int device_copies) // {{{
{
  int section;
  ppd_choice_t **choices;
  int i;
  char buf[1024];
  ppd_attr_t *attr;
  bool withJCL=false,
    datawritten=false;

  if (!ppd) return;

  if ((attr = ppdFindAttr(ppd,"pdftopdfJCLBegin",NULL)) != NULL) {
    withJCL=true;
    const int n=strlen(attr->value);
    for (i = 0;i < n;i++) {
      if (attr->value[i] == '\r' || attr->value[i] == '\n') {
        // skip new line
        continue;
      }
      fputc(attr->value[i],fp);
      datawritten=true;
    }
  }
         
  snprintf(buf,sizeof(buf),"%d",device_copies);
  if (ppdFindOption(ppd,"Copies") != NULL) {
    ppdMarkOption(ppd,"Copies",buf);
  } else {
    if ((attr = ppdFindAttr(ppd,"pdftopdfJCLCopies",buf)) != NULL) {
      fputs(attr->value,fp);
      datawritten=true;
    } else if (withJCL) {
      fprintf(fp,"Copies=%d;",device_copies);
      datawritten=true;
    }
  }
  for (section = (int)PPD_ORDER_ANY;
       section <= (int)PPD_ORDER_PROLOG;section++) {
    int n = ppdCollect(ppd,(ppd_section_t)section,&choices);
    for (i = 0;i < n;i++) {
      snprintf(buf,sizeof(buf),"pdftopdfJCL%s",
	       ((ppd_option_t *)(choices[i]->option))->keyword);
      if ((attr = ppdFindAttr(ppd,buf,choices[i]->choice)) != NULL) {
        fputs(attr->value,fp);
        datawritten=true;
      } else if (withJCL) {
        fprintf(fp,"%s=%s;",
		((ppd_option_t *)(choices[i]->option))->keyword,
		choices[i]->choice);
        datawritten=true;
      }
    }
  }
  if (datawritten) {
    fputc('\n',fp);
  }
}
// }}}

/* Copied ppd_decode() from CUPS which is not exported to the API; needed in _cfPDFToPDFEmitPreamble() */
// {{{ static int ppd_decode(char *string) 
static int				/* O - Length of decoded string */
ppd_decode(char *string)		/* I - String to decode */
{
  char	*inptr,				/* Input pointer */
    *outptr;			/* Output pointer */

  inptr  = string;
  outptr = string;

  while (*inptr != '\0')
    if (*inptr == '<' && isxdigit(inptr[1] & 255)) {
      /*
       * Convert hex to 8-bit values...
       */

      inptr ++;
      while (isxdigit(*inptr & 255)) {
	if (isalpha(*inptr))
	  *outptr = (tolower(*inptr) - 'a' + 10) << 4;
	else
	  *outptr = (*inptr - '0') << 4;

	inptr ++;

        if (!isxdigit(*inptr & 255))
	  break;

	if (isalpha(*inptr))
	  *outptr |= tolower(*inptr) - 'a' + 10;
	else
	  *outptr |= *inptr - '0';

	inptr ++;
	outptr ++;
      }

      while (*inptr != '>' && *inptr != '\0')
	inptr ++;
      while (*inptr == '>')
	inptr ++;
    } else
      *outptr++ = *inptr++;

  *outptr = '\0';

  return ((int)(outptr - string));
}
// }}}

void _cfPDFToPDFEmitPreamble(FILE *fp, ppd_file_t *ppd,
		  const _cfPDFToPDFProcessingParameters &param) // {{{
{
  if (ppd == 0) return;

  ppdEmit(ppd, fp, PPD_ORDER_EXIT);

  if (param.emit_jcl) {
    /* pdftopdf only adds JCL to the job if the printer is a native PDF
       printer and the PPD is for this mode, having the "*JCLToPDFInterpreter:"
       keyword. We need to read this keyword manually from the PPD and replace
       the content of ppd->jcl_ps by the value of this keyword, so that
       ppdEmitJCL() actually adds JCL based on the presence on 
       "*JCLToPDFInterpreter:". */
    ppd_attr_t *attr;
    char buf[1024];
    int devicecopies_done = 0;
    char *old_jcl_ps = ppd->jcl_ps;
    /* If there is a "Copies" option in the PPD file, assure that hardware
       copies are implemented as described by this option */
    if (ppdFindOption(ppd,"Copies") != NULL &&
	param.device_copies > 1) {
      snprintf(buf,sizeof(buf),"%d",param.device_copies);
      ppdMarkOption(ppd,"Copies",buf);
      devicecopies_done = 1;
    }
    if ((attr=ppdFindAttr(ppd,"JCLToPDFInterpreter",NULL)) != NULL) {
      if (param.device_copies > 1 && devicecopies_done == 0 && // HW copies
	  strncmp(ppd->jcl_begin, "\033%-12345X@", 10) == 0) { // PJL
	/* Add a PJL command to implement the hardware copies */
        const size_t size=strlen(attr->value)+1+30;
        ppd->jcl_ps=(char *)malloc(size*sizeof(char));
        if (param.device_collate) {
          snprintf(ppd->jcl_ps, size, "@PJL SET QTY=%d\n%s",
                   param.device_copies, attr->value);
        } else {
          snprintf(ppd->jcl_ps, size, "@PJL SET COPIES=%d\n%s",
                   param.device_copies, attr->value);
        }
      } else
	ppd->jcl_ps=strdup(attr->value);
      ppd_decode(ppd->jcl_ps);
    } else {
      ppd->jcl_ps=NULL;
    }
    ppdEmitJCL(ppd, fp, param.job_id, param.user, param.title);
    emitJCLOptions(fp, ppd, param.device_copies);
    free(ppd->jcl_ps);
    ppd->jcl_ps = old_jcl_ps; // cups uses pool allocator, not free()
  }
}
// }}}

void _cfPDFToPDFEmitPostamble(FILE *fp, ppd_file_t *ppd,
		   const _cfPDFToPDFProcessingParameters &param) // {{{
{
  if (param.emit_jcl) { 
    ppdEmitJCLEnd(ppd, fp);
  }
}
// }}}

// pass information to subsequent filters via PDF comments
void _cfPDFToPDFEmitComment(_cfPDFToPDFProcessor &proc,const _cfPDFToPDFProcessingParameters &param) // {{{
{
  std::vector<std::string> output;

  output.push_back("% This file was generated by pdftopdf");

  // This is not standard, but like PostScript. 
  if (param.device_copies>0) {
    char buf[256];
    snprintf(buf,sizeof(buf),"%d",param.device_copies);
    output.push_back(std::string("%%PDFTOPDFNumCopies : ")+buf);

    if (param.device_collate) {
      output.push_back("%%PDFTOPDFCollate : true");
    } else {
      output.push_back("%%PDFTOPDFCollate : false");
    }
  }

  proc.set_comments(output);
}
// }}}

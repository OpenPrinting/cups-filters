#include <ctype.h>
#include "pdftopdf_processor.h"
#include <cups/ppd.h>

#include <string.h>

// TODO: -currently changes ppd.  (Copies)
//
static void emitJCLOptions(FILE *fp, ppd_file_t *ppd, int deviceCopies) // {{{
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
         
  snprintf(buf,sizeof(buf),"%d",deviceCopies);
  if (ppdFindOption(ppd,"Copies") != NULL) {
    ppdMarkOption(ppd,"Copies",buf);
  } else {
    if ((attr = ppdFindAttr(ppd,"pdftopdfJCLCopies",buf)) != NULL) {
      fputs(attr->value,fp);
      datawritten=true;
    } else if (withJCL) {
      fprintf(fp,"Copies=%d;",deviceCopies);
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

/* Copied ppd_decode() from CUPS which is not exported to the API; needed in emitPreamble() */
// {{{ static int ppd_decode(char *string) 
static int				/* O - Length of decoded string */
ppd_decode(char *string)		/* I - String to decode */
{
  char	*inptr,				/* Input pointer */
	*outptr;			/* Output pointer */


  inptr  = string;
  outptr = string;

  while (*inptr != '\0')
    if (*inptr == '<' && isxdigit(inptr[1] & 255))
    {
     /*
      * Convert hex to 8-bit values...
      */

      inptr ++;
      while (isxdigit(*inptr & 255))
      {
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
    }
    else
      *outptr++ = *inptr++;

  *outptr = '\0';

  return ((int)(outptr - string));
}
// }}}

void emitPreamble(ppd_file_t *ppd,const ProcessingParameters &param) // {{{
{
  if (ppd == 0) return;

  ppdEmit(ppd,stdout,PPD_ORDER_EXIT);

  if (param.emitJCL) {
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
	param.deviceCopies > 1) {
      snprintf(buf,sizeof(buf),"%d",param.deviceCopies);
      ppdMarkOption(ppd,"Copies",buf);
      devicecopies_done = 1;
    }
    if ( (attr=ppdFindAttr(ppd,"JCLToPDFInterpreter",NULL)) != NULL) {
      if (param.deviceCopies > 1 && devicecopies_done == 0 && // HW copies
	  strncmp(ppd->jcl_begin, "\033%-12345X@", 10) == 0) { // PJL
	/* Add a PJL command to implement the hardware copies */
        const size_t size=strlen(attr->value)+1+30;
        ppd->jcl_ps=(char *)malloc(size*sizeof(char));
        if (param.deviceCollate) {
          snprintf(ppd->jcl_ps, size, "@PJL SET QTY=%d\n%s",
                   param.deviceCopies, attr->value);
        } else {
          snprintf(ppd->jcl_ps, size, "@PJL SET COPIES=%d\n%s",
                   param.deviceCopies, attr->value);
        }
      } else
	ppd->jcl_ps=strdup(attr->value);
      ppd_decode(ppd->jcl_ps);
    } else {
      ppd->jcl_ps=NULL;
    }
    ppdEmitJCL(ppd,stdout,param.jobId,param.user,param.title);
    emitJCLOptions(stdout,ppd,param.deviceCopies);
    free(ppd->jcl_ps);
    ppd->jcl_ps = old_jcl_ps; // cups uses pool allocator, not free()
  }
}
// }}}

void emitPostamble(ppd_file_t *ppd,const ProcessingParameters &param) // {{{
{
  if (param.emitJCL) { 
    ppdEmitJCLEnd(ppd,stdout);
  }
}
// }}}

// pass information to subsequent filters via PDF comments
void emitComment(PDFTOPDF_Processor &proc,const ProcessingParameters &param) // {{{
{
  std::vector<std::string> output;

  output.push_back("% This file was generated by pdftopdf");

  // This is not standard, but like PostScript. 
  if (param.deviceCopies>0) {
    char buf[256];
    snprintf(buf,sizeof(buf),"%d",param.deviceCopies);
    output.push_back(std::string("%%PDFTOPDFNumCopies : ")+buf);

    if (param.deviceCollate) {
      output.push_back("%%PDFTOPDFCollate : true");
    } else {
      output.push_back("%%PDFTOPDFCollate : false");
    }
  }

  proc.setComments(output);
}
// }}}

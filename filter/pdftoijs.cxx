/*
Copyright (c) 2008, BBR Inc.  All rights reserved.
          (c) 2008 Tobias Hoffmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/
/*
 pdftoijs.cc
 pdf to ijs filter
*/

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif
#include <goo/GooString.h>
#include <goo/gmem.h>
#include <Object.h>
#include <Stream.h>
#include <PDFDoc.h>
#include <SplashOutputDev.h>
#include <cups/cups.h>
#include <cups/ppd.h>
#include <stdarg.h>
#include "PDFError.h"
#include <GlobalParams.h>
#include <splash/SplashTypes.h>
#include <splash/SplashBitmap.h>
extern "C" {
#include <ijs/ijs.h>
#include <ijs/ijs_client.h>
}
#include <vector>
#include <string>

#define MAX_CHECK_COMMENT_LINES	20

namespace {
  int exitCode = 0;
  char *outputfile = NULL;
//  int deviceCopies = 1;
//  bool deviceCollate = false;
  const char *ijsserver = NULL;
  int resolution[2] = {0,0};
  enum ColEnum { NONE=-1, COL_RGB, COL_CMYK, COL_BLACK1, COL_WHITE1, COL_BLACK8, COL_WHITE8 } colspace=NONE;
  const char *devManu=NULL, *devModel=NULL;
  std::vector<std::pair<std::string,std::string> > params;

  ppd_file_t *ppd = 0; // holds the memory for the strings
}

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
void CDECL myErrorFun(void *data, ErrorCategory category,
    Goffset pos, char *msg)
#else
void CDECL myErrorFun(void *data, ErrorCategory category,
    int pos, char *msg)
#endif
{
  if (pos >= 0) {
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
    fprintf(stderr, "ERROR (%lld): ", pos);
#else
    fprintf(stderr, "ERROR (%d): ", pos);
#endif
  } else {
    fprintf(stderr, "ERROR: ");
  }
  fprintf(stderr, "%s\n",msg);
  fflush(stderr);
}
#else
void CDECL myErrorFun(int pos, char *msg, va_list args)
{
  if (pos >= 0) {
    fprintf(stderr, "ERROR (%d): ", pos);
  } else {
    fprintf(stderr, "ERROR: ");
  }
  vfprintf(stderr, msg, args);
  fprintf(stderr, "\n");
  fflush(stderr);
}
#endif

/* parse  "300 400" */
void parse_resolution(const char *str)
{
  const char *tmp=strchr(str,' ');
  if (tmp) {
    resolution[0]=atoi(str);
    resolution[1]=atoi(tmp+1);
  } else {
    resolution[0]=resolution[1]=atoi(str);
  }
}

/* parse  "cmyk" "grey" "rgb" */
void parse_colorspace(const char *str)
{
  if (strcasecmp(str,"rgb")==0) {
    colspace=COL_RGB;
  } else if (strcasecmp(str,"black1")==0) {
    colspace=COL_BLACK1;
  } else if (strcasecmp(str,"white1")==0) {
    colspace=COL_WHITE1;
  } else if (strcasecmp(str,"black8")==0) {
    colspace=COL_BLACK8;
  } else if (strcasecmp(str,"white8")==0) {
    colspace=COL_WHITE8;
#ifdef SPLASH_CMYK
  } else if (strcasecmp(str,"cmyk")==0) {
    colspace=COL_CMYK;
  } else {
    pdfError(-1,"Unknown colorspace; supported are 'rgb', 'cmyk', 'white1', 'black1', 'white8', 'black8'");
#else
  } else {
    pdfError(-1,"Unknown colorspace; supported are 'rgb', 'white1', 'black1', 'white8', 'black8'");
#endif
    exit(1);
  }
}

std::string str_trim(const char *str,int len) 
{
  int start=strspn(str," \r\n\t");
  for (len--;len>=0;len--) {
    if (!strchr(" \r\n\t",str[len])) {
      break;
    }
  }
  len++;
  if (start>=len) {
    return std::string();
  }
  return std::string(str+start,len-start);
}

/* parse  key=value */
void parse_param(const char *str)
{
  const char *eq=strchr(str,'=');
  if (!eq) {
    fprintf(stderr, "WARNING: ignored ijsParam without '='");
    return;
  }
  params.push_back(make_pair(str_trim(str,eq-str),str_trim(eq+1,strlen(eq+1))));
}

/* parse  key1=value1,key2=value2,... */
void parse_paramlist(const char *str) 
{
  std::string tmp;
  const char *cur=str;
  while (*cur) {
    tmp.clear();
    for (;*cur;++cur) {
      if ( (*cur=='\\')&&(cur[1]) ) {
        ++cur;
        tmp.push_back(*cur);
      } else if(*cur==',') {
        ++cur;
        break;
      } else {
        tmp.push_back(*cur);
      }
    }
    parse_param(tmp.c_str());
  }
}

void parseOpts(int argc, char **argv)
{
  int num_options = 0;
  cups_option_t *options = 0;

  if (argc < 6 || argc > 7) {
    pdfError(-1,"%s job-id user title copies options [file]",
      argv[0]);
    exit(1);
  }

  assert(!ppd);
  ppd = ppdOpenFile(getenv("PPD"));
  ppdMarkDefaults(ppd);

  // handle  *ijsServer, *ijsManufacturer, *ijsModel, *ijsColorspace
  ppd_attr_t *attr; 
  if ((attr = ppdFindAttr(ppd,"ijsServer",0)) != 0) {
    ijsserver=attr->value;
  }
  if ((attr = ppdFindAttr(ppd,"ijsManufacturer",0)) != 0) {
    devManu=attr->value;
  }
  if ((attr = ppdFindAttr(ppd,"ijsModel",0)) != 0) {
    devModel=attr->value;
  }
  if ((attr = ppdFindAttr(ppd,"ijsColorspace",0)) != 0) {
    parse_colorspace(attr->value);
  }
  if ( (!ijsserver)||(!devManu)||(!devModel)||(colspace==NONE) ) {
    pdfError(-1,"ijsServer, ijsManufacturer, ijsModel and ijsColorspace must be specified in the PPD");
    exit(1);
  }
  
  options = NULL;

  num_options = cupsParseOptions(argv[5],0,&options);
//  cupsMarkOptions(ppd,num_options,options); // TODO? returns 1 on conflict
  // handle *ijsResolution, *ijsParam here
  char spec[PPD_MAX_NAME];
  for (int iA=0;iA<num_options;iA++) {
    snprintf(spec,PPD_MAX_NAME,"%s=%s",options[iA].name,options[iA].value);
    if ((attr = ppdFindAttr(ppd,"ijsResolution",spec)) != 0) {
      parse_resolution(attr->value);
    }
    if ((attr = ppdFindAttr(ppd,"ijsParams",spec)) != 0) {
      parse_paramlist(attr->value);
    }
    if (strcmp(options[iA].name,"ijsOutputFile")==0) {
      outputfile=strdup(options[iA].value);
    }
  }
  if (!resolution[0]) {
    pdfError(-1,"ijsResolution must be specified");
    exit(1);
  }
  cupsFreeOptions(num_options,options);
}

#if 0
void parsePDFTOPDFComment(FILE *fp)
{
  char buf[4096];
  int i;

  /* skip until PDF start header */
  while (fgets(buf,sizeof(buf),fp) != 0) {
    if (strncmp(buf,"%PDF",4) == 0) {
      break;
    }
  }
  for (i = 0;i < MAX_CHECK_COMMENT_LINES;i++) {
    if (fgets(buf,sizeof(buf),fp) == 0) break;
    if (strncmp(buf,"%%PDFTOPDFNumCopies",19) == 0) {
      char *p;

      p = strchr(buf+19,':');
      deviceCopies = atoi(p+1);
    } else if (strncmp(buf,"%%PDFTOPDFCollate",17) == 0) {
      char *p;

      p = strchr(buf+17,':');
      while (*p == ' ' || *p == '\t') p++;
      if (strncasecmp(p,"true",4) == 0) {
	deviceCollate = true;
      } else {
	deviceCollate = false;
      }
    }
  }
}
#endif

int main(int argc, char *argv[]) {
  PDFDoc *doc;
  SplashOutputDev *out;
  SplashColor paperColor;
  int i;
  int npages;
  IjsClientCtx *ctx=NULL;
  int job_id;
  enum SplashColorMode cmode;
  int rowpad;
  GBool reverseVideo;

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  setErrorCallback(::myErrorFun,NULL);
#else
  setErrorFunction(::myErrorFun);
#endif
  globalParams = new GlobalParams();
  parseOpts(argc, argv);

  if (argc == 6) {
    /* stdin */
    int fd;
    char name[BUFSIZ];
    char buf[BUFSIZ];
    int n;

    fd = cupsTempFd(name,sizeof(name));
    if (fd < 0) {
      pdfError(-1,"Can't create temporary file");
      exit(1);
    }

    /* copy stdin to the tmp file */
    while ((n = read(0,buf,BUFSIZ)) > 0) {
      if (write(fd,buf,n) != n) {
        pdfError(-1,"Can't copy stdin to temporary file");
        close(fd);
	exit(1);
      }
    }
    close(fd);
    doc = new PDFDoc(new GooString(name));
    /* remove name */
    unlink(name);
  } else {
    GooString *fileName = new GooString(argv[6]);
    /* argc == 7 filenmae is specified */
    FILE *fp;

    if ((fp = fopen(argv[6],"rb")) == 0) {
        pdfError(-1,"Can't open input file %s",argv[6]);
	exit(1);
    }
//    parsePDFTOPDFComment(fp); // TODO?
    fclose(fp);
    doc = new PDFDoc(fileName,NULL,NULL);
  }

  if (!doc->isOk()) {
    exitCode = 1;
    goto err1;
  }

  char tmp[100];
  tmp[99]=0;
  // ... OutputFD=stdout .. needs to be done before forking
  int outfd;
  outfd=dup(fileno(stdout)); 

#if 0
  /* fix NumCopies, Collate ccording to PDFTOPDFComments */
  header.NumCopies = deviceCopies;
  header.Collate = deviceCollate ? CUPS_TRUE : CUPS_FALSE;
  /* fixed other values that pdftopdf handles */
  header.MirrorPrint = CUPS_FALSE;
  header.Orientation = CUPS_ORIENT_0;
#endif

  job_id=atoi(argv[1]);
  ctx = ijs_invoke_server (ijsserver);
  ijs_client_open (ctx);
  ijs_client_begin_job (ctx,job_id);
  if (outputfile) {
    ijs_client_set_param(ctx,job_id,"OutputFile",outputfile,strlen(outputfile));
  } else {
    snprintf(tmp,99,"%d",outfd);
    ijs_client_set_param(ctx,job_id,"OutputFD",tmp,strlen(tmp));
    close(outfd);
  }
  ijs_client_set_param(ctx,job_id,"DeviceManufacturer",devManu,strlen(devManu));
  ijs_client_set_param(ctx,job_id,"DeviceModel",devModel,strlen(devModel));
  // TODO: get supported output-formats from ijs-server, overriding PPD

  /* set image's values */
  int numChan,bitsPerSample;
  const char *devName;
  reverseVideo = gFalse;
  switch (colspace) {
  case COL_RGB:
    numChan=3;
    bitsPerSample=8;
    cmode = splashModeRGB8;
    devName = "DeviceRGB";
    rowpad = 3;
    /* set paper color white */
    paperColor[0] = 255;
    paperColor[1] = 255;
    paperColor[2] = 255;
    break;
  case COL_BLACK1:
    reverseVideo = gTrue;
  case COL_WHITE1:
    numChan=1;
    bitsPerSample=1;
    cmode = splashModeMono1;
    devName = "DeviceGray";
    /* set paper color white */
    paperColor[0] = 255;
    rowpad = 1;
    break;
  case COL_BLACK8:
    reverseVideo = gTrue;
  case COL_WHITE8:
    numChan=1;
    bitsPerSample=8;
    cmode = splashModeMono8;
    devName = "DeviceGray";
    /* set paper color white */
    paperColor[0] = 255;
    rowpad = 1;
    break;
#ifdef SPLASH_CMYK
  case COL_CMYK:
    numChan=4;
    bitsPerSample=8;
    cmode = splashModeCMYK8;
    devName = "DeviceCMYK";
    /* set paper color white */
    paperColor[0] = 0;
    paperColor[1] = 0;
    paperColor[2] = 0;
    paperColor[3] = 0;
    rowpad = 4;
    break;
#endif
  default:
    pdfError(-1,"Specified ColorSpace is not supported");
    exit(1);
    break;
  }

  out = new SplashOutputDev(cmode,rowpad/* row padding */,
    reverseVideo,paperColor,gTrue
#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 30
    ,gFalse
#endif
    );
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 19
  out->startDoc(doc);
#else
  out->startDoc(doc->getXRef());
#endif

  snprintf(tmp,99,"%d",numChan);
  ijs_client_set_param(ctx,job_id,"NumChan",tmp,strlen(tmp));
  snprintf(tmp,99,"%d",bitsPerSample);
  ijs_client_set_param(ctx,job_id,"BitsPerSample",tmp,strlen(tmp));
  ijs_client_set_param(ctx,job_id,"ColorSpace",devName,strlen(devName));
  snprintf(tmp,99,"%dx%d",resolution[0],resolution[1]);
  ijs_client_set_param(ctx,job_id,"Dpi",tmp,strlen(tmp));

  { // set the custom ijs parameters
    const int plen=params.size();
    for (i=0;i<plen;i++) {
      ijs_client_set_param(ctx,job_id,params[i].first.c_str(),params[i].second.c_str(),params[i].second.size());
    }
  }

  npages = doc->getNumPages();
  for (i = 1;i <= npages;i++) {
    SplashBitmap *bitmap;
    unsigned int size;

    doc->displayPage(out,i,resolution[0],resolution[1],0,gFalse,gFalse,gFalse);
    bitmap = out->getBitmap();

    /* set page parameters */
    snprintf(tmp,99,"%d",bitmap->getWidth());
    ijs_client_set_param(ctx,job_id,"Width",tmp,strlen(tmp));
    snprintf(tmp,99,"%d",bitmap->getHeight());
    ijs_client_set_param(ctx,job_id,"Height",tmp,strlen(tmp));
    ijs_client_begin_page(ctx,job_id);

    /* write page image */
    size = bitmap->getRowSize()*bitmap->getHeight();
    int status=ijs_client_send_data_wait(ctx,job_id,(const char *)bitmap->getDataPtr(),size);
    if (status) {
        pdfError(-1,"Can't write page %d image: %d",i,status);
	exit(1);
    }

    status=ijs_client_end_page(ctx,job_id);
    if (status) {
        pdfError(-1,"Can't finish page %d: %d",i,status);
	exit(1);
    }
  }
  ijs_client_end_job (ctx, job_id);
  ijs_client_close (ctx);

  ijs_client_begin_cmd (ctx, IJS_CMD_EXIT);
  ijs_client_send_cmd_wait (ctx);

  delete out;
err1:
  delete doc;
  ppdClose(ppd);
  free(outputfile);

  // Check for memory leaks
  Object::memCheck(stderr);
  gMemReport(stderr);

  return exitCode;
}

/* replace memory allocation methods for memory check */
/* For compatibility with g++ >= 4.7 compilers _GLIBCXX_THROW
 *  should be used as a guard, otherwise use traditional definition */
#ifndef _GLIBCXX_THROW
#define _GLIBCXX_THROW throw
#endif

void * operator new(size_t size) _GLIBCXX_THROW (std::bad_alloc)
{
  return gmalloc(size);
}

void operator delete(void *p) throw ()
{
  gfree(p);
}

void * operator new[](size_t size) _GLIBCXX_THROW (std::bad_alloc)
{
  return gmalloc(size);
}

void operator delete[](void *p) throw ()
{
  gfree(p);
}

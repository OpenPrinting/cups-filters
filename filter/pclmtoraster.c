//
// Legacy CUPS filter wrapper for cfFilterPCLmToRaster() for cups-filters.
//
// Copyright © 2020-2022 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#include <cupsfilters/filter.h>
#include <ppd/ppd-filter.h>


//
// Local globals...
//

static int	JobCanceled = 0;// Set to 1 on SIGTERM


int main(int argc, char **argv)
{
  int ret;

  //
  // Fire up the cfFilterPCLmToRaster() filter function
  //

  cf_filter_out_format_t outformat = CF_FILTER_OUT_FORMAT_PWG_RASTER;
  char *t = getenv("FINAL_CONTENT_TYPE");
  if (t)
  {
    if (strcasestr(t, "urf"))
      outformat = CF_FILTER_OUT_FORMAT_APPLE_RASTER;
    else if (strcasestr(t, "cups-raster"))
      outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
  }

  ret = ppdFilterCUPSWrapper(argc, argv, cfFilterPCLmToRaster, &outformat,
			     &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: pclmtoraster filter function failed.\n");

  return (ret);
}

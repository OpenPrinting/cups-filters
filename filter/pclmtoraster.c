/**
 * This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief Decode PCLm to a Raster file
 * @file pclmtoraster.c
 * @author Vikrant Malik <vikrantmalik051@gmail.com> (c) 2020
 */

/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>

/*
 * Local globals...
 */

static int	JobCanceled = 0;/* Set to 1 on SIGTERM */

int main(int argc, char **argv)
{
  int ret;

 /*
  * Fire up the cfFilterPCLmToRaster() filter function
  */

  cf_filter_out_format_t outformat = CF_FILTER_OUT_FORMAT_PWG_RASTER;
  char *t = getenv("FINAL_CONTENT_TYPE");
  if (t) {
    if (strcasestr(t, "urf"))
      outformat = CF_FILTER_OUT_FORMAT_APPLE_RASTER;
    else if (strcasestr(t, "cups-raster"))
      outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
  }

  ret = cfFilterCUPSWrapper(argc, argv, cfFilterPCLmToRaster, &outformat, &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: pclmtoraster filter function failed.\n");

  return ret;
}

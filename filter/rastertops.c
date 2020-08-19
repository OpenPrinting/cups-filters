/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief Convert PWG Raster to a PostScript file
 * @file rastertops.c
 * @author Pranjal Bhor <bhor.pranjal@gmail.com> (C) 2016
 * @author Neil 'Superna' Armstrong <superna9999@gmail.com> (C) 2010
 * @author Tobias Hoffmann <smilingthax@gmail.com> (c) 2012
 * @author Till Kamppeter <till.kamppeter@gmail.com> (c) 2014
 */

/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>

/*
 * Local globals...
 */

static int		JobCanceled = 0;/* Set to 1 on SIGTERM */

/*
 * 'main()' - Main entry and processing of driver.
 */

int		   /* O - Exit status */
main(int  argc,	   /* I - Number of command-line arguments */
     char *argv[]) /* I - Command-line arguments */
{
  int           ret;

  /*
  * Fire up the rastertops() filter function
  */

  ret = filterCUPSWrapper(argc, argv, rastertops, NULL, &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: rastertops filter function failed.\n");

  return (ret);
}

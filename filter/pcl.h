//
// This file contains model number definitions for the unified
// PCL driver of cups-filters.
//
// Copyright 2007 by Apple Inc.
// Copyright 1997-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

// General PCL Support
#define PCL_PAPER_SIZE		0x1		// Use ESC&l#A
#define PCL_INKJET		0x2		// Use inkjet commands

// Raster Support
#define PCL_RASTER_END_COLOR	0x100		// Use ESC*rC
#define PCL_RASTER_CID		0x200		// Use ESC*v#W
#define PCL_RASTER_CRD		0x400		// Use ESC*g#W
#define PCL_RASTER_SIMPLE	0x800		// Use ESC*r#U
#define PCL_RASTER_RGB24	0x1000		// Use 24-bit RGB mode

// PJL Support
#define PCL_PJL			0x10000		// Use PJL Commands
#define PCL_PJL_PAPERWIDTH	0x20000		// Use PJL PAPERWIDTH/LENGTH
#define PCL_PJL_HPGL2		0x40000		// Enter HPGL2
#define PCL_PJL_PCL3GUI		0x80000		// Enter PCL3GUI
#define PCL_PJL_RESOLUTION	0x100000	// Use PJL SET RESOLUTION

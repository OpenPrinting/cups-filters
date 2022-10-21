//
// This file contains model number definitions for the unified
// ESC/P driver of cups-filters.
//
// Copyright 2007 by Apple Inc.
// Copyright 1997-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

// General ESC/P Support
#define ESCP_DOTMATRIX		0x1		// Dot matrix printer?
#define ESCP_MICROWEAVE		0x2		// Use microweave command?
#define ESCP_STAGGER		0x4		// Are color jets staggered?
#define ESCP_ESCK		0x8		// Use print mode command?
#define ESCP_EXT_UNITS		0x10		// Use extended unit commands?
#define ESCP_EXT_MARGINS	0x20		// Use extended margin command
#define ESCP_USB		0x40		// Send USB packet mode escape?
#define ESCP_PAGE_SIZE		0x80		// Use page size command
#define ESCP_RASTER_ESCI	0x100		// Use ESC i graphics command

// Remote mode support
#define ESCP_REMOTE		0x1000		// Use remote mode commands?

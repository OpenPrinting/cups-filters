#include <config.h>
#include <stdio.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif
#include "splash/Splash.h"
#include "splash/SplashMath.h"
#include "OPVPSplashClip.h"
#include "OPVPSplashXPath.h"
#include "OPVPWrapper.h"
#include "OPVPSplash.h"

void OPVPSplashXPath::strokeNarrow(OPVPSplash *splash, OPVPSplashState *state)
{
  SplashXPathSeg *seg;
  int x0, x1, x2, x3, y0, y1, x, y, t;
  SplashCoord dx, dy, dxdy;
  SplashClipResult clipRes;
  int i;

  for (i = 0, seg = segs; i < length; ++i, ++seg) {

    x0 = splashFloor(seg->x0);
    x1 = splashFloor(seg->x1);
    y0 = splashFloor(seg->y0);
    y1 = splashFloor(seg->y1);

    // horizontal segment
    if (y0 == y1) {
      if (x0 > x1) {
	t = x0; x0 = x1; x1 = t;
      }
      if ((clipRes = state->clip->testSpan(x0, x1, y0))
	  != splashClipAllOutside) {
	splash->drawSpan(x0, x1, y0, clipRes == splashClipAllInside);
      }

    // segment with |dx| > |dy|
    } else if (splashAbs(seg->dxdy) > 1) {
      dx = seg->x1 - seg->x0;
      dy = seg->y1 - seg->y0;
      dxdy = seg->dxdy;
      if (y0 > y1) {
	t = y0; y0 = y1; y1 = t;
	t = x0; x0 = x1; x1 = t;
	dx = -dx;
	dy = -dy;
      }
      if ((clipRes = state->clip->testRect(x0 <= x1 ? x0 : x1, y0,
					   x0 <= x1 ? x1 : x0, y1))
	  != splashClipAllOutside) {
	if (dx > 0) {
	  x2 = x0;
	  for (y = y0; y < y1; ++y) {
	    x3 = splashFloor(seg->x0 + (y + 1 - seg->y0) * dxdy);
	    splash->drawSpan(x2, x3 - 1, y, clipRes == splashClipAllInside);
	    x2 = x3;
	  }
	  splash->drawSpan(x2, x1, y, clipRes == splashClipAllInside);
	} else {
	  x2 = x0;
	  for (y = y0; y < y1; ++y) {
	    x3 = splashFloor(seg->x0 + (y + 1 - seg->y0) * dxdy);
	    splash->drawSpan(x3 + 1, x2, y, clipRes == splashClipAllInside);
	    x2 = x3;
	  }
	  splash->drawSpan(x1, x2, y, clipRes == splashClipAllInside);
	}
      }

    // segment with |dy| > |dx|
    } else {
      dxdy = seg->dxdy;
      if (y0 > y1) {
	t = y0; y0 = y1; y1 = t;
      }
      if ((clipRes = state->clip->testRect(x0 <= x1 ? x0 : x1, y0,
					   x0 <= x1 ? x1 : x0, y1))
	  != splashClipAllOutside) {
	for (y = y0; y <= y1; ++y) {
	  x = splashFloor(seg->x0 + (y - seg->y0) * dxdy);
	  splash->drawPixel(x, y, clipRes == splashClipAllInside);
	}
      }
    }
  }
}


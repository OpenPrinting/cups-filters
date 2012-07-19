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

#if POPPLER_VERSION_MAJOR <= 0 && POPPLER_VERSION_MINOR < 19
OPVPSplashXPath *OPVPSplashXPath::makeDashedPath(OPVPSplashState *state)
{
  OPVPSplashXPath *dPath;
  GBool lineDashStartOn, lineDashOn;
  GBool atSegStart, atSegEnd, atDashStart, atDashEnd;
  int lineDashStartIdx, lineDashIdx, subpathStart;
  SplashCoord lineDashTotal, lineDashStartPhase, lineDashDist;
  int segIdx;
  SplashXPathSeg *seg;
  SplashCoord sx0, sy0, sx1, sy1, ax0, ay0, ax1, ay1, dist;
  int i;

  dPath = new OPVPSplashXPath();

  lineDashTotal = 0;
  for (i = 0; i < state->lineDashLength; ++i) {
    lineDashTotal += state->lineDash[i];
  }
  lineDashStartPhase = state->lineDashPhase;
  i = splashFloor(lineDashStartPhase / lineDashTotal);
  lineDashStartPhase -= i * lineDashTotal;
  lineDashStartOn = gTrue;
  lineDashStartIdx = 0;
  while (lineDashStartPhase >= state->lineDash[lineDashStartIdx]) {
    lineDashStartOn = !lineDashStartOn;
    lineDashStartPhase -= state->lineDash[lineDashStartIdx];
    ++lineDashStartIdx;
  }

  segIdx = 0;
  seg = segs;
  sx0 = seg->x0;
  sy0 = seg->y0;
  sx1 = seg->x1;
  sy1 = seg->y1;
  dist = splashDist(sx0, sy0, sx1, sy1);
  lineDashOn = lineDashStartOn;
  lineDashIdx = lineDashStartIdx;
  lineDashDist = state->lineDash[lineDashIdx] - lineDashStartPhase;
  atSegStart = gTrue;
  atDashStart = gTrue;
  subpathStart = dPath->length;

  while (segIdx < length) {

    ax0 = sx0;
    ay0 = sy0;
    if (dist <= lineDashDist) {
      ax1 = sx1;
      ay1 = sy1;
      lineDashDist -= dist;
      dist = 0;
      atSegEnd = gTrue;
      atDashEnd = lineDashDist == 0 || (seg->flags & splashXPathLast);
    } else {
      ax1 = sx0 + (lineDashDist / dist) * (sx1 - sx0);
      ay1 = sy0 + (lineDashDist / dist) * (sy1 - sy0);
      sx0 = ax1;
      sy0 = ay1;
      dist -= lineDashDist;
      lineDashDist = 0;
      atSegEnd = gFalse;
      atDashEnd = gTrue;
    }

    if (lineDashOn) {
      dPath->addSegment(ax0, ay0, ax1, ay1,
			atDashStart, atDashEnd,
			atDashStart, atDashEnd);
      // end of closed subpath
      if (atSegEnd &&
	  (seg->flags & splashXPathLast) &&
	  !(seg->flags & splashXPathEnd1)) {
	dPath->segs[subpathStart].flags &= ~splashXPathEnd0;
	dPath->segs[dPath->length - 1].flags &= ~splashXPathEnd1;
      }
    }

    if (atDashEnd) {
      lineDashOn = !lineDashOn;
      if (++lineDashIdx == state->lineDashLength) {
	lineDashIdx = 0;
      }
      lineDashDist = state->lineDash[lineDashIdx];
      atDashStart = gTrue;
    } else {
      atDashStart = gFalse;
    }
    if (atSegEnd) {
      if (++segIdx < length) {
	++seg;
	sx0 = seg->x0;
	sy0 = seg->y0;
	sx1 = seg->x1;
	sy1 = seg->y1;
	dist = splashDist(sx0, sy0, sx1, sy1);
	if (seg->flags & splashXPathFirst) {
	  lineDashOn = lineDashStartOn;
	  lineDashIdx = lineDashStartIdx;
	  lineDashDist = state->lineDash[lineDashIdx] - lineDashStartPhase;
	  atDashStart = gTrue;
	  subpathStart = dPath->length;
	}
      }
      atSegStart = gTrue;
    } else {
      atSegStart = gFalse;
    }
  }

  return dPath;
}

void OPVPSplashXPath::strokeWide(OPVPSplash *splash, OPVPSplashState *state)
{
  SplashXPathSeg *seg, *seg2;
  OPVPSplashPath *widePath;
  SplashCoord d, dx, dy, wdx, wdy, dxPrev, dyPrev, wdxPrev, wdyPrev;
  SplashCoord dotprod, miter;
  SplashCoord x0,y0,x1,y1,x2,y2,x3,y3;
  int i, j;

  dx = dy = wdx = wdy = 0; // make gcc happy
  dxPrev = dyPrev = wdxPrev = wdyPrev = 0; // make gcc happy

  for (i = 0, seg = segs; i < length; ++i, ++seg) {

    // save the deltas for the previous segment; if this is the first
    // segment on a subpath, compute the deltas for the last segment
    // on the subpath (which may be used to draw a line join)
    if (seg->flags & splashXPathFirst) {
      for (j = i + 1, seg2 = &segs[j];
          j < length; ++j, ++seg2) {
	if (seg2->flags & splashXPathLast) {
	  d = splashDist(seg2->x0, seg2->y0, seg2->x1, seg2->y1);
	  if (d == 0) {
	    //~ not clear what the behavior should be for joins with d==0
	    dxPrev = 0;
	    dyPrev = 1;
	  } else {
	    d = 1 / d;
	    dxPrev = d * (seg2->x1 - seg2->x0);
	    dyPrev = d * (seg2->y1 - seg2->y0);
	  }
	  wdxPrev = 0.5 * state->lineWidth * dxPrev;
	  wdyPrev = 0.5 * state->lineWidth * dyPrev;
	  break;
	}
      }
    } else {
      dxPrev = dx;
      dyPrev = dy;
      wdxPrev = wdx;
      wdyPrev = wdy;
    }

    // compute deltas for this line segment
    d = splashDist(seg->x0, seg->y0, seg->x1, seg->y1);
    if (d == 0) {
      // we need to draw end caps on zero-length lines
      //~ not clear what the behavior should be for splashLineCapButt with d==0
      dx = 0;
      dy = 1;
    } else {
      d = 1 / d;
      dx = d * (seg->x1 - seg->x0);
      dy = d * (seg->y1 - seg->y0);
    }
    wdx = 0.5 * state->lineWidth * dx;
    wdy = 0.5 * state->lineWidth * dy;

    // initialize the path (which will be filled)
    widePath = new OPVPSplashPath();
    widePath->moveTo(seg->x0 - wdy, seg->y0 + wdx);

    // draw the start cap
    if (seg->flags & splashXPathEnd0) {
      switch (state->lineCap) {
      case splashLineCapButt:
	widePath->lineTo(seg->x0 + wdy, seg->y0 - wdx);
	break;
      case splashLineCapRound:
	x0 = seg->x0 - wdy;
	y0 = seg->y0 + wdx;
	x3 = seg->x0 - wdx;
	y3 = seg->y0 - wdy;
	splash->arcToCurve(x0, y0, x3, y3,
	  seg->x0, seg->y0, &x1,&y1,&x2,&y2);
	widePath->curveTo(x2,y2,x1,y1,x3,y3);
	x0 = x3;
	y0 = y3;
	x3 = seg->x0 + wdy;
	y3 = seg->y0 - wdx;
	splash->arcToCurve(x0,y0,x3,y3,
	  seg->x0, seg->y0, &x1,&y1,&x2,&y2);
	widePath->curveTo(x2,y2,x1,y1,x3,y3);
	break;
      case splashLineCapProjecting:
	widePath->lineTo(seg->x0 - wdx - wdy, seg->y0 + wdx - wdy);
	widePath->lineTo(seg->x0 - wdx + wdy, seg->y0 - wdx - wdy);
	widePath->lineTo(seg->x0 + wdy, seg->y0 - wdx);
	break;
      }
    } else {
      widePath->lineTo(seg->x0 + wdy, seg->y0 - wdx);
    }

    // draw the left side of the segment
    widePath->lineTo(seg->x1 + wdy, seg->y1 - wdx);

    // draw the end cap
    if (seg->flags & splashXPathEnd1) {
      switch (state->lineCap) {
      case splashLineCapButt:
	widePath->lineTo(seg->x1 - wdy, seg->y1 + wdx);
	break;
      case splashLineCapRound:
	x0 = seg->x1 + wdy;
	y0 = seg->y1 - wdx;
	x3 = seg->x1 + wdx;
	y3 = seg->y1 + wdy;
	splash->arcToCurve(x0, y0, x3, y3,
	  seg->x1, seg->y1, &x1,&y1,&x2,&y2);
	widePath->curveTo(x2,y2,x1,y1,x3,y3);
	x0 = x3;
	y0 = y3;
	x3 = seg->x1 - wdy;
	y3 = seg->y1 + wdx;
	splash->arcToCurve(x0,y0,x3,y3,
	  seg->x1, seg->y1, &x1,&y1,&x2,&y2);
	widePath->curveTo(x2,y2,x1,y1,x3,y3);
	break;
      case splashLineCapProjecting:
	widePath->lineTo(seg->x1 + wdx + wdy, seg->y1 - wdx + wdy);
	widePath->lineTo(seg->x1 + wdx - wdy, seg->y1 + wdx + wdy);
	widePath->lineTo(seg->x1 - wdy, seg->y1 + wdx);
	break;
      }
    } else {
      widePath->lineTo(seg->x1 - wdy, seg->y1 + wdx);
    }

    // draw the right side of the segment
    widePath->lineTo(seg->x0 - wdy, seg->y0 + wdx);

    // fill the segment
    splash->fill(widePath, gTrue);
    delete widePath;

    // draw the line join
    if (!(seg->flags & splashXPathEnd0)) {
      widePath = NULL;
      switch (state->lineJoin) {
      case splashLineJoinMiter:
	dotprod = -(dx * dxPrev + dy * dyPrev);
	if (dotprod != 1) {
	  widePath = new OPVPSplashPath();
	  widePath->moveTo(seg->x0, seg->y0);
	  miter = 2 / (1 - dotprod);
	  if (splashSqrt(miter) <= state->miterLimit) {
	    miter = splashSqrt(miter - 1);
	    if (dy * dxPrev > dx * dyPrev) {
	      widePath->lineTo(seg->x0 + wdyPrev, seg->y0 - wdxPrev);
	      widePath->lineTo(seg->x0 + wdy - miter * wdx,
			       seg->y0 - wdx - miter * wdy);
	      widePath->lineTo(seg->x0 + wdy, seg->y0 - wdx);
	    } else {
	      widePath->lineTo(seg->x0 - wdyPrev, seg->y0 + wdxPrev);
	      widePath->lineTo(seg->x0 - wdy - miter * wdx,
			       seg->y0 + wdx - miter * wdy);
	      widePath->lineTo(seg->x0 - wdy, seg->y0 + wdx);
	    }
	  } else {
	    if (dy * dxPrev > dx * dyPrev) {
	      widePath->lineTo(seg->x0 + wdyPrev, seg->y0 - wdxPrev);
	      widePath->lineTo(seg->x0 + wdy, seg->y0 - wdx);
	    } else {
	      widePath->lineTo(seg->x0 - wdyPrev, seg->y0 + wdxPrev);
	      widePath->lineTo(seg->x0 - wdy, seg->y0 + wdx);
	    }
	  }
	}
	break;
      case splashLineJoinRound:
	widePath = new OPVPSplashPath();
	/* draw circle */
	widePath->moveTo(seg->x0 + wdy, seg->y0 - wdx);
	x0 = seg->x0 + wdy;
	y0 = seg->y0 - wdx;
	x3 = seg->x0 - wdx;
	y3 = seg->y0 - wdy;
	splash->arcToCurve(x0, y0, x3, y3,
	  seg->x0, seg->y0, &x1,&y1,&x2,&y2);
	widePath->curveTo(x1,y1,x2,y2,x3,y3);
	x0 = x3;
	y0 = y3;
	x3 = seg->x0 - wdy;
	y3 = seg->y0 + wdx;
	splash->arcToCurve(x0, y0, x3, y3,
	  seg->x0, seg->y0, &x1,&y1,&x2,&y2);
	widePath->curveTo(x1,y1,x2,y2,x3,y3);
	x0 = x3;
	y0 = y3;
	x3 = seg->x0 + wdx;
	y3 = seg->y0 + wdy;
	splash->arcToCurve(x0, y0, x3, y3,
	  seg->x0, seg->y0, &x1,&y1,&x2,&y2);
	widePath->curveTo(x1,y1,x2,y2,x3,y3);
	x0 = x3;
	y0 = y3;
	x3 = seg->x0 + wdy;
	y3 = seg->y0 - wdx;
	splash->arcToCurve(x0, y0, x3, y3,
	  seg->x0, seg->y0, &x1,&y1,&x2,&y2);
	widePath->curveTo(x1,y1,x2,y2,x3,y3);
	break;
      case splashLineJoinBevel:
	widePath = new OPVPSplashPath();
	widePath->moveTo(seg->x0, seg->y0);
	if (dy * dxPrev > dx * dyPrev) {
	  widePath->lineTo(seg->x0 + wdyPrev, seg->y0 - wdxPrev);
	  widePath->lineTo(seg->x0 + wdy, seg->y0 - wdx);
	} else {
	  widePath->lineTo(seg->x0 - wdyPrev, seg->y0 + wdxPrev);
	  widePath->lineTo(seg->x0 - wdy, seg->y0 + wdx);
	}
	break;
      }
      if (widePath) {
	splash->fill(widePath, gTrue);
	delete widePath;
      }
    }
  }
}
#endif

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


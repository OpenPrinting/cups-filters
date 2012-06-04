#include <config.h>
#include <stdio.h>
#include "splash/SplashMath.h"
#include "OPVPSplashPath.h"
#include "OPVPWrapper.h"
#include "OPRS.h"

void OPVPSplashPath::getBBox(int *xMinA, int *yMinA, int *xMaxA,
    int *yMaxA)
{
  int i;
  SplashCoord xMin, yMin, xMax, yMax;

  if (length <= 0) {
    /* return far away point */
    *xMinA = *yMinA = *xMaxA = *yMaxA = 0xC0000000;
    return;
  }
  xMin = xMax = pts[0].x;
  yMin = yMax = pts[0].y;
  for (i = 1;i < length;i++) {
    if (pts[i].x > xMax) {
      xMax = pts[i].x;
    } else if (pts[i].x < xMin) {
      xMin = pts[i].x;
    }
    if (pts[i].y > yMax) {
      yMax = pts[i].y;
    } else if (pts[i].y < yMin) {
      yMin = pts[i].y;
    }
  }
  *xMinA = splashRound(xMin);
  *xMaxA = splashRound(xMax);
  *yMinA = splashRound(yMin);
  *yMaxA = splashRound(yMax);
}

GBool OPVPSplashPath::isRectanglePath(
  SplashCoord *xMin, SplashCoord *yMin, SplashCoord *xMax, SplashCoord *yMax)
{
  if (length != 5
      || pts[0].x != pts[4].x
      || pts[0].y != pts[4].y
      || flags[0] != (splashPathFirst | splashPathClosed)
      || flags[1] != 0
      || flags[2] != 0
      || flags[3] != 0
      || flags[4] != (splashPathLast | splashPathClosed)) {
    return gFalse;
  }
  if (splashRound(pts[0].x) == splashRound(pts[1].x)) {
    if (splashRound(pts[1].y) != splashRound(pts[2].y)
      || splashRound(pts[2].x) != splashRound(pts[3].x)
      || splashRound(pts[3].y) != splashRound(pts[4].y)) {
      return gFalse;
    }
  } else if (splashRound(pts[0].y) == splashRound(pts[1].y)) {
    if (splashRound(pts[1].x) != splashRound(pts[2].x)
      || splashRound(pts[2].y) != splashRound(pts[3].y)
      || splashRound(pts[3].x) != splashRound(pts[4].x)) {
      return gFalse;
    }
  } else {
    return gFalse;
  }
  *xMin = pts[0].x;
  *yMin = pts[0].y;
  *xMax = pts[2].x;
  *yMax = pts[2].y;
  if (*xMin > *xMax) {
    SplashCoord t = *xMin;

    *xMin = *xMax;
    *xMax = t;
  }
  if (*yMin > *yMax) {
    SplashCoord t = *yMin;

    *yMin = *yMax;
    *yMax = t;
  }
  return gTrue;
}

SplashError OPVPSplashPath::makePath(OPVPWrapper *opvp)
{
  int i,j;
  opvp_fix_t x,y;

  if (opvp->NewPath() < 0) {
    OPRS::error("NewPath error\n");
    return splashErrOPVP;
  }
  for (i = 0;i < length;i = j) {
    int curve = 0; 
    int n;
    opvp_point_t *points;
    int k;

    if ((flags[i] & splashPathFirst) != 0) {
      /* first point of a subpath */
      if ((flags[i] & splashPathLast) == 0
          || (flags[i] & splashPathClosed) != 0) {
	OPVP_F2FIX((pts[i].x),(x));
	OPVP_F2FIX((pts[i].y),(y));
	if (opvp->SetCurrentPoint(x,y) < 0) {
	  OPRS::error("SetCurrentPoint error\n");
	  return splashErrOPVP;
	}
      }
      j = i+1;
      continue;
    }
    if (i+2 < length && flags[i] == splashPathCurve) {
      /* curve */
      curve = 1;
      for (j = i;j+2 < length
	 && flags[j] == splashPathCurve;j += 3);
    } else {
      curve = 0;
      for (j = i;j < length
	 && (flags[j] & splashPathCurve) == 0
	 && (flags[j] & splashPathFirst) == 0;j++);
    }

    n = j-i;
    points = new opvp_point_t[n];
    /* copy points */
    for (k = i; k < j;k++) {
      OPVP_F2FIX((pts[k].x),(points[k-i].x));
      OPVP_F2FIX((pts[k].y),(points[k-i].y));
    }

    if (curve) {
      /* curve */
      if (opvp->BezierPath(n,points) < 0) {
	OPRS::error("BezierPath error\n");
	return splashErrOPVP;
      }
    } else {
      /* line */
      GBool closed = (flags[j-1] & splashPathClosed) != 0;

      if (closed) {
	if (opvp->LinePath(OPVP_PATHCLOSE,
	      n,points) < 0) {
	  OPRS::error("LinePath error\n");
	  return splashErrOPVP;
	}
      } else {
	if (opvp->LinePath(OPVP_PATHOPEN,
	      n,points) < 0) {
	  OPRS::error("LinePath error\n");
	  return splashErrOPVP;
	}
      }
    }
    delete[] points;
  }
  if (opvp->EndPath() < 0) {
    OPRS::error("EndPath error\n");
    return splashErrOPVP;
  }
  return splashOk;
}

void OPVPSplashPath::closeAllSubPath()
{
  int i;
  int f = 0;

  for (i = 0;i < length;i++) {
    if ((flags[i] & splashPathFirst) != 0) {
      f = i;
    }
    if ((flags[i] & splashPathLast) != 0) {
      if (pts[f].x == pts[i].x
        && pts[f].y == pts[i].y) {
	flags[f] |= splashPathClosed;
	flags[i] |= splashPathClosed;
      }
    }
  }
}

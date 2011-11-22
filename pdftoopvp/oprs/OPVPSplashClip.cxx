#include <config.h>
#include "splash/SplashXPathScanner.h"
#include "OPVPSplashClip.h"

void OPVPSplashClip::getBBox(int *xMinA, int *yMinA, int *xMaxA, int *yMaxA)
{
  int i;
  int cxMin = splashRound(xMin), cyMin = splashRound(yMin);
  int cxMax = splashRound(xMax), cyMax = splashRound(yMax);
  int txMin, tyMin, txMax, tyMax;

  for (i = 0; i < length; ++i) {
    scanners[i]->getBBox(&txMin,&tyMin,&txMax,&tyMax);
    if (txMin > cxMin) cxMin = txMin;
    if (tyMin > cyMin) cyMin = tyMin;
    if (txMax < cxMax) cxMax = txMax;
    if (tyMax < cyMax) cyMax = tyMax;
  }
  *xMinA = cxMin;
  *yMinA = cyMin;
  *xMaxA = cxMax;
  *yMaxA = cyMax;
}

OPVPSplashPath *OPVPSplashClip::makePath()
{
  int i,j;
  int y, x0, x1;
  int txMin, tyMin, txMax, tyMax;
  int tsxMin, tsyMin, tsxMax, tsyMax;
  Guchar *cbuf,*tbuf;
  int blen;
  OPVPSplashPath *p = new OPVPSplashPath();

  getBBox(&txMin,&tyMin,&txMax,&tyMax);
  if (txMin > txMax || tyMin > tyMax) return p;
  blen = txMax-txMin+1;
  cbuf = new Guchar[blen];
  tbuf = new Guchar[blen];
  
  /* dummy call to clear state */
  scanners[0]->getBBox(&tsxMin,&tsyMin,&tsxMax,&tsyMax);
  scanners[0]->getNextSpan(tsyMin-2,&x0,&x1);

  for (y = tyMin;y <= tyMax;y++) {
    /* clear buffer */
    for (i = 0;i < blen;i++) {
      cbuf[i] = 0;
    }
    while (scanners[0]->getNextSpan(y,&x0,&x1)) {
      if (x0 < txMin) x0 = txMin;
      if (x1 > txMax) x1 = txMax;
      for (i = x0;i < x1;i++) {
	cbuf[i-txMin] = 1;
      }
    }
    for (j = 1; j < length; ++j) {
      /* clear buffer */
      for (i = 0;i < blen;i++) {
	tbuf[i] = 0;
      }
      while (scanners[j]->getNextSpan(y,&x0,&x1)) {
	if (x0 < txMin) x0 = txMin;
	if (x1 > txMax) x1 = txMax;
	for (i = x0;i < x1;i++) {
	  tbuf[i-txMin] = 1;
	}
      }
      /* and buffer */
      for (i = 0;i < blen;i++) {
	cbuf[i] &= tbuf[i];
      }
    }
    /* scan buffer and add path */
    for (i = 0;i < blen;i = j) {
      if (cbuf[i] != 0) {
	p->moveTo(i+txMin,y);
	for (j = i+1;j < blen && cbuf[j] != 0;j++);
	p->lineTo(j-1+txMin,y);
	p->lineTo(j-1+txMin,y+1);
	p->lineTo(i+txMin,y+1);
	p->close();
      } else {
	j = i+1;
      }
    }
  }
  delete[] cbuf;
  delete[] tbuf;
  return p;
}

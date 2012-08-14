/*

Copyright (c) 2006-2007, BBR Inc.  All rights reserved.

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
 P2PMatrix.h
 pdftopdf matrix
*/
#ifndef _P2PMATRIX_H_
#define _P2PMATRIX_H_

#include <math.h>
#include "P2POutputStream.h"

class P2PMatrix {
public:
  P2PMatrix() {
    mat[0] = 1;mat[1] = 0;mat[2] = 0;mat[3] = 1;mat[4] = 0; mat[5] = 0;
  }

  P2PMatrix(double m0, double m1, double m2,
    double m3, double m4, double m5) {
    mat[0] = m0;mat[1] = m1;mat[2] = m2;
    mat[3] = m3;mat[4] = m4;mat[5] = m5;
  }

  void output(P2POutputStream *str) {
    str->printf("%f %f %f %f %f %f",
      mat[0],mat[1],mat[2],mat[3],mat[4],mat[5]);
  }

  void rotate(double x) {
    double m0,m1,m2,m3,m4,m5;

    x = (x/180) * M_PI;

    m0 = mat[0]*cos(x)-mat[1]*sin(x);
    m1 = mat[0]*sin(x)+mat[1]*cos(x);
    m2 = mat[2]*cos(x)-mat[3]*sin(x);
    m3 = mat[2]*sin(x)+mat[3]*cos(x);
    m4 = mat[4]*cos(x)-mat[5]*sin(x);
    m5 = mat[4]*sin(x)+mat[5]*cos(x);

    mat[0] = m0;mat[1] = m1;mat[2] = m2;
    mat[3] = m3;mat[4] = m4;mat[5] = m5;
  }

  void rotate(int x) {
    /* special case */
    double m0,m1,m2,m3,m4,m5;

    if (x < 0 || (x % 90) != 0 || x >= 360) {
      rotate(static_cast<double>(x));
      return;
    }
    switch (x / 90) {
    case 1:
      m0 = -mat[1];
      m1 = mat[0];
      m2 = -mat[3];
      m3 = mat[2];
      m4 = -mat[5];
      m5 = mat[4];
      break;
    case 2:
      m0 = -mat[0];
      m1 = -mat[1];
      m2 = -mat[2];
      m3 = -mat[3];
      m4 = -mat[4];
      m5 = -mat[5];
      break;
    case 3:
      m0 = mat[1];
      m1 = -mat[0];
      m2 = mat[3];
      m3 = -mat[2];
      m4 = mat[5];
      m5 = -mat[4];
      break;
    case 0:
    default:
      m0 = mat[0];
      m1 = mat[1];
      m2 = mat[2];
      m3 = mat[3];
      m4 = mat[4];
      m5 = mat[5];
      break;
    }
    mat[0] = m0;mat[1] = m1;mat[2] = m2;
    mat[3] = m3;mat[4] = m4;mat[5] = m5;
  }

  void move(double x, double y) {
    mat[4] += x;
    mat[5] += y;
  }

  void scale(double x, double y) {
    mat[0] *= x;
    mat[1] *= y;
    mat[2] *= x;
    mat[3] *= y;
    mat[4] *= x;
    mat[5] *= y;
  }

  void scale(double x) {
    scale(x,x);
  }

  void trans(P2PMatrix *s) {
    double m0,m1,m2,m3,m4,m5;

    m0 = mat[0]*s->mat[0]+mat[1]*s->mat[2];
    m1 = mat[0]*s->mat[1]+mat[1]*s->mat[3];
    m2 = mat[2]*s->mat[0]+mat[3]*s->mat[2];
    m3 = mat[2]*s->mat[1]+mat[3]*s->mat[3];
    m4 = mat[4]*s->mat[0]+mat[5]*s->mat[2]+s->mat[4];
    m5 = mat[4]*s->mat[1]+mat[5]*s->mat[3]+s->mat[5];

    mat[0] = m0;mat[1] = m1;mat[2] = m2;
    mat[3] = m3;mat[4] = m4;mat[5] = m5;
  }

  void apply(double x, double y, double *rx, double *ry) {
    *rx = x*mat[0]+y*mat[2]+mat[4];
    *ry = x*mat[1]+y*mat[3]+mat[5];
  }

  double mat[6];
};

#endif

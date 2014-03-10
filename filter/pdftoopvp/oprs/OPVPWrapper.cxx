/*
  OPVPWrapper.cc
*/


#include <config.h>
#include <stdio.h>
#include "OPRS.h"
#include "OPVPWrapper.h"
#include "OPVPWrapper_0_2.h"
#include <string.h>
#include <dlfcn.h>

OPVPWrapper::OPVPWrapper(void *opvpHandleA, opvp_int_t *opvpErrorNoA,
  opvp_api_procs_t *procsA, opvp_dc_t printerContextA)
{
    procs = procsA;
    opvpHandle = opvpHandleA;
    opvpErrorNo = opvpErrorNoA;
    printerContext = printerContextA;
    version[0] = 1;
    version[1] = 0;
    supportClosePrinter = (procs->opvpClosePrinter != 0);
    supportStartJob = (procs->opvpStartJob != 0);
    supportEndJob = (procs->opvpEndJob != 0);
    supportAbortJob = (procs->opvpAbortJob != 0);
    supportStartDoc = (procs->opvpStartDoc != 0);
    supportEndDoc = (procs->opvpEndDoc != 0);
    supportStartPage = (procs->opvpStartPage != 0);
    supportEndPage = (procs->opvpEndPage != 0);
    supportResetCTM = (procs->opvpResetCTM != 0);
    supportSetCTM = (procs->opvpSetCTM != 0);
    supportGetCTM = (procs->opvpGetCTM != 0);
    supportInitGS = (procs->opvpInitGS != 0);
    supportSaveGS = (procs->opvpSaveGS != 0);
    supportRestoreGS = (procs->opvpRestoreGS != 0);
    supportQueryColorSpace = (procs->opvpQueryColorSpace != 0);
    supportSetColorSpace = (procs->opvpSetColorSpace != 0);
    supportGetColorSpace = (procs->opvpGetColorSpace != 0);
    supportSetFillMode = (procs->opvpSetFillMode != 0);
    supportGetFillMode = (procs->opvpGetFillMode != 0);
    supportSetAlphaConstant = (procs->opvpSetAlphaConstant != 0);
    supportGetAlphaConstant = (procs->opvpGetAlphaConstant != 0);
    supportSetLineWidth = (procs->opvpSetLineWidth != 0);
    supportGetLineWidth = (procs->opvpGetLineWidth != 0);
    supportSetLineDash = (procs->opvpSetLineDash != 0);
    supportGetLineDash = (procs->opvpGetLineDash != 0);
    supportSetLineDashOffset = (procs->opvpSetLineDashOffset != 0);
    supportGetLineDashOffset = (procs->opvpGetLineDashOffset != 0);
    supportSetLineStyle = (procs->opvpSetLineStyle != 0);
    supportGetLineStyle = (procs->opvpGetLineStyle != 0);
    supportSetLineCap = (procs->opvpSetLineCap != 0);
    supportGetLineCap = (procs->opvpGetLineCap != 0);
    supportSetLineJoin = (procs->opvpSetLineJoin != 0);
    supportGetLineJoin = (procs->opvpGetLineJoin != 0);
    supportSetMiterLimit = (procs->opvpSetMiterLimit != 0);
    supportGetMiterLimit = (procs->opvpGetMiterLimit != 0);
    supportSetPaintMode = (procs->opvpSetPaintMode != 0);
    supportGetPaintMode = (procs->opvpGetPaintMode != 0);
    supportSetStrokeColor = (procs->opvpSetStrokeColor != 0);
    supportSetFillColor = (procs->opvpSetFillColor != 0);
    supportSetBgColor = (procs->opvpSetBgColor != 0);
    supportNewPath = (procs->opvpNewPath != 0);
    supportEndPath = (procs->opvpEndPath != 0);
    supportStrokePath = (procs->opvpStrokePath != 0);
    supportFillPath = (procs->opvpFillPath != 0);
    supportStrokeFillPath = (procs->opvpStrokeFillPath != 0);
    supportSetClipPath = (procs->opvpSetClipPath != 0);
    supportSetCurrentPoint = (procs->opvpSetCurrentPoint != 0);
    supportLinePath = (procs->opvpLinePath != 0);
    supportPolygonPath = (procs->opvpPolygonPath != 0);
    supportRectanglePath = (procs->opvpRectanglePath != 0);
    supportRoundRectanglePath = (procs->opvpRoundRectanglePath != 0);
    supportBezierPath = (procs->opvpBezierPath != 0);
    supportArcPath = (procs->opvpArcPath != 0);
    supportDrawImage = (procs->opvpDrawImage != 0);
    supportStartDrawImage = (procs->opvpStartDrawImage != 0);
    supportTransferDrawImage = (procs->opvpTransferDrawImage != 0);
    supportEndDrawImage = (procs->opvpEndDrawImage != 0);
    supportStartScanline = (procs->opvpStartScanline != 0);
    supportScanline = (procs->opvpScanline != 0);
    supportEndScanline = (procs->opvpEndScanline != 0);
    supportStartRaster = (procs->opvpStartRaster != 0);
    supportTransferRasterData = (procs->opvpTransferRasterData != 0);
    supportSkipRaster = (procs->opvpSkipRaster != 0);
    supportEndRaster = (procs->opvpEndRaster != 0);
    supportStartStream = (procs->opvpStartStream != 0);
    supportTransferStreamData = (procs->opvpTransferStreamData != 0);
    supportEndStream = (procs->opvpEndStream != 0);
    supportQueryDeviceCapability = (procs->opvpQueryDeviceCapability != 0);
    supportQueryDeviceInfo = (procs->opvpQueryDeviceInfo != 0);
    supportResetClipPath = (procs->opvpResetClipPath != 0);
}

OPVPWrapper::~OPVPWrapper()
{
    unloadDriver(opvpHandle);
    opvpHandle = 0;
}

opvp_result_t OPVPWrapper::ClosePrinter()
{
    if (!supportClosePrinter) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpClosePrinter(printerContext);
}

opvp_result_t OPVPWrapper::StartJob(const opvp_char_t *jobInfo)
{
    if (!supportStartJob) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartJob(printerContext,jobInfo);
}

opvp_result_t OPVPWrapper::EndJob()
{
    if (!supportEndJob) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndJob(printerContext);
}

opvp_result_t OPVPWrapper::AbortJob()
{
    if (!supportAbortJob) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpAbortJob(printerContext);
}

opvp_result_t OPVPWrapper::StartDoc(const opvp_char_t *docInfo)
{
    if (!supportStartDoc) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartDoc(printerContext,docInfo);
}

opvp_result_t OPVPWrapper::EndDoc()
{
    if (!supportEndDoc) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndDoc(printerContext);
}

opvp_result_t OPVPWrapper::StartPage(const opvp_char_t *pageInfo)
{
    if (!supportStartPage) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartPage(printerContext,pageInfo);
}

opvp_result_t OPVPWrapper::EndPage()
{
    if (!supportEndPage) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndPage(printerContext);
}

opvp_result_t OPVPWrapper::QueryDeviceCapability(opvp_flag_t queryflag,
  opvp_int_t *buflen, opvp_byte_t *infoBuf)
{
    if (!supportQueryDeviceCapability) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpQueryDeviceCapability(printerContext,queryflag,
      buflen,infoBuf);
}

opvp_result_t OPVPWrapper::QueryDeviceInfo(opvp_flag_t queryflag,
  opvp_int_t *buflen, opvp_byte_t *infoBuf)
{
    if (!supportQueryDeviceInfo) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpQueryDeviceInfo(printerContext,queryflag,
      buflen,infoBuf);
}

opvp_result_t OPVPWrapper::ResetCTM()
{
    if (!supportResetCTM) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpResetCTM(printerContext);
}

opvp_result_t OPVPWrapper::SetCTM(const opvp_ctm_t *pCTM)
{
    if (!supportSetCTM) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetCTM(printerContext,pCTM);
}

opvp_result_t OPVPWrapper::GetCTM(opvp_ctm_t *pCTM)
{
    if (!supportGetCTM) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetCTM(printerContext,pCTM);
}

opvp_result_t OPVPWrapper::InitGS()
{
    if (!supportInitGS) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpInitGS(printerContext);
}

opvp_result_t OPVPWrapper::SaveGS()
{
    if (!supportSaveGS) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSaveGS(printerContext);
}

opvp_result_t OPVPWrapper::RestoreGS()
{
    if (!supportRestoreGS) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpRestoreGS(printerContext);
}

opvp_result_t OPVPWrapper::QueryColorSpace(opvp_int_t *pnum,
  opvp_cspace_t *pcspace)
{
    if (!supportQueryColorSpace) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpQueryColorSpace(printerContext,pnum,pcspace);
}

opvp_result_t OPVPWrapper::SetColorSpace(opvp_cspace_t cspace)
{
    if (!supportSetColorSpace) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetColorSpace(printerContext,cspace);
}

opvp_result_t OPVPWrapper::GetColorSpace(opvp_cspace_t *pcspace)
{
    if (!supportGetColorSpace) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetColorSpace(printerContext,pcspace);
}

opvp_result_t OPVPWrapper::SetFillMode(opvp_fillmode_t fillmode)
{
    if (!supportSetFillMode) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetFillMode(printerContext,fillmode);
}

opvp_result_t OPVPWrapper::GetFillMode(opvp_fillmode_t *pfillmode)
{
    if (!supportGetFillMode) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetFillMode(printerContext,pfillmode);
}

opvp_result_t OPVPWrapper::SetAlphaConstant(opvp_float_t alpha)
{
    if (!supportSetAlphaConstant) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetAlphaConstant(printerContext,alpha);
}

opvp_result_t OPVPWrapper::GetAlphaConstant(opvp_float_t *palpha)
{
    if (!supportGetAlphaConstant) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetAlphaConstant(printerContext,palpha);
}

opvp_result_t OPVPWrapper::SetLineWidth(opvp_fix_t width)
{
    if (!supportSetLineWidth) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetLineWidth(printerContext,width);
}

opvp_result_t OPVPWrapper::GetLineWidth(opvp_fix_t *pwidth)
{
    if (!supportGetLineWidth) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetLineWidth(printerContext,pwidth);
}

opvp_result_t OPVPWrapper::SetLineDash(opvp_int_t num,
  const opvp_fix_t *pdash)
{
    if (!supportSetLineDash) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetLineDash(printerContext,num,pdash);
}

opvp_result_t OPVPWrapper::GetLineDash(opvp_int_t *pnum, opvp_fix_t *pdash)
{
    if (!supportGetLineDash) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetLineDash(printerContext,pnum,pdash);
}

opvp_result_t OPVPWrapper::SetLineDashOffset(opvp_fix_t offset)
{
    if (!supportSetLineDashOffset) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetLineDashOffset(printerContext,offset);
}

opvp_result_t OPVPWrapper::GetLineDashOffset(opvp_fix_t *poffset)
{
    if (!supportGetLineDashOffset) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetLineDashOffset(printerContext,poffset);
}

opvp_result_t OPVPWrapper::SetLineStyle(opvp_linestyle_t linestyle)
{
    if (!supportSetLineStyle) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetLineStyle(printerContext,linestyle);
}

opvp_result_t OPVPWrapper::GetLineStyle(opvp_linestyle_t *plinestyle)
{
    if (!supportGetLineStyle) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetLineStyle(printerContext,plinestyle);
}

opvp_result_t OPVPWrapper::SetLineCap(opvp_linecap_t linecap)
{
    if (!supportSetLineCap) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetLineCap(printerContext,linecap);
}

opvp_result_t OPVPWrapper::GetLineCap(opvp_linecap_t *plinecap)
{
    if (!supportGetLineCap) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetLineCap(printerContext,plinecap);
}

opvp_result_t OPVPWrapper::SetLineJoin(opvp_linejoin_t linejoin)
{
    if (!supportSetLineJoin) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetLineJoin(printerContext,linejoin);
}

opvp_result_t OPVPWrapper::GetLineJoin(opvp_linejoin_t *plinejoin)
{
    if (!supportGetLineJoin) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetLineJoin(printerContext,plinejoin);
}

opvp_result_t OPVPWrapper::SetMiterLimit(opvp_fix_t miterlimit)
{
    if (!supportSetMiterLimit) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetMiterLimit(printerContext,miterlimit);
}

opvp_result_t OPVPWrapper::GetMiterLimit(opvp_fix_t *pmiterlimit)
{
    if (!supportGetMiterLimit) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetMiterLimit(printerContext,pmiterlimit);
}

opvp_result_t OPVPWrapper::SetPaintMode(opvp_paintmode_t paintmode)
{
    if (!supportSetPaintMode) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetPaintMode(printerContext,paintmode);
}

opvp_result_t OPVPWrapper::GetPaintMode(opvp_paintmode_t *ppaintmode)
{
    if (!supportGetPaintMode) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpGetPaintMode(printerContext,ppaintmode);
}

opvp_result_t OPVPWrapper::SetStrokeColor(const opvp_brush_t *brush)
{
    if (!supportSetStrokeColor) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetStrokeColor(printerContext,brush);
}

opvp_result_t OPVPWrapper::SetFillColor(const opvp_brush_t *brush)
{
    if (!supportSetFillColor) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetFillColor(printerContext,brush);
}

opvp_result_t OPVPWrapper::SetBgColor(const opvp_brush_t *brush)
{
    if (!supportSetBgColor) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetBgColor(printerContext,brush);
}

opvp_result_t OPVPWrapper::NewPath()
{
    if (!supportNewPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpNewPath(printerContext);
}

opvp_result_t OPVPWrapper::EndPath()
{
    if (!supportEndPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndPath(printerContext);
}

opvp_result_t OPVPWrapper::StrokePath()
{
    if (!supportStrokePath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStrokePath(printerContext);
}

opvp_result_t OPVPWrapper::FillPath()
{
    if (!supportFillPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpFillPath(printerContext);
}

opvp_result_t OPVPWrapper::StrokeFillPath()
{
    if (!supportStrokeFillPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStrokeFillPath(printerContext);
}

opvp_result_t OPVPWrapper::SetClipPath(opvp_cliprule_t clipRule)
{
    if (!supportSetClipPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetClipPath(printerContext,clipRule);
}

opvp_result_t OPVPWrapper::SetCurrentPoint(opvp_fix_t x, opvp_fix_t y)
{
    if (!supportSetCurrentPoint) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSetCurrentPoint(printerContext,x,y);
}

opvp_result_t OPVPWrapper::LinePath(opvp_pathmode_t flag,
  opvp_int_t npoints, const opvp_point_t *points)
{
    if (!supportLinePath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpLinePath(printerContext,flag,npoints,points);
}

opvp_result_t OPVPWrapper::PolygonPath(opvp_int_t npolygons,
  const opvp_int_t *nvertexes, const opvp_point_t *points)
{
    if (!supportPolygonPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpPolygonPath(printerContext,npolygons,nvertexes,points);
}

opvp_result_t OPVPWrapper::RectanglePath(opvp_int_t nrectangles,
  const opvp_rectangle_t *rectangles)
{
    if (!supportRectanglePath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpRectanglePath(printerContext,nrectangles,rectangles);
}

opvp_result_t OPVPWrapper::RoundRectanglePath(opvp_int_t nrectangles,
  const opvp_roundrectangle_t *rectangles)
{
    if (!supportRoundRectanglePath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpRoundRectanglePath(printerContext,nrectangles,rectangles);
}

opvp_result_t OPVPWrapper::BezierPath(opvp_int_t npoints,
  const opvp_point_t *points)
{
    if (!supportBezierPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpBezierPath(printerContext,npoints,points);
}

opvp_result_t OPVPWrapper::ArcPath(opvp_arcmode_t kind,
  opvp_arcdir_t dir, opvp_fix_t bbx0,
  opvp_fix_t bby0, opvp_fix_t bbx1, opvp_fix_t bby1, opvp_fix_t x0,
  opvp_fix_t y0, opvp_fix_t x1, opvp_fix_t y1)
{
    if (!supportArcPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpArcPath(printerContext,kind,dir,bbx0,bby0,
       bbx1,bby1,x0,y0,x1,y1);
}

opvp_result_t OPVPWrapper::DrawImage(
  opvp_int_t sourceWidth, opvp_int_t sourceHeight, opvp_int_t sourcePitch,
  opvp_imageformat_t imageFormat, opvp_int_t destinationWidth,
  opvp_int_t destinationHeight, const void *imagedata)
{
    if (!supportDrawImage) {
      int result;

      if ((result = StartDrawImage(sourceWidth,sourceHeight,sourcePitch,
        imageFormat,destinationWidth,destinationHeight)) < 0) {
	return result;
      }
      if ((result = TransferDrawImage(sourcePitch*sourceHeight,
        imagedata)) < 0) {
	return result;
      }
      return EndDrawImage();
    }
    return procs->opvpDrawImage(printerContext,sourceWidth, sourceHeight,
      sourcePitch, imageFormat, destinationWidth, destinationHeight,
      imagedata);
}

opvp_result_t OPVPWrapper::StartDrawImage(
  opvp_int_t sourceWidth, opvp_int_t sourceHeight, opvp_int_t sourcePitch,
  opvp_imageformat_t imageFormat, opvp_int_t destinationWidth,
  opvp_int_t destinationHeight)
{
    if (!supportStartDrawImage) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartDrawImage(printerContext,sourceWidth,
      sourceHeight,sourcePitch,imageFormat,
      destinationWidth,destinationHeight);
}

opvp_result_t OPVPWrapper::TransferDrawImage(opvp_int_t count,
  const void *imagedata)
{
    if (!supportTransferDrawImage) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpTransferDrawImage(printerContext,count,imagedata);
}

opvp_result_t OPVPWrapper::EndDrawImage()
{
    if (!supportEndDrawImage) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndDrawImage(printerContext);
}

opvp_result_t OPVPWrapper::StartScanline(opvp_int_t yposition)
{
    if (!supportStartScanline) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartScanline(printerContext,yposition);
}

opvp_result_t OPVPWrapper::Scanline(opvp_int_t nscanpairs,
  const opvp_int_t *scanpairs)
{
    if (!supportScanline) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpScanline(printerContext,nscanpairs,scanpairs);
}

opvp_result_t OPVPWrapper::EndScanline()
{
    if (!supportEndScanline) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndScanline(printerContext);
}

opvp_result_t OPVPWrapper::StartRaster(
  opvp_int_t rasterWidth)
{
    if (!supportStartRaster) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartRaster(printerContext,rasterWidth);
}

opvp_result_t OPVPWrapper::TransferRasterData(opvp_int_t count,
  const opvp_byte_t *data)
{
    if (!supportTransferRasterData) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpTransferRasterData(printerContext,count,
      data);
}

opvp_result_t OPVPWrapper::SkipRaster(opvp_int_t count)
{
    if (!supportSkipRaster) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpSkipRaster(printerContext,count);
}

opvp_result_t OPVPWrapper::EndRaster()
{
    if (!supportEndRaster) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndRaster(printerContext);
}

opvp_result_t OPVPWrapper::StartStream()
{
    if (!supportStartStream) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpStartStream(printerContext);
}

opvp_result_t OPVPWrapper::TransferStreamData(opvp_int_t count,
  const void *data)
{
    if (!supportTransferStreamData) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpTransferStreamData(printerContext,count,data);
}

opvp_result_t OPVPWrapper::EndStream()
{
    if (!supportEndStream) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpEndStream(printerContext);
}

opvp_result_t OPVPWrapper::ResetClipPath()
{
    if (!supportResetClipPath) {
	*opvpErrorNo = OPVP_NOTSUPPORTED;
	return -1;
    }
    return procs->opvpResetClipPath(printerContext);
}

char *OPVPWrapper::allocString(char **destin, unsigned int size)
{
    if (!destin) return 0;

    if (*destin != 0) delete[] *destin;
    if (size > 0) {
	*destin = new char[size];
    }

    return *destin;
}

char **OPVPWrapper::genDynamicLibName(const char *name)
{
    static char	*buff[5] = {0,0,0,0,0};

    allocString(&(buff[0]), strlen(name)+1);
    strcpy(buff[0], name);
    allocString(&(buff[1]), strlen(name)+3+1);
    strcpy(buff[1], name);
    strcat(buff[1], ".so");
    allocString(&(buff[2]), strlen(name)+4+1);
    strcpy(buff[2], name);
    strcat(buff[2], ".dll");
    allocString(&(buff[3]), strlen(name)+6+1);
    strcpy(buff[3], "lib");
    strcat(buff[3], name);
    strcat(buff[3], ".so");
    buff[4] = 0;

    return buff;
}

OPVPWrapper *OPVPWrapper::loadDriver(const char *driverName,
  int outputFD, const char *printerModel)
{
    char **list = 0;
    int	 i;
    void *h;
    int nApiEntry;
    int (*opvpOpenPrinter)(opvp_int_t outputFD,
      const opvp_char_t * printerModel, const opvp_int_t version[2],
      opvp_api_procs_t **apiEntry) = 0;
    int (*opvpOpenPrinter_0_2)(int outputFD, char* printerModel,
      int *nApiEntry, OPVP_api_procs **apiEntry) = 0;
    opvp_api_procs_t *opvpProcs;
    OPVP_api_procs *opvpProcs_0_2;
    opvp_dc_t opvpContext;
    int opvpContext_0_2 = 0;
    opvp_int_t *opvpErrorNo = 0;
    int *opvpErrorNo_0_2 = 0;
    void *handle = 0;
    OPVPWrapper *opvp = 0;

    // remove directory part
    const char *s = strrchr(driverName,'/');
    if (s != NULL) {
        driverName = s+1;
    }

    list = genDynamicLibName(driverName);

    if (list) {
	i = 0;
	while (list[i]) {
	    if ((h = dlopen(list[i],RTLD_NOW))) {
		opvpOpenPrinter = (int (*)(opvp_int_t,
		  const opvp_char_t *, const opvp_int_t[2],
		  opvp_api_procs_t **))dlsym(h,"opvpOpenPrinter");
		opvpErrorNo = (opvp_int_t *)dlsym(h,"opvpErrorNo");
		if (opvpOpenPrinter && opvpErrorNo) {
		    handle = h;
		    break;
		}
		opvpOpenPrinter = 0;
		opvpErrorNo = 0;
		/* try version 0.2 driver */
		opvpOpenPrinter_0_2 = (int (*)(int, char*, int *,
		  OPVP_api_procs **))dlsym(h,"OpenPrinter");
		opvpErrorNo_0_2 = (int *)dlsym(h,"errorno");
		if (opvpOpenPrinter_0_2 && opvpErrorNo_0_2) {
		    handle = h;
		    break;
		}
		opvpOpenPrinter_0_2 = 0;
		opvpErrorNo_0_2 = 0;
	    }
	    i++;
	}
	for (i = 0;list[i] != 0;i++) {
	    delete[] (list[i]);
	    list[i] = 0;
	}
    }
    if (handle == 0) {
      OPRS::error("Loading vector printer driver (%s) fail\n",driverName);
      return 0;
    }
    if (opvpOpenPrinter != 0) {
	opvp_int_t apiVersion[2];

	/* require version 1.0 */
	apiVersion[0] = 1;
	apiVersion[1] = 0;
	if ((opvpContext = (*opvpOpenPrinter)(outputFD,
	     (const opvp_char_t *)printerModel,apiVersion,&opvpProcs)) < 0) {
	    OPRS::error("OpenPrinter fail\n",driverName);
	    unloadDriver(handle);
	    return 0;
	}
	opvp = new OPVPWrapper(handle, opvpErrorNo, opvpProcs, opvpContext);
    } else if (opvpOpenPrinter_0_2) {
	if ((opvpContext_0_2 = (*opvpOpenPrinter_0_2)(outputFD,
	     (char *)printerModel,&nApiEntry,&opvpProcs_0_2)) < 0) {
	    OPRS::error("OpenPrinter fail\n",driverName);
	    unloadDriver(handle);
	    return 0;
	}
	opvp = (OPVPWrapper *)new OPVPWrapper_0_2(handle, opvpErrorNo_0_2,
	  opvpProcs_0_2, opvpContext_0_2);
    }
    return opvp;
}

/*
 * unload vector-driver
 */
int OPVPWrapper::unloadDriver(void *opvpHandleA)
{
    if (opvpHandleA != 0) {
	dlclose(opvpHandleA);
    }
    return 0;
}

opvp_int_t OPVPWrapper::getErrorNo()
{
    return *opvpErrorNo;
}


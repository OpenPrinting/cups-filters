/*
  OPVPWrapper.h
*/

#ifndef OPVPWRAPPER_H
#define OPVPWRAPPER_H

#include "opvp_common.h"

class OPVPWrapper {
public:
    static OPVPWrapper *loadDriver(const char *driverName, int outputFD,
      const char *printerModel);
    static int unloadDriver(void *opvpHandleA);
private:
    static char *allocString(char **destin, unsigned int size);
    static char **genDynamicLibName(const char *name);
public:
    OPVPWrapper() {};
    OPVPWrapper(void *opvpHandleA, opvp_int_t *opvpErrorNoA,
      opvp_api_procs_t *procsA, opvp_dc_t printerContextA);
    virtual ~OPVPWrapper();

    virtual opvp_int_t getErrorNo();

    void getVersion(opvp_int_t versionA[2])
    {
	versionA[0] = version[0];
	versionA[1] = version[1];
    }

    virtual opvp_result_t ClosePrinter();
    virtual opvp_result_t StartJob(const opvp_char_t *jobInfo);
    virtual opvp_result_t EndJob();
    virtual opvp_result_t AbortJob();
    virtual opvp_result_t StartDoc(const opvp_char_t *docInfo);
    virtual opvp_result_t EndDoc();
    virtual opvp_result_t StartPage(const opvp_char_t *pageInfo);
    virtual opvp_result_t EndPage();
    virtual opvp_result_t QueryDeviceCapability(opvp_flag_t queryflag,
      opvp_int_t *buflen, opvp_byte_t *infoBuf);
    virtual opvp_result_t QueryDeviceInfo(opvp_flag_t queryflag,
      opvp_int_t *buflen, opvp_byte_t *infoBuf);
    virtual opvp_result_t ResetCTM();
    virtual opvp_result_t SetCTM(const opvp_ctm_t *pCTM);
    virtual opvp_result_t GetCTM(opvp_ctm_t *pCTM);
    virtual opvp_result_t InitGS();
    virtual opvp_result_t SaveGS();
    virtual opvp_result_t RestoreGS();
    virtual opvp_result_t QueryColorSpace(opvp_int_t *pnum,
      opvp_cspace_t *pcspace);
    virtual opvp_result_t SetColorSpace(opvp_cspace_t cspace);
    virtual opvp_result_t GetColorSpace(opvp_cspace_t *pcspace);
    virtual opvp_result_t SetFillMode(opvp_fillmode_t fillmode);
    virtual opvp_result_t GetFillMode(opvp_fillmode_t *pfillmode);
    virtual opvp_result_t SetAlphaConstant(opvp_float_t alpha);
    virtual opvp_result_t GetAlphaConstant(opvp_float_t *palpha);
    virtual opvp_result_t SetLineWidth(opvp_fix_t width);
    virtual opvp_result_t GetLineWidth(opvp_fix_t *pwidth);
    virtual opvp_result_t SetLineDash(opvp_int_t num, const opvp_fix_t *pdash);
    virtual opvp_result_t GetLineDash(opvp_int_t *pnum, opvp_fix_t *pdash);
    virtual opvp_result_t SetLineDashOffset(opvp_fix_t offset);
    virtual opvp_result_t GetLineDashOffset(opvp_fix_t *poffset);
    virtual opvp_result_t SetLineStyle(opvp_linestyle_t linestyle);
    virtual opvp_result_t GetLineStyle(opvp_linestyle_t *plinestyle);
    virtual opvp_result_t SetLineCap(opvp_linecap_t linecap);
    virtual opvp_result_t GetLineCap(opvp_linecap_t *plinecap);
    virtual opvp_result_t SetLineJoin(opvp_linejoin_t linejoin);
    virtual opvp_result_t GetLineJoin(opvp_linejoin_t *plinejoin);
    virtual opvp_result_t SetMiterLimit(opvp_fix_t miterlimit);
    virtual opvp_result_t GetMiterLimit(opvp_fix_t *pmiterlimit);
    virtual opvp_result_t SetPaintMode(opvp_paintmode_t paintmode);
    virtual opvp_result_t GetPaintMode(opvp_paintmode_t *ppaintmode);
    virtual opvp_result_t SetStrokeColor(const opvp_brush_t *brush);
    virtual opvp_result_t SetFillColor(const opvp_brush_t *brush);
    virtual opvp_result_t SetBgColor(const opvp_brush_t *brush);
    virtual opvp_result_t NewPath();
    virtual opvp_result_t EndPath();
    virtual opvp_result_t StrokePath();
    virtual opvp_result_t FillPath();
    virtual opvp_result_t StrokeFillPath();
    virtual opvp_result_t SetClipPath(opvp_cliprule_t clipRule);
    virtual opvp_result_t SetCurrentPoint(opvp_fix_t x, opvp_fix_t y);
    virtual opvp_result_t LinePath(opvp_pathmode_t flag,
      opvp_int_t npoints, const opvp_point_t *points);
    virtual opvp_result_t PolygonPath(opvp_int_t npolygons,
      const opvp_int_t *nvertexes, const opvp_point_t *points);
    virtual opvp_result_t RectanglePath(opvp_int_t nrectangles,
      const opvp_rectangle_t *reclangles);
    virtual opvp_result_t RoundRectanglePath(opvp_int_t nrectangles,
      const opvp_roundrectangle_t *reclangles);
    virtual opvp_result_t BezierPath(opvp_int_t npoints,
      const opvp_point_t *points);
    virtual opvp_result_t ArcPath(opvp_arcmode_t kind,
      opvp_arcdir_t dir, opvp_fix_t bbx0, opvp_fix_t bby0,
      opvp_fix_t bbx1, opvp_fix_t bby1, opvp_fix_t x0,
      opvp_fix_t y0, opvp_fix_t x1, opvp_fix_t y1);
    virtual opvp_result_t DrawImage(opvp_int_t sourceWidth,
      opvp_int_t sourceHeight, opvp_int_t sourcePitch,
      opvp_imageformat_t imageFormat,
      opvp_int_t destinationWidth, opvp_int_t destinationHeight,
      const void *imageData);
    virtual opvp_result_t StartDrawImage(opvp_int_t sourceWidth,
      opvp_int_t sourceHeight, opvp_int_t sourcePitch,
      opvp_imageformat_t imageFormat,
      opvp_int_t destinationWidth, opvp_int_t destinationHeight);
    virtual opvp_result_t TransferDrawImage(opvp_int_t count,
      const void *imageData);
    virtual opvp_result_t EndDrawImage();
    virtual opvp_result_t StartScanline(opvp_int_t yposition);
    virtual opvp_result_t Scanline(opvp_int_t nscanpairs,
      const opvp_int_t *scanpairs);
    virtual opvp_result_t EndScanline();
    virtual opvp_result_t StartRaster(opvp_int_t rasterWidth);
    virtual opvp_result_t TransferRasterData(opvp_int_t count,
      const opvp_byte_t *data);
    virtual opvp_result_t SkipRaster(opvp_int_t count);
    virtual opvp_result_t EndRaster();
    virtual opvp_result_t StartStream();
    virtual opvp_result_t TransferStreamData(opvp_int_t count,
      const void *data);
    virtual opvp_result_t EndStream();
    virtual opvp_result_t ResetClipPath();

    bool supportClosePrinter;
    bool supportStartJob;
    bool supportEndJob;
    bool supportAbortJob;
    bool supportStartDoc;
    bool supportEndDoc;
    bool supportStartPage;
    bool supportEndPage;
    bool supportResetCTM;
    bool supportSetCTM;
    bool supportGetCTM;
    bool supportInitGS;
    bool supportSaveGS;
    bool supportRestoreGS;
    bool supportQueryColorSpace;
    bool supportSetColorSpace;
    bool supportGetColorSpace;
    bool supportSetFillMode;
    bool supportGetFillMode;
    bool supportSetAlphaConstant;
    bool supportGetAlphaConstant;
    bool supportSetLineWidth;
    bool supportGetLineWidth;
    bool supportSetLineDash;
    bool supportGetLineDash;
    bool supportSetLineDashOffset;
    bool supportGetLineDashOffset;
    bool supportSetLineStyle;
    bool supportGetLineStyle;
    bool supportSetLineCap;
    bool supportGetLineCap;
    bool supportSetLineJoin;
    bool supportGetLineJoin;
    bool supportSetMiterLimit;
    bool supportGetMiterLimit;
    bool supportSetPaintMode;
    bool supportGetPaintMode;
    bool supportSetStrokeColor;
    bool supportSetFillColor;
    bool supportSetBgColor;
    bool supportNewPath;
    bool supportEndPath;
    bool supportStrokePath;
    bool supportFillPath;
    bool supportStrokeFillPath;
    bool supportSetClipPath;
    bool supportSetCurrentPoint;
    bool supportLinePath;
    bool supportPolygonPath;
    bool supportRectanglePath;
    bool supportRoundRectanglePath;
    bool supportBezierPath;
    bool supportArcPath;
    bool supportDrawImage;
    bool supportStartDrawImage;
    bool supportTransferDrawImage;
    bool supportEndDrawImage;
    bool supportStartScanline;
    bool supportScanline;
    bool supportEndScanline;
    bool supportStartRaster;
    bool supportTransferRasterData;
    bool supportSkipRaster;
    bool supportEndRaster;
    bool supportStartStream;
    bool supportTransferStreamData;
    bool supportEndStream;
    bool supportQueryDeviceCapability;
    bool supportQueryDeviceInfo;
    bool supportResetClipPath;

protected:
    void *opvpHandle;
    opvp_int_t version[2];
private:
    opvp_api_procs_t *procs;
    opvp_int_t *opvpErrorNo;
    opvp_dc_t printerContext;
};

#endif

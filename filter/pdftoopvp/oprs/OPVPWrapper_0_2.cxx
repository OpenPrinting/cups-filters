/*
  OPVPWrapper_0_2.cc
*/


#include <config.h>
#include "OPVPWrapper_0_2.h"
#include <string.h>

/* color space mapping 0.2 to 1.0 */
opvp_cspace_t OPVPWrapper_0_2::cspace_0_2_to_1_0[] = {
    OPVP_CSPACE_BW,
    OPVP_CSPACE_DEVICEGRAY,
    OPVP_CSPACE_DEVICECMY,
    OPVP_CSPACE_DEVICECMYK,
    OPVP_CSPACE_DEVICERGB,
    OPVP_CSPACE_STANDARDRGB,
    OPVP_CSPACE_STANDARDRGB64
};

/* color space mapping 1.0 to 0.2 */
OPVP_ColorSpace OPVPWrapper_0_2::cspace_1_0_to_0_2[] = {
    OPVP_cspaceBW,
    OPVP_cspaceDeviceGray,
    OPVP_cspaceDeviceCMY,
    OPVP_cspaceDeviceCMYK,
    OPVP_cspaceDeviceRGB,
    (OPVP_ColorSpace)0, /* 0.2 doesn't have OPVP_CSPACE_DEVICEKRGB */
    OPVP_cspaceStandardRGB,
    OPVP_cspaceStandardRGB64,
};

/* image format mapping 1.0 to 0.2 */
OPVP_ImageFormat OPVPWrapper_0_2::iformat_1_0_to_0_2[] = {
    OPVP_iformatRaw,
    /* OPVP_IFORMAT_MASK use iformat raw in 0.2 */
    OPVP_iformatRaw,
    OPVP_iformatRLE,
    OPVP_iformatJPEG,
    OPVP_iformatPNG,
};

/* image colorDepth needed in 0.2 */
int OPVPWrapper_0_2::colorDepth_0_2[] = {
    1, /* OPVP_CSPACE_BW */
    8, /* OPVP_CSPACE_DEVICEGRAY */
    24, /* OPVP_CSPACE_DEVICECMY */
    32, /* OPVP_CSPACE_DEVICECMYK */
    24, /* OPVP_CSPACE_DEVICERGB */
    32, /* OPVP_CSPACE_DEVICEKRGB */
    24, /* OPVP_CSPACE_STANDARDRGB */
    64, /* OPVP_CSPACE_STANDARDRGB64 */
};

OPVPWrapper_0_2::OPVPWrapper_0_2(void *opvpHandleA, int *opvpErrorNoA,
  OPVP_api_procs *procsA, int printerContextA)
{
    procs_0_2 = procsA;
    opvpHandle = opvpHandleA;
    opvpErrorNo_0_2 = opvpErrorNoA;
    printerContext_0_2 = printerContextA;
    version[0] = 0;
    version[1] = 2;
    supportClosePrinter = (procs_0_2->ClosePrinter != 0);
    supportStartJob = (procs_0_2->StartJob != 0);
    supportEndJob = (procs_0_2->EndJob != 0);
    supportAbortJob = false;
    supportStartDoc = (procs_0_2->StartDoc != 0);
    supportEndDoc = (procs_0_2->EndDoc != 0);
    supportStartPage = (procs_0_2->StartPage != 0);
    supportEndPage = (procs_0_2->EndPage != 0);
    supportResetCTM = (procs_0_2->ResetCTM != 0);
    supportSetCTM = (procs_0_2->SetCTM != 0);
    supportGetCTM = (procs_0_2->GetCTM != 0);
    supportInitGS = (procs_0_2->InitGS != 0);
    supportSaveGS = (procs_0_2->SaveGS != 0);
    supportRestoreGS = (procs_0_2->RestoreGS != 0);
    supportQueryColorSpace = (procs_0_2->QueryColorSpace != 0);
    supportSetColorSpace = (procs_0_2->SetColorSpace != 0);
    supportGetColorSpace = (procs_0_2->GetColorSpace != 0);
    supportSetFillMode = (procs_0_2->SetFillMode != 0);
    supportGetFillMode = (procs_0_2->GetFillMode != 0);
    supportSetAlphaConstant = (procs_0_2->SetAlphaConstant != 0);
    supportGetAlphaConstant = (procs_0_2->GetAlphaConstant != 0);
    supportSetLineWidth = (procs_0_2->SetLineWidth != 0);
    supportGetLineWidth = (procs_0_2->GetLineWidth != 0);
    supportSetLineDash = (procs_0_2->SetLineDash != 0);
    supportGetLineDash = (procs_0_2->GetLineDash != 0);
    supportSetLineDashOffset = (procs_0_2->SetLineDashOffset != 0);
    supportGetLineDashOffset = (procs_0_2->GetLineDashOffset != 0);
    supportSetLineStyle = (procs_0_2->SetLineStyle != 0);
    supportGetLineStyle = (procs_0_2->GetLineStyle != 0);
    supportSetLineCap = (procs_0_2->SetLineCap != 0);
    supportGetLineCap = (procs_0_2->GetLineCap != 0);
    supportSetLineJoin = (procs_0_2->SetLineJoin != 0);
    supportGetLineJoin = (procs_0_2->GetLineJoin != 0);
    supportSetMiterLimit = (procs_0_2->SetMiterLimit != 0);
    supportGetMiterLimit = (procs_0_2->GetMiterLimit != 0);
    supportSetPaintMode = (procs_0_2->SetPaintMode != 0);
    supportGetPaintMode = (procs_0_2->GetPaintMode != 0);
    supportSetStrokeColor = (procs_0_2->SetStrokeColor != 0);
    supportSetFillColor = (procs_0_2->SetFillColor != 0);
    supportSetBgColor = (procs_0_2->SetBgColor != 0);
    supportNewPath = (procs_0_2->NewPath != 0);
    supportEndPath = (procs_0_2->EndPath != 0);
    supportStrokePath = (procs_0_2->StrokePath != 0);
    supportFillPath = (procs_0_2->FillPath != 0);
    supportStrokeFillPath = (procs_0_2->StrokeFillPath != 0);
    supportSetClipPath = (procs_0_2->SetClipPath != 0);
    supportSetCurrentPoint = (procs_0_2->SetCurrentPoint != 0);
    supportLinePath = (procs_0_2->LinePath != 0);
    supportPolygonPath = (procs_0_2->PolygonPath != 0);
    supportRectanglePath = (procs_0_2->RectanglePath != 0);
    supportRoundRectanglePath = (procs_0_2->RoundRectanglePath != 0);
    supportBezierPath = (procs_0_2->BezierPath != 0);
    supportArcPath = (procs_0_2->ArcPath != 0);
    supportDrawImage = (procs_0_2->DrawImage != 0);
    supportStartDrawImage = (procs_0_2->StartDrawImage != 0);
    supportTransferDrawImage = (procs_0_2->TransferDrawImage != 0);
    supportEndDrawImage = (procs_0_2->EndDrawImage != 0);
    supportStartScanline = (procs_0_2->StartScanline != 0);
    supportScanline = (procs_0_2->Scanline != 0);
    supportEndScanline = (procs_0_2->EndScanline != 0);
    supportStartRaster = (procs_0_2->StartRaster != 0);
    supportTransferRasterData = (procs_0_2->TransferRasterData != 0);
    supportSkipRaster = (procs_0_2->SkipRaster != 0);
    supportEndRaster = (procs_0_2->EndRaster != 0);
    supportStartStream = (procs_0_2->StartStream != 0);
    supportTransferStreamData = (procs_0_2->TransferStreamData != 0);
    supportEndStream = (procs_0_2->EndStream != 0);
    supportQueryDeviceCapability = (procs_0_2->QueryDeviceCapability != 0);
    supportQueryDeviceInfo = (procs_0_2->QueryDeviceInfo != 0);
    supportResetClipPath = (procs_0_2->ResetClipPath != 0);
    colorSpace = OPVP_CSPACE_STANDARDRGB;
    if (supportGetColorSpace) {
	if (GetColorSpace(&colorSpace) != OPVP_OK) {
	    colorSpace = OPVP_CSPACE_STANDARDRGB;
	}
    }
}

OPVPWrapper_0_2::~OPVPWrapper_0_2()
{
}

opvp_result_t OPVPWrapper_0_2::ClosePrinter()
{
    if (!supportClosePrinter) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->ClosePrinter(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::StartJob(
  const opvp_char_t *jobInfo)
{
    if (!supportStartJob) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StartJob(printerContext_0_2,
      (char *)jobInfo);
}

opvp_result_t OPVPWrapper_0_2::EndJob()
{
    if (!supportEndJob) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndJob(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::AbortJob()
{
    *opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
    return -1;
}

opvp_result_t OPVPWrapper_0_2::StartDoc(
  const opvp_char_t *docInfo)
{
    if (!supportStartDoc) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StartDoc(printerContext_0_2,
      (char *)docInfo);
}

opvp_result_t OPVPWrapper_0_2::EndDoc()
{
    if (!supportEndDoc) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndDoc(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::StartPage(
  const opvp_char_t *pageInfo)
{
    int r;

    if (!supportStartPage) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((r = procs_0_2->StartPage(printerContext_0_2,
           /* discard const */(char *)pageInfo)) != OPVP_OK) {
	  /* error */
	return (opvp_result_t)r;
    }
    /* initialize ROP */
    if (procs_0_2->SetROP != 0) {
	procs_0_2->SetROP(printerContext_0_2,
	  OPVP_0_2_ROP_P);
    }
    return OPVP_OK;
}

opvp_result_t OPVPWrapper_0_2::EndPage()
{
    if (!supportEndPage) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndPage(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::QueryDeviceCapability(
  opvp_flag_t queryflag, opvp_int_t *buflen, opvp_byte_t *infoBuf)
{
    if (!supportQueryDeviceCapability) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->
      QueryDeviceCapability(printerContext_0_2,queryflag,*buflen,
      (char *)infoBuf);
}

opvp_result_t OPVPWrapper_0_2::QueryDeviceInfo(
  opvp_flag_t queryflag, opvp_int_t *buflen, opvp_byte_t *infoBuf)
{
    if (!supportQueryDeviceInfo) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (queryflag & OPVP_QF_MEDIACOPY) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (queryflag & OPVP_QF_PRINTREGION) {
	queryflag &= ~OPVP_QF_PRINTREGION;
	queryflag |= 0x0020000;
    }
    return (opvp_result_t)procs_0_2->QueryDeviceInfo(printerContext_0_2,
      queryflag,*buflen,(char *)infoBuf);
}

opvp_result_t OPVPWrapper_0_2::ResetCTM()
{
    if (!supportResetCTM) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->ResetCTM(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::SetCTM(const opvp_ctm_t *pCTM)
{
    if (!supportSetCTM) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SetCTM(printerContext_0_2,
      (OPVP_CTM *)pCTM);
}

opvp_result_t OPVPWrapper_0_2::GetCTM(opvp_ctm_t *pCTM)
{
    if (!supportGetCTM) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->GetCTM(printerContext_0_2,
      (OPVP_CTM *)pCTM);
}

opvp_result_t OPVPWrapper_0_2::InitGS()
{
    int r;

    if (!supportInitGS) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((r = procs_0_2->InitGS(printerContext_0_2)) != OPVP_OK) {
	  /* error */
	return (opvp_result_t)r;
    }
    /* initialize ROP */
    if (procs_0_2->SetROP != 0) {
	procs_0_2->SetROP(printerContext_0_2,
	  OPVP_0_2_ROP_P);
    }
    return OPVP_OK;
}

opvp_result_t OPVPWrapper_0_2::SaveGS()
{
    if (!supportSaveGS) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SaveGS(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::RestoreGS()
{
    if (!supportRestoreGS) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->RestoreGS(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::QueryColorSpace(
  opvp_int_t *pnum, opvp_cspace_t *pcspace)
{
    int r;
    int i;

    if (!supportQueryColorSpace) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((r = procs_0_2->QueryColorSpace(printerContext_0_2,
	 (OPVP_ColorSpace *)pcspace,pnum)) != OPVP_OK) {
	/* error */
	return (opvp_result_t)r;
    }
    /* translate cspaces */
    for (i = 0;i < *pnum;i++) {
	if ((unsigned int)pcspace[i] 
	     > sizeof(cspace_0_2_to_1_0)/sizeof(opvp_cspace_t)) {
	    /* unknown color space */
	    /* set DEVICERGB instead */
	    pcspace[i] = OPVP_CSPACE_DEVICERGB;
	} else {
	    pcspace[i] = cspace_0_2_to_1_0[pcspace[i]];
	}
    }
    return OPVP_OK;
}

opvp_result_t OPVPWrapper_0_2::SetColorSpace(
  opvp_cspace_t cspace)
{
    int r;

    if (!supportSetColorSpace) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (cspace == OPVP_CSPACE_DEVICEKRGB) {
	/* 0.2 doesn't have OPVP_CSPACE_DEVICEKRGB */
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((unsigned int)cspace
	 > sizeof(cspace_1_0_to_0_2)/sizeof(OPVP_ColorSpace)) {
	/* unknown color space */
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    r =  procs_0_2->SetColorSpace(printerContext_0_2,
      cspace_1_0_to_0_2[cspace]);
    if (r == OPVP_OK) {
	colorSpace = cspace;
    }
    return (opvp_result_t)r;
}

opvp_result_t OPVPWrapper_0_2::GetColorSpace(
  opvp_cspace_t *pcspace)
{
    int r;

    if (!supportGetColorSpace) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((r = procs_0_2->GetColorSpace(printerContext_0_2,
      (OPVP_ColorSpace *)pcspace)) != OPVP_OK) {
	/* error */
	return (opvp_result_t)r;
    }
    if ((unsigned int)*pcspace
	 > sizeof(cspace_0_2_to_1_0)/sizeof(opvp_cspace_t)) {
	/* unknown color space */
	/* set DEVICERGB instead */
	*pcspace = OPVP_CSPACE_DEVICERGB;
    } else {
	*pcspace = cspace_0_2_to_1_0[*pcspace];
    }
    return (opvp_result_t)r;
}

opvp_result_t OPVPWrapper_0_2::SetFillMode(
  opvp_fillmode_t fillmode)
{
    if (!supportSetFillMode) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_FillMode is comaptible with opvp_fillmode_t */
    return (opvp_result_t)procs_0_2->SetFillMode(printerContext_0_2,
      (OPVP_FillMode)fillmode);
}

opvp_result_t OPVPWrapper_0_2::GetFillMode(
  opvp_fillmode_t *pfillmode)
{
    if (!supportGetFillMode) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_FillMode is comaptible with opvp_fillmode_t */
    return (opvp_result_t)procs_0_2->GetFillMode(printerContext_0_2,
      (OPVP_FillMode *)pfillmode);
}

opvp_result_t OPVPWrapper_0_2::SetAlphaConstant(
  opvp_float_t alpha)
{
    if (!supportSetAlphaConstant) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SetAlphaConstant(printerContext_0_2,alpha);
}

opvp_result_t OPVPWrapper_0_2::GetAlphaConstant(
  opvp_float_t *palpha)
{
    if (!supportGetAlphaConstant) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->GetAlphaConstant(printerContext_0_2,palpha);
}

opvp_result_t OPVPWrapper_0_2::SetLineWidth(
  opvp_fix_t width)
{
    if (!supportSetLineWidth) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SetLineWidth(printerContext_0_2,width);
}

opvp_result_t OPVPWrapper_0_2::GetLineWidth(
  opvp_fix_t *pwidth)
{
    if (!supportGetLineWidth) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->GetLineWidth(printerContext_0_2,pwidth);
}

opvp_result_t OPVPWrapper_0_2::SetLineDash(opvp_int_t num,
  const opvp_fix_t *pdash)
{
    if (!supportSetLineDash) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SetLineDash(printerContext_0_2,
      /* remove const */ (OPVP_Fix *)pdash,num);
}

opvp_result_t OPVPWrapper_0_2::GetLineDash(
  opvp_int_t *pnum, opvp_fix_t *pdash)
{
    if (!supportGetLineDash) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->GetLineDash(printerContext_0_2,
      pdash,pnum);
}

opvp_result_t OPVPWrapper_0_2::SetLineDashOffset(
  opvp_fix_t offset)
{
    if (!supportSetLineDashOffset) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SetLineDashOffset(printerContext_0_2,offset);
}

opvp_result_t OPVPWrapper_0_2::GetLineDashOffset(
  opvp_fix_t *poffset)
{
    if (!supportGetLineDashOffset) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->GetLineDashOffset(printerContext_0_2,poffset);
}

opvp_result_t OPVPWrapper_0_2::SetLineStyle(
  opvp_linestyle_t linestyle)
{
    if (!supportSetLineStyle) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_LineStyle is compatible with opvp_linestyle_t */
    return (opvp_result_t)procs_0_2->SetLineStyle(printerContext_0_2,
      (OPVP_LineStyle)linestyle);
}

opvp_result_t OPVPWrapper_0_2::GetLineStyle(
  opvp_linestyle_t *plinestyle)
{
    if (!supportGetLineStyle) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_LineStyle is compatible with opvp_linestyle_t */
    return (opvp_result_t)procs_0_2->GetLineStyle(printerContext_0_2,
      (OPVP_LineStyle *)plinestyle);
}

opvp_result_t OPVPWrapper_0_2::SetLineCap(
  opvp_linecap_t linecap)
{
    if (!supportSetLineCap) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_LineCap is compatible with opvp_cap_t */
    return (opvp_result_t)procs_0_2->SetLineCap(printerContext_0_2,
      (OPVP_LineCap)linecap);
}

opvp_result_t OPVPWrapper_0_2::GetLineCap(
  opvp_linecap_t *plinecap)
{
    if (!supportGetLineCap) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_LineCap is compatible with opvp_cap_t */
    return (opvp_result_t)procs_0_2->GetLineCap(printerContext_0_2,
      (OPVP_LineCap *)plinecap);
}

opvp_result_t OPVPWrapper_0_2::SetLineJoin(
  opvp_linejoin_t linejoin)
{
    if (!supportSetLineJoin) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_LineJoin is compatible with opvp_linejoin_t */
    return (opvp_result_t)procs_0_2->SetLineJoin(printerContext_0_2,
      (OPVP_LineJoin)linejoin);
}

opvp_result_t OPVPWrapper_0_2::GetLineJoin(
  opvp_linejoin_t *plinejoin)
{
    if (!supportGetLineJoin) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_LineJoin is compatible with opvp_linejoin_t */
    return (opvp_result_t)procs_0_2->GetLineJoin(printerContext_0_2,
      (OPVP_LineJoin *)plinejoin);
}

opvp_result_t OPVPWrapper_0_2::SetMiterLimit(
  opvp_fix_t miterlimit)
{
    if (!supportSetMiterLimit) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_Fix is compatible with opvp_fix_t */
    return (opvp_result_t)procs_0_2->SetMiterLimit(printerContext_0_2,
      (OPVP_Fix)miterlimit);
}

opvp_result_t OPVPWrapper_0_2::GetMiterLimit(
  opvp_fix_t *pmiterlimit)
{
    if (!supportGetMiterLimit) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_Fix is compatible with opvp_fix_t */
    return (opvp_result_t)procs_0_2->GetMiterLimit(printerContext_0_2,
      (OPVP_Fix *)pmiterlimit);
}

opvp_result_t OPVPWrapper_0_2::SetPaintMode(
  opvp_paintmode_t paintmode)
{
    if (!supportSetPaintMode) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_PaintMode is compatible with opvp_paintmode_t */
    return (opvp_result_t)procs_0_2->SetPaintMode(printerContext_0_2,
      (OPVP_PaintMode)paintmode);
}

opvp_result_t OPVPWrapper_0_2::GetPaintMode(
  opvp_paintmode_t *ppaintmode)
{
    if (!supportGetPaintMode) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_PaintMode is compatible with opvp_paintmode_t */
    return (opvp_result_t)procs_0_2->GetPaintMode(printerContext_0_2,
      (OPVP_PaintMode *)ppaintmode);
}

opvp_result_t OPVPWrapper_0_2::SetStrokeColor(
  const opvp_brush_t *brush)
{
    OPVP_Brush brush_0_2;

    if (!supportSetStrokeColor) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (brush == 0) {
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    if (brush->colorSpace == OPVP_CSPACE_DEVICEKRGB) {
	/* 0.2 doesn't have OPVP_CSPACE_DEVICEKRGB */
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((unsigned int)brush->colorSpace
	 > sizeof(cspace_1_0_to_0_2)/sizeof(OPVP_ColorSpace)) {
	/* unknown color space */
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    brush_0_2.colorSpace = cspace_1_0_to_0_2[brush->colorSpace];
    brush_0_2.xorg = brush->xorg;
    brush_0_2.yorg = brush->yorg;
    brush_0_2.pbrush = (OPVP_BrushData *)brush->pbrush;
    memcpy(brush_0_2.color,brush->color,sizeof(brush_0_2.color));
    return (opvp_result_t)procs_0_2->SetStrokeColor(printerContext_0_2,
      &brush_0_2);
}

opvp_result_t OPVPWrapper_0_2::SetFillColor(
  const opvp_brush_t *brush)
{
    OPVP_Brush brush_0_2;

    if (!supportSetFillColor) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (brush == 0) {
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    if (brush->colorSpace == OPVP_CSPACE_DEVICEKRGB) {
	/* 0.2 doesn't have OPVP_CSPACE_DEVICEKRGB */
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((unsigned int)brush->colorSpace
	 > sizeof(cspace_1_0_to_0_2)/sizeof(OPVP_ColorSpace)) {
	/* unknown color space */
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    brush_0_2.colorSpace = cspace_1_0_to_0_2[brush->colorSpace];
    brush_0_2.xorg = brush->xorg;
    brush_0_2.yorg = brush->yorg;
    brush_0_2.pbrush = (OPVP_BrushData *)brush->pbrush;
    memcpy(brush_0_2.color,brush->color,sizeof(brush_0_2.color));
    return (opvp_result_t)procs_0_2->SetFillColor(printerContext_0_2,
      &brush_0_2);
}

opvp_result_t OPVPWrapper_0_2::SetBgColor(
  const opvp_brush_t *brush)
{
    OPVP_Brush brush_0_2;

    if (!supportSetBgColor) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (brush == 0) {
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    if (brush->colorSpace == OPVP_CSPACE_DEVICEKRGB) {
	/* 0.2 doesn't have OPVP_CSPACE_DEVICEKRGB */
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if ((unsigned int)brush->colorSpace
	 > sizeof(cspace_1_0_to_0_2)/sizeof(OPVP_ColorSpace)) {
	/* unknown color space */
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    brush_0_2.colorSpace = cspace_1_0_to_0_2[brush->colorSpace];
    brush_0_2.xorg = brush->xorg;
    brush_0_2.yorg = brush->yorg;
    brush_0_2.pbrush = (OPVP_BrushData *)brush->pbrush;
    memcpy(brush_0_2.color,brush->color,sizeof(brush_0_2.color));
    return (opvp_result_t)procs_0_2->SetBgColor(printerContext_0_2,
      &brush_0_2);
}

opvp_result_t OPVPWrapper_0_2::NewPath()
{
    if (!supportNewPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->NewPath(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::EndPath()
{
    if (!supportEndPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndPath(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::StrokePath()
{
    if (!supportStrokePath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StrokePath(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::FillPath()
{
    if (!supportFillPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->FillPath(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::StrokeFillPath()
{
    if (!supportStrokeFillPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StrokeFillPath(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::SetClipPath(
  opvp_cliprule_t clipRule)
{
    if (!supportSetClipPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_ClipRule is compatible with opvp_cliprule_t */
    return (opvp_result_t)procs_0_2->SetClipPath(printerContext_0_2,
      (OPVP_ClipRule)clipRule);
}

opvp_result_t OPVPWrapper_0_2::SetCurrentPoint(
  opvp_fix_t x, opvp_fix_t y)
{
    if (!supportSetCurrentPoint) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SetCurrentPoint(printerContext_0_2,x,y);
}

opvp_result_t OPVPWrapper_0_2::LinePath(
  opvp_pathmode_t flag, opvp_int_t npoints, const opvp_point_t *points)
{
    if (!supportLinePath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_Point is compatible with opvp_point_t */
    return (opvp_result_t)procs_0_2->LinePath(printerContext_0_2,flag,npoints,
      (OPVP_Point *)points);
}

opvp_result_t OPVPWrapper_0_2::PolygonPath(
  opvp_int_t npolygons, const opvp_int_t *nvertexes,
  const opvp_point_t *points)
{
    if (!supportPolygonPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_Point is compatible with opvp_point_t */
    return (opvp_result_t)procs_0_2->PolygonPath(printerContext_0_2,
      (int)npolygons,(int *)nvertexes,(OPVP_Point *)points);
}

opvp_result_t OPVPWrapper_0_2::RectanglePath(
  opvp_int_t nrectangles, const opvp_rectangle_t *rectangles)
{
    if (!supportRectanglePath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_Rectangle is compatible with opvp_rectangle_t */
    return (opvp_result_t)procs_0_2->RectanglePath(printerContext_0_2,
      (int)nrectangles,(OPVP_Rectangle *)rectangles);
}

opvp_result_t OPVPWrapper_0_2::RoundRectanglePath(
  opvp_int_t nrectangles, const opvp_roundrectangle_t *rectangles)
{
    if (!supportRoundRectanglePath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_RoundRectangle is compatible with opvp_roundrectangle_t */
    return (opvp_result_t)procs_0_2->RoundRectanglePath(printerContext_0_2,
      (int)nrectangles,(OPVP_RoundRectangle *)rectangles);
}

opvp_result_t OPVPWrapper_0_2::BezierPath(opvp_int_t npoints,
    const opvp_point_t *points)
{
    if (!supportBezierPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* OPVP_Point is compatible with opvp_point_t */
    return (opvp_result_t)procs_0_2->BezierPath(printerContext_0_2,(int)npoints,
      (OPVP_Point *)points);
}

opvp_result_t OPVPWrapper_0_2::ArcPath(opvp_arcmode_t kind, opvp_arcdir_t dir,
  opvp_fix_t bbx0, opvp_fix_t bby0, opvp_fix_t bbx1,
  opvp_fix_t bby1, opvp_fix_t x0, opvp_fix_t y0, opvp_fix_t x1, opvp_fix_t y1)
{
    if (!supportArcPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    /* opvp_arcmode_t is compatible with int */
    /* opvp_arcdir_t is compatible with int */
    return (opvp_result_t)procs_0_2->ArcPath(printerContext_0_2,
      (int)kind,(int)dir,bbx0,bby0,
       bbx1,bby1,x0,y0,x1,y1);
}

opvp_result_t OPVPWrapper_0_2::DrawImage(opvp_int_t sourceWidth,
    opvp_int_t sourceHeight, opvp_int_t sourcePitch,
    opvp_imageformat_t imageFormat, opvp_int_t destinationWidth,
    opvp_int_t destinationHeight, const void *imagedata)
{
    int r;
    OPVP_Rectangle rect;
    OPVP_ImageFormat iformat_0_2;
    OPVP_PaintMode paintmode_0_2 = OPVP_paintModeTransparent;
    int depth;

    if (!supportDrawImage) {
      int result;

      if ((result = StartDrawImage(sourceWidth,
	sourceHeight,sourcePitch,imageFormat,destinationWidth,
	destinationHeight)) < 0) {
	return result;
      }
      if ((result = TransferDrawImage(sourcePitch*sourceHeight,
        imagedata)) < 0) {
	return result;
      }
      return EndDrawImage();
    }

    if (imageFormat == OPVP_IFORMAT_MASK) {
	if (procs_0_2->GetPaintMode != 0) {
	    procs_0_2->GetPaintMode(printerContext_0_2,
	      &paintmode_0_2);
	}
	if (paintmode_0_2 != OPVP_paintModeTransparent) {
	    if (procs_0_2->SetROP != 0) {
		procs_0_2->SetROP(printerContext_0_2,
		    OPVP_0_2_ROP_S);
	    }
	}
	else {
	    if (procs_0_2->SetROP != 0) {
		procs_0_2->SetROP(printerContext_0_2,
		    OPVP_0_2_ROP_OR);
	    }
	}
	depth = 1;
    } else {
	if (procs_0_2->SetROP != 0) {
	    procs_0_2->SetROP(printerContext_0_2,OPVP_0_2_ROP_S);
	}
	depth = colorDepth_0_2[colorSpace];
    }

    OPVP_I2FIX(0,rect.p0.x);
    OPVP_I2FIX(0,rect.p0.y);
    OPVP_I2FIX(destinationWidth,rect.p1.x);
    OPVP_I2FIX(destinationHeight,rect.p1.y);
    if ((unsigned int)imageFormat 
      > sizeof(iformat_1_0_to_0_2)/sizeof(OPVP_ImageFormat)) {
	/* illegal image format */
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    iformat_0_2 = iformat_1_0_to_0_2[imageFormat];
    r = procs_0_2->DrawImage(printerContext_0_2,sourceWidth,sourceHeight,
	    depth,iformat_0_2,rect,
	    sourcePitch*sourceHeight,
	    /* remove const */ (void *)imagedata);

    if (procs_0_2->SetROP != 0) {
        procs_0_2->SetROP(printerContext_0_2,OPVP_0_2_ROP_P);
    }

    return (opvp_result_t)r;
}

opvp_result_t OPVPWrapper_0_2::StartDrawImage(opvp_int_t sourceWidth,
    opvp_int_t sourceHeight, opvp_int_t sourcePitch,
    opvp_imageformat_t imageFormat, opvp_int_t destinationWidth,
    opvp_int_t destinationHeight)
{
    int r;
    OPVP_Rectangle rect;
    OPVP_ImageFormat iformat_0_2;
    OPVP_PaintMode paintmode_0_2 = OPVP_paintModeTransparent;
    int depth;

    if (!supportStartDrawImage) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    if (imageFormat == OPVP_IFORMAT_MASK) {
	if (procs_0_2->GetPaintMode != 0) {
	    procs_0_2->GetPaintMode(printerContext_0_2,
	      &paintmode_0_2);
	}
	if (paintmode_0_2 != OPVP_paintModeTransparent) {
	    if (procs_0_2->SetROP != 0) {
		procs_0_2->SetROP(printerContext_0_2,OPVP_0_2_ROP_S);
	    }
	}
	else {
	    if (procs_0_2->SetROP != 0) {
		procs_0_2->SetROP(printerContext_0_2,OPVP_0_2_ROP_OR);
	    }
	}
	depth = 1;
    } else {
	if (procs_0_2->SetROP != 0) {
	    procs_0_2->SetROP(printerContext_0_2,OPVP_0_2_ROP_S);
	}
	depth = colorDepth_0_2[colorSpace];
    }

    OPVP_I2FIX(0,rect.p0.x);
    OPVP_I2FIX(0,rect.p0.y);
    OPVP_I2FIX(destinationWidth,rect.p1.x);
    OPVP_I2FIX(destinationHeight,rect.p1.y);
    if ((unsigned int)imageFormat
      > sizeof(iformat_1_0_to_0_2)/sizeof(OPVP_ImageFormat)) {
	/* illegal image format */
	*opvpErrorNo_0_2 = OPVP_PARAMERROR_0_2;
	return -1;
    }
    iformat_0_2 = iformat_1_0_to_0_2[imageFormat];
    r = procs_0_2->StartDrawImage(printerContext_0_2,
	    sourceWidth,sourceHeight,
	    depth,iformat_0_2,rect);

    return (opvp_result_t)r;
}

opvp_result_t OPVPWrapper_0_2::TransferDrawImage(opvp_int_t count,
    const void *imagedata)
{
    if (!supportTransferDrawImage) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->TransferDrawImage(printerContext_0_2,
      count,(void *)imagedata);
}

opvp_result_t OPVPWrapper_0_2::EndDrawImage()
{
    int r;

    if (!supportEndDrawImage) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    r = procs_0_2->EndDrawImage(printerContext_0_2);

    /* make sure rop is pattern copy */
    if (procs_0_2->SetROP != 0) {
	procs_0_2->SetROP(printerContext_0_2,OPVP_0_2_ROP_P);
    }

    return (opvp_result_t)r;
}

opvp_result_t OPVPWrapper_0_2::StartScanline(opvp_int_t yposition)
{
    if (!supportStartScanline) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StartScanline(printerContext_0_2,yposition);
}

opvp_result_t OPVPWrapper_0_2::Scanline(opvp_int_t nscanpairs,
  const opvp_int_t *scanpairs)
{
    if (!supportScanline) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->Scanline(printerContext_0_2,
      (int)nscanpairs,(int *)scanpairs);
}

opvp_result_t OPVPWrapper_0_2::EndScanline()
{
    if (!supportEndScanline) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndScanline(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::StartRaster(opvp_int_t rasterWidth)
{
    if (!supportStartRaster) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StartRaster(printerContext_0_2,rasterWidth);
}

opvp_result_t OPVPWrapper_0_2::TransferRasterData(opvp_int_t count,
    const opvp_byte_t *data)
{
    if (!supportTransferRasterData) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->TransferRasterData(printerContext_0_2,
      (int)count, (unsigned char *)data);
}

opvp_result_t OPVPWrapper_0_2::SkipRaster(opvp_int_t count)
{
    if (!supportSkipRaster) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->SkipRaster(printerContext_0_2,count);
}

opvp_result_t OPVPWrapper_0_2::EndRaster()
{
    if (!supportEndRaster) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndRaster(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::StartStream()
{
    if (!supportStartStream) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->StartStream(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::TransferStreamData(opvp_int_t count,
  const void *data)
{
    if (!supportTransferStreamData) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->TransferStreamData(printerContext_0_2,
      count,(void *)data);
}

opvp_result_t OPVPWrapper_0_2::EndStream()
{
    if (!supportEndStream) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->EndStream(printerContext_0_2);
}

opvp_result_t OPVPWrapper_0_2::ResetClipPath()
{
    if (!supportResetClipPath) {
	*opvpErrorNo_0_2 = OPVP_NOTSUPPORTED_0_2;
	return -1;
    }
    return (opvp_result_t)procs_0_2->ResetClipPath(printerContext_0_2);
}

/* translate error code */
opvp_int_t OPVPWrapper_0_2::getErrorNo()
{
    switch(*opvpErrorNo_0_2) {
    case OPVP_FATALERROR_0_2:
	return OPVP_FATALERROR; 
	break;
    case OPVP_BADREQUEST_0_2:
	return OPVP_BADREQUEST; 
	break;
    case OPVP_BADCONTEXT_0_2:
	return OPVP_BADCONTEXT; 
	break;
    case OPVP_NOTSUPPORTED_0_2:
	return OPVP_NOTSUPPORTED; 
	break;
    case OPVP_JOBCANCELED_0_2:
	return OPVP_JOBCANCELED; 
	break;
    case OPVP_PARAMERROR_0_2:
	return OPVP_PARAMERROR; 
	break;
    default:
	break;
    }
    /* unknown error no */
    /* return FATALERROR instead */
    return OPVP_FATALERROR;
}

#!/usr/bin/env bash
#
# Test for the searchable PDF (OCR) output of the gstopdf filter: run it
# as a CUPS filter on a raster-only PDF, check the OCR text layer, the
# fallback and that plain PDF output is unchanged, and resolve the
# filter chain to application/vnd.cups-pdf-ocr with the cupsfilter tool
# using the MIME files of this build.
#
# Skipped (exit 77) if gstopdf is built without OCR support or
# Ghostscript's pdfocr8 device, the English Tesseract data, or
# poppler-utils are missing.
#

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="$(cd "${ROOT}/.." && pwd)"
GS="${GS:-gs}"
FILTER="${BUILD_ROOT}/gstopdf"
OCR_TYPE=application/vnd.cups-pdf-ocr

skip() {
  echo "$*; skipping." >&2
  exit 77
}

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[[ -x "${FILTER}" ]] || skip "gstopdf not built"
grep -q "^application/vnd.cups-pdf.*${OCR_TYPE}" \
  "${BUILD_ROOT}/mime/cupsfilters-ghostscript.convs" ||
  skip "built without OCR support (libcupsfilters too old)"

for tool in "${GS}" pdftotext pdfinfo pdffonts; do
  command -v "${tool}" >/dev/null 2>&1 || skip "${tool} not found"
done

"${GS}" -h 2>&1 | tr ' ' '\n' | grep -x pdfocr8 >/dev/null ||
  skip "Ghostscript has no pdfocr8 device"

TESSDATA=""
for dir in "${TESSDATA_PREFIX:-}" \
           /usr/share/tesseract-ocr/5/tessdata \
           /usr/share/tesseract-ocr/4.00/tessdata \
           /usr/share/tesseract/tessdata \
           /usr/share/tessdata \
           /usr/local/share/tessdata; do
  if [[ -n "${dir}" && -r "${dir}/eng.traineddata" ]]; then
    TESSDATA="${dir}"
    break
  fi
done
[[ -n "${TESSDATA}" ]] || skip "Tesseract language data eng.traineddata not found"

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/gstopdf-ocr.XXXXXX")"
cleanup() {
  rm -rf "${WORKDIR}"
}
trap cleanup EXIT

# The "scan": text rendered into a raster-only PDF
INPUT="${WORKDIR}/scan.pdf"
cat > "${WORKDIR}/scan.ps" <<'EOF'
%!PS
/Helvetica-Bold findfont 36 scalefont setfont
72 650 moveto (OPENPRINTING MAKES) show
72 590 moveto (SCANS SEARCHABLE) show
showpage
EOF
"${GS}" -q -dSAFER -dNOPAUSE -dBATCH -sDEVICE=pdfimage8 -r300 \
  -sOutputFile="${INPUT}" "${WORKDIR}/scan.ps"
if pdftotext "${INPUT}" - | grep OPENPRINTING >/dev/null; then
  fail "test input unexpectedly contains extractable text"
fi

EMPTY_TESSDATA="${WORKDIR}/no-tessdata"
mkdir -p "${EMPTY_TESSDATA}"

check_pdf() {
  [[ -s "$1" ]] || fail "$1 is empty"
  local pages
  pages="$(pdfinfo "$1" 2>/dev/null | sed -n 's/^Pages: *//p')"
  [[ "${pages}" == "1" ]] || fail "$1 has ${pages:-no} pages, expected 1"
}

# The OCR text layer uses Tesseract's invisible GlyphLessFont
check_ocr_layer() {
  pdffonts "$1" | grep GlyphLessFont >/dev/null || fail "$1: no OCR text layer"
  pdftotext "$1" - | grep OPENPRINTING >/dev/null || fail "$1: OCR text not found"
}

check_no_ocr_layer() {
  if pdffonts "$1" | grep GlyphLessFont >/dev/null; then
    fail "$1 unexpectedly contains an OCR text layer"
  fi
}

run_filter() {  # input type, final type, options, input file, output file, log
  env -u PPD CONTENT_TYPE="$1" FINAL_CONTENT_TYPE="$2" \
    "${FILTER}" 1 testuser "test" 1 "$3" "$4" > "$5" 2>"$6"
}

echo "1. Searchable PDF: input application/vnd.cups-pdf, final type ${OCR_TYPE}"
OUT="${WORKDIR}/out1.pdf"
run_filter application/vnd.cups-pdf "${OCR_TYPE}" \
  "ocr-language=eng ocr-tessdata-dir=${TESSDATA}" "${INPUT}" "${OUT}" "${WORKDIR}/log1" ||
  { cat "${WORKDIR}/log1" >&2; fail "gstopdf failed"; }
check_pdf "${OUT}"
check_ocr_layer "${OUT}"

echo "2. Same, reading from stdin"
OUT="${WORKDIR}/out2.pdf"
env -u PPD CONTENT_TYPE=application/vnd.cups-pdf FINAL_CONTENT_TYPE="${OCR_TYPE}" \
  "${FILTER}" 1 testuser "test" 1 "ocr-language=eng ocr-tessdata-dir=${TESSDATA}" \
  < "${INPUT}" > "${OUT}" 2>"${WORKDIR}/log2" ||
  { cat "${WORKDIR}/log2" >&2; fail "gstopdf failed on stdin input"; }
check_pdf "${OUT}"
check_ocr_layer "${OUT}"

echo "3. Plain PDF output is unchanged: PostScript to PDF with final type application/pdf"
OUT="${WORKDIR}/out3.pdf"
run_filter application/postscript application/pdf "" "${WORKDIR}/scan.ps" "${OUT}" "${WORKDIR}/log3" ||
  { cat "${WORKDIR}/log3" >&2; fail "gstopdf PostScript to PDF failed"; }
check_pdf "${OUT}"
check_no_ocr_layer "${OUT}"

echo "4. First step of a PostScript -> pdftopdf -> OCR chain must not OCR yet"
OUT="${WORKDIR}/out4.pdf"
run_filter application/postscript "${OCR_TYPE}" "" "${WORKDIR}/scan.ps" "${OUT}" "${WORKDIR}/log4" ||
  { cat "${WORKDIR}/log4" >&2; fail "gstopdf PostScript to PDF failed"; }
check_pdf "${OUT}"
check_no_ocr_layer "${OUT}"

echo "5. Missing language data: fallback to raster-only PDF"
OUT="${WORKDIR}/out5.pdf"
run_filter application/vnd.cups-pdf "${OCR_TYPE}" \
  "ocr-tessdata-dir=${EMPTY_TESSDATA} ocr-fallback=true" "${INPUT}" "${OUT}" "${WORKDIR}/log5" ||
  { cat "${WORKDIR}/log5" >&2; fail "fallback failed"; }
check_pdf "${OUT}"
check_no_ocr_layer "${OUT}"

echo "6. Missing language data, fallback disabled: must fail"
OUT="${WORKDIR}/out6.pdf"
if run_filter application/vnd.cups-pdf "${OCR_TYPE}" \
     "ocr-tessdata-dir=${EMPTY_TESSDATA} ocr-fallback=false" "${INPUT}" "${OUT}" "${WORKDIR}/log6"; then
  fail "gstopdf succeeded without language data"
fi
[[ ! -s "${OUT}" ]] || fail "output written although the job failed"

echo "7. Filter chain resolution with cupsfilter"
CUPS_DATADIR_SYS="$(cups-config --datadir 2>/dev/null || echo /usr/share/cups)"
if ! command -v cupsfilter >/dev/null 2>&1 || [[ ! -r "${CUPS_DATADIR_SYS}/mime/mime.types" ]]; then
  echo "   cupsfilter or the CUPS MIME database not available, skipping this part"
else
  D="${WORKDIR}/cups"
  mkdir -p "${D}/mime" "${D}/filter"
  cp "${CUPS_DATADIR_SYS}/mime/mime.types" "${D}/mime/"
  cp "${BUILD_ROOT}/mime/cupsfilters.types" "${D}/mime/"
  cp "${BUILD_ROOT}/mime/cupsfilters-ghostscript.convs" "${D}/mime/"
  printf 'application/pdf\tapplication/vnd.cups-pdf\t66\tpdftopdf\n' > "${D}/mime/test.convs"
  ln -s "${BUILD_ROOT}/pdftopdf" "${BUILD_ROOT}/gstopdf" "${D}/filter/"
  # cupsfilter always passes a PPD to the filters, so give it a minimal one
  cat > "${D}/test.ppd" <<'EOF'
*PPD-Adobe: "4.3"
*FormatVersion: "4.3"
*FileVersion: "1.0"
*LanguageVersion: English
*LanguageEncoding: ISOLatin1
*PCFileName: "TEST.PPD"
*Manufacturer: "OpenPrinting"
*Product: "(gstopdf OCR test)"
*ModelName: "gstopdf OCR test"
*ShortNickName: "gstopdf OCR test"
*NickName: "gstopdf OCR test"
*PSVersion: "(3010) 0"
*LanguageLevel: "3"
*ColorDevice: True
*DefaultColorSpace: RGB
*FileSystem: False
*Throughput: "1"
*LandscapeOrientation: Plus90
*TTRasterizer: Type42
*cupsVersion: 2.4
*cupsLanguages: "en"
*OpenUI *PageSize/Media Size: PickOne
*OrderDependency: 10 AnySetup *PageSize
*DefaultPageSize: Letter
*PageSize Letter/US Letter: "<</PageSize[612 792]>>setpagedevice"
*CloseUI: *PageSize
*OpenUI *PageRegion/Media Region: PickOne
*OrderDependency: 10 AnySetup *PageRegion
*DefaultPageRegion: Letter
*PageRegion Letter/US Letter: "<</PageSize[612 792]>>setpagedevice"
*CloseUI: *PageRegion
*DefaultImageableArea: Letter
*ImageableArea Letter/US Letter: "0 0 612 792"
*DefaultPaperDimension: Letter
*PaperDimension Letter/US Letter: "612 792"
EOF
  chain="$(CUPS_DATADIR="${D}" CUPS_SERVERBIN="${D}" cupsfilter --list-filters \
             -p "${D}/test.ppd" -i application/pdf -m "${OCR_TYPE}" \
             "${INPUT}" 2>"${WORKDIR}/log7" | tr '\n' ' ')"
  [[ "${chain}" == "pdftopdf gstopdf " ]] ||
    { cat "${WORKDIR}/log7" >&2; fail "unexpected filter chain: ${chain}"; }
  OUT="${WORKDIR}/out7.pdf"
  CUPS_DATADIR="${D}" CUPS_SERVERBIN="${D}" cupsfilter -p "${D}/test.ppd" \
    -i application/pdf -m "${OCR_TYPE}" \
    -o "ocr-tessdata-dir=${TESSDATA}" "${INPUT}" > "${OUT}" 2>"${WORKDIR}/log7" ||
    { cat "${WORKDIR}/log7" >&2; fail "cupsfilter chain failed"; }
  check_pdf "${OUT}"
  check_ocr_layer "${OUT}"

  echo "8. Scan as JPEG through the universal filter"
  if [[ ! -x "${BUILD_ROOT}/universal" ]]; then
    echo "   universal filter not built, skipping this part"
  else
    JPEG="${WORKDIR}/scan.jpg"
    "${GS}" -q -dSAFER -dNOPAUSE -dBATCH -sDEVICE=jpeg -r300 \
      -sOutputFile="${JPEG}" "${WORKDIR}/scan.ps"
    U="${WORKDIR}/cups-universal"
    mkdir -p "${U}/mime" "${U}/filter"
    cp "${CUPS_DATADIR_SYS}/mime/mime.types" "${BUILD_ROOT}/mime/cupsfilters.types" \
       "${BUILD_ROOT}/mime/cupsfilters-universal.convs" "${U}/mime/"
    ln -s "${BUILD_ROOT}/universal" "${U}/filter/"
    chain="$(CUPS_DATADIR="${U}" CUPS_SERVERBIN="${U}" cupsfilter --list-filters \
               -p "${D}/test.ppd" -i image/jpeg -m "${OCR_TYPE}" \
               "${JPEG}" 2>"${WORKDIR}/log8" | tr '\n' ' ')"
    [[ "${chain}" == "universal " ]] ||
      { cat "${WORKDIR}/log8" >&2; fail "unexpected filter chain: ${chain}"; }
    OUT="${WORKDIR}/out8.pdf"
    CUPS_DATADIR="${U}" CUPS_SERVERBIN="${U}" cupsfilter -p "${D}/test.ppd" \
      -i image/jpeg -m "${OCR_TYPE}" \
      -o "ocr-tessdata-dir=${TESSDATA}" "${JPEG}" > "${OUT}" 2>"${WORKDIR}/log8" ||
      { cat "${WORKDIR}/log8" >&2; fail "universal filter chain failed"; }
    check_pdf "${OUT}"
    check_ocr_layer "${OUT}"
  fi
fi

echo "PASS: gstopdf searchable PDF output"
exit 0

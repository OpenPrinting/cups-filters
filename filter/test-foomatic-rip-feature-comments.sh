#!/usr/bin/env bash
#
# Test that foomatic-rip passes DSC PostScript jobs with malformed
# "%%BeginFeature:" and "%% FoomaticRIPOptionSetting:" comments (option
# name or value missing) through instead of crashing.
#
# Skipped (exit 77) if foomatic-rip is not built.
#

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="$(cd "${ROOT}/.." && pwd)"
FILTER="${BUILD_ROOT}/foomatic-rip"

skip() {
  echo "$*; skipping." >&2
  exit 77
}

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[[ -x "${FILTER}" ]] || skip "foomatic-rip not built"

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/foomatic-rip-feature.XXXXXX")"
cleanup() {
  rm -rf "${WORKDIR}"
}
trap cleanup EXIT

# Without a FoomaticRIPCommandLine the job goes through "cat"
cat > "${WORKDIR}/test.ppd" <<'EOF'
*PPD-Adobe: "4.3"
*FormatVersion: "4.3"
*FileVersion: "1.0"
*LanguageVersion: English
*LanguageEncoding: ISOLatin1
*PCFileName: "TEST.PPD"
*Manufacturer: "OpenPrinting"
*Product: "(foomatic-rip test)"
*ModelName: "foomatic-rip test"
*NickName: "foomatic-rip test"
*PSVersion: "(3010) 0"
*LanguageLevel: "3"
*OpenUI *PageSize/Media Size: PickOne
*OrderDependency: 10 AnySetup *PageSize
*DefaultPageSize: Letter
*PageSize Letter/US Letter: "<</PageSize[612 792]>>setpagedevice"
*PageSize A4/A4: "<</PageSize[595 842]>>setpagedevice"
*CloseUI: *PageSize
EOF

n=0
for feature in \
    '%%BeginFeature: *PageSize A4' \
    '%%BeginFeature:' \
    '%%BeginFeature: *PageSize' \
    '%% FoomaticRIPOptionSetting:' \
    '%% FoomaticRIPOptionSetting: PageSize'; do
  n=$((n + 1))
  echo "${n}. ${feature}"
  printf '%%!PS-Adobe-3.0\n%%%%Pages: 1\n%%%%EndComments\n%%%%BeginSetup\n%s\n%%%%EndFeature\n%%%%EndSetup\n%%%%Page: 1 1\nshowpage\n%%%%EOF\n' \
    "${feature}" > "${WORKDIR}/in${n}.ps"
  PPD="${WORKDIR}/test.ppd" "${FILTER}" 1 testuser test 1 "" "${WORKDIR}/in${n}.ps" \
    > "${WORKDIR}/out${n}.ps" 2> "${WORKDIR}/log${n}" ||
    { tail -n 20 "${WORKDIR}/log${n}" >&2; fail "foomatic-rip failed on: ${feature}"; }
  grep -qx showpage "${WORKDIR}/out${n}.ps" || fail "job body missing for: ${feature}"
done

echo "PASS: foomatic-rip malformed feature comments"
exit 0

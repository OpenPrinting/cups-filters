#!/usr/bin/env bash
#
# Test that foomatic-rip refuses a PPD whose command-line option choices are
# not among the allowed values, also when the "*FoomaticRIPOption" line that
# makes the option a command-line option comes after its choices.
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

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/foomatic-rip-cmdline-hash.XXXXXX")"
cleanup() {
  rm -rf "${WORKDIR}"
}
trap cleanup EXIT

MARKER="${WORKDIR}/marker"

# Without a FoomaticRIPCommandLine the renderer command line is "cat" plus the
# option spots, so the choice code below ends up in the shell command line
header() {
  cat <<EOF
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
*OpenUI *Dummy/Dummy: PickOne
*DefaultDummy: Evil
EOF
}

# 1. option declared before its choices, 2. declared after them
{ header
  echo '*FoomaticRIPOption Dummy: enum CmdLine A'
  echo "*Dummy Evil/Evil: \"; touch ${MARKER}; \""
  echo '*CloseUI: *Dummy'
} > "${WORKDIR}/declared-first.ppd"

{ header
  echo "*Dummy Evil/Evil: \"; touch ${MARKER}; \""
  echo '*FoomaticRIPOption Dummy: enum CmdLine A'
  echo '*CloseUI: *Dummy'
} > "${WORKDIR}/declared-last.ppd"

printf '%%!PS-Adobe-3.0\n%%%%Pages: 1\n%%%%EndComments\n%%%%Page: 1 1\nshowpage\n%%%%EOF\n' \
  > "${WORKDIR}/in.ps"

for ppd in declared-first declared-last; do
  echo "${ppd}"
  rm -f "${MARKER}"
  status=0
  PPD="${WORKDIR}/${ppd}.ppd" "${FILTER}" 1 testuser test 1 "" "${WORKDIR}/in.ps" \
    > "${WORKDIR}/${ppd}.out" 2> "${WORKDIR}/${ppd}.log" || status=$?
  # 12 is EXIT_PRNERR_NOTALLOWED
  [[ "${status}" -eq 12 ]] ||
    { tail -n 20 "${WORKDIR}/${ppd}.log" >&2;
      fail "${ppd}: expected exit status 12, got ${status}"; }
  [[ ! -e "${MARKER}" ]] && continue
  fail "${ppd}: the choice code was run by the shell"
done

echo "PASS: foomatic-rip command-line option choice hashes"
exit 0

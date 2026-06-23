#!/bin/sh
# ci/autopkgtest/run.sh
#
# DESTDIR-staging wrapper for the cups-filters downstream autopkgtests.
#
# It points PATH / LD_LIBRARY_PATH / PKG_CONFIG_PATH at a staged install tree
# ($CIROOT, produced by `make stage-ciroot`) and then runs the vendored test
# scripts under ci/autopkgtest/debian-tests/.  The scripts themselves take
# environment overrides (CUPS_FILTERS_FILTERDIR, CUPS_FILTERS_BACKENDDIR,
# CUPS_FILTERS_BINDIR, CUPS_FILTERS_PPD, CUPS_FILTERS_TESTEXTERNAL) that default
# to the system /usr paths but can be redirected into the staging tree, so no
# script hard-codes an absolute path.  That keeps the staged run unprivileged
# and identical on native and QEMU-emulated architectures.
#
# Both execution models are supported:
#   * Staged (CI default): `make test-autopkgtest` exports CIROOT and the
#     CUPS_FILTERS_* overrides, so the tests hit the staged tree, no root.
#   * System (true autopkgtest): the package is installed under /usr and the
#     scripts run with their defaults; CIROOT may be unset/absent -- the wrapper
#     then simply runs against the system paths.
#
# Env in:
#   CIROOT         staging root        (optional; default: $PWD/_ciroot)
#   CIPREFIX       configured prefix   (default: /usr)
#   TOP_BUILDDIR   build tree          (default: $PWD)
#   Any extra exported CUPS_FILTERS_* variables pass straight through.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TESTS_DIR="$SCRIPT_DIR/debian-tests"

: "${CIROOT:=$PWD/_ciroot}"
: "${CIPREFIX:=/usr}"
: "${TOP_BUILDDIR:=$PWD}"

# Prepend the staged tree only when it actually exists.  When it does not, fall
# back to the system install (the genuine autopkgtest case) rather than aborting.
if [ -d "$CIROOT" ]; then
    ROOT="$CIROOT$CIPREFIX"
    MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null \
                || gcc -dumpmachine 2>/dev/null || echo "")
    PATH="$ROOT/bin:$ROOT/sbin:$TOP_BUILDDIR:$TOP_BUILDDIR/.libs:$PATH"
    LD_LIBRARY_PATH="$ROOT/lib${MULTIARCH:+:$ROOT/lib/$MULTIARCH}:$TOP_BUILDDIR/.libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    PKG_CONFIG_PATH="$ROOT/lib/pkgconfig${MULTIARCH:+:$ROOT/lib/$MULTIARCH/pkgconfig}:$ROOT/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PATH LD_LIBRARY_PATH PKG_CONFIG_PATH
    echo "run.sh: using staged tree $CIROOT (prefix $CIPREFIX)"
else
    echo "run.sh: no staging root at $CIROOT -- running against the system install"
fi

if [ "$#" -eq 0 ]; then
    echo "run.sh: usage: run.sh <test-name> [test-name...]" >&2
    exit 2
fi

rc=0
total=0
n_pass=0
n_fail=0
results=""
for name in "$@"; do
    total=$((total + 1))
    script="$TESTS_DIR/$name"
    if [ ! -f "$script" ]; then
        echo "run.sh: no such test: $script" >&2
        n_fail=$((n_fail + 1))
        results="$results
FAIL: $name (not found)"
        rc=1
        continue
    fi
    chmod +x "$script" 2>/dev/null || true
    workdir=$(mktemp -d)
    echo "=== autopkgtest: $name ==="
    if ( cd "$workdir" && "$script" ); then
        echo "=== PASS: $name ==="
        n_pass=$((n_pass + 1))
        results="$results
PASS: $name"
    else
        ec=$?
        echo "=== FAIL: $name (exit $ec) ===" >&2
        n_fail=$((n_fail + 1))
        results="$results
FAIL: $name (exit $ec)"
        rc=1
    fi
    rm -rf "$workdir"
done

echo "============================================================================"
echo "cups-filters autopkgtest summary"
echo "============================================================================"
printf '# TOTAL: %d\n' "$total"
printf '# PASS:  %d\n' "$n_pass"
printf '# FAIL:  %d\n' "$n_fail"
echo "----------------------------------------------------------------------------"
printf '%s\n' "$results" | sed '/^$/d'
echo "============================================================================"

exit $rc

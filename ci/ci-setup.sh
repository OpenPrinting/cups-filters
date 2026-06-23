#!/bin/sh
# ci/ci-setup.sh
#
# CI helper for building cups-filters against several CUPS releases on both
# native and QEMU-emulated runners.  cups-filters is a CUPS 2.x compatibility
# layer (classic filters), so it targets CUPS 2.4.x and 2.5.x only -- NOT
# CUPS 3.x/libcups3, which has no filter-executable concept.  This script
# provides each CUPS build, then the dependency stack (pdfio, libcupsfilters,
# libppd) and finally cups-filters itself against it.
#
# Subcommands:
#   deps                  install build dependencies
#   cups <kind>           provide libcups; <kind> is one of:
#                           system-2x    distro libcups2-dev  (CUPS 2.4.x)
#                           source-2.5.x OpenPrinting/cups@master    (CUPS 2.5.x)
#   pdfio                 build/install the latest released pdfio
#   libcupsfilters <kind> provide libcupsfilters: distro pkg on system-2x,
#                         OpenPrinting/libcupsfilters@master on the source legs
#   libppd <kind>         provide libppd: distro pkg on system-2x,
#                         OpenPrinting/libppd@master on the source legs
#   build-cups-filters    autogen + configure + make + make check (this tree)
#
# Environment knobs:
#   CUPS_KIND   the <kind> above (recorded in logs)
#   EMULATED    "1" when running under QEMU emulation
#
# The script runs as root inside emulation containers and via sudo on native
# runners; it detects which automatically.
set -eu

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"

# Make apt completely non-interactive.  Native GitHub runners ship needrestart,
# whose service-restart prompt otherwise hangs the job forever; the emulated
# containers do not have it, which is why only the native legs stalled.
export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a
export NEEDRESTART_SUSPEND=1

# Source-built CUPS / libs install their .pc files under $prefix/lib/pkgconfig;
# make sure pkg-config (and therefore each configure) can find them.
ma=$(gcc -dumpmachine 2>/dev/null || echo "")
PKG_CONFIG_PATH="/usr/lib/pkgconfig${ma:+:/usr/lib/$ma/pkgconfig}:/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_PATH

# Sibling libraries built from source on the source-CUPS legs (overridable).
LIBCUPSFILTERS_URL="${LIBCUPSFILTERS_URL:-https://github.com/OpenPrinting/libcupsfilters.git}"
LIBCUPSFILTERS_REF="${LIBCUPSFILTERS_REF:-master}"
LIBPPD_URL="${LIBPPD_URL:-https://github.com/OpenPrinting/libppd.git}"
LIBPPD_REF="${LIBPPD_REF:-master}"

apt_install() {
	$SUDO apt-get update --fix-missing -y -o Acquire::Retries=3
	$SUDO apt-get install -y -o Acquire::Retries=3 "$@"
}

cmd_deps() {
	apt_install \
		build-essential autoconf automake libtool pkg-config gettext autopoint \
		autotools-dev cmake git wget tar make gcc g++ file dbus \
		libavahi-client-dev libavahi-common-dev libssl-dev libpam-dev \
		libusb-1.0-0-dev zlib1g-dev libqpdf-dev libexif-dev liblcms2-dev \
		libfontconfig1-dev libfreetype6-dev libcairo2-dev libjpeg-dev libpng-dev \
		libtiff-dev libjxl-dev libpoppler-cpp-dev libdbus-1-dev libopenjp2-7-dev \
		mupdf-tools poppler-utils ghostscript
	# Per-leg: on system-2x the distro libcupsfilters/libppd are used (installed
	# by cmd_libcupsfilters/cmd_libppd); on the source legs those functions
	# remove the distro packages and build from master.  So nothing is removed
	# here unconditionally.
}

# build_autoconf <url> <ref> <submodule-flag> [configure-args...]
build_autoconf() {
	url="$1"; ref="$2"; sub="$3"; shift 3
	echo "ci-setup: building $url @ $ref"
	src="$(mktemp -d)"
	git clone --depth 1 --branch "$ref" $sub "$url" "$src"
	( cd "$src"
	  [ -x ./configure ] || ./autogen.sh
	  ./configure --prefix=/usr "$@" || ./configure --prefix=/usr
	  make -j"$(nproc)"
	  $SUDO make install )
	$SUDO ldconfig || true
}

cmd_cups() {
	kind="$1"
	case "$kind" in
		system-2x)
			apt_install libcups2-dev
			;;
		source-2.5.x)
			# CUPS 2.5 (OpenPrinting/cups master) ships cups.pc and has dropped
			# cups-config; configure detects it via pkg-config.  Force the
			# multiarch libdir so libcups lands on the default linker search path
			# (CUPS otherwise installs into /usr/lib64 on 64-bit hosts).
			build_autoconf https://github.com/OpenPrinting/cups.git master "" \
				--disable-systemd ${ma:+--libdir=/usr/lib/$ma}
			;;
		*)
			echo "ci-setup: unknown cups kind: $kind" >&2; exit 2 ;;
	esac
}

cmd_pdfio() {
	# Build the latest RELEASED pdfio.  libcupsfilters tracks the newest pdfio,
	# so pinning a version goes stale and breaks the build the moment it bumps
	# its requirement; resolve the newest release tag dynamically instead.
	ver=$(git ls-remote --tags --refs https://github.com/michaelrsweet/pdfio.git \
	      | sed 's#.*/v##' | sort -V | tail -1)
	echo "ci-setup: building pdfio $ver (latest release)"
	src="$(mktemp -d)"
	( cd "$src"
	  wget -q "https://github.com/michaelrsweet/pdfio/releases/download/v$ver/pdfio-$ver.tar.gz"
	  tar -xzf "pdfio-$ver.tar.gz"
	  cd "pdfio-$ver"
	  ./configure --prefix=/usr --enable-shared
	  make all
	  $SUDO make install )
	$SUDO ldconfig || true
}

# cmd_libcupsfilters <kind> -- provide libcupsfilters for the leg.
cmd_libcupsfilters() {
	kind="$1"
	case "$kind" in
		system-2x)
			apt_install libcupsfilters-dev
			;;
		source-*)
			# On the source legs never let a pre-shipped libcupsfilters/libppd
			# shadow the source builds under test.
			$SUDO apt-get remove -y libcupsfilters-dev libppd-dev || true
			build_autoconf "$LIBCUPSFILTERS_URL" "$LIBCUPSFILTERS_REF" ""
			;;
		*)
			echo "ci-setup: unknown libcupsfilters kind: $kind" >&2; exit 2 ;;
	esac
}

# cmd_libppd <kind> -- provide libppd for the leg.
cmd_libppd() {
	kind="$1"
	case "$kind" in
		system-2x)
			apt_install libppd-dev
			;;
		source-*)
			build_autoconf "$LIBPPD_URL" "$LIBPPD_REF" ""
			;;
		*)
			echo "ci-setup: unknown libppd kind: $kind" >&2; exit 2 ;;
	esac
}

cmd_build_cups_filters() {
	./autogen.sh
	./configure --prefix=/usr
	make -j"$(nproc)" V=1

	# Report which CUPS the configure step actually selected.
	echo "ci-setup: configured against:"
	grep -iE "libcups|cups-config|cups api|cups version" config.log 2>/dev/null | head || true

	# cups-filters' only check program is test-external; make check just compiles
	# it.  The real functional tests live in the autopkgtest suite, run by the
	# workflow via `make test-autopkgtest`.
	make check V=1 VERBOSE=1 || { test -f test-suite.log && cat test-suite.log; exit 1; }
}

case "${1:-}" in
	deps)               cmd_deps ;;
	cups)               shift; cmd_cups "$@" ;;
	pdfio)              cmd_pdfio ;;
	libcupsfilters)     shift; cmd_libcupsfilters "$@" ;;
	libppd)             shift; cmd_libppd "$@" ;;
	build-cups-filters) cmd_build_cups_filters ;;
	*)
		echo "usage: ci-setup.sh {deps | cups <kind> | pdfio | libcupsfilters <kind> | libppd <kind> | build-cups-filters}" >&2
		exit 2 ;;
esac

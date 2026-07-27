#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# install-pjsip.sh — build and install PJSIP (pjproject) into /usr/local
#
# PJSIP is not packaged for Ubuntu 24.04. The sip-demo subproject (see
# sip-demo/) needs it, so this script downloads the source tarball,
# configures it for a minimal SIP-signalling build, compiles and installs
# it. The install drops `libpjproject.pc` into /usr/local/lib/pkgconfig/
# which is how the demo's CMakeLists.txt finds it.
#
# Idempotent: if a recent enough PJSIP is already installed, it just exits.
# ---------------------------------------------------------------------------
set -euo pipefail

PJ_VERSION="${PJ_VERSION:-2.14.1}"
PREFIX="${PREFIX:-/usr/local}"
BUILD_DIR="${BUILD_DIR:-/tmp/pjproject-build}"

# ---- Fast-path: already installed? --------------------------------------
if pkg-config --exists libpjproject 2>/dev/null; then
    installed="$(pkg-config --modversion libpjproject)"
    echo "PJSIP ${installed} is already installed (pkg-config: libpjproject)."
    echo "Set PJ_VERSION and re-run if you want to upgrade."
    exit 0
fi

# ---- Sanity ----------------------------------------------------------------
if [ "$(id -u)" -ne 0 ]; then
    echo "This script needs to run as root (it installs into ${PREFIX})." >&2
    echo "Try:  sudo $0" >&2
    exit 1
fi

# ---- Tools we need ---------------------------------------------------------
echo "==> Installing build dependencies via apt"
apt-get update
apt-get install -y \
    build-essential \
    pkg-config \
    curl \
    libssl-dev \
    libasound2-dev \
    uuid-dev

# ---- Fetch the source tarball ---------------------------------------------
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

TARBALL="pjproject-${PJ_VERSION}.tar.gz"
URL="https://github.com/pjsip/pjproject/archive/refs/tags/${PJ_VERSION}.tar.gz"

if [ ! -f "${TARBALL}" ]; then
    echo "==> Downloading PJSIP ${PJ_VERSION} from ${URL}"
    curl -fL --retry 3 -o "${TARBALL}" "${URL}"
fi

SRCDIR="${BUILD_DIR}/pjproject-${PJ_VERSION}"
if [ ! -d "${SRCDIR}" ]; then
    echo "==> Unpacking"
    tar -xzf "${TARBALL}"
fi

# ---- Configure -------------------------------------------------------------
cd "${SRCDIR}"
echo "==> Configuring (--prefix=${PREFIX})"
# We use --disable-* for everything related to media codecs / sound: this
# build only signals SIP for us, Pulse owns the media. Position-independent
# code so we can link the static libs into our shared/PIE binary.
export CFLAGS="${CFLAGS:--O2 -fPIC -DPJ_AUTOCONF=1}"
export CXXFLAGS="${CXXFLAGS:--O2 -fPIC}"
./configure \
    --prefix="${PREFIX}" \
    --disable-sound \
    --disable-resample \
    --disable-video \
    --disable-opencore-amr \
    --disable-g711-codec \
    --disable-l16-codec \
    --disable-gsm-codec \
    --disable-g722-codec \
    --disable-g7221-codec \
    --disable-speex-codec \
    --disable-speex-aec \
    --disable-ilbc-codec \
    --disable-libsamplerate \
    --disable-resample-dll \
    --disable-sdl \
    --disable-ffmpeg \
    --disable-v4l2 \
    --disable-openh264 \
    --disable-libwebrtc \
    --disable-libyuv

# ---- Build + install -------------------------------------------------------
echo "==> Building"
make dep
make -j"$(nproc)"

echo "==> Installing into ${PREFIX}"
make install

ldconfig

echo
echo "==> Done. Verify with:  pkg-config --modversion libpjproject"

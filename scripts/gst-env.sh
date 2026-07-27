#!/bin/bash

(return 0 2>/dev/null) && sourced=1 || sourced=0

# from: https://www.linuxquestions.org/questions/programming-9/bash-script-return-full-path-and-filename-680368/page3.html
function abspath {
  if [[ -d $1 ]]; then
    pushd "$1" >/dev/null
    pwd
    popd >/dev/null
  elif [[ -e $1 ]]; then
    pushd "$(dirname "$1")" >/dev/null
    echo "$(pwd)/$(basename "$1")"
    popd >/dev/null
  else
    echo "$1" does not exist! >&2
    return 127
  fi
}

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

if [ $# -gt 1 ]; then
  echo 1>&2 "Usage: source gst-env.sh [prefix-dir]"
  echo 1>&2 "Example: source gst-env.sh /build/linux-x86_64/__install__"
  echo 1>&2 "Or: source /build/linux-x86_64/__install__/gst-env.sh"
  return
fi

if [ $# -eq 0 ]; then
  PREFIX_DIR=${DIR}
fi

if [ $# -eq 1 ]; then
  PREFIX_DIR=$(abspath $(
    cd "$(dirname "$1")"
    pwd
  )/$(basename "$1"))
fi

LIBDIR=${PREFIX_DIR}/lib
BINDIR=${PREFIX_DIR}/bin
TESTDIR=${PREFIX_DIR}/tests
SHAREDIR=${PREFIX_DIR}/share
if ! [ -d "$PREFIX_DIR" ]; then
  echo "$PREFIX_DIR does not exist!"
  return
fi

if ! [ -d "$LIBDIR" ]; then
  echo "$LIBDIR does not exist!"
  return
fi

if ! [ -d "$BINDIR" ]; then
  echo "$BINDIR does not exist!"
  return
fi

if ! [ -d "$TESTDIR" ]; then
  echo "$TESTDIR does not exist!"
  return
fi

PYTHON3=($(find ${LIBDIR} -name "python3*" -type d))
if ! [ -d "$PYTHON3" ]; then
  echo "Can't find python3.x directory!"
else
  export PYTHONPATH=${PYTHONPATH}:${PYTHON3}/site-packages
  echo "setting: PYTHONPATH=$PYTHONPATH"
fi

OS="$(uname)"

ISA=sse
for i in avx avx2 avx512 neon asimd; do
  case $OS in
  'Linux')
    if grep -qi $i /proc/cpuinfo; then
      ISA=$i
    fi
    ;;

  'Darwin')
    if sysctl -n machdep.cpu | grep -qi $i; then
      ISA=$i
    fi
    ;;
  esac
done

# asimd "means" NEON on aarch64
case $ISA in
'asimd')
  ISA=neon
  ;;
'sse')
  if sysctl -a hw.optional.neon; then
    ISA=neon
  fi
  ;;
esac

case $OS in
'Linux')
  export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${LIBDIR}
  echo "setting: LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
  ;;
'Darwin')
  export DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:${LIBDIR}
  echo "setting: DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH"
  ;;
esac

export GST_PLUGIN_PATH=${LIBDIR}/gstreamer-1.0:${LIBDIR}/gstreamer-1.0-${ISA}
export PEX_GST_PLUGIN_PATH=${LIBDIR}/gstreamer-1.0
export PEX_TESTS_DATA_PATH=${TESTDIR}/data
export GI_TYPELIB_PATH=${LIBDIR}/girepository-1.0
export PKG_CONFIG_PATH=${LIBDIR}/pkgconfig
export PEX_BASE_PATH=${PREFIX_DIR}
export PATH=:${PATH}:${BINDIR}

# Derive BUILD_DIR and BUILD_NAME from PEX_BASE_PATH unless the caller
# has already provided them. Sourced scripts that unconditionally stomp
# on environment variables are a fun kind of Heisenbug in CI.
# e.g. PEX_BASE_PATH=/build/linux-x86_64/__install__
#      BUILD_DIR=/build/linux-x86_64
#      BUILD_NAME=linux-x86_64
: "${BUILD_DIR:=$(dirname "$PEX_BASE_PATH")}"
: "${BUILD_NAME:=$(basename "$BUILD_DIR")}"
export BUILD_DIR
export BUILD_NAME

echo "setting: GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
echo "setting: PEX_GST_PLUGIN_PATH=$PEX_GST_PLUGIN_PATH"
echo "setting: PEX_TESTS_DATA_PATH=$PEX_TESTS_DATA_PATH"
echo "setting: GI_TYPELIB_PATH=$GI_TYPELIB_PATH"
echo "setting: PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
echo "setting: PEX_BASE_PATH=$PEX_BASE_PATH"
echo "setting: BUILD_DIR=$BUILD_DIR"
echo "setting: BUILD_NAME=$BUILD_NAME"
echo "adding to PATH: $BINDIR"

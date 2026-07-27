#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# uninstall-pjsip.sh — remove a PJSIP (pjproject) install done by
# scripts/install-pjsip.sh.
#
# PJSIP's `make install` drops files into ${PREFIX} (default /usr/local):
#   * lib/libpj*.a                       — static libraries (arch-suffixed)
#   * lib/pkgconfig/libpjproject.pc      — pkg-config file
#   * include/pj/, pjlib.h, pjlib-util/, pjlib-util.h,
#     pjmedia/, pjmedia.h, pjmedia-audiodev/, pjmedia-codec/,
#     pjmedia-codec.h, pjmedia-videodev/, pjnath/, pjnath.h,
#     pjsip/, pjsip.h, pjsip-simple/, pjsip-ua/, pjsua-lib/,
#     pjsua2/, pjsua2.hpp                — public headers
#
# This script removes exactly those (and nothing else under ${PREFIX}). It
# does NOT touch the apt-installed build deps (build-essential, libssl-dev,
# uuid-dev, ...) — those are harmless to leave in place.
#
# Pass --dry-run to print what would be removed without actually deleting.
# ---------------------------------------------------------------------------
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
DRY_RUN=0
for arg in "$@"; do
    case "$arg" in
        --dry-run|-n) DRY_RUN=1 ;;
        -h|--help)
            # Print the header comment block (everything from the opening
            # `# ---` line up to and including the matching closing one).
            sed -n '/^# ---/,/^# ---/p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

if [ "$(id -u)" -ne 0 ] && [ "${DRY_RUN}" -eq 0 ]; then
    echo "This script needs to run as root (it removes files under ${PREFIX})." >&2
    echo "Try:  sudo $0" >&2
    exit 1
fi

# Report what's currently installed, if anything.
if pkg-config --exists libpjproject 2>/dev/null; then
    echo "==> Found PJSIP $(pkg-config --modversion libpjproject) via pkg-config"
else
    echo "==> No libpjproject.pc found via pkg-config; will still sweep ${PREFIX} for stale files"
fi

# Collect everything we plan to remove. Globs that don't match anything are
# silently dropped (nullglob) so a partial install doesn't make us error.
shopt -s nullglob

targets=(
    # pkg-config
    "${PREFIX}/lib/pkgconfig/libpjproject.pc"

    # static libs (PJSIP suffixes them with the autoconf target triple, e.g.
    # libpj-x86_64-unknown-linux-gnu.a, so glob them all)
    "${PREFIX}"/lib/libpj*.a

    # public headers — directories and top-level umbrella headers
    "${PREFIX}/include/pj"
    "${PREFIX}/include/pjlib-util"
    "${PREFIX}/include/pjmedia"
    "${PREFIX}/include/pjmedia-audiodev"
    "${PREFIX}/include/pjmedia-codec"
    "${PREFIX}/include/pjmedia-videodev"
    "${PREFIX}/include/pjnath"
    "${PREFIX}/include/pjsip"
    "${PREFIX}/include/pjsip-simple"
    "${PREFIX}/include/pjsip-ua"
    "${PREFIX}/include/pjsua-lib"
    "${PREFIX}/include/pjsua2"
    "${PREFIX}/include/pjlib.h"
    "${PREFIX}/include/pjlib-util.h"
    "${PREFIX}/include/pjmedia.h"
    "${PREFIX}/include/pjmedia-codec.h"
    "${PREFIX}/include/pjnath.h"
    "${PREFIX}/include/pjsip.h"
    "${PREFIX}/include/pjsua2.hpp"
)

found_any=0
for t in "${targets[@]}"; do
    if [ -e "$t" ] || [ -L "$t" ]; then
        found_any=1
        if [ "${DRY_RUN}" -eq 1 ]; then
            echo "would remove: $t"
        else
            echo "removing: $t"
            rm -rf -- "$t"
        fi
    fi
done

if [ "${found_any}" -eq 0 ]; then
    echo "Nothing to remove — no PJSIP files found under ${PREFIX}."
    exit 0
fi

if [ "${DRY_RUN}" -eq 1 ]; then
    echo
    echo "Dry run complete. Re-run without --dry-run to actually delete."
    exit 0
fi

# Refresh the dynamic linker cache (cheap, and PJSIP can be built as .so).
ldconfig || true

echo
echo "==> Done. PJSIP removed from ${PREFIX}."
echo "    You can now re-run scripts/install-pjsip.sh to install a fresh copy."

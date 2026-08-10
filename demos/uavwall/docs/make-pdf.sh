#!/bin/sh
# make-pdf.sh — render the HTML documents in this folder to PDF.
#
#     ./demos/uavwall/docs/make-pdf.sh            # both
#     ./demos/uavwall/docs/make-pdf.sh setup      # just one
#
# Headless Chrome is used rather than a print dialog so the result is
# reproducible and the page size comes from the @page rule in each document
# (A4 for setup, 1600x900 slides for the deck). --no-pdf-header-footer keeps
# Chrome from stamping a URL and date across the artwork.

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

[ -x "$CHROME" ] || {
    echo "error: Chrome not found. Set CHROME=/path/to/chrome" >&2
    exit 1
}

render () {
    src="$DIR/$1.html"
    out="$DIR/$2.pdf"
    [ -f "$src" ] || { echo "error: no $src" >&2; exit 1; }
    "$CHROME" --headless --disable-gpu --no-sandbox \
        --no-pdf-header-footer \
        --print-to-pdf="$out" "file://$src" >/dev/null 2>&1
    [ -s "$out" ] || { echo "error: produced nothing for $1" >&2; exit 1; }
    echo "  $(basename "$out")  ($(du -h "$out" | awk '{print $1}'))"
}

case "${1:-all}" in
    setup) render setup uavwall-setup ;;
    deck)  render deck  uavwall-demo ;;
    all)   render setup uavwall-setup; render deck uavwall-demo ;;
    *)     echo "usage: $0 [setup|deck|all]" >&2; exit 1 ;;
esac

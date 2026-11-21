#!/usr/bin/env bash
#
# Builds gtaVI.nro and the ready to install zip archive.
#
#   ./build.sh
#
# DEVKITPRO defaults to /opt/devkitpro and can be overridden:
#   DEVKITPRO=/path/to/devkitpro ./build.sh

set -euo pipefail

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPRO
export DEVKITA64="${DEVKITA64:-${DEVKITPRO}/devkitA64}"
export PATH="${DEVKITA64}/bin:${DEVKITPRO}/tools/bin:${PATH}"

if [ ! -d "${DEVKITPRO}/libnx" ]; then
    echo "error: devkitPro not found at ${DEVKITPRO}" >&2
    echo "       install it from https://devkitpro.org/wiki/Getting_Started" >&2
    exit 1
fi

make clean
make -j"$(nproc 2>/dev/null || echo 2)"
make dist

echo
echo "Artifacts:"
echo "  $(pwd)/gtaVI.nro"
echo "  $(pwd)/dist/gtaVI-1.3.0.zip  (contient switch/gtaVI/)"

#!/usr/bin/env bash
# build.sh — configure and build InkscapeVectorizerFull
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

# Dependency check
missing=""
for pkg in glibmm-2.4 gdkmm-3.0 2geom libxml-2.0; do
    if ! pkg-config --exists "$pkg"; then
        missing="$missing $pkg"
    fi
done
if ! find /usr/include /usr/local/include -name potracelib.h 2>/dev/null | grep -q .; then
    missing="$missing potrace"
fi

if [ -n "$missing" ]; then
    echo "ERROR: missing development packages:$missing"
    echo "Install them with (Debian/Ubuntu):"
    echo "  sudo apt install build-essential cmake pkg-config libglibmm-2.4-dev libgdk-pixbuf-2.0-dev libgtkmm-3.0-dev libpotrace-dev lib2geom-dev libxml2-dev"
    echo "or (Fedora):"
    echo "  sudo dnf install gcc-c++ cmake pkgconf-pkg-config glibmm24-devel gdk-pixbuf2-devel gtkmm30-devel potrace-devel lib2geom-devel libxml2-devel"
    exit 1
fi

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$@"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

echo
echo "Build complete:"
ls -1 "$BUILD_DIR"/trace-bitmap* 2>/dev/null || true
echo
echo "Run tests with: ctest --test-dir $BUILD_DIR --output-on-failure"

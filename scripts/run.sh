#!/usr/bin/env bash
# run.sh — quick helpers to run the CLI / GUI / tests
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="build"

if [ ! -x "$BUILD_DIR/trace-bitmap" ]; then
    echo "Not built yet. Run ./scripts/build.sh first."
    exit 1
fi

case "${1:-help}" in
    cli)
        shift
        exec "$BUILD_DIR/trace-bitmap" "$@"
        ;;
    gui)
        shift
        exec "$BUILD_DIR/trace-bitmap-gui" "$@"
        ;;
    test)
        ctest --test-dir "$BUILD_DIR" --output-on-failure
        ;;
    help|*)
        cat <<EOF
Usage: ./scripts/run.sh <command>

  cli <args>   Run the CLI, e.g.:
               ./scripts/run.sh cli --mode color examples/sample_input.png out.svg
  gui          Launch the GTK3 GUI
  test         Run the test suite
EOF
        ;;
esac

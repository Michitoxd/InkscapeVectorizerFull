#!/usr/bin/env bash
# install.sh — install InkscapeVectorizerFull binaries and docs
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cd "$(dirname "$0")/.."

PREFIX="${1:-/usr/local}"

if [ ! -x build/trace-bitmap ]; then
    echo "Not built yet. Run ./scripts/build.sh first."
    exit 1
fi

if [ "$PREFIX" != "$HOME/.local" ] && [ "$(id -u)" != 0 ]; then
    echo "Installing to $PREFIX requires root; falling back to $HOME/.local"
    PREFIX="$HOME/.local"
fi

install -Dm755 build/trace-bitmap        "$PREFIX/bin/trace-bitmap"
[ -x build/trace-bitmap-gui ] && install -Dm755 build/trace-bitmap-gui "$PREFIX/bin/trace-bitmap-gui"
install -Dm644 README.md                 "$PREFIX/share/doc/inkscapevectorizerfull/README.md"
install -Dm644 LICENSE                   "$PREFIX/share/doc/inkscapevectorizerfull/LICENSE"
install -Dm644 NOTICE                    "$PREFIX/share/doc/inkscapevectorizerfull/NOTICE"

echo "Installed to $PREFIX"
echo "Try: trace-bitmap --help"

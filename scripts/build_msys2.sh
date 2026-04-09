#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ -z "${RACK_DIR:-}" ]]; then
  echo "RACK_DIR is not set. Export it before running this script."
  exit 1
fi

if [[ ! -f "${RACK_DIR}/plugin.mk" ]]; then
  echo "Invalid RACK_DIR: ${RACK_DIR}"
  echo "Expected to find ${RACK_DIR}/plugin.mk"
  exit 1
fi

echo "[krono] Using RACK_DIR=${RACK_DIR}"
echo "[krono] Ensuring required MSYS2 packages..."
# MINGW64 (msvcrt) matches most Rack + libRack Windows builds better than UCRT64-only GCC.
pacman -S --needed --noconfirm base-devel mingw-w64-x86_64-toolchain jq

echo "[krono] Configuring MINGW64 toolchain..."
export PATH="/mingw64/bin:/usr/bin:${PATH}"
export CC="gcc"
export CXX="g++"

echo "[krono] Building plugin..."
cd "${PLUGIN_DIR}"
make -j"$(nproc)"

echo "[krono] Build complete."

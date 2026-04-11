#!/usr/bin/env bash
# Usage: install_to_rack_msys2.sh <PLUGIN_DIR_UNIX> <PLUGINS_WIN_DIR_UNIX> <RACK_DIR_UNIX> <slug>
# Matches official `make install` from Rack SDK plugin.mk: copy .vcvplugin into plugins-win-x64.
# Rack extracts and loads it on launch (see VCV manual "Installing plugins").
set -euo pipefail

PLUGIN_DIR="${1:?plugin dir}"
PLUGINS_WIN="${2:?plugins-win-x64 parent}"
RACK_DIR="${3:?RACK_DIR}"
SLUG="${4:?slug}"

export MSYSTEM="${MSYSTEM:-MINGW64}"
export CHERE_INVOKING=1
export PATH="/mingw64/bin:/usr/bin:${PATH}"
export RACK_DIR
export CC="${CC:-gcc}"
export CXX="${CXX:-g++}"

cd "${PLUGIN_DIR}"
make dist

mkdir -p "${PLUGINS_WIN}"

shopt -s nullglob
archives=( "${PLUGIN_DIR}/dist/"*.vcvplugin )
if [[ ${#archives[@]} -eq 0 ]]; then
  echo "[krono] ERROR: no .vcvplugin in dist/"
  exit 1
fi
last="${archives[$(( ${#archives[@]} - 1 ))]}"

# Drop stale copies (old version / manual extract).
rm -rf "${PLUGINS_WIN}/${SLUG}"
rm -f "${PLUGINS_WIN}/${SLUG}"-*.vcvplugin

cp -f "${last}" "${PLUGINS_WIN}/"
echo "[krono] Installed package (Rack extracts on launch): ${PLUGINS_WIN}/$(basename "${last}")"

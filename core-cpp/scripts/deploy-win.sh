#!/usr/bin/env bash
# deploy-win.sh — make a wled-pc-rgb Windows build self-contained.
#
# Bundles, next to the .exe:
#   - Qt plugins (platforms/styles/tls/imageformats) via windeployqt, and
#   - the FULL transitive DLL closure (Qt6* + MinGW runtime + the third-party
#     libs Qt pulls in: libpcre2, libzstd, libpng, libharfbuzz, ...) via ldd.
#
# windeployqt alone is not enough for MSYS2/MinGW Qt — it omits the non-Qt
# dependency DLLs, so the exe still fails with STATUS_DLL_NOT_FOUND. The ldd
# pass closes that gap. Run inside the MSYS2 UCRT64 environment.
#
# Usage: deploy-win.sh <path-to-exe>
set -uo pipefail

EXE_IN="${1:?usage: deploy-win.sh <path-to-exe>}"
EXE="$(cygpath -u "$EXE_IN" 2>/dev/null || echo "$EXE_IN")"
DEST="$(dirname "$EXE")"

# Qt plugins are loaded at runtime (not listed as imports) → windeployqt.
if command -v windeployqt6 >/dev/null 2>&1; then
    windeployqt6 --no-translations --no-opengl-sw "$EXE" >/dev/null 2>&1 || true
elif command -v windeployqt >/dev/null 2>&1; then
    windeployqt --no-translations --no-opengl-sw "$EXE" >/dev/null 2>&1 || true
fi

# Full DLL closure from the toolchain prefix (ucrt64/mingw*). ldd resolves
# transitively, so this catches Qt's own dependencies too.
ldd "$EXE" | awk '/=> \/(ucrt64|mingw64|mingw32|clang64)\// {print $3}' | sort -u |
    while IFS= read -r dll; do
        if [ -f "$dll" ]; then cp -u "$dll" "$DEST/"; fi
    done

echo "deploy-win: $(ls "$DEST"/*.dll 2>/dev/null | wc -l) DLLs bundled in $DEST"

#!/usr/bin/env bash
# Super Ryu World 1 native PC port - source setup.
#
# 1. Fetches the pinned upstream SuperMarioWorldRecomp (+ snesrecomp etc.)
#    git submodules.
# 2. Applies the SRW1 patches and copies the SRW1 overlay sources in.
# 3. Optionally fetches the libretro GLSL shaders (see fetch_shaders.sh).
# 4. Optionally verifies YOUR OWN ROMs and regenerates the recompiled C.
#
# No ROM, patch, or ROM-derived data ships with this repository.
#
# Usage:
#   scripts/setup.sh [--smw PATH] [--srw1 PATH] [--no-shaders]
#     --smw PATH   clean, headerless Super Mario World (USA) ROM
#     --srw1 PATH  patched Super Ryu World 1 'Definitive' ROM (2 MB LoROM)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UP="$ROOT/SuperMarioWorldRecomp"
SMW_SHA=0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b
SRW1_SHA=d7c20cdf42e66a9b81d49359affd4e3548de5855444faceccfac8a1c845bf57f
SMW_ROM=""; SRW1_ROM=""; SHADERS=1
while [ $# -gt 0 ]; do
  case "$1" in
    --smw) SMW_ROM="$2"; shift 2 ;;
    --srw1) SRW1_ROM="$2"; shift 2 ;;
    --no-shaders) SHADERS=0; shift ;;
    -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "setup.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done

sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

echo "=== Fetching upstream submodules ==="
cd "$ROOT"
git submodule update --init --recursive

apply_once() { # dir patch
  if git -C "$1" apply --reverse --check "$2" >/dev/null 2>&1; then
    echo "already applied: $(basename "$2")"
  else
    git -C "$1" apply --whitespace=nowarn "$2"; echo "applied: $(basename "$2")"
  fi
}
echo "=== Applying SRW1 patches ==="
apply_once "$UP" "$ROOT/patches/SuperMarioWorldRecomp-srw1.patch"
apply_once "$UP/snesrecomp" "$ROOT/patches/snesrecomp-srw1.patch"
cp "$ROOT"/overlay/src/* "$UP/src/"
cp "$ROOT"/overlay/tools/regen_srw1.sh "$UP/tools/"
chmod +x "$UP/tools/regen_srw1.sh"
mkdir -p "$UP/recomp/srw1"
cp "$ROOT"/overlay/recomp/srw1/* "$UP/recomp/srw1/"

if [ "$SHADERS" = 1 ]; then
  echo "=== Fetching libretro GLSL shaders ==="
  bash "$ROOT/scripts/fetch_shaders.sh"
fi

if [ -n "$SMW_ROM" ]; then
  [ "$(sha "$SMW_ROM")" = "$SMW_SHA" ] || { echo "ERROR: $SMW_ROM is not a clean headerless SMW (USA) ROM (sha256 mismatch)" >&2; exit 1; }
  cp "$SMW_ROM" "$UP/smw.sfc"
  echo "=== Regenerating stock SMW C (required by the build) ==="
  (cd "$UP" && bash tools/regen.sh --stock --no-tests)
fi
if [ -n "$SRW1_ROM" ]; then
  [ "$(sha "$SRW1_ROM")" = "$SRW1_SHA" ] || { echo "ERROR: $SRW1_ROM does not match the expected patched Super Ryu World 1 sha256" >&2; exit 1; }
  cp "$SRW1_ROM" "$UP/srw1.sfc"
  echo "=== Regenerating Super Ryu World 1 C ==="
  (cd "$UP" && SRW1_ROM=srw1.sfc bash tools/regen_srw1.sh)
fi
echo "=== Setup done. See README.md for the build steps. ==="

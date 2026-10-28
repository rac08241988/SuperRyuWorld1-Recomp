#!/usr/bin/env bash
# Build the GitHub Release asset for Windows x64:
#   dist/SuperRyuWorld1-SNESRecomp-windows-x64-v<VERSION>.zip
#
# The zip is a one-click SETUP PACK, not the game. It holds:
#   - SuperRyuWorld1SNESRecomp.exe : the setup host (built with
#     -DSNESRECOMP_SETUP_HOST=ON, no recompiled game code inside)
#   - the source tree it needs to recompile (this repo's patched
#     SuperMarioWorldRecomp + snesrecomp + recomp-ui, minus everything
#     ROM-derived or stock-SMW-only)
#   - a prebuilt Windows snesrecomp-analyze.exe (players have no Rust)
#   - shaders/ (libretro/glsl-shaders presets, third-party, own licenses)
# On first run the player picks their own ROM; the launcher downloads a
# portable compiler into toolchain/ beside the exe, generates the C and
# rebuilds the game into build/ -- all writes stay inside the unzipped folder.
#
# Usage (Linux, after scripts/setup.sh --no-shaders or with shaders):
#   scripts/make_release_zip.sh [VERSION]
# Needs: cmake, ninja, x86_64-w64-mingw32-gcc/g++, cargo with the
# x86_64-pc-windows-gnu target, python3, zip, git.
set -euo pipefail
trap 'echo "make_release_zip: failed at line ${LINENO}: ${BASH_COMMAND}" >&2' ERR
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="$ROOT/SuperMarioWorldRecomp"
VERSION="${1:-1.0.0}"
EXE=SuperRyuWorld1SNESRecomp
NAME="SuperRyuWorld1-SNESRecomp-windows-x64-v${VERSION}"
BUILD="${BUILD_DIR:-$ROOT/.build/setup-win64}"
WORK="$ROOT/.build/stage"
OUT="$ROOT/dist/$NAME.zip"

[ -f "$UP/tools/srw1_generate.py" ] || { echo "run scripts/setup.sh first" >&2; exit 1; }
[ -d "$ROOT/shaders/presets" ] || bash "$ROOT/scripts/fetch_shaders.sh"

# 1. Setup host (no game code): empty generated-code dir.
EMPTY_GEN="$ROOT/.build/empty-gen"; rm -rf "$EMPTY_GEN"; mkdir -p "$EMPTY_GEN"
cmake -S "$UP" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DSMW_SRW1_PACK=ON -DSNESRECOMP_SETUP_HOST=ON \
  -DSMW_SRW1_GEN_DIR="$EMPTY_GEN" ${SDL3_SRC:+-DFETCHCONTENT_SOURCE_DIR_SDL3="$SDL3_SRC"}
cmake --build "$BUILD" --target "$EXE"
[ -f "$BUILD/$EXE.exe" ] || { echo "setup host exe missing" >&2; exit 1; }

# 2. Native analyzer for Windows, stamped with the crate's source digest.
RS="$UP/snesrecomp/recompiler-rs"
( cd "$RS" && CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="${CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER:-x86_64-w64-mingw32-gcc}" \
    cargo --locked build --release --bin snesrecomp-analyze --target x86_64-pc-windows-gnu )
ANALYZER="$RS/target/x86_64-pc-windows-gnu/release/snesrecomp-analyze.exe"

# 3. Stage tracked (+ patch-added / overlay) source files.
rm -rf "$WORK"; mkdir -p "$WORK"
list_repo() { # repo-dir prefix
  git -C "$1" ls-files -co --exclude-standard | sed "s#^#$2#"
}
{
  list_repo "$UP" ""
  for sub in snesrecomp recomp-ui; do list_repo "$UP/$sub" "$sub/"; done
} | grep -vE '^(snesrecomp|recomp-ui)$' \
  | grep -vE '(^|/)\.github/' \
  | grep -vE '^src/gen(-[^/]*)?/|^src/\.gen' \
  | grep -vE '(^|/)funcs\.h$' \
  | grep -viE '\.(sfc|smc|swc|fig|srm|ips|bps|ups)$' \
  | grep -vE '^recomp/launcher/boxart\.tga$|^recomp/disassembly-layout\.json$' \
  | grep -vE '^third_party/(SMWDisX|asar)(/|$)|^docs/|^tests?/|^mods/|^packages/|^dist/|^build|^_triage/' \
  | grep -vE '^[^/]+\.md$|^smw\.sln$|^lz2_ref\.py$|^\.mcp\.json$|^\.git(ignore|modules|attributes)$' \
  | grep -vE '^(snesrecomp|recomp-ui)/(tests|docs)/' \
  | grep -vE '^snesrecomp/recompiler-rs/target/' \
  | sort -u > "$ROOT/.build/pack-files.txt"
( cd "$UP" && while IFS= read -r f; do [ -f "$f" ] && printf '%s\0' "$f"; done < "$ROOT/.build/pack-files.txt" \
    | xargs -0 cp --parents -t "$WORK" )

# 4. Pack extras.
: > "$WORK/SRW1_PACK"
cp "$BUILD/$EXE.exe" "$WORK/"
for f in "$BUILD"/*.dll; do [ -e "$f" ] && cp "$f" "$WORK/"; done
[ -d "$BUILD/assets" ] && cp -r "$BUILD/assets" "$WORK/"
cp -r "$ROOT/shaders" "$WORK/shaders"
[ -f "$UP/keybinds.ini" ] && cp "$UP/keybinds.ini" "$WORK/" || true
mkdir -p "$WORK/snesrecomp/recompiler-rs/target/release"
cp "$ANALYZER" "$WORK/snesrecomp/recompiler-rs/target/release/snesrecomp-analyze.exe"
python3 - "$UP/snesrecomp/tools" "$WORK/snesrecomp" <<'PY'
import sys, pathlib
sys.path.insert(0, sys.argv[1])
import native_analyzer_identity as n
sdk = pathlib.Path(sys.argv[2])
exe = sdk / "recompiler-rs/target/release/snesrecomp-analyze.exe"
d = n.source_digest(sdk / "recompiler-rs")
if d:
    n.write_stamp(exe, d)
    print("analyzer stamp", d[:16])
PY
cp "$ROOT/scripts/release/README.txt" "$WORK/README.txt"
cmp -s "$ROOT/LICENSE" "$WORK/LICENSE" || cp "$ROOT/LICENSE" "$WORK/LICENSE"
cp "$ROOT/NOTICE.md" "$WORK/NOTICE.md"
{
  echo "SuperRyuWorld1-Recomp $(git -C "$ROOT" rev-parse HEAD)"
  echo "SuperMarioWorldRecomp $(git -C "$UP" rev-parse HEAD) + patches/SuperMarioWorldRecomp-srw1.patch"
  echo "snesrecomp $(git -C "$UP/snesrecomp" rev-parse HEAD) + patches/snesrecomp-srw1.patch"
  echo "recomp-ui $(git -C "$UP/recomp-ui" rev-parse HEAD) + patches/recomp-ui-srw1.patch"
  echo "libretro/glsl-shaders cba01a48b0974a1232bea68d999a7c21bc4c233e"
} > "$WORK/SOURCE_REVISIONS"

# 5. Hard guards: nothing ROM-derived, no generated C, one top-level exe.
fail() { echo "make_release_zip: $*" >&2; exit 1; }
find "$WORK" \( -iname '*.sfc' -o -iname '*.smc' -o -iname '*.fig' -o -iname '*.swc' \
  -o -iname '*.srm' -o -iname '*.ips' -o -iname '*.bps' -o -iname '*.ups' \) | grep -q . && fail "ROM/patch file in pack"
find "$WORK/src" -mindepth 1 -maxdepth 1 -type d \( -name 'gen*' -o -name '.gen*' \) | grep -q . && fail "generated C dir in pack"
find "$WORK" -name funcs.h | grep -q . && fail "funcs.h in pack"
[ -e "$WORK/recomp/launcher/boxart.tga" ] && fail "stock box art in pack"
[ "$(find "$WORK" -maxdepth 1 -iname '*.exe' | wc -l)" -eq 1 ] || fail "expected exactly one top-level exe"
for f in snesrecomp/snesrecomp_cli.py tools/srw1_generate.py recomp/bank00.cfg recomp/srw1/bank00.cfg CMakeLists.txt config.ini third_party/gl_core/gl_core_3_1.c third_party/gl_core/gl_core_3_1.h third_party/stb/stb_image.h; do
  [ -f "$WORK/$f" ] || fail "required file missing: $f"; done
grep -rlI -e '/workspace/' -e '/home/box' "$WORK" --include='*.c' --include='*.h' --include='*.py' --include='*.cmake' --include='*.txt' --include='*.cfg' --include='*.ini' | grep -q . && fail "local path leaked into pack"

# 6. Deterministic-ish timestamps (48 h before the commit) and zip, flat root.
STAMP="$(date -u -d "@$(( $(git -C "$ROOT" log -1 --format=%ct) - 172800 ))" +%Y%m%d%H%M.%S)"
find "$WORK" -exec touch -h -t "$STAMP" {} +
mkdir -p "$ROOT/dist"; rm -f "$OUT"
( cd "$WORK" && zip -qr -X -y "$OUT" . )
echo "wrote $OUT ($(du -h "$OUT" | cut -f1))"

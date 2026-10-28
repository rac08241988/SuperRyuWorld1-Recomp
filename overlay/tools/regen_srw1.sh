#!/usr/bin/env bash
# Super Ryu World 1 variant of tools/regen.sh (modeled on the --coop branch).
# The patched SRW1 ROM is the analysis input directly; output -> src/gen-srw1.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
SNESRECOMP_ROOT=snesrecomp; PYTHON=python3
GEN_ROM="${SRW1_ROM:-srw1.sfc}"   # your own patched SRW1 ROM (not distributed)
EXPECT=d7c20cdf42e66a9b81d49359affd4e3548de5855444faceccfac8a1c845bf57f
[ "$(sha256sum "$GEN_ROM" | cut -d' ' -f1)" = "$EXPECT" ] || { echo "SRW1 ROM sha mismatch" >&2; exit 1; }
mkdir -p recomp/srw1
CFG_DIR=.build/recomp-srw1; OUT_DIR=src/gen-srw1; FUNCS_HEADER=recomp/srw1/funcs.h
"$PYTHON" "$SNESRECOMP_ROOT/tools/build_native_analyzer.py"
rm -rf -- "$ROOT/$CFG_DIR"; mkdir -p "$CFG_DIR"
cp recomp/bank*.cfg "$CFG_DIR/"
# Stock-only facts invalidated by SRW1's patches.
sed -i '/^name 0086df GameMode14_InLevel_0086DF$/d' "$CFG_DIR"/bank*.cfg
# AddmusicK replaces the stock SPC upload body; run the ROM's own code against
# the emulated SPC700 instead of the stock-protocol HLE.
# AMK rewrites $00:8079 as a JSL/RTL routine (stock is JSR/RTS) that also
# leaves DP $05 and registers in a different state, so the stock-ABI HLE stub
# (RTS pop) corrupts the return frame when called natively. Compile it faithfully.
sed -i '/^hle_spc_upload 8079 live$/d' "$CFG_DIR"/bank00.cfg
if [ -f recomp/srw1/strip.sed ]; then sed -i -f recomp/srw1/strip.sed "$CFG_DIR"/bank*.cfg; fi
for overlay in recomp/srw1/bank*.cfg; do
  [ -f "$overlay" ] || continue
  dest="$CFG_DIR/$(basename "$overlay")"
  if [ -f "$dest" ]; then printf '\n' >> "$dest"
    sed '/^bank[[:space:]]*=/d; /^includes[[:space:]]*=/d; /^comment[[:space:]]*=/d' "$overlay" >> "$dest"
  else cp "$overlay" "$dest"; fi
done
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_sync_funcs_h.py" --cfg-dir "$CFG_DIR" --out "$FUNCS_HEADER"
cp "$FUNCS_HEADER" "$CFG_DIR/funcs.h"
SOURCE_ROOT_ARGS=(--source-root recomp/renderer_aot_roots.c)
while IFS= read -r s; do SOURCE_ROOT_ARGS+=(--source-root "$s"); done < <(find src -type f -name '*.c' ! -path 'src/gen/*' ! -path 'src/gen-coop/*' ! -path 'src/gen-srw1/*' ! -name coop_patch.c | sort)
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_emit.py" --rom "$GEN_ROM" --cfg-dir "$CFG_DIR" --out-dir "$OUT_DIR" --cfg-roots "${SOURCE_ROOT_ARGS[@]}" --analysis-backend native
echo "=== Done (srw1) ==="

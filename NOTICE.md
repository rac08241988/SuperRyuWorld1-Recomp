# Notices

Required Notice: Copyright (c) 2026 Matthew Stanley (SuperMarioWorldRecomp, snesrecomp)

The files in `patches/` and `overlay/` modify or extend SuperMarioWorldRecomp
(https://github.com/mstan/SuperMarioWorldRecomp) and snesrecomp
(https://github.com/RetroPortingToolKit/snesrecomp), both licensed under the
PolyForm Noncommercial License 1.0.0. This repository's own additions are
distributed under the same license (see `LICENSE`).

Not included here and governed by their own terms:

- Upstream submodules: SuperMarioWorldRecomp, snesrecomp (PolyForm
  Noncommercial 1.0.0; snesrecomp's `THIRD_PARTY_ATTRIBUTION.md` lists the MIT
  snesrev / LakeSnes material it contains), recomp-ui (MIT), SMWDisX, Asar
  (LGPL-3.0+, see its license files).
- libretro/glsl-shaders, fetched at setup by `scripts/fetch_shaders.sh`
  (commit cba01a48). Each shader file carries its own license header.
- The Inter typeface (Rasmus Andersson), embedded in `overlay/src/inter_font_data.h`
  for the Options panel, under the SIL Open Font License 1.1
  (`overlay/licenses/Inter-OFL.txt`). Glyphs are rasterized with stb_truetype
  (public domain / MIT, from recomp-ui).
- Super Ryu World 1 (Aguni_) and Super Mario World (Nintendo). No ROM and no data
  derived from Super Mario World is distributed here. The release zip (and
  `patch/` in this repo) carries the hack's official, unmodified `.bps` patch
  file, which is Aguni_'s work, redistributed for convenience; it holds only
  the hack's changes and no Nintendo data.
- Floating IPS (Flips) v198 by Alcaro, shipped unmodified in the release zip as
  `patch/flips/flips.exe` under the GNU GPL v3 or later (`patch/flips/COPYING`,
  `COPYING.gpl3`); source: https://github.com/Alcaro/Flips/tree/v198
  (see `patch/flips/SOURCE.txt`).

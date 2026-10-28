# Super Ryu World 1 — Native PC Port (Recomp tooling)

## How to play (Windows)

1. Download `SuperRyuWorld1-SNESRecomp-windows-x64-v1.0.0.zip` from
   [Releases](https://github.com/rac08241988/SuperRyuWorld1-Recomp/releases/latest)
2. Unzip it
3. Open `SuperRyuWorld1SNESRecomp.exe`
4. Click **Browse ROM** and pick your own Super Ryu World 1 ROM (patched SMW — see
   [ROMs you need](#roms-you-need-bring-your-own))
5. Click **Generate & rebuild...**, wait a few minutes, then play

The first launch downloads a portable compiler (roughly 800 MB once unpacked) into `toolchain\`
next to the exe, so it needs internet. Everything it writes — the toolchain,
the generated sources, `build\`, saves, `config.ini`, `SuperRyuWorld1.log` —
stays inside the unzipped folder. Later launches go straight to the game's
launcher; press **Play**.

In game: **Esc** or **F12** opens/closes the options menu (shader, 120 Hz
motion, side art), **Alt+Enter** toggles fullscreen.

---

Static-recompilation tooling that turns **your own** copy of the Super Ryu World 1
ROM hack into a native Linux / Windows executable. It is a small SRW1 layer on top of
[SuperMarioWorldRecomp](https://github.com/mstan/SuperMarioWorldRecomp) and the
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) framework.

> **No ROM, patch, or game data is included in this repository or its release.**
> There is no ROM, no patched ROM, no BPS/IPS patch and no generated C derived
> from a ROM. The release zip contains only the recompiler sources/tooling, a
> small setup program and a prebuilt analyzer; all game-specific C is generated
> and compiled locally on your machine from your ROM.
>
> This project is not affiliated with, endorsed by, or sponsored by Nintendo.
> Super Mario World is a trademark of Nintendo.

## Credits

- **Super Ryu World** is by **Aguni_** —
  [SMW Central listing](https://www.smwcentral.net/?a=details&id=18268). All
  credit for the hack's levels, graphics, music and design goes to the author.
  Get the patch only from that official page.
- **[SuperMarioWorldRecomp](https://github.com/mstan/SuperMarioWorldRecomp)** and
  **[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp)** by
  Matthew Stanley — the recompiler and runtime this port is built on (PolyForm
  Noncommercial 1.0.0). They in turn credit
  [SMWDisX](https://github.com/IsoFrieze/SMWDisX),
  [snesrev](https://github.com/snesrev) (MIT), [LakeSnes](https://github.com/elzo-d/LakeSnes)
  (MIT) and snes9x — see their READMEs / `THIRD_PARTY_ATTRIBUTION.md`.
- **[recomp-ui](https://github.com/mstan/recomp-ui)** (MIT) and
  **[Asar](https://github.com/RPGHacker/asar)** (LGPL-3.0+) are pulled in as
  upstream submodules.
- **[libretro/glsl-shaders](https://github.com/libretro/glsl-shaders)** — the
  shader presets offered in the options menu. Not vendored; fetched at setup.
- **[Floating IPS](https://github.com/Alcaro/Flips)** by Alcaro, for applying the BPS patch.

## What's in this repo

| Path | What |
|---|---|
| `SuperMarioWorldRecomp/` | git submodule → upstream SuperMarioWorldRecomp, pinned at `324dfc8` (it brings `snesrecomp` `5ac4b98`, `recomp-ui`, `SMWDisX`, `asar` as its own submodules) |
| `patches/SuperMarioWorldRecomp-srw1.patch` | SRW1 build target (`SMW_BUILD_SRW1`), SRW1 reset/NMI handling, F12 options menu + 120 Hz presentation hooks, runtime shader switching, native (AOT) execution on by default for the SRW1 target |
| `patches/snesrecomp-srw1.patch` | RetroArch `.glslp` loader fixes: `aliasN`, `PassPrevN` back to the original, bare parameter overrides, `#version` hoisting, `gl_FragColor` compat; opt-in debug aids (`SNESRECOMP_STATE_DUMP_FRAMES` ARAM/DSP state dumps, `SNESRECOMP_NATIVE_STATS` native-share report) |
| `overlay/src/srw1_*.c/.h` | SRW1 options menu (shader picker, motion mode), 120 Hz render interpolation, HLE stub file |
| `overlay/recomp/srw1/` | SRW1 recompiler config overlay (`bank00.cfg` interpreter-tier pins for the AddmusicK SPC handshake) and `strip.sed` (drops stale stock entry points) |
| `overlay/tools/regen_srw1.sh`, `overlay/tools/srw1_generate.py` | Generate `src/gen-srw1/` from **your** patched ROM (the Python one is what the Windows setup program calls) |
| `overlay/tools/srw1_setup_stubs.py` | Placeholder bodies so the ROM-free setup program links without any generated code |
| `patches/recomp-ui-srw1.patch` | Small recomp-ui tweaks for the options overlay |
| `scripts/make_release_zip.sh` | Builds the one-click Windows release zip (see below) |
| `scripts/release/README.txt` | README shipped inside the zip |
| `scripts/setup.sh` | Submodules → patches → overlay → shaders → (optional) ROM check + C generation |
| `scripts/fetch_shaders.sh`, `scripts/stage_shaders.py` | Fetch libretro/glsl-shaders @ `cba01a48` and stage the 88 root presets |
| `cmake/mingw-w64.cmake` | Toolchain file for Windows x64 cross-builds from Linux |

## ROMs you need (bring your own)

1. **Clean Super Mario World (USA)**, headerless, 512 KiB.
   SHA-256 `0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b`.
   (The upstream build also needs the stock SMW C generated from it.)
2. **Super Ryu World 1 "Definitive"**, patched by you: download the official
   BPS from the [SMW Central page](https://www.smwcentral.net/?a=details&id=18268)
   and apply it to the clean headerless ROM above with
   [Floating IPS](https://github.com/Alcaro/Flips)
   (`flips --apply SuperRyuWorld.bps smw.sfc srw1.sfc`, or the Flips GUI).
   The result is a 2 MiB LoROM (no copier header) with
   SHA-256 **`d7c20cdf42e66a9b81d49359affd4e3548de5855444faceccfac8a1c845bf57f`**.
   If your hash differs (e.g. your SMW ROM had a 512-byte header, or you have a
   different release of the hack), the tooling will refuse it.

ROMs, `*.bps`/`*.ips` and generated C are git-ignored; never commit them.

## Build — Linux

Requirements: git, bash, CMake, Ninja, a C/C++ compiler (GCC or Clang),
Python 3, Rust/Cargo (the native analyzer is built on first regen), OpenGL dev
headers (e.g. `libgl-dev`) and the usual X11/Wayland dev packages SDL3 needs.
SDL3 3.4.10 is downloaded and built statically by CMake.

```bash
git clone https://github.com/rac08241988/SuperRyuWorld1-Recomp.git
cd SuperRyuWorld1-Recomp
scripts/setup.sh --smw /path/to/smw.sfc --srw1 /path/to/srw1.sfc

cd SuperMarioWorldRecomp
cmake -S . -B build-srw1 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSMW_BUILD_SRW1=ON -DSMW_BUNDLE_FALCON_CACHE_HELPER=OFF
cmake --build build-srw1 --target SuperRyuWorld1SNESRecomp

# shaders/ must sit in the working directory you launch from
cp -r ../shaders build-srw1/
cd build-srw1 && ./SuperRyuWorld1SNESRecomp --no-launcher /path/to/srw1.sfc
```

## Build — Windows x64

**Cross-compile from Linux (tested path).** Install `mingw-w64` (posix thread
model, e.g. Debian/Ubuntu `g++-mingw-w64-x86-64-posix`), then after
`scripts/setup.sh` as above:

```bash
cd SuperMarioWorldRecomp
cmake -S . -B build-srw1-win64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64.cmake \
      -DSMW_BUILD_SRW1=ON -DSMW_BUNDLE_FALCON_CACHE_HELPER=OFF
cmake --build build-srw1-win64 --target SuperRyuWorld1SNESRecomp
```

Copy `SuperRyuWorld1SNESRecomp.exe` and the staged `shaders/` folder into one
folder on Windows and run `SuperRyuWorld1SNESRecomp.exe --no-launcher srw1.sfc`.

**Native Windows (untested).** Use an MSYS2 MINGW64/UCRT64 shell with
`git cmake ninja python rust` and the mingw gcc toolchain installed, run
`scripts/setup.sh ...` there, then the Linux `cmake` commands without the
toolchain file.

## The one-click Windows release (how it works)

`scripts/make_release_zip.sh` (run after `scripts/setup.sh`, no ROM needed)
cross-builds a **setup host** — the SRW1 target configured with
`-DSMW_SRW1_PACK=ON -DSNESRECOMP_SETUP_HOST=ON` and an empty generated-code
directory, linking placeholder stubs — plus the Rust analyzer for
`x86_64-pc-windows-gnu`, then stages the tracked SMW / snesrecomp / recomp-ui
sources (minus ROM data, generated code, docs and tests), the shaders and the
configs into a flat zip. Guards fail the build if any ROM/patch file, generated
C or `funcs.h` ends up in it.

On the player's PC the setup exe shows a first-run wizard: it downloads the
portable `cmake-clang-v1` toolchain from
[retrocomm-toolchains](https://github.com/TechnicallyComputers/retrocomm-toolchains)
into `toolchain\`, verifies the ROM's SHA-256, runs the analyzer +
`tools/srw1_generate.py` into `src\gen-srw1`, then configures and builds
`build\SuperRyuWorld1SNESRecomp.exe` with CMake/Ninja (SDL3 is fetched by
CMake). Afterwards the setup exe hands off to the built game. The game runs
without a console window; output goes to `SuperRyuWorld1.log` beside it (pass
`--console` to get a console).

## Features

- **Native recompiled build** of Super Ryu World 1 via snesrecomp, with
  SDL3 + OpenGL output.
- **In-game options menu — Esc or F12** (gamepad: Guide) opens and closes it;
  it never quits the game. Arrows/D-pad navigate, Enter/A accept, Backspace/B
  back. The game pauses while it is open.
  - **Shader**: any of the 88 root-level RetroArch `.glslp` presets from
    libretro/glsl-shaders (CRT, scanlines, xBR/ScaleFX, NTSC, handheld, …),
    multi-pass, switched live; Off by default. Needs the OpenGL output.
  - **Motion** (120 Hz+ displays): **Off** (original 60 Hz path),
    **Interpolate** (in-between frames toward the newest tick, ≤1 tick delay),
    **Low-latency** (projects forward from the newest tick). Game logic stays at
    60 Hz; only presentation is re-rasterized from captured PPU state with
    interpolated scroll/sprite positions.
  - **Side Art** (default **On**): fills the widescreen bars with the level
    rendered beyond the SNES screen edges (softened and darkened), computed
    live from the game state; no art files are shipped.
  - Choices persist in `srw1_options.ini`.

## Status

- The game boots and runs.
- **Native recompiled code is on by default.** Roughly **36% of game logic**
  CPU time now runs as compiled native code (measured over a boot-to-title run
  with `SNESRECOMP_NATIVE_STATS=1`); everything that has no faithful compiled
  body yet falls back automatically to snesrecomp's cycle-exact interpreter.
- AddmusicK fix: the stock SMW SPC-upload HLE stub is dropped (AMK's `$80:8079`
  uploader is JSL/RTL, not JSR/RTS), the AMK CPU<->SPC handshake entries
  (`$80:8079`, `$80:80B6`, `$80:80F7`) are kept on the interpreter tier, and
  stale stock `HandleSPCUploads_*` entry points are stripped
  (`overlay/recomp/srw1/`). A boot-to-title run matches the interpreter in
  WRAM and in all uploaded SPC music/sample data.
- **Known issue:** music playback is phase-shifted by roughly **28–74 ms**
  relative to interpreter-only mode (same notes and data, slightly different
  start time), due to the runtime's approximate cycle accounting for compiled code.
- **Interpreter-only mode:** set `SNESRECOMP_LLE_BOUNCE=0` before launching
  (e.g. `SNESRECOMP_LLE_BOUNCE=0 ./SuperRyuWorld1SNESRecomp ...`, or
  `set SNESRECOMP_LLE_BOUNCE=0` in a Windows command prompt).
- Expect bugs. Report issues with your ROM hash and OS.

## License

The SRW1 patches and overlay code are derivative works of SuperMarioWorldRecomp
and snesrecomp and are released under the same **PolyForm Noncommercial
License 1.0.0** (see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md)).
Noncommercial use only. Submodules and fetched shaders keep their own
licenses. Super Ryu World itself is Aguni_'s work and is not covered by this
license; Super Mario World is © Nintendo.

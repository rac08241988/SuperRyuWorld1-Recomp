Super Ryu World 1 - native Windows PC port (SNES static recompilation)
=====================================================================

This download does NOT contain the game. It contains a small setup program
plus the recompiler. It turns YOUR OWN copy of the Super Ryu World 1 ROM
into a native Windows game, on your PC.

How to play
-----------
1. Unzip this whole folder somewhere you can write to (e.g. Documents or
   Desktop - not Program Files).
2. Open SuperRyuWorld1SNESRecomp.exe.
3. Click Browse and pick your own Super Ryu World 1 ROM (a patched Super
   Mario World ROM, 2 MB, SHA-256
   d7c20cdf42e66a9b81d49359affd4e3548de5855444faceccfac8a1c845bf57f).
4. Click the build button. The first time, it downloads a portable compiler
   (about a few hundred MB) into the toolchain\ folder here, generates the
   game code from your ROM and compiles it. This takes a few minutes.
5. When it finishes, press Play. Later launches go straight to the game.

Everything (compiler, build, saves, settings, log) stays inside this folder.
Delete the folder to remove it completely.

In game
-------
  Esc or F12      options menu (shaders, 120 Hz motion, side-bar art)
  Alt+Enter       fullscreen
  Controls        see config.ini (keyboard and gamepad)
  Log file        SuperRyuWorld1.log beside the game exe (or run it with
                  --console to see a console window)

Getting the ROM
---------------
Super Ryu World is by Aguni_. Download the official patch from its SMW Central
page (https://www.smwcentral.net/?a=details&id=18268) and apply it with
Floating IPS to a clean, headerless Super Mario World (USA) ROM. No ROM, patch
or game data is included here.

Source, issues and technical details:
  https://github.com/rac08241988/SuperRyuWorld1-Recomp

Licenses: SuperMarioWorldRecomp / snesrecomp are PolyForm Noncommercial 1.0.0
(see LICENSE.txt and NOTICE.md); recomp-ui is MIT; shaders\ are from
libretro/glsl-shaders and each file keeps its own license header.
Not affiliated with or endorsed by Nintendo.

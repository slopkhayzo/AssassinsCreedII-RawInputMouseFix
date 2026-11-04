# Assassin's Creed II Raw Mouse Fix

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested a couple hours of gameplay and so far spotted no major issues (on my machine ofc, if you have issues feel free to open an Issue)
> this patch currently probably only works for the latest Ubisoft Connect and Steam versions of the game.
> If you're interested and want more (human generated) info, I have a blog post [here](https://slop-blog.enkhayzomachines.net/posts/assassin-s-creed-ii-raw-input-mouse-fix) :)

An ASI plugin for the 32-bit Ubisoft Connect and Steam versions of *Assassin's Creed II*
that removes the mouse acceleration from the third-person camera and makes its
sensitivity independent of the frame rate: the same physical movement turns
the camera by the same angle, however fast you move the mouse and whatever the
frame rate is.

Current release: **v1.0.1**

The patch changes only the running process; it never modifies the game
executable or game data on disk.

## What it fixes

The game reads the mouse once per frame through DirectInput 8, which delivers
raw counts, and then feeds those counts to the camera as if they were a
gamepad's right stick:

- the third-person camera squares the stick value. For a gamepad that is a
  normal response curve; for counts per frame, it makes the turn grow with
  mouse speed (acceleration) and shrink as the frame rate rises (about 5 times
  less turn at 300 FPS than at 60 FPS for the same movement);
- each frame's movement is clamped to ±150 counts horizontally and ±100
  vertically, so fast turns lose distance, more so at low frame rates.

The fix takes the raw counts before the clamp and gives them to the camera
without the square. The camera's mouse path has no frame-time factor of its
own, so the turn per count is then the same at any frame rate. Everything else
is the game's own code: the look-sensitivity sliders, invert options, pitch
limits, camera transitions and scripted camera locks. Gamepad, keyboard camera
keys and the menu cursor are unchanged.

## Compatibility

| Build | AssassinsCreedIIGame.exe SHA-256 |
| --- | --- |
| Ubisoft Connect, original (PE timestamp `0x5925BA62`) | `7749D96CE04D7383352A36327EA3910A076704A8E0273206D938574658EE717B` |
| Same, with Unlocked FPS 1.0 (Kamzik123) | `577FDAE3EBAA4295D8AD865762641AA94374BCABB01022A6DF1D4E6994A3E277` |
| Steam (the same build wrapped in Steam's DRM) | `A4BA454B0E2A9190A42DB631574B694CC0DBDCEC750106C1B0364B70A6E4D17E` |

The Steam executable is the Ubisoft Connect build with Steam's DRM wrapper
(SteamStub): an extra `.bind` section, and `.text` encrypted on disk and
decrypted when the game starts. Every other section is byte-identical. The
plugin accepts the larger image and, if the code is still encrypted when it
loads, checks again until it has been decrypted (up to 2 minutes).

The hashes identify the tested builds; the plugin decides from the code itself.
Before hooking it checks the image base and size, every code signature it
depends on (`ac2_layout.h`), the DirectInput mouse GUID and data format, and the
game's mouse and camera constants. Any mismatch fails closed: the game runs
unmodified and the reason is written to `ac2-raw-mouse-fix.log`. Other releases
(retail DVD, older patches) have not been tested.

## Install

1. With no ASI loader in the game folder, use the archive with Ultimate ASI
   Loader and copy `dinput8.dll` (Ultimate ASI Loader x86 v9.7.4),
   `AssassinsCreedIIRawMouseFix.asi` and `ac2-raw-mouse-fix.ini` beside
   `AssassinsCreedIIGame.exe`. With an existing 32-bit ASI loader, use the
   plugin-only archive and copy only the ASI and the ini. Never overwrite an
   existing `dinput8.dll` without checking which mods use it.
2. Start the game normally (through Ubisoft Connect or Steam).
3. If the fix does not seem active, check `ac2-raw-mouse-fix.log` beside the
   ASI (or in `%LOCALAPPDATA%\AC2RawMouseFix` if that folder is not writable).

Security software may inspect or warn about ASI loaders and runtime-hooking
plugins, because their job is to load code into the game process and change
its in-memory instructions.

## Configuration

`ac2-raw-mouse-fix.ini`, section `[Sensitivity]`, higher turns faster:

- `SensitivityX`, `SensitivityY`: turn per mouse count, horizontal and
  vertical. Default 50.
- `SensitivityMultiplier`: scales both. Default 1.00.

With the default camera and in-game sliders, `SensitivityX=50` is 0.079° per
count, or 4570 counts for a full turn (14.5 cm at 800 DPI, 7.3 cm at 1600 DPI).
The game gives each camera its own turn speed, and the fix keeps that: some
cameras turn up to about 2.75 times slower. The in-game look-sensitivity
sliders still apply on top.

Section `[Fix]`:

- `Enabled`: 1 loads the fix, 0 leaves the game unmodified.
- `LinearCamera`: 1 turns the third-person camera by raw counts, 0 keeps the
  game's own curve.
- `Hotkeys`: 1 enables F8 (switch between the fix and the game's own mouse
  handling), F9/F10 (lower/raise `SensitivityMultiplier` by 10 % until the game
  closes) and F6 (reload the ini).
- `Diagnostics`: 1 writes input statistics to the log every five seconds.

## Uninstall

Close the game and remove `AssassinsCreedIIRawMouseFix.asi`,
`ac2-raw-mouse-fix.ini` and the optional log. Remove `dinput8.dll` only if it
was installed for this fix and no other plugin uses it.

## Build

Requirements: Windows, Visual Studio (or Build Tools) with the C++ x86 tools.

```bat
build-msvc.cmd
```

It builds `build\AssassinsCreedIIRawMouseFix.asi` (x86, static runtime) and
runs the tests: `counts-test` (every mouse count reaches each camera state
once), and the ASI must stay inactive in a foreign host and in a renamed host
with the wrong layout. To also validate real executables, set their paths
first:

```bat
set "AC2_EXE_ORIGINAL=D:\...\AssassinsCreedIIGame.exe.orig"
set "AC2_EXE_UNLOCKED_FPS=D:\...\AssassinsCreedIIGame.exe"
build-msvc.cmd
```

`layout-profile-test` maps each executable like the Windows loader, checks the
layout and confirms that a corrupted signature byte is rejected. It cannot check
the Steam executable, whose code is encrypted on disk; that one is checked by the
plugin in the running game.
`build-msvc.cmd dev` adds the development tools (a hardware-breakpoint trace of
the code that reads the camera stick, `[Trace]` in the ini).

## Package

```powershell
.\package-release.ps1 -Version 1.0.0 -LoaderPath <x86 Ultimate ASI Loader v9.7.4 dinput8.dll>
```

It writes to `dist\`:

- `AssassinsCreedII-RawInputMouseFix-v<version>-plugin-only.zip`: the ASI,
  its ini, `README.txt`, `LICENSE.txt`, `SHA256SUMS.txt`;
- `AssassinsCreedII-RawInputMouseFix-v<version>-with-Ultimate-ASI-Loader-v9.7.4.zip`:
  the same plus `dinput8.dll` (x86 loader, hash-checked) and
  `THIRD-PARTY-NOTICES.txt`;
- a `.sha256` file for each archive.

The script refuses to overwrite an existing release, to package a non-x86
plugin, or to package while `release-readme.txt` still has TODO sections.

## Technical outline

- `ac2_layout.h`: addresses, signatures and constants, shared by the plugin and
  the layout test.
- `ac2_counts.h`: hands each frame's mouse counts to the camera: every camera
  state updated in a frame (two during a transition) gets that frame's counts
  once; counts older than 250 ms (camera not running) are dropped.
- `ac2_raw_mouse_fix.cpp`: the plugin. A worker thread validates the image and
  installs two detours: the pad's DirectInput mouse poll (`0x0148A930`), where
  the raw counts are collected, and the third-person camera's stick curve
  (`0x00756370`). When the camera takes its mouse branch, a small stub passes
  the branch flag and the camera's data, and the plugin writes the counts times
  the sensitivity in place of the squared stick, unless the game has locked
  camera input. Other threads are suspended while the jumps are written, and
  any thread caught inside the patched bytes is moved to the trampoline copy.

## Misc

For more game fixes, visit: https://slop-blog.enkhayzomachines.net/fixes :)

# Assassin's Creed II Raw Mouse Fix v1.0.0

First release.

## What it does

- **No more mouse acceleration.** The game feeds each frame's mouse movement to
  the camera as if it were a gamepad's right stick, and the third-person camera
  squares the stick value. For the mouse, that makes the turn grow with mouse
  speed. The fix gives the camera the raw mouse counts instead, so the same
  movement always turns the camera by the same amount.
- **Same sensitivity at any frame rate.** Because the counts were squared per
  frame, the turn shrank as the frame rate rose (about 5 times less at 300 FPS
  than at 60 FPS). The camera's mouse path has no frame-time factor of its own,
  so with raw counts the turn per count is the same at any frame rate. This
  matters especially with an FPS unlock.
- **No cut-off on fast movements.** The game discarded movement beyond 150
  counts per frame horizontally and 100 vertically; the fix applies all of it.
- **Sensitivity settings.** `[Sensitivity]` has `SensitivityX`, `SensitivityY`
  (turn per count, default 50) and `SensitivityMultiplier`. The in-game look
  sensitivity sliders and invert options still apply.
- **A/B hotkeys.** F8 switches between the fix and the game's own mouse
  handling, F9/F10 lower/raise the multiplier by 10 % for the session, F6
  reloads the ini.

Camera transitions (two cameras blending), scripted camera locks, pitch limits,
the gamepad, keyboard camera keys, menus and the map cursor all behave as in the
unmodified game.

## Compatibility

Tested on the Ubisoft Connect build (`AssassinsCreedIIGame.exe`, PE timestamp
`0x5925BA62`), both unmodified (SHA-256
`7749D96CE04D7383352A36327EA3910A076704A8E0273206D938574658EE717B`) and with
the Unlocked FPS 1.0 patch by Kamzik123 (SHA-256
`577FDAE3EBAA4295D8AD865762641AA94374BCABB01022A6DF1D4E6994A3E277`). There is
no hash check: before hooking, the plugin checks every code and data signature
it depends on, and on any mismatch it leaves the game unchanged and writes the
reason to `ac2-raw-mouse-fix.log`. Other releases have not been tested.

## Known limitations

- Only the third-person camera is changed. First-person views and other
  special camera modes use the game's own code; the first-person camera does
  not have the squared curve.
- Riding a horse has not been tested.
- The game gives each camera its own turn speed; the fix keeps those
  differences, so some cameras turn up to about 2.75 times slower than others.

## Release archives

- `AssassinsCreedII-RawInputMouseFix-v1.0.0-plugin-only.zip`: the ASI,
  configuration, project license, documentation and payload hashes, for
  installations that already have a 32-bit (x86) ASI loader.
- `AssassinsCreedII-RawInputMouseFix-v1.0.0-with-Ultimate-ASI-Loader-v9.7.4.zip`:
  the same files plus Ultimate ASI Loader x86 v9.7.4 as `dinput8.dll` and its
  third-party notice.

Each archive contains `SHA256SUMS.txt`; each `.zip` has a `.zip.sha256` beside
it.

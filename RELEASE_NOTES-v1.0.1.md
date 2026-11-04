# Assassin's Creed II Raw Mouse Fix v1.0.1

Adds support for the Steam version.

## Changes

- **Steam version supported.** The Steam executable is the same game build as
  the Ubisoft Connect one, wrapped in Steam's DRM: an extra section makes the
  image larger, and the game code is encrypted on disk and decrypted when the
  game starts. v1.0.0 required the exact Ubisoft Connect image size and stayed
  inactive on Steam. The plugin now accepts the larger image and, if the code is
  still encrypted when it loads, checks it again until it has been decrypted (up
  to 2 minutes). The checks on the code itself are unchanged: every signature
  must match before anything is hooked.
- **Hooks first, log details later.** The executable's SHA-256, which only names
  the build in the log, is now computed on its own thread after the hooks are
  installed, so it can no longer hold up the fix. The plugin also reads its own
  paths once when it is loaded.

Nothing changes for the Ubisoft Connect version, and the settings are the same:
keep your `ac2-raw-mouse-fix.ini` or use the new one.

## Compatibility

| Build | AssassinsCreedIIGame.exe SHA-256 |
| --- | --- |
| Ubisoft Connect (PE timestamp `0x5925BA62`) | `7749D96CE04D7383352A36327EA3910A076704A8E0273206D938574658EE717B` |
| Same, with Unlocked FPS 1.0 (Kamzik123) | `577FDAE3EBAA4295D8AD865762641AA94374BCABB01022A6DF1D4E6994A3E277` |
| Steam | `A4BA454B0E2A9190A42DB631574B694CC0DBDCEC750106C1B0364B70A6E4D17E` |

There is no hash check: the plugin checks every code and data signature it
depends on, and on any mismatch it leaves the game unchanged and writes the
reason to `ac2-raw-mouse-fix.log`.

## Release archives

- `AssassinsCreedII-RawInputMouseFix-v1.0.1-plugin-only.zip`: the ASI,
  configuration, project license, documentation and payload hashes, for
  installations that already have a 32-bit (x86) ASI loader.
- `AssassinsCreedII-RawInputMouseFix-v1.0.1-with-Ultimate-ASI-Loader-v9.7.4.zip`:
  the same files plus Ultimate ASI Loader x86 v9.7.4 as `dinput8.dll` and its
  third-party notice.

Each archive contains `SHA256SUMS.txt`; each `.zip` has a `.zip.sha256` beside
it.

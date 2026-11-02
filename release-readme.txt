Assassin's Creed II Raw Mouse Fix v@VERSION@
=============================================

Supported game builds
---------------------
Assassin's Creed II, Ubisoft Connect (Uplay) build for Windows x86
(AssassinsCreedIIGame.exe, PE timestamp 0x5925BA62). Two executables are
tested, with SHA-256:

Original:
7749D96CE04D7383352A36327EA3910A076704A8E0273206D938574658EE717B

With the Unlocked FPS 1.0 patch by Kamzik123:
577FDAE3EBAA4295D8AD865762641AA94374BCABB01022A6DF1D4E6994A3E277

These hashes identify the reference builds; they are not a hard allowlist.
Before writing a hook, the ASI checks the image layout and every code and data
signature it depends on. Any mismatch leaves the game unchanged and is written
to ac2-raw-mouse-fix.log. Other releases (retail DVD, older Steam versions)
have not been tested; if their code differs, the fix stays inactive and the log
says why.

Choose an installation
----------------------
Plugin-only archive:
  Requires a compatible 32-bit (x86) ASI loader already installed. Copy
  AssassinsCreedIIRawMouseFix.asi and ac2-raw-mouse-fix.ini beside
  AssassinsCreedIIGame.exe, or into the plugin folder of your loader. Keep
  both files together.

Convenience archive with Ultimate ASI Loader:
  If the game has no ASI loader, copy dinput8.dll,
  AssassinsCreedIIRawMouseFix.asi and ac2-raw-mouse-fix.ini beside
  AssassinsCreedIIGame.exe. The bundled dinput8.dll is Ultimate ASI Loader
  x86 v9.7.4.

  If the game already has a 32-bit ASI loader, do not overwrite it. Copy only
  AssassinsCreedIIRawMouseFix.asi and ac2-raw-mouse-fix.ini.

For either installation, close the game before copying files, then start it
normally through Ubisoft Connect.

What it fixes
-------------
The game reads the mouse once per frame and feeds the movement to the camera
as if it were a gamepad's right stick. The third-person camera squares the
stick value, which is a normal response curve for a gamepad but, for the mouse,
makes the turn grow with mouse speed (acceleration) and shrink as the frame
rate rises: at 300 FPS the same movement turned about 5 times less than at 60
FPS. Fast movements were also cut off at 150 counts per frame horizontally and
100 vertically.

With the fix, the third-person camera turns by the raw mouse counts: the same
movement always turns the camera by the same amount, at any speed and any
frame rate, with nothing cut off. Everything else is the game's own code: the
in-game look sensitivity sliders and invert options still work, and so do the
pitch limits, camera transitions and scripted camera locks.

Usage
-----
Install and play. Settings are in ac2-raw-mouse-fix.ini:

[Sensitivity]
  SensitivityX, SensitivityY: turn per mouse count, horizontal and vertical.
    Higher turns faster. Default 50.
  SensitivityMultiplier: scales both. Default 1.00.

  With the default camera and the in-game sliders at their default,
  SensitivityX=50 is 0.079 degrees per count, or 4570 counts for a full turn:
    400 DPI: 29 cm    800 DPI: 14.5 cm    1600 DPI: 7.3 cm    3200 DPI: 3.6 cm
  Some cameras turn slower by design (the game sets a speed per camera), up
  to about 2.75 times slower; the fix keeps those differences.

[Fix]
  LinearCamera=0 brings back the game's own mouse handling.
  Hotkeys=1 (default) enables, in game:
    F8        switch between the fix and the game's own mouse handling
    F9 / F10  lower / raise SensitivityMultiplier by 10 % until the game
              closes (set it in the ini to keep it)
    F6        reload the ini
  Diagnostics=1 writes input statistics to the log every five seconds.

Safety and scope
----------------
Only the mouse turning the third-person camera is changed. Menus and the map
cursor, the gamepad, keyboard camera keys, and the other camera modes (such as
first-person views) use the game's own code. The first-person camera does not
have the squared curve; riding a horse has not been tested yet. If something feels
wrong, F8 switches back to the game's own handling for a comparison; please
report where it happened.

The fix changes the running game only. It never modifies the game executable
or its data on disk.

Diagnostics
-----------
The fix writes ac2-raw-mouse-fix.log beside the ASI (or, if that folder is
not writable, in %LOCALAPPDATA%\AC2RawMouseFix). Check it if the fix does not
activate: executable-layout and signature failures are reported there.
Diagnostics=1 in the ini adds input statistics every five seconds.

Uninstallation
--------------
Close the game and remove AssassinsCreedIIRawMouseFix.asi and
ac2-raw-mouse-fix.ini. The ac2-raw-mouse-fix.log file may also be removed.

Do not remove dinput8.dll if it existed before this fix or if another plugin
depends on it. Remove the bundled loader only when you installed that exact
file for this fix and no other ASI plugin needs it.

Third-party component
---------------------
The explicitly named convenience archive includes Ultimate ASI Loader v9.7.4
(x86) by ThirteenAG under the MIT License. See THIRD-PARTY-NOTICES.txt. The
plugin-only archive contains no third-party loader binary.

Misc
----
For more game fixes, visit:
https://slop-blog.enkhayzomachines.net/fixes :)

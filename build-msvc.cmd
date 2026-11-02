@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

rem Assassin's Creed II is a 32-bit game: always build with the x86 toolchain.
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "!VSWHERE!" (
    echo Microsoft C++ Build Tools were not found.
    echo Install Visual Studio Build Tools with Desktop development with C++.
    exit /b 1
  )
  set "VSINSTALL="
  for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
  if not defined VSINSTALL (
    echo The Visual Studio C++ toolchain was not found.
    exit /b 1
  )
  for %%D in ("!VSWHERE!") do set "PATH=%%~dpD;!PATH!"
  call "!VSINSTALL!\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
  if errorlevel 1 exit /b 1
)

if not exist "build" mkdir "build"
set "CFLAGS=/nologo /std:c++17 /O2 /EHsc /W4 /MT /DUNICODE /D_UNICODE"

rem "build-msvc.cmd dev" adds the development tools (right-stick reader trace).
set "PLUGINFLAGS="
if /i "%~1"=="dev" set "PLUGINFLAGS=/DAC2_DEV_TOOLS=1"

cl.exe %CFLAGS% %PLUGINFLAGS% /LD /Fo:"build\ac2-raw-mouse-fix.obj" "ac2_raw_mouse_fix.cpp" ^
  /link /OUT:"build\AssassinsCreedIIRawMouseFix.asi" /IMPLIB:"build\AssassinsCreedIIRawMouseFix.lib" bcrypt.lib user32.lib
if errorlevel 1 (
  echo.
  echo Build failed.
  exit /b 1
)

cl.exe %CFLAGS% /Fo:"build\asi-load-test.obj" "asi-load-test.cpp" /link /OUT:"build\asi-load-test.exe"
if errorlevel 1 (
  echo.
  echo ASI load-test build failed.
  exit /b 1
)

cl.exe %CFLAGS% /Fo:"build\layout-profile-test.obj" "layout-profile-test.cpp" /link /OUT:"build\layout-profile-test.exe"
if errorlevel 1 (
  echo.
  echo Layout-profile test build failed.
  exit /b 1
)

cl.exe %CFLAGS% /Fo:"build\counts-test.obj" "counts-test.cpp" /link /OUT:"build\counts-test.exe"
if errorlevel 1 (
  echo.
  echo Count test build failed.
  exit /b 1
)

"build\counts-test.exe"
if errorlevel 1 (
  echo.
  echo Count tests failed.
  exit /b 1
)

if not exist "build\layout-host" mkdir "build\layout-host"
copy /Y "build\asi-load-test.exe" "build\layout-host\AssassinsCreedIIGame.exe" >nul
if errorlevel 1 (
  echo.
  echo Layout-host test staging failed.
  exit /b 1
)

rem Optional: validate the real executables when their paths are set.
if defined AC2_EXE_ORIGINAL (
  "build\layout-profile-test.exe" "%AC2_EXE_ORIGINAL%" "original"
  if errorlevel 1 (
    echo.
    echo Layout-profile test failed on the original executable.
    exit /b 1
  )
)
if defined AC2_EXE_UNLOCKED_FPS (
  "build\layout-profile-test.exe" "%AC2_EXE_UNLOCKED_FPS%" "Unlocked FPS 1.0"
  if errorlevel 1 (
    echo.
    echo Layout-profile test failed on the Unlocked FPS executable.
    exit /b 1
  )
)

"build\asi-load-test.exe" "build\AssassinsCreedIIRawMouseFix.asi"
if errorlevel 1 (
  echo.
  echo ASI load test failed.
  exit /b 1
)

"build\layout-host\AssassinsCreedIIGame.exe" "build\AssassinsCreedIIRawMouseFix.asi" --layout-host
if errorlevel 1 (
  echo.
  echo Executable-layout fail-closed test failed.
  exit /b 1
)

echo.
echo Built and load-tested: %CD%\build\AssassinsCreedIIRawMouseFix.asi
endlocal

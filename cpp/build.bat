@echo off
setlocal EnableExtensions EnableDelayedExpansion
set ROOT=%~dp0
set OUT=%ROOT%build
if not exist "%OUT%" mkdir "%OUT%"

rem Use an existing MSVC environment, or locate Visual Studio / Build Tools via vswhere.
where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
      if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
  )
)
where cl >nul 2>nul
if errorlevel 1 (
  echo [ERROR] MSVC cl.exe not found.
  echo Install Visual Studio 2022 Build Tools with the C++ desktop workload,
  echo or run this script from "x64 Native Tools Command Prompt for VS 2022".
  exit /b 1
)

rc /nologo /fo "%OUT%\app.res" "%ROOT%res\app.rc"
if errorlevel 1 exit /b 1

cl /nologo /O2 /GL /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /I"%ROOT%src" ^
   "%ROOT%src\main.cpp" ^
   "%ROOT%src\core\json.cpp" ^
   "%ROOT%src\core\zip_npy.cpp" ^
   "%ROOT%src\core\paths.cpp" ^
   "%ROOT%src\core\settings.cpp" ^
   "%ROOT%src\core\models.cpp" ^
   "%ROOT%src\audio\audio_engine.cpp" ^
   "%ROOT%src\gfx\gfx.cpp" ^
   "%ROOT%src\gfx\assets.cpp" ^
   "%ROOT%src\gfx\spectrum.cpp" ^
   "%ROOT%src\app\curtain.cpp" ^
   "%ROOT%src\app\title_screen.cpp" ^
   "%ROOT%src\app\freeplay_screen.cpp" ^
   "%ROOT%src\app\player_screen.cpp" ^
   "%ROOT%src\app\gallery_screen.cpp" ^
   "%ROOT%src\app\viewer_overlay.cpp" ^
   "%ROOT%src\app\detail_overlay.cpp" ^
   "%ROOT%src\app\settings_overlay.cpp" ^
   "%ROOT%src\app\app.cpp" ^
   "%OUT%\app.res" ^
   /Fo"%OUT%\\" /Fe:"%OUT%\NientumPlayer_v2.exe" ^
   /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF ^
   user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib ^
   d3d11.lib dxgi.lib d2d1.lib dwrite.lib windowscodecs.lib dxguid.lib ^
   dwmapi.lib winmm.lib mfplat.lib mfreadwrite.lib mfuuid.lib avrt.lib

if errorlevel 1 exit /b 1
echo Build succeeded: %OUT%\NientumPlayer_v2.exe
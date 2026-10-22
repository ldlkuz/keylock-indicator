@echo off
setlocal
cd /d "%~dp0"

rem Optional: set VCVARS to a vcvars64.bat before running this script.
rem Otherwise vswhere locates the newest Visual Studio with the C++ toolset.
if defined VCVARS goto :have_vcvars

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL goto :no_vs

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"

:have_vcvars
if not exist "%VCVARS%" goto :no_vs

call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

if not exist build mkdir build

rc /nologo /fo build\app.res res\app.rc
if errorlevel 1 exit /b 1

cl /nologo /std:c11 /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE ^
   /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
   /Ires ^
   /Fo:build\ /Fe:build\KeyLockIndicator.exe ^
   src\main.c src\icon.c src\tray.c src\keystate.c src\config.c src\panel.c src\palette.c ^
   build\app.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:res\app.manifest ^
   user32.lib gdi32.lib shell32.lib advapi32.lib

if errorlevel 1 exit /b 1
echo [+] build\KeyLockIndicator.exe

rem Deploy over an existing per-user install so the desktop shortcut always
rem starts the freshly built binary.
set "DEST=%LOCALAPPDATA%\Programs\KeyLockIndicator"
if exist "%DEST%\KeyLockIndicator.exe" (
    copy /y "build\KeyLockIndicator.exe" "%DEST%\KeyLockIndicator.exe" >nul 2>&1
    if errorlevel 1 (
        echo [!] deploy skipped: the program is running, quit it from the tray first
    ) else (
        echo [+] deployed to %DEST%
    )
) else (
    echo [i] not installed yet, run install.bat to create the shortcuts
)
exit /b 0

:no_vs
echo [!] Visual Studio with the C++ toolset was not found.
echo     Install "Desktop development with C++" (VS 2019/2022), or set the
echo     VCVARS environment variable to the full path of vcvars64.bat.
exit /b 1

@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

set "DEST=%LOCALAPPDATA%\Programs\KeyLockIndicator"

if not exist "build\KeyLockIndicator.exe" (
    echo [!] 找不到 build\KeyLockIndicator.exe，请先运行 build.bat
    pause
    exit /b 1
)

echo 正在安装到 %DEST%
if not exist "%DEST%" mkdir "%DEST%"
copy /y "build\KeyLockIndicator.exe" "%DEST%\KeyLockIndicator.exe" >nul
if errorlevel 1 (
    echo [!] 复制失败：程序可能正在运行，先在托盘右键退出
    pause
    exit /b 1
)

set "DESKTOP=%USERPROFILE%\Desktop"
if not exist "%DESKTOP%" set "DESKTOP=%USERPROFILE%\桌面"

powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP%\键盘指示灯.lnk'); $s.TargetPath = '%DEST%\KeyLockIndicator.exe'; $s.WorkingDirectory = '%DEST%'; $s.Description = '任务栏键盘指示灯'; $s.Save()"

echo.
echo [+] 安装完成：%DEST%\KeyLockIndicator.exe
echo [+] 桌面快捷方式：%DESKTOP%\键盘指示灯.lnk
echo.
echo 直接从桌面快捷方式启动即可。不要在受限权限的沙箱里启动，那样无法注册托盘图标。
pause

@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo ============================================
echo  键盘指示灯 - 诊断
echo ============================================
echo.

if not exist "build\KeyLockIndicator.exe" (
    echo [!] 找不到 build\KeyLockIndicator.exe，请先运行 build.bat 编译。
    pause
    exit /b 1
)

if exist "build\diag" rmdir /s /q "build\diag"
start "" /wait "build\KeyLockIndicator.exe" --diag "%~dp0build\diag"

echo.
if exist "build\diag\diag.txt" (
    type "build\diag\diag.txt"
    echo.
    echo --------------------------------------------
    echo 报告已保存: build\diag\diag.txt
    echo 同时把 %LOCALAPPDATA%\KeyLockIndicator\log.txt 也发给我更好。
    echo --------------------------------------------
    notepad "build\diag\diag.txt"
) else (
    echo [!] 没有生成报告，说明程序根本没启动起来。
    echo     请检查是不是被安全软件拦截了。
)
pause

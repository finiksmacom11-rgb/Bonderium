@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
if errorlevel 1 (
    echo.
    echo Не удалось установить Bonderium.
    echo Проверьте подключение к интернету и повторите попытку.
    pause
    exit /b 1
)

echo.
echo Готово! Запускайте Bonderium с рабочего стола.
pause

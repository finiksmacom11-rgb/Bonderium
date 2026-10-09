@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul

set "PYTHON=python"
python -c "import sys" >nul 2>&1
if errorlevel 1 (
    set "PYTHON="
    for /d %%D in ("%LocalAppData%\Programs\Python\Python*") do (
        if exist "%%~D\python.exe" set "PYTHON=%%~D\python.exe"
    )
)
if not defined PYTHON (
    echo Python 3 не найден. Установите Python 3 и повторите запуск.
    pause
    exit /b 1
)

"%PYTHON%" create_shortcut.py
if errorlevel 1 (
    echo.
    echo Не удалось установить Bonderium.
    echo Проверьте, что Python 3, CMake и LLVM-MinGW установлены и добавлены в PATH.
    pause
    exit /b 1
)

echo.
echo Готово! Запускайте Bonderium ярлыком на рабочем столе.
pause

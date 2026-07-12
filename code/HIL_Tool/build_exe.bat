@echo off
REM ============================================================
REM  Build EdgeProfiler.exe (standalone, no console window)
REM  Run this on Windows from inside the HIL_Tool folder.
REM ============================================================
setlocal

echo [1/3] Creating virtual environment...
python -m venv venv || goto :error
call venv\Scripts\activate.bat

echo [2/3] Installing dependencies...
python -m pip install --upgrade pip || goto :error
pip install -r requirements.txt || goto :error

echo [3/3] Building EdgeProfiler.exe with PyInstaller...
pyinstaller --noconfirm --clean --onefile --noconsole ^
    --name EdgeProfiler ^
    --collect-submodules pyqtgraph ^
    --collect-submodules openpyxl ^
    --hidden-import serial.tools.list_ports ^
    --hidden-import openpyxl ^
    edge_profiler.py || goto :error

echo.
echo ============================================================
echo  Done! Find your executable at:  dist\EdgeProfiler.exe
echo ============================================================
pause
exit /b 0

:error
echo.
echo ************************************************************
echo  BUILD FAILED. Doc thong bao loi o tren de biet nguyen nhan.
echo ************************************************************
pause
exit /b 1

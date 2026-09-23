@echo off
setlocal
set "ROOT=%~dp0"
set "PAPER_APP="
set "QT_BIN="

if exist "%ROOT%dist\paper-demo\campus-print.exe" set "PAPER_APP=%ROOT%dist\paper-demo\campus-print.exe"
if not defined PAPER_APP if exist "%ROOT%dist\paper-demo-20260923\campus-print.exe" set "PAPER_APP=%ROOT%dist\paper-demo-20260923\campus-print.exe"
if not defined PAPER_APP if exist "%ROOT%build\dev\bin\campus-print.exe" (
    set "PAPER_APP=%ROOT%build\dev\bin\campus-print.exe"
    where qtpaths6 >nul 2>nul
    if not errorlevel 1 for /f "delims=" %%I in ('qtpaths6 --query QT_INSTALL_BINS 2^>nul') do set "QT_BIN=%%I"
)

if defined QT_BIN set "PATH=%QT_BIN%;%PATH%"
if not defined PAPER_APP (
    echo Paper demo executable is not built or packaged.
    echo Build it first:
    echo   cmake --preset dev -DCMAKE_PREFIX_PATH="<Qt directory>"
    echo   cmake --build --preset dev --parallel 2
    echo See docs/paper-demo.md for details.
    pause
    exit /b 1
)
start "" "%PAPER_APP%" --data-dir "%ROOT%runtime\paper-demo" --demo-mode --demo-workload --page-ms 650

@echo off
setlocal
set "ROOT=%~dp0"
set "STUDIO_APP=%ROOT%dist\print-studio\campus-print.exe"
if not exist "%STUDIO_APP%" (
    set "STUDIO_APP=%ROOT%build\dev\bin\campus-print.exe"
    for /f "delims=" %%I in ('qtpaths6 --query QT_INSTALL_BINS 2^>nul') do set "STUDIO_QT=%%I"
)
if defined STUDIO_QT set "PATH=%STUDIO_QT%;%PATH%"
if not exist "%STUDIO_APP%" (
    echo Build the application first. See docs/development.md.
    pause
    exit /b 1
)
start "" "%STUDIO_APP%" --data-dir "%ROOT%runtime\print-studio" --demo-mode --demo-workload --page-ms 650
start "" "%STUDIO_APP%" --role student --data-dir "%ROOT%runtime\print-studio" --demo-mode --page-ms 650

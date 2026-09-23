@echo off
setlocal
set "PAPER_APP=%~dp0dist\paper-demo-20260923\campus-print.exe"
if not exist "%PAPER_APP%" (
    echo Paper demo package is missing. See docs/paper-demo.md.
    pause
    exit /b 1
)
start "" "%PAPER_APP%" --data-dir "%~dp0runtime\paper-demo" --demo-mode --demo-workload --page-ms 650

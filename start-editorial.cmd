@echo off
setlocal
cd /d "%~dp0"
if exist "dist\editorial-v10\campus-print.exe" (
    start "" "%~dp0dist\editorial-v10\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v9\campus-print.exe" (
    start "" "%~dp0dist\editorial-v9\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v8\campus-print.exe" (
    start "" "%~dp0dist\editorial-v8\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v7\campus-print.exe" (
    start "" "%~dp0dist\editorial-v7\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v6\campus-print.exe" (
    start "" "%~dp0dist\editorial-v6\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v5\campus-print.exe" (
    start "" "%~dp0dist\editorial-v5\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v4\campus-print.exe" (
    start "" "%~dp0dist\editorial-v4\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v3\campus-print.exe" (
    start "" "%~dp0dist\editorial-v3\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if exist "dist\editorial-v2\campus-print.exe" (
    start "" "%~dp0dist\editorial-v2\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*
    exit /b 0
)
if not exist "build\dev\bin\campus-print.exe" (
    echo Build the application or create dist\editorial-v2 first. See docs\editorial-frontend.md.
    pause
    exit /b 1
)
for /f "delims=" %%Q in ('where qtpaths6.exe 2^>nul') do set "CPS_EDITORIAL_QT_BIN=%%~dpQ"
if defined CPS_EDITORIAL_QT_BIN set "PATH=%CPS_EDITORIAL_QT_BIN%;%PATH%"
start "" "%~dp0build\dev\bin\campus-print.exe" --role student --data-dir "%~dp0runtime\editorial-shop" %*

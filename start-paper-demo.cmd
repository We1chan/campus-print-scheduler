@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-classroom-demo.ps1" -StateDirectory "%~dp0runtime\paper-demo" -PageMs 650
exit /b %errorlevel%

@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_nightly_cab.ps1" %*
exit /b %ERRORLEVEL%

@echo off
setlocal

REM Package existing Debug/Release outputs; this wrapper never builds.
set "ROOT=%~dp0.."
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_nightly.ps1" %*
exit /b %ERRORLEVEL%

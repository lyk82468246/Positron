@echo off
setlocal
set "PS32=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS32%" exit /b 3
call "%~dp0repair_wmdc_rapi.bat" -QuietHealthy
if errorlevel 1 exit /b %ERRORLEVEL%
"%PS32%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0internal_pages_gate.ps1" %*
exit /b %ERRORLEVEL%

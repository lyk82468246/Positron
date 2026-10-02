@echo off
setlocal
set "PS32=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS32%" exit /b 3
"%PS32%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0app_history_gate.ps1" %*
exit /b %ERRORLEVEL%

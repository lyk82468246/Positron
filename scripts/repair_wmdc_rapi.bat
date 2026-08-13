@echo off
setlocal

set "PS32=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS32%" (
    echo ERROR: 32-bit Windows PowerShell was not found at "%PS32%".
    exit /b 3
)

"%PS32%" -NoProfile -NonInteractive -ExecutionPolicy Bypass ^
    -File "%~dp0repair_wmdc_rapi.ps1" %*
exit /b %ERRORLEVEL%

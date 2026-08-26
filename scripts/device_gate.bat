@echo off
setlocal

set "PS32=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS32%" (
    echo ERROR: 32-bit Windows PowerShell was not found at "%PS32%".
    exit /b 3
)

call "%~dp0repair_wmdc_rapi.bat" -QuietHealthy
if errorlevel 1 exit /b %ERRORLEVEL%

"%PS32%" -NoProfile -NonInteractive -ExecutionPolicy Bypass ^
    -File "%~dp0device_gate.ps1" %*
exit /b %ERRORLEVEL%

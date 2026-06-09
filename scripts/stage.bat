@echo off
REM Phase 2 staging: copy built artifacts to the WM6 emulator shared
REM folder C:\WMShare\.
REM
REM VS2008 Smart Device deploy is broken for this project (see PHASE1.md).
REM Workaround: use the emulator's shared folder. This script collects
REM the four binaries we need.

setlocal
set CFG=Debug
if not "%~1"=="" set CFG=%~1

set ROOT=%~dp0..
set STAGE=C:\WMShare

if not exist "%STAGE%" mkdir "%STAGE%"

echo Staging %CFG% artifacts to %STAGE% ...
copy /Y "%ROOT%\positron_tls\bin\%CFG%\positron_tls.dll"   "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_json\bin\%CFG%\positron_json.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_http\bin\%CFG%\positron_http.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_core\bin\%CFG%\positron_core.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\test_host\bin\%CFG%\test_host.exe"         "%STAGE%\" || goto :fail

echo.
echo Done. In the emulator, open File Explorer -^> Storage Card
echo and double-click test_host.exe.
echo.
exit /b 0

:fail
echo.
echo FAILED. Did you build the solution for configuration "%CFG%"?
exit /b 1

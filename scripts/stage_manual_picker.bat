@echo off
REM Stage the normal Debug artifacts, then replace only the staged test
REM selection with the manual real-WM6 file-picker package. The repository
REM test_host.ini remains the automatic smoke configuration.

setlocal
set CFG=Debug
if not "%~1"=="" set CFG=%~1

set ROOT=%~dp0..
set STAGE=C:\WMShare\Positron-manual-next295
if not "%~2"=="" set STAGE=%~2

call "%ROOT%\scripts\stage.bat" "%CFG%" "%STAGE%"
if errorlevel 1 exit /b %ERRORLEVEL%

copy /Y "%ROOT%\test_host\test_host_manual_picker.ini" "%STAGE%\test_host.ini" || goto :fail

echo.
echo Manual picker package staged to %STAGE%.
echo Run test_host.exe from the shared-folder equivalent on the connected WM6 device.
exit /b 0

:fail
echo.
echo FAILED. Could not install the manual picker INI in %STAGE%.
exit /b 1

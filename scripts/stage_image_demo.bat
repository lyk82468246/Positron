@echo off
REM Stage the independent positron_image consumer and its only product DLL.

setlocal
set CFG=Debug
if not "%~1"=="" set CFG=%~1

set ROOT=%~dp0..
set STAGE=C:\WMShare\Positron-image-demo
if not "%~2"=="" set STAGE=%~2

if not exist "%STAGE%" mkdir "%STAGE%"

echo Staging positron_image demo (%CFG%) to %STAGE% ...
copy /Y "%ROOT%\positron_image\bin\%CFG%\positron_image.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\samples\positron_image_demo\bin\%CFG%\positron_image_demo.exe" "%STAGE%\" || goto :fail

echo.
echo Done. Run positron_image_demo.exe from the emulator shared folder.
exit /b 0

:fail
echo.
echo FAILED. Build Positron.sln for configuration "%CFG%" first.
exit /b 1

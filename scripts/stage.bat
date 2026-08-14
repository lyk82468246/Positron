@echo off
REM Phase 2 staging: copy built artifacts to the WM6 emulator shared
REM folder C:\WMShare\.
REM
REM VS2008 Smart Device deploy is broken for this project
REM (see docs\history\PHASE1.md).
REM Workaround: use the emulator's shared folder. This script collects
REM the eight binaries and optional test selection file we need. Optional arg 2
REM selects an alternate folder.

setlocal
set CFG=Debug
if not "%~1"=="" set CFG=%~1

set ROOT=%~dp0..
set STAGE=C:\WMShare
if not "%~2"=="" set STAGE=%~2

echo Building %CFG% artifacts incrementally before staging ...
call "%ROOT%\scripts\build.bat" "%CFG%" build || goto :buildfail

if not exist "%STAGE%" mkdir "%STAGE%"

echo Staging %CFG% artifacts to %STAGE% ...
copy /Y "%ROOT%\positron_tls\bin\%CFG%\positron_tls.dll"   "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_json\bin\%CFG%\positron_json.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_http\bin\%CFG%\positron_http.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_core\bin\%CFG%\positron_core.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_image\bin\%CFG%\positron_image.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_script\bin\%CFG%\positron_script.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\positron_browser\bin\%CFG%\positron_browser.dll" "%STAGE%\" || goto :fail
copy /Y "%ROOT%\test_host\bin\%CFG%\test_host.exe"         "%STAGE%\" || goto :fail
copy /Y "%ROOT%\test_host\test_host.ini"                   "%STAGE%\" || goto :fail
if not exist "%STAGE%\fonts" mkdir "%STAGE%\fonts"
copy /Y "%ROOT%\assets\fonts\PositronSymbolsBasic.ttf" "%STAGE%\fonts\" || goto :fail
copy /Y "%ROOT%\assets\fonts\PositronSymbols.ttf" "%STAGE%\fonts\" || goto :fail
copy /Y "%ROOT%\assets\fonts\PositronEmoji.ttf"   "%STAGE%\fonts\" || goto :fail
copy /Y "%ROOT%\third_party\noto-symbols\OFL.txt" "%STAGE%\fonts\OFL-NotoSymbols.txt" || goto :fail
copy /Y "%ROOT%\third_party\noto-symbols2\OFL.txt" "%STAGE%\fonts\OFL-NotoSymbols2.txt" || goto :fail
copy /Y "%ROOT%\third_party\noto-emoji\OFL.txt"    "%STAGE%\fonts\OFL-NotoEmoji.txt" || goto :fail

echo.
echo Done. In the emulator, open File Explorer -^> Storage Card
echo and run test_host.exe from the shared-folder equivalent of %STAGE%.
echo.
exit /b 0

:buildfail
echo.
echo FAILED. Incremental %CFG% build did not complete; nothing was staged.
exit /b 1

:fail
echo.
echo FAILED. Did you build the solution for configuration "%CFG%"?
echo Also close any running test_host.exe that may lock the old binaries,
echo or stage to another folder: scripts\stage.bat %CFG% C:\WMShare\Positron-next
exit /b 1

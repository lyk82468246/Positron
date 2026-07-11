@echo off
setlocal

set "ROOT=%~dp0.."
set "SLN=%ROOT%\Positron.sln"
set "CFG=Debug"
set "ACTION=Build"
set "PLATFORM=Windows Mobile 6 Professional SDK (ARMV4I)"
set "LOG=%ROOT%\vs2008-build.log"

if not "%~1"=="" set "CFG=%~1"
if /I not "%CFG%"=="Debug" if /I not "%CFG%"=="Release" goto :usage

if not "%~2"=="" set "ACTION=%~2"
if /I "%ACTION%"=="build" set "ACTION=Build"
if /I "%ACTION%"=="rebuild" set "ACTION=Rebuild"
if /I "%ACTION%"=="clean" set "ACTION=Clean"
if /I not "%ACTION%"=="Build" if /I not "%ACTION%"=="Rebuild" if /I not "%ACTION%"=="Clean" goto :usage

set "DEVENV="
if defined VS90COMNTOOLS if exist "%VS90COMNTOOLS%..\IDE\devenv.com" set "DEVENV=%VS90COMNTOOLS%..\IDE\devenv.com"
if not defined DEVENV if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\Common7\IDE\devenv.com" set "DEVENV=%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\Common7\IDE\devenv.com"
if not defined DEVENV if exist "%ProgramFiles%\Microsoft Visual Studio 9.0\Common7\IDE\devenv.com" set "DEVENV=%ProgramFiles%\Microsoft Visual Studio 9.0\Common7\IDE\devenv.com"
if not defined DEVENV goto :missing

if exist "%LOG%" del /Q "%LOG%"
echo VS2008: "%DEVENV%"
echo Solution: "%SLN%"
echo Action: %ACTION% "%CFG%|%PLATFORM%"
echo Log: "%LOG%"
echo.

"%DEVENV%" "%SLN%" /%ACTION% "%CFG%|%PLATFORM%" /Out "%LOG%"
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo BUILD FAILED with exit code %RC%. See "%LOG%".
) else (
    echo BUILD SUCCEEDED. See "%LOG%".
)
exit /b %RC%

:usage
echo Usage: scripts\build.bat [Debug^|Release] [build^|rebuild^|clean]
exit /b 2

:missing
echo ERROR: Visual Studio 2008 devenv.com was not found.
echo Install VS2008 or define VS90COMNTOOLS.
exit /b 3

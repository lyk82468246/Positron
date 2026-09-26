@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Rebuild the checked-in FFmpeg ARMV4I archive without changing the source tree.
rem Required external inputs: VS2008, the WM6 SDK and c99-to-c89 1.0.3 c99conv.exe.

set "ROOT=%~dp0.."
set "SOURCE=%ROOT%\third_party\ffmpeg-3.4.14"
set "MANIFEST=%SOURCE%\positron_sources.txt"
set "BUILDROOT=%ROOT%\tmp\ffmpeg-armv4i-build"
set "OUTPUT=%SOURCE%\positron_ffmpeg_armv4i.lib"

if defined VS90ROOT goto :vs90root_ready
set "VS90ROOT=%ProgramFiles(x86)%\Microsoft Visual Studio 9.0"
:vs90root_ready
if defined WM6SDKROOT goto :wm6sdkroot_ready
set "WM6SDKROOT=%ProgramFiles(x86)%\Windows Mobile 6 SDK\PocketPC"
:wm6sdkroot_ready
if not defined C99CONV set "C99CONV=%ROOT%\third_party\c99-to-c89-1.0.3\c99conv.exe"

if exist "%VS90ROOT%\Common7\Tools\vsvars32.bat" goto :toolchain_ready
echo Missing VS2008 toolchain: %VS90ROOT%
exit /b 2
:toolchain_ready
if exist "%WM6SDKROOT%\Include\Armv4i" goto :sdk_ready
echo Missing Windows Mobile 6 SDK: %WM6SDKROOT%
exit /b 2
:sdk_ready
if exist "%C99CONV%" goto :converter_ready
echo Missing c99conv.exe: %C99CONV%
echo Install c99-to-c89 1.0.3 and set C99CONV to its c99conv.exe.
exit /b 2
:converter_ready
if exist "%MANIFEST%" goto :manifest_ready
echo Missing FFmpeg source manifest: %MANIFEST%
exit /b 2
:manifest_ready

call "%VS90ROOT%\Common7\Tools\vsvars32.bat" >nul
setlocal EnableExtensions EnableDelayedExpansion
set "VCROOT=%VS90ROOT%\VC"
set "ARMCC=%VCROOT%\ce\bin\x86_arm\cl.exe"
set "AR=%VCROOT%\ce\bin\x86_arm\lib.exe"
set "SDKINC=%WM6SDKROOT%\Include"
set "SDKLIB=%WM6SDKROOT%\Lib\Armv4i"
set "CEINC=%VCROOT%\ce\include"
set "CELIB=%VCROOT%\ce\lib\armv4i"
set "INCLUDE=%CEINC%;%SDKINC%\Armv4i;%SDKINC%;%INCLUDE%"
set "LIB=%CELIB%;%SDKLIB%;%VCROOT%\lib;%LIB%"

if exist "%ARMCC%" goto :compiler_ready
echo Missing ARM compiler: %ARMCC%
exit /b 2
:compiler_ready
if exist "%AR%" goto :librarian_ready
echo Missing ARM librarian: %AR%
exit /b 2
:librarian_ready

if exist "%BUILDROOT%" rmdir /s /q "%BUILDROOT%"
mkdir "%BUILDROOT%" || exit /b 1
mkdir "%BUILDROOT%\source" || exit /b 1
mkdir "%BUILDROOT%\pre" || exit /b 1
mkdir "%BUILDROOT%\converted" || exit /b 1
mkdir "%BUILDROOT%\obj\libavutil" || exit /b 1
mkdir "%BUILDROOT%\obj\libavcodec" || exit /b 1
mkdir "%BUILDROOT%\obj\libavformat" || exit /b 1

robocopy "%SOURCE%" "%BUILDROOT%\source" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto :copy_failed

pushd "%ROOT%"
git apply --no-index --whitespace=nowarn --directory=tmp/ffmpeg-armv4i-build/source third_party/ffmpeg-3.4.14/positron_patches/0001-avoid-vs2008-conditional-struct-initializer.patch
if errorlevel 1 goto :patch_failed
git apply --no-index --whitespace=nowarn --directory=tmp/ffmpeg-armv4i-build/source third_party/ffmpeg-3.4.14/positron_patches/0002-rename-flv-leave-label-for-c89-converter.patch
if errorlevel 1 goto :patch_failed
popd

set "LOG=%BUILDROOT%\compile.log"
set "FAILED=0"
for /f "usebackq eol=# delims=" %%F in ("%MANIFEST%") do call :compile_source "%%F"
if "%FAILED%" NEQ "0" goto :failed

set "RUNTIMEOBJ=%BUILDROOT%\obj\pmedia_ffmpeg_runtime.obj"
"%ARMCC%" -nologo -O1 -GS- -c -Fo"%RUNTIMEOBJ%" ^
    -I"%ROOT%\compat" -I"%ROOT%\positron_media" ^
    -D_WIN32_WCE=420 -DUNDER_CE -DWINCE -DARM -D_ARM_ -D_UNICODE -DUNICODE ^
    -FIstdlib.h -FIexcpt.h "%ROOT%\positron_media\pmedia_ffmpeg_runtime.c" >>"%LOG%" 2>&1
if errorlevel 1 goto :failed

set "RSP=%BUILDROOT%\objects.rsp"
if exist "%RSP%" del /q "%RSP%"
for /r "%BUILDROOT%\obj" %%F in (*.obj) do echo "%%F" >>"%RSP%"
"%AR%" /nologo /out:"%OUTPUT%" @"%RSP%" >>"%LOG%" 2>&1
if errorlevel 1 goto :failed

echo FFmpeg ARMV4I archive rebuilt: %OUTPUT%
certutil -hashfile "%OUTPUT%" SHA256 | findstr /v /i "CertUtil SHA256 hash"
exit /b 0

:compile_source
set "REL=%~1"
set "SUBDIR=libavutil"
if not "%REL:libavcodec/=%"=="%REL%" set "SUBDIR=libavcodec"
if not "%REL:libavformat/=%"=="%REL%" set "SUBDIR=libavformat"
for %%N in ("%REL%") do set "NAME=%%~nN"
set "SRC=%BUILDROOT%\source\%REL%"
set "PRE=%BUILDROOT%\pre\%NAME%.pre.c"
set "CONV=%BUILDROOT%\converted\%NAME%.c"
set "OBJ=%BUILDROOT%\obj\%SUBDIR%\%NAME%.obj"
echo [FFMPEG] %REL%>>"%LOG%"
"%ARMCC%" -nologo -EP ^
    -I"%ROOT%\compat" -I"%SOURCE%\positron_config" ^
    -I"%ROOT%\positron_media\ffmpeg_config" ^
    -I"%BUILDROOT%\source" -I"%BUILDROOT%\source\compat" ^
    -I"%BUILDROOT%\source\compat\atomics\dummy" ^
    -I"%BUILDROOT%\source\libavutil" -I"%BUILDROOT%\source\libavcodec" ^
    -I"%BUILDROOT%\source\libavformat" ^
    -D_WIN32_WCE=420 -D__MINGW32CE__ -DSTRSAFE_NO_DEPRECATE ^
    -D_ISOC99_SOURCE -DHAVE_AV_CONFIG_H -Dinline=__inline ^
    -FIstdlib.h -FIexcpt.h -FIpositron_ffmpeg.h ^
    "%SRC%" >"%PRE%" 2>>"%LOG%"
if errorlevel 1 (
    set "FAILED=1"
    exit /b 0
)
"%C99CONV%" -ms "%PRE%" "%CONV%" >>"%LOG%" 2>&1
if errorlevel 1 (
    set "FAILED=1"
    exit /b 0
)
"%ARMCC%" -nologo -O1 -GS- -c -Fo"%OBJ%" -I"%ROOT%\compat" "%CONV%" >>"%LOG%" 2>&1
if errorlevel 1 set "FAILED=1"
exit /b 0

:failed
echo FFmpeg ARMV4I archive rebuild failed. See %LOG%
exit /b 1

:copy_failed
echo Failed to copy FFmpeg source snapshot.
exit /b 1

:patch_failed
popd
echo Failed to apply FFmpeg C89 patches.
exit /b 1

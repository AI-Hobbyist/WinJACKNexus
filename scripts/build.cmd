@echo off
rem ===========================================================================
rem  WinJACKNexus - 构建脚本（Ninja）
rem  用法: scripts\build.cmd [目标...]
rem ===========================================================================
setlocal

set "VCVARS=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "NINJA=C:\Qt\Tools\Ninja\ninja.exe"
set "VSLANG=1033"

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    exit /b 1
)

call "%VCVARS%"
if errorlevel 1 exit /b 1

"%NINJA%" -C "%~dp0..\build" %*
exit /b %errorlevel%

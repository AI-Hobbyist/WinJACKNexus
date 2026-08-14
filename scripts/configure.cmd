@echo off
rem ===========================================================================
rem  WinJACKNexus - CMake 配置脚本（Ninja + MSVC x64）
rem  用法: scripts\configure.cmd [额外 CMake 参数...]
rem ===========================================================================
setlocal

set "VCVARS=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "VSLANG=1033"
set "SHOW_INCLUDES_PREFIX=Note: including file: "

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    exit /b 1
)

call "%VCVARS%"
if errorlevel 1 exit /b 1

"%CMAKE%" -G Ninja -S "%~dp0.." -B "%~dp0..\build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CL_SHOWINCLUDES_PREFIX="%SHOW_INCLUDES_PREFIX%" %*
exit /b %errorlevel%

@echo off
echo Starting CMake configuration for SAL C...
cd /d d:\NAS\ceph-20.1.1\build
cmake ..\src -DWITH_RGW_SAL_C=ON -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo CMake configuration complete.

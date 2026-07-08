@echo off
setlocal
call "D:\Programs\vs2026\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

:: VS 2026 preview vcvars64.bat does not set INCLUDE/LIB correctly.
:: Set them manually as a workaround.
set "MSVC_DIR=D:\Programs\vs2026\VC\Tools\MSVC\14.51.36231"
set "SDK_DIR=C:\Program Files (x86)\Windows Kits\10"
set "SDK_VER=10.0.26100.0"

set "INCLUDE=%MSVC_DIR%\include;%SDK_DIR%\Include\%SDK_VER%\ucrt;%SDK_DIR%\Include\%SDK_VER%\shared;%SDK_DIR%\Include\%SDK_VER%\um"
set "LIB=%MSVC_DIR%\lib\x64;%SDK_DIR%\Lib\%SDK_VER%\ucrt\x64;%SDK_DIR%\Lib\%SDK_VER%\um\x64"
set "PATH=%MSVC_DIR%\bin\Hostx64\x64;%PATH%"

cmake --build D:/projects/synthrt/cmake-build-release --config Release
endlocal

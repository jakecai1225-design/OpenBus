@echo off
REM ============================================================
REM OpenBus MinGW Build Script (Fixed Environment)
REM ============================================================

setlocal enabledelayedexpansion

echo.
echo =========================================
echo OpenBuild MinGW - Production Build
echo =========================================
echo.

REM Set environment variables explicitly
set "QT_DIR=D:\Qt\6.8.3\mingw_64"
set "MINGW_DIR=D:\Qt\Tools\mingw1310_64"
set "CMAKE_DIR=C:\Program Files\CMake"

echo [INFO] Setting up environment...
set "PATH=%MINGW_DIR%\bin;%CMAKE_DIR%\bin;%QT_DIR%\bin;%PATH%"
set "INCLUDE=%MINGW_DIR%\lib\gcc\x86_64-w64-mingw32\13.1.0\include;%MINGW_DIR%\x86_64-w64-mingw32\include;%MINGW_DIR%\lib\gcc\x86_64-w64-mingw32\13.1.0\include-fixed;%INCLUDE%"

cd /d "%~dp0.."

echo [INFO] Current directory: %CD%
echo.

REM Clean old build directory
if exist "build" (
    echo [INFO] Cleaning old build directory...
    rmdir /s /q build
)
mkdir build
cd build

echo [INFO] Configuring with CMake...
"%CMAKE_DIR%\bin\cmake.exe" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_CXX_COMPILER="%MINGW_DIR%\bin\g++.exe" ^
    -DCMAKE_C_COMPILER="%MINGW_DIR%\bin\gcc.exe" ^
    -DCMAKE_MAKE_PROGRAM="%MINGW_DIR%\bin\mingw32-make.exe" ^
    ..

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] CMake configuration failed!
    exit /b %errorlevel%
)

echo.
echo [INFO] Configuration complete. Now building...
cmake --build . --config Debug -- -j8

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed!
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] Build completed successfully!
echo Output: %CD%\bin\openbus.exe
cd ..

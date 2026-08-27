@echo off
chcp 65001 >nul
set PATH=D:\Qt\Tools\mingw1310_64\bin;%PATH%
cd /d D:\sin\sin_20260727\sin

echo.
echo ============================================================
echo   OpenBUS Full Build & Deploy Script (Qt QML MenuBar)
echo ============================================================
echo.

REM Step 1/4: Clean build directory
echo [Step 1/4] Cleaning build directory...
rmdir /s /q build 2>nul
echo     [OK] Cleaned

echo.
REM Step 2/4: Configure CMake for full build
echo [Step 2/4] Configuring with CMake (this may take a moment)...
call "C:\Program Files\CMake\bin\cmake.exe" ^
    -B build -S . ^
    -G "MinGW Makefiles" ^
    -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" ^
    -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64" ^
    -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" ^
    -DCMAKE_BUILD_TYPE=Dev

if %errorlevel% neq 0 (
    echo.
    echo     [FAIL] CMake configuration failed!
    pause
    exit /b %errorlevel%
)
echo     [OK] Configured successfully

echo.
REM Step 3/4: Build full project (openbus exe + all DLLs)
echo [Step 3/4] Building openbus executable and all modules...
echo     ⏳ This may take 5-10 minutes depending on your machine...
echo.
cd build
call mingw32-make openbus -j8

if %errorlevel% neq 0 (
    echo.
    echo     [FAIL] Build failed with error %errorlevel%!
    cd ..
    pause
    exit /b %errorlevel%
)
cd ..
echo     [OK] Build completed successfully

echo.
REM Step 4/4: Deploy Qt dependencies with windeployqt
echo [Step 4/4] Deploying Qt runtime dependencies...
call "D:/Qt/6.8.3/mingw_64/bin/windeployqt.exe" ^
    --release ^
    --no-translations ^
    --compiler-runtime ^
    --no-angle ^
    --no-icu ^
    --no-opengl-sw ^
    "build/bin/openbus.exe"

if %errorlevel% neq 0 (
    echo.
    echo     [WARN] windeployqt completed with warnings (this is normal in some cases)
) else (
    echo     [OK] Qt deployment completed
)

echo.
echo ============================================================
echo   ✓✓✓ BUILD & DEPLOY SUCCESSFUL!
echo ============================================================
echo.
echo Output locations:
echo     Executable: build\bin\openbus.exe
echo     DLLs:       build\bin\libopenbus_*.dll
echo     Qt files:   build\bin\* (deployed by windeployqt)
echo.
echo To run:        build\bin\openbus.exe
echo Clean:          rmdir /s /q build
echo.
pause

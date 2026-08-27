@echo off
chcp 65001 >nul
set PATH=D:\Qt\Tools\mingw1310_64\bin;%PATH%
cd /d D:\sin\sin_20260727\sin
echo.
echo ==========================================
echo   OpenBUS QML MenuBar - DLL Build Script
echo ==========================================
echo.
echo [Step 1/3] Clean build directory...
rmdir /s /q build 2>nul
echo [OK] Cleaned
echo.
echo [Step 2/3] Configure with CMake...
call "C:\Program Files\CMake\bin\cmake.exe" ^
    -B build -S . ^
    -G "MinGW Makefiles" ^
    -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" ^
    -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64" ^
    -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" ^
    -DCMAKE_BUILD_TYPE=Dev
if %errorlevel% neq 0 (
    echo.
    echo [FAIL] CMake configuration failed!
    pause
    exit /b %errorlevel%
)
echo [OK] Configured successfully!
echo.
echo [Step 3/3] Building openbus_qml_menu.dll...
cd build
call mingw32-make openbus_qml_menu -j8
if %errorlevel% equ 0 (
    cd ..
    echo.
    echo ==========================================
    echo [SUCCESS] ✓ Build completed!
    echo Output: build\bin\Release\openbus_qml_menu.dll
    echo ==========================================
    dir bin\Release\openbus_qml_menu.dll
) else (
    echo.
    echo [FAIL] Build failed with error %errorlevel%!
)
pause
exit /b %errorlevel%

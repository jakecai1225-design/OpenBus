@echo off
chcp 65001 >nul
cd /d %~dp0
echo.
echo [Step 1/3] Clean...
rmdir /s /q build 2>nul
echo [Step 2/3] CMake Configure...
call "C:\Program Files\CMake\bin\cmake.exe" -B build -S . -G "MinGW Makefiles" ^
    -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" ^
    -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64" ^
    -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" ^
    -DCMAKE_BUILD_TYPE=Dev
if %errorlevel% neq 0 (
    echo [FAIL] Configure failed!
    pause
    exit /b %errorlevel%
)
echo [OK] Configured
echo [Step 3/3] Build DLL...
cd build
call mingw32-make openbus_qml_menu -j8
cd ..
if %errorlevel% equ 0 (
    echo.
    echo [SUCCESS] Build completed!
    dir bin\Release\openbus_qml_menu.dll
) else (
    echo [FAIL] Build failed!
    pause
    exit /b 1
)

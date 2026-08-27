@echo off
chcp 65001 >nul
echo.
echo ===================================================
echo   OpenBUS QML MenuBar - 独立 DLL 编译脚本
echo ===================================================
echo.

cd /d %~dp0

REM 清理旧构建目录
if exist build (
    echo [1/4] 清理旧构建目录...
    rmdir /s /q build
    echo     ✓ 已清理
) else (
    echo [1/4] 无旧构建目录需要清理
)

REM CMake 配置
echo.
echo [2/4] CMake 配置...
echo     Qt 目录：D:\Qt\6.8.3\mingw_64
echo     MinGW: D:\Qt\Tools\mingw1310_64
echo     CMake: C:\Program Files\CMake
echo.

"%SystemDrive%\Program Files\CMake\bin\cmake.exe" ^
    -B build -S . ^
    -G "MinGW Makefiles" ^
    -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" ^
    -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64" ^
    -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" ^
    -DCMAKE_BUILD_TYPE=Dev

if %errorlevel% neq 0 (
    echo.
    echo ✗ CMake 配置失败！请检查错误信息。
    pause
    exit /b %errorlevel%
)

echo     ✓ 配置完成！

REM 编译 openbus_qml_menu DLL
echo.
echo [3/4] 编译 openbus_qml_menu DLL...
cd build
mingw32-make --target openbus_qml_menu -j8
cd ..

if %errorlevel% neq 0 (
    echo.
    echo ✗ 编译失败！请检查错误信息。
    pause
    exit /b %errorlevel%
)

echo     ✓ 编译完成！

REM 检查输出文件
echo.
echo [4/4] 检查构建结果...
if exist "build\bin\Release\openbus_qml_menu.dll" (
    echo     ✓ 生成：build\bin\Release\openbus_qml_menu.dll
    echo     ✓ 成功！
) else if exist "build\src\CMakeFiles\openbus_qml_menu.dir\RELEASE\*.dll" (
    for /f %%i in ('dir /b /o-d "build\src\CMakeFiles\openbus_qml_menu.dir\RELEASE\*.dll"') do (
        echo     ✓ 生成：%CD%\build\src\CMakeFiles\openbus_qml_menu.dir\RELEASE\%%i
    )
) else (
    echo     ! 未找到 dll 输出文件
)

echo.
echo ===================================================
echo   编译完成！
echo ===================================================
echo.
echo 下一步：
echo   1. 验证 QML 资源已正确加载
echo   2. 在 MainWindow 中集成 createQmlMenuBar()
echo   3. 链接 openbus_qml_menu DLL
echo.
pause

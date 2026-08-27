@echo off
REM ============================================
REM  Generate Qt6QuickWidgets import library (workaround for MinGW Qt)
REM ============================================
echo.
echo =========================================
echo  🛠️  Generating Qt6QuickWidgets import library
echo =========================================
echo.

cd /d D:\Qt\6.8.3\mingw_64\bin

REM Create DEF file using echo to avoid PowerShell encoding issues
echo LIBRARY Qt6QuickWidgets.dll > qtquickwidgets.def
echo EXPORTS >> qtquickwidgets.def
echo     @1 QQuickWidget::QQuickWidget@8 >> qtquickwidgets.def
echo     @2 QQuickWidget::QQuickWidget@12 >> qtquickwidgets.def  
echo     @3 QQuickWidget::~QQuickWidget@4 >> qtquickwidgets.def
echo     @4 QQuickWidget::setResizeMode@4 >> qtquickwidgets.def
echo     @5 QQuickWidget::resizeMode@4 >> qtquickwidgets.def
echo     @6 QQuickWidget::setSource@4 >> qtquickwidgets.def
echo     @7 QQuickWidget::source@4 >> qtquickwidgets.def
echo     @8 QQuickWidget::engine@4 >> qtquickwidgets.def
echo     @9 QQuickWidget::rootObject@4 >> qtquickwidgets.def
echo     @10 QQuickWidget::setContextProperty@8 >> qtquickwidgets.def
echo     @11 QQuickWidget::contextProperty@8 >> qtquickwidgets.def
echo     @12 QQuickWidget::keyPressEvent@8 >> qtquickwidgets.def
echo     @13 QQuickWidget::keyReleaseEvent@8 >> qtquickwidgets.def
echo     @14 QQuickWidget::mousePressEvent@8 >> qtquickwidgets.def
echo     @15 QQuickWidget::mouseMoveEvent@8 >> qtquickwidgets.def
echo     @16 QQuickWidget::mouseReleaseEvent@8 >> qtquickwidgets.def
echo     @17 QQuickWidget::wheelEvent@8 >> qtquickwidgets.def
echo     @18 QQuickWidget::focusInEvent@4 >> qtquickwidgets.def
echo     @19 QQuickWidget::focusOutEvent@4 >> qtquickwidgets.def
echo     @20 QQuickWidget::paintEvent@4 >> qtquickwidgets.def
echo     @21 QQuickWidget::resizeEvent@4 >> qtquickwidgets.def
echo     @22 QQuickWidget::hideEvent@4 >> qtquickwidgets.def
echo     @23 QQuickWidget::showEvent@4 >> qtquickwidgets.def
echo     @24 QQuickWidget::event@4 >> qtquickwidgets.def

echo [INFO] DEF file created: qtquickwidgets.def

REM Generate import library using dlltool from MinGW toolchain
D:\Qt\Tools\mingw1310_64\bin\dlltool.exe -d qtquickwidgets.def -l libQt6QuickWidgets.a -D Qt6QuickWidgets.dll

if exist libQt6QuickWidgets.a (
    echo.
    echo [SUCCESS] Import library generated successfully!
    echo          Location: D:/Qt/6.8.3/mingw_64/bin/libQt6QuickWidgets.a
    echo.
) else (
    echo.
    echo [FAIL] Failed to generate import library
    echo         This may require administrator privileges or a complete MinGW-w64 toolchain.
    echo.
    echo         Alternative: Switch to MSVC Qt installation which includes prebuilt libraries.
    echo.
)

pause

@echo off
set QT_QPA_PLATFORM=
set QT_ENABLE_HIGHDPI_SCALING=
set QT_SCALE_FACTOR_ROUNDING_POLICY=
set QT_SCALE_FACTOR=
set QT_AUTO_SCREEN_SCALE_FACTOR=
cd /d D:\code\openbus\sin\build\bin
openbus.exe
echo EXITCODE=%ERRORLEVEL% > D:\code\openbus\sin\tmp\ob_fix_exit.txt

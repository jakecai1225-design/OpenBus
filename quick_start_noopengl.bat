@echo off
chcp 65001 >nul
cd /d %~dp0
set QT_QUICK_BACKEND=software
start "" "build\bin\openbus.exe"
pause

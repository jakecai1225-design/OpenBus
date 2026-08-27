@echo off
chcp 65001 >nul
set QT_LOGGING_CONF=%~dp0qtlogging.conf
set QT_QPA_OFFSCREEN=1
start "" "build\bin\openbus.exe"
echo 已使用离屏模式启动 openbus
pause

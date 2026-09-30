@echo off
set QT_QPA_PLATFORM=offscreen
cd /d D:\code\openbus\sin\build\bin
openbus.exe
echo EXITCODE=%ERRORLEVEL%

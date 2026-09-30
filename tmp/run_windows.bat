@echo off
set QT_QPA_PLATFORM=windows
cd /d D:\code\openbus\sin\build\bin
openbus.exe
echo EXITCODE=%ERRORLEVEL%

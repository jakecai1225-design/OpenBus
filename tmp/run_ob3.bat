@echo off
set QT_QPA_PLATFORM=
set QT_PLUGIN_PATH=
set QT_QPA_PLATFORM_PLUGIN_PATH=
set QT_DEBUG_PLUGINS=1
set QT_LOGGING_RULES=*.debug=true
cd /d D:\code\openbus\sin\build\bin
openbus.exe > D:\code\openbus\sin\tmp\ob_out3.txt 2> D:\code\openbus\sin\tmp\ob_err3.txt
echo EXITCODE=%ERRORLEVEL% > D:\code\openbus\sin\tmp\ob_exit3.txt

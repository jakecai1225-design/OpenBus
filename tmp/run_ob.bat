set QT_LOGGING_RULES=*.debug=false
set QT_FATAL_WARNINGS=0
unset QT_QPA_PLATFORM 2>nul
cd /d D:\code\openbus\sin\build\bin
openbus.exe > D:\code\openbus\sin\tmp\ob_out.txt 2> D:\code\openbus\sin\tmp\ob_err.txt

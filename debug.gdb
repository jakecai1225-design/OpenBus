# GDB Debug Script for openbus.exe
set logging on
set logging file gdb_debug.log
set pagination off
run
bt full
info locals
thread apply all bt
frame 0
info registers
quit

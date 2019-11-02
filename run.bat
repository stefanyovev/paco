@set f=%1
@set f=%f:~1,-4%
@echo %1
@tcc -run c:\tcc\brace.c %f%.ic > %f%.c
@tcc %f%.c -llibportaudio64bit
@del %f%.c
@%f%
@echo.
@pause
@del %f%.exe
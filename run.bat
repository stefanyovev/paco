
@rem tcc is "Tiny C Compiler"

@tcc -run brace.c pac.ic>pac.c
@tcc -llibportaudio64bit pac.c -o pac.exe
@pac.exe

@echo.
@pause

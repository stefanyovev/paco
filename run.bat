
@rem tcc is "Tiny C Compiler"


@tcc -llibportaudio64bit paco.c -o paco.exe
@if %errorlevel% == 0 ( @paco.exe )


@echo.
@pause

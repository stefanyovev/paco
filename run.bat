@tcc -llibportaudio64bit main.c -o main.exe
@if %errorlevel% == 0 ( @main.exe )


@echo.
@pause

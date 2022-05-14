@echo.

@set LIBRARY_PATH=%~dp0%lib
@set PATH=%PATH%%LIBRARY_PATH%;

@tcc -llibportaudio64bit main.c -o main.exe

@if %errorlevel% == 0 ( @main.exe ) else ( @echo. & @pause )

@echo.

@set LIBRARY_PATH=%~dp0%lib
@set PATH=%PATH%%LIBRARY_PATH%;

@tcc -llibportaudio64bit main.c -o main.exe

if %errorlevel% neq 0 ( pause ) else ( where py & if %errorlevel% neq 0 ( main.exe ) else ( py -3 ui.py ) )

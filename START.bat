
@set LIBRARY_PATH=%~dp0%lib
@set PATH=%PATH%%LIBRARY_PATH%;

@tcc -lportaudio -luser32 -lgdi32 main.c -o paco.exe

@if %errorlevel% neq 0 goto error

@paco.exe
@goto endd

:error
@echo ERROR compiling main.c
@pause

:endd

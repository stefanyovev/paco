
@set LIBRARY_PATH=%~dp0%lib
@set PATH=%PATH%%LIBRARY_PATH%;

@tcc -llibportaudio64bit main.c -o main.exe
@if %errorlevel% neq 0 goto compileerror

@where py 2>NUL
@if %errorlevel% neq 0 goto nopy

@py -3 ui.py
@goto enddd

:nopy
@echo ************************************************
@echo *** Install Python to use this APP with GUI! ***
@echo ************************************************
@main.exe
@goto enddd

:compileerror
@echo ERROR compiling main.c
@pause

:enddd

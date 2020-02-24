
@rem ASSOC .IC files with this bat so you can 'execute document' in notepad2 or just double click it to run

tcc -run brace.c %1>%1.c
tcc -llibportaudio64bit %1.c -o paco3.exe

paco3.exe

@echo.
@pause


@rem ASSOC .IC files with this bat so you can 'execute document' in notepad2 or just double click it to run

@tcc -run brace.c %1>%1.c
@tcc "-run -llibportaudio64bit" %1.c %2 %3 %4 %5 %6 %7 %8 %9
@echo.
@pause

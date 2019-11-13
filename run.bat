@tcc -run brace.c %1>tmp.c
@tcc "-run -llibportaudio64bit" tmp.c %2 %3 %4 %5 %6 %7 %8 %9
@del tmp.c
@echo.
@pause
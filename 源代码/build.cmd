@echo off
REM classChat 编译脚本 (Dev-C++ MinGW64)
C:\PROGRA~2\Dev-Cpp\MinGW64\bin\windres.exe app.rc -O coff -o app_res.o
if errorlevel 1 (echo RES FAILED & exit /b 1)
C:\PROGRA~2\Dev-Cpp\MinGW64\bin\g++.exe -std=c++14 -municode -mwindows -O2 main.cpp app_res.o -o classChat.exe -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lole32 -lshell32 -lshlwapi -ladvapi32 -lws2_32 -lwinmm -lgdiplus -lavicap32 -luuid -lversion
if errorlevel 1 (echo BUILD FAILED & exit /b 1) else echo BUILD OK

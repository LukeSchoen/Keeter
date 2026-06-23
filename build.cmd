@echo off
setlocal
cd /d "%~dp0"

cpc.exe main.c -o keeter.exe -lwinmm -luser32 -lkernel32
if errorlevel 1 exit /b 1

echo Built keeter.exe

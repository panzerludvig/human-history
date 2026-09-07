@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /O2 /openmp /W3 /EHsc /std:c++17 src\dbg_qg2.cpp /Fo:build\ /Fe:build\dbg_qg2.exe
if errorlevel 1 exit /b 1
echo built build\dbg_qg2.exe

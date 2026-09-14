@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /O2 /openmp /W3 /EHsc /std:c++17 src\test_resources.cpp /Fo:build\ /Fe:build\test_resources.exe
if errorlevel 1 exit /b 1
echo built build\test_resources.exe

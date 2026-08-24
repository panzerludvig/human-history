@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build mkdir build
if not exist build\shaders mkdir build\shaders
copy /Y shaders\*.* build\shaders\ >nul
cl /nologo /O2 /openmp /W3 /EHsc /std:c++17 src\main.cpp /Fo:build\ /Fe:build\ironblood.exe /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup user32.lib gdi32.lib opengl32.lib
if errorlevel 1 exit /b 1
echo built build\ironblood.exe

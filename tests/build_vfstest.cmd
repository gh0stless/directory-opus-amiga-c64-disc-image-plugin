@echo off
rem Builds vfstest.exe (the VFS test host) into %1, default R:\probe
setlocal
set OUT=%~1
if "%OUT%"=="" set OUT=R:\probe
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
cl /nologo /c /std:c++17 /EHsc /O2 /I"%~dp0..\headers" /Fo"%OUT%\obj\\" "%~dp0vfstest.cpp" > "%OUT%\obj\build.log" 2>&1 || (type "%OUT%\obj\build.log" & exit /b 1)
link /nologo "%OUT%\obj\vfstest.obj" /OUT:"%OUT%\vfstest.exe" >> "%OUT%\obj\build.log" 2>&1 || (type "%OUT%\obj\build.log" & exit /b 1)
echo built %OUT%\vfstest.exe

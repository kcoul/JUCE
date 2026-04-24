@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b %errorlevel%

%*

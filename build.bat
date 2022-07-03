@echo off
.\sokol-shdc.exe --input src/loadpng-sapp.glsl --output include/loadpng-sapp.glsl.h --slang glsl330:hlsl5 || goto :error
mkdir build
pushd build
call vcvarsall.bat x64
cl /std:c11 /EHsc /Zi /nologo /I ..\include ..\src\*.c ..\src\spine\*.c /link /OUT:game.exe || goto :error
.\game.exe
popd
goto :EOF

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%

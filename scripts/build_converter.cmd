@echo off
setlocal EnableExtensions
call "%~dp0_init_msvc.cmd" || exit /b 1

set "ROOT=%~dp0.."
set "OUT=%ROOT%\artifacts\manager"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%ROOT%\manager"
cl /nologo /std:c++17 /EHsc /W4 /O2 /MD ACModernUIConverter.cpp ^
  /Fe:"%OUT%\ACModernUIConverter.exe" ^
  /link windowscodecs.lib ole32.lib
set "ERR=%ERRORLEVEL%"
popd
if not "%ERR%"=="0" exit /b %ERR%

echo Built %OUT%\ACModernUIConverter.exe
exit /b 0

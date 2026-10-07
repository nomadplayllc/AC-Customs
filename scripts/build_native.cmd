@echo off
setlocal EnableExtensions
call "%~dp0_init_msvc.cmd" || exit /b 1

set "ROOT=%~dp0.."
set "SRC=%ROOT%\plugin\native\ACCustoms.Native.cpp"
set "MH=%ROOT%\third_party\minhook"
set "OUT=%ROOT%\artifacts\plugin"

if not exist "%OUT%" mkdir "%OUT%"

pushd "%ROOT%\plugin\native"
cl /nologo /std:c++17 /EHsc /W4 /MT /LD ^
  /I"%MH%\include" ^
  "%SRC%" ^
  "%MH%\src\buffer.c" ^
  "%MH%\src\hook.c" ^
  "%MH%\src\trampoline.c" ^
  "%MH%\src\hde\hde32.c" ^
  /link user32.lib /OUT:"%OUT%\ACCustoms.Native.dll"
set "ERR=%ERRORLEVEL%"
popd

if not "%ERR%"=="0" exit /b %ERR%
echo Built %OUT%\ACCustoms.Native.dll
exit /b 0

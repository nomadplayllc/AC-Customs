@echo off
setlocal EnableExtensions
call "%~dp0_init_msvc.cmd" || exit /b 1

set "ROOT=%~dp0.."
set "IMGUI=%ROOT%\third_party\imgui"
set "OUT=%ROOT%\artifacts\manager"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\Defaults" mkdir "%OUT%\Defaults"

pushd "%ROOT%\manager"
cl /nologo /std:c++17 /EHsc /W4 /O2 /MD ^
  /I"%IMGUI%" /I"%IMGUI%\backends" ^
  ACModernUIManager.cpp ^
  "%IMGUI%\imgui.cpp" ^
  "%IMGUI%\imgui_draw.cpp" ^
  "%IMGUI%\imgui_tables.cpp" ^
  "%IMGUI%\imgui_widgets.cpp" ^
  "%IMGUI%\backends\imgui_impl_win32.cpp" ^
  "%IMGUI%\backends\imgui_impl_dx11.cpp" ^
  /Fe:"%OUT%\ACModernUIManager.exe" ^
  /link user32.lib gdi32.lib d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib comctl32.lib windowscodecs.lib ole32.lib comdlg32.lib
set "ERR=%ERRORLEVEL%"
popd
if not "%ERR%"=="0" exit /b %ERR%

copy /Y "%ROOT%\manager\Defaults\custom_tabs.txt" "%OUT%\Defaults\custom_tabs.txt" >nul
copy /Y "%ROOT%\manager\Defaults\texture_notes.txt" "%OUT%\Defaults\texture_notes.txt" >nul

echo Built %OUT%\ACModernUIManager.exe
exit /b 0

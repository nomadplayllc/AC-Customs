@echo off
setlocal EnableExtensions

call "%~dp0build_native.cmd" || exit /b 1
call "%~dp0build_managed.cmd" || exit /b 1
call "%~dp0build_converter.cmd" || exit /b 1
call "%~dp0build_manager.cmd" || exit /b 1

echo.
echo AC Customs build completed successfully.
echo Outputs are under: %~dp0..\artifacts
exit /b 0

@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "DLL=%ROOT%\artifacts\plugin\ACCustoms.dll"
set "REGASM=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\RegAsm.exe"

if not exist "%DLL%" (
  echo ERROR: %DLL% was not found. Run scripts\build_all.cmd first.
  exit /b 1
)
if not exist "%REGASM%" (
  echo ERROR: 32-bit .NET Framework RegAsm.exe was not found.
  exit /b 1
)

"%REGASM%" /codebase "%DLL%"
if errorlevel 1 exit /b 1

echo Registration completed. Open Decal and enable AC Customs in Manage Plugins.
exit /b 0

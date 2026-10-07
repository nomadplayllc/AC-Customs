@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0.."
set "PROJECT=%ROOT%\plugin\managed\ACCustoms.csproj"
set "MANAGED=%ROOT%\plugin\managed"
set "OUT=%ROOT%\artifacts\plugin"
set "DECAL_CACHE=%ROOT%\.build_decal_adapter_path.txt"

if not exist "%OUT%" mkdir "%OUT%"

rem Locate MSBuild.
set "MSBUILD_EXE="
for /f "delims=" %%I in ('where MSBuild.exe 2^>nul') do if not defined MSBUILD_EXE set "MSBUILD_EXE=%%I"

if not defined MSBUILD_EXE (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD_EXE set "MSBUILD_EXE=%%I"
    )
)

if not defined MSBUILD_EXE (
    echo ERROR: MSBuild.exe could not be located.
    exit /b 1
)

rem Locate rc.exe and generate the managed assembly's Win32 icon resource.
set "RC_EXE="
for /f "delims=" %%I in ('where rc.exe 2^>nul') do if not defined RC_EXE set "RC_EXE=%%I"
if not defined RC_EXE (
    for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$r='${env:ProgramFiles(x86)}\Windows Kits\10\bin'; if(Test-Path $r){Get-ChildItem $r -Filter rc.exe -File -Recurse -ErrorAction SilentlyContinue ^| Where-Object {$_.DirectoryName -match '\\x86$'} ^| Sort-Object FullName -Descending ^| Select-Object -First 1 -ExpandProperty FullName}"`) do if not defined RC_EXE set "RC_EXE=%%I"
)
if not defined RC_EXE (
    echo ERROR: rc.exe was not found. Install a Windows SDK through Visual Studio Installer.
    exit /b 1
)

pushd "%MANAGED%"
"!RC_EXE!" /nologo /fo ACCustoms.res ACCustoms.rc
if errorlevel 1 (
    popd
    exit /b 1
)
popd

rem Locate Decal.Adapter.dll. Decal supplies it at runtime; it is never vendored.
set "DECAL_ADAPTER="
if exist "%DECAL_CACHE%" (
    set /p DECAL_ADAPTER=<"%DECAL_CACHE%"
    if defined DECAL_ADAPTER if not exist "!DECAL_ADAPTER!" set "DECAL_ADAPTER="
)
if not defined DECAL_ADAPTER if exist "%ProgramFiles(x86)%\Decal 3.0\Decal.Adapter.dll" set "DECAL_ADAPTER=%ProgramFiles(x86)%\Decal 3.0\Decal.Adapter.dll"
if not defined DECAL_ADAPTER if exist "%ProgramFiles%\Decal 3.0\Decal.Adapter.dll" set "DECAL_ADAPTER=%ProgramFiles%\Decal 3.0\Decal.Adapter.dll"
if not defined DECAL_ADAPTER if exist "C:\Decal 3.0\Decal.Adapter.dll" set "DECAL_ADAPTER=C:\Decal 3.0\Decal.Adapter.dll"
if not defined DECAL_ADAPTER (
    for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$roots=@('C:\Games',$env:ProgramFiles,${env:ProgramFiles(x86)}); foreach($r in $roots){if($r -and (Test-Path $r)){ $f=Get-ChildItem $r -Filter Decal.Adapter.dll -File -Recurse -ErrorAction SilentlyContinue ^| Select-Object -First 1 -ExpandProperty FullName; if($f){$f;break}}}"`) do if not defined DECAL_ADAPTER set "DECAL_ADAPTER=%%I"
)

if not defined DECAL_ADAPTER (
    echo ERROR: Decal.Adapter.dll could not be found. Install Decal 3.0 before building the plugin.
    exit /b 1
)

for %%I in ("!DECAL_ADAPTER!") do set "DECAL_DIR=%%~dpI"
if "!DECAL_DIR:~-1!"=="\" set "DECAL_DIR=!DECAL_DIR:~0,-1!"
> "%DECAL_CACHE%" echo !DECAL_ADAPTER!

"!MSBUILD_EXE!" "%PROJECT%" /t:Rebuild /p:Configuration=Release /p:Platform=x86 "/p:DecalDir=!DECAL_DIR!" /m
if errorlevel 1 exit /b 1

copy /Y "%MANAGED%\bin\Release\ACCustoms.dll" "%OUT%\ACCustoms.dll" >nul || exit /b 1
if exist "%MANAGED%\bin\Release\ACCustoms.pdb" copy /Y "%MANAGED%\bin\Release\ACCustoms.pdb" "%OUT%\ACCustoms.pdb" >nul

echo Built %OUT%\ACCustoms.dll
exit /b 0

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Find-Tool([string]$Name) {
    $tool = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($tool) { return $tool.Source }
    return $null
}

function Find-Assembly([string]$Name, [string]$ExplicitDirectory) {
    if ($ExplicitDirectory) {
        $candidate = Join-Path $ExplicitDirectory $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        throw "$Name was not found in the supplied directory: $ExplicitDirectory"
    }
    $roots = @('C:\Games', 'C:\Decal 3.0', ${env:ProgramFiles(x86)}, $env:ProgramFiles) |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique
    foreach ($directory in $roots) {
        $candidate = Get-ChildItem -LiteralPath $directory -Filter $Name -File -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    return $null
}

try {
    $root = Split-Path -Parent $PSScriptRoot
    $managed = Join-Path $root 'plugin\managed'
    $out = Join-Path $root 'artifacts\plugin'
    $project = Join-Path $managed 'ACCustoms.csproj'

    Write-Host 'Locating MSBuild...'
    $msbuild = Find-Tool 'MSBuild.exe'
    if (-not $msbuild) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
                Select-Object -First 1
        }
    }
    if (-not $msbuild) { throw 'MSBuild.exe was not found. Install Visual Studio with MSBuild and .NET Framework 4.8 developer tools.' }

    Write-Host 'Locating Windows resource compiler...'
    $rc = Find-Tool 'rc.exe'
    if (-not $rc) {
        $sdkRoots = @()
        $kits = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
        if ($kits -and $kits.PSObject.Properties['KitsRoot10']) { $sdkRoots += $kits.KitsRoot10 }
        $sdkRoots += (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10')
        foreach ($sdkRoot in ($sdkRoots | Select-Object -Unique)) {
            $bin = Join-Path $sdkRoot 'bin'
            if (-not (Test-Path -LiteralPath $bin)) { continue }
            $candidate = Get-ChildItem -LiteralPath $bin -Filter rc.exe -File -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.Directory.Name -eq 'x86' } |
                Sort-Object FullName -Descending | Select-Object -First 1
            if ($candidate) { $rc = $candidate.FullName; break }
        }
    }
    if (-not $rc) { throw 'rc.exe was not found. Install a Windows 10 or Windows 11 SDK through Visual Studio Installer.' }

    Write-Host 'Locating Decal and VVS assemblies...'
    $decal = $null
    $cache = Join-Path $root '.build_decal_adapter_path.txt'
    if (-not $env:DECAL_DIR -and (Test-Path -LiteralPath $cache)) {
        $cached = (Get-Content -LiteralPath $cache -Raw).Trim()
        if ($cached -and (Test-Path -LiteralPath $cached -PathType Leaf)) { $decal = $cached }
    }
    if (-not $decal) { $decal = Find-Assembly 'Decal.Adapter.dll' $env:DECAL_DIR }
    if (-not $decal) { throw 'Decal.Adapter.dll was not found. Install Decal or set DECAL_DIR to its directory.' }
    $vvs = Find-Assembly 'VirindiViewService.dll' $env:VVS_DIR
    if (-not $vvs) { throw 'VirindiViewService.dll was not found. Install the Virindi Plugin Bundle or set VVS_DIR to its directory.' }
    Set-Content -LiteralPath $cache -Value $decal -Encoding ASCII

    Write-Host "MSBuild: $msbuild"
    Write-Host "Resource compiler: $rc"
    Write-Host "Decal: $decal"
    Write-Host "VVS: $vvs"
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    Push-Location -LiteralPath $managed
    try {
        & $rc /nologo /fo ACCustoms.res ACCustoms.rc
        if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed with exit code $LASTEXITCODE." }
    } finally { Pop-Location }

    $buildArguments = @($project, '/t:Rebuild', '/p:Configuration=Release', '/p:Platform=x86',
        ('/p:DecalDir=' + (Split-Path -Parent $decal)),
        ('/p:VvsDir=' + (Split-Path -Parent $vvs)), '/m')
    & $msbuild @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE. See the compiler errors above." }
    Copy-Item -LiteralPath (Join-Path $managed 'bin\Release\ACCustoms.dll') -Destination $out -Force
    $pdb = Join-Path $managed 'bin\Release\ACCustoms.pdb'
    if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination $out -Force }
    # The plugin project embeds Assets\template_mapping.json in ACCustoms.dll.
    # No separate mapping file or original-master PNG is needed at runtime.
    Write-Host 'Official texture mapping embedded in ACCustoms.dll.'

    Write-Host "Built $(Join-Path $out 'ACCustoms.dll')"
    exit 0
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

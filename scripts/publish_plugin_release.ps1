<#
Builds a versioned plugin release from TWO locally compiled DLLs.
Run after build_native.cmd and build_managed.cmd have succeeded.
The script does not push to GitHub; publish the generated DLLs and manifest
in one commit after final testing. It also creates a two-DLL download ZIP.
#>
param(
    [Parameter(Mandatory = $false)] [string] $ReleaseVersion = '',
    [string[]] $Notes = @(
        'Optional Auto Apply On Login remembers the last successfully applied theme',
        'Smoother theme switching with a three-textures-per-frame queue',
        'Animated theme swap progress bar and safer interrupted-apply recovery'
    )
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'artifacts\plugin'
$managed = Join-Path $bin 'ACCustoms.dll'
$native = Join-Path $bin 'ACCustoms.Native.dll'
if (!(Test-Path -LiteralPath $managed -PathType Leaf) -or
    !(Test-Path -LiteralPath $native -PathType Leaf)) {
    throw 'Build BOTH DLLs first using scripts\build_native.cmd and scripts\build_managed.cmd.'
}
# Prevent a stale 1.4/native DLL (or a failed build) from being published
# simply because a matching-version managed DLL happens to be present.
$buildInputs = @(
    @{ Output = $native; Source = 'plugin\native\ACCustoms.Native.cpp' },
    @{ Output = $managed; Source = 'plugin\managed\PluginCore.cs' },
    @{ Output = $managed; Source = 'plugin\managed\NativeBridge.cs' },
    @{ Output = $managed; Source = 'plugin\managed\mainView.xml' },
    @{ Output = $managed; Source = 'plugin\managed\ACCustoms.rc' },
    @{ Output = $managed; Source = 'plugin\managed\Properties\AssemblyInfo.cs' }
)
foreach ($pair in $buildInputs) {
    $source = Join-Path $root $pair.Source
    if ((Get-Item -LiteralPath $pair.Output).LastWriteTimeUtc -lt
        (Get-Item -LiteralPath $source).LastWriteTimeUtc) {
        throw "Build output is older than source '$($pair.Source)'. Rebuild both DLLs before publishing."
    }
}
$actualVersion = [Reflection.AssemblyName]::GetAssemblyName($managed).Version.ToString()
if (!$ReleaseVersion) { $ReleaseVersion = $actualVersion }
if ($ReleaseVersion -notmatch '^\d+\.\d+\.\d+(?:\.\d+)?$') { throw 'Invalid release version.' }
if ([version]$ReleaseVersion -ne [version]$actualVersion) {
    throw "Manifest version $ReleaseVersion must match ACCustoms.dll assembly version $actualVersion."
}
$latest = Join-Path $root 'updates\latest.json'
$folder = Join-Path $root ("updates\releases\v" + $ReleaseVersion)
New-Item -Path $folder -ItemType Directory -Force | Out-Null
Copy-Item -LiteralPath $managed -Destination (Join-Path $folder 'ACCustoms.dll') -Force
Copy-Item -LiteralPath $native -Destination (Join-Path $folder 'ACCustoms.Native.dll') -Force
$files = [ordered]@{}
$sha256 = [ordered]@{}
$bytes = [ordered]@{}
foreach ($name in @('ACCustoms.dll', 'ACCustoms.Native.dll')) {
    $path = Join-Path $folder $name
    $files[$name] = "releases/v$ReleaseVersion/$name"
    $sha256[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
    $bytes[$name] = (Get-Item -LiteralPath $path).Length
}
$manifest = [ordered]@{
    version = $ReleaseVersion
    changelog = @($Notes)
    files = $files
    sha256 = $sha256
    bytes = $bytes
}
# UTF-8 without BOM (GitHub raw). Pinned hashes cover the exact bytes uploaded.
$json = $manifest | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($latest, $json + "`n", (New-Object Text.UTF8Encoding($false)))
# Exactly two files in the public/manual-download ZIP, with no nested folder.
$zipDir = Join-Path $root 'zip download'
New-Item -Path $zipDir -ItemType Directory -Force | Out-Null
$zipPath = Join-Path $zipDir ("ACCustoms_Plugin_v" + $ReleaseVersion + '.zip')
# Use explicit archive entry names to guarantee two root-level DLL files.
# Compress-Archive may vary by PowerShell version; explicit entries avoid path ambiguity.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $zipPath -PathType Leaf) {
    Remove-Item -LiteralPath $zipPath -Force
}
$archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, (Join-Path $folder 'ACCustoms.dll'), 'ACCustoms.dll')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, (Join-Path $folder 'ACCustoms.Native.dll'), 'ACCustoms.Native.dll')
} finally { $archive.Dispose() }

# Validate exact root-level names, ignoring archive-entry order.
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $names = @($archive.Entries | ForEach-Object { $_.FullName })
    if ($names.Count -ne 2 -or
        -not ($names -ccontains 'ACCustoms.dll') -or
        -not ($names -ccontains 'ACCustoms.Native.dll')) {
        throw ('The manual download ZIP must contain exactly two root-level DLLs. ZIP entries: ' +
            ($names -join ', '))
    }
} finally { $archive.Dispose() }
Write-Host "Created GitHub manifest: $latest"
Write-Host "Created release assets: $folder"
Write-Host "Created manual download ZIP: $zipPath"
Write-Host 'After testing, commit/push updates/latest.json, both release DLLs and the versioned ZIP together.'
Write-Host 'The updater activates only after latest.json and both matching DLLs are available on main.'

<#
Creates a complete GitHub plugin update release from TWO compiled DLLs.
Run after build_native.cmd and build_managed.cmd have succeeded.
The script does not push to GitHub; review/commit the generated updates/ directory.
#>
param(
    [Parameter(Mandatory = $false)] [string] $ReleaseVersion = '',
    [string[]] $Notes = @(
        'One-click plugin updates inside Asheron''s Call',
        'New Update tab appears only when a newer plugin is available',
        'Verified downloads; automatic install after exiting the game'
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
Write-Host "Created GitHub manifest: $latest"
Write-Host "Created release assets: $folder"
Write-Host 'Commit/push updates/latest.json and the entire new updates/releases/v* folder to main.'
Write-Host "Old plugin builds will see the new version after they have installed a release containing this updater."

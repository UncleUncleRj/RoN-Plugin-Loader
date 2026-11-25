# Developer install: copies the built bootstrap, loader and HelloPlugin into a game folder, and
# records every path it creates in <game>\plugins\.dev-installed.txt so dev-uninstall.ps1 can remove
# exactly those. Refuses when shfolder.dll or RonPluginLoader.dll is already there without that record.
param([string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Rise of Nations')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$plugins = Join-Path $GameDir 'plugins'
$record = Join-Path $plugins '.dev-installed.txt'

if (-not (Test-Path (Join-Path $GameDir 'riseofnations.exe'))) {
    Write-Output "no riseofnations.exe in $GameDir"
    exit 1
}
$hasRecord = Test-Path $record
foreach ($f in 'shfolder.dll', 'RonPluginLoader.dll') {
    if ((Test-Path (Join-Path $GameDir $f)) -and -not $hasRecord) {
        Write-Output "refusing: $f is already in $GameDir and dev-install did not put it there"
        exit 1
    }
}

$created = [System.Collections.Generic.List[string]]::new()
if ($hasRecord) { Get-Content $record | Where-Object { $_ } | ForEach-Object { $created.Add($_) } }
function Add-Created([string]$path) { if (-not $created.Contains($path)) { $created.Add($path) } }

if (-not (Test-Path $plugins)) {
    New-Item -ItemType Directory $plugins | Out-Null
    Add-Created $plugins
}
$copies = @(
    @((Join-Path $build 'shfolder.dll'), (Join-Path $GameDir 'shfolder.dll')),
    @((Join-Path $build 'RonPluginLoader.dll'), (Join-Path $GameDir 'RonPluginLoader.dll')),
    @((Join-Path $build 'plugins\HelloPlugin.dll'), (Join-Path $plugins 'HelloPlugin.dll')),
    @((Join-Path $build 'plugins\HelloPlugin.ini'), (Join-Path $plugins 'HelloPlugin.ini'))
)
foreach ($c in $copies) {
    if (-not (Test-Path $c[0])) {
        Write-Output "missing $($c[0]); run build.ps1 first"
        exit 1
    }
}
foreach ($c in $copies) {
    Copy-Item $c[0] $c[1] -Force
    Add-Created $c[1]
}
Add-Created $record
Set-Content -Path $record -Value $created -Encoding UTF8
Write-Output "installed into $GameDir ($($created.Count) paths recorded)"

# Undoes dev-install.ps1: deletes exactly the paths in <game>\plugins\.dev-installed.txt, plus the
# loader's own loader.log and load-order.txt, then plugins\ if nothing else is left in it.
param([string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Rise of Nations')
$ErrorActionPreference = 'Stop'
$plugins = Join-Path $GameDir 'plugins'
$record = Join-Path $plugins '.dev-installed.txt'
if (-not (Test-Path $record)) {
    Write-Output "nothing recorded in $record; nothing removed"
    exit 1
}
$paths = @(Get-Content $record | Where-Object { $_ })
$removed = 0
foreach ($p in $paths) {
    if ((Test-Path $p -PathType Leaf) -and $p -ne $record) {
        Remove-Item -Force $p
        $removed++
    }
}
foreach ($f in 'loader.log', 'load-order.txt') {
    $p = Join-Path $plugins $f
    if (Test-Path $p) { Remove-Item -Force $p; $removed++ }
}
Remove-Item -Force $record
if ($paths -contains $plugins -and -not (Get-ChildItem -Force $plugins)) {
    Remove-Item -Force $plugins
}
Write-Output "removed $removed files from $GameDir"

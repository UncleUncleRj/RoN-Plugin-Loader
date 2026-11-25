# Builds the RoN Plugin Loader with zig cc (python -m pip install ziglang).
#   powershell -File build.ps1          build the DLLs and the example plugin into build\
#   powershell -File build.ps1 -Test    also build build\tests.exe (+ build\testgame\) and run the tests
param(
    [switch]$Test,
    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Rise of Nations'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $build | Out-Null

$cc = @('-m', 'ziglang', 'cc', '-target', 'x86-windows-gnu', '-O2', '-msse2', '-mstackrealign',
        '-I', (Join-Path $root 'include'), '-I', (Join-Path $root 'src'))

function Get-Sources([string[]]$patterns) {
    foreach ($p in $patterns) {
        Get-ChildItem -Path (Join-Path $root $p) -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
    }
}

function Invoke-CC([string]$what, [string[]]$ccArgs) {
    & python @cc @ccArgs
    if ($LASTEXITCODE) { throw "build failed: $what" }
}

if ($Test) {
    $src = @(Get-Sources @('tests\*.c', 'src\common\*.c', 'src\loader\*.c', 'src\bootstrap\decide.c'))
    $exe = Join-Path $build 'tests.exe'
    Invoke-CC 'tests.exe' (@('-o', $exe) + $src + @('-lkernel32', '-luser32', '-lbcrypt'))
    & $exe
    exit $LASTEXITCODE
}

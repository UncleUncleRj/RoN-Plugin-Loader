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

# The bootstrap (shfolder.dll) and the loader (RonPluginLoader.dll)
$boot = @('src\bootstrap\shfolder.c', 'src\bootstrap\decide.c', 'src\common\identity.c', 'src\bootstrap\shfolder.def') |
    ForEach-Object { Join-Path $root $_ }
Invoke-CC 'shfolder.dll' (@('-shared', '-s', '-o', (Join-Path $build 'shfolder.dll')) + $boot + @('-lkernel32', '-lbcrypt'))
$loaderSrc = @(Get-Sources @('src\loader\*.c', 'src\common\identity.c'))
Invoke-CC 'RonPluginLoader.dll' (@('-shared', '-s', '-o', (Join-Path $build 'RonPluginLoader.dll')) + $loaderSrc +
    @('-lkernel32', '-luser32'))
Write-Output 'built shfolder.dll, RonPluginLoader.dll'

if ($Test) {
    # build\testgame\plugins: test plugins for the loader tests (tests\plugins\*.c and their .ini files)
    $testgame = Join-Path $build 'testgame'
    if (Test-Path $testgame) { Remove-Item -Recurse -Force $testgame }
    $tp = Join-Path $testgame 'plugins'
    New-Item -ItemType Directory -Force $tp | Out-Null
    foreach ($p in 'Good', 'Zero', 'BadMajor') {
        Invoke-CC "$p.dll" @('-shared', '-o', (Join-Path $tp "$p.dll"), (Join-Path $root "tests\plugins\$p.c"), '-lkernel32')
    }
    Get-ChildItem $tp -Exclude '*.dll' | Remove-Item -Force   # import libraries and the like
    foreach ($copy in 'NoIni', 'MissingFolder', 'Twin') { Copy-Item (Join-Path $tp 'Good.dll') (Join-Path $tp "$copy.dll") }
    Copy-Item (Join-Path $root 'tests\plugins\*.ini') $tp
    $junk = New-Object byte[] 64
    (New-Object Random 7).NextBytes($junk)
    $junk[0] = 0
    [IO.File]::WriteAllBytes((Join-Path $tp 'Garbage.dll'), $junk)
    New-Item -ItemType Directory -Force (Join-Path $tp 'GoodFiles') | Out-Null

    $src = @(Get-Sources @('tests\*.c', 'src\common\*.c', 'src\loader\*.c', 'src\bootstrap\decide.c'))
    $exe = Join-Path $build 'tests.exe'
    Invoke-CC 'tests.exe' (@('-o', $exe) + $src + @('-lkernel32', '-luser32', '-lbcrypt'))
    & $exe
    exit $LASTEXITCODE
}

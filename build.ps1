# Builds the RoN Plugin Loader with zig cc (python -m pip install ziglang).
#   powershell -File build.ps1          build shfolder.dll (the loader, one file) and the example plugin into build\
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

# The loader, one file (user decision 2026-11-24): shfolder.dll = the start code (src\bootstrap) + the loader (src\loader)
$dllSrc = @(Get-Sources @('src\bootstrap\*.c', 'src\loader\*.c', 'src\common\*.c')) + @(Join-Path $root 'src\bootstrap\shfolder.def')
Invoke-CC 'shfolder.dll' (@('-shared', '-s', '-o', (Join-Path $build 'shfolder.dll')) + $dllSrc + @('-lkernel32', '-luser32', '-lbcrypt'))
Get-ChildItem $build -Filter 'RonPluginLoader.*' | Remove-Item -Force   # left over from the two-file builds
# The example plugin: build\plugins\HelloPlugin.dll + HelloPlugin.ini
$bp = Join-Path $build 'plugins'
New-Item -ItemType Directory -Force $bp | Out-Null
Invoke-CC 'HelloPlugin.dll' @('-shared', '-s', '-o', (Join-Path $bp 'HelloPlugin.dll'),
    (Join-Path $root 'plugins\HelloPlugin\HelloPlugin.c'), '-lkernel32')
Get-ChildItem $bp -Filter 'HelloPlugin.*' -Exclude 'HelloPlugin.dll' | Remove-Item -Force
Copy-Item (Join-Path $root 'plugins\HelloPlugin\HelloPlugin.ini') $bp
# Load-order test plugins: SplashTest1.dll and SplashTest2.dll from one source, their .ini files, and
# their videos in plugins\SplashTest1\ and plugins\SplashTest2\ (ffmpeg; skipped with a warning without it)
foreach ($n in 1, 2) {
    $name = "SplashTest$n"
    Invoke-CC "$name.dll" @('-shared', '-s', "-DSPLASH_N=$n", '-o', (Join-Path $bp "$name.dll"),
        (Join-Path $root 'plugins\SplashTest\SplashTest.c'), '-lkernel32')
    Get-ChildItem $bp -Filter "$name.*" -Exclude "$name.dll" | Remove-Item -Force
    Copy-Item (Join-Path $root "plugins\SplashTest\$name.ini") $bp
    & (Join-Path $root 'tools\make_splash_videos.ps1') -N $n -Out (Join-Path $bp $name)
}
Write-Output 'built shfolder.dll, plugins\HelloPlugin.dll, plugins\SplashTest1.dll, plugins\SplashTest2.dll'

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

    # build\testgame2\plugins: a C++ plugin and one that hooks too late (separate, so the counts above stay)
    $testgame2 = Join-Path $build 'testgame2'
    if (Test-Path $testgame2) { Remove-Item -Recurse -Force $testgame2 }
    $tp2 = Join-Path $testgame2 'plugins'
    New-Item -ItemType Directory -Force $tp2 | Out-Null
    Invoke-CC 'LateHook.dll' @('-shared', '-o', (Join-Path $tp2 'LateHook.dll'), (Join-Path $root 'tests\plugins\LateHook.c'), '-lkernel32')
    & python -m ziglang c++ -target x86-windows-gnu -O2 -shared -I (Join-Path $root 'include') -o (Join-Path $tp2 'CppGood.dll') (Join-Path $root 'tests\plugins\CppGood.cpp')
    if ($LASTEXITCODE) { throw 'build failed: CppGood.dll' }
    Get-ChildItem $tp2 -Exclude '*.dll' | Remove-Item -Force
    Copy-Item (Join-Path $root 'tests\plugins\LateHook.ini'), (Join-Path $root 'tests\plugins\CppGood.ini') $tp2

    # The menu code needs the game: tests\stub_menu.c stands in for menu.c, menu_draw.c and game.c
    $gameOnly = @('menu.c', 'menu_draw.c', 'game.c')
    $src = @(Get-Sources @('tests\*.c', 'src\common\*.c', 'src\loader\*.c', 'src\bootstrap\decide.c', 'src\bootstrap\start.c') |
        Where-Object { -not ($_ -like '*\src\loader\*' -and $gameOnly -contains (Split-Path $_ -Leaf)) })
    $exe = Join-Path $build 'tests.exe'
    Invoke-CC 'tests.exe' (@('-o', $exe) + $src + @('-lkernel32', '-luser32', '-lbcrypt'))
    & $exe
    exit $LASTEXITCODE
}

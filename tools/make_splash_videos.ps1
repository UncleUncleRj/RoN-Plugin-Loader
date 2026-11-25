# Makes the SplashTest plugins' videos with ffmpeg: 3 seconds of a still picture saying
# "Plugin <N> Success", in the two sizes of the game's first intro (1920x1080 and 1440x1080), WMV with
# a silent audio track like the stock intro (WMV3 + WMA2; ffmpeg writes WMV2, which DirectShow plays).
#   powershell -File tools\make_splash_videos.ps1 -N 1 -Out build\plugins\SplashTest1 [-Force]
# Without ffmpeg (on PATH or -Ffmpeg) it warns and makes nothing; existing videos are kept unless -Force.
param(
    [Parameter(Mandatory)][ValidateSet(1, 2)][int]$N,
    [Parameter(Mandatory)][string]$Out,
    [string]$Ffmpeg = 'ffmpeg',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command $Ffmpeg -ErrorAction SilentlyContinue)) {
    Write-Warning "ffmpeg not found: SplashTest$N gets no videos (install ffmpeg or pass -Ffmpeg)"
    return
}
New-Item -ItemType Directory -Force $Out | Out-Null
$bold = 'C\:/Windows/Fonts/georgiab.ttf'
$regular = 'C\:/Windows/Fonts/georgia.ttf'
foreach ($w in 1920, 1440) {
    $file = Join-Path $Out "splash_$w.wmv"
    if ((Test-Path $file) -and -not $Force) { continue }
    $vf = "drawtext=fontfile='$bold':text='Plugin $N Success':fontcolor=white:fontsize=110:x=(w-text_w)/2:y=(h-text_h)/2-30," +
          "drawtext=fontfile='$regular':text='SplashTest$N.dll':fontcolor=0x9A9A9A:fontsize=40:x=(w-text_w)/2:y=h/2+70," +
          'fade=t=in:st=0:d=0.4,fade=t=out:st=2.6:d=0.4'
    & $Ffmpeg -y -hide_banner -loglevel error `
        -f lavfi -i "color=c=0x0E0E0E:s=${w}x1080:r=30:d=3" -f lavfi -i 'anullsrc=r=44100:cl=stereo' `
        -vf $vf -c:v wmv2 -b:v 6M -pix_fmt yuv420p -c:a wmav2 -b:a 64k -t 3 -shortest $file
    if ($LASTEXITCODE) { throw "ffmpeg failed: $file" }
}

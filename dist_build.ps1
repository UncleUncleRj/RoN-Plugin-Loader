# Makes the release zip and nothing else: dist\RoN-Plugin-Loader-<version>.zip, holding shfolder.dll,
# installation.txt and LICENSE.txt, with the version from src\common\version.h. It builds no example
# or test plugins and runs no tests (build.ps1 -Test does).
#   ./dist_build.ps1   (from PowerShell; or powershell -File dist_build.ps1)
& (Join-Path $PSScriptRoot 'build.ps1') -LoaderOnly -Package

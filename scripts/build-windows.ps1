<#
.SYNOPSIS
  Build RecastDemo, mmtile_loadcheck and the unit tests on Windows (VS 2022, x64).

.DESCRIPTION
  Fetches the SDL2 VC development zip (SHA-256 checked) into RecastDemo/Contrib/SDL, where
  RecastDemo's CMake looks for it, then configures and builds into build/ (git-ignored).
  Detour is always 64-bit-polyref here (DT_POLYREF64 is #defined in DetourNavMesh.h), which
  is what AzerothCore tiles need. Re-running is safe; finished steps are skipped.
#>
param(
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'

$SdlVersion = '2.32.10'
$SdlSha256  = 'af347939395a58b365846aaea27391e69f9ec9d4dd650d6ac40802159b418a6e'

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $build | Out-Null

$sdlDir = Join-Path $root 'RecastDemo/Contrib/SDL'
if (-not (Test-Path (Join-Path $sdlDir 'include/SDL.h'))) {
    $dl  = Join-Path $build 'dl'
    $zip = Join-Path $dl "SDL2-devel-$SdlVersion-VC.zip"
    New-Item -ItemType Directory -Force $dl | Out-Null
    if (-not (Test-Path $zip)) {
        Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion/SDL2-devel-$SdlVersion-VC.zip" -OutFile $zip
    }
    $got = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    if ($got -ne $SdlSha256) { throw "SDL2 zip hash $got != $SdlSha256" }
    $unz = Join-Path $dl 'unzipped'
    if (Test-Path $unz) { Remove-Item -Recurse -Force $unz }
    Expand-Archive -Path $zip -DestinationPath $unz
    if (Test-Path $sdlDir) { Remove-Item -Recurse -Force $sdlDir }
    Move-Item (Join-Path $unz "SDL2-$SdlVersion") $sdlDir
}

$cmakeDir = Join-Path $build 'cmake'
cmake -S $root -B $cmakeDir -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $cmakeDir --config $Config --target RecastDemo mmtile_loadcheck Tests -- /m
if ($LASTEXITCODE) { throw 'cmake build failed' }

Get-ChildItem -Recurse $cmakeDir -Include RecastDemo.exe, mmtile_loadcheck.exe, Tests.exe | ForEach-Object FullName

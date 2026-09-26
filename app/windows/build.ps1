# Builds the 64-bit Windows console app with MSYS2 UCRT64 (gcc, cmake, ninja, pkgconf, libusb).
# From the repository root (the VS Code "Windows:" tasks do this):
#   app\windows\build.ps1 Debug      build in app\windows\build\Debug
#   app\windows\build.ps1 Release    build in app\windows\build\Release
#   app\windows\build.ps1 clean      remove app\windows\build
# MSYS2 location: -Msys2Root, else the MSYS2_ROOT environment variable, else C:\msys64.
# Run the program from app\windows\build: its default paths to cfg\ are ..\..\..\cfg.
param(
    [ValidateSet('Debug', 'Release', 'clean')]
    [string]$Type = 'Debug',
    [string]$Msys2Root = $env:MSYS2_ROOT
)

$out = Join-Path $PSScriptRoot 'build'

if ($Type -eq 'clean') {
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    Write-Host "Removed $out"
    exit 0
}

if (-not $Msys2Root) { $Msys2Root = 'C:\msys64' }
$bin = Join-Path $Msys2Root 'ucrt64\bin'
$missing = @('gcc.exe', 'cmake.exe', 'ninja.exe', 'pkg-config.exe') |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $bin $_)) }
if ($missing) {
    Write-Host "Not found in ${bin}: $($missing -join ', ')"
    Write-Host 'Install MSYS2 (https://www.msys2.org), then in an MSYS2 UCRT64 shell:'
    Write-Host '  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-gdb'
    Write-Host 'If MSYS2 is not in C:\msys64, set "a2b.msys2Root" in VS Code or pass -Msys2Root.'
    exit 1
}

# UCRT64 first on PATH: cmake then finds its gcc, ninja and pkg-config (not another MinGW).
$env:PATH = "$bin;$env:PATH"
$build = Join-Path $out $Type

& (Join-Path $bin 'cmake.exe') -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Type"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $bin 'cmake.exe') --build $build
exit $LASTEXITCODE

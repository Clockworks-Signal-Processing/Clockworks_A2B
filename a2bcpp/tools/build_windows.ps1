# Builds the bench console (a2b_bench.exe, the ADI USBi) with MSYS2 UCRT64 (gcc, cmake, ninja,
# pkgconf, libusb). From the repository root (the VS Code "Windows bench:" tasks do this):
#   a2bcpp\tools\build_windows.ps1 Debug      build in build\a2b-bench-windows\Debug
#   a2bcpp\tools\build_windows.ps1 Release    build in build\a2b-bench-windows\Release
#   a2bcpp\tools\build_windows.ps1 clean      remove build\a2b-bench-windows
# a2b_bench.exe and libusb-1.0.dll are in <build folder>\examples\bench. The unit tests are
# built by run_tests.ps1, in their own folder.
# MSYS2 location: -Msys2Root, else the MSYS2_ROOT environment variable, else C:\msys64.
param(
    [ValidateSet('Debug', 'Release', 'clean')]
    [string]$Type = 'Debug',
    [string]$Msys2Root = $env:MSYS2_ROOT
)

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$out = Join-Path $root 'build\a2b-bench-windows'

if ($Type -eq 'clean') {
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    Write-Host "Removed $out"
    exit 0
}

if (-not $Msys2Root) { $Msys2Root = 'C:\msys64' }
$bin = Join-Path $Msys2Root 'ucrt64\bin'
$missing = @('g++.exe', 'cmake.exe', 'ninja.exe', 'pkg-config.exe') |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $bin $_)) }
if ($missing) {
    Write-Host "Not found in ${bin}: $($missing -join ', ')"
    Write-Host 'Install MSYS2 (https://www.msys2.org), then in an MSYS2 UCRT64 shell:'
    Write-Host '  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-gdb'
    Write-Host 'If MSYS2 is not in C:\msys64, set "a2b.msys2Root" in VS Code or pass -Msys2Root.'
    exit 1
}

# UCRT64 first on PATH: cmake then finds its g++, ninja and pkg-config (not another MinGW).
$env:PATH = "$bin;$env:PATH"
$build = Join-Path $out $Type

# A2B_CPP_USBI=ON: a missing libusb is a configure error that says what to install, not a
# missing a2b_bench target.
& (Join-Path $bin 'cmake.exe') -S (Join-Path $root 'a2bcpp') -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Type" -DA2B_CPP_TESTS=OFF -DA2B_CPP_USBI=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $bin 'cmake.exe') --build $build --target a2b_bench
exit $LASTEXITCODE

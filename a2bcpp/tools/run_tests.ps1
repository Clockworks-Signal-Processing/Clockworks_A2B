# Builds the a2bcpp unit tests with MSYS2 UCRT64 (gcc, cmake, ninja) and runs them with CTest.
# With pkgconf and libusb installed it also builds the bench console (the USBi), in
# build\a2bcpp-windows\examples\bench\a2b_bench.exe (a2bcpp/docs/bench_console.md).
# From the repository root:
#   a2bcpp\tools\run_tests.ps1          build in build\a2bcpp-windows and run the tests
#   a2bcpp\tools\run_tests.ps1 clean    remove build\a2bcpp-windows
# MSYS2 location: -Msys2Root, else the MSYS2_ROOT environment variable, else C:\msys64.
param(
    [ValidateSet('test', 'clean')]
    [string]$Action = 'test',
    [string]$Msys2Root = $env:MSYS2_ROOT
)

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$build = Join-Path $root 'build\a2bcpp-windows'

if ($Action -eq 'clean') {
    if (Test-Path -LiteralPath $build) { Remove-Item -LiteralPath $build -Recurse -Force }
    Write-Host "Removed $build"
    exit 0
}

if (-not $Msys2Root) { $Msys2Root = 'C:\msys64' }
$bin = Join-Path $Msys2Root 'ucrt64\bin'
$missing = @('g++.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe') |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $bin $_)) }
if ($missing) {
    Write-Host "Not found in ${bin}: $($missing -join ', ')"
    Write-Host 'In an MSYS2 UCRT64 shell:'
    Write-Host '  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja'
    Write-Host 'If MSYS2 is not in C:\msys64, set "a2b.msys2Root" in VS Code or pass -Msys2Root.'
    exit 1
}

$usbPc = Join-Path $Msys2Root 'ucrt64\lib\pkgconfig\libusb-1.0.pc'
$hasUsb = (Test-Path -LiteralPath (Join-Path $bin 'pkg-config.exe')) -and (Test-Path -LiteralPath $usbPc)
if (-not $hasUsb) {
    Write-Host 'pkgconf or libusb not installed: the tests build, the bench console does not. For it:'
    Write-Host '  pacman -S --needed mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libusb'
}

# UCRT64 first on PATH: cmake finds its g++ and pkg-config, and the tests find its runtime DLLs.
$env:PATH = "$bin;$env:PATH"
& (Join-Path $bin 'cmake.exe') -S (Join-Path $root 'a2bcpp') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $bin 'cmake.exe') --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $bin 'ctest.exe') --test-dir $build --output-on-failure
$code = $LASTEXITCODE
$bench = Join-Path $build 'examples\bench\a2b_bench.exe'
if (Test-Path -LiteralPath $bench) { Write-Host "Bench console: $bench" }
exit $code

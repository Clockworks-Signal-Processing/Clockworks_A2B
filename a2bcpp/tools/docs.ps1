# Builds the a2bcpp Doxygen docs on Windows and fails on any Doxygen warning.
# From the repository root:
#   a2bcpp\tools\docs.ps1           development docs, no graphs  -> build\a2bcpp-doxygen\html
#   a2bcpp\tools\docs.ps1 release   published docs, with graphs  -> build\a2bcpp-doxygen-release\html
# Doxygen is taken from PATH, else from C:\Program Files\doxygen\bin. The release docs
# also need Graphviz's dot on PATH.
# ClockWorks Signal Processing LLC
param(
    [ValidateSet('dev', 'release')]
    [string]$Kind = 'dev'
)

$a2bcpp = Resolve-Path (Join-Path $PSScriptRoot '..')
$doxygen = (Get-Command doxygen -ErrorAction SilentlyContinue).Source
if (-not $doxygen) {
    $doxygen = 'C:\Program Files\doxygen\bin\doxygen.exe'
    if (-not (Test-Path -LiteralPath $doxygen)) {
        Write-Host 'Doxygen not found: install it from https://www.doxygen.nl or put it on PATH.'
        exit 1
    }
}
$config = 'Doxyfile'
$output = 'a2bcpp-doxygen'
if ($Kind -eq 'release') {
    if (-not (Get-Command dot -ErrorAction SilentlyContinue)) {
        Write-Host "Graphviz's dot is not on PATH; the release docs need it for the graphs."
        exit 1
    }
    $config = 'Doxyfile.release'
    $output = 'a2bcpp-doxygen-release'
}

# Start from an empty folder so pages of removed code do not linger.
$outDir = Join-Path $a2bcpp "..\build\$output"
if (Test-Path -LiteralPath $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }

Push-Location $a2bcpp
try {
    $messages = @(& $doxygen $config 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
$messages | ForEach-Object { Write-Host $_ }
if ($code -ne 0 -or $messages.Count -gt 0) {
    Write-Host "Doxygen reported problems (exit $code, $($messages.Count) message lines)."
    exit 1
}
Write-Host "Docs: $(Resolve-Path (Join-Path $outDir 'html\index.html'))"
exit 0

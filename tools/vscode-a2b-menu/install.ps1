# Packages this folder as a .vsix and installs it into VS Code. Run it once on each
# machine, and again after changing the extension; then run "Developer: Reload Window".
#   powershell -ExecutionPolicy Bypass -File tools\vscode-a2b-menu\install.ps1
# No Node.js or vsce needed: a .vsix is a zip with the extension under extension/.
$ErrorActionPreference = 'Stop'

$src = $PSScriptRoot
$pkg = Get-Content -Raw -LiteralPath (Join-Path $src 'package.json') | ConvertFrom-Json
$stage = Join-Path $env:TEMP "$($pkg.name)-vsix"
$vsix = Join-Path $env:TEMP "$($pkg.name)-$($pkg.version).vsix"
$utf8 = New-Object System.Text.UTF8Encoding $false

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
$ext = New-Item -ItemType Directory -Force (Join-Path $stage 'extension')
foreach ($item in 'package.json', 'extension.js', 'README.md', 'media') {
    Copy-Item -LiteralPath (Join-Path $src $item) -Destination $ext.FullName -Recurse
}

$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($pkg.name)" Version="$($pkg.version)" Publisher="$($pkg.publisher)" />
    <DisplayName>$($pkg.displayName)</DisplayName>
    <Description xml:space="preserve">$($pkg.description)</Description>
    <Categories>Other</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($pkg.engines.vscode)" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value="" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
  </Assets>
</PackageManifest>
"@
$types = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json" />
  <Default Extension=".js" ContentType="application/javascript" />
  <Default Extension=".md" ContentType="text/markdown" />
  <Default Extension=".svg" ContentType="image/svg+xml" />
  <Default Extension=".vsixmanifest" ContentType="text/xml" />
</Types>
"@
[System.IO.File]::WriteAllText((Join-Path $stage 'extension.vsixmanifest'), $manifest, $utf8)
[System.IO.File]::WriteAllText((Join-Path $stage '[Content_Types].xml'), $types, $utf8)

# Zip with forward-slash entry names (what VS Code expects)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $vsix) { Remove-Item -LiteralPath $vsix -Force }
$zip = [System.IO.Compression.ZipFile]::Open($vsix, 'Create')
try {
    Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $entry = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $entry)
    }
} finally {
    $zip.Dispose()
}
Remove-Item -LiteralPath $stage -Recurse -Force

Write-Host "Packaged $vsix"
& code --install-extension $vsix --force
if ($LASTEXITCODE -ne 0) { throw "code --install-extension failed ($LASTEXITCODE)" }
Write-Host 'Installed. In VS Code, run "Developer: Reload Window" to load it.'

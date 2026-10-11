param([string]$Build = "build")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $root $Build
$binary = Join-Path $buildRoot "Release"
$output = Join-Path $root "out\MS-Visualizer"
New-Item -ItemType Directory -Force $output | Out-Null
Copy-Item "$binary\*.exe", "$binary\*.dll" $output
Copy-Item (Join-Path $root "presets") $output -Recurse -Force
Copy-Item (Join-Path $root "Start MS-Visualizer.cmd"), (Join-Path $root "README.md"), `
  (Join-Path $root "LICENSE"), (Join-Path $root "THIRD-PARTY-NOTICES.md"), `
  (Join-Path $binary "projectM-LICENSE.txt") $output
Copy-Item (Join-Path $PSScriptRoot "start-desktop.ps1") $output
Copy-Item (Join-Path $PSScriptRoot "install-presets.ps1") $output
Copy-Item (Join-Path $root "Install More Presets.cmd") $output
$licenses = Join-Path $output "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
Get-ChildItem "$buildRoot\vcpkg_installed\x64-windows\share" -Directory | ForEach-Object {
  if (Test-Path (Join-Path $_.FullName "copyright")) {
    Copy-Item (Join-Path $_.FullName "copyright") (Join-Path $licenses "$($_.Name).txt")
  }
}
Copy-Item "$buildRoot\_deps\imgui-src\LICENSE.txt" (Join-Path $licenses "imgui.txt")
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$runtime = Get-ChildItem "$vs\VC\Redist\MSVC" -Directory -Recurse |
  Where-Object { $_.Name -like "Microsoft.VC*.CRT" -and $_.Parent.Name -eq "x64" } |
  Sort-Object FullName -Descending | Select-Object -First 1
if (!$runtime) { throw "Visual C++ x64 runtime directory not found under $vs" }
Copy-Item (Join-Path $runtime.FullName "*.dll") $output
Compress-Archive -Path $output -DestinationPath (Join-Path $root "out\MS-Visualizer-windows-x64.zip") -Force
Write-Host "Portable release: out\MS-Visualizer-windows-x64.zip"

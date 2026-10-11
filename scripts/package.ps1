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
$crt = Get-ChildItem "$vs\VC\Redist\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$runtime = Join-Path $crt.FullName "x64\Microsoft.VC143.CRT"
if (!(Test-Path $runtime)) { throw "Visual C++ runtime directory not found" }
Copy-Item "$runtime\*.dll" $output
Compress-Archive -Path $output -DestinationPath (Join-Path $root "out\MS-Visualizer-windows-x64.zip") -Force
Write-Host "Portable release: out\MS-Visualizer-windows-x64.zip"

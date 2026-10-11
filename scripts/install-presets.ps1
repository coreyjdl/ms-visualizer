param(
  [string]$AppDirectory = (Split-Path $PSScriptRoot -Parent),
  [string]$PresetArchive,
  [string]$TextureArchive
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (!(Test-Path (Join-Path $AppDirectory "MS-Visualizer.exe"))) {
  if (Test-Path (Join-Path $PSScriptRoot "MS-Visualizer.exe")) { $AppDirectory = $PSScriptRoot }
  else { throw "Specify -AppDirectory pointing to the extracted MS-Visualizer folder." }
}
$destination = Join-Path $AppDirectory "presets\Cream-of-the-Crop"
if (Test-Path $destination) { throw "Cream-of-the-Crop is already installed at $destination" }
$staging = Join-Path $AppDirectory (".preset-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $staging | Out-Null
function Get-Pack($archive, $repo, $revision, $hash, $name) {
  if (!$archive) {
    $archive = Join-Path $staging $name
    Write-Host "Downloading $repo..."
    Invoke-WebRequest -UseBasicParsing -Headers @{"User-Agent" = "MS-Visualizer"} `
      "https://api.github.com/repos/projectM-visualizer/$repo/zipball/$revision" -OutFile $archive
  }
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash -ne $hash) {
    throw "Archive verification failed for $repo. Nothing has been installed."
  }
  return (Resolve-Path -LiteralPath $archive).Path
}
function Copy-Entry($entry, $path) {
  $inputStream = $entry.Open()
  try {
    $outputStream = [IO.File]::Create($path)
    try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
  } finally { $inputStream.Dispose() }
}
try {
  $presets = Get-Pack $PresetArchive "presets-cream-of-the-crop" `
    "0180df21f5e0bd39b9060cc5de420ed2f1f9e509" `
    "454c2259c39604db6d6c3a5c2ddb88f02c4e1eaa967689a84d90518c564f6f8a" "presets.zip"
  $textures = Get-Pack $TextureArchive "presets-milkdrop-texture-pack" `
    "6368812f27bc747b517218fbf89d21d59afce4d9" `
    "d3c31a91f74ed88294775af9ab652745317e867088b37b94b6820706685beb98" "textures.zip"
  $pack = Join-Path $staging "Cream-of-the-Crop"
  New-Item -ItemType Directory $pack | Out-Null
  $zip = [IO.Compression.ZipFile]::OpenRead($presets)
  $count = 0
  $mapping = @()
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    foreach ($entry in $zip.Entries) {
      if ($entry.Name -like "*.milk") {
        # Short, unique names avoid Windows MAX_PATH failures in the upstream folder tree.
        $stem = [IO.Path]::GetFileNameWithoutExtension($entry.Name)
        if ($stem.Length -gt 100) { $stem = $stem.Substring(0, 100) }
        $suffix = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($entry.FullName)))).Replace("-", "").Substring(0, 12)
        $name = "$stem-$suffix.milk"
        Copy-Entry $entry (Join-Path $pack $name)
        $mapping += [pscustomobject]@{ Installed = $name; Original = $entry.FullName }
        $count++
      } elseif ($entry.Name -eq "LICENSE.md" -or $entry.Name -eq "README.md") {
        Copy-Entry $entry (Join-Path $pack ("UPSTREAM-" + $entry.Name))
      }
    }
  } finally { $zip.Dispose(); $sha.Dispose() }
  if ($count -ne 9795) { throw "Expected 9795 presets, found $count. Nothing has been installed." }
  $mapping | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $pack "preset-index.csv")
  $zip = [IO.Compression.ZipFile]::OpenRead($textures)
  $textureCount = 0
  try {
    foreach ($entry in $zip.Entries) {
      if ($entry.FullName -match "/textures/" -and $entry.Name) {
        Copy-Entry $entry (Join-Path $pack $entry.Name)
        $textureCount++
      } elseif ($entry.Name -eq "README.md") {
        Copy-Entry $entry (Join-Path $pack "UPSTREAM-textures-README.md")
      }
    }
  } finally { $zip.Dispose() }
  if (!$textureCount) { throw "Companion texture archive was empty. Nothing has been installed." }
  Move-Item -LiteralPath $pack -Destination $destination
  Write-Host "Installed $count additional presets and $textureCount textures."
  Write-Host "Restart MS-Visualizer to load them. Preset authors retain their rights; see UPSTREAM-LICENSE.md."
} finally {
  Remove-Item -LiteralPath $staging -Recurse -Force
}

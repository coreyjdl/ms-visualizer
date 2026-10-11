param(
  [string]$AppDirectory = (Join-Path $env:LOCALAPPDATA "Programs\MS-Visualizer"),
  [ValidateSet("Normal", "Smoke", "Capture", "Microphone", "Inputs")][string]$Mode = "Normal",
  [switch]$CreateShortcut
)
$ErrorActionPreference = "Stop"
$exe = Join-Path $AppDirectory "MS-Visualizer.exe"
if (!(Test-Path $exe)) { throw "Application not found: $exe" }
$arguments = switch ($Mode) {
  "Smoke" { "--smoke-test" }
  "Capture" { "--capture-test" }
  "Microphone" { "--capture-test --microphone" }
  "Inputs" { "--list-inputs" }
  default { "" }
}
# SSH processes live outside the interactive desktop. Use the already logged-in
# user's token, without storing a password or starting a privileged service.
$user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
if ($arguments) {
  $action = New-ScheduledTaskAction -Execute $exe -Argument $arguments -WorkingDirectory $AppDirectory
} else {
  $action = New-ScheduledTaskAction -Execute $exe -WorkingDirectory $AppDirectory
}
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero)
$taskName = "MS-Visualizer-$Mode"
Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
if ($CreateShortcut) {
  $shell = New-Object -ComObject WScript.Shell
  $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath("Desktop")) "MS-Visualizer.lnk"))
  $shortcut.TargetPath = $exe
  $shortcut.WorkingDirectory = $AppDirectory
  $shortcut.Save()
}
Write-Host "Started $Mode on the logged-in desktop as $user."
Write-Host "Task: $taskName"

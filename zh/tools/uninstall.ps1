# Uninstall the current user's Sentinel screen saver.
$ErrorActionPreference = 'SilentlyContinue'

$desktop = 'HKCU:\Control Panel\Desktop'
Remove-ItemProperty -Path $desktop -Name 'SCRNSAVE.EXE'
Set-ItemProperty -Path $desktop -Name 'ScreenSaveActive' -Value '0'

$dir = Join-Path $env:LOCALAPPDATA 'Sentinel'
if (Test-Path $dir) { Remove-Item -Path $dir -Recurse -Force }

Write-Host 'Sentinel screen saver removed. The Windows screen saver setting has been cleared.'
Start-Process -FilePath 'control.exe' -ArgumentList 'desk.cpl,,1'

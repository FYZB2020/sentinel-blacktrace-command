# Install Sentinel v2.3.1 as the current user's screen saver.
param([switch]$NoOpen)
# No administrator rights are required. The .scr association is not touched;
# Windows is pointed directly at the copied Sentinel-v2.3.0.scr file.
$ErrorActionPreference = 'Stop'

$versioned = 'Sentinel-v2.3.1.scr'
$candidates = @(
    (Join-Path $PSScriptRoot $versioned),
    (Join-Path (Split-Path -Parent $PSScriptRoot) "dist\$versioned"),
    (Join-Path $PSScriptRoot 'Sentinel.scr'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\Sentinel.scr'),
    (Join-Path (Get-Location) $versioned),
    (Join-Path (Get-Location) "dist\$versioned")
)
$src = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $src) {
    throw "$versioned was not found. Keep install.ps1 next to $versioned (the release zip does this)."
}

$dir = Join-Path $env:LOCALAPPDATA 'Sentinel'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$dest = Join-Path $dir $versioned
Copy-Item -Path $src -Destination $dest -Force

# Quotes need zlib1.dll next to the screensaver; copy it and a default config if missing.
$extra = @('zlib1.dll', 'config.ini', 'quotes.xlsx')
foreach ($name in $extra) {
    $cand = @(
        (Join-Path $PSScriptRoot $name),
        (Join-Path (Split-Path -Parent $PSScriptRoot) "dist\$name")
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($cand) {
        $target = Join-Path $dir $name
        if ($name -eq 'config.ini' -and (Test-Path $target)) { continue }
        Copy-Item -Path $cand -Destination $target -Force
    }
}

$musicSrc = @(
    (Join-Path $PSScriptRoot 'music'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\music')
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($musicSrc) {
    $musicDir = Join-Path $dir 'music'
    New-Item -ItemType Directory -Force -Path $musicDir | Out-Null
    Copy-Item -Path (Join-Path $musicSrc '*') -Destination $musicDir -Force
}

$desktop = 'HKCU:\Control Panel\Desktop'
if (-not (Test-Path $desktop)) { New-Item -Path $desktop -Force | Out-Null }

# Read the installed config (the screen saver reads this same file at runtime).
# IdleMinutes: 0 = double-click only; 5/10/15/60/120/180 or any custom 1..1440.
$configPath = Join-Path $dir 'config.ini'
if (-not (Test-Path $configPath)) { $configPath = $null }
$idleMinutes = 0
if ($configPath) {
    $line = Get-Content -LiteralPath $configPath -Encoding UTF8 |
        Where-Object { $_ -match '^\s*IdleMinutes\s*=' } |
        Select-Object -First 1
    if ($line -match '^\s*IdleMinutes\s*=\s*(\d+)') {
        $idleMinutes = [int]$matches[1]
    }
}
if ($idleMinutes -lt 0) { $idleMinutes = 0 }
if ($idleMinutes -gt 1440) { $idleMinutes = 1440 }

Set-ItemProperty -Path $desktop -Name 'SCRNSAVE.EXE' -Value $dest
Set-ItemProperty -Path $desktop -Name 'ScreenSaverIsSecure' -Value '0'
if ($idleMinutes -gt 0) {
    Set-ItemProperty -Path $desktop -Name 'ScreenSaveActive' -Value '1'
    Set-ItemProperty -Path $desktop -Name 'ScreenSaveTimeOut' -Value ([string]($idleMinutes * 60))
    Write-Host "Idle activation: $idleMinutes minute(s) ($($idleMinutes * 60) seconds)."
} else {
    # Default: double-click only. Do not let Windows auto-start this or another screen saver.
    Set-ItemProperty -Path $desktop -Name 'ScreenSaveActive' -Value '0'
    Set-ItemProperty -Path $desktop -Name 'ScreenSaveTimeOut' -Value '0'
    Write-Host 'Idle activation: off (double-click only).'
}

Write-Host "Installed: $dest (Sentinel v2.3.1)"
if ($NoOpen) {
    Write-Host 'Screen saver settings were not opened (test mode).'
} else {
    Write-Host 'Opened the Windows screen saver settings. Press Enter or 1984 inside the simulation to exit.'
    Start-Process -FilePath 'control.exe' -ArgumentList 'desk.cpl,,1'
}




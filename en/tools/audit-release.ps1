$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$verification = Join-Path $root 'verification'
$zipPath = Join-Path $dist 'Sentinel-v2.3.0.zip'
$expected = @(
    'Sentinel-v2.3.0.exe', 'Sentinel-v2.3.0.scr', 'config.ini', 'quotes.xlsx',
    'README-v2.3.0.md', 'CHANGES.md', 'install.ps1', 'install.bat',
    'uninstall.ps1', 'uninstall.bat', 'zlib1.dll', 'selftest.txt',
    'benchmark.txt', 'contentcheck.txt', 'musiccheck.txt', 'config-smoke.txt',
    'message-smoke.txt', 'input-smoke.txt',
    'music/A sudden blow.mp3', 'music/The wheels of the world.mp3'
) | Sort-Object
$failures = @()
$lines = @('Sentinel Fusion v2.3.0 release audit')
New-Item -ItemType Directory -Force -Path $verification | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $entries = @($archive.Entries | Where-Object { -not $_.FullName.EndsWith('/') } |
        ForEach-Object { $_.FullName.Replace('\', '/') } | Sort-Object)
    $differences = @(Compare-Object -ReferenceObject $expected -DifferenceObject $entries)
    $entryPass = $entries.Count -eq 20 -and $differences.Count -eq 0
    if (-not $entryPass) { $failures += 'ZIP entry manifest mismatch' }
    $lines += "ZIP files: $($entries.Count) / 20; exact manifest: $entryPass"
    foreach ($entry in $entries) { $lines += "  $entry" }

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($name in @('Sentinel-v2.3.0.exe', 'Sentinel-v2.3.0.scr')) {
            $entry = $archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $name } | Select-Object -First 1
            if (-not $entry) { $failures += "$name missing from ZIP"; continue }
            $stream = $entry.Open()
            try { $zipHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
            finally { $stream.Dispose() }
            $diskPath = Join-Path $dist $name
            $diskHash = (Get-FileHash -Algorithm SHA256 $diskPath).Hash.ToLowerInvariant()
            $match = $zipHash -eq $diskHash
            if (-not $match) { $failures += "$name ZIP/disk hash mismatch" }
            $lines += "${name}: SHA256=$diskHash; ZIP matches disk=$match"
            $version = (Get-Item $diskPath).VersionInfo
            $lines += "  FileVersion=$($version.FileVersion); ProductVersion=$($version.ProductVersion)"
            if ($version.FileVersion -ne '2.3.0.0' -or $version.ProductVersion -ne '2.3.0.0') {
                $failures += "$name version mismatch"
            }
        }
    }
    finally { $sha.Dispose() }
}
finally { $archive.Dispose() }

$config = Get-Content (Join-Path $dist 'config.ini') -Raw
foreach ($setting in @('Sound=on', 'Quotes=on', 'Music=on', 'Motion=full', 'ShowSimulationTag=0')) {
    $present = $config -match "(?m)^$([regex]::Escape($setting))\s*$"
    $lines += "Default ${setting}: $present"
    if (-not $present) { $failures += "Default setting missing: $setting" }
}
$residual = @(Get-Process -Name 'Sentinel-v2.3.0' -ErrorAction SilentlyContinue)
$lines += "Residual Sentinel-v2.3.0 processes: $($residual.Count)"
if ($residual.Count -ne 0) { $failures += 'Residual test process detected' }
$lines += "result: $(if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' })"
foreach ($failure in $failures) { $lines += "  $failure" }
$lines | Set-Content -Path (Join-Path $verification 'package-audit.txt') -Encoding UTF8
$lines | ForEach-Object { Write-Output $_ }
if ($failures.Count -ne 0) { exit 1 }

# Runtime configuration smoke checks. All files stay inside this project.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'dist\Sentinel-v2.3.0.exe'
$distConfig = Join-Path $root 'dist\config.ini'
$baseConfig = Join-Path $root 'config.ini'
$verification = Join-Path $root 'verification'
New-Item -ItemType Directory -Force -Path $verification | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
$original = [System.IO.File]::ReadAllText($distConfig)
$base = [System.IO.File]::ReadAllText($baseConfig)
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('Sentinel runtime configuration checks v2.3.0')

function Set-TestConfig([string]$motion) {
    $text = $base
    $text = [regex]::Replace($text, '(?m)^Sound=.*$', 'Sound=off')
    $text = [regex]::Replace($text, '(?m)^Quotes=.*$', 'Quotes=off')
    $text = [regex]::Replace($text, '(?m)^Music=.*$', 'Music=off')
    $text = [regex]::Replace($text, '(?m)^Motion=.*$', "Motion=$motion")
    [System.IO.File]::WriteAllText($distConfig, $text, $utf8)
}

function Run-Check([string]$name, [string[]]$arguments) {
    $started = Get-Date
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru
    $elapsed = ((Get-Date) - $started).TotalSeconds
    $ok = $process.ExitCode -eq 0
    $lines.Add(('{0}: {1} (exit={2}, {3:N2}s)' -f $name, $(if ($ok) { 'PASS' } else { 'FAIL' }), $process.ExitCode, $elapsed))
    if (-not $ok) { throw "$name failed with exit code $($process.ExitCode)" }
}

try {
    Set-TestConfig 'full'
    Run-Check 'Sound=off + Quotes=off + Music=off windowed runtime' @('--windowed', '--test-exit', '3')

    Set-TestConfig 'reduced'
    Run-Check 'Motion=reduced selftest' @('--selftest')
    Run-Check 'Motion=reduced windowed runtime' @('--windowed', '--test-exit', '3')
    $reducedShot = Join-Path $root 'screenshots\v230-reduced.png'
    Run-Check 'Motion=reduced screenshot' @('--screenshot', $reducedShot, '--theme', 'terminal', '--at', '8', '--seed', '12345')

    $lines.Add('result: PASS')
}
catch {
    $lines.Add("result: FAIL // $($_.Exception.Message)")
    throw
}
finally {
    [System.IO.File]::WriteAllText($distConfig, $original, $utf8)
    $report = Join-Path $verification 'config-smoke.txt'
    [System.IO.File]::WriteAllLines($report, $lines, $utf8)
    Copy-Item -Path $baseConfig -Destination $distConfig -Force
}

Get-Content (Join-Path $verification 'config-smoke.txt')

# Live-window message checks. These do not claim physical keyboard delivery.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WindowMessages {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'dist\Sentinel-v2.3.0.exe'
$distConfig = Join-Path $root 'dist\config.ini'
$baseConfig = Join-Path $root 'config.ini'
$verification = Join-Path $root 'verification'
New-Item -ItemType Directory -Force -Path $verification | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
$original = [System.IO.File]::ReadAllText($distConfig)
$quiet = [System.IO.File]::ReadAllText($baseConfig)
$quiet = [regex]::Replace($quiet, '(?m)^Sound=.*$', 'Sound=off')
$quiet = [regex]::Replace($quiet, '(?m)^Quotes=.*$', 'Quotes=off')
$quiet = [regex]::Replace($quiet, '(?m)^Music=.*$', 'Music=off')
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('Sentinel live-window message checks v2.3.0')
$lines.Add('Scope: real App windows and production message loop; not physical keyboard verification')
$lines.Add('Safety: every run has a 12-second internal test-exit guard; PASS requires exit before 10 seconds')

function Post([IntPtr]$window, [uint32]$message, [IntPtr]$wParam) {
    if (-not [WindowMessages]::PostMessage($window, $message, $wParam, [IntPtr]::Zero)) {
        throw "PostMessage failed for message $message"
    }
}
function Invoke-MessageTest([string]$name, [bool]$windowed, [bool]$hotkey) {
    $arguments = @('--test-exit', '12')
    if ($windowed) { $arguments += '--windowed' }
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $exited = $false
    try {
        Start-Sleep -Milliseconds 1800
        $process.Refresh()
        $window = $process.MainWindowHandle
        if ($window -eq [IntPtr]::Zero) { throw 'MainWindowHandle is unavailable' }
        if ($hotkey) {
            Post $window 0x0312 ([IntPtr]1) # WM_HOTKEY, production emergency-exit identifier
        }
        else {
            Post $window 0x0100 ([IntPtr]0x0D) # WM_KEYDOWN, Enter opens the real modal
            Start-Sleep -Milliseconds 350
            foreach ($ch in @('1', '9', '8', '4')) {
                Post $window 0x0102 ([IntPtr][int][char]$ch) # WM_CHAR enters the override code
            }
            Post $window 0x0100 ([IntPtr]0x0D)
        }
        $exited = $process.WaitForExit(6000)
        if (-not $exited) { $exited = $process.WaitForExit(9000) }
    }
    finally {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit(3000) | Out-Null
        }
        $watch.Stop()
    }
    $elapsed = $watch.Elapsed.TotalSeconds
    $pass = $exited -and $process.ExitCode -eq 0 -and $elapsed -lt 10.0
    $lines.Add(('{0}: {1} (exit={2}, {3:N2}s)' -f $name, $(if ($pass) { 'PASS' } else { 'FAIL' }), $process.ExitCode, $elapsed))
    if (-not $pass) { throw "$name did not exit through the requested message path" }
}

try {
    [System.IO.File]::WriteAllText($distConfig, $quiet, $utf8)
    Invoke-MessageTest 'Windowed modal messages + 1984' $true $false
    Invoke-MessageTest 'Fullscreen modal messages + 1984' $false $false
    Invoke-MessageTest 'Windowed WM_HOTKEY emergency branch' $true $true
    Invoke-MessageTest 'Fullscreen WM_HOTKEY emergency branch' $false $true
    $lines.Add('result: PASS')
}
catch {
    $lines.Add("result: FAIL // $($_.Exception.Message)")
    throw
}
finally {
    [System.IO.File]::WriteAllText($distConfig, $original, $utf8)
    Copy-Item -Path $baseConfig -Destination $distConfig -Force
    [System.IO.File]::WriteAllLines((Join-Path $verification 'message-smoke.txt'), $lines, $utf8)
}

Get-Content (Join-Path $verification 'message-smoke.txt')

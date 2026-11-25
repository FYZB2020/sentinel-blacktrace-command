# Automated keyboard smoke checks for both real exit paths.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk, wScan;
        public uint dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct HARDWAREINPUT {
        public uint uMsg;
        public ushort wParamL, wParamH;
    }
    [StructLayout(LayoutKind.Explicit)]
    public struct INPUTUNION {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public HARDWAREINPUT hi;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public uint type;
        public INPUTUNION U;
    }
    [DllImport("user32.dll", SetLastError=true)]
    static extern uint SendInput(uint nInputs, ref INPUT pInputs, int cbSize);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr hWnd);
    public static uint Key(ushort vk, bool up) {
        INPUT input = new INPUT();
        input.type = 1;
        input.U.ki.wVk = vk;
        input.U.ki.dwFlags = up ? 2u : 0u;
        return SendInput(1, ref input, Marshal.SizeOf(typeof(INPUT)));
    }
    public static uint Tap(ushort vk) {
        uint down = Key(vk, false);
        uint up = Key(vk, true);
        return down + up;
    }
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
$lines.Add('Sentinel automated keyboard smoke checks v2.3.0')
$lines.Add('Input delivery: native Win32 SendInput key events to the live application')
$lines.Add('Safety: every run has a 12-second internal test-exit guard; PASS requires exit before 10 seconds')

function Send-Vk([uint16]$vk) {
    if ([NativeInput]::Tap($vk) -ne 2) { throw "SendInput failed for VK $vk" }
}
function Set-Vk([uint16]$vk, [bool]$up) {
    if ([NativeInput]::Key($vk, $up) -ne 1) { throw "SendInput failed for VK $vk" }
}

function Invoke-InputTest([string]$name, [bool]$windowed, [bool]$emergency) {
    $arguments = @('--test-exit', '12')
    if ($windowed) { $arguments += '--windowed' }
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $exited = $false
    try {
        Start-Sleep -Milliseconds 2200
        $process.Refresh()
        $window = $process.MainWindowHandle
        if ($window -eq [IntPtr]::Zero) { throw 'MainWindowHandle is unavailable' }
        [NativeInput]::ShowWindowAsync($window, 9) | Out-Null
        [NativeInput]::BringWindowToTop($window) | Out-Null
        [NativeInput]::SetForegroundWindow($window) | Out-Null
        Start-Sleep -Milliseconds 350
        if ([NativeInput]::GetForegroundWindow() -ne $window) {
            [NativeInput]::BringWindowToTop($window) | Out-Null
            [NativeInput]::SetForegroundWindow($window) | Out-Null
            Start-Sleep -Milliseconds 350
        }
        if ([NativeInput]::GetForegroundWindow() -ne $window) { throw 'Native input target did not become foreground' }
        if ($emergency) {
            Set-Vk 0x11 $false
            Set-Vk 0x10 $false
            Set-Vk 0x51 $false
            Start-Sleep -Milliseconds 120
            Set-Vk 0x51 $true
            Set-Vk 0x10 $true
            Set-Vk 0x11 $true
        }
        else {
            Send-Vk 0x0D
            Start-Sleep -Milliseconds 400
            foreach ($vk in @(0x31, 0x39, 0x38, 0x34)) {
                Send-Vk $vk
                Start-Sleep -Milliseconds 120
            }
            Send-Vk 0x0D
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
    if (-not $pass) { throw "$name did not exit through the requested input path" }
}

try {
    [System.IO.File]::WriteAllText($distConfig, $quiet, $utf8)
    Invoke-InputTest 'Windowed Enter + 1984 + Enter' $true $false
    Invoke-InputTest 'Fullscreen Enter + 1984 + Enter' $false $false
    Invoke-InputTest 'Windowed Ctrl+Shift+Q' $true $true
    Invoke-InputTest 'Fullscreen Ctrl+Shift+Q' $false $true
    $lines.Add('result: PASS')
}
catch {
    $lines.Add("result: FAIL // $($_.Exception.Message)")
    throw
}
finally {
    [System.IO.File]::WriteAllText($distConfig, $original, $utf8)
    Copy-Item -Path $baseConfig -Destination $distConfig -Force
    [System.IO.File]::WriteAllLines((Join-Path $verification 'input-smoke.txt'), $lines, $utf8)
}

Get-Content (Join-Path $verification 'input-smoke.txt')

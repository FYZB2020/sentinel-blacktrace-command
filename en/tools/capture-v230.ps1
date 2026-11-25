$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force -Path '.\screenshots' | Out-Null
$exe = '.\dist\Sentinel-v2.3.0.exe'
$distConfig = '.\dist\config.ini'
$baseConfig = '.\config.ini'
$original = [System.IO.File]::ReadAllText($distConfig)
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Set-Motion([string]$motion) {
    $text = [System.IO.File]::ReadAllText($baseConfig)
    $text = [regex]::Replace($text, '(?m)^Motion=.*$', "Motion=$motion")
    [System.IO.File]::WriteAllText($distConfig, $text, $utf8)
}
function Shot([string]$name, [string]$theme, [string]$scenario, [string]$at, [string]$goal = '') {
    $cli = @('--screenshot', (Join-Path '.\screenshots' $name), '--theme', $theme,
             '--scenario', $scenario, '--at', $at, '--seed', '12345')
    if ($goal) { $cli += @('--goal', $goal) }
    $process = Start-Process -FilePath $exe -ArgumentList $cli -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "$name failed: $($process.ExitCode)" }
}

try {
    Set-Motion 'full'
    Shot 'v230-terminal.png' 'terminal' 'probe' '8'
    Shot 'v230-split.png' 'split' 'breach' '16'
    Shot 'v230-warroom.png' 'warroom' 'breach' '16'
    Shot 'v230-forensic.png' 'forensic' 'breach' '16'
    Shot 'v230-containment.png' 'containment' 'recovery' '8'
    Shot 'v230-blackbox.png' 'blackbox' 'recovery' '8'
    Shot 'v230-modal.png' 'terminal' 'modal' '8'
    Shot 'v230-damage.png' 'warroom' 'damage' '16'
    Shot 'v230-ghost.png' 'blackbox' 'ghost' '8'

    foreach ($at in @('4', '8', '16', '24')) {
        Shot "v230-boundary-$($at)s.png" 'boundary' 'breach' $at
    }
    Shot 'v230-boundary.png' 'boundary' 'breach' '16'
    Shot 'v230-dock-empty.png' 'terminal' 'dock-empty' '8'
    Shot 'v230-dock-one.png' 'terminal' 'dock-one' '8'
    Shot 'v230-dock-many.png' 'terminal' 'dock-many' '8'

    foreach ($goal in @('exfiltrate', 'ransomware', 'credential', 'miner', 'espionage', 'sabotage')) {
        Shot "v230-last-$goal.png" 'blackbox' 'recovery' '16' $goal
    }

    Set-Motion 'reduced'
    Shot 'v230-reduced.png' 'terminal' 'probe' '8'
    Shot 'v230-boundary-reduced.png' 'boundary' 'breach' '16'
    Shot 'v230-blackbox-reduced.png' 'blackbox' 'recovery' '16' 'ransomware'
}
finally {
    [System.IO.File]::WriteAllText($distConfig, $original, $utf8)
    Copy-Item -Path $baseConfig -Destination $distConfig -Force
}

Get-ChildItem '.\screenshots\v230-*.png' | Sort-Object Name | Select-Object Name, Length

# Sentinel Defense Grid - build script
# Requires Visual Studio 2022 Build Tools with the MSVC x64 toolchain and a Windows 10/11 SDK.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vcvarsCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) {
            $candidate = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { $vcvars = $candidate }
        }
    }
}
if (-not $vcvars) {
    throw 'vcvars64.bat was not found. Install Visual Studio 2022 Build Tools with the C++ workload.'
}

New-Item -ItemType Directory -Force -Path build, dist | Out-Null

$Version = '2.3.1-EN'
$ExeName = "Sentinel-v$Version.exe"
$ScrName = "Sentinel-v$Version.scr"

$compile = 'rc /nologo /fo build\sentinel.res src\sentinel.rc && ' +
    'cl /nologo /std:c++17 /O2 /GL /MT /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 ' +
    '/I src /Fo:build\ src\main.cpp src\app.cpp src\sim.cpp src\render.cpp src\boundary.cpp src\common.cpp src\motion.cpp src\tracefx.cpp src\audio.cpp src\quotes.cpp src\music.cpp ' +
    "build\sentinel.res /Fe:dist\$ExeName " +
    '/link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest ' +
    'user32.lib gdi32.lib msimg32.lib shell32.lib shcore.lib psapi.lib gdiplus.lib ole32.lib winmm.lib mfplay.lib mfplat.lib'

$cmd = 'call "' + $vcvars + '" >nul && ' + $compile
& cmd.exe /d /c $cmd
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

Copy-Item -Path "dist\$ExeName" -Destination "dist\$ScrName" -Force
$zlib = 'C:\Program Files\Git\mingw64\bin\zlib1.dll'
if (Test-Path $zlib) {
    Copy-Item -Path $zlib -Destination .\dist\zlib1.dll -Force
} else {
    Write-Warning 'zlib1.dll was not found; .xlsx quotes will be unavailable.'
}
$musicSrc = Join-Path $root 'music'
if (Test-Path $musicSrc) {
    $musicDst = Join-Path $root 'dist\music'
    New-Item -ItemType Directory -Force -Path $musicDst | Out-Null
    Copy-Item -Path (Join-Path $musicSrc '*') -Destination $musicDst -Force
}
Write-Host "Built dist\$ExeName and dist\$ScrName"

$selftest = Start-Process -FilePath ".\dist\$ExeName" -ArgumentList '--selftest' -Wait -PassThru
Write-Host "selftest exit=$($selftest.ExitCode)"
if (Test-Path .\dist\selftest.txt) { Get-Content .\dist\selftest.txt | ForEach-Object { Write-Host "  $_" } }
if ($selftest.ExitCode -ne 0) { throw "selftest failed with exit code $($selftest.ExitCode)" }

$bench = Start-Process -FilePath ".\dist\$ExeName" -ArgumentList '--benchmark' -Wait -PassThru
Write-Host "benchmark exit=$($bench.ExitCode)"
if (Test-Path .\dist\benchmark.txt) { Get-Content .\dist\benchmark.txt | ForEach-Object { Write-Host "  $_" } }
if ($bench.ExitCode -ne 0) { throw "benchmark failed with exit code $($bench.ExitCode)" }

$content = Start-Process -FilePath ".\dist\$ExeName" -ArgumentList '--contentcheck' -Wait -PassThru
Write-Host "contentcheck exit=$($content.ExitCode)"
if (Test-Path .\dist\contentcheck.txt) { Get-Content .\dist\contentcheck.txt | Select-Object -First 8 | ForEach-Object { Write-Host "  $_" } }
if ($content.ExitCode -ne 0) { throw "contentcheck failed with exit code $($content.ExitCode)" }

$music = Start-Process -FilePath ".\dist\$ExeName" -ArgumentList '--musiccheck' -Wait -PassThru
Write-Host "musiccheck exit=$($music.ExitCode)"
if (Test-Path .\dist\musiccheck.txt) { Get-Content .\dist\musiccheck.txt | Select-Object -First 8 | ForEach-Object { Write-Host "  $_" } }
if ($music.ExitCode -ne 0) { throw "musiccheck failed with exit code $($music.ExitCode)" }

Copy-Item -Path .\config.ini, .\quotes-en.txt -Destination .\dist -Force
Copy-Item -Path .\README-EN.md -Destination ".\dist\README-v$Version.md" -Force
Copy-Item -Path .\CHANGES-EN.md -Destination .\dist\CHANGES.md -Force
Copy-Item -Path .\tools\install.ps1, .\tools\install.bat, .\tools\uninstall.ps1, .\tools\uninstall.bat -Destination .\dist -Force
foreach ($report in @('config-smoke.txt', 'message-smoke.txt', 'input-smoke.txt')) {
    $source = Join-Path .\verification $report
    if (Test-Path $source) { Copy-Item -Path $source -Destination (Join-Path .\dist $report) -Force }
}

$zip = ".\dist\Sentinel-v$Version.zip"
if (Test-Path $zip) { Remove-Item -Path $zip -Force }
$zipItems = @(
    ".\dist\$ExeName", ".\dist\$ScrName", ".\dist\config.ini", ".\dist\quotes-en.txt",
    ".\dist\README-v$Version.md", ".\dist\CHANGES.md", ".\dist\install.ps1", ".\dist\install.bat",
    ".\dist\uninstall.ps1", ".\dist\uninstall.bat"
)
foreach ($optional in @('.\dist\zlib1.dll', '.\dist\selftest.txt', '.\dist\benchmark.txt', '.\dist\contentcheck.txt', '.\dist\musiccheck.txt', '.\dist\config-smoke.txt', '.\dist\message-smoke.txt', '.\dist\input-smoke.txt', '.\dist\music')) {
    if (Test-Path $optional) { $zipItems += $optional }
}
Compress-Archive -Path $zipItems -DestinationPath $zip

Write-Host ''
Write-Host 'Package contents:'
Get-ChildItem .\dist | Where-Object { $_.Name -match 'Sentinel|README|config|install|uninstall|selftest|benchmark' } |
    Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host



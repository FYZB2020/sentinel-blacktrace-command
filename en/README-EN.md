# Sentinel Blacktrace Command v2.3.1 — English Edition

A native Windows screensaver / prank simulation that behaves like a live
containment console under attack. It is pure GDI/win32 C++17, with no runtime,
no browser engine, no network access and no administrator requirement.

> This is a simulation. It does not encrypt, delete, lock or modify the system.
> The `1984` code only dismisses the simulation.

---

## Quick start

- **Double-click** `Sentinel-v2.3.1-EN.exe` to run full-screen.
- Press **Enter** at any time to open the containment override window.
- Type the override code (default `1984`) and press **Enter** to exit.
- Press **Ctrl+Shift+Q** for an immediate emergency exit.

The `Sentinel-v2.3.1-EN.scr` copy is for Windows screen-saver registration. On
some machines the `.scr` file type is associated with AutoCAD or another
application; use the `.exe` for direct launch, or run `install.bat`.

---

## Controls

| Key | Action |
| --- | --- |
| Enter | Open override window / submit the focused fake console input |
| Ctrl+Enter | Open the override window while a fake console input is focused |
| Esc | Close the override window (when not in lockout) |
| Backspace | Edit the focused fake console input |
| F5 | Reload `config.ini` and restart the current scene |
| F6 | Next layout (preview only; does not write the INI) |
| F7 | Next incident goal (preview only) |
| F8 / M | Next music track |
| Bottom-right corner | Hidden music-track switch |
| Ctrl+Shift+Q | Emergency exit |

The fake console boxes are interactive theater. Typing is accepted, Enter
echoes the line and a fake response, and the text is quickly washed away by the
live feed. If a box closes, its unfinished input is discarded. No command is
executed.

---

## Install as a Windows screen saver (optional)

1. Right-click `install.bat` and choose **Run** (or run it from a terminal).
2. Windows opens the screen-saver settings; select **Sentinel-v2.3.1-EN**.
3. Set the wait time there if you want Windows to start it automatically.

The installer sets `HKCU\Control Panel\Desktop` and copies the executable,
`config.ini`, `quotes-en.txt`, `zlib1.dll` and the `music` folder into
`%LOCALAPPDATA%\Sentinel-EN`. It does **not** change the `.scr` file association.

### IdleMinutes

`IdleMinutes` in the installed `config.ini` controls the Windows idle timeout:

```ini
[general]
; 0 = double-click only
; 5/10/15/60/120/180 or any custom 1..1440
IdleMinutes=0
```

After changing it, run `install.bat` again so the new value is written to the
registry. `0` disables Windows auto-start and leaves the program as a
double-click-only tool.

---

## Configuration

`config.ini` lives next to the executable. If it is absent, the program uses
`%APPDATA%\Sentinel\config.ini`. Press `F5` after editing, or restart.

```ini
[general]
Code=1984                 ; 4-32 printable ASCII, case-sensitive
Theme=auto                ; auto | terminal | split | boundary | warroom | forensic | containment | blackbox
Fps=30                    ; 10-60
Quality=auto
MonitorMode=all
AutoPromptSeconds=0
IdleMinutes=0             ; installer-only: idle minutes, 0 = double-click only
ShowSimulationTag=0
KeepDisplayOn=0
Language=en
Speed=1.6                 ; 0.5-3.0

[effects]
Sound=on
Flash=med                 ; low | med | high
Shake=0
Glitch=1
ErrorText=1
Dust=med
Corruption=med
Motion=full

[quotes]
Quotes=on
QuotesFile=quotes-en.txt

[music]
Music=on
MusicDir=music
```

The override code is configured by `Code`. Invalid values fall back to `1984`.
The code is never printed in the UI.

### Ghost quotes

`QuotesFile` can point to a UTF-8 `.txt` file or an `.xlsx` workbook. The
English edition ships `quotes-en.txt`. Set `Quotes=off` to disable the ghost
lines entirely. Missing or invalid files are ignored safely.

### Music

Place `.mp3`, `.wav` or `.m4a` files in the `music` folder. `Music=on` enables
the layer; `M` / `F8` or the hidden bottom-right corner switches tracks. The
program rotates automatically when a track ends. The English edition ships
eight tracks:

- A sudden blow
- A sudden blow (v2)
- The wheels of the world
- The wheels of the world 2.59
- The ghost in the machine v2
- A wilderness of mirrors
- Slouching towards Bethlehem
- Slouching towards Bethlehem V2

---

## Layouts and direction

Seven layouts share the same simulation state: `TERMINAL`, `SPLIT OPS`,
`BOUNDARY INTERCEPT`, `WAR ROOM`, `FORENSIC`, `CONTAINMENT`, `BLACK BOX`.
`Theme=auto` changes layout at act boundaries; a fixed theme always keeps its
layout. The story is technical, not scripted dialogue: attack goals, staged
containment, defensive rollback, evidence handling and recovery.

The persistent artist signature `原罪知鸮` appears in the lower-right corner of
every layout and every overlay. It is part of the piece and must remain visible.

---

## Build

Requirements: Windows 10/11 x64, Visual Studio 2022 Build Tools with the C++
workload and a Windows 10/11 SDK.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1
```

The script runs `rc`, `cl`, selftest, benchmark, contentcheck and musiccheck,
then writes the executable, `.scr`, config, README, installer, music, zlib and
reports into `dist`.

`zlib1.dll` is required for `.xlsx` quote loading. The text quote corpus does
not need it; keep the DLL beside the executable if you use xlsx.

---

## Verification

- Selftest: 54,000 simulated updates, 14 phases + wrap, integrity bounds,
  boundary queues, override lockout, 1984 termination, seven layouts.
- Benchmark: 600 offscreen frames at 2560x1440.
- Contentcheck: 360 simulated seconds, duplicate-rate report.
- Musiccheck: discovers tracks and exercises the next-track path.

The current build is measured in `dist\benchmark.txt`, `selftest.txt`,
`contentcheck.txt` and `musiccheck.txt`.

---

## Troubleshooting

**Double-click does nothing / black screen**
Wait one second for the boot fade; if needed press `Ctrl+Shift+Q`, then run the
`.exe --windowed` to inspect the windowed mode.

**Cannot exit**
Press `Ctrl+Shift+Q`. The override code is `1984` by default. The program is a
user-mode window and Task Manager remains available.

**A `.scr` opens Notepad / AutoCAD**
That is a file-association issue on the machine, not the program. Launch the
`.exe` directly or use `install.bat`.

**No music**
Check that `Music=on` and that `music` contains supported files. The footer
shows the active track. Set `Music=off` to mute.

**No ghost quotes**
Check `Quotes=on`, `QuotesFile`, and that the file is valid UTF-8 text or a
readable `.xlsx`. `Quotes=off` disables them.

**Flashing / photosensitivity**
Use `Flash=low`, `ErrorText=0`, `Motion=reduced`, or avoid the program if
flashing images affect you.

---

## Safety and license

This is fictional UI. It makes no network connections, does not touch user
files, does not change the `.scr` association, and does not require
administrator rights. Do not install it on someone else's machine without
consent. Free for personal use.

Art signature: **原罪知鸮**.

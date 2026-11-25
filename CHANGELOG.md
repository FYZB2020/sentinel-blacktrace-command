# Changelog

## v2.3.1 — Kimi 3.1 Pro Edition

- Chinese and English editions packaged separately.
- Persistent footer signature `// 原罪知鸮` in both editions (small, normal-weight,
  same baseline as the footer; it survives every layout and the containment modal).
- Eight music tracks included in the release zips.
- English edition: all UI, logs, errors, quotes, README, and installer text translated;
  the only Chinese text is the artist signature.
- `IdleMinutes` added to `config.ini`; `install.ps1` writes it to the native Windows
  screen-saver timeout. Default `0` means double-click only.
- Chinese and English install to separate folders:
  `%LOCALAPPDATA%\Sentinel` and `%LOCALAPPDATA%\Sentinel-EN`.
- Seven layouts, three acts, fourteen technical stages, 5+ minute rotation.
- Ghost quotes, audio cues, MFPlay music, input-capable fake consoles, boundary
  interception, forensic evidence views, adaptive layouts.
- Screen shake removed; flash/error-text/takeover effects instead.
- Selftest, benchmark, contentcheck, and musiccheck all pass.

## v2.3.0 — baseline

- Earlier framework and visual system. Preserved in the release assets; this
  repository starts from it and adds the v2.3.1 work above.

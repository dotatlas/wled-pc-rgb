# Roadmap

Where **wled-pc-rgb** is and where it might go. For the released version history, see the
[CHANGELOG](../CHANGELOG.md).

## Done — the mirror is feature-complete

- ✅ Mirror WLED's live output onto PC RGB in real time (via OpenRGB).
- ✅ Follows WLED effects, solid colours, and **LedFx** audio-reactive takeovers.
- ✅ Multiple live sources, auto-selected: WLED live-view, **DDP** (UDP 4048), and
  **E1.31/sACN** (UDP 5568) straight to the PC.
- ✅ Per-device mirror toggle.
- ✅ Tone controls: PC brightness, flash gain, minimum-brightness floor, idle colour.
- ✅ Positional mapping: *Spread* (whole strip per device) and *Wrap* (strip across all
  devices).
- ✅ Configurable ARGB zone size.
- ✅ Auto-launch of OpenRGB + the WLED helper, system tray, start-at-login /
  start-minimised, and persisted settings.
- ✅ Portable, self-contained Windows release.

## Ideas / help wanted

These are optional extras — the mirror works fully without them. Contributions welcome.

- **In-house audio reactivity** — a WASAPI + FFT source so the PC can react to system audio
  directly, without needing LedFx running. (Would be a distinct mode from the WLED mirror.)
- **NZXT Kraken Elite LCD** — drive the 640×640 screen (static image, then animation). This
  needs a manual [Zadig](https://zadig.akeo.ie) WinUSB driver bind, which can interfere with
  NZXT CAM — so it would ship as an isolated, opt-in module.
- **Free-form mapping editor** — assign specific strip regions to specific device zones
  (Spread/Wrap cover the common cases today).
- **A proper installer** — an [Inno Setup](https://jrsoftware.org/isinfo.php) script exists
  in `packaging/`; a built, optionally-signed installer could be published alongside the zip.
- **More platforms** — the stack (Qt + OpenRGB + a JVM) is cross-platform; only a
  Windows build is produced and tested today.

## Not planned

- **RAM / DDR5 (SMBus) RGB.** Writing to DIMM SMBus carries a real SPD-corruption / brick
  risk on some kits, so the app deliberately never touches it and keeps OpenRGB
  non-elevated. Everything else works without it.

# Changelog

All notable changes to **wled-pc-rgb**. Newest first. Versions are git tags (`vX.Y`).

## v1.1

- **Real-time mirror.** The PC now follows WLED at its true frame rate instead of a fixed
  ~10 FPS cap — the WLED helper pushes frames the instant they're rendered, and the
  internal links run with Nagle disabled for low latency.
- **Off means off.** A black/off WLED frame now turns the PC devices off (they used to
  glow when a minimum-brightness floor was set). The floor now only lifts *dim* content.
- **Idle colour.** New "When off, show colour" option + colour picker — off LEDs stay off,
  or show a static colour you choose.
- **Configurable ARGB zone size** (default 8, e.g. an MSI JARGB header) instead of a fixed 24.

## v1.0

- **First public release.** Portable, self-contained Windows build; MIT licensed.
- Packaging fix (v1.0.1): bundle the Qt platform plugins so the zip runs on a clean PC.

## v0.18 – v0.22

- **UX overhaul** — setup-readiness dots (OpenRGB / backend / WLED), a primary **Mirror**
  button, live colour swatches, configurable WLED host, system tray with close-to-tray,
  start-at-login / start-minimised / auto-mirror options, and full settings persistence.
- **Flash gain** (1–5× multiplier) and a **minimum-brightness floor**.
- **Positional mapping** — *Wrap* mode (the strip flows once across all devices) alongside
  *Spread* (the whole strip on each device); smoother gradients.
- **More live sources** — a DDP listener (UDP 4048) and an E1.31/sACN listener (UDP 5568)
  so LedFx can stream straight to the PC; the app auto-selects the fastest source.
- Robustness — self-healing connections, auto re-scan for late-arriving USB devices, and
  automatic launching of OpenRGB and the WLED helper.

## v0.7 – v0.17

- **The mirror.** WLED's live output is read back and applied to PC devices in real time,
  including LedFx overrides — average colour and positional *Spread* mapping.
- Per-device mirror toggle; independent PC-only brightness (WLED's own brightness is never
  changed).
- NZXT Kraken ring driven via its Static mode so it follows along.
- Kept OpenRGB non-elevated for safety — the app never touches the motherboard SMBus/RAM.

## v0.1 – v0.6

- Project scaffold and the local RGB core: connect to OpenRGB, enumerate devices, and
  control colours and modes from a Qt app.

---

*RAM/DDR5 RGB was intentionally dropped from scope — writing to DIMM SMBus carries a real
brick risk, and everything else works without it.*

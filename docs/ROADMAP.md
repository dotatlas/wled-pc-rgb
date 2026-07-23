# wled-pc-rgb — Roadmap

Order follows the user's priority: **local RGB control first, WLED mirror second,
stretch goals last.** See [DESIGN.md](DESIGN.md) for architecture.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done

## Versioning & commit strategy

Every commit is a **working, standalone build** — a new feature only lands once the app
still launches and does everything the prior version did, plus the new thing. Each
increment is a tagged version (`vX.Y`). No half-finished features on `main`.

| Ver | Phase | Standalone capability delivered | State |
|----|----|----|----|
| **v0.1** | 0 | Scaffold + 2 spikes: C++ probe lists OpenRGB devices; Java mirrors WLED live output | ✅ done |
| v0.2 | 1 | Qt app shell — launches, single-instance, lives in the system tray (self-contained build) | ✅ done |
| v0.3 | 1 | Live device inspector — connect to OpenRGB, show devices/zones/LEDs in a tree (read-only) | ✅ done |
| v0.4 | 1 | Color control — set a device color from a picker → writes via OpenRGB | ✅ done |
| v0.5 | 1 | Modes + brightness + motherboard/SMBIOS panel = **full local RGB control (Goal #1)** | ✅ done |
| v0.6 | 1+ | Mode switching + set-all-devices colour (RAM/SMBus path scrapped) | ✅ done |
| v0.7 | 3 | Java backend + C++↔Java loopback IPC; room's live colour shown in-app | ✅ done |
| v0.8 | 3 | Control WLED from the app (Set room… → backend → /json/state) | ✅ done |
| v0.9 | 3 | **The mirror** — live-view → IPC → OrgbMirror → PC mirrors WLED (incl. LedFx), average colour | ✅ done |
| v0.10 | 3 | Positional (spread) mapping + policy toggle = **Goal #2 complete** | ✅ done |
| v0.11 | 1+ | Auto-size motherboard ARGB zones to 24 (fixes OpenRGB-wiped JARGB sizes; fans light) | ✅ done |
| v0.12 | 3+ | Self-healing mirror (reconnect + watchdog) + auto-launch backend + generic "WLED" naming + PC-only brightness | ✅ done |
| v0.13 | 3+ | Mirror-focused UI: per-device mirror toggle, condensed controls, DRAM-safe, no forced mode | ✅ done |
| v0.14 | 3+ | Double-click a mode to select it; mode change no longer collapses the device | ✅ done |
| v0.15 | 3+ | Auto-launch OpenRGB elevated (GPU works) + Kraken mirrors via its mode colour | ✅ done |
| v0.16 | 3+ | Retry while OpenRGB reports 0 devices (still detecting) + restore diagnostic dump | ✅ done |
| v0.17 | 3+ | Safety: OpenRGB stays NON-elevated (never touch SMBus/DDR5); Kraken auto-switches to Static so its ring mirrors | ✅ done |
| v0.18 | 4 + 1.0 | UX overhaul: setup-readiness strip, primary Mirror button, live swatches, per-device ticks + all settings persisted, configurable WLED host, tray + close-to-tray, autostart/start-min, backend DDP tap (UDP 4048) + auto-select, self-healing mirror socket, pause-on-WLED-off, late-HID rescan | ✅ done |
| v0.19 | 1.0 | Portable self-contained release (package-win.ps1 → versioned zip) + Inno Setup installer script + full usage/troubleshooting guide (docs/USAGE.md) | ✅ done |
| v0.20 | 4+ | Mirror tone controls: Flash gain (1–5× multiplier so dim WLED flashes read brighter) + Min brightness floor (hue-preserving, never-off baseline). All three sliders persist | ✅ done |
| v0.21 | 4 | E1.31/sACN tap (backend UDP 5568, unicast + multicast universe 1) — completes Phase 4 protocol coverage; auto-selects alongside DDP/live-view | ✅ done |
| v0.22 | 4 | Positional mapping: Wrap mode (strip distributed once across all devices in sequence) beside Spread (whole strip per device); buckets 16→64 for smoother gradients | ✅ done |
| v0.23 | 5 | Bespoke NZXT Kraken ring driver (hidapi, HUE2 Direct) — deferred (ring already mirrors via OpenRGB Static) | |
| v0.24 | 5 | Kraken Elite LCD — static sensor screen → looping GIF — deferred (needs manual Zadig/WinUSB driver swap) | |
| **v1.0** | — | Release: code-sign the exe/installer + publish (needs a signing cert + Inno Setup installed) | |

---

## Phase 0 — Scaffold, toolchain & seam-proving spikes
Goal: prove the two integration seams before investing in the full app.

- [x] Repo skeleton, `.gitignore`, README, DESIGN, ROADMAP
- [x] C++ OpenRGB probe spike (`core-cpp/`) — connect to SDK, list controllers
- [x] Java WLED live-view mirror spike (`wled-backend-java/spike/`) — read live output
- [x] Install toolchain via Scoop (JDK 21 Temurin, gcc 15.2, cmake 4.4, ninja 1.13; Qt6 deferred to Phase 1)
- [x] Install OpenRGB **1.0rc3** (`C:\.software\OpenRGB`)
- [x] Run both spikes: Java mirror **✓** (Seam 2 — 141 LEDs) AND C++ probe **✓** (Seam 1)
- [x] Run OpenRGB headless + probe against it
- [x] **EXIT MET (2026-07-22):** probe enumerated GPU (5070 Ti) + MSI Mystic Light + Kraken ring + mouse; Java mirrored the ESP32 live output. DDR5 RAM correctly absent (non-elevated = no SMBus).

## Phase 1 — Local RGB core (Goal #1)
Goal: detect the board, enumerate + control PC RGB locally, in a Qt inspector.

- [~] C++ device model — OrgbDevice/Zone/LED structs + colour read/write ✓ (v0.4); modes next (v0.5)
- [x] OpenRGB SDK client — read + write ✓ (v0.3/v0.4, `orgb_client` over QTcpSocket)
- [x] Motherboard detection via SMBIOS ✓ (v0.5): reads "MSI PRO X870E-P WIFI (MS-7E70)"
- [~] MSI Mystic Light ARGB controller **detected by OpenRGB ✓ (R2 resolved 2026-07-22)** — remaining: locate the RS120 fans as zones on it + drive them
- [ ] GPU RGB control (RTX 5070 Ti via OpenRGB RC)
- [x] Qt shell+tray ✓ (v0.2); inspector tree ✓ (v0.3); colour control ✓ (v0.4); modes shown + brightness + mobo panel ✓ (v0.5)
- [x] Single-instance lock ✓ (v0.2, QLocalServer); autostart (HKCU Run) + start-minimised ✓ (v0.18)
- [x] **Exit (Goal #1) MET (2026-07-22):** board detected; devices enumerated with zones/modes/LEDs; colour + brightness control verified on Kraken + mouse. Follow-ups: RS120 fan headers need an LED-count resize; GPU write needs elevation.

## Phase 2 — SCRAPPED (DDR5 RAM / SMBus)
**Dropped 2026-07-23 by user decision** to eliminate any DIMM-brick risk. The app will
never touch the SMBus/RAM. RGB stays OpenRGB-SDK-driven (USB-HID / GPU-I2C / mobo-ARGB)
+ WLED. Items below are kept only as a record of what was cut.

- [ ] PawnIO signed-driver integration + elevation story (elevated helper)
- [ ] Vengeance DDR5 control behind off-by-default toggle + corruption warning
- [ ] Single global SMBus lock + named mutex; SPD backup/recovery tooling
- [ ] "Safe mode" that disables all SMBus access
- [ ] **Exit:** user can opt into RAM RGB safely; safe mode disables it

## Phase 3 — WLED mirror (Goal #2, the inverted flow)
Goal: PC devices mirror WLED's live output, including LedFx overrides.

- [x] Java WLED backend ✓ (v0.7/v0.8, WledBackend.java): /json/info + live-view + JSON control (Set room…)
- [x] Live-view tap ✓ (v0.7): backend subscribes {"lv":true}, tracks the strip's average colour
- [x] IPC ✓ (v0.7): backend→app frame JSON over loopback; app→backend wled-command channel ready
- [x] C++ mapper — average ✓ (v0.9) + positional/spread ✓ (v0.10, applyBuckets over 16 buckets)
- [x] Mirror on/off toggle + Spread policy ✓ (v0.9/v0.10)
- [x] **Exit MET (2026-07-23):** room solid green → mouse #10ff40; PC follows WLED's live output (incl. LedFx)

## Phase 4 — Fidelity & latency upgrades
Goal: tighter audio-reactivity and richer mapping. Optional.

- [x] DDP sniffer tap for the LedFx case ✓ (v0.18): backend listens on UDP 4048, decodes DDP → avg + 16 buckets
- [x] E1.31/sACN tap ✓ (v0.21): backend listens on UDP 5568 (unicast + multicast universe 1), decodes E1.31 data packets → avg + 16 buckets
- [x] Auto-select tap ✓ (v0.18/v0.21): live-view is the default; DDP or sACN wins for 1500ms whenever their frames arrive
- [x] Positional mapping ✓ (v0.10 Spread, v0.22 Wrap): Spread = whole strip per device; Wrap = strip
      distributed once across all devices in sequence (64 buckets). A free-form per-zone region editor
      remains possible but Wrap/Spread cover the practical cases without a fragile per-device UI.
- [ ] (Optional) in-house WASAPI+FFT fallback if running without LedFx
- [~] **Exit:** low-latency audio-reactive PC mirror when LedFx streams DDP or E1.31/sACN to this PC ✓; richer mapping pending

## Phase 5 — Stretch: NZXT Kraken Elite V2 (deferred)
Goal: the genuinely bare-metal, reverse-engineered pieces.

- [~] Kraken RGB ring — **already mirrors via OpenRGB** (auto-Static, v0.17). A bespoke
      hidapi HUE2 "Direct" driver (`1e71:3012`) is **deferred**: it would duplicate the
      working path and risk fighting OpenRGB for the same HID device.
- [ ] Kraken LCD — port liquidctl `kraken3` bucket/bulk sequence to C++ (RGB565, alpha
      0x00, WinUSB binding). **Deferred**: requires a manual Zadig/WinUSB driver rebind,
      which can't be done blind and could disrupt NZXT CAM's access to the device.
- [ ] **Exit:** custom image / monitoring on the LCD, ring driven by the app

## Packaging & release (v0.19 → v1.0)
- [x] Portable self-contained build: `core-cpp/scripts/deploy-win.sh` bundles the full
      DLL closure; `scripts/package-win.ps1` stages it + docs → `dist/*-win64.zip`
- [x] Installer script: `packaging/wled-pc-rgb.iss` (Inno Setup 6) — prompts for install
      dir, optional desktop shortcut + launch-at-login
- [x] Usage/troubleshooting docs: `docs/USAGE.md`; README status refreshed
- [ ] **v1.0:** compile the installer (needs Inno Setup) + code-sign exe/installer
      (needs a certificate) + publish — the only remaining steps, both external/manual

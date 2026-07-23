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
| v0.9 | 3 | **The mirror** — live-view tap → IPC → mapper → PC mirrors WLED (incl. LedFx), average policy | |
| v0.10 | 3 | Positional mapping + mapping config UI = **Goal #2 complete** | |
| v0.11 | 4 | DDP/E1.31 sniff tap + auto-select + mapping editor (fidelity/latency) | |
| v0.12 | 5 | Bespoke NZXT Kraken ring driver (hidapi, HUE2 Direct) | |
| v0.13 | 5 | Kraken Elite LCD — static sensor screen → looping GIF | |
| **v1.0** | — | Polish: autostart, packaging/installer, signing, docs — release | |

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
- [~] Single-instance lock ✓ (v0.2, QLocalServer); autostart (HKCU Run) pending → v1.0
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
- [ ] C++ mapper: WLED frame → PC device zones (average + positional policies)
- [ ] Tray: pick WLED node, mapping policy, on/off mirror
- [ ] **Exit:** whatever WLED shows (incl. LedFx audio-reactive) mirrors onto PC RGB

## Phase 4 — Fidelity & latency upgrades
Goal: tighter audio-reactivity and richer mapping. Optional.

- [ ] DDP/E1.31 sniffer tap for the LedFx case (full res/rate/RGBW, low latency)
- [ ] Auto-select tap (live-view universal default; DDP-sniff when LedFx detected)
- [ ] Mapping editor UI (assign strip regions to device zones)
- [ ] (Optional) in-house WASAPI+FFT fallback if running without LedFx
- [ ] **Exit:** low-latency audio-reactive PC mirror when LedFx is active

## Phase 5 — Stretch: NZXT Kraken Elite V2
Goal: the genuinely bare-metal, reverse-engineered pieces.

- [ ] Kraken RGB ring — bespoke hidapi HUE2 "Direct" driver (`1e71:3012`)
- [ ] Kraken LCD — port liquidctl `kraken3` bucket/bulk sequence to C++
      (RGB565, alpha 0x00, WinUSB binding); static sensor screens first, then GIFs
- [ ] **Exit:** custom image / monitoring on the LCD, ring driven by the app

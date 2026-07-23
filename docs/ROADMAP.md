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
| v0.2 | 1 | Qt app shell — launches, single-instance, lives in the system tray | next |
| v0.3 | 1 | Live device inspector — connect to OpenRGB, show devices/zones/LEDs in a tree (read-only) | |
| v0.4 | 1 | Color control — set a device/zone color from a picker → writes via OpenRGB | |
| v0.5 | 1 | Modes + brightness + motherboard/SMBIOS panel = **full local RGB control (Goal #1)** | |
| v0.6 | 2 | Gated DDR5 RAM RGB via elevated helper + PawnIO, off-by-default opt-in | |
| v0.7 | 3 | Java backend process + C++↔Java loopback IPC handshake | |
| v0.8 | 3 | Control WLED from the app (JSON/WebSocket) — drive the room strip | |
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

- [ ] C++ device model (`Controller/Zone/LED/Mode`, mirrors OpenRGB's)
- [ ] Robust OpenRGB SDK client wrapper (replaces the probe)
- [ ] Motherboard detection (passive SMBIOS/DMI read — no bus writes)
- [~] MSI Mystic Light ARGB controller **detected by OpenRGB ✓ (R2 resolved 2026-07-22)** — remaining: locate the RS120 fans as zones on it + drive them
- [ ] GPU RGB control (RTX 5070 Ti via OpenRGB RC)
- [ ] Qt device-inspector GUI (`QTreeView` + color control) + `QSystemTrayIcon`
- [ ] Single-instance lock + autostart (HKCU Run)
- [ ] **Exit:** detect board, enumerate + control fans & GPU RGB, no vendor SW running

## Phase 2 — Gated SMBus / DDR5 RAM (opt-in, high-risk)
Goal: allow Vengeance DDR5 RGB with informed consent. Legitimately skippable.

- [ ] PawnIO signed-driver integration + elevation story (elevated helper)
- [ ] Vengeance DDR5 control behind off-by-default toggle + corruption warning
- [ ] Single global SMBus lock + named mutex; SPD backup/recovery tooling
- [ ] "Safe mode" that disables all SMBus access
- [ ] **Exit:** user can opt into RAM RGB safely; safe mode disables it

## Phase 3 — WLED mirror (Goal #2, the inverted flow)
Goal: PC devices mirror WLED's live output, including LedFx overrides.

- [ ] Java WLED backend: mDNS discovery, JSON/WebSocket control
- [ ] Live-view tap (`{"lv":true}`) with reconnect + single-consumer handling
- [ ] IPC: stream `LiveFrame` Java → C++; `WledCommand` C++ → Java
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

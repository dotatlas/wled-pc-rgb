# wled-pc-rgb — Roadmap

Order follows the user's priority: **local RGB control first, WLED mirror second,
stretch goals last.** See [DESIGN.md](DESIGN.md) for architecture.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done

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

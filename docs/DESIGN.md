# wled-pc-rgb — Design

> Status: Phase 0 (scaffold). This document is the source of truth for architecture
> and decisions. It reflects research + adversarial verification performed 2026-07-22.

## 1. What it is

A native, cross-platform tool that makes this PC's RGB hardware a **mirror of a
WLED-driven room**. Unlike OpenRGB (which is the local-device engine we build on) or
LedFx (a source), the defining idea here is the **data-flow direction**:

> **WLED is the master. The PC follows.**
> The ESP32 running WLED holds the authoritative "picture." The program reads WLED's
> *live rendered output* back and applies it to the PC's RGB devices. So whatever WLED
> shows — a WLED effect, a color set in the app, or an external takeover such as
> **LedFx making it audio-reactive** — the PC reproduces automatically. The program
> never renders RGB independently of what WLED is displaying.

The user's phrasing: `program -> wled esp32 -> back to program, rgb directly controlled
by wled`. The program *controls* WLED (set effects/colors/presets) and *reads WLED's
output back* to drive the PC. When something else overrides WLED, the PC is overridden
with it.

## 2. Corrected assumptions (from verification)

1. **You cannot host a WLED instance on the PC.** WLED is ESP32 firmware needing the
   MCU's timing peripherals. The ESP32 stays the single WLED node; the PC is a client.
2. **The mirror mechanism is WLED's WebSocket live-view.** `ws://<ip>/ws` + `{"lv":true}`
   → binary frames of the real pixel buffer (`strip.getPixelColor()`), which **reflects
   external realtime overrides (LedFx)** by default. Caveats in §5.
3. **The RS120 fans need no Corsair hub** — standard 5V ARGB on the MSI board's header.
4. **The RTX 5070 Ti is controllable** via OpenRGB (I2C `0x68`) — but only in the
   **1.0 release-candidate**, not 0.9 stable.
5. **DDR5 RAM RGB is deferred/gated** — SMBus writes to Vengeance DDR5 carry real
   SPD-corruption/brick risk.

## 3. Architecture

Two cooperating user-session processes on loopback IPC, plus a bundled headless OpenRGB
for device I/O. Two processes (not JNI-fused) for **fault isolation** — crash-prone
USB/SMBus drivers must not take down the JVM.

```
                         ┌─────────────────────────────────────────────┐
                         │  C++ app (Qt)  — the showcase + device core   │
   set effects/colors    │   • device-inspector GUI + system tray        │
   ┌────────────────────►│   • OpenRGB SDK client (TCP 6742)             │
   │                     │   • MAPPER: WLED live frame → PC device zones  │
   │                     │   • (Phase 5) bespoke Kraken ring/LCD driver   │
   │                     └───────────────▲───────────────┬───────────────┘
   │                       WLED live frames│              │ per-device colors
   │                        (over IPC)     │              ▼   (UpdateLEDs)
   │              ┌────────────────────────┴──┐   ┌───────────────────────┐
   │  loopback    │  Java backend (headless)   │   │  headless OpenRGB     │
   │  IPC ────────┤   • WLED control: JSON/WS  │   │  (--server, ELEVATED) │
   │              │   • LIVE-VIEW TAP: ws {lv}  │   │   • MSI ARGB → RS120  │
   │              │   • (Phase 4) DDP sniffer  │   │   • RTX 5070 Ti I2C   │
   │              └───────────┬────────────────┘   │   • [gated] DDR5 RAM  │
   │      JSON/WS control     │  ▲ live-view (ws)   └───────────────────────┘
   │      ┌──────────────────►│  │ binary frames
   │      │                   ▼  │
   │   ┌──┴─────────────────────────────┐         LedFx (optional, external)
   └───┤  ESP32 — stock WLED (room LEDs) │◄──────── DDP/UDP realtime (audio-reactive)
       └─────────────────────────────────┘
```

**Primary flow (mirror):** LedFx or a WLED effect drives the ESP32 → Java's live-view
tap reads the rendered frames → forwards over IPC → C++ mapper distributes them across
PC device zones → OpenRGB writes them to fans/GPU/(gated)RAM. The PC mirrors the room.

**Control flow:** the C++ GUI / tray sends commands (effect, color, preset, brightness,
power) → Java → WLED JSON/WebSocket. This is how you *drive* WLED from the app.

### 3.1 Component responsibilities

| Component | Owns |
|---|---|
| **C++ app (Qt)** | Device detection & inventory GUI, tray, OpenRGB SDK client, the mapper (WLED frame → device zones), later a bespoke Kraken driver. The "bare-metal showcase." |
| **Java backend** | All WLED interaction: mDNS discovery, JSON/WebSocket control, the live-view tap, later the DDP sniffer. A headless microservice. |
| **Headless OpenRGB** | The risky, already-solved bus I/O (MSI ARGB, GPU I2C, SMBus RAM). Bundled, run `--server --noautoconnect`. Spoken to over the SDK (TCP 6742). |

### 3.2 IPC contract (to be finalized in Phase 3)

Loopback only. Leaning toward **gRPC** (AF_UNIX socket on JDK 16+, else 127.0.0.1) for
schema + streaming across languages; a length-framed binary socket is the fallback if
profiling demands. Two logical streams:
- **Java → C++:** `LiveFrame { seq, width, height, bytes[] (RGB) }` at ~25 fps.
- **C++ → Java:** `WledCommand { power?, bri?, effectId?, color?, presetId? }` on demand.

## 4. Device support matrix (this machine)

| Device | Feasibility | Mechanism |
|---|---|---|
| MSI PRO X870E-P ARGB → RS120 fans | Proven* | Motherboard ARGB controller via OpenRGB (*pending confirmation OpenRGB covers this exact board's ARGB chip — top Phase-1 unknown). Positional addressing (fan1 = LEDs 0–7, …). |
| RTX 5070 Ti (`10DE:2C05`/`1462:5315`) | Likely | GPU I2C `0x68`, OpenRGB **1.0 RC** detector (not 0.9 stable). |
| Corsair Vengeance RGB DDR5 `CMH32GX5M2M6000Z36` | Hard / gated | SMBus; OpenRGB `CorsairDRAMController` + PawnIO signed driver + Admin. **Off by default; opt-in with warning + SPD backup.** |
| NZXT Kraken Elite V2 (`1E71:3012`) ring | Likely / imperfect | USB-HID HUE2 "Direct". OpenRGB support for this model is unreliable → bespoke C++ driver candidate (Phase 5). |
| NZXT Kraken Elite V2 LCD (640×640) | Hard (stretch) | Composite USB: HID control + vendor BULK framebuffer (RGB565). Port liquidctl `kraken3`; PID `1e71:3012` is community-only (PR #882). Needs WinUSB/libusbK (disables CAM). |
| ESP32 room LEDs (WLED) | Proven | Read via WebSocket live-view; control via JSON/WebSocket; (Phase 4) DDP sniff for fidelity. |

## 5. The WLED live-view tap — details & caveats

- **Connect:** WebSocket `ws://<ip>/ws`; send text `{"lv":true}` (`{"lv":false}` to stop).
- **Frames:** binary, ~25 fps (40 ms). `byte[0]='L'` (0x4C); `byte[1]=version`
  (1=1D → pixels at offset 2; 2=2D → `w=byte[2]`,`h=byte[3]`, pixels at offset 4);
  then 3 bytes `R,G,B` per LED.
- **Downsampled:** ≤256 LEDs (ESP8266) / ≤1024 (ESP32) per frame, every n-th pixel.
  Fine — PC devices have far fewer LEDs than that.
- **Brightness-scaled & RGBW-folded:** values are post-master-brightness; a white channel
  is added into R,G,B. If WLED `bri==0`, frames are black.
- **⚠️ Single consumer:** only one live client at a time (a new `{"lv":true}` steals it;
  the WLED web "Peek" tab will contend). ~4 total `/ws` clients.
- **⚠️ "Live data override" must be OFF** (default) for LedFx/realtime output to appear.
  If ON, WLED re-renders its own effects and the tap shows those instead.
- **No HTTP alternative:** `/json/live` returns `{"error":4}` in current firmware;
  `/json/state` gives configured segment colors, not rendered pixels; DDP is receive-only
  (no QUERY/REPLY). The WebSocket is the only live-output read path.
- **Fidelity upgrade (Phase 4, optional):** sniff LedFx's DDP/E1.31 stream on the PC for
  full resolution/rate/RGBW and lower latency — but that only captures LedFx, not
  WLED-internal effects, so it supplements (never replaces) the live-view.

### 5.1 Mapping model (room strip → PC devices)

The room strip and PC devices differ in count/layout, so the mapper needs a policy
(configurable, default TBD in Phase 3):
- **Average/dominant** — all PC devices glow the room's mean color (simplest ambient).
- **Positional stretch** — slices of the strip map to device zones (fan1 ← LEDs a..b, …),
  so PC devices show a spatial slice of the room canvas.

## 6. Decisions log

| # | Decision | Rationale |
|---|---|---|
| D1 | Reuse OpenRGB engine (SDK client), not clean-room drivers | Most target devices already covered incl. 5070 Ti + DDR5; fastest/safest. Bespoke C++ reserved for Kraken (Phase 5). |
| D2 | DDR5 RAM RGB deferred, gated, off by default | Documented SPD/brick risk on this exact kit; everything else works without it. |
| D3 | Qt Widgets for the C++ GUI/tray | Same toolkit as OpenRGB (prior art); LGPL dynamic-link. |
| D4 | WLED is master; PC mirrors via WebSocket live-view | User's stated pipeline; verified feasible incl. LedFx override reflection. |
| D5 | Two processes (C++ + Java) over loopback IPC | Fault isolation; honors the C++/Java split. |

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | DDR5 SPD corruption via SMBus | Delegate to OpenRGB; off-by-default opt-in; SPD backup; never touch `0x48`/`0x50–57`. |
| R2 | MSI X870E-P ARGB not covered by OpenRGB | Verify against OpenRGB supported list + live probe before committing Phase 1 fan control. |
| R3 | WLED "Live data override" ON → mirror shows effects not LedFx | Detect via `info.lm`; warn user; document the required setting. |
| R4 | Live-view single-consumer contention (web Peek steals it) | One long-lived subscriber in Java; reconnect logic; document. |
| R5 | Mirror latency (LedFx→WiFi→WLED→WiFi→PC + 25fps cap) | Acceptable for ambient; DDP-sniff path (Phase 4) for tight sync. |
| R6 | OpenRGB version skew / 5070 Ti only in RC | Pin a specific OpenRGB 1.0 RC build; negotiate SDK protocol version. |
| R7 | Windows SMBus driver trust (WinRing0 blocklisted) | Use PawnIO (MS-signed), separate consented install; app works without it. |
| R8 | Kraken LCD WinUSB binding disables CAM; fragile | Isolated optional module; bundle patched libusb; stretch only. |

## 8. Open questions for the user
- ESP32 IP / mDNS name.
- Mapping default preference (average vs positional).
- Whether to pursue the DDP-sniff fidelity path (Phase 4) at all.

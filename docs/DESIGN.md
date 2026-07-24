# Design & architecture

How **wled-pc-rgb** is put together and why. This reflects the shipped design.

## 1. What it is

A tool that makes a PC's RGB hardware a **mirror of a WLED-driven light setup**. Unlike
OpenRGB (the local-device engine it builds on) or LedFx (a source), the defining idea is
the **data-flow direction**:

> **WLED is the master. The PC follows.**
> The WLED device holds the authoritative "picture." The app reads WLED's *live rendered
> output* and applies it to the PC's RGB devices — so whatever WLED shows (an effect, a
> colour, or an external takeover such as **LedFx making it audio-reactive**) the PC
> reproduces automatically. The app never renders RGB independently of WLED.

The app can also *control* WLED (set its colour), but it only ever changes WLED's colour —
never its power or brightness.

## 2. Key facts this design rests on

1. **WLED runs on the ESP32, not the PC** — the WLED node stays the single source of truth;
   the PC is a client that reads its output back.
2. **The mirror mechanism is WLED's WebSocket live-view** — `ws://<ip>/ws` + `{"lv":true}`
   → binary frames of the real pixel buffer, which **reflects external realtime overrides
   (LedFx)** by default. Caveats in §5.
3. **Lower-latency alternative:** LedFx (or anything) can stream **DDP** or **E1.31/sACN**
   straight to the PC; the app listens for both and prefers them when active.
4. **Device I/O is delegated to OpenRGB**, run non-elevated for safety (see §6).

## 3. Architecture

Three cooperating pieces, so a crash-prone device driver can't take down the whole app:

```
                        ┌──────────────────────────────────────────────┐
                        │  C++ app (Qt) — GUI + device core             │
   set colour           │   • setup UI, device list, system tray        │
   ┌───────────────────►│   • OpenRGB SDK client (TCP 6742)             │
   │                    │   • MAPPER: WLED frame → PC device colours     │
   │                    └───────────────▲──────────────┬────────────────┘
   │                     WLED frames     │              │ per-device colours
   │                     (loopback IPC)  │              ▼   (UpdateLEDs / mode)
   │             ┌───────────────────────┴──┐   ┌────────────────────────┐
   │  loopback   │  Java helper (headless)   │   │  OpenRGB (--server,    │
   │  TCP 47900  │   • WLED control (colour)  │   │  non-elevated)         │
   │             │   • live-view tap (ws)     │   │   • motherboard ARGB   │
   │             │   • DDP (4048) + sACN(5568)│   │   • GPU / AIO / mouse  │
   │             └───────────┬────────────────┘   └────────────────────────┘
   │   colour cmd            │  ▲ live-view / DDP / sACN
   │   ┌────────────────────►│  │ frames
   │   │                     ▼  │
   │  ┌┴──────────────────────────────┐        LedFx (optional, external)
   └──┤  WLED device (the room lights) │◄──────── DDP / E1.31 realtime (audio-reactive)
      └────────────────────────────────┘
```

**Mirror flow:** LedFx or a WLED effect drives WLED → the Java helper reads the rendered
frames (live-view, or a DDP/sACN feed pointed at the PC) → forwards them over loopback IPC
→ the C++ mapper turns each frame into per-device colours → OpenRGB writes them to the
hardware.

**Control flow:** the app sends a colour to the Java helper → WLED's JSON API. Colour only.

### 3.1 Components

| Component | Responsibility |
|---|---|
| **C++ app (Qt Widgets)** | Setup UI, device list + per-device toggle, tray, the colour mapper (brightness / gain / floor / idle, and Spread/Wrap layout), and the OpenRGB SDK client. |
| **Java helper** (`WledBackend.java`) | All WLED interaction: the live-view tap, the DDP + sACN listeners, `/json/state` polling, and colour commands. A single-file headless microservice the app launches automatically. |
| **OpenRGB** | The device I/O (motherboard ARGB, GPU I2C, USB-HID). Run `--server --noautoconnect`, non-elevated, spoken to over its network SDK (TCP 6742). |

### 3.2 IPC

Loopback TCP on `127.0.0.1:47900`, newline-delimited JSON, `TCP_NODELAY` for low latency:

- **helper → app:** `{"type":"hello",...}` once, then
  `{"type":"frame","avg":"#rrggbb","cols":[...],"src":"live|ddp|sacn","reachable":b,"on":b,"bri":n}`
  pushed the instant each WLED frame is decoded (with a ~2 Hz keepalive).
- **app → helper:** `{"type":"wled","color":"#rrggbb"}` and `{"type":"host","host":"..."}`.

Two processes (not JNI-fused) for **fault isolation** — a misbehaving USB driver must not
take down the JVM, and vice-versa.

## 4. The colour mapper

Each frame carries an average colour plus a set of buckets across the strip. Per PC device:

- **Average** (default) — one colour everywhere.
- **Spread** — the whole strip is stretched across each device's LEDs.
- **Wrap** — the strip is distributed *once* across all included devices in sequence, so
  the colour flows from one device to the next.

Every output colour then passes through the tone transform: `brightness × gain`, a
minimum-brightness **floor** that lifts dim (non-black) content, and an **idle** step that
maps a fully-black result to *off* (or a chosen idle colour). Devices that can't do per-LED
colour (e.g. an AIO cooler in a single-colour mode) get the average of their slice.

## 5. The WLED live-view tap — caveats

- **Connect:** WebSocket `ws://<ip>/ws`; send `{"lv":true}`. Frames are binary
  (`byte[0]='L'`, a version byte, then `R,G,B` per LED), ~25 fps, downsampled and
  post-master-brightness (so `bri==0` reads as black).
- **Single consumer:** WLED serves one live-view client at a time (the web UI "Peek" tab
  contends). The helper keeps one long-lived subscription and reconnects as needed.
- **"Live data override" must be OFF** (the default) for LedFx/realtime output to appear —
  otherwise WLED re-renders its own effects and the tap shows those instead.
- The DDP (4048) / sACN (5568) listeners are the low-latency alternative and capture LedFx
  streams pointed at the PC; they supplement, never replace, the live-view.

## 6. Safety decisions

- **OpenRGB stays non-elevated.** Elevated OpenRGB probes the motherboard SMBus, which on
  some boards risks the DDR5 SPD and can hang detection. Non-elevated is safe and reliable.
  Trade-off: GPU RGB is detected but only lights physically when OpenRGB is run as admin —
  a choice left to the user.
- **RAM / DDR5 RGB is out of scope.** The app never writes to the SMBus/DIMMs (brick risk).
- **WLED's own brightness is never changed** — the app sends colour only, so its master
  brightness stays exactly where you set it.

## 7. Notable decisions

| Decision | Rationale |
|---|---|
| Reuse the OpenRGB engine (SDK client) rather than clean-room device drivers | Most devices are already supported; fastest and safest. |
| WLED is master; PC mirrors via the live-view (+ optional DDP/sACN) | Matches the intended pipeline and reflects LedFx overrides. |
| Three processes over loopback IPC | Fault isolation across the C++ / Java / OpenRGB split. |
| Qt Widgets for the GUI | Same toolkit family as OpenRGB; LGPL dynamic-link. |
| Non-elevated OpenRGB, no SMBus/RAM | Eliminates the DIMM-brick risk entirely. |

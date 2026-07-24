# Design and architecture

This document explains how **wled-pc-rgb** is built, and why. It describes the released
design.

## 1. What it is

An app that shows a PC's RGB hardware as a copy of a WLED light setup. OpenRGB is the local
device engine that the app uses. LedFx is one source. The important idea here is the
direction of the data:

> **WLED is the master. The PC follows.**
> The WLED device holds the true picture. The app reads the live output of WLED and shows it
> on the PC RGB devices. So the PC shows what WLED shows — an effect, a color, or an override
> from LedFx. The app does not make its own light.

The app can also control WLED, and it can set the color. But it changes the color of WLED
only. It never changes the power or the brightness.

## 2. Main facts

1. **WLED runs on the ESP32, not on the PC.** The WLED device is the one source of truth.
   The PC is a client that reads the output.
2. **The mirror uses the WLED live-view WebSocket** — `ws://<ip>/ws` with `{"lv":true}`.
   WLED sends binary frames of the real pixels. By default, these frames also show LedFx
   overrides. See the notes in section 5.
3. **There is a faster path.** LedFx (or another program) can send **DDP** or **E1.31/sACN**
   directly to the PC. The app listens for both, and it uses them when they send data.
4. **OpenRGB does the device input and output.** The app runs it without administrator
   rights, for safety (see section 6).

## 3. Architecture

The app has three parts. If one device driver stops, the other parts continue.

```
                        ┌──────────────────────────────────────────────┐
                        │  C++ app (Qt) — window + device core          │
   set color            │   • setup UI, device list, system tray        │
   ┌───────────────────►│   • OpenRGB SDK client (TCP 6742)             │
   │                    │   • MAP: WLED frame → PC device colors         │
   │                    └───────────────▲──────────────┬────────────────┘
   │                     WLED frames     │              │ colors for each device
   │                     (loopback IPC)  │              ▼   (UpdateLEDs / mode)
   │             ┌───────────────────────┴──┐   ┌────────────────────────┐
   │  loopback   │  Java helper (headless)   │   │  OpenRGB (--server,    │
   │  TCP 47900  │   • WLED control (color)   │   │  no admin rights)      │
   │             │   • live-view tap (ws)     │   │   • motherboard ARGB   │
   │             │   • DDP (4048) + sACN(5568)│   │   • GPU / AIO / mouse  │
   │             └───────────┬────────────────┘   └────────────────────────┘
   │   color command         │  ▲ live-view / DDP / sACN
   │   ┌────────────────────►│  │ frames
   │   │                     ▼  │
   │  ┌┴──────────────────────────────┐        LedFx (optional, another program)
   └──┤  WLED device (the room lights) │◄──────── DDP / E1.31 in real time (audio-reactive)
      └────────────────────────────────┘
```

**Mirror flow:** LedFx or a WLED effect drives WLED. The Java helper reads the frames (the
live-view, or a DDP/sACN feed sent to the PC). It sends the frames to the app over loopback
IPC. The C++ map turns each frame into a color for each device. OpenRGB writes the colors to
the hardware.

**Control flow:** the app sends a color to the Java helper. The helper sends it to the WLED
JSON API. It is a color only.

### 3.1 Parts

| Part | Job |
|---|---|
| **C++ app (Qt Widgets)** | The setup UI, the device list and per-device switch, the tray, the color map (brightness, gain, floor, idle, and the Spread/Wrap layout), and the OpenRGB SDK client. |
| **Java helper** (`WledBackend.java`) | All the work with WLED: the live-view tap, the DDP and sACN listeners, the `/json/state` reads, and the color commands. It is one file. The app starts it. |
| **OpenRGB** | The device input and output (motherboard ARGB, GPU I2C, USB). Run it with `--server --noautoconnect`, and without administrator rights. The app talks to it over its network SDK (TCP 6742). |

### 3.2 IPC

The two programs talk on loopback TCP, port 47900. The data is JSON, one message on each
line. The sockets use TCP_NODELAY for a low delay.

- **helper → app:** first `{"type":"hello",...}`, then a frame message for each WLED frame:
  `{"type":"frame","avg":"#rrggbb","cols":[...],"src":"live|ddp|sacn","reachable":b,"on":b,"bri":n}`.
  The helper sends the frame as soon as it reads it. It also sends a keepalive about 2 times
  each second.
- **app → helper:** `{"type":"wled","color":"#rrggbb"}` and `{"type":"host","host":"..."}`.

There are two programs, not one, for fault isolation. A bad USB driver must not stop the
JVM, and a bad JVM must not stop the C++ app.

## 4. The color map

Each frame has an average color and a set of parts across the strip. For each PC device:

- **Average** (default) — one color on all devices.
- **Spread** — the app shows the full strip across the LEDs of each device.
- **Wrap** — the app puts the strip one time across all the selected devices in sequence, so
  the color moves from one device to the next.

Then each color goes through the tone steps: brightness × gain, a minimum-brightness floor
that lifts dim (non-black) colors, and an idle step. The idle step turns a black result off,
or shows a color that you choose. A device that cannot set each LED (for example, an AIO
cooler in a single-color mode) shows the average of its part.

## 5. The WLED live-view — notes

- **Connect:** WebSocket `ws://<ip>/ws`. Send `{"lv":true}`. The frames are binary
  (`byte[0]='L'`, then a version byte, then `R,G,B` for each LED). There are about 25 frames
  each second. The data has fewer LEDs than the strip, and it uses the master brightness (so
  `bri==0` is black).
- **One client only:** WLED serves one live-view client at a time. (The "Peek" tab in the
  web UI takes it too.) The helper keeps one connection, and it connects again when it must.
- **"Live data override" must be OFF** (the default). Then the LedFx output shows in the tap.
  If it is ON, WLED makes its own effects, and the tap shows those.
- The DDP (4048) and sACN (5568) listeners are the fast path. They read the LedFx streams
  that go to the PC. They add to the live-view. They do not replace it.

## 6. Safety choices

- **OpenRGB runs without administrator rights.** An elevated OpenRGB reads the motherboard
  SMBus. On some boards, this is a risk to the DDR5 SPD, and it can stop the device scan.
  Without administrator rights, it is safe and it works well. The cost: OpenRGB finds the GPU
  RGB, but the GPU lights only when you run OpenRGB as administrator. This is your choice.
- **The app does not do RAM/DDR5 RGB.** The app never writes to the SMBus or the DIMMs (a
  brick risk).
- **The app does not change the brightness of WLED.** It sends a color only, so the master
  brightness stays where you set it.

## 7. Main decisions

| Decision | Reason |
|---|---|
| Use the OpenRGB engine (as an SDK client), not new device drivers. | OpenRGB already supports most devices. This is the fast and safe way. |
| WLED is the master. The PC mirrors it with the live-view (and the DDP/sACN paths). | This is the correct pipeline, and it shows LedFx overrides too. |
| Three parts over loopback IPC. | Fault isolation across the C++, Java, and OpenRGB parts. |
| Qt Widgets for the window. | The same toolkit family as OpenRGB. It links to LGPL DLLs. |
| OpenRGB without administrator rights, and no SMBus/RAM. | This removes the risk to the memory. |

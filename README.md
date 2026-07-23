# wled-pc-rgb

A native, cross-platform tool that makes this PC's RGB hardware a **mirror of a
WLED-driven room**. The defining idea is the data-flow direction:

> **WLED is the master. The PC follows.**
> The program reads WLED's *live rendered output* back and applies it to the PC's RGB
> devices — so whatever WLED shows (a WLED effect, a color you set, or an external
> takeover like **LedFx making it audio-reactive**) is reproduced on your fans, GPU,
> and (optionally) RAM automatically. Pipeline: `program → WLED → back to program`.

- **C++ app** (Qt) — device detection, the device-inspector GUI, the system tray, and
  the **mapper** that distributes WLED's live pixels across PC device zones. Talks to
  hardware through a bundled **headless OpenRGB** over its SDK (TCP `6742`).
- **Java backend** — a headless WLED microservice: discovers the ESP32, **controls** it
  (JSON/WebSocket), and **reads its live output back** via the WebSocket live-view
  (`{"lv":true}`), forwarding frames to the C++ side.
- Two processes over **loopback IPC** — for fault isolation (a crashing USB/SMBus driver
  must not take down the JVM).

See [docs/DESIGN.md](docs/DESIGN.md) for architecture and [docs/ROADMAP.md](docs/ROADMAP.md)
for the phase plan.

## Target hardware (this machine, verified)

| Part | Identity | Path |
|---|---|---|
| Motherboard | MSI PRO X870E-P WIFI (MS-7E70), AMD X870E | ARGB header → RS120 fans, via OpenRGB (Mystic Light) |
| GPU | NVIDIA RTX 5070 Ti (`10DE:2C05` / `1462:5315`) | GPU I2C `0x68`, OpenRGB **1.0 RC** detector |
| RAM | 2× Corsair Vengeance RGB DDR5 `CMH32GX5M2M6000Z36` | SMBus — **deferred / gated** (DDR5 brick risk) |
| AIO | NZXT Kraken Elite V2 (`1E71:3012`) | RGB ring (USB-HID) + 640×640 LCD (bespoke, stretch) |
| Room | ESP32 running stock WLED | Live-view read + JSON/WebSocket control |

## Status

Phase 0 (scaffold). The two spikes below prove the integration seams — the C++→OpenRGB
seam (list PC devices) and the WLED→PC seam (mirror WLED's live output) — before we
build the full app.

## Prerequisites (Windows, Python-free)

Nothing dev-related is installed on this machine yet. Recommended toolchain:

**C++ / Qt (via MSYS2 + MinGW-w64 UCRT64):**
```bash
winget install MSYS2.MSYS2
```
Then in the **MSYS2 UCRT64** shell:
```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
```

**Java (JDK 21 LTS):**
```bash
winget install Microsoft.OpenJDK.21
```

**OpenRGB — the device engine.** Install the **1.0 release-candidate / experimental**
build (NOT 0.9 stable — 0.9 lacks the RTX 5070 Ti detector), then run it headless:
```bash
OpenRGB.exe --server --noautoconnect
```
Close/stop **iCUE, MSI Center/Mystic Light, and NZXT CAM** (and their background
services) first — concurrent access to the same bus/device causes flicker, lost
detection, and (on the SMBus) corruption.

## Running the Phase-0 spikes

### 1. C++ → OpenRGB (see your PC devices)
Connects to the OpenRGB SDK server and lists detected controllers. Dependency-free:
```bash
cmake -S core-cpp -B core-cpp/build -G Ninja
cmake --build core-cpp/build
./core-cpp/build/orgb_probe            # defaults to 127.0.0.1:6742
```

### 2. Java → WLED (mirror the room's live output)
Reads WLED's live view and prints the decoded frames (LED count + average color),
so you can confirm the mirror reflects whatever WLED is showing — **including LedFx**.
Only needs a JDK (single-file launch, no build step):
```bash
java wled-backend-java/spike/WledMirrorSpike.java <esp32-ip> 30
```
> In WLED, **Config → Sync Interfaces → "Live data override" must be OFF** (default)
> for LedFx/realtime output to appear in the mirror. Make sure the strip is on and
> brightness > 0, or frames read as black.

## What I need from you to move forward
1. The **ESP32's IP address** (or confirm mDNS name `wled.local`).
2. Confirm you'll install **OpenRGB 1.0 RC** (or say the word and I'll script it).
3. Whether you want me to **run the toolchain install** for you, or you'll do it.
4. Preferred mapping default once we reach Phase 3: **average color** (all devices glow
   the room's mean) vs **positional** (device zones show slices of the strip).

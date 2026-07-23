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

**v1.0 — working end-to-end.** The mirror is complete: the app auto-launches OpenRGB
(non-elevated, so RAM/SMBus is never touched) and the Java WLED backend, detects the
PC's RGB devices, and mirrors WLED's live output onto the ticked devices in real time —
including LedFx audio-reactive takeovers (via the WLED live-view, or lower-latency DDP on
UDP 4048 / E1.31 sACN on UDP 5568 straight to this PC). Colour can be tuned per-PC with
independent brightness, a flash-gain multiplier and a never-off minimum-brightness floor,
and mapped positionally (Spread the whole strip per device, or Wrap it once across all
devices). The NZXT Kraken ring is driven via its Static mode; the mouse and motherboard
ARGB fans light directly; the GPU is detected (its RGB needs OpenRGB run as admin).
Shipped as a self-contained, MIT-licensed portable Windows zip.

- **Full setup + usage:** see **[docs/USAGE.md](docs/USAGE.md)**.
- **Phase plan / history:** see [docs/ROADMAP.md](docs/ROADMAP.md) (Phases 0/1/3 done,
  Phase 4 DDP tap done; Phase 2 RAM scrapped for safety; Kraken LCD deferred — needs a
  manual Zadig/WinUSB driver swap).

The dev-toolchain notes below are for building from source.

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

## Install & run

Download the latest `wled-pc-rgb-vX.Y-win64.zip`, unzip anywhere, and run
`wled_pc_rgb.exe`. It's fully self-contained (Qt + all DLLs bundled). You only need two
things installed that the app launches for you — a **JDK 21+** and **OpenRGB** — see
**[docs/USAGE.md](docs/USAGE.md)** for the full setup, use cases (LedFx, spread/wrap,
gain/floor), and troubleshooting.

## Build from source

Windows, via MSYS2 UCRT64 (gcc + cmake + ninja + qt6-base — see Prerequisites above):
```bash
cmake -S core-cpp -B core-cpp/build -G Ninja
cmake --build core-cpp/build           # a POST_BUILD step bundles the full DLL closure
```
The self-contained app lands in `core-cpp/build/app/` (exe + `WledBackend.java` + DLLs).
`powershell -File scripts/package-win.ps1` zips it into `dist/` as a portable release.
The Java backend is single-file (no build step); it's launched by the app automatically.

## License

MIT — see [LICENSE](LICENSE). This project bundles the **Qt 6** runtime (LGPL v3, as
separate replaceable DLLs) and talks to **OpenRGB** over its network SDK (OpenRGB is a
separate GPL program that you install; its code is not linked in). Other bundled DLLs are
the MinGW-w64 runtime and common permissive libraries (zlib, libpng, freetype, …).

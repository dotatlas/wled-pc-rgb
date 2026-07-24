# wled-pc-rgb

![license](https://img.shields.io/badge/license-MIT-blue)
![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D6)
![built with Qt 6](https://img.shields.io/badge/built%20with-Qt%206-41CD52)
![engine: OpenRGB](https://img.shields.io/badge/engine-OpenRGB-cc0000)

**Make your PC's RGB a live mirror of your WLED lights.** Whatever your WLED strip is
showing — an effect, a solid colour, or a **LedFx audio-reactive** takeover — is
reproduced on your PC's fans, GPU, AIO, mouse and motherboard in real time.

> **WLED is the master. The PC follows.**
> The app reads WLED's *live rendered output* and applies it to your PC devices. It never
> invents its own effect — it shows exactly what WLED shows, so your whole room and rig
> stay in sync automatically.

---

## Features

- 🎨 **Real-time mirror** of WLED's live output onto every RGB device OpenRGB can see.
- 🎵 **Audio-reactive via LedFx** — reads WLED's output back, so LedFx takeovers show on
  the PC too. Point LedFx straight at this PC (DDP or E1.31/sACN) for the lowest latency;
  the app auto-selects the fastest available source.
- ✅ **Per-device toggle** — choose exactly which devices mirror; the rest are left alone.
- 🎚️ **Tone controls** — independent PC brightness, a **flash gain** multiplier to make
  dim flashes pop, and a **minimum-brightness floor** to lift dim content.
- 🌑 **Off means off** — a black frame turns the PC off, or shows an **idle colour** you pick.
- 🌈 **Positional mapping** — *Spread* the whole strip across each device, or *Wrap* it
  once across all devices so the colour flows from one to the next.
- 🚀 **Just works** — auto-launches OpenRGB and its WLED helper, lives in the system tray,
  optional start-at-login / start-minimised, and remembers all your settings.
- 🔒 **Safe by design** — never touches RAM/SMBus (no DIMM-brick risk) and never changes
  WLED's own brightness.
- 📦 **Portable & self-contained** — a single unzip-and-run folder. MIT licensed.

## Requirements

| Need | Why |
|---|---|
| **Windows 10/11 (64-bit)** | the shipped build |
| **[OpenRGB](https://openrgb.org)** | the device engine — the app drives your hardware through it (auto-launched if installed) |
| **A JDK 21+** ([Adoptium](https://adoptium.net)) | runs the small bundled WLED helper (auto-launched) |
| **A WLED device** on your network | the thing you're mirroring |

## Install & run

1. Download the latest **`wled-pc-rgb-vX.Y-win64.zip`** from the
   [Releases](https://github.com/dotatlas/wled-pc-rgb/releases) page.
2. Unzip it anywhere and run **`wled_pc_rgb.exe`** — it's fully self-contained (Qt and all
   DLLs are bundled; no installer, no PATH setup).
3. Make sure OpenRGB and a JDK are installed (see above). The app launches both for you.

## Quick start

1. Watch the three **setup dots** at the top go green: `OpenRGB · Backend · WLED`.
2. Type your **WLED host** (`wled.local` or its IP) and click **Apply**.
3. **Tick the devices** you want to mirror.
4. Click **▶ Mirror WLED**.
5. Close the window — it keeps running in the tray.

The full walkthrough, every control, all the use cases, and troubleshooting live in
**[docs/USAGE.md](docs/USAGE.md)**.

## How it works

Three small pieces, so a crashing device driver can never take down the whole app:

```
  WLED (ESP32)  ──live output──►  Java helper  ──frames (loopback)──►  C++ app (Qt)
       ▲                          (WebSocket /                          │  maps colours
       │  control (colour only)    DDP / sACN)                          ▼  to devices
       └───────────────────────────────────────────────  OpenRGB (SDK, TCP 6742)
                                                              └─► fans · GPU · AIO · mouse
```

- **C++ app (Qt)** — the GUI, tray, device list, colour mapping, and the OpenRGB client.
- **Java helper** — talks to WLED: reads its live output and forwards frames; sends colour
  commands. A tiny headless microservice the app starts automatically.
- **OpenRGB** — does the actual, already-solved device I/O, spoken to over its network SDK.

Architecture details and design decisions are in **[docs/DESIGN.md](docs/DESIGN.md)**.

## Build from source

Windows, using MSYS2 UCRT64 (`gcc`, `cmake`, `ninja`, `qt6-base`):

```bash
# in an MSYS2 UCRT64 shell
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
cmake -S core-cpp -B core-cpp/build -G Ninja
cmake --build core-cpp/build          # a POST_BUILD step bundles the full DLL closure
```

The self-contained app lands in `core-cpp/build/app/`. Run `scripts/package-win.ps1` to
zip a portable release into `dist/`. The Java helper is a single source file launched by
the app — no separate build step. See **[CONTRIBUTING.md](CONTRIBUTING.md)** for more.

## Compatibility

Device support comes from **OpenRGB**, so anything OpenRGB detects can be mirrored. A few
device-specific notes (all covered in [docs/USAGE.md](docs/USAGE.md)):

- **NZXT Kraken ring** lights only in a single-colour mode — the app switches it to Static
  automatically while mirroring.
- **GPU RGB** is detected but only lights when OpenRGB is run as administrator (the app
  keeps OpenRGB non-elevated by default, so it never probes the motherboard SMBus/RAM).
- **Motherboard ARGB headers** may need a one-click zone resize (configurable, default 8
  LEDs per header).

Developed and tested on an MSI X870E board, RTX 5070 Ti, NZXT Kraken Elite, a Razer mouse,
and an ESP32 running stock WLED — but it isn't tied to that hardware.

## Roadmap

See **[docs/ROADMAP.md](docs/ROADMAP.md)**. The mirror is feature-complete; the open ideas
are optional extras (in-house audio reactivity, NZXT Kraken LCD).

## Contributing

Issues and pull requests are welcome — see **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## License & credits

Released under the **MIT License** — see [LICENSE](LICENSE).

Built on the shoulders of great open-source projects: **[OpenRGB](https://openrgb.org)**
(device engine), **[WLED](https://kno.wled.ge)** (the master light controller),
**[LedFx](https://www.ledfx.app)** (audio reactivity), and **[Qt](https://www.qt.io)**
(the GUI, bundled as LGPL v3 replaceable DLLs). OpenRGB is a separate GPL program the app
talks to over its network SDK; its code is not linked in.

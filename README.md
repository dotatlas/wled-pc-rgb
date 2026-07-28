# wled-pc-rgb

![license](https://img.shields.io/badge/license-MIT-blue)
![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D6)
![built with Qt 6](https://img.shields.io/badge/built%20with-Qt%206-41CD52)
![engine: OpenRGB](https://img.shields.io/badge/engine-OpenRGB-cc0000)

**Show your WLED lights on your PC RGB devices in real time.** When your WLED strip shows
an effect, a color, or a **LedFx audio-reactive** pattern, your PC fans, GPU, AIO, mouse,
and motherboard show the same colors.

---

## Requirements

| You need | Reason |
|---|---|
| **Windows 10 or 11 (64-bit)** | The app runs on this. |
| **[OpenRGB](https://openrgb.org)** | The app controls your devices through OpenRGB. The app starts it for you. |
| **A JDK, version 21 or higher** ([Adoptium](https://adoptium.net)) | It runs the small WLED helper. The app starts it for you. |
| **A WLED device** on your network | This is the light that you mirror. |

## Install and run

1. Open the [Releases](https://github.com/dotatlas/wled-pc-rgb/releases) page. Download the
   newest **`wled-pc-rgb-vX.Y-win64.zip`**.
2. Unzip the file to any folder. Run **`wled_pc_rgb.exe`**.
3. Make sure that OpenRGB and a JDK are installed. The app starts both for you.

The folder has Qt and all the DLLs in it. There is no installer. There is no PATH setup.

## Quick start

1. Look at the three status dots at the top: `OpenRGB · Backend · WLED`. Wait for them to
   become green.
2. Type your WLED address (`wled.local` or the IP). Click **Apply**.
3. Select the devices that you want to mirror.
4. Click **▶ Mirror WLED**.
5. Close the window. The app continues in the tray.

For all the controls, the use cases, and help, read **[docs/USAGE.md](docs/USAGE.md)**.

## Build from source

Build on Windows with MSYS2 UCRT64 (`gcc`, `cmake`, `ninja`, `qt6-base`):

```bash
# in an MSYS2 UCRT64 shell
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
cmake -S core-cpp -B core-cpp/build -G Ninja
cmake --build core-cpp/build          # a POST_BUILD step adds all the DLLs
```

The app is built to `core-cpp/build/app/`. Run `scripts/package-win.ps1` to make a portable
zip in `dist/`. The Java helper is one source file, and the app starts it. There is no
separate build step for it. For more information, read **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## Compatibility

OpenRGB gives the device support. Thus the app can mirror any device that OpenRGB finds.
Some devices have special notes (read the details in [docs/USAGE.md](docs/USAGE.md)):

- **NZXT Kraken ring** — it shows light only in a single-color mode. The app sets it to
  Static automatically when it mirrors.
- **GPU RGB** — OpenRGB finds it, but it shows light only when you run OpenRGB as
  administrator. By default the app runs OpenRGB without administrator rights, so it does
  not touch the motherboard SMBus or the RAM.
- **Motherboard ARGB headers** — you can set the number of LEDs for each header. The default
  is 8.

The developer made and tested the app with an MSI X870E board, an RTX 5070 Ti, an NZXT
Kraken Elite, a Razer mouse, and an ESP32 with WLED. But the app is not limited to this
hardware.

## How to contribute

You can send issues and pull requests. Read **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## License and credits

The license is **MIT**. Read [LICENSE](LICENSE).

This project uses other open-source projects: **[OpenRGB](https://openrgb.org)** (the
device engine), **[WLED](https://kno.wled.ge)** (the master light controller),
**[LedFx](https://www.ledfx.app)** (the audio source), and **[Qt](https://www.qt.io)** (the
window). The app includes Qt as LGPL v3 DLLs, and you can replace them. OpenRGB is a
separate GPL program. The app talks to it over the network. The app does not include the
code of OpenRGB.

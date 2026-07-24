# Contributing to wled-pc-rgb

Thanks for your interest! This is a small, focused project — issues and pull requests are
welcome.

## Reporting bugs / requesting features

Open an [issue](https://github.com/dotatlas/wled-pc-rgb/issues). For bugs, please include:

- What you expected vs. what happened.
- Your OS, OpenRGB version, and JDK version.
- The devices involved (and which RGB software you have installed — iCUE, NZXT CAM,
  SignalRGB, etc. can fight for the same devices).
- The device scan dump the app writes to `%TEMP%\wled-pc-rgb-scan.txt`.

## Project layout

```
core-cpp/            C++ Qt app (the GUI, tray, colour mapping, OpenRGB client)
  app/src/           main.cpp, main_window.*, orgb_client.*, ipc_client.*, sysinfo.*
  scripts/           deploy-win.sh (bundles the self-contained DLL closure)
wled-backend-java/   the WLED helper (single-file WledBackend.java, no build step)
scripts/             package-win.ps1 (builds the portable release zip)
packaging/           wled-pc-rgb.iss (optional Inno Setup installer script)
docs/                DESIGN.md (architecture), USAGE.md (user guide), ROADMAP.md
```

See **[docs/DESIGN.md](docs/DESIGN.md)** for the architecture and why it's split into
three processes.

## Building

Windows, via MSYS2 UCRT64:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
cmake -S core-cpp -B core-cpp/build -G Ninja
cmake --build core-cpp/build
```

The self-contained app is built to `core-cpp/build/app/`. You'll also need a JDK 21+ and
OpenRGB installed to run it. `powershell -File scripts/package-win.ps1` produces a portable
zip in `dist/`.

## Testing your change

There's no unit-test harness; verify against real hardware and the headless helpers:

- Launch the app and confirm it still detects devices and mirrors (the scan dump is written
  on every scan).
- Headless colour/mirror checks:
  ```
  wled_pc_rgb.exe --set <deviceIndex> <#rrggbb> [brightness%]
  wled_pc_rgb.exe --mirror <seconds> [spread|wrap]
  ```
- For colour-math or protocol changes, a quick standalone `g++` program that copies the
  pure function is an easy way to check the arithmetic in isolation.

## Pull requests

- Keep changes focused and match the surrounding code style (it's plain, comment-light
  where the code is obvious, with a short "why" comment where it isn't).
- Every change should leave the app building and launching.
- **Keep `scripts/*.ps1` pure ASCII** — Windows PowerShell 5.1 reads scripts as ANSI, so a
  stray `—`/`→` breaks parsing.
- Describe what you changed and how you verified it.

## Safety ground rules

- **Never write to the motherboard SMBus / DIMM RAM.** It carries a real brick risk and is
  out of scope on purpose. Keep OpenRGB non-elevated by default.
- The app controls WLED's **colour only** — never its power or brightness.

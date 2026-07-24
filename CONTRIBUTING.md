# How to contribute to wled-pc-rgb

Thank you for your interest. This is a small project. Issues and pull requests are welcome.

## Report a problem or ask for a feature

Open an [issue](https://github.com/dotatlas/wled-pc-rgb/issues). For a problem, add this
information:

- What you expected, and what happened.
- Your operating system, your OpenRGB version, and your JDK version.
- The devices that you use. Also list the other RGB programs that you have (iCUE, NZXT CAM,
  SignalRGB, Synapse, and so on). They can take the same devices.
- The device scan file. The app writes it to `%TEMP%\wled-pc-rgb-scan.txt`.

## Project files

```
core-cpp/            The C++ Qt app (the window, the tray, the color map, the OpenRGB client)
  app/src/           main.cpp, main_window.*, orgb_client.*, ipc_client.*, sysinfo.*
  scripts/           deploy-win.sh (adds the DLLs to make the app self-contained)
wled-backend-java/   The WLED helper (one file, WledBackend.java; no build step)
scripts/             package-win.ps1 (makes the portable release zip)
packaging/           wled-pc-rgb.iss (an optional Inno Setup installer script)
docs/                DESIGN.md (architecture), USAGE.md (user guide), ROADMAP.md
```

For the architecture and the reason for the three parts, read
**[docs/DESIGN.md](docs/DESIGN.md)**.

## Build

Build on Windows with MSYS2 UCRT64:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
cmake -S core-cpp -B core-cpp/build -G Ninja
cmake --build core-cpp/build
```

The app is built to `core-cpp/build/app/`. You also need a JDK 21+ and OpenRGB to run it.
Run `powershell -File scripts/package-win.ps1` to make a portable zip in `dist/`.

## Test your change

There is no unit-test system. Test with real hardware and the command-line helpers:

- Start the app. Make sure that it finds the devices and mirrors. (The app writes the scan
  file at each scan.)
- Use the command-line color and mirror checks:
  ```
  wled_pc_rgb.exe --set <deviceIndex> <#rrggbb> [brightness%]
  wled_pc_rgb.exe --mirror <seconds> [spread|wrap]
  ```
- For a color-math change or a protocol change, write a small standalone `g++` program that
  copies the pure function. This is an easy way to check the math alone.

## Pull requests

- Keep each change small. Match the style of the code near it. (The style is plain. It has
  few comments where the code is clear, and a short reason comment where it is not.)
- Each change must keep the app able to build and start.
- Keep the `scripts/*.ps1` files in plain ASCII. Windows PowerShell 5.1 reads a script as
  ANSI, so a special character (for example `—` or `→`) breaks it.
- Write what you changed and how you tested it.

## Safety rules

- Do not write to the motherboard SMBus or the DIMM RAM. It is a risk to the memory, and it
  is not part of this project. Keep OpenRGB without administrator rights by default.
- The app controls the color of WLED only. It does not change the power or the brightness.

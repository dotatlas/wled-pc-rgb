# Roadmap

This is where **wled-pc-rgb** is now, and where it can go. For the list of released
versions, read the [CHANGELOG](../CHANGELOG.md).

## Done — the mirror is complete

- ✅ Show the live output of WLED on the PC RGB devices in real time (through OpenRGB).
- ✅ Follow WLED effects, colors, and **LedFx** audio-reactive patterns.
- ✅ More than one live source, selected automatically: the WLED live-view, **DDP** (UDP
  4048), and **E1.31/sACN** (UDP 5568) directly to the PC.
- ✅ A switch for each device.
- ✅ One **Brightness** control for the whole mirror (the app does not change WLED brightness).
- ✅ Drive the **NZXT Kraken Elite ring** directly over USB — its own pipeline, with the same
  protocol as SignalRGB (a 512-byte report for each frame) — a smooth, per-LED mirror.
- ✅ Drive a supported **GPU** directly over **NVAPI**, on the GPU's own I2C bus — per-LED, smooth,
  and with **no administrator rights**. (It does not use the motherboard SMBus, so there is no risk
  to the RAM.) For other GPUs, the app rate-limits the OpenRGB path so it does not lag.
- ✅ Position modes: *Spread* (the full strip on each device) and *Wrap* (the strip across
  all devices).
- ✅ A zone size that you can set.
- ✅ A device blacklist: hide a device from the scan so OpenRGB does not touch it. Advanced has
  a dropdown of hidden devices and a "Re-add to scan" button. Session only.
- ✅ The app starts OpenRGB and the WLED helper, stays in the tray, can start at login or
  start small, and keeps the settings.
- ✅ A portable, one-folder Windows release.

## Known issues

- **Only MSI Blackwell (RTX 50-series) GPUs use the direct pipeline.** The app has its own GPU
  driver, which it uses for a card that it knows (at this time, the MSI RTX 5070 Ti Gaming Trio).
  For any other card, the app uses OpenRGB, which needs administrator rights and does not give a
  per-LED mirror. To add more cards, you must add their PCI identity and their LED layout. Help is
  welcome — the pattern is in `core-cpp/app/src/gpu_driver.cpp`.

## Ideas — help is welcome

These are extra options. The mirror works fully without them.

- **In-house audio** — a WASAPI and FFT source, so the PC can react to the sound of the PC
  directly, without LedFx. This is a separate mode from the WLED mirror.
- **A mapping editor** — set which part of the strip goes to which device zone. (Spread and
  Wrap cover the common cases now.)
- **An installer** — there is an [Inno Setup](https://jrsoftware.org/isinfo.php) script in
  `packaging/`. You can build and publish an installer with the zip.
- **More systems** — the parts (Qt, OpenRGB, and a JVM) run on more than one system. But only
  a Windows build exists and is tested now.

## Not planned

- **RAM / DDR5 (SMBus) RGB.** To write to the DIMM SMBus is a risk to the memory on some
  memory kits. So the app never writes to it, and it keeps OpenRGB without administrator
  rights. Everything else works without it.
- **The NZXT Kraken LCD screen.** The app drives the Kraken *ring* live (see the changelog),
  but not the LCD screen. The screen needs a different USB interface (a bulk image transfer),
  which NZXT CAM does well. So the app leaves the LCD to CAM.
- **A bespoke GPU RGB driver (raw SMBus).** GPU RGB is on the SMBus, which needs a
  kernel-level driver and administrator rights. OpenRGB already does this well for many GPUs,
  so the app drives the GPU through OpenRGB (and only rate-limits it, for smoothness). The app
  does not write to the SMBus itself, to keep it safe and simple.

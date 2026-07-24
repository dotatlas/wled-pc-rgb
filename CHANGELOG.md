# Changelog

This is a list of the important changes to **wled-pc-rgb**. The newest is first. Each
version is a git tag (`vX.Y`).

## v1.1.1

- **Fix the Kraken ring lag.** The NZXT Kraken ring shows a color with a full mode-update.
  At the new high frame rate, too many mode-updates made a queue in OpenRGB, so the ring
  changed only after some minutes. The app now sends a cooler update at most about 15 times
  each second, and it does not send the same color again. The ring now follows in real time.

## v1.1

- **Real-time mirror.** The PC now follows WLED at its true frame rate. Before, a fixed
  limit of about 10 frames each second made the light choppy. The WLED helper now sends a
  frame as soon as it reads one. The internal links use TCP_NODELAY for a low delay.
- **Off is off.** A black WLED frame now turns the PC devices off. Before, they showed a
  glow when you set a minimum brightness. The minimum brightness now lifts only dim colors.
- **Idle color.** There is a new option ("When off, show color") and a color picker. An off
  LED stays off, or shows a color that you choose.
- **Zone size.** You can set the number of LEDs for each ARGB zone. The default is 8 (for
  example, an MSI JARGB header). Before, the number was fixed at 24.

## v1.0

- The first public release. It is a portable, one-folder Windows build. The license is MIT.
- Version 1.0.1 fixed the package. It now includes the Qt platform plugins, so the zip runs
  on a clean PC.

## v0.18 to v0.22

- **New window design** — status dots (OpenRGB, backend, WLED), a main **Mirror** button,
  live color swatches, a WLED host field, a system tray with close-to-tray, options for
  start-at-login, start-small, and auto-mirror, and saved settings.
- **Flash gain** (1× to 5×) and a **minimum brightness** floor.
- **Position modes** — *Wrap* mode (the strip moves across all devices) and *Spread* mode
  (the full strip on each device). The gradients are smoother.
- **More live sources** — a DDP listener (UDP 4048) and an E1.31/sACN listener (UDP 5568).
  LedFx can send data directly to the PC. The app selects the fastest source.
- More strength — the connections repair themselves, the app scans again for late USB
  devices, and the app starts OpenRGB and the WLED helper for you.

## v0.7 to v0.17

- **The mirror.** The app reads the live output of WLED and shows it on the PC devices in
  real time. This includes LedFx overrides. It uses the average color and the *Spread*
  position mode.
- A switch for each device, to select which devices mirror. The PC brightness is separate
  (the app does not change the brightness of WLED).
- The app drives the NZXT Kraken ring in Static mode, so it follows along.
- The app keeps OpenRGB without administrator rights, for safety. It does not write to the
  motherboard SMBus or the RAM.

## v0.1 to v0.6

- The project start and the local RGB core: connect to OpenRGB, list the devices, and
  control the colors and modes from a Qt app.

---

*The app does not do RAM/DDR5 RGB. To write to the DIMM SMBus is a risk to the memory.
Everything else works without it.*

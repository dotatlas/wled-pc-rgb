# Changelog

This is a list of the important changes to **wled-pc-rgb**. The newest is first. Each
version is a git tag (`vX.Y`).

## v1.5

- **The Kraken ring is left to NZXT CAM.** Testing showed the ring cannot update live over
  USB — the device throttles streamed color badly (a large delay), and per-LED data does not
  display. So the app no longer drives the Kraken ring at all. The Kraken is hidden from the
  device list by default (blacklisted), so OpenRGB does not touch it and NZXT CAM (or another
  program) controls the ring and the screen. To let OpenRGB drive it instead, re-add it in
  Advanced. This also removed the direct USB driver and the hidapi library.

## v1.4.1

- **The Kraken ring shows one solid color that follows WLED live.** The ring cannot show a
  per-LED pattern reliably (the device collapses per-LED data to nothing), but one color
  displays well and fast. So the ring now shows a single color — the average of what the
  other devices show — updated every frame, even in Spread or Wrap mode. It flashes and
  changes color live with WLED. This is the Kraken-specific pipeline.

## v1.4

- **The Kraken ring shows the strip smoothly.** Before, the ring could show a fixed
  checkerboard pattern (the app sampled only some points of the strip, and it drove a second
  channel). Now the app averages each part of the strip onto the ring's LEDs and uses one
  channel, so the ring shows a smooth gradient that moves and flashes with WLED.
- **Hide the Kraken from the device list.** The Kraken is driven directly, so the app now
  hides it from the scanned device list (it does not need OpenRGB). In Advanced, the "Hidden
  (driven directly)" dropdown shows hidden devices, and "Re-add to scan" puts one back so
  OpenRGB drives it instead. This is for the session only; it resets when you close the app.

## v1.3.1

- **The Kraken ring now moves with everything else.** Before, the ring showed one average
  color, so it did not flash or move like the fans. Now the app streams the WLED strip across
  the ring's LEDs, so the ring shows the same motion and flashes as the rest of your RGB.

## v1.3

- **One brightness slider.** Before, three controls (PC brightness, flash gain, minimum
  brightness) changed the brightness together, which was confusing (you had to set more than
  one to 100% for full brightness). Now there is one **Brightness** slider for the whole
  mirror. The flash gain, the minimum brightness, and the "when off, show color" option are
  removed.
- **All devices on by default.** The app now enables every RGB device it finds, each time.
  You can still turn a device off in the list.
- **Direct mode.** When the mirror starts, the app puts each device in its "Direct" mode (if
  it has one), so the colors show. This fixes the GPU, which started in an "off" mode.
- **The Kraken ring is its own device.** The app drives the Kraken ring only through its
  direct USB driver; OpenRGB does not touch it. This removes the conflict where the ring
  grabbed a color at random or got stuck when you changed modes. The code now has a
  per-device pipeline layer, so more devices can get their own driver later.

## v1.2

- **The NZXT Kraken Elite ring now updates live.** Before, the ring color went through
  OpenRGB, which was slow — the ring changed only after some seconds or minutes. The app
  now talks to the Kraken ring directly over USB (HID), with the same lighting commands
  that OpenRGB and NZXT CAM use (HUE2 "Direct" streaming). So the ring follows WLED in real
  time. This applies only to the NZXT Kraken 2024 Elite; it does not change other coolers.
  You can test the ring with `wled_pc_rgb.exe --kraken #rrggbb`.

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

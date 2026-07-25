# Changelog

This is a list of the important changes to **wled-pc-rgb**. The newest is first. Each
version is a git tag (`vX.Y`).

## v1.7

- **A cleaner window for the public release.** The Advanced section is now grouped into labelled
  panels (Zones, Kraken ring, Colour mapping, Music-reactive only, Hidden devices), so each control
  says what it does. The three position modes (Same colour / Spread / Wrap) are now one set of
  radio buttons, so the choice and the default are clear. The device list is a titled panel, and it
  shows a short tag (for example "direct (NVAPI)") with the full note in a tooltip. The status
  lights use a shape and a colour, so they read without colour. There is a new **About** window
  (press the About button or F1) with the version, the licence and links to the project, the guide
  and the bug tracker. The app has its own icon.
- **New: "Reactive only"** in Advanced. If your source is music-reactive and also shows an always-on
  background color (LedFx does this), the app can subtract the background, so only the reactive part
  lights the PC.
  You **calibrate** it, in three steps: stop the music, tick the box (the app asks you to confirm,
  then measures for about a second), then start the music. A swatch and the hex code show what the app
  stored. Use **Set from now** if you change the background color later. The app keeps the color.
  The app measures for a second, and not one single frame, and it keeps the highest value that it saw.
  A value that is a little too high costs almost nothing, but a value that is a little too low leaves
  a dim background. The app also asks you to confirm, so it cannot store a color while music plays.
  The subtraction is exact arithmetic, not a guess: LedFx adds the background to each pixel, and no
  step after it changes the values, so the app subtracts the same amount. You calibrate instead of
  typing the color, because LedFx multiplies the color by three brightness values before it sends the
  data — so the color on the wire is not the color that you selected.
  Two limits, which the guide explains: at full brightness the two colors mix before the app receives
  them, so the peaks show a little darker; and if the strip shows only the background, the PC stays
  dark, because there is no reactive part.
- **New: "Match strip gamma"** in Advanced, on by default. WLED sends its colors to the strip through
  a gamma curve (2.2), but the app sent them to the PC directly. So the PC was much brighter than the
  strip at low levels — the same color could be 5 times brighter on the PC. Also, the strip shows every
  value from 1 to 14 as black, but the PC showed them, so very dark colors were visible on the PC and
  not on the strip. The app now uses the same curve as WLED, so the PC matches the strip. This is also
  why a small remainder from *Reactive only* is not visible any more. The whole mirror is darker now,
  so you can raise the **Brightness** slider. You can turn the control off.
- **Removed the "Elevate OpenRGB to admin" button.** It is not necessary any more. The GPU has its
  own driver, which needs no administrator rights (see below). The app now never asks for
  administrator rights, and it never reads the motherboard SMBus.
- **The GPU RGB works, and it needs no administrator rights.** Before, the GPU showed no light,
  even with OpenRGB as administrator and after a restart. The reason: OpenRGB cannot control this
  card. It gives the card only one LED zone with an empty update function, it has no per-LED
  command set for MSI GPUs, and it does not send the "50 Series" enable command that a Blackwell
  (RTX 50-series) card needs. Also, it waits 20 ms after each command, which gives about 7 frames
  each second.
  The app now controls the GPU itself, with the same method as SignalRGB: **NVAPI**, on the
  **GPU's own I2C bus** — not the motherboard SMBus. This is important:
    - It needs **no administrator rights**. The "Elevate OpenRGB" button is not necessary for the
      GPU any more.
    - It does **not** touch the motherboard SMBus or your DDR5 RAM. The app has no code for that
      bus, so there is no risk to the memory.
  The GPU shows a per-LED mirror (the MSI logo and the fan strips) at about 30 frames each second,
  smooth. Test it with `--gpuinfo` (a safe read-only check), `--gpu #rrggbb`, or `--gpuspin`.
  This works on the MSI RTX 5070 Ti Gaming Trio. Other cards continue to use OpenRGB.
- **Safety.** All GPU commands go through one function that permits only the RGB controller's
  address and only four known commands. The app never searches the bus, and never uses the monitor
  (DDC) path. This keeps the voltage controller and the monitor memory out of reach.
- **The Kraken ring pattern grows correctly now.** Before, the app put the WLED strip around the
  ring in a line. A ring is a circle, not a line, so each lit part of the strip had only one LED to
  grow across: the ring showed a thin pattern that did not seem to move. Now the pattern **grows
  out from a number of points** ("origins"), at equal distances around the circle. Each origin
  grows outward and meets its neighbour half way. With the default of 2 origins, each one has 6
  LEDs to grow across, so the movement is easy to see. Set the number in **Advanced → Ring
  origins** (1 to 6). The app keeps your choice. The limit is 6, because each origin must keep a
  minimum of 2 LEDs in each direction: with more origins, each one has too few LEDs, and you get
  the same thin pattern again.

## v1.6

- **The Kraken Elite ring updates live again — with the correct protocol.** Version 1.5 was
  wrong: the ring *can* update live and per-LED. The earlier versions used the wrong USB
  commands (the HUE2 protocol — opcode `0x22`, 64-byte reports, with an "apply" each frame),
  which this model does not stream well, so the ring lagged and the per-LED pattern collapsed.
  The app now uses the exact commands that SignalRGB uses for this model (opcode `0x26`,
  512-byte reports, one write for each frame, no "apply"), at about 30 frames each second.
  The ring now shows a smooth, per-LED gradient that moves and flashes with WLED. The Kraken
  is its own USB pipeline, separate from OpenRGB. Test it with `--kraken #rrggbb` (solid),
  `--krakencycle` (a color sweep), or `--krakenspin` (a moving rainbow — the per-LED test).
  You must close other Kraken software (NZXT CAM, SignalRGB) first, or they fight the ring.
- **The GPU RGB is smooth now.** Before, the GPU updated on every frame (up to 60 each
  second). GPU RGB goes through OpenRGB over the slow SMBus, so the writes made a queue and
  the GPU lagged and was choppy. The app now updates the slow devices (the GPU and coolers)
  at a set rate (the GPU about 30 times each second), and it does not send the same colors
  again. This makes the GPU smooth, for every GPU that OpenRGB supports. (GPU RGB still shows
  light only when you run OpenRGB as administrator — the SMBus needs it. This does not change.)

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

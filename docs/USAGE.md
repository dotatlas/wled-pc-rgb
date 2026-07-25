# wled-pc-rgb — Usage guide

This app shows your WLED lights on your PC RGB devices (fans, GPU, the NZXT Kraken ring,
the mouse, and more) in real time. When WLED shows an effect, a color, or a **LedFx
audio-reactive** pattern, the PC shows the same colors.

> WLED is the master. The PC follows. The app does not change the brightness of WLED. The
> app does not write to your RAM or SMBus, so there is no risk to the memory.

---

## 1. What you need

The app needs two programs. It does not include them. It starts them for you.

| You need | Reason | Where to get it |
|---|---|---|
| **A JDK, version 21 or higher** | It runs the small WLED helper (`WledBackend.java`). | [Adoptium Temurin](https://adoptium.net), or `scoop install temurin21-jdk` |
| **OpenRGB** | The app controls your devices through OpenRGB (its SDK on TCP 6742). | [openrgb.org](https://openrgb.org) — install it, for example to `C:\.software\OpenRGB` |

Install the two programs and run the app. The app starts both for you. The three status
dots at the top show when each part is ready.

## 2. Install

- **Portable:** unzip `wled-pc-rgb-vX.Y-win64.zip` to any folder. Run `wled_pc_rgb.exe`.
  The folder has Qt and all the DLLs in it. There is no PATH setup.
- **Installer** (if you build one): run `wled-pc-rgb-setup-vX.Y.exe`. Select a folder. If
  you want, select *Launch at login*.

## 3. First run

1. Look at the status dots: `OpenRGB` · `Backend` · `WLED`. Each dot becomes **green** when
   the part is ready. (Grey = not started. Amber = busy. Red = a problem.) Point at a dot to
   see the details.
2. In the **WLED host** field, type your controller address (`wled.local`, or an IP such as
   `192.168.1.50`). Click **Apply**. The WLED dot becomes green and shows the name.
3. The **Devices to mirror** list fills with your devices. The app selects every possible
   device. Clear the ones that you do not want. The app keeps your choice.
4. Click the **▶ Mirror WLED** button. The **Live** swatches show the WLED color and the PC
   color. The PC now follows WLED in real time.

Close the window. The app continues in the tray. Right-click the tray icon for Show,
Mirror, and Quit.

## 4. Controls and use cases

- **Mirror a WLED color or effect** — turn Mirror on. The PC shows the average color of the
  strip.
- **Audio-reactive with LedFx** — point LedFx at your WLED as normal. The app reads the
  output of WLED, so the PC reacts too. For a lower delay, send the data from LedFx directly
  to this PC. Use a **DDP** device on **UDP 4048**, or an **E1.31/sACN** device on **UDP
  5568** (unicast to this PC, or multicast universe 1). The app uses the source that sends
  data, or the WLED live-view if no source sends data. The app selects the source
  automatically. The WLED dot shows the active source (live, ddp, or sacn).
- **Position modes** (Advanced) — the app splits the strip into small parts, so gradients and
  chases show by position. There are two modes:
    - **Spread (whole strip per device)** — each device shows the full strip across its own
      LEDs.
    - **Wrap (strip across all devices)** — the app puts the strip one time across all the
      selected devices in sequence. The color moves from one device to the next (for example
      GPU → fans → mouse). The Kraken ring runs its own pipeline, so it always shows the full
      strip across its 24 LEDs (a moving, per-LED gradient).
  The two modes are exclusive. Turn both off for one average color on all devices.
- **Ring origins** (Advanced) — for the NZXT Kraken ring only. The ring is a circle, not a line, so
  the app grows the pattern out from a number of points at equal distances around the circle. Each
  point grows outward and meets its neighbour half way. The default is **2** (two opposite points),
  which gives each one 6 of the ring's 24 LEDs to grow across. Use **1** for one large bloom across
  12 LEDs, or a larger number for more, smaller, faster blooms. The maximum is **6**: each origin
  must keep a minimum of 2 LEDs in each direction, or the pattern becomes thin again.
- **Reactive only** (Advanced) — for a music-reactive source that also shows an always-on background
  color. LedFx is the usual example: you set a background color, and the reactive color grows over it.
  With this control on, the app subtracts the background, so only the reactive part lights the PC.
  The PC then goes dark between the beats.
  **How to set it (this is a calibration):**
    1. **Stop the music.** The strip must show the background color only.
    2. Tick **Reactive only**. The app asks you to confirm, then it measures for about one second and
       stores what it saw. The swatch and the hex code next to the box show the stored color.
    3. Start the music. Only the reactive part now shows on the PC.
  The app measures for a second, and not one single frame, because one frame includes the noise of
  that moment. It keeps the highest value that it saw. This is on purpose: a value that is a little
  too high costs almost nothing, but a value that is a little too low leaves a dim background.
  If you change the background color later, click **Set from now** to store the new color. The app
  keeps the stored color, so it is still correct after you close the app. The window title shows the
  stored color while the control is on.
  **If you still see a dim background**, make sure that **Match strip gamma** is on (see below). Your
  strip does not show very low values at all, but the PC can show them, so a small remainder can be
  visible on the PC and not on the strip.
  You calibrate, and the app does not measure by itself, for two reasons. You know when the strip
  shows the background only, so the stored color is exact. And LedFx multiplies the color by three
  brightness values before it sends the data, so the color on the wire is not the color that you
  selected in LedFx — you cannot type it in.
  Two limits:
    - If your source is at full brightness, the background and the reactive color mix together before
      the app receives them (the data cannot go higher than 255). The bright peaks then show a little
      darker than on the strip. To correct this, lower the brightness in LedFx, or raise the
      **Brightness** slider.
    - If the strip shows only the background color, the PC stays dark. There is no reactive part to
      show. This is correct behavior, not a fault.
  **For an exact result with no calibration:** in LedFx, make a second device (type DDP, the IP of
  this PC, port 4048, 64 pixels). Give its virtual the same effect, then set its **Background
  Brightness to 0**. The app selects this source automatically, and it has no background to remove.
  Your WLED strip does not change. Windows Firewall must permit UDP 4048.
- **Match strip gamma** (Advanced, on by default) — WLED sends the colors to the strip through a gamma
  curve (2.2). The app now uses the same curve for the PC. This is necessary for two reasons:
    - Without it, the PC is much brighter than the strip at low levels. The same color can be 5 times
      brighter on the PC than on the strip.
    - The strip shows all values from 1 to 14 as black. The PC can show them. So a very dark color, or
      a small remainder from *Reactive only*, is visible on the PC but not on the strip.
  With this control on, the PC matches the strip. The whole mirror becomes darker, so you can raise
  the **Brightness** slider. Turn it off if you prefer the brighter, less accurate look.
- **Brightness** — one slider for the whole mirror. It makes the PC colors darker or
  brighter. 100% shows the WLED colors as they are. It changes the PC only; it does not
  change the brightness of WLED. A black frame is off (0 stays 0).
- **Start with Windows** — in Options, select *Launch at login* and *Start minimised to
  tray*. This makes a background mirror. *Auto-mirror on launch* starts the mirror when the
  app is ready.
- **More than one WLED, or a new IP** — type the new address in the WLED host field and click
  **Apply**. The helper changes to the new address immediately. You do not restart it.

## 5. Device notes

- **Hidden devices** — a device that the app drives directly (for example the Kraken ring)
  is hidden from the device list, because it does not use OpenRGB. In Advanced, the "Hidden
  (driven directly)" dropdown shows these, and "Re-add to scan" puts one back so OpenRGB
  drives it instead. This is for the session only; it resets when you close the app.
- **NZXT Kraken Elite (ring)** — the app drives the ring **directly over USB**, with its own
  pipeline (not OpenRGB). It uses the same commands as SignalRGB for this model (a 512-byte
  report for each frame), so the ring shows a smooth, per-LED gradient that moves and flashes
  with WLED, at about 30 frames each second. Because the app drives the ring, the app **hides
  the Kraken** from the OpenRGB device list (OpenRGB must not fight the app's USB writes). Only
  one program can control the ring at a time, so **close NZXT CAM and SignalRGB** first — if
  they run, they fight the app for the ring. (The app does not control the Kraken LCD screen;
  CAM controls that.) To hand the ring to OpenRGB instead, re-add the Kraken in Advanced.
- **GPU** — the app drives a **supported** GPU itself, over **NVAPI** on the GPU's own I2C bus. It
  shows a per-LED mirror (the logo and the fan strips) at about 30 frames each second. This needs
  **no administrator rights**, and it does **not** use the motherboard SMBus, so there is no risk to
  your RAM. The device list marks the row "driven directly over NVAPI". Untick the row to release
  the GPU (for example, to give it back to SignalRGB or MSI Center).
  At this time the app knows the **MSI RTX 5070 Ti Gaming Trio**. For any other card, the app uses
  OpenRGB, and then the RGB shows light only if you run **OpenRGB as administrator** (the row says
  so). The "Elevate OpenRGB to admin" button in Setup does this for you.
  Check what your card can do with `wled_pc_rgb.exe --gpuinfo`. It only reads; it changes nothing.
  The report goes to `%TEMP%\wled-pc-rgb-gpu.txt`.
- **MSI Mystic Light / ARGB fans** — a motherboard ARGB zone can show 0 LEDs after an OpenRGB
  restart. Each JARGB header is one zone. In Advanced, set the **zone size** (the number of
  LEDs for each header — the default is **8**, for example an 8-LED JARGB fan). Then click
  **Size zones**.
- **Mouse** and other USB devices — the app drives them directly. They do not need
  administrator rights.

### Smoothness and delay

The PC follows the live output of WLED at its real frame rate. There is no fixed limit. The
internal links use TCP_NODELAY for a low delay. If the light is still not smooth, the limit
is the source: the WLED live-view WebSocket sends fewer frames than a direct feed. For the
smoothest result and the lowest delay, send the data from LedFx directly to this PC with
**DDP (4048)** or **E1.31/sACN (5568)**. These carry the full frame rate.

## 6. Problems and fixes

| Problem | Fix |
|---|---|
| **OpenRGB dot is red** | OpenRGB is not found. Install it, or start it: `OpenRGB.exe --server`. The app tries again automatically. |
| **Only some devices show** (for example, no Kraken or mouse) | USB devices appear a few seconds after OpenRGB starts. The app scans again at about 6 s and 13 s. You can also click Advanced → **Rescan**. |
| **Backend dot is red** | Java is not found. Install a JDK (21+) or set `JAVA_HOME`. The app waits and tries again. |
| **WLED dot is red ("unreachable")** | The address is wrong. Type the IP directly (`192.168.x.x`) in the WLED host field. Click **Apply**. |
| **WLED dot is amber** | WLED is **off**, so the PC goes off too. (Or it shows your idle color, if *When off, show color* is on.) Turn WLED on. |
| **The PC RGB stays white or lit when WLED is black** | Version 1.1 fixed this (black is now off). To show a color when off, select *When off, show color*. |
| **The fans, GPU, or Kraken flicker or fight** | Close other RGB programs (NZXT CAM, SignalRGB, iCUE, Synapse). They take the same devices. |
| **The Kraken ring stays dark, or fights another program** | The app drives the ring directly over USB. Close **NZXT CAM** and **SignalRGB** — if they run, they take the ring. Then start Mirror again. Test the ring on its own with `wled_pc_rgb.exe --krakenspin`. |

## 7. Command-line options

The app also runs some one-shot commands. Each command exits immediately, before the window
opens.

```
wled_pc_rgb.exe --set <deviceIndex> <#rrggbb> [brightness%]   # set one device
wled_pc_rgb.exe --setmode <deviceIndex> <modeIndex>           # change the mode of a device
wled_pc_rgb.exe --setall <#rrggbb> [brightness%]              # set every device
wled_pc_rgb.exe --maxzones                                    # set resizable zones to the default
wled_pc_rgb.exe --mirror <seconds> [spread|wrap]              # run the mirror for N seconds
wled_pc_rgb.exe --kraken <#rrggbb>                            # set the Kraken ring to one color
wled_pc_rgb.exe --krakencycle [seconds]                       # sweep the ring through the colors
wled_pc_rgb.exe --krakenspin [seconds]                        # a moving rainbow (the per-LED test)
wled_pc_rgb.exe --gpuinfo                                     # GPU check — READ ONLY, changes nothing
wled_pc_rgb.exe --gpu <#rrggbb>                               # set the GPU to one color
wled_pc_rgb.exe --gpucycle [seconds]                          # sweep the GPU through the colors
wled_pc_rgb.exe --gpuspin [seconds]                           # a moving rainbow on the GPU (per-LED test)
wled_pc_rgb.exe --minimized                                   # start in the tray
```

The GPU commands write their report to `%TEMP%\wled-pc-rgb-gpu.txt`.

The app writes a device scan file to `%TEMP%\wled-pc-rgb-scan.txt` at each scan.

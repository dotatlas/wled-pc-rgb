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
      GPU → fans → Kraken ring → mouse). The Kraken ring shows the average of its part.
  The two modes are exclusive. Turn both off for one average color on all devices.
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
- **NZXT Kraken 2024 Elite ring** — the app drives this ring **directly over USB**, not
  through OpenRGB (OpenRGB drives it too slowly, so the ring lagged by seconds). So the ring
  follows WLED in real time. It sends only lighting commands (the same ones OpenRGB and NZXT
  CAM use). NZXT CAM can keep control of the **LCD screen** at the same time (the screen and
  the ring are separate). Test the ring with `wled_pc_rgb.exe --kraken #rrggbb`.
- **Other NZXT Kraken models** — the app sets the ring to Static mode through OpenRGB so it
  follows WLED. Its default *Direct* mode does not light the ring.
- **GPU (RTX)** — OpenRGB finds it, but the RGB shows light only when you run **OpenRGB as
  administrator**. By default the app runs OpenRGB without administrator rights. (An elevated
  OpenRGB reads the motherboard SMBus/DDR5, which is a risk to the RAM.) The device list
  marks the GPU row. To light the GPU, start OpenRGB as administrator before you start the
  app. This is your choice.
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
| **The Kraken ring stays dark** | Make sure it is in **Static** mode. The app sets this automatically when it mirrors. Or double-click its Static mode row. |

## 7. Command-line options

The app also runs some one-shot commands. Each command exits immediately, before the window
opens.

```
wled_pc_rgb.exe --set <deviceIndex> <#rrggbb> [brightness%]   # set one device
wled_pc_rgb.exe --setmode <deviceIndex> <modeIndex>           # change the mode of a device
wled_pc_rgb.exe --setall <#rrggbb> [brightness%]              # set every device
wled_pc_rgb.exe --maxzones                                    # set resizable zones to the default
wled_pc_rgb.exe --mirror <seconds> [spread|wrap]              # run the mirror for N seconds
wled_pc_rgb.exe --minimized                                   # start in the tray
```

The app writes a device scan file to `%TEMP%\wled-pc-rgb-scan.txt` at each scan.

# wled-pc-rgb — Usage guide

**What it does:** makes this PC's RGB (fans, GPU, NZXT Kraken ring, mouse, …) a live
mirror of a WLED instance. Whatever WLED shows — a WLED effect, a colour you set, or an
external takeover like **LedFx making it audio-reactive** — is reproduced on the PC.

> WLED is the master. The PC follows. The app never changes WLED's own brightness, and
> it never touches your RAM/SMBus (zero DIMM-brick risk).

---

## 1. Prerequisites

The app orchestrates two things it does **not** bundle:

| Need | Why | Get it |
|---|---|---|
| **A JDK (21+)** | runs the bundled `WledBackend.java` (the WLED microservice) | [Adoptium Temurin](https://adoptium.net) — or `scoop install temurin21-jdk` |
| **OpenRGB** | the app drives your hardware through its SDK server on TCP 6742 | [openrgb.org](https://openrgb.org) — install to e.g. `C:\.software\OpenRGB` |

The app auto-launches both when it starts, so you normally just install them and run
the app. The three **setup dots** at the top show whether each piece is ready.

## 2. Install

- **Portable:** unzip `wled-pc-rgb-vX.Y-win64.zip` anywhere and run `wled_pc_rgb.exe`.
  It is fully self-contained (Qt + all DLLs bundled); no PATH setup needed.
- **Installer** (if built): run `wled-pc-rgb-setup-vX.Y.exe`, choose an install folder,
  optionally tick *Launch at login*.

## 3. First run

1. Watch the **setup strip**: `OpenRGB` · `Backend` · `WLED` each turn
   **green** when ready (grey = not started, amber = working, red = problem). Hover a
   dot for detail.
2. In **WLED host**, enter your controller (`wled.local`, or its IP like
   `192.168.1.50`) and click **Apply**. The WLED dot goes green and its name appears.
3. The **Devices to mirror** list fills in. Every eligible device is ticked by default —
   untick any you don't want driven. Your selection is remembered.
4. Click the big **▶ Mirror WLED** button. The **Live** swatches show the WLED colour
   and the brightness-scaled PC colour. Your PC now follows WLED in real time.

Close the window and the app keeps running in the **tray** (right-click → Show / Mirror
/ Quit).

## 4. Use cases

- **Mirror a WLED colour or effect** — just turn Mirror on. The PC follows the strip's
  average colour.
- **Audio-reactive via LedFx** — point LedFx at your WLED as usual; the app reads WLED's
  live output back, so the PC reacts too. For **lower latency**, stream from LedFx straight
  at *this PC* — either a **DDP** device on **UDP 4048** or an **E1.31/sACN** device on
  **UDP 5568** (unicast to this PC, or multicast universe 1). The app uses whichever tap is
  streaming and falls back to the WLED live-view otherwise. No setting to flip; it
  auto-selects, and the WLED dot shows the active source (live / ddp / sacn).
- **Spread across LEDs** (Advanced → *Spread across LEDs*) — instead of one average
  colour, WLED's strip is split into 16 buckets and stretched across each device's LEDs,
  so gradients/chases show positionally.
- **PC brightness** — the slider is a **PC-only** scaler. It dims the mirrored colour on
  your hardware and never changes WLED's brightness.
- **Flash gain** (1.0×–5.0×) — a multiplier for when WLED's flashes are too dim on the PC.
  It amplifies the mirrored colour (clamped at full brightness), so a faint spike on the
  strip reads as a punchy flash on your fans/ring. 1.0× = no change.
- **Min brightness** (0–100%) — a floor the PC light never drops below. WLED spikes still
  flash *above* it, but between flashes the PC keeps a baseline glow instead of going all
  the way off. Colours keep their hue (a dim red is lifted to a brighter red); a fully
  black frame lifts to a dim neutral glow. 0% = follow WLED exactly (can go dark).

  The three sliders compose: **brightness** sets the overall level, **gain** amplifies the
  flashes, **min brightness** guarantees a floor. All three are remembered between runs.
- **Start with Windows** — Options → *Launch at login* and *Start minimised to tray* for
  a set-and-forget background mirror. *Auto-mirror on launch* starts mirroring
  automatically once everything is ready.
- **Multiple WLEDs / changed IP** — just edit the WLED host field and Apply; the running
  backend retargets live, no restart.

## 5. Device notes

- **NZXT Kraken ring** — only lights in a single-colour mode. The app **auto-switches it
  to Static** when mirroring so the ring follows WLED. (Its default *Direct* mode won't
  light the ring.)
- **GPU (RTX)** — detected, but its RGB only physically lights when **OpenRGB runs as
  administrator**. The app keeps OpenRGB non-elevated on purpose (elevated OpenRGB probes
  the motherboard SMBus/DDR5, which risks the RAM). The GPU row is annotated accordingly.
  If you want GPU lighting, start OpenRGB as admin yourself before launching the app —
  accepting that trade-off.
- **MSI Mystic Light / ARGB fans** — motherboard ARGB zones can read as 0 LEDs after an
  OpenRGB restart. Advanced → **Size zones (24)** resizes them so the fans light.
- **Mouse** and other USB-HID devices — driven directly, no elevation needed.

## 6. Troubleshooting

| Symptom | Fix |
|---|---|
| **OpenRGB dot red** | OpenRGB not found/– install it, or start it manually: `OpenRGB.exe --server`. The app retries automatically. |
| **Only some devices listed** (e.g. Kraken/mouse missing) | USB-HID devices enumerate a few seconds after OpenRGB starts. The app re-scans automatically at ~6s and ~13s; you can also hit Advanced → **Rescan**. |
| **Backend dot red** | Java isn't found. Install a JDK (21+) or set `JAVA_HOME`. The app backs off and retries. |
| **WLED dot red / “unreachable”** | Wrong host. Enter the IP directly (`192.168.x.x`) in the WLED host field and Apply. |
| **WLED dot amber, mirror paused** | WLED is **off** — the app holds the PC RGB rather than blacking it out. Turn WLED on. |
| **Fans/GPU/Kraken flicker or fight** | Close other RGB software (NZXT CAM, SignalRGB, iCUE, Synapse) — they grab the same devices. |
| **Kraken ring stays dark** | Make sure it's in **Static** (the app sets this automatically while mirroring; or double-click its Static mode row). |

## 7. Headless / scripting

The exe also runs a few one-shot commands (they exit immediately, before the GUI):

```
wled_pc_rgb.exe --set <deviceIndex> <#rrggbb> [brightness%]   # set one device
wled_pc_rgb.exe --setmode <deviceIndex> <modeIndex>           # switch a device's mode
wled_pc_rgb.exe --setall <#rrggbb> [brightness%]              # set every device
wled_pc_rgb.exe --maxzones                                    # size resizable zones to 24
wled_pc_rgb.exe --mirror <seconds> [spread]                   # run the mirror for N seconds
wled_pc_rgb.exe --minimized                                   # launch straight to the tray
```

A device scan dump is written to `%TEMP%\wled-pc-rgb-scan.txt` on every scan.

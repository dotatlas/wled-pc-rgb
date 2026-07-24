# Roadmap

This is where **wled-pc-rgb** is now, and where it can go. For the list of released
versions, read the [CHANGELOG](../CHANGELOG.md).

## Done — the mirror is complete

- ✅ Show the live output of WLED on the PC RGB devices in real time (through OpenRGB).
- ✅ Follow WLED effects, colors, and **LedFx** audio-reactive patterns.
- ✅ More than one live source, selected automatically: the WLED live-view, **DDP** (UDP
  4048), and **E1.31/sACN** (UDP 5568) directly to the PC.
- ✅ A switch for each device.
- ✅ Color controls: PC brightness, flash gain, minimum brightness, and an idle color.
- ✅ Position modes: *Spread* (the full strip on each device) and *Wrap* (the strip across
  all devices).
- ✅ A zone size that you can set.
- ✅ A device blacklist: hide a device from the scan so OpenRGB does not touch it (the NZXT
  Kraken is hidden by default). Advanced has a dropdown of hidden devices and a "Re-add to
  scan" button. Session only.
- ✅ The app starts OpenRGB and the WLED helper, stays in the tray, can start at login or
  start small, and keeps the settings.
- ✅ A portable, one-folder Windows release.

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
- **Driving the NZXT Kraken ring or LCD.** The ring cannot update live over USB — the device
  throttles streamed color, so it lags badly. Smooth motion on the ring only comes from the
  device's own effects, which cannot mirror WLED. So the app leaves the Kraken to NZXT CAM and
  hides it from the scan. (The reverse-engineered protocol is kept in the project notes.)

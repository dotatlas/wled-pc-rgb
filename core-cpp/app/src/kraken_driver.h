// kraken_driver — direct HID control of the NZXT Kraken Elite V2 (2024) RGB ring.
//
// Protocol taken from SignalRGB's own device plugin (NZXT_Kraken_Elite_V2_AIO.js), which
// drives this exact device (VID 0x1E71 / PID 0x3012) fluidly. The ring is 24 LEDs, written
// as ONE 512-byte HID report per frame: [0x26,0x14,0x01,0x01] + 24 * GRB, no separate apply.
// (OpenRGB's HUE2 path — opcode 0x22, 64-byte reports, + an Apply each frame — is the WRONG
// protocol for this model and is what lagged / collapsed per-LED.)
//
// Lighting only — never pump/fan/firmware — so it cannot damage the cooler.
#pragma once
#include <QColor>
#include <QList>

struct hid_device_;   // hidapi opaque handle (fwd)

class KrakenDriver {
public:
    ~KrakenDriver();
    bool open();                                 // find + open the Kraken Elite V2; true if present
    bool isOpen() const { return dev_ != nullptr; }
    void setRing(const QList<QColor>& leds);     // per-LED ring colours (bloomed from the origins, GRB)
    void setRingColor(const QColor& c);          // solid ring
    void close();

    // How many points the pattern blooms outward from, spaced symmetrically around the circle.
    // A ring is not a strip: laying the WLED strip around it linearly gives each lit pixel only
    // one or two LEDs to grow across, so nothing appears to move. With N origins each one owns a
    // wedge and the pattern spreads out from its centre, so growth is actually visible. Default 2.
    void setOrigins(int n);
    int  origins() const { return origins_; }
    static int ringLeds();                       // ring LED count (the wire format's slot count)
    static int maxOrigins();                     // ceiling that keeps the bloom from degenerating

    // Diagnostic (CLI only): light exactly one ring slot, so the physical LED count can be counted.
    void lightOneSlot(int slot, const QColor& c);

private:
    hid_device_*  dev_ = nullptr;
    int           origins_ = 2;
    QList<QColor> last_;                          // skip-unchanged
};

// kraken_driver — direct HID control of the NZXT Kraken Elite RGB ring.
//
// OpenRGB drives this device correctly but very slowly: its SDK server polls sensors and
// serialises writes, so a streamed mirror colour lags the ring by seconds/minutes. This
// class talks to the device's HUE2 "Direct" streaming path over HID directly, with no
// server in the way, so the ring follows WLED live.
//
// Kraken-Elite-specific: it only opens VID 0x1E71 / PID 0x3012 (NZXT Kraken 2024 Elite),
// and it sends ONLY lighting reports (the same 0x22 opcodes OpenRGB and NZXT CAM use) —
// never pump, fan, or firmware commands. So it cannot damage the cooler.
#pragma once
#include <QColor>
#include <QList>
#include "device_pipeline.h"

struct hid_device_;   // hidapi opaque handle (fwd)

class KrakenDriver : public DevicePipeline {
public:
    ~KrakenDriver() override;
    QString name()  const override { return "NZXT Kraken Elite ring"; }
    QString match() const override { return "kraken"; }   // OpenRGB names it "NZXT Kraken 2024 ELITE Series RGB"
    bool open() override;                        // find + open the Kraken Elite; true if present
    bool isOpen() const override { return dev_ != nullptr; }
    void apply(const QColor& c) override { setRingColor(c); }
    void apply(const QList<QColor>& strip) override;   // resample the strip onto the ring's LEDs
    void close() override;

    void setRingColor(const QColor& c);          // solid ring colour
    void setRing(const QList<QColor>& leds);     // per-LED ring colours (GRB streamed)

private:
    bool sendDirect(unsigned char channel, unsigned char group, unsigned char count, const unsigned char* grb);
    bool sendApply(unsigned char channel);
    void stream(const QList<QColor>& leds);

    hid_device_*   dev_ = nullptr;
    QList<QColor>  last_;                         // skip-unchanged (avoid needless USB traffic)
};

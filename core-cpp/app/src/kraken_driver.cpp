#include "kraken_driver.h"
#include <hidapi/hidapi.h>
#include <cstring>

namespace {
constexpr unsigned short kVid       = 0x1E71;   // NZXT
constexpr unsigned short kPid       = 0x3012;   // Kraken Elite V2 (2024). 0x3014 is the sibling.
constexpr unsigned short kPidAlt    = 0x3014;
constexpr int            kRingLeds  = 24;       // physical LEDs on the pump ring
constexpr int            kReportLen = 512;      // this model uses 512-byte HID reports (not 64)
}

KrakenDriver::~KrakenDriver() { close(); }

bool KrakenDriver::open() {
    if (dev_) return true;
    if (hid_init() != 0) return false;

    // Prefer interface 1 (MI_01) — the RGB/control interface SignalRGB writes to.
    hid_device_info* devs = nullptr;
    for (unsigned short pid : { kPid, kPidAlt }) {
        devs = hid_enumerate(kVid, pid);
        if (devs) { break; }
    }
    if (!devs) return false;

    char chosen[1024] = {0};
    bool haveChosen = false;
    for (hid_device_info* d = devs; d; d = d->next) {
        if (!d->path) continue;
        if (d->interface_number == 1) {                 // exact match — take it
            std::strncpy(chosen, d->path, sizeof(chosen) - 1);
            haveChosen = true;
            break;
        }
        if (!haveChosen) {                               // fallback: first path
            std::strncpy(chosen, d->path, sizeof(chosen) - 1);
            haveChosen = true;
        }
    }
    hid_free_enumeration(devs);

    if (haveChosen) dev_ = hid_open_path(chosen);
    last_.clear();
    return dev_ != nullptr;
}

void KrakenDriver::close() {
    if (dev_) { hid_close(dev_); dev_ = nullptr; }
    last_.clear();
}

void KrakenDriver::setRing(const QList<QColor>& leds) {
    if (!dev_ || leds.isEmpty()) return;

    // Resample whatever we're given to exactly 24 LEDs (segment-average), so a moving
    // gradient wraps the ring per-LED instead of collapsing to one colour.
    QList<QColor> ring;
    ring.reserve(kRingLeds);
    const int n = leds.size();
    for (int i = 0; i < kRingLeds; ++i) {
        int lo = static_cast<int>(static_cast<long long>(i)     * n / kRingLeds);
        int hi = static_cast<int>(static_cast<long long>(i + 1) * n / kRingLeds);
        if (hi <= lo) hi = lo + 1;
        if (hi > n)   hi = n;
        long r = 0, g = 0, b = 0; int cnt = 0;
        for (int k = lo; k < hi; ++k) { r += leds[k].red(); g += leds[k].green(); b += leds[k].blue(); ++cnt; }
        if (!cnt) cnt = 1;
        ring.push_back(QColor(int(r / cnt), int(g / cnt), int(b / cnt)));
    }

    if (ring == last_) return;   // skip-unchanged: never re-send an identical frame

    // One 512-byte report per frame — SignalRGB's exact wire format:
    //   [0x26, 0x14, 0x01, 0x01] + 24 * (G, R, B), zero-padded. No separate "apply".
    unsigned char buf[kReportLen];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 0x26; buf[1] = 0x14; buf[2] = 0x01; buf[3] = 0x01;
    for (int i = 0; i < kRingLeds; ++i) {
        const QColor& c = ring[i];
        buf[4 + i * 3 + 0] = static_cast<unsigned char>(c.green());
        buf[4 + i * 3 + 1] = static_cast<unsigned char>(c.red());
        buf[4 + i * 3 + 2] = static_cast<unsigned char>(c.blue());
    }
    if (hid_write(dev_, buf, kReportLen) < 0) {
        close();       // handle died (unplug / sleep-resume) — drop it so the caller can reopen
        return;
    }
    last_ = ring;      // record only after a successful write, so a dropped frame is retried
}

void KrakenDriver::setRingColor(const QColor& c) {
    QList<QColor> one; one.push_back(c);
    setRing(one);
}

#include "kraken_driver.h"
#include <hidapi/hidapi.h>
#include <cstring>
#include <cmath>
#include <utility>

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

int  KrakenDriver::ringLeds()   { return kRingLeds; }
// Each origin must keep at least 2 LEDs of travel in each direction, or the bloom degenerates: at
// kRingLeds/2 origins every LED is either an origin or a midpoint, which is the sparse alternating
// look this mapping exists to remove. kRingLeds/4 = 6 is therefore the ceiling.
int  KrakenDriver::maxOrigins() { return kRingLeds / 4; }
void KrakenDriver::setOrigins(int n) {
    const int hi = maxOrigins();
    origins_ = (n < 1) ? 1 : (n > hi ? hi : n);
}

namespace {
// Average the strip over the normalised span [a,b] of its length. Averaging (not point-sampling)
// keeps a narrow pulse from being missed between two ring LEDs.
QColor stripSpan(const QList<QColor>& leds, double a, double b) {
    const int n = leds.size();
    if (n <= 0) return QColor(0, 0, 0);
    if (a > b) std::swap(a, b);
    int lo = int(a * (n - 1));
    int hi = int(b * (n - 1) + 0.9999);
    if (lo < 0) lo = 0;
    if (hi >= n) hi = n - 1;
    if (hi < lo) hi = lo;
    long r = 0, g = 0, bl = 0; int cnt = 0;
    for (int k = lo; k <= hi; ++k) { r += leds[k].red(); g += leds[k].green(); bl += leds[k].blue(); ++cnt; }
    if (!cnt) return QColor(0, 0, 0);
    return QColor(int(r / cnt), int(g / cnt), int(bl / cnt));
}
}

void KrakenDriver::setRing(const QList<QColor>& leds) {
    if (!dev_ || leds.isEmpty()) return;

    // Map the (linear) WLED strip onto the (circular) ring as a BLOOM from `origins_` points,
    // spaced symmetrically. For each ring LED we take its circular distance to the nearest origin
    // and use that as the position along the strip: the strip's start sits at every origin, and its
    // end sits at the midpoints between them. So a pulse grows outward from each origin and meets
    // its neighbours halfway — symmetric, and each origin gets kRingLeds/(2*origins) LEDs to grow
    // across instead of one. (A straight linear wrap is what made the pattern look static.)
    const int N = (origins_ < 1) ? 1 : origins_;
    const double half = double(kRingLeds) / (2.0 * N);      // LEDs per origin, one direction
    QList<QColor> ring;
    ring.reserve(kRingLeds);
    for (int i = 0; i < kRingLeds; ++i) {
        double best = double(kRingLeds);
        for (int k = 0; k < N; ++k) {
            const double o = double(k) * kRingLeds / N;
            double d = std::fabs(double(i) - o);
            if (double(kRingLeds) - d < d) d = double(kRingLeds) - d;   // wrap around the circle
            if (d < best) best = d;
        }
        // This LED covers the strip band [best/half, (best+1)/half] — average it.
        const double a = (half > 0.0) ? best / half : 0.0;
        const double b = (half > 0.0) ? (best + 1.0) / half : 1.0;
        ring.push_back(stripSpan(leds, a > 1.0 ? 1.0 : a, b > 1.0 ? 1.0 : b));
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

// Diagnostic only: drive exactly one ring slot, everything else off. Used by --krakenwalk to count
// the ring's real physical LEDs (the wire format has 24 slots; the hardware may light fewer).
void KrakenDriver::lightOneSlot(int slot, const QColor& c) {
    if (!dev_) return;
    unsigned char buf[kReportLen];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 0x26; buf[1] = 0x14; buf[2] = 0x01; buf[3] = 0x01;
    if (slot >= 0 && slot < kRingLeds) {
        buf[4 + slot * 3 + 0] = static_cast<unsigned char>(c.green());
        buf[4 + slot * 3 + 1] = static_cast<unsigned char>(c.red());
        buf[4 + slot * 3 + 2] = static_cast<unsigned char>(c.blue());
    }
    if (hid_write(dev_, buf, kReportLen) < 0) close();
    last_.clear();      // this bypasses the normal path, so don't let skip-unchanged cache it
}

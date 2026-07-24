#include "kraken_driver.h"
#include <hidapi/hidapi.h>
#include <QElapsedTimer>
#include <cstring>

// NZXT Kraken 2024 Elite (VID 0x1E71, PID 0x3012). OpenRGB detects its ring as a HUE2
// device with 2 RGB channels; channel 0 is the pump ring. We drive both channels with the
// same colours so the ring lights regardless of which channel it is (a spare channel is a
// harmless no-op). The ring has 24 LEDs; the HUE2 Direct protocol carries up to 40.
namespace {
constexpr unsigned short kVid = 0x1E71;
constexpr unsigned short kPid = 0x3012;   // Kraken 2024 Elite RGB
constexpr int kRingLeds = 24;
// Channel 0 is the pump ring. (Channel 1 is the accessory RGB port; driving it with the
// ring's colours produced a two-tone/alternating artifact on the ring, so we leave it.)
const unsigned char kChannels[] = { 0 };
}

KrakenDriver::~KrakenDriver() { close(); }

bool KrakenDriver::open() {
    if (dev_) return true;
    if (hid_init() != 0) return false;

    // Prefer the vendor-defined collection (usage page 0xFF00); fall back to the first path.
    hid_device_info* devs = hid_enumerate(kVid, kPid);
    const char* best = nullptr;
    static char pathBuf[512];
    for (hid_device_info* d = devs; d; d = d->next) {
        if (!d->path) continue;
        if (!best) { strncpy(pathBuf, d->path, sizeof(pathBuf) - 1); pathBuf[sizeof(pathBuf)-1] = 0; best = pathBuf; }
        if (d->usage_page >= 0xFF00) { strncpy(pathBuf, d->path, sizeof(pathBuf) - 1); pathBuf[sizeof(pathBuf)-1] = 0; best = pathBuf; break; }
    }
    if (best) dev_ = hid_open_path(best);
    if (devs) hid_free_enumeration(devs);
    last_.clear();
    return dev_ != nullptr;
}

void KrakenDriver::close() {
    if (dev_) { hid_close(dev_); dev_ = nullptr; }
    last_.clear();
}

// HUE2 Direct colour packet: 0x22, 0x10|group, (1<<channel), then GRB colour bytes.
bool KrakenDriver::sendDirect(unsigned char channel, unsigned char group, unsigned char count, const unsigned char* grb) {
    unsigned char buf[64];
    std::memset(buf, 0, sizeof(buf));
    buf[0x00] = 0x22;
    buf[0x01] = 0x10 | group;
    buf[0x02] = (unsigned char)(1 << channel);
    buf[0x03] = 0x00;
    std::memcpy(&buf[0x04], grb, size_t(count) * 3);
    return hid_write(dev_, buf, 64) >= 0;
}

// HUE2 Apply/commit packet (mirrors OpenRGB's SendApply byte-for-byte).
bool KrakenDriver::sendApply(unsigned char channel) {
    unsigned char buf[64];
    std::memset(buf, 0, sizeof(buf));
    buf[0x00] = 0x22;
    buf[0x01] = 0xA0;
    buf[0x02] = (unsigned char)(1 << channel);
    buf[0x04] = 0x01;
    buf[0x07] = 0x28;
    buf[0x0A] = 0x80;
    buf[0x0C] = 0x32;
    buf[0x0F] = 0x01;
    return hid_write(dev_, buf, 64) >= 0;
}

void KrakenDriver::stream(const QList<QColor>& leds) { streamRaw(leds, true); }

void KrakenDriver::streamRaw(const QList<QColor>& leds, bool apply) {
    if (!dev_) return;
    int count = leds.size();
    if (count > 40) count = 40;
    if (count <= 0) return;

    unsigned char grb[120];
    std::memset(grb, 0, sizeof(grb));
    for (int i = 0; i < count; ++i) {
        const QColor& c = leds[i];
        grb[i*3 + 0] = (unsigned char)c.green();
        grb[i*3 + 1] = (unsigned char)c.red();
        grb[i*3 + 2] = (unsigned char)c.blue();
    }
    const unsigned char first = (unsigned char)(count > 20 ? 20 : count);
    for (unsigned char ch : kChannels) {
        sendDirect(ch, 0, first, &grb[0]);
        if (count > 20) sendDirect(ch, 1, (unsigned char)(count - 20), &grb[60]);
        if (apply) sendApply(ch);
    }
}

void KrakenDriver::setRing(const QList<QColor>& leds) {
    if (!dev_ || leds.isEmpty()) return;
    if (leds == last_) return;                   // skip unchanged
    last_ = leds;
    stream(leds);
}

void KrakenDriver::setRingColor(const QColor& c) {
    QList<QColor> leds;
    leds.reserve(kRingLeds);
    for (int i = 0; i < kRingLeds; ++i) leds.push_back(c);
    setRing(leds);
}

// Diagnostic: stream a moving pattern as fast as possible and report the achievable ring
// frame rate, both with the Apply commit (as we normally send) and Direct-only (to see if
// the Apply is the bottleneck). Writes are synchronous, so this measures the real ceiling.
QString KrakenDriver::benchmark(int secs) {
    if (!dev_) return "Kraken not open";
    if (secs < 2) secs = 2;
    const qint64 half = qint64(secs) * 1000 / 2;
    auto pattern = [](int f) {
        QList<QColor> r; r.reserve(24);
        for (int j = 0; j < 24; ++j) r.push_back(QColor::fromHsv(((j * 15) + (f * 12)) % 360, 255, 255));
        return r;
    };
    QElapsedTimer t;
    int fa = 0; t.start(); while (t.elapsed() < half) { streamRaw(pattern(fa), true);  ++fa; } const double ea = t.elapsed() / 1000.0;
    int fb = 0; t.restart(); while (t.elapsed() < half) { streamRaw(pattern(fb), false); ++fb; } const double eb = t.elapsed() / 1000.0;
    setRingColor(QColor(0, 0, 0));   // leave it off
    return QString("Direct+Apply: %1 frames / %2 s = %3 FPS (%4 ms/frame)\n"
                   "Direct-only : %5 frames / %6 s = %7 FPS (%8 ms/frame)\n")
        .arg(fa).arg(ea, 0, 'f', 2).arg(fa / (ea > 0 ? ea : 1), 0, 'f', 1).arg(ea * 1000 / (fa > 0 ? fa : 1), 0, 'f', 2)
        .arg(fb).arg(eb, 0, 'f', 2).arg(fb / (eb > 0 ? eb : 1), 0, 'f', 1).arg(eb * 1000 / (fb > 0 ? fb : 1), 0, 'f', 2);
}

// Resample the WLED strip (any length) onto the ring's LEDs, so the ring shows the strip's
// gradient/motion instead of one flat average — it flashes and moves with everything else.
// Each ring LED is the AVERAGE of its segment of the strip (not a single sample), so no
// motion is missed (which looked "static") and a low-res source doesn't alias into a
// checkerboard.
void KrakenDriver::apply(const QList<QColor>& strip) {
    const int n = strip.size();
    if (n <= 0) return;
    QList<QColor> ring;
    ring.reserve(kRingLeds);
    for (int i = 0; i < kRingLeds; ++i) {
        int lo = i * n / kRingLeds;
        int hi = (i + 1) * n / kRingLeds;
        if (hi <= lo) hi = lo + 1;
        if (hi > n) hi = n;
        long r = 0, g = 0, b = 0; int cnt = 0;
        for (int k = lo; k < hi; ++k) { r += strip[k].red(); g += strip[k].green(); b += strip[k].blue(); ++cnt; }
        if (cnt == 0) cnt = 1;
        ring.push_back(QColor(int(r / cnt), int(g / cnt), int(b / cnt)));
    }
    setRing(ring);
}

#include "orgb_client.h"
#include <QTcpSocket>
#include <cstring>

// --- OpenRGB SDK protocol -----------------------------------------------------
// Packet: 16-byte header ("ORGB" + u32 device + u32 command + u32 size) + payload.
// Little-endian. Protocol pinned to v3 for a deterministic layout.
namespace {

enum : quint32 {
    CMD_CONTROLLER_COUNT = 0,
    CMD_CONTROLLER_DATA  = 1,
    CMD_PROTOCOL_VERSION = 40,
    CMD_SET_CLIENT_NAME  = 50,
    CMD_RESIZE_ZONE      = 1000,
    CMD_UPDATE_LEDS      = 1050,
    CMD_UPDATE_MODE      = 1101,
    CMD_SET_CUSTOM_MODE  = 1100,
};

quint16 le16(const char* p){ return quint16(quint8(p[0]) | (quint8(p[1]) << 8)); }
quint32 le32(const char* p){ return quint32(quint8(p[0]) | (quint8(p[1]) << 8) | (quint8(p[2]) << 16) | (quint32(quint8(p[3])) << 24)); }
void    put16(QByteArray& b, quint16 v){ char t[2]{char(v&0xFF),char((v>>8)&0xFF)}; b.append(t,2); }
void    put32(QByteArray& b, quint32 v){ char t[4]{char(v&0xFF),char((v>>8)&0xFF),char((v>>16)&0xFF),char((v>>24)&0xFF)}; b.append(t,4); }

void sendPacket(QTcpSocket& s, quint32 dev, quint32 cmd, const QByteArray& payload = {}) {
    QByteArray p;
    p.append("ORGB", 4);
    put32(p, dev); put32(p, cmd); put32(p, quint32(payload.size()));
    p.append(payload);
    s.write(p);
    s.flush();
}

QByteArray recvExactly(QTcpSocket& s, int n, bool& ok) {
    QByteArray b;
    while (b.size() < n) {
        if (s.bytesAvailable() <= 0 && !s.waitForReadyRead(3000)) { ok = false; return b; }
        b.append(s.read(n - b.size()));
    }
    ok = true;
    return b;
}

QByteArray recvPacket(QTcpSocket& s, quint32& cmd, bool& ok) {
    QByteArray h = recvExactly(s, 16, ok);
    if (!ok) return {};
    if (std::memcmp(h.constData(), "ORGB", 4) != 0) { ok = false; return {}; }
    cmd = le32(h.constData() + 8);
    return recvExactly(s, int(le32(h.constData() + 12)), ok);
}

quint32 handshake(QTcpSocket& s) {
    { QByteArray n("wled-pc-rgb"); n.append('\0'); sendPacket(s, 0, CMD_SET_CLIENT_NAME, n); }
    QByteArray p; put32(p, 3); sendPacket(s, 0, CMD_PROTOCOL_VERSION, p);
    bool ok; quint32 c; QByteArray r = recvPacket(s, c, ok);
    return (ok && r.size() >= 4) ? qMin(quint32(3), le32(r.constData())) : 0;
}

// Force the server to process prior (reply-less) writes before we disconnect.
void syncFlush(QTcpSocket& s) { sendPacket(s, 0, CMD_CONTROLLER_COUNT); bool ok; quint32 c; recvPacket(s, c, ok); }

class Reader {
public:
    explicit Reader(const QByteArray& b) : d(b) {}
    bool ok() const { return !err; }
    int  tell() const { return pos; }
    quint32 u32()  { if (pos+4 > d.size()) { err=true; return 0; } quint32 v=le32(d.constData()+pos); pos+=4; return v; }
    qint32  i32()  { return qint32(u32()); }
    quint16 u16()  { if (pos+2 > d.size()) { err=true; return 0; } quint16 v=le16(d.constData()+pos); pos+=2; return v; }
    QString str()  { quint16 n=u16(); if (err || pos+n > d.size()) { err=true; return {}; }
                     QByteArray s(d.constData()+pos, n); pos+=n;
                     if (s.endsWith('\0')) s.chop(1);
                     return QString::fromUtf8(s); }
    void    skip(int n) { if (pos+n > d.size()) err=true; else pos+=n; }
private:
    QByteArray d; int pos = 0; bool err = false;
};

OrgbDevice parseDevice(const QByteArray& blob, quint32 ver) {
    Reader r(blob);
    OrgbDevice d;
    r.u32();                       // data_size
    d.type = r.i32();
    d.name = r.str();
    if (ver >= 1) r.str();         // vendor
    r.str(); r.str(); r.str(); r.str();   // description, version, serial, location

    quint16 numModes = r.u16();
    d.activeMode = r.i32();
    for (int m = 0; m < numModes && r.ok(); ++m) {
        const int start = r.tell();          // remember where this mode's bytes begin
        OrgbMode mode;
        mode.name = r.str();
        r.i32();                             // value
        r.u32();                             // flags
        r.u32(); r.u32();                    // speed_min/max
        if (ver >= 3) { r.u32(); r.u32(); }  // brightness_min/max
        r.u32(); r.u32();                    // colors_min/max
        r.u32();                             // speed
        if (ver >= 3) r.u32();               // brightness
        r.u32();                             // direction
        r.u32();                             // color_mode
        quint16 nc = r.u16();
        for (int k = 0; k < nc && r.ok(); ++k) r.u32();
        if (r.ok()) mode.raw = blob.mid(start, r.tell() - start);   // exact bytes, for replay
        d.modes.push_back(mode);
    }

    quint16 numZones = r.u16();
    for (int z = 0; z < numZones && r.ok(); ++z) {
        OrgbZone zone;
        zone.name = r.str();
        r.i32();                   // zone type
        zone.ledsMin  = int(r.u32());
        zone.ledsMax  = int(r.u32());
        zone.ledCount = int(r.u32());
        quint16 matrixLen = r.u16();
        if (matrixLen > 0) r.skip(matrixLen);
        d.zones.push_back(zone);
    }

    quint16 numLeds = r.u16();
    for (int l = 0; l < numLeds && r.ok(); ++l) {
        OrgbLed led;
        led.name = r.str();
        r.u32();                   // led 'value' — identifier, not colour
        d.leds.push_back(led);
    }
    quint16 numColors = r.u16();
    for (int k = 0; k < numColors && r.ok(); ++k) {
        quint32 c = r.u32();       // 0x00BBGGRR
        if (k < int(d.leds.size()))
            d.leds[size_t(k)].color = QColor(int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF));
    }
    return d;
}

bool requestDevice(QTcpSocket& s, quint32 idx, quint32 ver, OrgbDevice& out) {
    QByteArray p; if (ver >= 1) put32(p, ver);
    sendPacket(s, idx, CMD_CONTROLLER_DATA, p);
    bool ok; quint32 c; QByteArray blob = recvPacket(s, c, ok);
    if (!ok) return false;
    out = parseDevice(blob, ver);
    return true;
}

// Send SetCustomMode + UpdateLEDs on an already-connected socket. `d` must be
// the freshly-read device (for its LED count). Returns false if 0 LEDs.
bool writeColor(QTcpSocket& s, quint32 idx, const OrgbDevice& d, const QColor& color) {
    const int ledN = int(d.leds.size());
    if (ledN == 0) return false;
    sendPacket(s, idx, CMD_SET_CUSTOM_MODE);
    const quint32 c = quint32(color.red()) | (quint32(color.green()) << 8) | (quint32(color.blue()) << 16);
    QByteArray up;
    put32(up, quint32(4 + 2 + 4 * ledN));
    put16(up, quint16(ledN));
    for (int i = 0; i < ledN; ++i) put32(up, c);
    sendPacket(s, idx, CMD_UPDATE_LEDS, up);
    return true;
}

QTcpSocket* connectHandshake(const QString& host, quint16 port, quint32& ver, QString* error) {
    auto* s = new QTcpSocket();
    s->connectToHost(host, port);
    if (!s->waitForConnected(2000)) {
        if (error) *error = QString("Can't reach OpenRGB at %1:%2 — start it with --server.").arg(host).arg(port);
        delete s; return nullptr;
    }
    ver = handshake(*s);
    return s;
}

} // namespace

std::vector<OrgbDevice> OrgbClient::load(const QString& host, quint16 port, QString* error) {
    std::vector<OrgbDevice> out;
    quint32 ver; QTcpSocket* s = connectHandshake(host, port, ver, error);
    if (!s) return out;

    sendPacket(*s, 0, CMD_CONTROLLER_COUNT);
    bool ok; quint32 c; QByteArray r = recvPacket(*s, c, ok);
    if (!ok || r.size() < 4) { if (error) *error = "OpenRGB did not respond."; delete s; return out; }

    quint32 count = le32(r.constData());
    for (quint32 i = 0; i < count; ++i) {
        OrgbDevice d;
        if (!requestDevice(*s, i, ver, d)) break;
        out.push_back(d);
    }
    delete s;
    return out;
}

bool OrgbClient::setDeviceColor(const QString& host, quint16 port,
                                int index, const QColor& color, QString* error) {
    quint32 ver; QTcpSocket* s = connectHandshake(host, port, ver, error);
    if (!s) return false;
    OrgbDevice d;
    bool ok = requestDevice(*s, quint32(index), ver, d);
    if (!ok) { if (error) *error = "Couldn't read that device."; delete s; return false; }
    if (!writeColor(*s, quint32(index), d, color)) {
        if (error) *error = QString("'%1' has 0 LEDs — resize its zones first.").arg(d.name);
        delete s; return false;
    }
    syncFlush(*s);
    delete s;
    return true;
}

bool OrgbClient::setDeviceMode(const QString& host, quint16 port,
                               int index, int modeIndex, QString* error) {
    quint32 ver; QTcpSocket* s = connectHandshake(host, port, ver, error);
    if (!s) return false;
    OrgbDevice d;
    if (!requestDevice(*s, quint32(index), ver, d)) { if (error) *error = "Couldn't read that device."; delete s; return false; }
    if (modeIndex < 0 || modeIndex >= int(d.modes.size()) || d.modes[size_t(modeIndex)].raw.isEmpty()) {
        if (error) *error = "Invalid mode index."; delete s; return false;
    }
    const QByteArray& raw = d.modes[size_t(modeIndex)].raw;
    QByteArray p;
    put32(p, quint32(4 + 4 + raw.size()));   // data_size
    put32(p, quint32(modeIndex));            // mode index to activate
    p.append(raw);
    sendPacket(*s, quint32(index), CMD_UPDATE_MODE, p);
    syncFlush(*s);
    delete s;
    return true;
}

// Return a copy of a mode's raw bytes with all its colour slots overwritten to
// `color` (0x00BBGGRR). Used to drive cooler devices (Kraken) that ignore
// per-LED writes and only show their active mode's colour.
static QByteArray modeWithColor(const QByteArray& raw, quint32 ver, const QColor& color) {
    QByteArray out = raw;
    const int n = raw.size();
    auto u16at = [&](int p) -> int { return (p + 2 <= n) ? (quint8(raw[p]) | (quint8(raw[p+1]) << 8)) : -1; };
    int pos = 0;
    int nlen = u16at(pos); if (nlen < 0) return out; pos += 2 + nlen;   // name string
    pos += 4;                 // value
    pos += 4;                 // flags
    pos += 8;                 // speed min/max
    if (ver >= 3) pos += 8;   // brightness min/max
    pos += 8;                 // colors min/max
    pos += 4;                 // speed
    if (ver >= 3) pos += 4;   // brightness
    pos += 4;                 // direction
    pos += 4;                 // color_mode
    int nc = u16at(pos); if (nc < 0) return out; pos += 2;             // num_colors
    const quint32 c = quint32(color.red()) | (quint32(color.green()) << 8) | (quint32(color.blue()) << 16);
    for (int k = 0; k < nc && pos + 4 <= out.size(); ++k) {
        out[pos]   = char(c & 0xFF);   out[pos+1] = char((c >> 8) & 0xFF);
        out[pos+2] = char((c >> 16) & 0xFF); out[pos+3] = char((c >> 24) & 0xFF);
        pos += 4;
    }
    return out;
}

// --- OrgbMirror: persistent session for the realtime mirror --------------------
OrgbMirror::~OrgbMirror() { close(); }

void OrgbMirror::close() {
    if (sock_) { delete sock_; sock_ = nullptr; }
    devs_.clear();
}

bool OrgbMirror::open(const QString& host, quint16 port, QString* error) {
    close();
    quint32 ver; QTcpSocket* s = connectHandshake(host, port, ver, error);
    if (!s) return false;
    sendPacket(*s, 0, CMD_CONTROLLER_COUNT);
    bool ok; quint32 c; QByteArray r = recvPacket(*s, c, ok);
    if (!ok || r.size() < 4) { if (error) *error = "OpenRGB did not respond."; delete s; return false; }
    quint32 count = le32(r.constData());
    devs_.clear(); included_.clear();
    for (quint32 i = 0; i < count; ++i) {
        OrgbDevice d;
        if (!requestDevice(*s, i, ver, d)) continue;
        if (d.type == 1) continue;                 // never drive DRAM (RAM scrapped for safety)
        if (d.leds.empty()) continue;              // nothing to light
        int am = d.activeMode;
        QByteArray mraw = (am >= 0 && am < int(d.modes.size())) ? d.modes[size_t(am)].raw : QByteArray();
        if (d.type == 4) {   // cooler (Kraken): it lights only in a single-colour mode — prefer Static
            for (int m = 0; m < int(d.modes.size()); ++m) {
                if (d.modes[size_t(m)].name.compare(QLatin1String("Static"), Qt::CaseInsensitive) == 0
                        && !d.modes[size_t(m)].raw.isEmpty()) {
                    QByteArray p; put32(p, quint32(4 + 4 + d.modes[size_t(m)].raw.size()));
                    put32(p, quint32(m)); p.append(d.modes[size_t(m)].raw);
                    sendPacket(*s, quint32(i), CMD_UPDATE_MODE, p);   // activate Static so the ring shows our colour
                    am = m; mraw = d.modes[size_t(m)].raw;
                    break;
                }
            }
        }
        devs_.push_back(Dev{int(i), int(d.leds.size()), d.type, am, mraw});
        included_.insert(int(i));                  // include every eligible device by default
    }
    ver_ = ver; sock_ = s;
    return true;
}

void OrgbMirror::setIncluded(const QList<int>& deviceIndices) {
    included_.clear();
    for (int i : deviceIndices) included_.insert(i);
}

int OrgbMirror::deviceCount() const {
    int n = 0;
    for (const Dev& d : devs_) if (included_.count(d.idx)) ++n;
    return n;
}

// Drive one device to `color`: coolers (Kraken) via their active mode's colour
// (they ignore per-LED writes); everything else via per-LED UpdateLEDs.
static void driveDevice(QTcpSocket& s, const OrgbMirror::Dev& d, quint32 ver, const QColor& color) {
    if (d.type == 4 && !d.modeRaw.isEmpty()) {
        const QByteArray mr = modeWithColor(d.modeRaw, ver, color);
        QByteArray p; put32(p, quint32(4 + 4 + mr.size())); put32(p, quint32(d.activeMode)); p.append(mr);
        sendPacket(s, quint32(d.idx), CMD_UPDATE_MODE, p);
    } else {
        const quint32 cc = quint32(color.red()) | (quint32(color.green()) << 8) | (quint32(color.blue()) << 16);
        QByteArray up; put32(up, quint32(4 + 2 + 4 * d.ledN)); put16(up, quint16(d.ledN));
        for (int i = 0; i < d.ledN; ++i) put32(up, cc);
        sendPacket(s, quint32(d.idx), CMD_UPDATE_LEDS, up);
    }
}

void OrgbMirror::apply(const QColor& color) {
    if (!sock_ || sock_->state() != QAbstractSocket::ConnectedState) return;
    for (const Dev& d : devs_)
        if (included_.count(d.idx)) driveDevice(*sock_, d, ver_, color);
}

void OrgbMirror::applyBuckets(const QList<QColor>& cols) {
    if (!sock_ || cols.isEmpty() || sock_->state() != QAbstractSocket::ConnectedState) return;
    long r = 0, g = 0, b = 0;
    for (const QColor& c : cols) { r += c.red(); g += c.green(); b += c.blue(); }
    const QColor avg(int(r / cols.size()), int(g / cols.size()), int(b / cols.size()));   // coolers get the average
    for (const Dev& d : devs_) {
        if (!included_.count(d.idx)) continue;
        if (d.type == 4 && !d.modeRaw.isEmpty()) { driveDevice(*sock_, d, ver_, avg); continue; }
        QByteArray up; put32(up, quint32(4 + 2 + 4 * d.ledN)); put16(up, quint16(d.ledN));
        for (int j = 0; j < d.ledN; ++j) {
            const QColor& c = cols[qBound(0, j * cols.size() / qMax(1, d.ledN), cols.size() - 1)];
            put32(up, quint32(c.red()) | (quint32(c.green()) << 8) | (quint32(c.blue()) << 16));
        }
        sendPacket(*sock_, quint32(d.idx), CMD_UPDATE_LEDS, up);
    }
}

int OrgbClient::setAllColor(const QString& host, quint16 port, const QColor& color, QString* error) {
    quint32 ver; QTcpSocket* s = connectHandshake(host, port, ver, error);
    if (!s) return -1;
    sendPacket(*s, 0, CMD_CONTROLLER_COUNT);
    bool ok; quint32 c; QByteArray r = recvPacket(*s, c, ok);
    if (!ok || r.size() < 4) { if (error) *error = "OpenRGB did not respond."; delete s; return -1; }
    quint32 count = le32(r.constData());
    int done = 0;
    for (quint32 i = 0; i < count; ++i) {
        OrgbDevice d;
        if (!requestDevice(*s, i, ver, d)) continue;
        if (writeColor(*s, i, d, color)) ++done;
    }
    syncFlush(*s);
    delete s;
    return done;
}

int OrgbClient::resizeZones(const QString& host, quint16 port, int target, bool onlyZero, QString* error) {
    quint32 ver; QTcpSocket* s = connectHandshake(host, port, ver, error);
    if (!s) return -1;
    sendPacket(*s, 0, CMD_CONTROLLER_COUNT);
    bool ok; quint32 c; QByteArray r = recvPacket(*s, c, ok);
    if (!ok || r.size() < 4) { if (error) *error = "OpenRGB did not respond."; delete s; return -1; }
    quint32 count = le32(r.constData());
    int done = 0;
    for (quint32 i = 0; i < count; ++i) {
        OrgbDevice d;
        if (!requestDevice(*s, i, ver, d)) continue;
        // Only motherboard ARGB headers (type 0). NZXT Hue2 / cooler / peripheral
        // drivers manage their own zones — resizing them APPENDS zones and corrupts
        // the device (observed: Kraken grew to 76 zones / 29200 LEDs).
        if (d.type != 0) continue;
        for (int zi = 0; zi < int(d.zones.size()); ++zi) {
            const OrgbZone& z = d.zones[size_t(zi)];
            if (z.ledsMax <= z.ledsMin) continue;              // not resizable
            if (onlyZero && z.ledCount != 0) continue;
            const int newSize = qBound(z.ledsMin, target, z.ledsMax);
            if (newSize == z.ledCount) continue;
            QByteArray p; put32(p, quint32(zi)); put32(p, quint32(newSize));
            sendPacket(*s, i, CMD_RESIZE_ZONE, p);
            ++done;
        }
    }
    syncFlush(*s);
    delete s;
    return done;
}

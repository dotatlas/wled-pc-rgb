#include "orgb_client.h"
#include <QTcpSocket>
#include <cstring>

// --- OpenRGB SDK protocol -----------------------------------------------------
// Every packet: 16-byte header = "ORGB" + u32 device + u32 command + u32 size,
// then `size` bytes. All integers little-endian. We pin the protocol to v3 so
// the device-description layout is deterministic.
namespace {

// Command IDs we use.
enum : quint32 {
    CMD_CONTROLLER_COUNT = 0,
    CMD_CONTROLLER_DATA  = 1,
    CMD_PROTOCOL_VERSION = 40,
    CMD_SET_CLIENT_NAME  = 50,
    CMD_UPDATE_LEDS      = 1050,
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

// Announce our name + negotiate the protocol version (pin to 3).
quint32 handshake(QTcpSocket& s) {
    { QByteArray n("wled-pc-rgb"); n.append('\0'); sendPacket(s, 0, CMD_SET_CLIENT_NAME, n); }
    QByteArray p; put32(p, 3); sendPacket(s, 0, CMD_PROTOCOL_VERSION, p);
    bool ok; quint32 c; QByteArray r = recvPacket(s, c, ok);
    return (ok && r.size() >= 4) ? qMin(quint32(3), le32(r.constData())) : 0;
}

// Bounds-checked cursor over a device-description blob.
class Reader {
public:
    explicit Reader(const QByteArray& b) : d(b) {}
    bool ok() const { return !err; }
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

// Parse one controller-description blob (protocol v3 layout).
OrgbDevice parseDevice(const QByteArray& blob, quint32 ver) {
    Reader r(blob);
    OrgbDevice d;
    r.u32();                       // data_size
    d.type = r.i32();
    d.name = r.str();
    if (ver >= 1) r.str();         // vendor
    r.str(); r.str(); r.str(); r.str();   // description, version, serial, location

    quint16 numModes = r.u16();
    r.i32();                       // active_mode
    for (int m = 0; m < numModes && r.ok(); ++m) {
        r.str();                   // mode name
        r.i32();                   // value
        r.u32();                   // flags
        r.u32(); r.u32();          // speed_min/max
        if (ver >= 3) { r.u32(); r.u32(); }
        r.u32(); r.u32();          // colors_min/max
        r.u32();                   // speed
        if (ver >= 3) r.u32();     // brightness
        r.u32();                   // direction
        r.u32();                   // color_mode
        quint16 nc = r.u16();
        for (int k = 0; k < nc && r.ok(); ++k) r.u32();
    }

    quint16 numZones = r.u16();
    for (int z = 0; z < numZones && r.ok(); ++z) {
        OrgbZone zone;
        zone.name = r.str();
        r.i32();                   // zone type
        r.u32(); r.u32();          // leds_min/max
        zone.ledCount = int(r.u32());
        quint16 matrixLen = r.u16();
        if (matrixLen > 0) r.skip(matrixLen);
        d.zones.push_back(zone);
    }

    quint16 numLeds = r.u16();
    for (int l = 0; l < numLeds && r.ok(); ++l) {
        OrgbLed led;
        led.name = r.str();
        r.u32();                   // led 'value' — a device identifier, NOT the colour
        d.leds.push_back(led);
    }
    // The actual colours live in a separate parallel array at the very end.
    quint16 numColors = r.u16();
    for (int k = 0; k < numColors && r.ok(); ++k) {
        quint32 c = r.u32();       // 0x00BBGGRR
        if (k < int(d.leds.size()))
            d.leds[size_t(k)].color = QColor(int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF));
    }
    return d;
}

// Fetch just one controller's blob (used by the write path to learn LED count).
bool requestDevice(QTcpSocket& s, quint32 idx, quint32 ver, OrgbDevice& out) {
    QByteArray p; if (ver >= 1) put32(p, ver);
    sendPacket(s, idx, CMD_CONTROLLER_DATA, p);
    bool ok; quint32 c; QByteArray blob = recvPacket(s, c, ok);
    if (!ok) return false;
    out = parseDevice(blob, ver);
    return true;
}

} // namespace

std::vector<OrgbDevice> OrgbClient::load(const QString& host, quint16 port, QString* error) {
    std::vector<OrgbDevice> out;
    QTcpSocket s;
    s.connectToHost(host, port);
    if (!s.waitForConnected(2000)) {
        if (error) *error = QString("Can't reach OpenRGB at %1:%2 — start it with --server.").arg(host).arg(port);
        return out;
    }
    quint32 ver = handshake(s);

    sendPacket(s, 0, CMD_CONTROLLER_COUNT);
    bool ok; quint32 c; QByteArray r = recvPacket(s, c, ok);
    if (!ok || r.size() < 4) { if (error) *error = "OpenRGB did not respond."; return out; }

    quint32 count = le32(r.constData());
    for (quint32 i = 0; i < count; ++i) {
        OrgbDevice d;
        if (!requestDevice(s, i, ver, d)) break;
        out.push_back(d);
    }
    return out;
}

bool OrgbClient::setDeviceColor(const QString& host, quint16 port,
                                int index, const QColor& color, QString* error) {
    QTcpSocket s;
    s.connectToHost(host, port);
    if (!s.waitForConnected(2000)) {
        if (error) *error = "Can't reach OpenRGB — start it with --server.";
        return false;
    }
    quint32 ver = handshake(s);

    OrgbDevice d;
    if (!requestDevice(s, quint32(index), ver, d)) {
        if (error) *error = "Couldn't read that device from OpenRGB.";
        return false;
    }
    const int ledN = int(d.leds.size());
    if (ledN == 0) {
        if (error) *error = QString("'%1' has 0 configured LEDs — needs a zone resize first.").arg(d.name);
        return false;
    }

    sendPacket(s, quint32(index), CMD_SET_CUSTOM_MODE);   // switch to direct/software mode

    const quint32 c = quint32(color.red()) | (quint32(color.green()) << 8) | (quint32(color.blue()) << 16);
    QByteArray up;
    put32(up, quint32(4 + 2 + 4 * ledN));   // data_size
    put16(up, quint16(ledN));                // num LEDs
    for (int i = 0; i < ledN; ++i) put32(up, c);
    sendPacket(s, quint32(index), CMD_UPDATE_LEDS, up);

    // SetCustomMode/UpdateLEDs send no reply, so force the server to process
    // them (TCP is ordered) before we disconnect: issue a request and read it.
    sendPacket(s, 0, CMD_CONTROLLER_COUNT);
    bool syncOk; quint32 syncCmd; recvPacket(s, syncCmd, syncOk);
    return true;
}

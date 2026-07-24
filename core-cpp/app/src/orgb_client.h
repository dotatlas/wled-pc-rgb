// orgb_client — talks to a running OpenRGB SDK server (TCP 6742).
// Reads devices; sets a device's colour; switches a device's active mode;
// sets all devices at once. Hand-written OpenRGB wire protocol.
#pragma once
#include <QString>
#include <QByteArray>
#include <QColor>
#include <vector>
#include <utility>
#include <set>
#include <map>
#include <cstdint>
#include <QList>

class QTcpSocket;

struct OrgbLed  { QString name; QColor color; };
struct OrgbZone { QString name; int ledCount = 0; int ledsMin = 0; int ledsMax = 0; };
struct OrgbMode { QString name; QByteArray raw; };   // raw = the mode's exact bytes, replayed to switch
struct OrgbDevice {
    QString name;
    int type = 0;
    int activeMode = -1;
    std::vector<OrgbMode> modes;
    std::vector<OrgbZone> zones;
    std::vector<OrgbLed>  leds;
};

class OrgbClient {
public:
    static std::vector<OrgbDevice> load(const QString& host, quint16 port, QString* error);

    // Put device #index into direct mode and set all its LEDs to `color`.
    static bool setDeviceColor(const QString& host, quint16 port,
                               int index, const QColor& color, QString* error);

    // Switch device #index to hardware mode #modeIndex (e.g. Direct, Rainbow).
    static bool setDeviceMode(const QString& host, quint16 port,
                              int index, int modeIndex, QString* error);

    // Set every device that has LEDs to `color`. Returns how many were set, or -1 on connect failure.
    static int setAllColor(const QString& host, quint16 port, const QColor& color, QString* error);

    // Resize every resizable zone toward `target` LEDs (clamped to each zone's max).
    // If onlyZero, only touch zones currently at 0 (fixes sizes wiped by an OpenRGB
    // restart). Returns how many zones were resized, or -1 on connect failure.
    static int resizeZones(const QString& host, quint16 port, int target, bool onlyZero, QString* error);
};

// A persistent OpenRGB connection for the live mirror: connect once, cache the
// LED-bearing devices (each put into direct mode), then push a colour to all of
// them every frame with no reconnect — fast enough to reflect WLED in realtime.
class OrgbMirror {
public:
    struct Dev { int idx; int ledN; int type; int activeMode; QByteArray modeRaw; };
    ~OrgbMirror();
    bool open(const QString& host, quint16 port, QString* error);  // caches eligible devices (leds>0, not DRAM)
    void setIncluded(const QList<int>& deviceIndices);             // which detected devices to actually drive
    void setSkip(const QList<QString>& nameSubstrings) { skip_ = nameSubstrings; }  // never cache/drive these (a bespoke pipeline owns them)
    void apply(const QColor& color);
    void applyBuckets(const QList<QColor>& cols);   // stretch the whole strip across EACH device's LEDs
    void applyWrapped(const QList<QColor>& cols);    // distribute the strip ONCE across all devices in sequence
    void close();
    bool isOpen() const { return sock_ != nullptr; }
    bool alive() const;                              // socket connected?
    int  deviceCount() const;                        // number currently included
private:
    // Per-device write gate. Two OpenRGB device classes are SLOW: coolers (a whole
    // mode-update packet per frame) and GPUs (I2C/SMBus register writes). Pushing one every
    // frame at 60 FPS backs the socket up and the device lags by seconds/minutes (the GPU
    // was choppy for exactly this reason). The gate: (a) skip when the payload is unchanged —
    // for EVERY device, so a steady colour never re-sends; (b) rate-cap the slow devices
    // (GPU/cooler) to ~30/15 FPS; (c) back off when the OpenRGB socket is congested. Fast
    // devices (mouse, motherboard) still send every changed frame.
    bool deviceDue(int idx, int type, quint32 payloadHash);

    QTcpSocket* sock_ = nullptr;
    quint32 ver_ = 0;
    std::vector<Dev> devs_;                  // eligible devices (leds>0, not DRAM)
    std::set<int> included_;                 // device indices actually driven
    std::map<int, quint32> lastHash_;        // device idx -> last payload hash sent (skip-unchanged)
    std::map<int, qint64>  lastAt_;          // slow device idx -> last send time (ms since epoch)
    QList<QString> skip_;                    // device-name substrings a bespoke pipeline owns
};

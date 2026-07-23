// orgb_client — talks to a running OpenRGB SDK server (TCP 6742).
// Reads devices (v0.3) and writes a colour to a device (v0.4). This is our
// hand-written half: OpenRGB has no C++ client library we want to link, so we
// speak its wire protocol ourselves.
#pragma once
#include <QString>
#include <QColor>
#include <vector>
#include <cstdint>

struct OrgbLed  { QString name; QColor color; };
struct OrgbZone { QString name; int ledCount = 0; };
struct OrgbDevice {
    QString name;
    int type = 0;
    std::vector<OrgbZone> zones;
    std::vector<OrgbLed>  leds;
};

class OrgbClient {
public:
    // Connect, load every controller, return them. On failure returns empty
    // and fills *error.
    static std::vector<OrgbDevice> load(const QString& host, quint16 port, QString* error);

    // Put device #index into direct mode and set all its LEDs to `color`.
    // Returns false (with *error) on failure or if the device has 0 LEDs.
    static bool setDeviceColor(const QString& host, quint16 port,
                               int index, const QColor& color, QString* error);
};

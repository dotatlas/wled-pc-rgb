// orgb_client — talks to a running OpenRGB SDK server (TCP 6742) and returns
// the detected devices. This is our hand-written half: OpenRGB has no C++
// client library we want to link, so we speak its wire protocol ourselves.
#pragma once
#include <QString>
#include <QColor>
#include <vector>
#include <cstdint>

// A plain data model of what OpenRGB reports. (Later this grows into the
// Controller/Zone/LED model the whole app shares.)
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
    // and fills *error with a human-readable reason.
    static std::vector<OrgbDevice> load(const QString& host, quint16 port, QString* error);
};

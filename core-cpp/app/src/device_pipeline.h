// device_pipeline — the seam for per-device output drivers.
//
// The mirror decides a colour device-agnostically ("be this colour"). Most devices are
// driven the generic way, through OpenRGB (see OrgbMirror). Some devices need their own
// communication — e.g. the NZXT Kraken ring over raw HID, because OpenRGB's path is too
// slow. Those implement this interface and are driven alongside the generic path.
//
// To add a bespoke device: subclass DevicePipeline, implement the communication in its own
// file, and register an instance with MainWindow. `match()` names the OpenRGB device this
// pipeline replaces, so the generic path leaves that device alone (no double-driving).
#pragma once
#include <QColor>
#include <QString>

class DevicePipeline {
public:
    virtual ~DevicePipeline() = default;

    virtual QString name()  const = 0;   // human-readable label
    virtual QString match() const = 0;   // OpenRGB device-name substring this pipeline owns (lowercase)

    virtual bool open() = 0;             // acquire the device; true if present
    virtual bool isOpen() const = 0;
    virtual void apply(const QColor& color) = 0;   // drive the device to this colour
    virtual void close() = 0;
};

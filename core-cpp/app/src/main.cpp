// wled-pc-rgb — v1.7: the WLED mirror.
// The window (MainWindow) owns the tray, the setup strip, the device list and all
// persisted settings. main() keeps the headless CLI helpers (including the Kraken
// ring pipeline verifiers), the single-instance guard, and start-minimised behaviour.

// Single source of truth for the version — set by CMake (project VERSION), with a fallback
// so the file still builds outside CMake. Shown in the About dialog and the window title.
#ifndef APP_VERSION
#define APP_VERSION "1.7"
#endif

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QColor>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QTextStream>

#include "main_window.h"
#include "orgb_client.h"
#include "ipc_client.h"
#include "kraken_driver.h"
#include "gpu_driver.h"
#include "colour_ops.h"

namespace {
constexpr auto kInstanceKey = "wled-pc-rgb.singleton";

// The app is a GUI-subsystem exe, so stdout is not visible. The GPU verifiers write their
// report here instead: %TEMP%\wled-pc-rgb-gpu.txt
void gpuLog(const QString& text) {
    QFile f(QDir::tempPath() + "/wled-pc-rgb-gpu.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) { QTextStream(&f) << text; }
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // NB: keep applicationName/organizationName as "wled-pc-rgb" — they locate the saved settings,
    // so changing them would silently discard every existing user's configuration. The friendly
    // display name "WLED PC RGB" is used only in the window title and the About dialog.
    app.setApplicationName("wled-pc-rgb");
    app.setApplicationVersion(APP_VERSION);
    // NB: do NOT setApplicationDisplayName — Qt would append it to every explicit window title,
    // duplicating "WLED PC RGB". The titles set the friendly name themselves.
    app.setOrganizationName("wled-pc-rgb");
    app.setWindowIcon(MainWindow::appIcon());
    app.setQuitOnLastWindowClosed(false);   // closing the window hides to tray

    // --- headless helpers for scripting/verification --------------------------
    const QStringList args = app.arguments();
    auto scaledArg = [](QColor c, const QStringList& a, int at) {
        if (at < a.size()) { int p = a[at].toInt(); return QColor(c.red()*p/100, c.green()*p/100, c.blue()*p/100); }
        return c;
    };
    if (int i = args.indexOf("--set"); i > 0 && i + 2 < args.size()) {         // --set <dev> <#rrggbb> [pct]
        QString e; return OrgbClient::setDeviceColor("127.0.0.1", 6742, args[i+1].toInt(),
                                                     scaledArg(QColor(args[i+2]), args, i+3), &e) ? 0 : 2;
    }
    if (int i = args.indexOf("--setmode"); i > 0 && i + 2 < args.size()) {     // --setmode <dev> <modeIndex>
        QString e; return OrgbClient::setDeviceMode("127.0.0.1", 6742, args[i+1].toInt(), args[i+2].toInt(), &e) ? 0 : 2;
    }
    if (int i = args.indexOf("--setall"); i > 0 && i + 1 < args.size()) {      // --setall <#rrggbb> [pct]
        QString e; return OrgbClient::setAllColor("127.0.0.1", 6742,
                                                  scaledArg(QColor(args[i+1]), args, i+2), &e) >= 0 ? 0 : 2;
    }
    if (args.indexOf("--maxzones") > 0) {                                      // resize all resizable zones to the default
        QString e; return OrgbClient::resizeZones("127.0.0.1", 6742, 24, false, &e) >= 0 ? 0 : 2;
    }
    // --- Kraken ring HID pipeline (SignalRGB 0x26 protocol) verification ------
    if (int i = args.indexOf("--kraken"); i > 0 && i + 1 < args.size()) {       // --kraken <#rrggbb>  (solid ring)
        KrakenDriver k;
        if (!k.open()) { return 3; }
        k.setRingColor(QColor(args[i+1]));
        return 0;
    }
    if (int i = args.indexOf("--krakencycle"); i > 0) {                        // --krakencycle [secs]  (live hue sweep)
        const int secs = (i + 1 < args.size()) ? args[i+1].toInt() : 8;
        KrakenDriver k;
        if (!k.open()) { return 3; }
        auto* t = new QTimer(&app);
        int hue = 0;
        QObject::connect(t, &QTimer::timeout, &app, [&k, &hue]{
            k.setRingColor(QColor::fromHsv(hue % 360, 255, 255)); hue = (hue + 3) % 360;
        });
        t->start(33);   // ~30 FPS — the rate SignalRGB targets
        QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (int i = args.indexOf("--krakenwalk"); i > 0) {                         // --krakenwalk [ms/step] (count the ring's real LEDs)
        const int stepMs = (i + 1 < args.size()) ? qMax(120, args[i+1].toInt()) : 700;
        KrakenDriver k;
        if (!k.open()) { return 3; }
        const int slotCount = KrakenDriver::ringLeds();     // NB: 'slots' is a Qt macro keyword
        auto* t = new QTimer(&app);
        int slot = 0;
        QObject::connect(t, &QTimer::timeout, &app, [&k, &slot, slotCount]{
            if (slot >= slotCount) { QCoreApplication::quit(); return; }
            k.lightOneSlot(slot, QColor(255, 255, 255));    // one slot at a time, white
            ++slot;
        });
        t->start(stepMs);
        const int rc = app.exec();
        k.setRingColor(QColor(0, 0, 0));
        return rc;
    }
    if (int i = args.indexOf("--krakenspin"); i > 0) {                         // --krakenspin [secs] (rotating rainbow — per-LED test)
        const int secs = (i + 1 < args.size()) ? args[i+1].toInt() : 8;
        KrakenDriver k;
        if (!k.open()) { return 3; }
        auto* t = new QTimer(&app);
        int phase = 0;
        QObject::connect(t, &QTimer::timeout, &app, [&k, &phase]{
            QList<QColor> ring; ring.reserve(24);
            for (int j = 0; j < 24; ++j) ring.push_back(QColor::fromHsv((j * 15 + phase) % 360, 255, 255));
            k.setRing(ring); phase = (phase + 6) % 360;
        });
        t->start(33);
        QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    // --- GPU (MSI Blackwell over NVAPI I2C) verification ladder ---------------
    // Run in order: --gpuinfo (read-only) → --gpunoop (one benign write) → --gpu (first frame)
    // → --gpucycle / --gpuspin. Reports go to %TEMP%\wled-pc-rgb-gpu.txt.
    // --gpunoreg switches to the no-register framing, if 31 bytes + a register address is rejected.
    {
        const bool noReg = args.contains("--gpunoreg");
        if (args.indexOf("--gpuinfo") > 0) {                                   // READ-ONLY: no writes at all
            GpuDriver g; QString out;
            const bool ok = g.probeInfo(&out);
            gpuLog(out);
            return ok ? 0 : 3;
        }
        if (args.indexOf("--gpunoop") > 0) {                                   // the single benign write 0x20 = 0x00
            GpuDriver g; g.setNoRegisterFraming(noReg); QString e;
            const bool ok = g.noopWrite(&e);
            gpuLog(ok ? QString("no-op write OK (reg 0x20 = 0x00%1)\n").arg(noReg ? ", no-register framing" : "")
                      : "no-op write FAILED: " + e + "\n");
            return ok ? 0 : 3;
        }
        if (int i = args.indexOf("--gpu"); i > 0 && i + 1 < args.size()) {      // --gpu <#rrggbb>  (init + one frame)
            GpuDriver g; g.setNoRegisterFraming(noReg); QString e;
            if (!g.open(&e)) { gpuLog("open FAILED: " + e + "\n"); return 3; }
            g.setColor(QColor(args[i + 1]));
            gpuLog(QString("open OK + one frame sent (%1%2)\n").arg(args[i + 1]).arg(noReg ? ", no-register framing" : ""));
            return g.isOpen() ? 0 : 4;      // 4 = the frame write failed and closed the handle
        }
        if (int i = args.indexOf("--gpucycle"); i > 0) {                        // --gpucycle [secs] (hue sweep)
            const int secs = (i + 1 < args.size()) ? args[i + 1].toInt() : 8;
            auto* g = new GpuDriver; g->setNoRegisterFraming(noReg); QString e;
            if (!g->open(&e)) { gpuLog("open FAILED: " + e + "\n"); return 3; }
            auto* t = new QTimer(&app);
            int hue = 0;
            QObject::connect(t, &QTimer::timeout, &app, [g, &hue]{
                g->setColor(QColor::fromHsv(hue % 360, 255, 255)); hue = (hue + 3) % 360;
            });
            t->start(33);
            QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
            const int rc = app.exec();
            gpuLog(g->isOpen() ? "cycle finished, handle still open (writes kept succeeding)\n"
                               : "cycle ABORTED — a frame write failed\n");
            return rc;
        }
        if (int i = args.indexOf("--gpuspin"); i > 0) {                         // --gpuspin [secs] (per-LED test)
            const int secs = (i + 1 < args.size()) ? args[i + 1].toInt() : 8;
            auto* g = new GpuDriver; g->setNoRegisterFraming(noReg); QString e;
            if (!g->open(&e)) { gpuLog("open FAILED: " + e + "\n"); return 3; }
            auto* t = new QTimer(&app);
            int phase = 0;
            QObject::connect(t, &QTimer::timeout, &app, [g, &phase]{
                QList<QColor> strip; strip.reserve(GpuDriver::kLeds);
                for (int j = 0; j < GpuDriver::kLeds; ++j)
                    strip.push_back(QColor::fromHsv((j * 60 + phase) % 360, 255, 255));
                g->setLeds(strip); phase = (phase + 6) % 360;
            });
            t->start(33);
            QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
            const int rc = app.exec();
            gpuLog(g->isOpen() ? "spin finished, handle still open (writes kept succeeding)\n"
                               : "spin ABORTED — a frame write failed\n");
            return rc;
        }
    }

    if (int i = args.indexOf("--mirror"); i > 0) {                             // --mirror [seconds] [spread|wrap]
        const int secs = (i + 1 < args.size()) ? args[i+1].toInt() : 5;
        const bool spread = args.contains("spread");
        const bool wrap   = args.contains("wrap");
        OrgbMirror mir; QString e;
        if (!mir.open("127.0.0.1", 6742, &e)) return 2;
        IpcClient ipc;
        // --mirror <secs> [spread|wrap] [nobg]  — nobg subtracts the background colour calibrated in
        // the GUI (QSettings key mirror/bgCal), so the headless path matches what the app shows.
        QColor cal;
        if (args.contains("nobg")) cal = QColor(QSettings().value("mirror/bgCal").toString());
        QObject::connect(&ipc, &IpcClient::frame, &app,
                         [&mir, spread, wrap, cal](const QColor& c, const QList<QColor>& cols){
            QColor avg = c; QList<QColor> use = cols;
            if (cal.isValid() && !cols.isEmpty()) avg = colour::stripBgAll(cols, cal, &use);
            if (wrap) mir.applyWrapped(use); else if (spread) mir.applyBuckets(use); else mir.apply(avg);
        });
        ipc.start(47900);
        QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
        const int rc = app.exec();
        mir.close();
        return rc;
    }

    // --- single-instance guard: a second launch just shows the running one ----
    {
        QLocalSocket ping;
        ping.connectToServer(kInstanceKey);
        if (ping.waitForConnected(200)) {
            ping.write("show"); ping.flush(); ping.waitForBytesWritten(200);
            return 0;
        }
    }
    QLocalServer::removeServer(kInstanceKey);
    QLocalServer instanceServer;
    instanceServer.listen(kInstanceKey);

    MainWindow window;
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&] {
        if (QLocalSocket* c = instanceServer.nextPendingConnection()) c->deleteLater();
        window.showAndRaise();
    });

    // Start minimised to tray if asked (flag or persisted option); else show.
    const bool startMin = args.contains("--minimized") || QSettings().value("opts/startMin", false).toBool();
    if (!startMin) window.showAndRaise();

    return app.exec();
}

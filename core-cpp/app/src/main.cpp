// wled-pc-rgb — v1.3.1: the WLED mirror.
// The window (MainWindow) owns the tray, the setup strip, the device list and all
// persisted settings. main() keeps the headless CLI helpers, the single-instance
// guard, and the start-minimised behaviour.

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QColor>
#include <QTimer>

#include "main_window.h"
#include "orgb_client.h"
#include "ipc_client.h"
#include "kraken_driver.h"

namespace {
constexpr auto kInstanceKey = "wled-pc-rgb.singleton";
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("wled-pc-rgb");
    app.setApplicationVersion("1.3.1");
    app.setOrganizationName("wled-pc-rgb");
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
    if (int i = args.indexOf("--kraken"); i > 0 && i + 1 < args.size()) {      // --kraken <#rrggbb> : test the ring
        KrakenDriver k;
        if (!k.open()) return 2;                                               // no Kraken Elite found
        k.setRingColor(QColor(args[i+1]));
        return 0;
    }
    if (int i = args.indexOf("--mirror"); i > 0) {                             // --mirror [seconds] [spread|wrap]
        const int secs = (i + 1 < args.size()) ? args[i+1].toInt() : 5;
        const bool spread = args.contains("spread");
        const bool wrap   = args.contains("wrap");
        OrgbMirror mir; QString e;
        if (!mir.open("127.0.0.1", 6742, &e)) return 2;
        IpcClient ipc;
        QObject::connect(&ipc, &IpcClient::frame, &app, [&mir, spread, wrap](const QColor& c, const QList<QColor>& cols){
            if (wrap) mir.applyWrapped(cols); else if (spread) mir.applyBuckets(cols); else mir.apply(c);
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

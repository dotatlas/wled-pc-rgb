// wled-pc-rgb — v0.3: device inspector.
// Same shell as v0.2 (tray + single-instance), but the window is now a
// MainWindow that connects to OpenRGB and shows a live device tree.

#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QColor>

#include "main_window.h"
#include "orgb_client.h"

namespace {
constexpr auto kInstanceKey = "wled-pc-rgb.singleton";
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("wled-pc-rgb");
    app.setApplicationVersion("0.5");
    app.setOrganizationName("wled-pc-rgb");
    app.setQuitOnLastWindowClosed(false);

    // Headless helper:  --set <deviceIndex> <#rrggbb> [brightnessPercent]
    const QStringList args = app.arguments();
    const int si = args.indexOf("--set");
    if (si > 0 && si + 2 < args.size()) {
        QColor col(args[si + 2]);
        if (si + 3 < args.size()) {                       // optional brightness %
            const int pct = args[si + 3].toInt();
            col = QColor(col.red() * pct / 100, col.green() * pct / 100, col.blue() * pct / 100);
        }
        QString err;
        const bool ok = OrgbClient::setDeviceColor("127.0.0.1", 6742, args[si + 1].toInt(), col, &err);
        return ok ? 0 : 2;
    }

    // --- single-instance guard (unchanged from v0.2) ---
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
    auto raise = [&] { window.show(); window.raise(); window.activateWindow(); };

    // --- system tray ---
    QSystemTrayIcon tray;
    tray.setIcon(app.style()->standardIcon(QStyle::SP_ComputerIcon));
    tray.setToolTip("wled-pc-rgb");

    QMenu trayMenu;
    QObject::connect(trayMenu.addAction("Open"),            &QAction::triggered, raise);
    QObject::connect(trayMenu.addAction("Rescan devices"),  &QAction::triggered, &window, &MainWindow::refresh);
    QObject::connect(trayMenu.addAction("Quit"),            &QAction::triggered, qApp, &QApplication::quit);
    tray.setContextMenu(&trayMenu);
    tray.show();

    QObject::connect(&tray, &QSystemTrayIcon::activated,
        [&](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) raise();
        });

    QObject::connect(&instanceServer, &QLocalServer::newConnection, [&] {
        if (QLocalSocket* c = instanceServer.nextPendingConnection()) c->deleteLater();
        raise();
    });

    raise();
    return app.exec();
}

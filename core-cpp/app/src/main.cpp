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

#include "main_window.h"

namespace {
constexpr auto kInstanceKey = "wled-pc-rgb.singleton";
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("wled-pc-rgb");
    app.setApplicationVersion("0.3");
    app.setOrganizationName("wled-pc-rgb");
    app.setQuitOnLastWindowClosed(false);

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

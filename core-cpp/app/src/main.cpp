// wled-pc-rgb — v0.2: Qt application shell + system tray + single instance
// -----------------------------------------------------------------------------
// The real app begins here. v0.2 is deliberately minimal but a complete,
// working build: it launches, shows a placeholder window, lives in the system
// tray (closing the window keeps it running), and is single-instance — a second
// launch just refocuses the first.
//
// Next (v0.3): connect to OpenRGB's SDK and populate the window with a live
// device tree, replacing the placeholder label.
// -----------------------------------------------------------------------------

#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
constexpr auto kInstanceKey = "wled-pc-rgb.singleton";
constexpr auto kVersion     = "0.2";
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("wled-pc-rgb");
    app.setApplicationVersion(kVersion);
    app.setOrganizationName("wled-pc-rgb");
    app.setQuitOnLastWindowClosed(false); // closing the window keeps us in the tray

    // --- single-instance guard -------------------------------------------------
    // If we can connect to an existing server, another instance owns it: ping it
    // (so it can raise its window) and exit.
    {
        QLocalSocket ping;
        ping.connectToServer(kInstanceKey);
        if (ping.waitForConnected(200)) {
            ping.write("show");
            ping.flush();
            ping.waitForBytesWritten(200);
            return 0;
        }
    }
    QLocalServer::removeServer(kInstanceKey); // clear any stale socket
    QLocalServer instanceServer;
    instanceServer.listen(kInstanceKey);

    // --- main window (placeholder inspector) -----------------------------------
    QMainWindow window;
    window.setWindowTitle("wled-pc-rgb");
    auto* label = new QLabel(
        QString("wled-pc-rgb  v%1\n\n"
                "Device inspector arrives in v0.3.\n"
                "Running in the system tray — closing this window keeps it alive.")
            .arg(kVersion));
    label->setAlignment(Qt::AlignCenter);
    label->setMargin(28);
    window.setCentralWidget(label);
    window.resize(460, 260);

    auto raise = [&window] {
        window.show();
        window.raise();
        window.activateWindow();
    };

    // --- system tray -----------------------------------------------------------
    QSystemTrayIcon tray;
    tray.setIcon(app.style()->standardIcon(QStyle::SP_ComputerIcon));
    tray.setToolTip("wled-pc-rgb");

    QMenu trayMenu;
    QObject::connect(trayMenu.addAction("Open"),  &QAction::triggered, raise);
    QObject::connect(trayMenu.addAction("Quit"),  &QAction::triggered, qApp, &QApplication::quit);
    tray.setContextMenu(&trayMenu);
    tray.show();

    QObject::connect(&tray, &QSystemTrayIcon::activated,
        [&](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
                raise();
        });

    // A second launch connects to our server → surface the window.
    QObject::connect(&instanceServer, &QLocalServer::newConnection, [&] {
        if (QLocalSocket* c = instanceServer.nextPendingConnection())
            c->deleteLater();
        raise();
    });

    raise(); // show on first launch
    return app.exec();
}

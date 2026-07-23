#include "main_window.h"
#include "orgb_client.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QPixmap>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QTextStream>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("wled-pc-rgb");
    resize(560, 480);

    auto* central = new QWidget(this);          // Qt owns these via the parent/child tree
    auto* layout  = new QVBoxLayout(central);

    status_ = new QLabel("Connecting to OpenRGB…", central);
    tree_   = new QTreeWidget(central);
    tree_->setHeaderLabels({"Device / Zone / LED", "Info"});
    tree_->setColumnWidth(0, 340);
    auto* rescan = new QPushButton("Rescan", central);

    layout->addWidget(status_);
    layout->addWidget(tree_, 1);
    layout->addWidget(rescan);
    setCentralWidget(central);

    connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);   // button click -> our slot
    refresh();                                   // scan once on open
}

// A tiny solid-colour icon so each LED shows its current colour.
static QIcon swatch(const QColor& c) {
    QPixmap pm(14, 14);
    pm.fill(c);
    return QIcon(pm);
}

void MainWindow::refresh() {
    tree_->clear();

    QString err;
    auto devices = OrgbClient::load("127.0.0.1", 6742, &err);
    if (!err.isEmpty()) {
        status_->setText("⚠  " + err);
        setWindowTitle("wled-pc-rgb — no connection");
        return;
    }

    int zoneTotal = 0, ledTotal = 0;
    for (const auto& d : devices) {
        auto* dItem = new QTreeWidgetItem(tree_, {d.name, "device"});
        for (const auto& z : d.zones) {
            new QTreeWidgetItem(dItem, {z.name, QString("%1 LEDs").arg(z.ledCount)});
            ++zoneTotal;
        }
        auto* ledsNode = new QTreeWidgetItem(dItem, {QString("LEDs (%1)").arg(int(d.leds.size())), ""});
        for (const auto& l : d.leds) {
            auto* li = new QTreeWidgetItem(ledsNode, {l.name, l.color.name()});
            li->setIcon(1, swatch(l.color));
            ++ledTotal;
        }
        dItem->setExpanded(true);
    }

    status_->setText(QString("Connected — %1 devices · %2 zones · %3 LEDs")
                         .arg(devices.size()).arg(zoneTotal).arg(ledTotal));
    setWindowTitle(QString("wled-pc-rgb — %1 devices").arg(devices.size()));

    // Dev aid: dump the scan so the parse can be verified outside the GUI.
    QFile f(QDir::tempPath() + "/wled-pc-rgb-scan.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << "devices=" << devices.size() << " zones=" << zoneTotal << " leds=" << ledTotal << "\n";
        for (const auto& d : devices) {
            out << "DEVICE: " << d.name << "  (type " << d.type << ", "
                << d.zones.size() << " zones, " << d.leds.size() << " leds)\n";
            for (const auto& z : d.zones)
                out << "   ZONE: " << z.name << "  (" << z.ledCount << " leds)\n";
        }
    }
}

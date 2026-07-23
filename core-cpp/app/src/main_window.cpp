#include "main_window.h"
#include "orgb_client.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPixmap>
#include <QIcon>
#include <QColorDialog>
#include <QVariant>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace { constexpr int kDeviceIndexRole = Qt::UserRole + 1; }

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("wled-pc-rgb");
    resize(560, 480);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    status_ = new QLabel("Connecting to OpenRGB…", central);
    tree_   = new QTreeWidget(central);
    tree_->setHeaderLabels({"Device / Zone / LED", "Info"});
    tree_->setColumnWidth(0, 340);

    auto* buttons = new QHBoxLayout;
    auto* rescan  = new QPushButton("Rescan", central);
    auto* setCol  = new QPushButton("Set colour…", central);
    buttons->addWidget(rescan);
    buttons->addWidget(setCol);
    buttons->addStretch(1);

    layout->addWidget(status_);
    layout->addWidget(tree_, 1);
    layout->addLayout(buttons);
    setCentralWidget(central);

    connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(setCol, &QPushButton::clicked, this, &MainWindow::setSelectedColor);
    refresh();
}

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
    for (int di = 0; di < int(devices.size()); ++di) {
        const auto& d = devices[di];
        auto* dItem = new QTreeWidgetItem(tree_, {d.name, "device"});
        dItem->setData(0, kDeviceIndexRole, di);      // remember which device this row is
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

    status_->setText(QString("Connected — %1 devices · %2 zones · %3 LEDs   (select a device, then Set colour…)")
                         .arg(devices.size()).arg(zoneTotal).arg(ledTotal));
    setWindowTitle(QString("wled-pc-rgb — %1 devices").arg(devices.size()));

    // Dev aid: dump the scan (incl. first LED colour) so the parse/write can be verified.
    QFile f(QDir::tempPath() + "/wled-pc-rgb-scan.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << "devices=" << devices.size() << " zones=" << zoneTotal << " leds=" << ledTotal << "\n";
        for (const auto& d : devices) {
            out << "DEVICE: " << d.name << "  (type " << d.type << ", "
                << d.zones.size() << " zones, " << d.leds.size() << " leds, first="
                << (d.leds.empty() ? QString("-") : d.leds.front().color.name()) << ")\n";
            for (const auto& z : d.zones)
                out << "   ZONE: " << z.name << "  (" << z.ledCount << " leds)\n";
        }
    }
}

void MainWindow::setSelectedColor() {
    QTreeWidgetItem* item = tree_->currentItem();
    if (!item) { status_->setText("Select a device row first."); return; }
    while (item->parent()) item = item->parent();          // climb to the device row
    const QVariant v = item->data(0, kDeviceIndexRole);
    if (!v.isValid()) { status_->setText("Select a device row (top level)."); return; }

    const QColor col = QColorDialog::getColor(Qt::red, this, "Pick a colour");
    if (!col.isValid()) return;                            // user cancelled

    QString err;
    if (OrgbClient::setDeviceColor("127.0.0.1", 6742, v.toInt(), col, &err)) {
        status_->setText(QString("Set '%1' → %2").arg(item->text(0), col.name()));
        refresh();
    } else {
        status_->setText("⚠  " + err);
    }
}

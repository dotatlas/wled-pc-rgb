#include "main_window.h"
#include "orgb_client.h"
#include "sysinfo.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
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

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
QColor scaled(const QColor& c, int pct) {   // software brightness
    return QColor(c.red() * pct / 100, c.green() * pct / 100, c.blue() * pct / 100);
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("wled-pc-rgb");
    resize(600, 520);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    mobo_ = new QLabel(central);
    mobo_->setText("Motherboard: " + (sysinfo::motherboard().isEmpty() ? QString("(unknown)")
                                                                        : sysinfo::motherboard()));
    mobo_->setStyleSheet("font-weight:600;");

    status_ = new QLabel("Connecting to OpenRGB…", central);
    tree_   = new QTreeWidget(central);
    tree_->setHeaderLabels({"Device / Zone / Mode / LED", "Info"});
    tree_->setColumnWidth(0, 360);

    // brightness row
    auto* bRow  = new QHBoxLayout;
    auto* bLbl  = new QLabel("Brightness:", central);
    bright_     = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100);
    bright_->setValue(100);
    auto* bVal  = new QLabel("100%", central);
    connect(bright_, &QSlider::valueChanged, bVal, [bVal](int v){ bVal->setText(QString::number(v) + "%"); });
    bRow->addWidget(bLbl);
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    // button row
    auto* buttons = new QHBoxLayout;
    auto* rescan  = new QPushButton("Rescan", central);
    auto* setCol  = new QPushButton("Set colour…", central);
    buttons->addWidget(rescan);
    buttons->addWidget(setCol);
    buttons->addStretch(1);

    layout->addWidget(mobo_);
    layout->addWidget(status_);
    layout->addWidget(tree_, 1);
    layout->addLayout(bRow);
    layout->addLayout(buttons);
    setCentralWidget(central);

    connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(setCol, &QPushButton::clicked, this, &MainWindow::setSelectedColor);
    refresh();
}

static QIcon swatch(const QColor& c) { QPixmap pm(14, 14); pm.fill(c); return QIcon(pm); }

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
        dItem->setData(0, kDeviceIndexRole, di);

        // zones
        for (const auto& z : d.zones) {
            new QTreeWidgetItem(dItem, {z.name, QString("%1 LEDs").arg(z.ledCount)});
            ++zoneTotal;
        }
        // modes (read-only), active one marked
        const QString active = (d.activeMode >= 0 && d.activeMode < int(d.modes.size()))
                                   ? d.modes[size_t(d.activeMode)] : QString("?");
        auto* modesNode = new QTreeWidgetItem(dItem, {QString("Modes (%1)").arg(d.modes.size()),
                                                      "active: " + active});
        for (int mi = 0; mi < int(d.modes.size()); ++mi)
            new QTreeWidgetItem(modesNode, {d.modes[size_t(mi)], mi == d.activeMode ? "● active" : ""});
        // LEDs with colour swatches
        auto* ledsNode = new QTreeWidgetItem(dItem, {QString("LEDs (%1)").arg(int(d.leds.size())), ""});
        for (const auto& l : d.leds) {
            auto* li = new QTreeWidgetItem(ledsNode, {l.name, l.color.name()});
            li->setIcon(1, swatch(l.color));
            ++ledTotal;
        }
        dItem->setExpanded(true);
    }

    status_->setText(QString("Connected — %1 devices · %2 zones · %3 LEDs   (pick a device, then Set colour…)")
                         .arg(devices.size()).arg(zoneTotal).arg(ledTotal));
    setWindowTitle(QString("wled-pc-rgb — %1 devices").arg(devices.size()));

    // Dev aid: dump the scan for out-of-GUI verification.
    QFile f(QDir::tempPath() + "/wled-pc-rgb-scan.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << "MOTHERBOARD: " << sysinfo::motherboard() << "\n";
        out << "devices=" << devices.size() << " zones=" << zoneTotal << " leds=" << ledTotal << "\n";
        for (const auto& d : devices) {
            out << "DEVICE: " << d.name << "  (type " << d.type << ", " << d.modes.size() << " modes, "
                << d.zones.size() << " zones, " << d.leds.size() << " leds, first="
                << (d.leds.empty() ? QString("-") : d.leds.front().color.name()) << ")\n";
        }
    }
}

void MainWindow::setSelectedColor() {
    QTreeWidgetItem* item = tree_->currentItem();
    if (!item) { status_->setText("Select a device row first."); return; }
    while (item->parent()) item = item->parent();
    const QVariant v = item->data(0, kDeviceIndexRole);
    if (!v.isValid()) { status_->setText("Select a device row (top level)."); return; }

    const QColor picked = QColorDialog::getColor(Qt::red, this, "Pick a colour");
    if (!picked.isValid()) return;

    const QColor applied = scaled(picked, bright_->value());
    QString err;
    if (OrgbClient::setDeviceColor("127.0.0.1", 6742, v.toInt(), applied, &err)) {
        status_->setText(QString("Set '%1' → %2 @ %3%%").arg(item->text(0), picked.name()).arg(bright_->value()));
        refresh();
    } else {
        status_->setText("⚠  " + err);
    }
}

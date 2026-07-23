#include "main_window.h"
#include "orgb_client.h"
#include "sysinfo.h"
#include "ipc_client.h"

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
#include <QCheckBox>
#include <QVariant>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
constexpr int kModeIndexRole   = Qt::UserRole + 2;
constexpr auto kHost = "127.0.0.1";
constexpr quint16 kPort = 6742;
constexpr int kDefaultZoneLeds = 24;    // resize target per zone (user's JARGB/Kraken = 24 each);
                                        // clamped to each zone's max. NOT max — max over-allocates
                                        // (960/1720 LEDs here) which bloats updates / can overload devices.
QColor scale(const QColor& c, int pct) { return QColor(c.red()*pct/100, c.green()*pct/100, c.blue()*pct/100); }
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("wled-pc-rgb");
    resize(620, 560);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    mobo_ = new QLabel("Motherboard: " + (sysinfo::motherboard().isEmpty() ? QString("(unknown)")
                                                                           : sysinfo::motherboard()), central);
    mobo_->setStyleSheet("font-weight:600;");

    status_ = new QLabel("Connecting to OpenRGB…", central);
    wled_ = new QLabel("WLED backend: connecting…", central);
    wled_->setStyleSheet("color:#2a8;");

    auto* tip = new QLabel("Tip: colours apply in a per-LED mode. If a device doesn't change, "
                           "select one of its modes and click \"Set mode\" (e.g. the Kraken ring "
                           "needs Static, not Direct).", central);
    tip->setWordWrap(true);
    tip->setStyleSheet("color: gray; font-size: 11px;");

    tree_ = new QTreeWidget(central);
    tree_->setHeaderLabels({"Device / Zone / Mode / LED", "Info"});
    tree_->setColumnWidth(0, 380);

    auto* bRow = new QHBoxLayout;
    bright_ = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100); bright_->setValue(100);
    auto* bVal = new QLabel("100%", central);
    connect(bright_, &QSlider::valueChanged, bVal, [bVal](int v){ bVal->setText(QString::number(v) + "%"); });
    bRow->addWidget(new QLabel("Brightness:", central));
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    auto* btn = new QHBoxLayout;
    auto* rescan = new QPushButton("Rescan", central);
    auto* setCol = new QPushButton("Set colour…", central);
    auto* setMod = new QPushButton("Set mode", central);
    auto* setAll = new QPushButton("Set ALL…", central);
    auto* setRoom = new QPushButton("Set room…", central);
    auto* mirBtn  = new QPushButton("Mirror room", central);
    mirBtn->setCheckable(true);
    auto* spreadChk = new QCheckBox("Spread", central);
    auto* maxZ = new QPushButton("Size zones (24)", central);
    btn->addWidget(rescan); btn->addWidget(setCol); btn->addWidget(setMod); btn->addWidget(setAll);
    btn->addWidget(setRoom); btn->addWidget(maxZ); btn->addWidget(mirBtn); btn->addWidget(spreadChk);
    btn->addStretch(1);

    layout->addWidget(mobo_);
    layout->addWidget(status_);
    layout->addWidget(wled_);
    layout->addWidget(tip);
    layout->addWidget(tree_, 1);
    layout->addLayout(bRow);
    layout->addLayout(btn);
    setCentralWidget(central);

    connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(setCol, &QPushButton::clicked, this, &MainWindow::setSelectedColor);
    connect(setMod, &QPushButton::clicked, this, &MainWindow::setSelectedMode);
    connect(setAll, &QPushButton::clicked, this, &MainWindow::setAllColor);
    connect(setRoom, &QPushButton::clicked, this, &MainWindow::setRoomColor);
    connect(maxZ, &QPushButton::clicked, this, &MainWindow::maxZones);
    refresh();

    // Java WLED backend (Phase 3): show its status + the room's live colour.
    ipc_ = new IpcClient(this);
    connect(ipc_, &IpcClient::connectionChanged, this, [this](bool c) {
        if (!c) wled_->setText("WLED backend: not connected (run WledBackend.java)");
    });
    connect(ipc_, &IpcClient::hello, this, [this](const QString& n, int leds, bool ok) {
        wled_->setText(QString("WLED backend: %1 · %2 · %3 LEDs")
                           .arg(ok ? "reachable" : "UNREACHABLE", n).arg(leds));
    });
    connect(spreadChk, &QCheckBox::toggled, this, [this](bool on) { spread_ = on; });
    connect(ipc_, &IpcClient::frame, this, [this](const QColor& avg, const QList<QColor>& cols) {
        room_ = avg;
        setWindowTitle(baseTitle_ + " · room " + avg.name());
        if (mirroring_) { if (spread_) mirror_.applyBuckets(cols); else mirror_.apply(avg); }
    });
    connect(mirBtn, &QPushButton::toggled, this, [this, mirBtn](bool on) {
        if (on) {
            QString e;
            if (mirror_.open(kHost, kPort, &e)) {
                mirroring_ = true;
                status_->setText(QString("Mirroring room onto %1 device(s)…").arg(mirror_.deviceCount()));
            } else { mirroring_ = false; mirBtn->setChecked(false); status_->setText("⚠  " + e); }
        } else { mirroring_ = false; mirror_.close(); status_->setText("Mirror off."); }
    });
    ipc_->start(47900);
}

static QIcon swatch(const QColor& c) { QPixmap pm(14, 14); pm.fill(c); return QIcon(pm); }

void MainWindow::refresh() {
    tree_->clear();
    OrgbClient::resizeZones(kHost, kPort, kDefaultZoneLeds, /*onlyZero*/true, nullptr);  // auto-fix wiped zones
    QString err;
    auto devices = OrgbClient::load(kHost, kPort, &err);
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

        for (const auto& z : d.zones) {
            new QTreeWidgetItem(dItem, {z.name, QString("%1 LEDs").arg(z.ledCount)});
            ++zoneTotal;
        }
        const QString active = (d.activeMode >= 0 && d.activeMode < int(d.modes.size()))
                                   ? d.modes[size_t(d.activeMode)].name : QString("?");
        auto* modesNode = new QTreeWidgetItem(dItem, {QString("Modes (%1)").arg(d.modes.size()), "active: " + active});
        for (int mi = 0; mi < int(d.modes.size()); ++mi) {
            auto* mItem = new QTreeWidgetItem(modesNode, {d.modes[size_t(mi)].name, mi == d.activeMode ? "● active" : ""});
            mItem->setData(0, kDeviceIndexRole, di);
            mItem->setData(0, kModeIndexRole, mi);
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
    baseTitle_ = QString("wled-pc-rgb — %1 devices").arg(devices.size());
    setWindowTitle(baseTitle_);

    QFile f(QDir::tempPath() + "/wled-pc-rgb-scan.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << "MOTHERBOARD: " << sysinfo::motherboard() << "\n";
        out << "devices=" << devices.size() << " zones=" << zoneTotal << " leds=" << ledTotal << "\n";
        for (const auto& d : devices) {
            out << "DEVICE: " << d.name << "  (active=" << d.activeMode << ", "
                << d.leds.size() << " leds, first=" << (d.leds.empty() ? QString("-") : d.leds.front().color.name()) << ")\n";
            for (int zi = 0; zi < int(d.zones.size()); ++zi) {
                const auto& z = d.zones[size_t(zi)];
                out << "   ZONE " << zi << ": " << z.name << "  min=" << z.ledsMin
                    << " max=" << z.ledsMax << " count=" << z.ledCount << "\n";
            }
            QStringList mn; for (int i = 0; i < int(d.modes.size()); ++i) mn << QString("%1:%2").arg(i).arg(d.modes[size_t(i)].name);
            out << "   modes: " << mn.join("  ") << "\n";
        }
    }
}

QColor MainWindow::pickColour() {
    const QColor picked = QColorDialog::getColor(Qt::red, this, "Pick a colour");
    if (!picked.isValid()) return QColor();
    return scale(picked, bright_->value());
}

void MainWindow::setSelectedColor() {
    QTreeWidgetItem* item = tree_->currentItem();
    if (!item) { status_->setText("Select a device first."); return; }
    while (item->parent()) item = item->parent();
    const QVariant v = item->data(0, kDeviceIndexRole);
    if (!v.isValid()) { status_->setText("Select a device row (top level)."); return; }
    const QColor col = pickColour();
    if (!col.isValid()) return;
    QString err;
    if (OrgbClient::setDeviceColor(kHost, kPort, v.toInt(), col, &err)) {
        status_->setText(QString("Set '%1' → %2. (No change? Switch its mode with Set mode.)")
                             .arg(item->text(0), col.name()));
        refresh();
    } else status_->setText("⚠  " + err);
}

void MainWindow::setSelectedMode() {
    QTreeWidgetItem* item = tree_->currentItem();
    const QVariant dev = item ? item->data(0, kDeviceIndexRole) : QVariant();
    const QVariant mode = item ? item->data(0, kModeIndexRole) : QVariant();
    if (!item || !mode.isValid()) { status_->setText("Select a mode row (under a device's Modes)."); return; }
    QString err;
    if (OrgbClient::setDeviceMode(kHost, kPort, dev.toInt(), mode.toInt(), &err)) {
        status_->setText(QString("Activated mode '%1'.").arg(item->text(0)));
        refresh();
    } else status_->setText("⚠  " + err);
}

void MainWindow::setAllColor() {
    const QColor col = pickColour();
    if (!col.isValid()) return;
    QString err;
    const int n = OrgbClient::setAllColor(kHost, kPort, col, &err);
    if (n >= 0) { status_->setText(QString("Set %1 device(s) → %2.").arg(n).arg(col.name())); refresh(); }
    else status_->setText("⚠  " + err);
}

void MainWindow::setRoomColor() {
    const QColor col = pickColour();
    if (!col.isValid()) return;
    ipc_->sendWledColor(col, true);       // app -> backend -> WLED /json/state
    status_->setText(QString("Sent room colour %1 to WLED.").arg(col.name()));
}

void MainWindow::maxZones() {
    QString err;
    const int n = OrgbClient::resizeZones(kHost, kPort, kDefaultZoneLeds, /*onlyZero*/false, &err);
    if (n >= 0) { status_->setText(QString("Sized %1 zone(s) to %2 LEDs — rescanning…").arg(n).arg(kDefaultZoneLeds)); refresh(); }
    else status_->setText("⚠  " + err);
}

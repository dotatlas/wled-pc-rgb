#include "main_window.h"
#include "orgb_client.h"
#include "sysinfo.h"
#include "ipc_client.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPixmap>
#include <QIcon>
#include <QColorDialog>
#include <QVariant>
#include <QColor>
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QTcpSocket>
#include <QDir>
#include <QTextStream>

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
constexpr int kModeIndexRole   = Qt::UserRole + 2;
constexpr auto kHost = "127.0.0.1";
constexpr quint16 kPort = 6742;
constexpr quint16 kIpcPort = 47900;
constexpr int kDefaultZoneLeds = 24;    // resize target per motherboard ARGB zone (clamped to its max)

QColor scale(const QColor& c, int pct) { return QColor(c.red()*pct/100, c.green()*pct/100, c.blue()*pct/100); }

QString findJava() {
    const QString jh = qEnvironmentVariable("JAVA_HOME");
    if (!jh.isEmpty() && QFile::exists(jh + "/bin/java.exe")) return jh + "/bin/java.exe";
    const QString scoop = "C:/.software/scoop/apps/temurin21-jdk/current/bin/java.exe";
    if (QFile::exists(scoop)) return scoop;
    return "java";   // rely on PATH
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("wled-pc-rgb");
    resize(640, 560);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    mobo_ = new QLabel("Motherboard: " + (sysinfo::motherboard().isEmpty() ? QString("(unknown)")
                                                                           : sysinfo::motherboard()), central);
    mobo_->setStyleSheet("font-weight:600;");
    status_ = new QLabel("Connecting to OpenRGB…", central);
    wled_   = new QLabel("WLED: connecting…", central);
    wled_->setStyleSheet("color:#2a8;");

    auto* tip = new QLabel("Tip: colours apply in a per-LED mode. If a device doesn't change, select one of its "
                           "modes and click \"Set mode\" (e.g. the Kraken ring needs Static, not Direct).", central);
    tip->setWordWrap(true);
    tip->setStyleSheet("color: gray; font-size: 11px;");

    tree_ = new QTreeWidget(central);
    tree_->setHeaderLabels({"Device / Zone / Mode / LED", "Info"});
    tree_->setColumnWidth(0, 380);

    // PC-only brightness scaler (does NOT affect WLED)
    auto* bRow = new QHBoxLayout;
    bright_ = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100); bright_->setValue(100);
    auto* bVal = new QLabel("100%", central);
    connect(bright_, &QSlider::valueChanged, bVal, [bVal](int v){ bVal->setText(QString::number(v) + "%"); });
    bRow->addWidget(new QLabel("PC brightness:", central));
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    auto* btn = new QHBoxLayout;
    auto* rescan  = new QPushButton("Rescan", central);
    auto* setCol  = new QPushButton("Set colour…", central);
    auto* setMod  = new QPushButton("Set mode", central);
    auto* setAll  = new QPushButton("Set ALL…", central);
    auto* setWled = new QPushButton("Set WLED…", central);
    auto* maxZ    = new QPushButton("Size zones (24)", central);
    auto* mirBtn  = new QPushButton("Mirror WLED", central);
    mirBtn->setCheckable(true);
    auto* spreadChk = new QCheckBox("Spread", central);
    btn->addWidget(rescan); btn->addWidget(setCol); btn->addWidget(setMod); btn->addWidget(setAll);
    btn->addWidget(setWled); btn->addWidget(maxZ); btn->addWidget(mirBtn); btn->addWidget(spreadChk);
    btn->addStretch(1);

    layout->addWidget(mobo_);
    layout->addWidget(status_);
    layout->addWidget(wled_);
    layout->addWidget(tip);
    layout->addWidget(tree_, 1);
    layout->addLayout(bRow);
    layout->addLayout(btn);
    setCentralWidget(central);

    connect(rescan,  &QPushButton::clicked, this, &MainWindow::refresh);
    connect(setCol,  &QPushButton::clicked, this, &MainWindow::setSelectedColor);
    connect(setMod,  &QPushButton::clicked, this, &MainWindow::setSelectedMode);
    connect(setAll,  &QPushButton::clicked, this, &MainWindow::setAllColor);
    connect(setWled, &QPushButton::clicked, this, &MainWindow::setWledColor);
    connect(maxZ,    &QPushButton::clicked, this, &MainWindow::maxZones);
    connect(spreadChk, &QCheckBox::toggled, this, [this](bool on){ spread_ = on; });
    refresh();

    startBackend();   // launch + supervise the Java WLED backend

    // Java WLED backend: status + live mirror.
    ipc_ = new IpcClient(this);
    connect(ipc_, &IpcClient::connectionChanged, this, [this](bool c) {
        if (!c) wled_->setText("WLED: backend starting / reconnecting…");
    });
    connect(ipc_, &IpcClient::hello, this, [this](const QString& n, int leds, bool ok) {
        wled_->setText(QString("WLED: %1 · %2 · %3 LEDs").arg(ok ? "reachable" : "UNREACHABLE", n).arg(leds));
    });
    connect(ipc_, &IpcClient::frame, this, [this](const QColor& avg, const QList<QColor>& cols) {
        wledColour_ = avg;
        setWindowTitle(baseTitle_ + " · WLED " + avg.name());
        if (mirroring_) {                            // reflect WLED onto PC devices, scaled by PC brightness
            const int b = bright_->value();
            if (spread_) {
                QList<QColor> sc; sc.reserve(cols.size());
                for (const QColor& c : cols) sc.push_back(scale(c, b));
                mirror_.applyBuckets(sc);
            } else {
                mirror_.apply(scale(avg, b));
            }
        }
    });
    connect(mirBtn, &QPushButton::toggled, this, [this, mirBtn](bool on) {
        if (on) {
            QString e;
            if (mirror_.open(kHost, kPort, &e)) {
                mirroring_ = true;
                status_->setText(QString("Mirroring WLED onto %1 device(s)…").arg(mirror_.deviceCount()));
            } else { mirroring_ = false; mirBtn->setChecked(false); status_->setText("⚠  " + e); }
        } else { mirroring_ = false; mirror_.close(); status_->setText("Mirror off."); }
    });
    ipc_->start(kIpcPort);
}

void MainWindow::startBackend() {
    // If a backend is already listening (e.g. started manually), just use it.
    { QTcpSocket probe; probe.connectToHost(kHost, kIpcPort);
      if (probe.waitForConnected(300)) { probe.abort(); return; } }

    const QString java   = findJava();
    const QString script = QCoreApplication::applicationDirPath() + "/WledBackend.java";
    if (!QFile::exists(script)) return;              // fall back to a manually-run backend

    backend_ = new QProcess(this);
    backend_->setProgram(java);
    backend_->setArguments({script, "wled.local", QString::number(kIpcPort)});
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]{ stopping_ = true; if (backend_) backend_->kill(); });
    connect(backend_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                if (!stopping_) QTimer::singleShot(1500, this, [this]{ if (!stopping_ && backend_) backend_->start(); });
            });
    backend_->start();
}

static QIcon swatch(const QColor& c) { QPixmap pm(14, 14); pm.fill(c); return QIcon(pm); }

void MainWindow::refresh() {
    tree_->clear();
    OrgbClient::resizeZones(kHost, kPort, kDefaultZoneLeds, /*onlyZero*/true, nullptr);  // auto-fix wiped mobo zones

    QString err;
    auto devices = OrgbClient::load(kHost, kPort, &err);
    if (!err.isEmpty()) {
        status_->setText("⚠  " + err);
        baseTitle_ = "wled-pc-rgb — no OpenRGB";
        setWindowTitle(baseTitle_);
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
}

QColor MainWindow::pickColour() {
    return QColorDialog::getColor(Qt::red, this, "Pick a colour");   // raw; PC brightness applied per use
}

void MainWindow::setSelectedColor() {
    QTreeWidgetItem* item = tree_->currentItem();
    if (!item) { status_->setText("Select a device first."); return; }
    while (item->parent()) item = item->parent();
    const QVariant v = item->data(0, kDeviceIndexRole);
    if (!v.isValid()) { status_->setText("Select a device row (top level)."); return; }
    const QColor raw = pickColour();
    if (!raw.isValid()) return;
    const QColor col = scale(raw, bright_->value());
    QString err;
    if (OrgbClient::setDeviceColor(kHost, kPort, v.toInt(), col, &err)) {
        status_->setText(QString("Set '%1' → %2. (No change? Switch its mode with Set mode.)").arg(item->text(0), col.name()));
        refresh();
    } else status_->setText("⚠  " + err);
}

void MainWindow::setSelectedMode() {
    QTreeWidgetItem* item = tree_->currentItem();
    const QVariant dev  = item ? item->data(0, kDeviceIndexRole) : QVariant();
    const QVariant mode = item ? item->data(0, kModeIndexRole)   : QVariant();
    if (!item || !mode.isValid()) { status_->setText("Select a mode row (under a device's Modes)."); return; }
    QString err;
    if (OrgbClient::setDeviceMode(kHost, kPort, dev.toInt(), mode.toInt(), &err)) {
        status_->setText(QString("Activated mode '%1'.").arg(item->text(0)));
        refresh();
    } else status_->setText("⚠  " + err);
}

void MainWindow::setAllColor() {
    const QColor raw = pickColour();
    if (!raw.isValid()) return;
    const QColor col = scale(raw, bright_->value());
    QString err;
    const int n = OrgbClient::setAllColor(kHost, kPort, col, &err);
    if (n >= 0) { status_->setText(QString("Set %1 device(s) → %2.").arg(n).arg(col.name())); refresh(); }
    else status_->setText("⚠  " + err);
}

void MainWindow::setWledColor() {
    const QColor col = pickColour();            // raw hex; WLED keeps its own brightness
    if (!col.isValid()) return;
    ipc_->sendWledColor(col);
    status_->setText(QString("Sent %1 to WLED (its brightness left unchanged).").arg(col.name()));
}

void MainWindow::maxZones() {
    QString err;
    const int n = OrgbClient::resizeZones(kHost, kPort, kDefaultZoneLeds, /*onlyZero*/false, &err);
    if (n >= 0) { status_->setText(QString("Sized %1 motherboard zone(s) to %2 LEDs — rescanning…").arg(n).arg(kDefaultZoneLeds)); refresh(); }
    else status_->setText("⚠  " + err);
}

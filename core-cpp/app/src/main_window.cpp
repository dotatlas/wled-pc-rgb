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
#include <QVariant>
#include <QColor>
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QTcpSocket>
#include <QDir>
#include <string>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
constexpr int kModeIndexRole   = Qt::UserRole + 2;
constexpr auto kHost = "127.0.0.1";
constexpr quint16 kPort = 6742;
constexpr quint16 kIpcPort = 47900;
constexpr int kDefaultZoneLeds = 24;

QColor scale(const QColor& c, int pct) { return QColor(c.red()*pct/100, c.green()*pct/100, c.blue()*pct/100); }

QString findJava() {
    const QString jh = qEnvironmentVariable("JAVA_HOME");
    if (!jh.isEmpty() && QFile::exists(jh + "/bin/java.exe")) return jh + "/bin/java.exe";
    const QString scoop = "C:/.software/scoop/apps/temurin21-jdk/current/bin/java.exe";
    if (QFile::exists(scoop)) return scoop;
    return "java";
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
    wled_   = new QLabel("WLED: connecting…", central);
    wled_->setStyleSheet("color:#2a8; font-weight:600;");
    status_ = new QLabel("Tick the devices to mirror, then click \"Mirror WLED\".", central);

    auto* tip = new QLabel("Each ticked device follows WLED live while mirroring is on. A device only lights in a "
                           "per-LED mode — most default to Direct; the Kraken ring needs Static (select its mode "
                           "row → \"Set mode\"). GPU RGB needs OpenRGB run as administrator.", central);
    tip->setWordWrap(true);
    tip->setStyleSheet("color: gray; font-size: 11px;");

    tree_ = new QTreeWidget(central);
    tree_->setHeaderLabels({"Mirror?  Device / Zone / Mode / LED", "Info"});
    tree_->setColumnWidth(0, 380);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (!building_ && col == 0 && it->data(0, kDeviceIndexRole).isValid() && mirroring_) pushIncluded();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        if (it && it->data(0, kModeIndexRole).isValid()) activateMode(it);   // double-click a mode row to select it
    });

    auto* bRow = new QHBoxLayout;
    bright_ = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100); bright_->setValue(100);
    auto* bVal = new QLabel("100%", central);
    connect(bright_, &QSlider::valueChanged, bVal, [bVal](int v){ bVal->setText(QString::number(v) + "%"); });
    bRow->addWidget(new QLabel("PC brightness:", central));
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    auto* btn = new QHBoxLayout;
    auto* rescan = new QPushButton("Rescan", central);
    auto* maxZ   = new QPushButton("Size zones (24)", central);
    auto* setMod = new QPushButton("Set mode", central);
    auto* mirBtn = new QPushButton("Mirror WLED", central);
    mirBtn->setCheckable(true);
    auto* spreadChk = new QCheckBox("Spread", central);
    btn->addWidget(rescan); btn->addWidget(maxZ); btn->addWidget(setMod);
    btn->addWidget(mirBtn); btn->addWidget(spreadChk);
    btn->addStretch(1);

    layout->addWidget(mobo_);
    layout->addWidget(wled_);
    layout->addWidget(status_);
    layout->addWidget(tip);
    layout->addWidget(tree_, 1);
    layout->addLayout(bRow);
    layout->addLayout(btn);
    setCentralWidget(central);

    connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(maxZ,   &QPushButton::clicked, this, &MainWindow::maxZones);
    connect(setMod, &QPushButton::clicked, this, &MainWindow::setSelectedMode);
    connect(spreadChk, &QCheckBox::toggled, this, [this](bool on){ spread_ = on; });
    startOpenRGB();     // launch OpenRGB elevated + SDK server if it isn't already up
    refresh();

    startBackend();

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
        if (mirroring_) {
            const int b = bright_->value();          // PC-only brightness
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
                pushIncluded();
                status_->setText(QString("Mirroring WLED onto %1 ticked device(s)…").arg(mirror_.deviceCount()));
            } else { mirroring_ = false; mirBtn->setChecked(false); status_->setText("⚠  " + e); }
        } else { mirroring_ = false; mirror_.close(); status_->setText("Mirror off."); }
    });
    ipc_->start(kIpcPort);
}

void MainWindow::startBackend() {
    { QTcpSocket probe; probe.connectToHost(kHost, kIpcPort);
      if (probe.waitForConnected(300)) { probe.abort(); return; } }
    const QString java   = findJava();
    const QString script = QCoreApplication::applicationDirPath() + "/WledBackend.java";
    if (!QFile::exists(script)) return;
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

void MainWindow::startOpenRGB() {
#ifdef _WIN32
    { QTcpSocket probe; probe.connectToHost(kHost, kPort);
      if (probe.waitForConnected(300)) { probe.abort(); return; } }   // already running
    const QStringList candidates = {
        QStringLiteral("C:/.software/OpenRGB/OpenRGB Windows 64-bit/OpenRGB.exe"),
        qEnvironmentVariable("ProgramFiles") + "/OpenRGB/OpenRGB.exe",
        qEnvironmentVariable("LOCALAPPDATA") + "/OpenRGB/OpenRGB.exe",
    };
    QString exe;
    for (const QString& c : candidates) if (QFile::exists(c)) { exe = c; break; }
    if (exe.isEmpty()) { status_->setText("OpenRGB not found — launch it manually (SDK server; as admin for GPU RGB)."); return; }
    const std::wstring wexe  = QDir::toNativeSeparators(exe).toStdWString();
    const std::wstring wargs = L"--server --noautoconnect";
    HINSTANCE h = ShellExecuteW(nullptr, L"runas", wexe.c_str(), wargs.c_str(), nullptr, SW_SHOWMINIMIZED);  // elevated (UAC)
    status_->setText((INT_PTR)h > 32 ? "Starting OpenRGB as administrator — approve the UAC prompt…"
                                     : "Couldn't launch OpenRGB elevated — launch it manually.");
#endif
}

QList<int> MainWindow::gatherChecked() {
    QList<int> out;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        const QVariant v = it->data(0, kDeviceIndexRole);
        if (v.isValid() && (it->flags() & Qt::ItemIsUserCheckable) && it->checkState(0) == Qt::Checked)
            out.push_back(v.toInt());
    }
    return out;
}

void MainWindow::pushIncluded() {
    mirror_.setIncluded(gatherChecked());
    if (mirroring_) status_->setText(QString("Mirroring WLED onto %1 device(s).").arg(mirror_.deviceCount()));
}

static QIcon swatch(const QColor& c) { QPixmap pm(14, 14); pm.fill(c); return QIcon(pm); }

void MainWindow::refresh() {
    building_ = true;
    tree_->clear();
    OrgbClient::resizeZones(kHost, kPort, kDefaultZoneLeds, /*onlyZero*/true, nullptr);

    QString err;
    auto devices = OrgbClient::load(kHost, kPort, &err);
    if (!err.isEmpty()) {
        status_->setText("⚠  " + err + "   (retrying…)");
        baseTitle_ = "wled-pc-rgb — connecting to OpenRGB…";
        setWindowTitle(baseTitle_);
        building_ = false;
        QTimer::singleShot(2000, this, &MainWindow::refresh);   // keep trying until OpenRGB is up
        return;
    }

    int zoneTotal = 0, ledTotal = 0, mirrorable = 0;
    for (int di = 0; di < int(devices.size()); ++di) {
        const auto& d = devices[di];
        const bool isDram    = (d.type == 1);
        const bool canMirror = (!isDram && !d.leds.empty());

        auto* dItem = new QTreeWidgetItem(tree_, {d.name + (isDram ? "   (RAM — excluded for safety)" : QString()), "device"});
        dItem->setData(0, kDeviceIndexRole, di);
        if (canMirror) {
            dItem->setFlags(dItem->flags() | Qt::ItemIsUserCheckable);
            dItem->setCheckState(0, Qt::Checked);    // mirror all eligible by default
            ++mirrorable;
        }
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
        dItem->setExpanded(false);
    }
    status_->setText(QString("%1 devices (%2 mirror-able) · %3 zones · %4 LEDs — tick devices, then Mirror WLED")
                         .arg(devices.size()).arg(mirrorable).arg(zoneTotal).arg(ledTotal));
    baseTitle_ = QString("wled-pc-rgb — %1 devices").arg(devices.size());
    setWindowTitle(baseTitle_);
    building_ = false;
    if (mirroring_) pushIncluded();
}

void MainWindow::setSelectedMode() { activateMode(tree_->currentItem()); }

void MainWindow::activateMode(QTreeWidgetItem* item) {
    const QVariant dev  = item ? item->data(0, kDeviceIndexRole) : QVariant();
    const QVariant mode = item ? item->data(0, kModeIndexRole)   : QVariant();
    if (!item || !mode.isValid()) { status_->setText("Select or double-click a mode row (under a device's Modes)."); return; }
    QString err;
    if (!OrgbClient::setDeviceMode(kHost, kPort, dev.toInt(), mode.toInt(), &err)) { status_->setText("⚠  " + err); return; }
    // Update the active marker in place — no tree rebuild, so the device stays expanded.
    if (QTreeWidgetItem* modesNode = item->parent()) {
        for (int i = 0; i < modesNode->childCount(); ++i)
            modesNode->child(i)->setText(1, modesNode->child(i) == item ? "● active" : QString());
        modesNode->setText(1, "active: " + item->text(0));
    }
    status_->setText(QString("Activated mode '%1'.").arg(item->text(0)));
    if (mirroring_) { QString e; mirror_.open(kHost, kPort, &e); pushIncluded(); }  // refresh mirror's cached mode
}

void MainWindow::maxZones() {
    QString err;
    const int n = OrgbClient::resizeZones(kHost, kPort, kDefaultZoneLeds, /*onlyZero*/false, &err);
    if (n >= 0) { status_->setText(QString("Sized %1 motherboard zone(s) to %2 LEDs — rescanning…").arg(n).arg(kDefaultZoneLeds)); refresh(); }
    else status_->setText("⚠  " + err);
}
